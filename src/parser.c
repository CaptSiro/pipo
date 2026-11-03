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
    bool no_lambda;  /* parsing a match pattern / guard, where `x -> ...` starts the arm body */
    bool in_native;  /* parsing the declaration after `native` (it must not have a body) */
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

static PP_NORETURN void perr_line(Parser* p, int line, const char* fmt, ...) {
    fflush(stdout);
    fprintf(stderr, "%s:%d: syntax error: ", p->file, line);
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

/*
 * A token that can name a function: an identifier, an operator, or the
 * keyword `in`, which is also an infix operator (`x in xs` calls in(x, xs)).
 */
static bool is_fn_name(TokKind k) { return k == T_IDENT || k == T_OP || k == T_IN; }

/*
 * A function name at offset `at`: an identifier, an operator, `in`, or a bracket
 * pair `[]` / `[]=` (written without spaces). Bracket names only appear in name
 * positions (after `function`, `.`, `:`, in import / export lists); in an
 * expression `[]` stays an empty array.
 */
static bool at_fn_name(Parser* p, int at) {
    Token* t = peek_at(p, at);
    if (is_fn_name(t->kind)) return true;
    Token* c = peek_at(p, at + 1);
    return t->kind == T_LBRACKET && c->kind == T_RBRACKET && !c->space_before && !c->nl_before;
}

/* consumes the name at_fn_name accepted */
static ObjString* take_fn_name(Parser* p) {
    Token* t = advance(p);
    if (t->kind != T_LBRACKET) return intern_tok(p, t);
    advance(p); /* ] */
    Token* eq = peek(p);
    if (eq->kind == T_ASSIGN && !eq->space_before && !eq->nl_before) {
        advance(p);
        return str_cintern(p->vm, "[]=");
    }
    return str_cintern(p->vm, "[]");
}

static bool is_assignable(Node* e) { return e->kind == N_IDENT || e->kind == N_FIELD || e->kind == N_INDEX; }

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
static void end_statement(Parser* p);
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
        if (t->len == 5 && memcmp(t->start, "tuple", 5) == 0) {
            /* tuple<int>, tuple<string, float>: the type arguments are the elements, so they are required */
            Token* n = peek(p);
            if (!(n->kind == T_OP && !n->nl_before && n->start[0] == '<'))
                perr(p, t, "'tuple' needs its element types as type arguments, e.g. tuple<int, string>");
            eat_op_prefix(p, "<");
            Type* ty = new_type(TY_TUPLE, t->line);
            int cap = 0;
            do {
                Type* a = parse_type(p);
                GROW(ty->args, ty->nargs, cap);
                ty->args[ty->nargs++] = a;
            } while (accept(p, T_COMMA));
            if (!eat_op_prefix(p, ">")) perr(p, peek(p), "expected '>' to close the element types of 'tuple'");
            return ty;
        }
        Type* ty = new_type(TY_NAME, t->line);
        ty->name = intern_tok(p, t);
        int qcap = 0;
        while (check(p, T_COLON) && !peek(p)->nl_before) {
            /* module:Type, package:module:Type */
            advance(p);
            GROW(ty->quals, ty->nquals, qcap);
            ty->quals[ty->nquals++] = ty->name;
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

/* offset (from the current token) of the ')' matching the '(' at offset `at`, or -1 */
static int matching_paren(Parser* p, int at) {
    int depth = 0;
    for (int i = at; p->pos + i < p->count; i++) {
        TokKind k = peek_at(p, i)->kind;
        if (k == T_EOF) return -1;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) {
            if (--depth == 0) return k == T_RPAREN ? i : -1;
        }
    }
    return -1;
}

/*
 * The right-hand side of `type Name = ...`, which may be a callback type:
 *     float -> int      (A, B) -> R      () -> void      (...) -> R
 * Callback types only exist as named types; `const float -> int f` is an error.
 */
static Type* parse_type_def(Parser* p) {
    Token* t = peek(p);
    Type* fn = NULL;
    if (t->kind == T_LPAREN) {
        int close = matching_paren(p, 0);
        if (close > 0 && peek_at(p, close + 1)->kind == T_ARROW) {
            advance(p);
            fn = new_type(TY_FUNC, t->line);
            int cap = 0;
            while (!check(p, T_RPAREN)) {
                if (tok_is(peek(p), T_OP, "...")) {
                    advance(p);
                    fn->variadic = true;
                    break;
                }
                Type* a = parse_type(p);
                GROW(fn->args, fn->nargs, cap);
                fn->args[fn->nargs++] = a;
                if (!accept(p, T_COMMA)) break;
            }
            expect(p, T_RPAREN, "')' to close the callback parameter types");
        }
    }
    if (!fn) {
        Type* single = parse_type(p);
        if (!check(p, T_ARROW) || peek(p)->nl_before) return single;
        fn = new_type(TY_FUNC, single->line);
        fn->args = pp_malloc(sizeof(Type*));
        fn->args[0] = single;
        fn->nargs = 1;
    }
    expect(p, T_ARROW, "'->' and the return type of the callback type");
    fn->inner = parse_type_def(p);
    return fn;
}

static void no_inline_fn_type(Parser* p) {
    if (check(p, T_ARROW))
        perr(p, peek(p), "a callback type cannot be written inline, name it first: type ToInt = float -> int");
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

/*
 * unnamed: NULL when every typed parameter needs a name; otherwise a missing
 * name is allowed (a declaration) and *unnamed gets the first place one is missing
 */
static void parse_params(Parser* p, Signature* sig, Token** unnamed) {
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
            /* (int) is a type without a name: fine in a declaration, an error anywhere else */
            Prim pr;
            if (prim_from_name(t->start, t->len, &pr)) {
                if (!unnamed) perr(p, peek(p), "expected parameter name but found %s", tok_desc(peek(p)));
                if (!*unnamed) *unnamed = peek(p);
            }
        } else {
            prm.type = parse_type(p);
            no_inline_fn_type(p);
            if (unnamed && !check(p, T_IDENT)) {
                if (!*unnamed) *unnamed = peek(p);
                prm.name = str_cintern(p->vm, "");
            } else {
                prm.name = intern_tok(p, expect(p, T_IDENT, "parameter name"));
            }
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
    bool named = false;
    if (at_fn_name(p, 0)) {
        sig->name = take_fn_name(p);
        named = true;
    } else {
        sig->name = str_cintern(p->vm, "anonymous");
    }
    Token* unnamed = NULL;
    parse_params(p, sig, statement && named ? &unnamed : NULL);
    if (accept(p, T_ARROW)) sig->ret = parse_type(p);
    if (p->in_native && check(p, T_LBRACE))
        perr(p, peek(p), "native function '%s' cannot have a body: its implementation is written in C", sig->name->chars);
    if (statement && named && !check(p, T_LBRACE)) {
        /*
         * function a(int) -> float: a declaration, written like an interface
         * requirement (names are optional, so a lone name is a type). The
         * definition must follow in the same block.
         */
        ObjString* empty = str_cintern(p->vm, "");
        for (int i = 0; i < sig->nparams; i++) {
            Param* prm = &sig->params[i];
            if (prm->type || prm->name == empty) continue;
            Prim pr;
            const char* nm = prm->name->chars;
            if (prim_from_name(nm, (int)strlen(nm), &pr)) {
                prm->type = new_type(TY_PRIM, kw->line);
                prm->type->prim = pr;
            } else {
                prm->type = new_type(TY_NAME, kw->line);
                prm->type->name = prm->name;
            }
            prm->name = empty;
        }
        end_statement(p);
        Node* n = new_node(S_FUNCDECL, kw->line);
        n->as.func.sig = sig;
        return n;
    }
    if (unnamed) perr(p, unnamed, "expected parameter name but found %s", tok_desc(unnamed));
    Node* body = parse_block(p);
    Node* n = new_node(statement && named ? S_FUNCDECL : N_FUNC, kw->line);
    n->as.func.sig = sig;
    n->as.func.body = body;
    return n;
}

/*
 * Lambdas: x -> expr, (x, y) -> expr, () -> expr, (int x) -> { block }.
 * The current token is the parameter name or the '('. A block body works like
 * a match arm: its last expression is the result unless `return` is used.
 */
static Node* parse_lambda(Parser* p) {
    Token* start = peek(p);
    Signature* sig = pp_calloc(1, sizeof(Signature));
    sig->name = str_cintern(p->vm, "lambda");
    sig->file = p->file;
    sig->line = start->line;
    sig->implicit_result = true;
    if (start->kind == T_IDENT) {
        advance(p);
        sig->params = pp_calloc(1, sizeof(Param));
        sig->params[0].name = intern_tok(p, start);
        sig->nparams = 1;
    } else {
        parse_params(p, sig, NULL);
    }
    expect(p, T_ARROW, "'->'");
    bool saved_nl = p->no_lambda;
    p->no_lambda = false;
    Node* body;
    if (check(p, T_LBRACE)) {
        body = parse_block(p);
    } else {
        int saved_depth = p->depth;
        Node* e = parse_expression(p);
        p->depth = saved_depth;
        Node* st = new_node(S_EXPR, e->line);
        st->as.expr.expr = e;
        body = new_node(N_BLOCK, e->line);
        nl_push(&body->as.block.stmts, st);
    }
    p->no_lambda = saved_nl;
    Node* n = new_node(N_FUNC, start->line);
    n->as.func.sig = sig;
    n->as.func.body = body;
    n->as.func.lambda = true;
    return n;
}

static bool at_lambda(Parser* p) {
    if (p->no_lambda) return false;
    Token* t = peek(p);
    if (t->kind == T_IDENT) {
        Token* a = peek_at(p, 1);
        return a->kind == T_ARROW && !a->nl_before;
    }
    if (t->kind == T_LPAREN) {
        int close = matching_paren(p, 0);
        if (close <= 0) return false;
        Token* a = peek_at(p, close + 1);
        return a->kind == T_ARROW && !a->nl_before;
    }
    return false;
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
        /* `x -> ...` in an arm is the pattern x and the arm body, not a lambda */
        bool saved_nl = p->no_lambda;
        p->no_lambda = true;
        arm.pat = to_pattern(p, parse_expression(p), pt);
        if (accept(p, T_IF)) arm.guard = parse_expression(p);
        p->no_lambda = saved_nl;
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
    if ((t->kind == T_IDENT || t->kind == T_LPAREN) && at_lambda(p)) return parse_lambda(p);
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
        if (t->kind == T_LBRACE && !t->nl_before && !t->space_before &&
            (e->kind == N_IDENT || e->kind == N_FIELD)) {
            /* record construction shorthand: Rect{ ... } == Rect({ ... }) */
            Node* c = new_call(p, e, t->line);
            nl_push(&c->as.call.args, parse_primary(p));
            e = c;
            continue;
        }
        if (t->kind == T_LBRACKET && !t->nl_before && !t->space_before) {
            advance(p);
            p->depth++;
            Node* ix = new_node(N_INDEX, t->line);
            ix->as.index.obj = e;
            ix->as.index.ic = new_ic(p);
            if (check(p, T_RBRACKET)) perr(p, peek(p), "expected an index between '[' and ']'");
            do {
                nl_push(&ix->as.index.args, parse_expression(p));
            } while (accept(p, T_COMMA) && !check(p, T_RBRACKET));
            expect(p, T_RBRACKET, "']'");
            p->depth--;
            e = ix;
            continue;
        }
        if (t->kind == T_COLON && !t->nl_before) {
            /* attribute access: object:field, module:symbol, Enum:VARIANT */
            advance(p);
            Token* nm = peek(p);
            if (!at_fn_name(p, 0)) perr(p, nm, "expected an attribute name after ':' but found %s", tok_desc(nm));
            Node* f = new_node(N_FIELD, nm->line);
            f->as.field.obj = e;
            f->as.field.name = take_fn_name(p);
            e = f;
            continue;
        }
        if (t->kind == T_DOT) {
            /* dot syntax: a.f(b) == f(a, b), a.mod:f(b) == mod:f(a, b) */
            advance(p);
            Token* nm = peek(p);
            if (!at_fn_name(p, 0)) perr(p, nm, "expected a function name after '.' but found %s", tok_desc(nm));
            Node* callee = new_node(N_IDENT, nm->line);
            callee->as.str = take_fn_name(p);
            while (check(p, T_COLON) && !peek(p)->nl_before) {
                advance(p);
                Token* part = peek(p);
                if (!at_fn_name(p, 0))
                    perr(p, part, "expected an attribute name after ':' but found %s", tok_desc(part));
                Node* f = new_node(N_FIELD, part->line);
                f->as.field.obj = callee;
                f->as.field.name = take_fn_name(p);
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
    if (t->kind == T_IN) {
        /* `in` is a keyword but also a function: in(3, 0...3), or a value: any(xs, in) */
        Token* nx = peek_at(p, 1);
        bool call = nx->kind == T_LPAREN && !nx->space_before;
        bool value = nx->kind == T_COMMA || nx->kind == T_RPAREN || nx->kind == T_RBRACKET || nx->kind == T_RBRACE ||
                     nx->kind == T_EOF || nx->kind == T_SEMI || nx->nl_before;
        if (call || value) {
            advance(p);
            Node* id = new_node(N_IDENT, t->line);
            id->as.str = intern_tok(p, t);
            if (value) return parse_postfix(p, id);
            Node* c = new_call(p, id, t->line);
            parse_args(p, &c->as.call.args);
            return parse_postfix(p, c);
        }
        perr(p, t, "'in' needs a left operand (x in xs) or call parentheses with no space (in(x, xs))");
    }
    return parse_postfix(p, parse_primary(p));
}

/* an infix operator continues the expression: any operator token, or `in` */
static bool at_infix(Parser* p) {
    Token* t = peek(p);
    return (t->kind == T_OP || t->kind == T_IN) && (!t->nl_before || p->depth > 0);
}

static Node* parse_expression(Parser* p) {
    Node* first = parse_unary(p);
    if (!at_infix(p)) return first;
    Node* n = new_node(N_CHAIN, first->line);
    nl_push(&n->as.chain.operands, first);
    int cap = 0;
    while (at_infix(p)) {
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

static void sig_types_to_string(Signature* sig, StrBuf* sb) {
    if (sig->ngenerics) {
        sb_putc(sb, '<');
        for (int i = 0; i < sig->ngenerics; i++) sb_printf(sb, i ? ", %s" : "%s", sig->generics[i]->chars);
        sb_putc(sb, '>');
    }
    sb_printf(sb, "%s(", sig->name->chars);
    for (int i = 0; i < sig->nparams; i++) {
        if (i) sb_puts(sb, ", ");
        type_to_string(sig->params[i].type, sb);
    }
    if (sig->variadic) sb_puts(sb, sig->nparams ? ", ..." : "...");
    sb_puts(sb, ") -> ");
    type_to_string(sig->ret, sb);
}

/*
 * Every declaration (function without a body) in a block needs a definition
 * later in the same block, written with the same types (parameter names may
 * differ). The definition is linked to the declaration, so at run time it
 * fills in the function object the declaration created.
 */
static Node* unwrap_export(Node* s) {
    return s->kind == S_EXPORT && s->as.export_.decl ? s->as.export_.decl : s;
}

static void check_declarations(Parser* p, NodeList* stmts) {
    for (int i = 0; i < stmts->count; i++) {
        Node* d = unwrap_export(stmts->items[i]);
        if (d->kind != S_FUNCDECL || d->as.func.body || d->as.func.native) continue; /* native: the body is in C */
        StrBuf want;
        sb_init(&want);
        sig_types_to_string(d->as.func.sig, &want);
        Node* def = NULL;
        for (int j = i + 1; j < stmts->count && !def; j++) {
            Node* s = unwrap_export(stmts->items[j]);
            if (s->kind != S_FUNCDECL || s->as.func.sig->name != d->as.func.sig->name) continue;
            StrBuf got;
            sb_init(&got);
            sig_types_to_string(s->as.func.sig, &got);
            bool same = strcmp(want.data, got.data) == 0;
            sb_free(&got);
            if (!same) continue;
            if (!s->as.func.body)
                perr_line(p, s->line, "function %s is already declared on line %d", want.data, d->line);
            if (s->as.func.global != d->as.func.global)
                perr_line(p, s->line, "function %s is declared %s on line %d, so its definition must be %s too", want.data,
                          d->as.func.global ? "'global'" : "without 'global'", d->line,
                          d->as.func.global ? "'global'" : "without 'global'");
            def = s;
        }
        if (!def)
            perr_line(p, d->line, "function %s is declared but not defined (the definition must follow in the same block, with the same types)",
                      want.data);
        def->as.func.decl = d->as.func.sig;
        sb_free(&want);
    }
}

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
            case S_INTERFACE:
            case S_IMPORT: b->as.block.scoped = true; break;
            default: break;
        }
    }
    advance(p);
    p->depth = saved;
    p->block_depth--;
    check_declarations(p, &b->as.block.stmts);
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
        no_inline_fn_type(p);
        n->as.let.name = intern_tok(p, expect(p, T_IDENT, "variable name"));
    }
    if (accept(p, T_ASSIGN)) {
        n->as.let.init = parse_expression(p);
        if (n->as.let.init->kind == N_FUNC && n->as.let.init->as.func.lambda && !n->as.let.type)
            perr(p, kw, "a lambda stored in a variable needs a callback type: type Fn = (int) -> int, then %s Fn %s = ...",
                 n->as.let.is_const ? "const" : "let", n->as.let.name->chars);
    } else if (n->as.let.is_const && n->as.let.type) {
        perr(p, peek(p), "constant '%s' needs a value", n->as.let.name->chars);
    }
    return n;
}

static Node* parse_import(Parser* p) {
    Token* kw = advance(p);
    if (p->block_depth > 0) perr(p, kw, "'import' is only allowed at the top of a file, before any other statement");
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
    /* submodules: import os:fs, import "./ui":shapes */
    int scap = 0;
    while (check(p, T_COLON) && !peek(p)->nl_before) {
        advance(p);
        Token* seg = expect(p, T_IDENT, "submodule name after ':'");
        last = intern_tok(p, seg);
        GROW(n->as.import.subs, n->as.import.nsubs, scap);
        n->as.import.subs[n->as.import.nsubs++] = last;
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
                if (!at_fn_name(p, 0)) perr(p, nm, "expected a name to import but found %s", tok_desc(nm));
                GROW(n->as.import.names, n->as.import.nnames, cap);
                n->as.import.names[n->as.import.nnames++] = take_fn_name(p);
                if (!accept(p, T_COMMA)) break;
            }
            expect(p, T_RBRACE, "'}'");
        } else {
            n->as.import.alias = intern_tok(p, expect(p, T_IDENT, "alias name"));
        }
    }
    return n;
}

