#include "parser.h"

#include "lexer.h"

typedef struct {
    VM* vm;
    Token* toks;
    int count;
    int pos;
    const char* file;
    jmp_buf jb;
    int depth; /* nesting inside (), [] and {} literals: newlines do not end expressions there */
    int block_depth; /* 0 = top level of the file */
} Parser;

/* ---------------------------------------------------------------- helpers */

static const char* tok_desc(Token* t) {
    static char buf[128];
    int len = t->len > 40 ? 40 : t->len;
    switch (t->kind) {
        case T_EOF: return "end of file";
        case T_STRING: return "string literal";
        case T_INT:
        case T_FLOAT: snprintf(buf, sizeof buf, "number '%.*s'", len, t->start); return buf;
        case T_IDENT: snprintf(buf, sizeof buf, "identifier '%.*s'", len, t->start); return buf;
        case T_COMPOUND: snprintf(buf, sizeof buf, "'%.*s='", len, t->start); return buf;
        default: snprintf(buf, sizeof buf, "'%.*s'", len, t->start); return buf;
    }
}

static PP_NORETURN void perr(Parser* p, Token* t, const char* fmt, ...) {
    fflush(stdout);
    fprintf(stderr, "%s:%d:%d: syntax error: ", p->file, t->line, t->col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    longjmp(p->jb, 1);
}

static Token* peek(Parser* p) {
    return &p->toks[p->pos];
}

static Token* peek_at(Parser* p, int n) {
    int i = p->pos + n;
    if (i >= p->count) i = p->count - 1;
    return &p->toks[i];
}

static Token* advance(Parser* p) {
    Token* t = &p->toks[p->pos];
    if (t->kind != T_EOF) p->pos++;
    return t;
}

static bool check(Parser* p, TokKind k) {
    return peek(p)->kind == k;
}

static bool accept(Parser* p, TokKind k) {
    if (!check(p, k)) return false;
    advance(p);
    return true;
}

static Token* expect(Parser* p, TokKind k, const char* what) {
    if (!check(p, k)) perr(p, peek(p), "expected %s but found %s", what, tok_desc(peek(p)));
    return advance(p);
}

static bool tok_is(Token* t, TokKind k, const char* s) {
    int n = (int)strlen(s);
    return t->kind == k && t->len == n && memcmp(t->start, s, (size_t)n) == 0;
}

static ObjString* intern_tok(Parser* p, Token* t) {
    return str_intern(p->vm, t->start, t->len);
}

static bool is_terminator(Token* t) {
    return t->kind == T_SEMI || t->kind == T_RBRACE || t->kind == T_EOF || t->nl_before;
}

/* consumes a prefix of an operator token, e.g. one '>' out of '>>' */
static bool eat_op_prefix(Parser* p, const char* s) {
    Token* t = peek(p);
    int n = (int)strlen(s);
    if (t->kind != T_OP || t->len < n || memcmp(t->start, s, (size_t)n) != 0) return false;
    if (t->len == n) {
        advance(p);
        return true;
    }
    t->start += n;
    t->len -= n;
    t->col += n;
    t->space_before = false;
    t->nl_before = false;
    return true;
}

static Node* new_node(NodeKind k, int line) {
    Node* n = pp_calloc(1, sizeof(Node));
    n->kind = k;
    n->line = line;
    return n;
}

static void nl_push(NodeList* l, Node* n) {
    GROW(l->items, l->count, l->cap);
    l->items[l->count++] = n;
}

static InlineCache* new_ic(Parser* p) {
    InlineCache* ic = pp_calloc(1, sizeof(InlineCache));
    vm_register_ic(p->vm, ic);
    return ic;
}

static Node* new_call(Parser* p, Node* callee, int line) {
    Node* c = new_node(N_CALL, line);
    c->as.call.callee = callee;
    c->as.call.ic = new_ic(p);
    return c;
}

static Type* new_type(TypeKind k, int line) {
    Type* t = pp_calloc(1, sizeof(Type));
    t->kind = k;
    t->line = line;
    return t;
}

static bool prim_from_name(const char* s, int len, Prim* out) {
    static const struct {
        const char* name;
        Prim prim;
    } prims[] = {
        {"int", PRIM_INT},   {"float", PRIM_FLOAT},   {"string", PRIM_STRING}, {"bool", PRIM_BOOL},
        {"void", PRIM_VOID}, {"range", PRIM_RANGE},   {"object", PRIM_OBJECT}, {"any", PRIM_ANY},
        {"type", PRIM_TYPE}, {NULL, PRIM_ANY},
    };
    for (int i = 0; prims[i].name; i++) {
        if ((int)strlen(prims[i].name) == len && memcmp(prims[i].name, s, (size_t)len) == 0) {
            *out = prims[i].prim;
            return true;
        }
    }
    return false;
}

static Node* parse_expression(Parser* p);
static Node* parse_statement(Parser* p);
static Node* parse_block(Parser* p);
static Node* parse_unary(Parser* p);
static Type* parse_type(Parser* p);

/* ------------------------------------------------------------------ types */

static Type* parse_type_primary(Parser* p) {
    Token* t = peek(p);
    if (t->kind == T_IDENT) {
        advance(p);
        Prim pr;
        if (prim_from_name(t->start, t->len, &pr)) {
            Type* ty = new_type(TY_PRIM, t->line);
            ty->prim = pr;
            return ty;
        }
        Type* ty = new_type(TY_NAME, t->line);
        ty->name = intern_tok(p, t);
        if (check(p, T_COLON) && !peek(p)->nl_before) {
            /* module:Type */
            advance(p);
            ty->qual = ty->name;
            ty->name = intern_tok(p, expect(p, T_IDENT, "type name after ':'"));
        }
        Token* n = peek(p);
        if (n->kind == T_OP && !n->nl_before && n->start[0] == '<') {
            eat_op_prefix(p, "<");
            int cap = 0;
            do {
                Type* a = parse_type(p);
                GROW(ty->args, ty->nargs, cap);
                ty->args[ty->nargs++] = a;
            } while (accept(p, T_COMMA));
            if (!eat_op_prefix(p, ">")) perr(p, peek(p), "expected '>' to close the type arguments of '%s'", ty->name->chars);
        }
        return ty;
    }
    if (t->kind == T_FUNCTION) {
        advance(p);
        Type* ty = new_type(TY_PRIM, t->line);
        ty->prim = PRIM_FUNCTION;
        return ty;
    }
    if (t->kind == T_LBRACE) {
        advance(p);
        Type* ty = new_type(TY_RECORD, t->line);
        int cap = 0, cap2 = 0, n2 = 0;
        while (!check(p, T_RBRACE)) {
            if (accept(p, T_COMMA) || accept(p, T_SEMI)) continue;
            Type* ft = parse_type(p);
            Token* name = expect(p, T_IDENT, "field name");
            GROW(ty->fnames, ty->nfields, cap);
            GROW(ty->ftypes, n2, cap2);
            ty->fnames[ty->nfields++] = intern_tok(p, name);
            ty->ftypes[n2++] = ft;
        }
        expect(p, T_RBRACE, "'}'");
        return ty;
    }
    if (t->kind == T_LPAREN) {
        advance(p);
        Type* ty = parse_type(p);
        expect(p, T_RPAREN, "')'");
        return ty;
    }
    perr(p, t, "expected a type but found %s", tok_desc(t));
}

static Type* parse_type_postfix(Parser* p) {
    Type* t = parse_type_primary(p);
    for (;;) {
        Token* n = peek(p);
        if (n->kind == T_LBRACKET && !n->nl_before && peek_at(p, 1)->kind == T_RBRACKET) {
            advance(p);
            advance(p);
            Type* a = new_type(TY_ARRAY, n->line);
            a->inner = t;
            t = a;
            continue;
        }
        if (n->kind == T_OP && !n->nl_before && n->start[0] == '?') {
            eat_op_prefix(p, "?");
            /* T? is sugar for Option<T> */
            Type* o = new_type(TY_NAME, n->line);
            o->name = str_cintern(p->vm, "Option");
            o->args = pp_malloc(sizeof(Type*));
            o->args[0] = t;
            o->nargs = 1;
            t = o;
            continue;
        }
        break;
    }
    return t;
}

static Type* parse_type(Parser* p) {
    Type* t = parse_type_postfix(p);
    while (tok_is(peek(p), T_OP, "||")) {
        Token* op = advance(p);
        Type* u = new_type(TY_UNION, op->line);
        u->a = t;
        u->b = parse_type_postfix(p);
        t = u;
    }
    return t;
}

/* <T, U = string> -- the opening '<' must be the current token */
static void parse_generic_params(Parser* p, ObjString*** names, Type*** defaults, int* count, bool allow_defaults) {
    if (!eat_op_prefix(p, "<")) perr(p, peek(p), "expected '<'");
    int cap = 0, cap2 = 0, n2 = 0;
    do {
        Token* name = expect(p, T_IDENT, "generic parameter name");
        Type* def = NULL;
        if (check(p, T_ASSIGN)) {
            if (!allow_defaults) perr(p, peek(p), "default type arguments are only allowed on enums");
            advance(p);
            def = parse_type(p);
        }
        GROW(*names, *count, cap);
        (*names)[(*count)++] = intern_tok(p, name);
        if (defaults) {
            GROW(*defaults, n2, cap2);
            (*defaults)[n2++] = def;
        }
    } while (accept(p, T_COMMA));
    if (!eat_op_prefix(p, ">")) perr(p, peek(p), "expected '>' to close the generic parameter list");
}

static void parse_params(Parser* p, Signature* sig) {
    expect(p, T_LPAREN, "'('");
    int cap = 0;
    while (!check(p, T_RPAREN)) {
        if (tok_is(peek(p), T_OP, "...")) {
            advance(p);
            sig->variadic = true;
            break;
        }
        Param prm;
        memset(&prm, 0, sizeof prm);
        Token* t = peek(p);
        TokKind nk = peek_at(p, 1)->kind;
        if (t->kind == T_IDENT && (nk == T_COMMA || nk == T_RPAREN)) {
            advance(p);
            prm.name = intern_tok(p, t);
        } else {
            prm.type = parse_type(p);
            prm.name = intern_tok(p, expect(p, T_IDENT, "parameter name"));
        }
        GROW(sig->params, sig->nparams, cap);
        sig->params[sig->nparams++] = prm;
        if (!accept(p, T_COMMA)) break;
    }
    expect(p, T_RPAREN, "')'");
}

/* --------------------------------------------------------------- patterns */

static Pattern* to_pattern(Parser* p, Node* e, Token* at) {
    Pattern* pt = pp_calloc(1, sizeof(Pattern));
    pt->line = e->line;
    switch (e->kind) {
        case N_INT:
        case N_FLOAT:
        case N_STRING:
        case N_BOOL:
            pt->kind = P_LITERAL;
            pt->lit = e;
            return pt;
        case N_IDENT:
            if (str_equals(e->as.str, "_")) {
                pt->kind = P_WILD;
            } else {
                pt->kind = P_BIND;
                pt->name = e->as.str;
            }
            return pt;
        case N_FIELD:
            pt->kind = P_VARIANT;
            pt->ctor = e;
            return pt;
        case N_CALL: {
            Node* c = e->as.call.callee;
            NodeList* args = &e->as.call.args;
            if (c->kind == N_IDENT && str_equals(c->as.str, "-") && args->count == 1 &&
                (args->items[0]->kind == N_INT || args->items[0]->kind == N_FLOAT)) {
                Node* lit = args->items[0];
                if (lit->kind == N_INT) lit->as.ival = -lit->as.ival;
                else lit->as.fval = -lit->as.fval;
                pt->kind = P_LITERAL;
                pt->lit = lit;
                return pt;
            }
            if (c->kind == N_IDENT || c->kind == N_FIELD) {
                pt->kind = P_VARIANT;
                pt->ctor = c;
                pt->has_parens = true;
                pt->nsubs = args->count;
                pt->subs = pp_calloc((size_t)(args->count ? args->count : 1), sizeof(Pattern*));
                for (int i = 0; i < args->count; i++) pt->subs[i] = to_pattern(p, args->items[i], at);
                return pt;
            }
            break;
        }
        default: break;
    }
    perr(p, at, "invalid pattern (expected a literal, a name, '_' or a variant like some(x))");
}

/* ------------------------------------------------------------ expressions */

static void parse_args(Parser* p, NodeList* out) {
    expect(p, T_LPAREN, "'('");
    p->depth++;
    while (!check(p, T_RPAREN)) {
        nl_push(out, parse_expression(p));
        if (!accept(p, T_COMMA)) break;
    }
    expect(p, T_RPAREN, "')' to close the argument list");
    p->depth--;
}

static Node* parse_function(Parser* p, ObjString** generics, int ngenerics, bool statement) {
    Token* kw = expect(p, T_FUNCTION, "'function'");
    Signature* sig = pp_calloc(1, sizeof(Signature));
    sig->generics = generics;
    sig->ngenerics = ngenerics;
    sig->file = p->file;
    sig->line = kw->line;
    Token* nt = peek(p);
    bool named = false;
    if (nt->kind == T_IDENT || nt->kind == T_OP) {
        advance(p);
        sig->name = intern_tok(p, nt);
        named = true;
    } else {
        sig->name = str_cintern(p->vm, "anonymous");
    }
    parse_params(p, sig);
    if (accept(p, T_ARROW)) sig->ret = parse_type(p);
    Node* body = parse_block(p);
    Node* n = new_node(statement && named ? S_FUNCDECL : N_FUNC, kw->line);
    n->as.func.sig = sig;
    n->as.func.body = body;
    return n;
}

static Node* parse_if(Parser* p) {
    Token* kw = advance(p);
    Node* n = new_node(N_IF, kw->line);
    bool forced = false;
    if (accept(p, T_LET)) {
        forced = true;
        n->as.if_.pat_mut = true;
    } else if (accept(p, T_CONST)) {
        forced = true;
    }
    Token* cond_tok = peek(p);
    Node* cond = parse_expression(p);
    if (check(p, T_ASSIGN)) {
        advance(p);
        n->as.if_.pat = to_pattern(p, cond, cond_tok);
        cond = parse_expression(p);
    } else if (forced) {
        perr(p, peek(p), "expected '=' after the pattern of 'if let'");
    }
    n->as.if_.cond = cond;
    n->as.if_.then_b = parse_block(p);
    if (accept(p, T_ELSE)) {
        if (check(p, T_IF)) n->as.if_.else_b = parse_if(p);
        else n->as.if_.else_b = parse_block(p);
    }
    return n;
}

static Node* parse_match(Parser* p) {
    Token* kw = advance(p);
    Node* n = new_node(N_MATCH, kw->line);
    n->as.match.subject = parse_expression(p);
    expect(p, T_LBRACE, "'{' to open the match arms");
    int saved = p->depth;
    p->depth = 0;
    int cap = 0;
    while (!check(p, T_RBRACE)) {
        if (accept(p, T_COMMA) || accept(p, T_SEMI)) continue;
        if (check(p, T_EOF)) perr(p, peek(p), "unterminated match (missing '}')");
        MatchArm arm;
        memset(&arm, 0, sizeof arm);
        Token* pt = peek(p);
        arm.pat = to_pattern(p, parse_expression(p), pt);
        if (accept(p, T_IF)) arm.guard = parse_expression(p);
        expect(p, T_ARROW, "'->' after the match pattern");
        if (check(p, T_LBRACE)) {
            arm.body = parse_block(p);
            arm.body_is_block = true;
        } else {
            arm.body = parse_expression(p);
        }
        GROW(n->as.match.arms, n->as.match.narms, cap);
        n->as.match.arms[n->as.match.narms++] = arm;
    }
    expect(p, T_RBRACE, "'}'");
    p->depth = saved;
    return n;
}

static bool starts_juxtaposed_arg(Token* t) {
    if (t->nl_before) return false;
    switch (t->kind) {
        case T_IDENT:
        case T_INT:
        case T_FLOAT:
        case T_STRING:
        case T_TRUE:
        case T_FALSE:
        case T_FUNCTION: return true;
        case T_LBRACKET: return t->space_before;
        default: return false;
    }
}

static Node* parse_primary(Parser* p) {
    Token* t = peek(p);
    switch (t->kind) {
        case T_INT: {
            advance(p);
            Node* n = new_node(N_INT, t->line);
            n->as.ival = t->ival;
            return n;
        }
        case T_FLOAT: {
            advance(p);
            Node* n = new_node(N_FLOAT, t->line);
            n->as.fval = t->fval;
            return n;
        }
        case T_STRING: {
            advance(p);
            Node* n = new_node(N_STRING, t->line);
            n->as.str = str_intern(p->vm, t->sval, t->slen);
            return n;
        }
        case T_TRUE:
        case T_FALSE: {
            advance(p);
            Node* n = new_node(N_BOOL, t->line);
            n->as.bval = t->kind == T_TRUE;
            return n;
        }
        case T_IDENT: {
            advance(p);
            Node* id = new_node(N_IDENT, t->line);
            id->as.str = intern_tok(p, t);
            if (starts_juxtaposed_arg(peek(p))) {
                /* unary syntax: `success value` == success(value) */
                Node* c = new_call(p, id, t->line);
                nl_push(&c->as.call.args, parse_expression(p));
                return c;
            }
            return id;
        }
        case T_LPAREN: {
            advance(p);
            p->depth++;
            Node* e = parse_expression(p);
            expect(p, T_RPAREN, "')'");
            p->depth--;
            return e;
        }
        case T_LBRACKET: {
            advance(p);
            p->depth++;
            Node* n = new_node(N_ARRAY, t->line);
            while (!check(p, T_RBRACKET)) {
                nl_push(&n->as.array.items, parse_expression(p));
                if (!accept(p, T_COMMA)) break;
            }
            expect(p, T_RBRACKET, "']' to close the array");
            p->depth--;
            return n;
        }
        case T_LBRACE: {
            advance(p);
            p->depth++;
            Node* n = new_node(N_OBJECT, t->line);
            int cap = 0, nkeys = 0;
            while (!check(p, T_RBRACE)) {
                Token* k = peek(p);
                ObjString* key;
                if (k->kind == T_STRING) key = str_intern(p->vm, k->sval, k->slen);
                else if (k->kind == T_IDENT || (k->kind >= T_IMPORT && k->kind <= T_FALSE)) key = intern_tok(p, k);
                else perr(p, k, "expected an object key but found %s", tok_desc(k));
                advance(p);
                Node* value;
                if (accept(p, T_COLON)) {
                    value = parse_expression(p);
                } else if (k->kind == T_IDENT && (check(p, T_COMMA) || check(p, T_RBRACE))) {
                    value = new_node(N_IDENT, k->line); /* shorthand { name } */
                    value->as.str = key;
                } else {
                    perr(p, peek(p), "expected ':' after object key");
                }
                GROW(n->as.object.keys, nkeys, cap);
                n->as.object.keys[nkeys++] = key;
                nl_push(&n->as.object.values, value);
                if (!accept(p, T_COMMA)) break;
            }
            expect(p, T_RBRACE, "'}' to close the object");
            p->depth--;
            return n;
        }
        case T_FUNCTION: return parse_function(p, NULL, 0, false);
        case T_IF: return parse_if(p);
        case T_MATCH: return parse_match(p);
        default: perr(p, t, "unexpected %s", tok_desc(t));
    }
}

static Node* parse_postfix(Parser* p, Node* e) {
    if (e->kind == N_IF || e->kind == N_MATCH) return e;
    for (;;) {
        Token* t = peek(p);
        if (t->kind == T_LPAREN && !t->nl_before) {
            Node* c = new_call(p, e, t->line);
            parse_args(p, &c->as.call.args);
            e = c;
            continue;
        }
        if (t->kind == T_LBRACKET && !t->nl_before && !t->space_before) {
            advance(p);
            p->depth++;
            Node* ix = new_node(N_INDEX, t->line);
            ix->as.index.obj = e;
            ix->as.index.index = parse_expression(p);
            expect(p, T_RBRACKET, "']'");
            p->depth--;
            e = ix;
            continue;
        }
        if (t->kind == T_COLON && !t->nl_before) {
            /* attribute access: object:field, module:symbol, Enum:VARIANT */
            advance(p);
            Token* nm = peek(p);
            if (nm->kind != T_IDENT && nm->kind != T_OP) perr(p, nm, "expected an attribute name after ':' but found %s", tok_desc(nm));
            advance(p);
            Node* f = new_node(N_FIELD, nm->line);
            f->as.field.obj = e;
            f->as.field.name = intern_tok(p, nm);
            e = f;
            continue;
        }
        if (t->kind == T_DOT) {
            /* dot syntax: a.f(b) == f(a, b), a.mod:f(b) == mod:f(a, b) */
            advance(p);
            Token* nm = peek(p);
            if (nm->kind != T_IDENT && nm->kind != T_OP) perr(p, nm, "expected a function name after '.' but found %s", tok_desc(nm));
            advance(p);
            Node* callee = new_node(N_IDENT, nm->line);
            callee->as.str = intern_tok(p, nm);
            while (check(p, T_COLON) && !peek(p)->nl_before) {
                advance(p);
                Token* part = peek(p);
                if (part->kind != T_IDENT && part->kind != T_OP)
                    perr(p, part, "expected an attribute name after ':' but found %s", tok_desc(part));
                advance(p);
                Node* f = new_node(N_FIELD, part->line);
                f->as.field.obj = callee;
                f->as.field.name = intern_tok(p, part);
                callee = f;
            }
            if (!check(p, T_LPAREN) || peek(p)->nl_before)
                perr(p, nm, "'.' calls a function, expected '(' after '.%.*s' (use ':' to access an attribute)", nm->len, nm->start);
            Node* m = new_node(N_METHOD, nm->line);
            m->as.method.recv = e;
            m->as.method.callee = callee;
            m->as.method.ic = new_ic(p);
            parse_args(p, &m->as.method.args);
            e = m;
            continue;
        }
        break;
    }
    return e;
}

static Node* parse_unary(Parser* p) {
    Token* t = peek(p);
    if (t->kind == T_OP) {
        advance(p);
        Node* id = new_node(N_IDENT, t->line);
        id->as.str = intern_tok(p, t);
        Token* nx = peek(p);
        if (nx->kind == T_COMMA || nx->kind == T_RPAREN || nx->kind == T_RBRACKET || nx->kind == T_RBRACE ||
            nx->kind == T_EOF || nx->kind == T_SEMI || nx->nl_before)
            return id; /* operator used as a value: reduce(xs, +, 0) */
        if (nx->kind == T_LPAREN && !nx->space_before) {
            /* classic call of an operator: +(a, b) */
            Node* c = new_call(p, id, t->line);
            parse_args(p, &c->as.call.args);
            return parse_postfix(p, c);
        }
        Node* c = new_call(p, id, t->line);
        nl_push(&c->as.call.args, parse_unary(p));
        return c;
    }
    return parse_postfix(p, parse_primary(p));
}

static Node* parse_expression(Parser* p) {
    Node* first = parse_unary(p);
    Token* t = peek(p);
    if (!(t->kind == T_OP && (!t->nl_before || p->depth > 0))) return first;
    Node* n = new_node(N_CHAIN, first->line);
    nl_push(&n->as.chain.operands, first);
    int cap = 0;
    while (peek(p)->kind == T_OP && (!peek(p)->nl_before || p->depth > 0)) {
        Token* op = advance(p);
        GROW(n->as.chain.ops, n->as.chain.nops, cap);
        n->as.chain.ops[n->as.chain.nops++] = intern_tok(p, op);
        nl_push(&n->as.chain.operands, parse_unary(p));
    }
    n->as.chain.ics = pp_calloc((size_t)n->as.chain.nops, sizeof(InlineCache));
    for (int i = 0; i < n->as.chain.nops; i++) vm_register_ic(p->vm, &n->as.chain.ics[i]);
    n->as.chain.version = -1;
    return n;
}

/* ------------------------------------------------------------- statements */

static Node* parse_block(Parser* p) {
    Token* open = expect(p, T_LBRACE, "'{'");
    int saved = p->depth;
    p->depth = 0;
    p->block_depth++;
    Node* b = new_node(N_BLOCK, open->line);
    while (!check(p, T_RBRACE)) {
        if (check(p, T_EOF)) perr(p, peek(p), "unterminated block (missing '}' for the block opened on line %d)", open->line);
        if (accept(p, T_SEMI)) continue;
        Node* s = parse_statement(p);
        nl_push(&b->as.block.stmts, s);
        switch (s->kind) {
            case S_LET:
            case S_FUNCDECL:
            case S_TYPE:
            case S_ENUM:
            case S_IMPORT: b->as.block.scoped = true; break;
            default: break;
        }
    }
    advance(p);
    p->depth = saved;
    p->block_depth--;
    return b;
}

static void end_statement(Parser* p) {
    Token* t = peek(p);
    if (t->kind == T_SEMI) {
        while (accept(p, T_SEMI)) {}
        return;
    }
    if (is_terminator(t)) return;
    if (p->pos > 0 && p->toks[p->pos - 1].kind == T_RBRACE) return;
    perr(p, t, "expected the end of the statement (a newline or ';') before %s", tok_desc(t));
}

static Node* parse_let(Parser* p) {
    Token* kw = advance(p);
    Node* n = new_node(S_LET, kw->line);
    n->as.let.is_const = kw->kind == T_CONST;
    Token* t = peek(p);
    Token* nx = peek_at(p, 1);
    if (t->kind == T_IDENT && (nx->kind == T_ASSIGN || is_terminator(nx))) {
        advance(p);
        n->as.let.name = intern_tok(p, t);
    } else {
        n->as.let.type = parse_type(p);
        n->as.let.name = intern_tok(p, expect(p, T_IDENT, "variable name"));
    }
    if (accept(p, T_ASSIGN)) {
        n->as.let.init = parse_expression(p);
    } else if (n->as.let.is_const && n->as.let.type) {
        perr(p, peek(p), "constant '%s' needs a value", n->as.let.name->chars);
    }
    return n;
}

static Node* parse_import(Parser* p) {
    Token* kw = advance(p);
    Node* n = new_node(S_IMPORT, kw->line);
    StrBuf path;
    sb_init(&path);
    ObjString* last;
    if (check(p, T_STRING)) {
        /* import "./file" - a file relative to the importing file */
        Token* s = advance(p);
        if (s->slen == 0) perr(p, s, "empty import path");
        sb_append(&path, s->sval, (size_t)s->slen);
        const char* base = s->sval;
        for (const char* c = s->sval; *c; c++)
            if (*c == '/' || *c == '\\') base = c + 1;
        int len = (int)strlen(base);
        if (len > 3 && strcmp(base + len - 3, ".pp") == 0) len -= 3;
        if (len == 0) perr(p, s, "import path '%s' does not name a file", s->sval);
        last = str_intern(p->vm, base, len);
        n->as.import.is_file = true;
    } else {
        /* import name - a library module (native or from the lib directories) */
        Token* id = expect(p, T_IDENT, "module name or a \"./path\" string");
        sb_append(&path, id->start, (size_t)id->len);
        last = intern_tok(p, id);
    }
    n->as.import.path = str_intern(p->vm, path.data, (int)path.len);
    sb_free(&path);
    n->as.import.alias = last;
    if (accept(p, T_ARROW)) {
        if (accept(p, T_LBRACE)) {
            n->as.import.destructure = true;
            int cap = 0;
            while (!check(p, T_RBRACE)) {
                Token* nm = peek(p);
                if (nm->kind != T_IDENT && nm->kind != T_OP) perr(p, nm, "expected a name to import but found %s", tok_desc(nm));
                advance(p);
                GROW(n->as.import.names, n->as.import.nnames, cap);
                n->as.import.names[n->as.import.nnames++] = intern_tok(p, nm);
                if (!accept(p, T_COMMA)) break;
            }
            expect(p, T_RBRACE, "'}'");
        } else {
            n->as.import.alias = intern_tok(p, expect(p, T_IDENT, "alias name"));
        }
    }
    return n;
}

/* export name | export name -> alias | export { a, b -> c, + } */
static Node* parse_export(Parser* p) {
    Token* kw = advance(p);
    if (p->block_depth > 0) perr(p, kw, "'export' is only allowed at the top level of a file");
    Node* n = new_node(S_EXPORT, kw->line);
    int cap = 0, cap2 = 0, n2 = 0;
    bool braces = accept(p, T_LBRACE);
    do {
        if (braces && check(p, T_RBRACE)) break;
        Token* nm = peek(p);
        if (nm->kind != T_IDENT && nm->kind != T_OP) perr(p, nm, "expected a name to export but found %s", tok_desc(nm));
        advance(p);
        ObjString* name = intern_tok(p, nm);
        ObjString* alias = name;
        if (accept(p, T_ARROW)) {
            Token* a = peek(p);
            if (a->kind != T_IDENT && a->kind != T_OP) perr(p, a, "expected an export alias but found %s", tok_desc(a));
            advance(p);
            alias = intern_tok(p, a);
        }
        GROW(n->as.export_.names, n->as.export_.count, cap);
        GROW(n->as.export_.aliases, n2, cap2);
        n->as.export_.names[n->as.export_.count++] = name;
        n->as.export_.aliases[n2++] = alias;
    } while (braces && accept(p, T_COMMA));
    if (braces) expect(p, T_RBRACE, "'}'");
    return n;
}

static Node* parse_for(Parser* p) {
    Token* kw = advance(p);
    Node* n = new_node(S_FOR, kw->line);
    if (accept(p, T_LET)) n->as.for_.mut = true;
    else accept(p, T_CONST);
    n->as.for_.var1 = intern_tok(p, expect(p, T_IDENT, "loop variable name"));
    if (accept(p, T_COMMA)) n->as.for_.var2 = intern_tok(p, expect(p, T_IDENT, "second loop variable name"));
    expect(p, T_IN, "'in'");
    n->as.for_.iter = parse_expression(p);
    n->as.for_.body = parse_block(p);
    return n;
}

static Node* parse_enum(Parser* p) {
    Token* kw = advance(p);
    Node* n = new_node(S_ENUM, kw->line);
    if (check(p, T_IDENT) && peek_at(p, 1)->kind == T_IDENT) n->as.enum_decl.value_type = parse_type(p);
    n->as.enum_decl.name = intern_tok(p, expect(p, T_IDENT, "enum name"));
    Token* lt = peek(p);
    if (lt->kind == T_OP && lt->start[0] == '<')
        parse_generic_params(p, &n->as.enum_decl.generics, &n->as.enum_decl.defaults, &n->as.enum_decl.ngenerics, true);
    expect(p, T_LBRACE, "'{'");
    int cap = 0;
    while (!check(p, T_RBRACE)) {
        if (accept(p, T_COMMA) || accept(p, T_SEMI)) continue;
        VariantDecl vd;
        memset(&vd, 0, sizeof vd);
        vd.name = intern_tok(p, expect(p, T_IDENT, "variant name"));
        if (accept(p, T_LPAREN)) {
            vd.is_ctor = true;
            int pcap = 0;
            while (!check(p, T_RPAREN)) {
                Type* t = parse_type(p);
                GROW(vd.payload, vd.npayload, pcap);
                vd.payload[vd.npayload++] = t;
                if (!accept(p, T_COMMA)) break;
            }
            expect(p, T_RPAREN, "')'");
        }
        if (accept(p, T_ASSIGN)) vd.value = parse_expression(p);
        GROW(n->as.enum_decl.variants, n->as.enum_decl.nvariants, cap);
        n->as.enum_decl.variants[n->as.enum_decl.nvariants++] = vd;
    }
    expect(p, T_RBRACE, "'}'");
    return n;
}

static bool is_generic_prefix(Parser* p) {
    Token* t = peek(p);
    if (!(t->kind == T_OP && t->len == 1 && t->start[0] == '<')) return false;
    int i = 1;
    for (;;) {
        if (peek_at(p, i)->kind != T_IDENT) return false;
        Token* sep = peek_at(p, i + 1);
        if (sep->kind == T_COMMA) {
            i += 2;
            continue;
        }
        return sep->kind == T_OP && sep->start[0] == '>' && peek_at(p, i + 2)->kind == T_FUNCTION;
    }
}

static Node* parse_statement(Parser* p) {
    Token* t = peek(p);
    Node* n = NULL;
    switch (t->kind) {
        case T_IMPORT: n = parse_import(p); break;
        case T_EXPORT: n = parse_export(p); break;
        case T_CONST:
        case T_LET: n = parse_let(p); break;
        case T_FUNCTION: {
            TokKind k = peek_at(p, 1)->kind;
            if (k == T_IDENT || k == T_OP) return parse_function(p, NULL, 0, true);
            break;
        }
        case T_OP:
            if (is_generic_prefix(p)) {
                ObjString** gens = NULL;
                int ngen = 0;
                parse_generic_params(p, &gens, NULL, &ngen, false);
                if (!check(p, T_FUNCTION)) perr(p, peek(p), "expected 'function' after the generic parameter list");
                return parse_function(p, gens, ngen, true);
            }
            break;
        case T_RETURN:
            advance(p);
            n = new_node(S_RETURN, t->line);
            if (!is_terminator(peek(p))) n->as.ret.value = parse_expression(p);
            break;
        case T_BREAK:
            advance(p);
            n = new_node(S_BREAK, t->line);
            break;
        case T_CONTINUE:
            advance(p);
            n = new_node(S_CONTINUE, t->line);
            break;
        case T_FOR: return parse_for(p);
        case T_WHILE: {
            advance(p);
            n = new_node(S_WHILE, t->line);
            n->as.while_.cond = parse_expression(p);
            n->as.while_.body = parse_block(p);
            return n;
        }
        case T_ENUM: return parse_enum(p);
        case T_LBRACE: return parse_block(p);
        case T_IDENT: {
            Token* a = peek_at(p, 1);
            Token* b = peek_at(p, 2);
            if (tok_is(t, T_IDENT, "type") && a->kind == T_IDENT && !a->nl_before &&
                (b->kind == T_ASSIGN || (b->kind == T_OP && b->start[0] == '<'))) {
                advance(p);
                n = new_node(S_TYPE, t->line);
                Prim pr;
                if (prim_from_name(a->start, a->len, &pr)) perr(p, a, "cannot redefine the built-in type '%.*s'", a->len, a->start);
                advance(p);
                n->as.type_decl.name = intern_tok(p, a);
                if (check(p, T_OP)) parse_generic_params(p, &n->as.type_decl.generics, NULL, &n->as.type_decl.ngenerics, false);
                expect(p, T_ASSIGN, "'='");
                n->as.type_decl.def = parse_type(p);
                break;
            }
            if (tok_is(t, T_IDENT, "precedence") && (a->kind == T_OP || a->kind == T_IDENT) && !a->nl_before &&
                (tok_is(b, T_IDENT, "over") || tok_is(b, T_IDENT, "under") || tok_is(b, T_IDENT, "as"))) {
                advance(p);
                advance(p);
                advance(p);
                Token* c = advance(p);
                if (c->kind != T_OP && c->kind != T_IDENT) perr(p, c, "expected an operator after '%.*s'", b->len, b->start);
                n = new_node(S_PRECEDENCE, t->line);
                n->as.prec.op = intern_tok(p, a);
                n->as.prec.other = intern_tok(p, c);
                n->as.prec.rel = tok_is(b, T_IDENT, "over") ? 1 : tok_is(b, T_IDENT, "under") ? -1 : 0;
                break;
            }
            break;
        }
        default: break;
    }
    if (!n) {
        Node* e = parse_expression(p);
        Token* at = peek(p);
        if (at->kind == T_ASSIGN || at->kind == T_COMPOUND) {
            if (!(e->kind == N_IDENT || e->kind == N_FIELD || e->kind == N_INDEX))
                perr(p, at, "invalid assignment target (expected a variable, field or index)");
            advance(p);
            n = new_node(S_ASSIGN, at->line);
            n->as.assign.target = e;
            if (at->kind == T_COMPOUND) {
                n->as.assign.op = intern_tok(p, at);
                n->as.assign.ic = new_ic(p);
            }
            n->as.assign.value = parse_expression(p);
        } else {
            n = new_node(S_EXPR, e->line);
            n->as.expr.expr = e;
        }
    }
    end_statement(p);
    return n;
}

/* ----------------------------------------------------------------- entry */

Node* parse_program(VM* vm, const char* src, const char* file) {
    TokenList tl = {0};
    if (!lex(src, file, &tl)) return NULL;
    Parser p;
    memset(&p, 0, sizeof p);
    p.vm = vm;
    p.toks = tl.toks;
    p.count = tl.count;
    p.file = file;
    if (setjmp(p.jb)) {
        free(tl.toks);
        return NULL;
    }
    Node* block = new_node(N_BLOCK, 1);
    block->as.block.scoped = true;
    while (!check(&p, T_EOF)) {
        if (accept(&p, T_SEMI)) continue;
        if (check(&p, T_RBRACE)) perr(&p, peek(&p), "unexpected '}'");
        nl_push(&block->as.block.stmts, parse_statement(&p));
    }
    free(tl.toks);
    return block;
}

Signature* parse_native_signature(VM* vm, const char* name, const char* sig_src) {
    TokenList tl = {0};
    if (!lex(sig_src, "<native>", &tl)) {
        fprintf(stderr, "fatal: bad native signature for '%s'\n", name);
        exit(70);
    }
    Parser p;
    memset(&p, 0, sizeof p);
    p.vm = vm;
    p.toks = tl.toks;
    p.count = tl.count;
    p.file = "<native>";
    if (setjmp(p.jb)) {
        fprintf(stderr, "fatal: bad native signature for '%s': %s\n", name, sig_src);
        exit(70);
    }
    Signature* sig = pp_calloc(1, sizeof(Signature));
    sig->name = str_cintern(vm, name);
    sig->file = "<internal>";
    if (check(&p, T_OP) && peek(&p)->start[0] == '<') parse_generic_params(&p, &sig->generics, NULL, &sig->ngenerics, false);
    parse_params(&p, sig);
    if (accept(&p, T_ARROW)) sig->ret = parse_type(&p);
    free(tl.toks);
    return sig;
}