static bool is_generic_prefix(Parser* p);
static bool at_native(Parser* p);

/* after `export`: a declaration such as `type A = int` or `function f(...)` */
static bool at_exported_decl(Parser* p) {
    Token* t = peek(p);
    switch (t->kind) {
        case T_FUNCTION:
        case T_GLOBAL:
        case T_CONST:
        case T_LET:
        case T_ENUM: return true;
        case T_OP: return is_generic_prefix(p);
        case T_IDENT: {
            Token* a = peek_at(p, 1);
            if (at_native(p)) return true;
            return (tok_is(t, T_IDENT, "type") || tok_is(t, T_IDENT, "interface")) && a->kind == T_IDENT && !a->nl_before;
        }
        default: return false;
    }
}

/* the name an exported declaration binds */
static ObjString* declared_name(Node* d) {
    switch (d->kind) {
        case S_FUNCDECL: return d->as.func.sig->name;
        case S_LET: return d->as.let.name;
        case S_TYPE: return d->as.type_decl.name;
        case S_ENUM: return d->as.enum_decl.name;
        case S_INTERFACE: return d->as.iface.name;
        default: return NULL;
    }
}

/*
 * export name | export name -> alias | export { a, b -> c, + } | export import ...
 * | export <declaration>: export type A = int, export function f(...) { ... }
 */
static Node* parse_export(Parser* p) {
    Token* kw = advance(p);
    if (p->block_depth > 0) perr(p, kw, "'export' is only allowed at the top level of a file");
    if (check(p, T_IMPORT)) {
        /* export import "./fs" == import "./fs" + export fs */
        Node* im = parse_import(p);
        im->as.import.reexport = true;
        return im;
    }
    Node* n = new_node(S_EXPORT, kw->line);
    if (at_exported_decl(p)) {
        Token* at = peek(p);
        Node* d = parse_statement(p);
        ObjString* name = declared_name(d);
        if (!name) perr(p, at, "'export' before a statement needs a named declaration (type, enum, interface, function, const or let)");
        n->as.export_.decl = d;
        n->as.export_.names = pp_malloc(sizeof(ObjString*));
        n->as.export_.aliases = pp_malloc(sizeof(ObjString*));
        n->as.export_.names[0] = name;
        n->as.export_.aliases[0] = name;
        n->as.export_.count = 1;
        return n;
    }
    int cap = 0, cap2 = 0, n2 = 0;
    bool braces = accept(p, T_LBRACE);
    do {
        if (braces && check(p, T_RBRACE)) break;
        Token* nm = peek(p);
        if (!at_fn_name(p, 0)) perr(p, nm, "expected a name to export but found %s", tok_desc(nm));
        ObjString* name = take_fn_name(p);
        ObjString* alias = name;
        if (accept(p, T_ARROW)) {
            Token* a = peek(p);
            if (!at_fn_name(p, 0)) perr(p, a, "expected an export alias but found %s", tok_desc(a));
            alias = take_fn_name(p);
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
        if (!(sep->kind == T_OP && sep->start[0] == '>')) return false;
        Token* after = peek_at(p, i + 2);
        return after->kind == T_FUNCTION || tok_is(after, T_IDENT, "type") || tok_is(after, T_IDENT, "interface") ||
               tok_is(after, T_IDENT, "impl");
    }
}

/* type Name = ..., type Name<T> = ...; the current token is the name */
static Node* parse_type_decl(Parser* p, int line, ObjString** gens, int ngen) {
    Token* a = advance(p);
    if (a->kind != T_IDENT) perr(p, a, "expected a type name but found %s", tok_desc(a));
    Prim pr;
    if (prim_from_name(a->start, a->len, &pr) || (a->len == 5 && memcmp(a->start, "tuple", 5) == 0))
        perr(p, a, "cannot redefine the built-in type '%.*s'", a->len, a->start);
    Node* n = new_node(S_TYPE, line);
    n->as.type_decl.name = intern_tok(p, a);
    n->as.type_decl.generics = gens;
    n->as.type_decl.ngenerics = ngen;
    if (check(p, T_OP)) {
        if (ngen) perr(p, peek(p), "generic parameters are already given before 'type'");
        parse_generic_params(p, &n->as.type_decl.generics, NULL, &n->as.type_decl.ngenerics, false);
    }
    expect(p, T_ASSIGN, "'='");
    n->as.type_decl.def = parse_type_def(p);
    return n;
}

static bool at_impl(Parser* p);

/*
 * interface Name {
 *     function str(Name) -> string          // Name stands for the implementing type
 *     <F>function convert(Name, F) -> F
 *     function show(Name n) -> string {     // default implementation, used when the type has none
 *         return "<" + n.str() + ">"
 *     }
 *     impl Other for Name                   // extends Other: implementing Name implements Other too
 * }
 */
static Node* parse_interface(Parser* p, int line, ObjString** gens, int ngen) {
    Token* nm = expect(p, T_IDENT, "interface name");
    Node* n = new_node(S_INTERFACE, line);
    n->as.iface.name = intern_tok(p, nm);
    n->as.iface.generics = gens;
    n->as.iface.ngenerics = ngen;
    if (check(p, T_OP)) {
        if (ngen) perr(p, peek(p), "generic parameters are already given before 'interface'");
        parse_generic_params(p, &n->as.iface.generics, NULL, &n->as.iface.ngenerics, false);
    }
    expect(p, T_LBRACE, "'{' to open the interface");
    int cap = 0, pcap_parents = 0;
    ObjString* empty = str_cintern(p->vm, "");
    while (!check(p, T_RBRACE)) {
        if (accept(p, T_SEMI) || accept(p, T_COMMA)) continue;
        if (at_impl(p)) {
            advance(p);
            Type* parent = parse_type(p);
            expect(p, T_FOR, "'for'");
            Token* self = peek(p);
            Type* target = parse_type(p);
            if (target->kind != TY_NAME || target->nquals || target->nargs || target->name != n->as.iface.name)
                perr(p, self, "inside an interface, 'impl' extends another interface: write 'impl Other for %s'",
                     n->as.iface.name->chars);
            GROW(n->as.iface.parents, n->as.iface.nparents, pcap_parents);
            n->as.iface.parents[n->as.iface.nparents++] = parent;
            continue;
        }
        Signature* sig = pp_calloc(1, sizeof(Signature));
        sig->file = p->file;
        if (is_generic_prefix(p)) parse_generic_params(p, &sig->generics, NULL, &sig->ngenerics, false);
        Token* kw = expect(p, T_FUNCTION, "'function' (an interface lists the functions a type must have)");
        sig->line = kw->line;
        Token* fname = peek(p);
        if (!at_fn_name(p, 0)) perr(p, fname, "expected a function name but found %s", tok_desc(fname));
        sig->name = take_fn_name(p);
        expect(p, T_LPAREN, "'('");
        int pcap = 0;
        while (!check(p, T_RPAREN)) {
            if (tok_is(peek(p), T_OP, "...")) {
                advance(p);
                sig->variadic = true;
                break;
            }
            Param prm;
            prm.type = parse_type(p);
            no_inline_fn_type(p);
            prm.name = check(p, T_IDENT) ? intern_tok(p, advance(p)) : empty; /* the name is optional */
            GROW(sig->params, sig->nparams, pcap);
            sig->params[sig->nparams++] = prm;
            if (!accept(p, T_COMMA)) break;
        }
        expect(p, T_RPAREN, "')'");
        if (accept(p, T_ARROW)) sig->ret = parse_type(p);
        Node* body = NULL;
        if (check(p, T_LBRACE)) {
            for (int i = 0; i < sig->nparams; i++)
                if (sig->params[i].name == empty)
                    perr(p, peek(p), "the parameters of a default implementation need names (parameter %d of '%s' has none)", i + 1,
                         sig->name->chars);
            body = parse_block(p);
        }
        int old_cap = cap;
        GROW(n->as.iface.reqs, n->as.iface.nreqs, cap);
        if (cap != old_cap) n->as.iface.bodies = pp_realloc(n->as.iface.bodies, sizeof(Node*) * (size_t)cap);
        n->as.iface.bodies[n->as.iface.nreqs] = body;
        n->as.iface.reqs[n->as.iface.nreqs++] = sig;
    }
    expect(p, T_RBRACE, "'}' to close the interface");
    return n;
}

/* impl Iface for Type, impl Container<int> for IntList */
static bool at_impl(Parser* p) {
    Token* t = peek(p);
    if (!tok_is(t, T_IDENT, "impl") || peek_at(p, 1)->kind != T_IDENT || peek_at(p, 1)->nl_before) return false;
    for (int i = 2;; i++) {
        Token* x = peek_at(p, i);
        if (x->nl_before || x->kind == T_EOF) return false;
        if (x->kind == T_FOR) return true;
        if (!(x->kind == T_IDENT || x->kind == T_COLON || x->kind == T_COMMA || x->kind == T_OP || x->kind == T_LBRACKET ||
              x->kind == T_RBRACKET))
            return false;
    }
}

/* `native` is contextual: native function f(...), native "impl" function f(...), native <T>function f(...) */
static bool at_native(Parser* p) {
    Token* t = peek(p);
    if (!tok_is(t, T_IDENT, "native")) return false;
    Token* a = peek_at(p, 1);
    if (a->nl_before) return false;
    if (tok_is(a, T_IDENT, "type")) return peek_at(p, 2)->kind == T_IDENT; /* native type map<K, V> */
    return a->kind == T_FUNCTION || a->kind == T_STRING || (a->kind == T_OP && a->start[0] == '<');
}

/*
 * native type map<K, V>
 *
 * Binds a type that is implemented in C (std:map, std:set). Only the types the
 * interpreter itself provides exist; the generic parameters must match them.
 */
static Node* parse_native_type(Parser* p, Token* kw) {
    advance(p); /* type */
    Token* name = expect(p, T_IDENT, "type name after 'native type'");
    Node* n = new_node(S_TYPE, kw->line);
    n->as.type_decl.name = intern_tok(p, name);
    Token* lt = peek(p);
    if (lt->kind == T_OP && !lt->nl_before && lt->start[0] == '<')
        parse_generic_params(p, &n->as.type_decl.generics, NULL, &n->as.type_decl.ngenerics, false);
    if (check(p, T_ASSIGN)) perr(p, peek(p), "native type '%s' cannot have a definition: it is implemented in C", n->as.type_decl.name->chars);
    return n;
}

/*
 * native function sqrt(float) -> float
 * native "vec_add" function +(Vec, Vec) -> Vec
 *
 * A function whose body is written in C. The signature (types are resolved in
 * this file, so they may name its types) is declared here; the implementation
 * comes from the table the module registered with vm_register_native_impls,
 * looked up by the function's name or by the string after `native`.
 */
static Node* parse_native(Parser* p) {
    Token* kw = advance(p);
    if (p->block_depth > 0) perr(p, kw, "'native' is only allowed at the top level of a file");
    ObjString* impl = NULL;
    if (check(p, T_STRING)) {
        Token* s = advance(p);
        if (s->slen == 0) perr(p, s, "empty native implementation name");
        impl = str_intern(p->vm, s->sval, s->slen);
    }
    if (tok_is(peek(p), T_IDENT, "type")) {
        if (impl) perr(p, peek(p), "a native type takes no implementation name");
        return parse_native_type(p, kw);
    }
    ObjString** gens = NULL;
    int ngen = 0;
    if (is_generic_prefix(p)) parse_generic_params(p, &gens, NULL, &ngen, false);
    if (!check(p, T_FUNCTION) || !at_fn_name(p, 1))
        perr(p, peek(p), "expected a named function declaration after 'native' but found %s", tok_desc(peek(p)));
    p->in_native = true;
    Node* fn = parse_function(p, gens, ngen, true);
    p->in_native = false;
    fn->as.func.native = impl ? impl : fn->as.func.sig->name;
    return fn;
}

/* impl Iface for Type, <K, V>impl Index<K, V> for std:map<K, V>; the current token is `impl` */
static Node* parse_impl(Parser* p, ObjString** gens, int ngen) {
    Token* kw = advance(p);
    Node* n = new_node(S_IMPL, kw->line);
    n->as.impl.generics = gens;
    n->as.impl.ngenerics = ngen;
    n->as.impl.iface = parse_type(p);
    expect(p, T_FOR, "'for'");
    n->as.impl.target = parse_type(p);
    return n;
}

static Node* parse_statement(Parser* p) {
    Token* t = peek(p);
    if (at_native(p)) return parse_native(p);
    Node* n = NULL;
    switch (t->kind) {
        case T_IMPORT: n = parse_import(p); break;
        case T_EXPORT: n = parse_export(p); break;
        case T_GLOBAL: {
            /* global function f(...) / global <T>function f(...): visible everywhere once the file is loaded */
            advance(p);
            if (p->block_depth > 0) perr(p, t, "'global' is only allowed at the top level of a file");
            Node* fn;
            if (at_native(p)) {
                fn = parse_native(p); /* global native function f(...) */
                if (fn->kind != S_FUNCDECL) perr_line(p, fn->line, "'global' only applies to functions, not to a native type");
            } else if (is_generic_prefix(p)) {
                ObjString** gens = NULL;
                int ngen = 0;
                parse_generic_params(p, &gens, NULL, &ngen, false);
                fn = parse_function(p, gens, ngen, true);
            } else if (check(p, T_FUNCTION) && at_fn_name(p, 1)) {
                fn = parse_function(p, NULL, 0, true);
            } else {
                perr(p, peek(p), "expected a named function declaration after 'global' but found %s", tok_desc(peek(p)));
            }
            fn->as.func.global = true;
            return fn;
        }
        case T_CONST:
        case T_LET: n = parse_let(p); break;
        case T_FUNCTION: {
            if (at_fn_name(p, 1)) return parse_function(p, NULL, 0, true);
            break;
        }
        case T_OP:
            if (is_generic_prefix(p)) {
                ObjString** gens = NULL;
                int ngen = 0;
                parse_generic_params(p, &gens, NULL, &ngen, false);
                if (tok_is(peek(p), T_IDENT, "type")) {
                    advance(p);
                    n = parse_type_decl(p, t->line, gens, ngen);
                    break;
                }
                if (tok_is(peek(p), T_IDENT, "interface")) {
                    advance(p);
                    return parse_interface(p, t->line, gens, ngen);
                }
                if (tok_is(peek(p), T_IDENT, "impl")) {
                    if (!at_impl(p)) perr(p, peek(p), "expected 'impl Interface for Type' after the generic parameter list");
                    n = parse_impl(p, gens, ngen);
                    break;
                }
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
                n = parse_type_decl(p, t->line, NULL, 0);
                break;
            }
            if (tok_is(t, T_IDENT, "interface") && a->kind == T_IDENT && !a->nl_before &&
                (b->kind == T_LBRACE || (b->kind == T_OP && b->start[0] == '<'))) {
                advance(p);
                return parse_interface(p, t->line, NULL, 0);
            }
            if (at_impl(p)) {
                n = parse_impl(p, NULL, 0);
                break;
            }
            if (tok_is(t, T_IDENT, "precedence") && is_fn_name(a->kind) && !a->nl_before &&
                (tok_is(b, T_IDENT, "over") || tok_is(b, T_IDENT, "under") || tok_is(b, T_IDENT, "as"))) {
                advance(p);
                advance(p);
                advance(p);
                Token* c = advance(p);
                if (!is_fn_name(c->kind)) perr(p, c, "expected an operator after '%.*s'", b->len, b->start);
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
        /*
         * `target op= value` is a statement even when the lexer read `op=` as one operator (because a
         * function `+=`, or the comparison `<=`, exists). The right side is the whole expression, and the
         * interpreter chooses between an in-place `op=` and `target = op(target, value)`.
         */
        int save = p->pos;
        Node* e = parse_unary(p);
        Token* at = peek(p);
        bool opeq = is_assignable(e) && at->kind == T_OP && !at->nl_before && at->len >= 2 &&
                    at->start[at->len - 1] == '=' && !(at->len == 2 && at->start[0] == '=');
        if (!opeq && at->kind != T_ASSIGN && at->kind != T_COMPOUND) {
            p->pos = save;
            e = parse_expression(p);
            at = peek(p);
        }
        if (opeq || at->kind == T_ASSIGN || at->kind == T_COMPOUND) {
            if (!is_assignable(e)) perr(p, at, "invalid assignment target (expected a variable, field or index)");
            advance(p);
            n = new_node(S_ASSIGN, at->line);
            n->as.assign.target = e;
            if (at->kind != T_ASSIGN) {
                int oplen = at->kind == T_COMPOUND ? at->len : at->len - 1;
                n->as.assign.op = str_intern(p->vm, at->start, oplen);
                n->as.assign.opeq = str_intern(p->vm, at->start, oplen + 1);
                n->as.assign.ic = new_ic(p);
                n->as.assign.ic2 = new_ic(p);
            }
            if (e->kind == N_INDEX) n->as.assign.ic3 = new_ic(p);
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
    /* a file is: imports, then the code, then exports */
    enum { IMPORTS, CODE, EXPORTS } part = IMPORTS;
    int code_line = 0, export_line = 0;
    while (!check(&p, T_EOF)) {
        if (accept(&p, T_SEMI)) continue;
        if (check(&p, T_RBRACE)) perr(&p, peek(&p), "unexpected '}'");
        Token* start = peek(&p);
        Node* s = parse_statement(&p);
        if (s->kind == S_IMPORT) {
            if (part == CODE)
                perr(&p, start, "imports must come first in a file, before any other statement (code starts on line %d)",
                     code_line);
            if (part == EXPORTS)
                perr(&p, start, "imports must come first in a file, before any other statement (exports start on line %d)",
                     export_line);
        } else if (s->kind == S_EXPORT && !s->as.export_.decl) {
            if (part != EXPORTS) export_line = start->line;
            part = EXPORTS;
        } else {
            if (part == EXPORTS)
                perr(&p, start, "exports must be the last statements of a file (exports start on line %d)", export_line);
            if (part == IMPORTS) code_line = start->line;
            part = CODE;
        }
        nl_push(&block->as.block.stmts, s);
    }
    check_declarations(&p, &block->as.block.stmts);
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
    parse_params(&p, sig, NULL);
    if (accept(&p, T_ARROW)) sig->ret = parse_type(&p);
    free(tl.toks);
    return sig;
}
