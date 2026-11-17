#include "check_int.h"
#include "parser.h"



// Expressions and statements of the static checker. See check.c and md/STATIC_TYPING.md.

extern bool (*st_callable_fits_hook)(SType* want, SType* got);
void global_add(Checker* c, ObjString* name, CFunc* f);
void module_add_export(CModule* m, ObjString* name, ObjString* src);

static SType* check_lambda(Ctx* x, Node* n, SType* expect);
static SType* check_branchy(Ctx* x, Node* n, bool as_value, SType* expect);
static void want_yield(Ctx* x, SType* got);
static SType* check_scope_call(Ctx* x, Node* n, bool as_value);
static Checker* hook_checker;



// ---------------------------------------------------------------- helpers

static bool is_name(const ObjString* s, const char* text) {
    return s && str_equals((ObjString*)s, text);
}

static bool same_name_req(const ObjString* a, const ObjString* b) {
    return a == b || (a->len == b->len && memcmp(a->chars, b->chars, (size_t)a->len) == 0);
}

static SType* record_info(Ctx* x, Node* n, SType* t) {
    if (x->inst) {
        info_put(x->inst, n)->type = t;
    }

    count_type(x, t);

    return t;
}

static SType* join(SType* a, SType* b) {
    if (!a) {
        return b;
    }

    if (!b) {
        return a;
    }

    if (a->kind == ST_NEVER) {
        return b;
    }

    if (b->kind == ST_NEVER) {
        return a;
    }

    if (st_equal(a, b)) {
        return a;
    }

    // an empty array or none() takes the other branch's type
    if (a->kind == ST_ARRAY && b->kind == ST_ARRAY) {
        if (a->elem->kind == ST_NEVER) {
            return b;
        }

        if (b->elem->kind == ST_NEVER) {
            return a;
        }
    }

    if (a->kind == ST_ENUM && b->kind == ST_ENUM && a->decl == b->decl) {
        SType* r = st_new(ST_ENUM);
        *r = *a;
        r->args = pp_calloc((size_t)(a->nargs + 1), sizeof(SType*));
        for (int i = 0; i < a->nargs; ++i) {
            r->args[i] = join(a->args[i], i < b->nargs ? b->args[i] : NULL);
        }

        return r;
    }

    if (st_fit(a, b, NULL) >= 3 && b->kind != ST_INT) {
        return a;
    }

    if (st_fit(b, a, NULL) >= 3 && a->kind != ST_INT) {
        return b;
    }

    return st_union(a, b);
}

static bool contains_never(const SType* t) {
    if (!t) {
        return false;
    }

    switch (t->kind) {
        case ST_NEVER: return true;
        case ST_ARRAY: return contains_never(t->elem);
        case ST_ENUM:
        case ST_TUPLE:
        case ST_NATIVE:
            for (int i = 0; i < t->nargs; ++i) {
                if (contains_never(t->args[i])) {
                    return true;
                }
            }

            return false;
        default: return false;
    }
}

static bool is_option(Checker* c, const SType* t) {
    return t->kind == ST_ENUM && t->decl == c->option;
}

static bool is_error(Checker* c, const SType* t) {
    return t->kind == ST_ENUM && t->decl == c->error;
}

static SType* func_type(int nparams, SType** params, SType* ret) {
    SType* f = st_new(ST_FUNC);
    f->nargs = nparams;
    f->args = pp_calloc((size_t)(nparams + 1), sizeof(SType*));
    for (int i = 0; i < nparams; ++i) {
        f->args[i] = params[i];
    }

    f->elem = ret;

    return f;
}

static void format_args(StrBuf* sb, SType** args, int argc) {
    sb_putc(sb, '(');
    for (int i = 0; i < argc; ++i) {
        if (i) {
            sb_puts(sb, ", ");
        }

        st_print(args[i], sb);
    }

    sb_putc(sb, ')');
}

static bool any_dyn(SType** args, int argc) {
    for (int i = 0; i < argc; ++i) {
        if (st_is_dyn(args[i])) {
            return true;
        }
    }

    return false;
}

static bool is_open(const SType* t) {
    switch (t->kind) {
        case ST_UNION:
        case ST_UNKNOWN:
        case ST_IFACE:
        case ST_OBJECT:
        case ST_FUNCTION:
        case ST_PARAM: return true;
        default: return false;
    }
}



// ---------------------------------------------------------------- signatures

static int func_nparams(CFunc* f) {
    if (f->kind == CF_CTOR) {
        return f->en->variants[f->variant].npayload;
    }

    return f->sig ? f->sig->nparams : 0;
}

static bool func_variadic(CFunc* f) {
    return f->kind != CF_CTOR && f->sig && f->sig->variadic;
}

static void func_generics(CFunc* f, SBindings* b) {
    memset(b, 0, sizeof *b);
    if (f->kind == CF_CTOR) {
        b->count = f->en->ngenerics;
        for (int i = 0; i < b->count && i < PP_MAX_BINDINGS; ++i) {
            b->names[i] = f->en->generics[i];
        }

        return;
    }

    if (f->sig) {
        b->count = f->sig->ngenerics < PP_MAX_BINDINGS ? f->sig->ngenerics : PP_MAX_BINDINGS;
        for (int i = 0; i < b->count; ++i) {
            b->names[i] = f->sig->generics[i];
        }
    }

    for (int i = 0; i < f->nextra && b->count < PP_MAX_BINDINGS; ++i) {
        b->names[b->count++] = f->extra_generics[i];
    }

    // a requirement of a generic interface: the interface's parameters are free
    if (f->kind == CF_REQ && f->en) {
        for (int i = 0; i < f->en->ngenerics && b->count < PP_MAX_BINDINGS; ++i) {
            b->names[b->count++] = f->en->generics[i];
        }
    }
}

static SType* param_type(Ctx* x, CFunc* f, int i, SBindings* gen) {
    if (f->kind == CF_CTOR) {
        return resolve_type(x, f->en->variants[f->variant].payload[i], f->en->scope, gen);
    }

    Type* t = f->sig->params[i].type;
    if (!t) {
        return st_prim(ST_UNKNOWN);
    }

    return resolve_type(x, t, f->closure, gen);
}

static SType* declared_ret(Ctx* x, CFunc* f, SBindings* gen) {
    if (f->kind == CF_CTOR) {
        SType** args = pp_calloc((size_t)(f->en->ngenerics + 1), sizeof(SType*));
        for (int i = 0; i < f->en->ngenerics; ++i) {
            args[i] = gen && i < gen->count && gen->types[i] ? gen->types[i] : st_prim(ST_NEVER);
        }

        return enum_type(x->c, f->en, args, f->en->ngenerics);
    }

    if (!f->sig || !f->sig->ret) {
        return NULL;
    }

    return resolve_type(x, f->sig->ret, f->closure, gen);
}

// how well the arguments fit f, 0 = not at all; b gets the generic bindings
static int score_func(Ctx* x, CFunc* f, SType** args, int argc, SBindings* b) {
    const int np = func_nparams(f);
    if (func_variadic(f) ? argc < np : argc != np) {
        return 0;
    }

    if (f->kind == CF_USER && f->declared_only && !f->body && f->lambda) {
        return 0;
    }

    func_generics(f, b);
    SBindings gen = *b;
    int total = 1;
    for (int i = 0; i < np; ++i) {
        SType* want = param_type(x, f, i, &gen);
        const int s = st_fit(want, args[i], b);
        if (!s) {
            return 0;
        }

        total += s;
    }

    return total;
}

static void sig_to_static(Ctx* x, CFunc* f, StrBuf* sb) {
    if (f->kind == CF_CTOR) {
        sb_printf(sb, "%s(", f->name->chars);
        SBindings gen;
        func_generics(f, &gen);
        for (int i = 0; i < func_nparams(f); ++i) {
            if (i) {
                sb_puts(sb, ", ");
            }

            st_print(param_type(x, f, i, &gen), sb);
        }

        sb_putc(sb, ')');

        return;
    }

    sig_to_string(f->sig, sb);
}



// ---------------------------------------------------------------- instances

static bool params_equal(SType** a, SType** b, int n) {
    for (int i = 0; i < n; ++i) {
        if (!st_equal(a[i], b[i])) {
            return false;
        }
    }

    return true;
}

static CScope* function_scope(Checker* c, CInstance* inst) {
    CFunc* f = inst->fn;
    CScope* s = scope_new(f->closure, f->closure ? f->closure->module : inst->module, inst);
    s->module = f->module ? f->module : (f->closure ? f->closure->module : NULL);
    for (int i = 0; i < inst->bindings.count; ++i) {
        CSym* sym = scope_define(c, s, inst->bindings.names[i], SYM_TYPE, 0);
        sym->type = inst->bindings.types[i] ? inst->bindings.types[i] : st_prim(ST_UNKNOWN);
    }

    for (int i = 0; i < inst->nparams && f->sig && i < f->sig->nparams; ++i) {
        if (!f->sig->params[i].name) {
            continue;
        }

        CSym* sym = scope_define(c, s, f->sig->params[i].name, SYM_VAR, 0);
        sym->type = inst->params[i];
    }

    return s;
}

static void run_body(Checker* c, CInstance* inst) {
    CFunc* f = inst->fn;
    const char* saved_file = c->file;
    if (f->module && f->module->path && f->module->path[0] != 'n') {
        c->file = f->module->path;
    }

    if (f->sig && f->sig->file) {
        c->file = f->sig->file;
    }

    Ctx x = {0};
    x.c = c;
    x.inst = inst;
    x.scope = function_scope(c, inst);
    x.module = x.scope->module;
    x.ret_want = inst->infer_ret ? NULL : inst->ret;
    x.in_scope_fn = f->name && f->name->chars[0] == '_';
    SType* last = check_block(&x, f->body, false, x.ret_want);
    const bool implicit = f->sig && f->sig->implicit_result;
    if (implicit && x.dropped && !x.returned && x.ret_want && x.ret_want->kind != ST_VOID && x.ret_want->kind != ST_UNKNOWN &&
        x.ret_want->kind != ST_DYN) {
        cerr(c, x.dropped->line, "a lambda with a block body returns its value with 'return' (its last expression is "
                                 "not the result)");
    }

    if (!inst->infer_ret && implicit && last && inst->ret && inst->ret->kind != ST_VOID && inst->ret->kind != ST_UNKNOWN &&
        last->kind != ST_VOID && !st_fit(inst->ret, last, NULL)) {
        // an expression-bodied lambda's expression is its result
        StrBuf want, got;
        sb_init(&want);
        sb_init(&got);
        st_print(inst->ret, &want);
        st_print(last, &got);
        cerr(c, f->body->line, "return value of '%s': expected %s but got %s", f->name ? f->name->chars : "lambda",
             want.data, got.data);
    }

    if (inst->infer_ret) {
        SType* r = x.ret_got;
        if (f->sig && f->sig->implicit_result && last && last->kind != ST_VOID) {
            r = join(r, last);
        }

        if (!r || (!x.returned && !(f->sig && f->sig->implicit_result && last && last->kind != ST_VOID))) {
            r = r ? r : st_prim(ST_VOID);
        }

        inst->ret = r;
    }

    c->file = saved_file;
}

void check_body(Checker* c, CInstance* inst) {
    if (inst->checked || inst->checking || !inst->fn->body) {
        return;
    }

    inst->checking = true;
    if (inst->infer_ret) {
        // a recursive call sees `never` the first time; the second run sees the inferred type
        inst->ret = st_prim(ST_NEVER);
        run_body(c, inst);
        SType* first = inst->ret;
        memset(inst->slots, 0, sizeof(InfoSlot) * (size_t)inst->capslots);
        inst->nslots = 0;
        inst->ret = first;
        inst->infer_ret = false;
        run_body(c, inst);
        inst->infer_ret = true;
        inst->ret = first;
    } else {
        run_body(c, inst);
    }

    inst->checking = false;
    inst->checked = true;
}

CInstance* instantiate(Ctx* x, CFunc* f, SType** args, int argc, SBindings* b, int line) {
    Checker* c = x->c;
    const int np = func_nparams(f);
    SType** params = pp_calloc((size_t)(np + 1), sizeof(SType*));
    SBindings gen;
    func_generics(f, &gen);
    for (int i = 0; i < gen.count && b; ++i) {
        gen.types[i] = b->types[i];
    }

    for (int i = 0; i < np; ++i) {
        const bool typed = f->kind == CF_CTOR || (f->sig && f->sig->params[i].type);
        if (typed) {
            params[i] = subst(param_type(x, f, i, &gen), &gen);
            if (params[i]->kind == ST_PARAM) {
                params[i] = i < argc ? args[i] : st_prim(ST_UNKNOWN);
            }

            // a scope's block: the instance is for blocks that give this kind of value
            if (params[i]->kind == ST_BLOCK && i < argc && args[i]->kind == ST_BLOCK && args[i]->nargs) {
                SType* bt = st_new(ST_BLOCK);
                *bt = *params[i];
                bt->nargs = 1;
                bt->args = args[i]->args;
                params[i] = bt;
            }
        } else {
            params[i] = i < argc && args[i]->kind != ST_NEVER ? args[i] : st_prim(ST_UNKNOWN);
        }
    }

    for (int i = 0; i < f->ninsts; ++i) {
        if (params_equal(f->insts[i]->params, params, np)) {
            free(params);

            return f->insts[i];
        }
    }

    CInstance* inst = cinstance_new(c, f, f->module);
    inst->params = params;
    inst->nparams = np;
    inst->bindings = gen;
    GROW(f->insts, f->ninsts, f->capinsts);
    f->insts[f->ninsts++] = inst;
    SType* ret = declared_ret(x, f, &gen);
    if (ret) {
        inst->ret = subst(ret, &gen);
    } else if (f->kind == CF_USER && f->body) {
        inst->infer_ret = true;
        inst->ret = st_prim(ST_NEVER);
    } else {
        inst->ret = st_prim(ST_DYN);
    }

    if (f->kind == CF_USER && f->body) {
        check_body(c, inst);
    }

    return inst;
}

// a function used as a value: its type when the parameters are typed; also checks the body
SType* func_value_type(Ctx* x, CFunc* f) {
    if (f->kind == CF_USER && f->sig && f->sig->ngenerics == 0) {
        bool typed = true;
        for (int i = 0; i < f->sig->nparams; ++i) {
            if (!f->sig->params[i].type) {
                typed = false;
            }
        }

        if (typed) {
            const int np = f->sig->nparams;
            SType** params = pp_calloc((size_t)(np + 1), sizeof(SType*));
            for (int i = 0; i < np; ++i) {
                params[i] = param_type(x, f, i, NULL);
            }

            CInstance* inst = f->body ? instantiate(x, f, params, np, NULL, f->sig->line) : NULL;
            SType* ret = inst ? inst->ret : declared_ret(x, f, NULL);

            return func_type(np, params, ret ? ret : st_prim(ST_UNKNOWN));
        }
    }

    return NULL;
}



// ---------------------------------------------------------------- callables

static bool fits_callable(SType* want, SType* got);

static bool func_fits_type(Ctx* x, CFunc* f, SType* want) {
    if (want->variadic) {
        return true;
    }

    SBindings b;
    if (!score_func(x, f, want->args, want->nargs, &b)) {
        return false;
    }

    if (!want->elem || want->elem->kind == ST_VOID || want->elem->kind == ST_UNKNOWN) {
        return true;
    }

    SType* ret = declared_ret(x, f, &b);
    if (!ret) {
        return true; // not declared: decided when the body is checked
    }

    return st_fit(want->elem, subst(ret, &b), NULL) > 0;
}

static bool fits_callable(SType* want, SType* got) {
    Ctx x = {0};
    x.c = hook_checker;
    if (got->kind == ST_OVERLOAD) {
        for (int i = 0; i < got->funcs->count; ++i) {
            if (func_fits_type(&x, got->funcs->items[i], want)) {
                return true;
            }
        }

        return false;
    }

    if (got->kind != ST_FUNC) {
        return false;
    }

    if (want->variadic || got->variadic) {
        return true;
    }

    if (want->nargs != got->nargs) {
        return false;
    }

    for (int i = 0; i < want->nargs; ++i) {
        if (!st_fit(got->args[i], want->args[i], NULL)) {
            return false;
        }
    }

    if (!want->elem || want->elem->kind == ST_VOID || !got->elem) {
        return true;
    }

    return st_fit(want->elem, got->elem, NULL) > 0;
}

// the candidate sets of a call, best first: in scope, the receiver's module, impls, global
typedef struct {
    CFuncSet* sets[8];
    int count;
} Cands;

static void cands_add(Cands* cs, CFuncSet* s) {
    if (!s || cs->count >= 8) {
        return;
    }

    for (int i = 0; i < cs->count; ++i) {
        if (cs->sets[i] == s) {
            return;
        }
    }

    cs->sets[cs->count++] = s;
}

static TDecl* type_decl_of(const SType* t) {
    switch (t->kind) {
        case ST_RECORD:
        case ST_ENUM:
        case ST_NATIVE:
        case ST_IFACE: return t->decl;
        default: return NULL;
    }
}

// the functions named `name` exported by the module that declared the receiver's type
static CFuncSet* receiver_module_set(Ctx* x, SType* recv, ObjString* name) {
    TDecl* d = type_decl_of(recv);
    if (!d || !d->module || !d->module->scope) {
        return NULL;
    }

    CSym sym;
    if (!cmodule_export(x->c, d->module, name, &sym) || sym.kind != SYM_FUNCS) {
        return NULL;
    }

    return sym.funcs;
}

// the functions an `impl` recorded for the receiver's type, and an interface's requirements
static CFuncSet* receiver_impl_set(Ctx* x, SType* recv, ObjString* name) {
    Checker* c = x->c;
    CFuncSet* set = NULL;
    for (int i = 0; i < c->nimpl_fns; ++i) {
        CImplFn* fn = &c->impl_fns[i];
        if (fn->name != name && !str_equals(fn->name, name->chars)) {
            continue;
        }

        SBindings b;
        memset(&b, 0, sizeof b);
        const bool fits = recv->kind != ST_UNKNOWN && recv->kind != ST_DYN &&
                          (st_equal(fn->target, recv) || st_fit(fn->target, recv, &b) >= 2 ||
                           (type_decl_of(fn->target) && type_decl_of(fn->target) == type_decl_of(recv)));
        if (!fits) {
            continue;
        }

        if (!set) {
            set = pp_calloc(1, sizeof(CFuncSet));
            set->name = name;
        }

        GROW(set->items, set->count, set->cap);
        set->items[set->count++] = fn->fn;
    }

    return set;
}

static CFuncSet* iface_req_set(Ctx* x, SType* recv, ObjString* name);

static void call_cands(Ctx* x, Cands* cs, ObjString* name, SType* recv) {
    cs->count = 0;
    const CSym* sym = scope_lookup(x->scope, name);
    if (sym && sym->kind == SYM_FUNCS) {
        cands_add(cs, sym->funcs);
    }

    if (recv) {
        cands_add(cs, receiver_module_set(x, recv, name));
        cands_add(cs, receiver_impl_set(x, recv, name));
        if (recv->kind == ST_IFACE) {
            cands_add(cs, iface_req_set(x, recv, name));
        }
    }

    cands_add(cs, global_funcs(x->c, name));
}

static SType* builtin_result(Ctx* x, CFunc* f, SType** a, int argc, CCall* call, Node* n);
static SType* index_builtin(Ctx* x, Node* n, Node* key, SType** a, int argc, bool set);

// the result of calling f with args, once f was chosen
static SType* call_result(Ctx* x, CFunc* f, SType** args, int argc, SBindings* b, CCall* call, Node* n) {
    call->func = f;
    if (f->kind == CF_USER) {
        if (!f->body && f->declared_only && x->inst && x->inst == f->module->top && x->scope == f->closure) {
            StrBuf sb;
            sb_init(&sb);
            sig_to_string(f->sig, &sb);
            cerr(x->c, n->line, "function %s is called before its definition has run", sb.data);
        }

        if (!f->body) {
            SType* ret = declared_ret(x, f, b);

            return ret ? subst(ret, b) : dyn_type(x, n, "a function declared but not defined yet");
        }

        CInstance* inst = instantiate(x, f, args, argc, b, n->line);
        call->target = inst;

        return inst->ret;
    }

    if (f->kind == CF_NATIVE && f->builtin) {
        SType* r = builtin_result(x, f, args, argc, call, n);
        if (r) {
            return r;
        }
    }

    SType* ret = declared_ret(x, f, b);
    if (!ret) {
        return dyn_type(x, n, "a native function without a declared return type");
    }

    ret = subst(ret, b);
    if (ret->kind == ST_NATIVE && !ret->nargs && ret->decl->ngenerics && ret->decl->which != NT_CLASS) {
        // map() and set() give a bare Map / Set: its types come from the argument, or from its first use
        SType* t = st_new(ST_NATIVE);
        t->decl = ret->decl;
        t->nargs = ret->decl->ngenerics;
        t->args = pp_calloc((size_t)t->nargs + 1, sizeof(SType*));
        for (int i = 0; i < t->nargs; ++i) {
            t->args[i] = st_prim(ST_NEVER);
        }

        if (argc == 1 && args[0]->kind == ST_RECORD && t->nargs == 2) {
            st_record_fill(x->c, args[0]);
            SType* v = NULL;
            for (int i = 0; i < args[0]->nfields; ++i) {
                v = join(v, args[0]->ftypes[i]);
            }

            t->args[0] = st_prim(ST_STRING);
            t->args[1] = v ? v : st_prim(ST_NEVER);
        }

        return t;
    }

    return ret;
}

static CFunc** last_cands; // the candidates that fit, of the last pick() that needs dispatch
static int last_ncands;

// can a parameter of type want hold some value of static type got (a member of a union, a type that
// implements an interface, anything for unknown)? Those candidates take part in dynamic dispatch.
static bool could_fit(Checker* c, SType* want, SType* got) {
    if (st_fit(want, got, NULL)) {
        return true;
    }

    switch (got->kind) {
        case ST_UNKNOWN: return want->kind != ST_VOID;
        case ST_OBJECT: return want->kind == ST_RECORD;
        case ST_UNION:
            for (int i = 0; i < got->nargs; ++i) {
                if (could_fit(c, want, got->args[i])) {
                    return true;
                }
            }

            return false;
        case ST_IFACE: return st_implements(c, got->decl, got->args, got->nargs, want, NULL);
        default: return false;
    }
}

static bool could_take(Ctx* x, CFunc* f, SType** args, int argc) {
    const int np = func_nparams(f);
    if (func_variadic(f) ? argc < np : argc != np) {
        return false;
    }

    SBindings gen;
    func_generics(f, &gen);
    for (int i = 0; i < np; ++i) {
        if (!could_fit(x->c, param_type(x, f, i, &gen), args[i])) {
            return false;
        }
    }

    return true;
}

// Picks the best candidate like the runtime's resolve_overload: earlier sets win ties, and
// within a set the later declaration. NULL when none fits (quiet), else an error.
static CFunc* pick(Ctx* x, Cands* cs, SType** args, int argc, SBindings* out, bool* dispatch) {
    CFunc* best = NULL;
    int best_score = 0;
    int fitting = 0;
    for (int k = 0; k < cs->count; ++k) {
        CFuncSet* set = cs->sets[k];
        for (int i = set->count - 1; i >= 0; --i) {
            SBindings b;
            const int s = score_func(x, set->items[i], args, argc, &b);
            if (s) {
                fitting++;
            }

            if (s > best_score) {
                best_score = s;
                best = set->items[i];
                *out = b;
            }
        }
    }

    bool open = false;
    for (int i = 0; i < argc; ++i) {
        open = open || is_open(args[i]);
    }

    // a value of an open type (an interface, a union, unknown) may fit candidates that its static
    // type does not: they are tried first at runtime, as the more specific ones
    int total = 0;
    for (int k = 0; open && k < cs->count; ++k) {
        total += cs->sets[k]->count;
    }

    int possible = fitting;
    for (int k = 0; open && k < cs->count; ++k) {
        for (int i = 0; i < cs->sets[k]->count; ++i) {
            SBindings b;
            if (!score_func(x, cs->sets[k]->items[i], args, argc, &b) && could_take(x, cs->sets[k]->items[i], args, argc)) {
                possible++;
            }
        }
    }

    *dispatch = open && possible > 1;
    last_cands = NULL;
    last_ncands = 0;
    if (*dispatch) {
        // every candidate that may fit, in the order the runtime would prefer them
        last_cands = pp_calloc((size_t)total + 1, sizeof(CFunc*));
        int* scores = pp_calloc((size_t)total + 1, sizeof(int));
        for (int k = 0; k < cs->count; ++k) {
            for (int i = cs->sets[k]->count - 1; i >= 0; --i) {
                SBindings b;
                int s = score_func(x, cs->sets[k]->items[i], args, argc, &b);
                if (!s && could_take(x, cs->sets[k]->items[i], args, argc)) {
                    s = 100; // written for a type the value may have: beats a catch-all
                }

                if (!s) {
                    continue;
                }

                int at = last_ncands;
                while (at > 0 && scores[at - 1] < s) {
                    last_cands[at] = last_cands[at - 1];
                    scores[at] = scores[at - 1];
                    at--;
                }

                last_cands[at] = cs->sets[k]->items[i];
                scores[at] = s;
                last_ncands++;
            }
        }

        free(scores);
    }

    return best;
}

static PP_NORETURN void no_overload(Ctx* x, ObjString* name, Cands* cs, SType** args, int argc, int line) {
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "no overload of '%s' accepts ", name->chars);
    format_args(&sb, args, argc);
    sb_puts(&sb, "\n  candidates:");
    for (int k = 0; k < cs->count; ++k) {
        for (int i = 0; i < cs->sets[k]->count; ++i) {
            sb_puts(&sb, "\n    ");
            sig_to_static(x, cs->sets[k]->items[i], &sb);
        }
    }

    cerr(x->c, line, "%s", sb.data);
}

static SType* expect_for_lambda(Ctx* x, CFunc* f, int i, SType** args, int argc, SBindings* b);

static bool fully_known(const SType* t) {
    return t && !contains_never(t) && !st_has_param(t) && !st_is_dyn(t) && t->kind != ST_UNKNOWN;
}

// a variable set to [] (or none(), map()) takes the type its first use gives it
static SType* refine_var(Ctx* x, Node* arg, SType* want) {
    if (!arg || arg->kind != N_IDENT || !fully_known(want)) {
        return NULL;
    }

    CSym* sym = scope_lookup(x->scope, arg->as.str);
    if (!sym || sym->kind != SYM_VAR || !sym->pending || !st_fit(want, sym->type, NULL)) {
        return NULL;
    }

    sym->type = want;
    sym->pending = false;
    if (sym->init_info) {
        sym->init_info->type = want;
    }

    NodeInfo* ai = x->inst ? instance_info(x->inst, arg) : NULL;
    if (ai) {
        ai->type = want;
    }

    return want;
}

static void refine_args(Ctx* x, CFunc* f, SType** args, Node** nodes, int argc, SBindings* b) {
    if (!nodes) {
        return;
    }

    const bool push = f->kind == CF_NATIVE && f->builtin && (is_name(f->name, "push") || is_name(f->name, "insert"));
    for (int i = 0; i < argc && i < func_nparams(f); ++i) {
        if (!nodes[i] || nodes[i]->kind != N_IDENT || !contains_never(args[i])) {
            continue;
        }

        SType* want;
        if (push && i == 0) {
            want = st_array(args[argc - 1]);
        } else {
            SBindings gen;
            func_generics(f, &gen);
            want = subst(param_type(x, f, i, &gen), b);
        }

        SType* r = refine_var(x, nodes[i], want);
        if (r) {
            args[i] = r;
        }
    }
}

static bool is_lambda(Node* n) {
    return n->kind == N_FUNC;
}

// a call of the candidates with argument nodes (some may be lambdas typed from the chosen candidate)
static char* tname(const SType* t);
static bool literal_with_lambdas(Node* n);

// a record or array literal with a record literal somewhere inside
static bool nested_literal(Node* n) {
    NodeList* items = n->kind == N_OBJECT ? &n->as.object.values : n->kind == N_ARRAY ? &n->as.array.items : NULL;
    for (int i = 0; items && i < items->count; ++i) {
        if (items->items[i]->kind == N_OBJECT || nested_literal(items->items[i])) {
            return true;
        }
    }

    return false;
}

static bool is_map_type(const SType* t) {
    return t && t->kind == ST_NATIVE && t->decl && t->decl->which == NT_MAP;
}

// A record value where a Map<string, V> is expected (an argument, a declaration, an assignment, a
// return, a field of a literal): the expression is rewritten in place into map(expression), so every
// target sees a real map. Returns the type the value has now.
static SType* record_to_map(Ctx* x, Node* n, SType* want, SType* got) {
    if (!n || !is_map_type(want) || !got || got->kind != ST_RECORD || got->decl) {
        return got;
    }

    Node* inner = pp_malloc(sizeof(Node));
    *inner = *n;
    memset(&n->as, 0, sizeof n->as);
    n->kind = N_CALL;
    Node* id = pp_calloc(1, sizeof(Node));
    id->kind = N_IDENT;
    id->line = n->line;
    id->as.str = str_cintern(x->c->vm, "map");
    n->as.call.callee = id;
    n->as.call.ic = pp_calloc(1, sizeof(InlineCache));
    GROW(n->as.call.args.items, n->as.call.args.count, n->as.call.args.cap);
    n->as.call.args.items[n->as.call.args.count++] = inner;
    check_expr(x, n, NULL);

    return want;
}

// how far apart two names are (edits), for "did you mean"
static int name_distance(const char* a, const char* b) {
    const int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 31 || lb > 31) {
        return 99;
    }

    int d[32][32];
    for (int i = 0; i <= la; ++i) {
        d[i][0] = i;
    }

    for (int j = 0; j <= lb; ++j) {
        d[0][j] = j;
    }

    for (int i = 1; i <= la; ++i) {
        for (int j = 1; j <= lb; ++j) {
            const int sub = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            const int del = d[i - 1][j] + 1;
            const int ins = d[i][j - 1] + 1;
            d[i][j] = sub < del ? (sub < ins ? sub : ins) : (del < ins ? del : ins);
        }
    }

    return d[la][lb];
}

// A record literal given where a closed record type is expected: a field the type does not name is
// an error (with a rest field, ...Map<string, V> name, they go to the map instead).
static void check_literal_fields(Ctx* x, Node* lit, SType* want) {
    if (!lit || lit->kind != N_OBJECT || !want || want->kind != ST_RECORD) {
        return;
    }

    SType* rec = st_record_fill(x->c, want);
    if (rec->rest || !rec->fnames) {
        return;
    }

    for (int i = 0; i < lit->as.object.values.count; ++i) {
        ObjString* key = lit->as.object.keys[i];
        bool known = false;
        const char* best = NULL;
        int best_d = 3;
        for (int f = 0; f < rec->nfields && !known; ++f) {
            known = record_key_names(key, rec->fnames[f]);
            const int dist = name_distance(key->chars, rec->fnames[f]->chars);
            if (dist < best_d && dist <= key->len / 2) { // a suggestion only when it is close
                best_d = dist;
                best = rec->fnames[f]->chars;
            }
        }

        if (!known) {
            StrBuf hint;
            sb_init(&hint);
            if (best) {
                sb_printf(&hint, " (did you mean '%s'?)", best);
            }

            cerr(x->c, lit->line, "%s has no field '%s'%s", tname(want), key->chars, hint.data);
        }
    }
}

static SType* resolve_and_call(Ctx* x, Node* n, ObjString* name, Cands* cs, SType** args, Node** arg_nodes, int argc,
                               CCall* call) {
    if (any_dyn(args, argc)) {
        for (int i = 0; i < argc; ++i) {
            if (arg_nodes && arg_nodes[i] && is_lambda(arg_nodes[i]) && args[i]->kind == ST_FUNCTION) {
                check_lambda(x, arg_nodes[i], NULL);
            }
        }

        return dyn_type(x, n, "a call with a dynamic argument");
    }

    SBindings b;
    bool dispatch = false;
    CFunc* f = pick(x, cs, args, argc, &b, &dispatch);
    if (!f) {
        no_overload(x, name, cs, args, argc, n->line);
    }

    // lambdas take their types from the parameter they are passed to, also inside record and array literals
    // ({ each: xs, key: t -> t:id }): those are checked again against the parameter's type
    bool had_lambda = false;
    for (int i = 0; arg_nodes && i < argc; ++i) {
        if (arg_nodes[i] && literal_with_lambdas(arg_nodes[i])) {
            args[i] = check_expr(x, arg_nodes[i], expect_for_lambda(x, f, i, args, argc, &b));
            had_lambda = true;
            continue;
        }

        if (!arg_nodes[i] || !is_lambda(arg_nodes[i]) || args[i]->kind != ST_FUNCTION) {
            continue;
        }

        SType* want = expect_for_lambda(x, f, i, args, argc, &b);
        args[i] = check_lambda(x, arg_nodes[i], want);
        had_lambda = true;
    }

    if (had_lambda) {
        if (any_dyn(args, argc)) {
            return dyn_type(x, n, "a lambda the checker could not type");
        }

        if (!score_func(x, f, args, argc, &b)) {
            no_overload(x, name, cs, args, argc, n->line);
        }
    }

    for (int i = 0; arg_nodes && i < argc && !dispatch; ++i) {
        if (!arg_nodes[i]) {
            continue;
        }

        SType* want = expect_for_lambda(x, f, i, args, argc, &b);
        if (arg_nodes[i]->kind == N_OBJECT) {
            check_literal_fields(x, arg_nodes[i], want);
        }

        if (want && (want->kind == ST_RECORD || want->kind == ST_ARRAY) && nested_literal(arg_nodes[i])) {
            // a literal inside a literal ({ attrs: { a: "1" } }): checked again against the parameter's type, so
            // the inner one becomes what its field expects (a map, ...)
            args[i] = check_expr(x, arg_nodes[i], want);
        }

        // map(record) itself: its C body copies a record's fields (no map(map(...)) of the rewrite)
        const bool ctor = f->kind == CF_NATIVE && f->impl_name && is_name(f->impl_name, "std_map_new");
        if (!ctor) {
            args[i] = record_to_map(x, arg_nodes[i], want, args[i]);
        }
    }

    call->dispatch = dispatch;
    if (dispatch) {
        call->cands = last_cands;
        call->ncands = last_ncands;
    }

    refine_args(x, f, args, arg_nodes, argc, &b);

    return call_result(x, f, args, argc, &b, call, n);
}



// ---------------------------------------------------------------- builtins

static SType* elem_of(SType* t) {
    if (t->kind == ST_ARRAY) {
        return t->elem;
    }

    return st_prim(ST_DYN);
}

static SType* numeric_result(SType* a, SType* b) {
    if (a->kind == ST_NEVER || b->kind == ST_NEVER) {
        return st_prim(ST_NEVER);
    }

    if (a->kind == ST_INT && b->kind == ST_INT) {
        return st_prim(ST_INT);
    }

    if ((a->kind == ST_FLOAT || a->kind == ST_INT) && (b->kind == ST_FLOAT || b->kind == ST_INT)) {
        return st_prim(ST_FLOAT);
    }

    return st_union(st_prim(ST_INT), st_prim(ST_FLOAT));
}

static int arith_op(const char* name, SType* r) {
    const bool i = r->kind == ST_INT;
    const bool f = r->kind == ST_FLOAT;
    if (!i && !f) {
        return COP_NONE;
    }

    switch (name[0]) {
        case '+': return i ? COP_ADD_I : COP_ADD_F;
        case '-': return i ? COP_SUB_I : COP_SUB_F;
        case '*': return name[1] == '*' ? (i ? COP_POW_I : COP_POW_F) : (i ? COP_MUL_I : COP_MUL_F);
        case '/': return i ? COP_DIV_I : COP_DIV_F;
        case '%': return i ? COP_MOD_I : COP_MOD_F;
        default: return COP_NONE;
    }
}

static int compare_op(const char* name, SType* a, SType* b) {
    static const char* names[] = {"==", "!=", "<", ">", "<=", ">="};
    int k = -1;
    for (int i = 0; i < 6; ++i) {
        if (strcmp(name, names[i]) == 0) {
            k = i;
        }
    }

    if (k < 0) {
        return COP_NONE;
    }

    if (a->kind == ST_INT && b->kind == ST_INT) {
        return COP_EQ_I + k;
    }

    if ((a->kind == ST_INT || a->kind == ST_FLOAT) && (b->kind == ST_INT || b->kind == ST_FLOAT)) {
        return COP_EQ_F + k;
    }

    if (a->kind == ST_STRING && b->kind == ST_STRING) {
        return COP_EQ_S + k;
    }

    if (a->kind == ST_BOOL && b->kind == ST_BOOL && k < 2) {
        return COP_EQ_B + k;
    }

    return COP_NONE;
}

static SType* range_of(STKind elem) {
    SType* r = st_new(ST_RANGE);
    r->elem = st_prim(elem);

    return r;
}

static SType* fn_ret(Ctx* x, SType* f, SType** args, int argc, Node* n) {
    if (f->kind == ST_FUNC) {
        return f->elem ? f->elem : dyn_type(x, n, "a callback without a declared return type");
    }

    if (f->kind == ST_OVERLOAD) {
        Cands cs = {{f->funcs}, 1};
        SBindings b;
        bool dispatch;
        CFunc* fn = pick(x, &cs, args, argc, &b, &dispatch);
        if (!fn) {
            return dyn_type(x, n, "a function value that does not fit its use");
        }

        CCall call = {0};

        return call_result(x, fn, args, argc, &b, &call, n);
    }

    return dyn_type(x, n, "a call of a value of type 'function'");
}

// The static result of a builtin whose runtime signature does not say it precisely
// (number arithmetic, the array functions written with `any`). NULL: use the signature.
static SType* builtin_result(Ctx* x, CFunc* f, SType** a, int argc, CCall* call, Node* n) {
    const char* name = f->name->chars;
    if (strcmp(name, "[]") == 0 || strcmp(name, "[]=") == 0) {
        // xs.[](i): the same as xs[i]
        Node* key = n->kind == N_METHOD && n->as.method.args.count ? n->as.method.args.items[0] : NULL;

        return index_builtin(x, n, key, a, argc, name[2] == '=');
    }

    if (argc == 2 && (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 || strcmp(name, "*") == 0 ||
                      strcmp(name, "/") == 0 || strcmp(name, "%") == 0 || strcmp(name, "**") == 0)) {
        if (st_is_numeric(a[0]) && st_is_numeric(a[1])) {
            SType* r = numeric_result(a[0], a[1]);
            call->op = arith_op(name, r);

            return r;
        }

        if (a[0]->kind == ST_STRING && a[1]->kind == ST_STRING && name[0] == '+') {
            call->op = COP_CONCAT;
        }

        return st_prim(ST_STRING);
    }

    if (argc == 1 && strcmp(name, "-") == 0) {
        call->op = a[0]->kind == ST_INT ? COP_NEG_I : a[0]->kind == ST_FLOAT ? COP_NEG_F : COP_NONE;

        return a[0];
    }

    if (argc == 2 && strlen(name) <= 2 && (name[0] == '=' || name[0] == '!' || name[0] == '<' || name[0] == '>')) {
        call->op = compare_op(name, a[0], a[1]);

        return st_prim(ST_BOOL);
    }

    if (strcmp(name, "!") == 0) {
        call->op = COP_NOT;

        return st_prim(ST_BOOL);
    }

    if (strcmp(name, "#") == 0) {
        call->op = a[0]->kind == ST_STRING ? COP_LEN_S : a[0]->kind == ST_ARRAY ? COP_LEN_A : COP_NONE;

        return st_prim(ST_INT);
    }

    if (strcmp(name, "...") == 0 || strcmp(name, "...=") == 0) {
        // the built-in numeric ranges: ints count in ints, any float makes a float range
        bool ints = true;
        for (int i = 0; i < argc; ++i) {
            ints = ints && a[i]->kind == ST_INT;
        }

        if (!ints) {
            return range_of(ST_FLOAT);
        }

        const bool incl = name[3] == '=';
        call->op = argc == 1 ? (incl ? COP_RANGE_TO_INCL : COP_RANGE_TO) : (incl ? COP_RANGE_INCL : COP_RANGE);

        return range_of(ST_INT);
    }

    if (strcmp(name, "decode") == 0 && argc == 2) {
        // decode(m, T): the second argument is a record type, the result an Error<T>
        TDecl* d = a[1]->kind == ST_TYPE ? a[1]->decl : NULL;
        if (!d || d->kind != TD_ALIAS || !d->def || d->def->kind != TY_RECORD) {
            cerr(x->c, n->line, "decode(m, T): T must be the name of a record type, got %s", tname(a[1]));
        }

        const bool map = a[0]->kind == ST_NATIVE && a[0]->decl && a[0]->decl->which == NT_MAP &&
                         (!a[0]->nargs || a[0]->args[0]->kind == ST_STRING || st_is_dyn(a[0]->args[0]));
        if (!map && !st_is_dyn(a[0]) && a[0]->kind != ST_UNKNOWN) {
            cerr(x->c, n->line, "decode(m, T): m must be a Map<string, unknown>, got %s", tname(a[0]));
        }

        SType* ea[2] = {named_record(x->c, d), st_prim(ST_STRING)};

        return enum_type(x->c, x->c->error, ea, 2);
    }

    if (strcmp(name, "range") == 0) {
        bool ints = true;
        for (int i = 0; i < argc; ++i) {
            ints = ints && a[i]->kind == ST_INT;
        }

        bool floats = true;
        for (int i = 0; i < argc; ++i) {
            floats = floats && (a[i]->kind == ST_INT || a[i]->kind == ST_FLOAT);
        }

        return ints ? range_of(ST_INT) : floats ? range_of(ST_FLOAT) : st_prim(ST_RANGE);
    }

    static const char* to_string[] = {"str", "format", "join", "upper", "lower", "trim", "replace", "chr", "type_of",
                                      "get_type", NULL};
    for (int i = 0; to_string[i]; ++i) {
        if (strcmp(name, to_string[i]) == 0) {
            return st_prim(ST_STRING);
        }
    }

    static const char* to_bool[] = {"contains", "starts_with", "ends_with", "has", "remove_key", "defined", NULL};
    for (int i = 0; to_bool[i]; ++i) {
        if (strcmp(name, to_bool[i]) == 0) {
            return st_prim(ST_BOOL);
        }
    }

    if (strcmp(name, "int") == 0 || strcmp(name, "ord") == 0) {
        return st_prim(ST_INT);
    }

    if (strcmp(name, "float") == 0 || strcmp(name, "clock") == 0) {
        return st_prim(ST_FLOAT);
    }

    if (strcmp(name, "parse_int") == 0) {
        return option_of(x->c, st_prim(ST_INT));
    }

    if (strcmp(name, "parse_float") == 0) {
        return option_of(x->c, st_prim(ST_FLOAT));
    }

    if (strcmp(name, "index_of") == 0) {
        return option_of(x->c, st_prim(ST_INT));
    }

    if (strcmp(name, "push") == 0 || strcmp(name, "insert") == 0 || strcmp(name, "assert") == 0) {
        return st_prim(ST_VOID);
    }

    if (strcmp(name, "pop") == 0) {
        return option_of(x->c, elem_of(a[0]));
    }

    if (strcmp(name, "remove") == 0 && a[0]->kind == ST_ARRAY) {
        return elem_of(a[0]);
    }

    static const char* same_array[] = {"concat", "multiply", "repeat", "slice", "reverse", "sort", NULL};
    for (int i = 0; same_array[i]; ++i) {
        if (strcmp(name, same_array[i]) == 0) {
            if (a[0]->kind == ST_STRING) {
                return st_prim(ST_STRING);
            }

            if (strcmp(name, "concat") == 0 && argc == 2) {
                return join(a[0], a[1]);
            }

            return a[0];
        }
    }

    if (strcmp(name, "split") == 0 || strcmp(name, "chars") == 0 || strcmp(name, "keys") == 0) {
        return st_array(st_prim(ST_STRING));
    }

    if (strcmp(name, "values") == 0) {
        return dyn_type(x, n, "values(object): the fields of an object are not typed");
    }

    if (strcmp(name, "copy") == 0) {
        return a[0];
    }

    if (strcmp(name, "tuple") == 0) {
        SType* t = st_new(ST_TUPLE);
        t->nargs = argc;
        t->args = pp_calloc((size_t)argc, sizeof(SType*));
        for (int i = 0; i < argc; ++i) {
            t->args[i] = a[i];
        }

        return t;
    }

    if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0 || strcmp(name, "clamp") == 0) {
        SType* r = NULL;
        for (int i = 0; i < argc; ++i) {
            r = join(r, a[i]);
        }

        return r;
    }

    if (strcmp(name, "panic") == 0) {
        return st_prim(ST_NEVER);
    }

    if (strcmp(name, "generator_next") == 0 && argc == 1) {
        return is_generator(x->c, a[0]) && a[0]->nargs ? option_of(x->c, a[0]->args[0])
                                                       : dyn_type(x, n, "next() of a value that is not a Generator");
    }

    if (strcmp(name, "fields") == 0 && argc == 1) {
        // fields(record): tuple(name, value) for each field; the values have the fields' joined type
        SType* v = st_prim(ST_UNKNOWN);
        if (a[0]->kind == ST_RECORD) {
            st_record_fill(x->c, a[0]);
            SType* r = NULL;
            for (int i = 0; i < a[0]->nfields; ++i) {
                r = join(r, a[0]->ftypes[i]);
            }

            v = r ? r : st_prim(ST_NEVER);
        }

        SType* t = st_new(ST_TUPLE);
        t->nargs = 2;
        t->args = pp_calloc(3, sizeof(SType*));
        t->args[0] = st_prim(ST_STRING);
        t->args[1] = v;

        return generator_of(x->c, t);
    }

    if (strcmp(name, "call") == 0 && argc >= 1) {
        return fn_ret(x, a[0], a + 1, argc - 1, n);
    }

    if (strcmp(name, "lift") == 0) {
        if ((is_option(x->c, a[0]) || is_error(x->c, a[0])) && a[0]->nargs) {
            return a[0]->args[0];
        }

        return dyn_type(x, n, "lift() of a value that is not an Option or an Error");
    }

    if (strcmp(name, "enum_value") == 0) {
        if (a[0]->kind == ST_ENUM) {
            if (a[0]->decl->value_type) {
                return resolve_type(x, a[0]->decl->value_type, a[0]->decl->scope, NULL);
            }

            return st_prim(ST_INT);
        }

        return dyn_type(x, n, "enum_value() of a value that is not an enum");
    }

    return NULL;
}

// the callback type a lambda argument of a builtin gets: map(xs, x -> ...) types x from xs
static SType* expect_for_lambda(Ctx* x, CFunc* f, int i, SType** args, int argc, SBindings* b) {
    if (f->kind == CF_NATIVE && f->builtin) {
        const char* name = f->name->chars;
        SType* e = argc ? elem_of(args[0]) : st_prim(ST_DYN);
        if (strcmp(name, "sort") == 0 && i == 1) {
            SType* two[2] = {e, e};

            return func_type(2, two, NULL);
        }

        // xs.push(x -> ...) / xs.insert(i, x -> ...) into an array of callbacks: the element's type
        if (e->kind == ST_FUNC && ((strcmp(name, "push") == 0 && i == 1) || (strcmp(name, "insert") == 0 && i == 2))) {
            return e;
        }
    }

    if (i >= func_nparams(f)) {
        return NULL;
    }

    SBindings gen;
    func_generics(f, &gen);
    SType* want = param_type(x, f, i, &gen);

    return subst(want, b);
}



// ---------------------------------------------------------------- interfaces

static CFuncSet* iface_req_set(Ctx* x, SType* recv, ObjString* name) {
    TDecl* d = recv->decl;
    CFuncSet* set = NULL;
    // in the requirements, the interface's name is the interface with its own parameters, which the receiver binds
    CScope* rs = scope_new(d->scope, d->module, NULL);
    CSym* self = scope_define(x->c, rs, d->name, SYM_TYPE, 0);
    SType* it = st_new(ST_IFACE);
    it->decl = d;
    it->nargs = d->ngenerics;
    it->args = pp_calloc((size_t)(d->ngenerics + 1), sizeof(SType*));
    for (int i = 0; i < d->ngenerics; ++i) {
        it->args[i] = st_new(ST_PARAM);
        it->args[i]->name = d->generics[i];
    }

    self->type = it;
    for (int i = 0; i < d->nreqs; ++i) {
        if (!same_name_req(d->reqs[i]->name, name)) {
            continue;
        }

        if (!set) {
            set = pp_calloc(1, sizeof(CFuncSet));
            set->name = name;
        }

        CFunc* f = func_new(x->c, CF_REQ, name, d->reqs[i], rs);
        f->en = d;
        GROW(set->items, set->count, set->cap);
        set->items[set->count++] = f;
    }

    for (int p = 0; p < d->nparents; ++p) {
        SType* parent = resolve_type(x, d->parents[p], d->scope, NULL);
        if (parent->kind != ST_IFACE) {
            continue;
        }

        CFuncSet* more = iface_req_set(x, parent, name);
        for (int i = 0; more && i < more->count; ++i) {
            if (!set) {
                set = pp_calloc(1, sizeof(CFuncSet));
                set->name = name;
            }

            GROW(set->items, set->count, set->cap);
            set->items[set->count++] = more->items[i];
        }
    }

    return set;
}



// ---------------------------------------------------------------- names

static char* tname(const SType* t) {
    StrBuf sb;
    sb_init(&sb);
    sb_puts(&sb, "");
    st_print(t, &sb);

    return sb.data; // lives as long as the message that prints it
}

static ObjString* lambda_name; // the variable, parameter or field a lambda is stored in, for messages

static SType* overload_type(CFuncSet* set) {
    SType* t = st_new(ST_OVERLOAD);
    t->funcs = set;

    return t;
}

static SType* sym_value_type(Ctx* x, const CSym* sym) {
    switch (sym->kind) {
        case SYM_VAR: return sym->type ? sym->type : st_prim(ST_DYN);
        case SYM_FUNCS: return overload_type(sym->funcs);
        case SYM_MODULE: {
            SType* t = st_new(ST_MODULE);
            t->module = sym->module;

            return t;
        }
        case SYM_TYPE: {
            SType* t = st_new(ST_TYPE);
            t->decl = sym->tdecl;
            t->elem = sym->type;

            return t;
        }
    }

    return st_prim(ST_DYN);
}

static void fold_type_of(Ctx* x, Node* n, SType* t);



// ---------------------------------------------------------------- lambdas

static SType* check_lambda(Ctx* x, Node* n, SType* expect) {
    Checker* c = x->c;
    Signature* sig = n->as.func.sig;
    const int np = sig->nparams;
    if (expect && (expect->kind != ST_FUNC || (!expect->variadic && expect->nargs != np))) {
        expect = NULL;
    }

    SType** params = pp_calloc((size_t)(np + 1), sizeof(SType*));
    for (int i = 0; i < np; ++i) {
        if (sig->params[i].type) {
            params[i] = resolve_type(x, sig->params[i].type, x->scope, NULL);
        } else if (expect && !expect->variadic && expect->args[i]->kind != ST_PARAM && expect->args[i]->kind != ST_NEVER) {
            params[i] = expect->args[i];
        } else {
            return record_info(x, n, dyn_type(x, n, "a lambda whose parameter types are not known"));
        }
    }

    Signature* named = sig;
    if (lambda_name) {
        named = pp_malloc(sizeof(Signature));
        *named = *sig;
        named->name = lambda_name;
    }

    CFunc* f = func_new(c, CF_USER, named->name, named, x->scope);
    f->body = n->as.func.body;
    f->lambda = true;
    f->module = x->module;
    CInstance* inst = cinstance_new(c, f, x->module);
    inst->params = params;
    inst->nparams = np;
    if (sig->ret) {
        inst->ret = resolve_type(x, sig->ret, x->scope, NULL);
    } else if (expect && expect->elem && expect->elem->kind != ST_VOID && expect->elem->kind != ST_UNKNOWN &&
               expect->elem->kind != ST_PARAM && !contains_never(expect->elem)) {
        inst->ret = expect->elem;
    } else {
        inst->infer_ret = true;
        inst->ret = st_prim(ST_NEVER);
    }

    GROW(f->insts, f->ninsts, f->capinsts);
    f->insts[f->ninsts++] = inst;
    ObjString* saved_name = lambda_name;
    lambda_name = NULL;
    check_body(c, inst);
    lambda_name = saved_name;
    if (x->inst) {
        info_put(x->inst, n)->call.target = inst;
    }

    SType* t = func_type(np, params, inst->ret);
    t->fnames = pp_calloc((size_t)(np + 1), sizeof(ObjString*));
    for (int i = 0; i < np; ++i) {
        t->fnames[i] = sig->params[i].name;
    }

    t->name = named->name;

    return record_info(x, n, t);
}



// ---------------------------------------------------------------- fields

static CFunc* find_ctor(Ctx* x, TDecl* en, ObjString* name) {
    const CSym* sym = scope_lookup(en->scope, name);
    if (!sym || sym->kind != SYM_FUNCS) {
        return NULL;
    }

    for (int i = 0; i < sym->funcs->count; ++i) {
        CFunc* f = sym->funcs->items[i];
        if (f->kind == CF_CTOR && f->en == en) {
            return f;
        }
    }

    return NULL;
}

static SType* field_of(Ctx* x, Node* n, SType* obj, ObjString* name) {
    Checker* c = x->c;
    switch (obj->kind) {
        case ST_DYN: return obj;
        case ST_NEVER: return obj;
        case ST_RECORD: {
            st_record_fill(c, obj);
            for (int i = 0; i < obj->nfields; ++i) {
                if (same_name_req(obj->fnames[i], name)) {
                    return obj->ftypes[i];
                }
            }

            cerr(c, n->line, "%s has no field '%s' (the fields of a record are fixed; a value declared as "
                             "'object' or a Map can take any key)", tname(obj), name->chars);
        }
        case ST_OBJECT: return st_prim(ST_UNKNOWN); // any record: a field can hold anything
        case ST_UNKNOWN:
            cerr(c, n->line, "cannot read field '%s' of a value of type unknown; get a typed value first: "
                             "if some(o) = value as object { o:%s }", name->chars, name->chars);
        case ST_PARAM: return dyn_type(x, n, "a field of a value of a generic type");
        case ST_IFACE:
            cerr(c, n->line, "cannot access field '%s' on a value of interface type %s; get the type first: "
                             "(value as Type).unwrap():%s", name->chars, tname(obj), name->chars);
        case ST_MODULE: {
            CSym sym;
            if (!cmodule_export(c, obj->module, name, &sym)) {
                cerr(c, n->line, "module '%s' does not export '%s'", obj->module->name->chars, name->chars);
            }

            return sym_value_type(x, &sym);
        }
        case ST_TYPE: {
            TDecl* d = obj->decl;
            if (!d || d->kind != TD_ENUM) {
                return dyn_type(x, n, "a field of a type that is not an enum");
            }

            for (int i = 0; i < d->nvariants; ++i) {
                if (!same_name_req(d->variants[i].name, name)) {
                    continue;
                }

                if (d->variants[i].is_ctor) {
                    CFunc* f = find_ctor(x, d, name);
                    if (!f) {
                        return dyn_type(x, n, "an enum constructor out of scope");
                    }

                    CFuncSet* set = pp_calloc(1, sizeof(CFuncSet));
                    set->name = name;
                    GROW(set->items, set->count, set->cap);
                    set->items[set->count++] = f;

                    return overload_type(set);
                }

                return enum_type(c, d, NULL, 0);
            }

            cerr(c, n->line, "enum '%s' has no variant '%s'", d->name->chars, name->chars);
        }
        case ST_RANGE:
            if (is_name(name, "step") && obj->elem && !st_is_numeric(obj->elem)) {
                cerr(c, n->line, "only a range of numbers has a step; %s has none", tname(obj));
            }

            if (is_name(name, "start") || is_name(name, "end") || is_name(name, "step")) {
                return obj->elem ? obj->elem : st_union(st_prim(ST_INT), st_prim(ST_FLOAT)); // bare Range: the numeric ranges
            }

            if (is_name(name, "inclusive")) {
                return st_prim(ST_BOOL);
            }

            cerr(c, n->line, "range has no field '%s' (it has start, end, step and inclusive)", name->chars);
        case ST_UNION: {
            SType* r = NULL;
            for (int i = 0; i < obj->nargs; ++i) {
                if (obj->args[i]->kind != ST_RECORD && obj->args[i]->kind != ST_OBJECT) {
                    return dyn_type(x, n, "a field of a union that is not made of records");
                }

                r = join(r, field_of(x, n, obj->args[i], name));
            }

            return r;
        }
        default: cerr(c, n->line, "cannot access field '%s' on a value of type %s", name->chars, tname(obj));
    }
}



// ---------------------------------------------------------------- indexing

// the element type of a native class (typed arrays), NULL = not known
static SType* native_element(Checker* c, TDecl* d) {
    if (!d || d->kind != TD_NATIVE || d->which != NT_CLASS || !d->module) {
        return NULL;
    }

    ObjNativeType* nt = vm_native_type(c->vm, d->module->name->chars, d->name->chars);
    if (!nt || !nt->cls || !nt->cls->element) {
        return NULL;
    }

    return strcmp(nt->cls->element, "float") == 0 ? st_prim(ST_FLOAT) : st_prim(ST_INT);
}

static SType* index_builtin(Ctx* x, Node* n, Node* key, SType** a, int argc, bool set) {
    Checker* c = x->c;
    SType* obj = a[0];
    if (argc != (set ? 3 : 2)) {
        return dyn_type(x, n, "an index with several keys on a built-in collection");
    }

    switch (obj->kind) {
        case ST_ARRAY:
            if (a[1]->kind != ST_INT && a[1]->kind != ST_NEVER) {
                return NULL;
            }

            if (set) {
                if (!st_fit(obj->elem, a[2], NULL) && obj->elem->kind != ST_NEVER) {
                    cerr(c, n->line, "cannot store %s in %s", tname(a[2]), tname(obj));
                }

                return st_prim(ST_VOID);
            }

            return obj->elem;
        case ST_STRING:
            if (set || a[1]->kind != ST_INT) {
                return NULL;
            }

            return st_prim(ST_STRING);
        case ST_TUPLE:
            if (set) {
                cerr(c, n->line, "a tuple cannot be changed; build a new one with tuple(...)");
            }

            if (key && key->kind == N_INT) {
                int64_t i = key->as.ival < 0 ? key->as.ival + obj->nargs : key->as.ival;
                if (i < 0 || i >= obj->nargs) {
                    cerr(c, n->line, "index %lld out of range (length %d)", (long long)key->as.ival, obj->nargs);
                }

                return obj->args[i];
            }

            {
                SType* r = NULL;
                for (int i = 0; i < obj->nargs; ++i) {
                    r = join(r, obj->args[i]);
                }

                return r;
            }
        case ST_RECORD:
            if (a[1]->kind != ST_STRING) {
                return NULL;
            }

            if (key && key->kind == N_STRING) {
                SType* f = field_of(x, n, obj, key->as.str);
                if (set) {
                    if (!st_fit(f, a[2], NULL)) {
                        cerr(c, n->line, "field '%s' is %s, cannot store %s", key->as.str->chars, tname(f), tname(a[2]));
                    }

                    return st_prim(ST_VOID);
                }

                return f;
            }

            return dyn_type(x, n, "a record indexed with a computed key");
        case ST_OBJECT: return set ? st_prim(ST_VOID) : st_prim(ST_UNKNOWN);
        case ST_NATIVE: {
            SType* e = native_element(c, obj->decl);
            if (!e) {
                return set ? st_prim(ST_VOID) : dyn_type(x, n, "indexing a native class without a typed []");
            }

            if (set) {
                if (!st_fit(e, a[2], NULL)) {
                    cerr(c, n->line, "%s holds %ss, got %s", obj->decl->name->chars, tname(e), tname(a[2]));
                }

                return st_prim(ST_VOID);
            }

            return e;
        }
        case ST_DYN:
        case ST_UNKNOWN:
        case ST_PARAM: return set ? st_prim(ST_VOID) : dyn_type(x, n, "indexing a value whose type is not known");
        case ST_UNION: {
            // every member must be indexable the same way; the result is any of theirs
            SType* r = NULL;
            for (int i = 0; i < obj->nargs; ++i) {
                SType** each = pp_calloc((size_t)(argc + 1), sizeof(SType*));
                memcpy(each, a, sizeof(SType*) * (size_t)argc);
                each[0] = obj->args[i];
                SType* t = index_builtin(x, n, key, each, argc, set);
                if (!t) {
                    return NULL;
                }

                r = join(r, t);
            }

            return r;
        }
        default: return NULL;
    }
}

static SType* check_index(Ctx* x, Node* n, Node* obj_node, NodeList* keys, Node* value, CCall* call) {
    Checker* c = x->c;
    const bool set = value != NULL;
    const int argc = keys->count + 1 + (set ? 1 : 0);
    SType** args = pp_calloc((size_t)(argc + 1), sizeof(SType*));
    args[0] = check_expr(x, obj_node, NULL);
    for (int i = 0; i < keys->count; ++i) {
        args[i + 1] = check_expr(x, keys->items[i], NULL);
    }

    if (set) {
        SType* want = args[0]->kind == ST_ARRAY ? args[0]->elem : NULL;
        args[argc - 1] = check_expr(x, value, want);
        if (args[0]->kind == ST_ARRAY && args[0]->elem->kind == ST_NEVER) {
            SType* r = refine_var(x, obj_node, st_array(args[argc - 1]));
            args[0] = r ? r : args[0];
        }
    }

    if (any_dyn(args, argc)) {
        return dyn_type(x, n, "an index with a dynamic operand");
    }

    ObjString* name = set ? c->s_index_set : c->s_index;
    Cands cs;
    call_cands(x, &cs, name, args[0]);
    SBindings b;
    bool dispatch = false;
    CFunc* f = cs.count ? pick(x, &cs, args, argc, &b, &dispatch) : NULL;
    if (f && !(f->kind == CF_NATIVE && f->builtin)) {
        call->dispatch = dispatch;

        return call_result(x, f, args, argc, &b, call, n);
    }

    SType* r = index_builtin(x, n, keys->count ? keys->items[0] : NULL, args, argc, set);
    if (r) {
        call->func = f;

        return r;
    }

    no_overload(x, name, &cs, args, argc, n->line);
}



// ---------------------------------------------------------------- operator chains

static int chain_build(ChainInfo* ci, double* precs, int* pos, double minp) {
    int left = -(*pos) - 1;
    while (*pos < ci->nops && precs[*pos] >= minp) {
        const int k = *pos;
        const double p = precs[k];
        (*pos)++;
        const int right = chain_build(ci, precs, pos, p + 1e-9);
        ci->left[k] = left;
        ci->right[k] = right;
        left = k;
    }

    return left;
}

static SType* chain_at(Ctx* x, Node* n, ChainInfo* ci, int enc) {
    Checker* c = x->c;
    if (enc < 0) {
        return check_expr(x, n->as.chain.operands.items[-enc - 1], NULL);
    }

    const int k = enc;
    ObjString* op = n->as.chain.ops[k];
    SType* l = chain_at(x, n, ci, ci->left[k]);
    SType* r = chain_at(x, n, ci, ci->right[k]);
    CCall* call = &ci->calls[k];
    if (op == c->s_and || op == c->s_or) {
        for (int i = 0; i < 2; ++i) {
            const SType* t = i ? r : l;
            if (t->kind != ST_BOOL && t->kind != ST_DYN && t->kind != ST_NEVER) {
                cerr(c, n->line, "operand of '&&' / '||' must be a bool, got %s", tname(t));
            }
        }

        call->type = st_prim(ST_BOOL);

        return call->type;
    }

    SType* args[2] = {l, r};
    Cands cs;
    call_cands(x, &cs, op, l);
    if (!cs.count) {
        cerr(c, n->line, "operator '%s' is not defined", op->chars);
    }

    call->type = resolve_and_call(x, n, op, &cs, args, NULL, 2, call);

    return call->type;
}

static SType* check_chain(Ctx* x, Node* n) {
    Checker* c = x->c;
    ChainInfo* ci = pp_calloc(1, sizeof(ChainInfo));
    ci->nops = n->as.chain.nops;
    ci->left = pp_calloc((size_t)ci->nops + 1, sizeof(int));
    ci->right = pp_calloc((size_t)ci->nops + 1, sizeof(int));
    ci->calls = pp_calloc((size_t)ci->nops + 1, sizeof(CCall));
    double* precs = pp_calloc((size_t)ci->nops + 1, sizeof(double));
    for (int k = 0; k < ci->nops; ++k) {
        precs[k] = prec_of(c, n->as.chain.ops[k]);
    }

    int pos = 0;
    ci->root = chain_build(ci, precs, &pos, -1e300);
    free(precs);
    if (x->inst) {
        info_put(x->inst, n)->chain = ci;
    }

    return chain_at(x, n, ci, ci->root);
}



// ---------------------------------------------------------------- calls

static bool untyped_lambda(Node* n) {
    if (n->kind != N_FUNC) {
        return false;
    }

    for (int i = 0; i < n->as.func.sig->nparams; ++i) {
        if (!n->as.func.sig->params[i].type) {
            return true;
        }
    }

    return !n->as.func.sig->ret && n->as.func.lambda;
}

static SType* lambda_placeholder(Node* lambda) {
    SType* placeholder = st_new(ST_FUNCTION);
    placeholder->arity = lambda->as.func.sig->nparams + 1;

    return placeholder;
}

// a record or array literal with an untyped lambda in it ({ key: t -> t:id }, [t -> t:title]), at any depth:
// its lambdas take their types from the parameter the literal is passed to, once the call is resolved
static bool literal_with_lambdas(Node* n) {
    NodeList* items = n->kind == N_OBJECT ? &n->as.object.values : n->kind == N_ARRAY ? &n->as.array.items : NULL;
    for (int i = 0; items && i < items->count; ++i) {
        if (untyped_lambda(items->items[i]) || literal_with_lambdas(items->items[i])) {
            return true;
        }
    }

    return false;
}

// the type of such a literal while the call is resolved: its lambdas as placeholders (they fit a callback
// type by their parameter count), every other value checked as usual
static SType* literal_placeholder(Ctx* x, Node* n) {
    if (untyped_lambda(n)) {
        return lambda_placeholder(n);
    }

    if (n->kind == N_ARRAY && literal_with_lambdas(n)) {
        SType* elem = NULL;
        for (int i = 0; i < n->as.array.items.count; ++i) {
            SType* e = literal_placeholder(x, n->as.array.items.items[i]);
            elem = elem && e->kind == ST_FUNCTION && elem->kind == ST_FUNCTION ? elem : join(elem, e);
        }

        return st_array(elem ? elem : st_prim(ST_NEVER));
    }

    if (n->kind == N_OBJECT && literal_with_lambdas(n)) {
        SType* r = st_new(ST_RECORD);
        const int nf = n->as.object.values.count;
        r->nfields = nf;
        r->fnames = pp_calloc((size_t)(nf + 1), sizeof(ObjString*));
        r->ftypes = pp_calloc((size_t)(nf + 1), sizeof(SType*));
        for (int i = 0; i < nf; ++i) {
            r->fnames[i] = n->as.object.keys[i];
            r->ftypes[i] = literal_placeholder(x, n->as.object.values.items[i]);
        }

        return r;
    }

    return check_expr(x, n, NULL);
}

static void check_args(Ctx* x, NodeList* al, SType** args, Node** nodes, int offset) {
    for (int i = 0; i < al->count; ++i) {
        Node* a = al->items[i];
        nodes[offset + i] = a;
        if (literal_with_lambdas(a)) {
            args[offset + i] = literal_placeholder(x, a);
            continue;
        }

        if (untyped_lambda(a)) {
            // typed once the call is resolved; its parameter count already picks between callback types
            SType* placeholder = st_new(ST_FUNCTION);
            placeholder->arity = a->as.func.sig->nparams + 1;
            args[offset + i] = placeholder;
        } else {
            args[offset + i] = check_expr(x, a, NULL);
        }
    }
}

static SType* construct_record(Ctx* x, Node* n, TDecl* d, SType** args, int argc) {
    Checker* c = x->c;
    if (d->kind == TD_NATIVE) {
        const char* ctor = d->which == NT_MAP ? "map" : d->which == NT_SET ? "set" : NULL;
        if (!ctor && d->module) {
            const ObjNativeType* nt = vm_native_type(c->vm, d->module->name->chars, d->name->chars);
            ctor = nt && nt->cls ? nt->cls->constructor : NULL;
        }

        cerr(c, n->line, "type '%s' cannot be called; make a value with %s(...)", d->name->chars, ctor ? ctor : "its constructor");
    }

    if (d->kind != TD_ALIAS || !d->def || d->def->kind != TY_RECORD || argc != 1) {
        return dyn_type(x, n, "a type called like a function");
    }

    SType* t = named_record(c, d);
    Node* lit = n->kind == N_CALL && n->as.call.args.count == 1 ? n->as.call.args.items[0] : NULL;
    if (lit && lit->kind == N_OBJECT) {
        // Type({ ... }): the literal checked against the type (strict fields, nested maps, lambdas)
        args[0] = check_expr(x, lit, t);
    }

    if (!st_fit(t, args[0], NULL)) {
        cerr(c, n->line, "expected %s but got %s", tname(t), tname(args[0]));
    }

    return t;
}

static SType* call_func_value(Ctx* x, Node* n, SType* f, SType** args, Node** nodes, int argc, const char* name) {
    Checker* c = x->c;
    for (int i = 0; i < argc; ++i) {
        if (nodes[i] && args[i]->kind == ST_FUNCTION && nodes[i]->kind == N_FUNC) {
            args[i] = check_lambda(x, nodes[i], i < f->nargs ? f->args[i] : NULL);
        }
    }

    if (any_dyn(args, argc)) {
        return dyn_type(x, n, "a call with a dynamic argument");
    }

    bool ok = f->variadic || f->nargs == argc;
    for (int i = 0; ok && !f->variadic && i < argc; ++i) {
        ok = st_fit(f->args[i], args[i], NULL) > 0;
    }

    if (!ok) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "no overload of '%s' accepts ", name);
        format_args(&sb, args, argc);
        sb_printf(&sb, "\n  candidates:\n    %s(", name);
        for (int i = 0; i < f->nargs; ++i) {
            if (i) {
                sb_puts(&sb, ", ");
            }

            st_print(f->args[i], &sb);
            if (f->fnames && f->fnames[i]) {
                sb_printf(&sb, " %s", f->fnames[i]->chars);
            }
        }

        sb_puts(&sb, ")");
        if (f->elem && f->elem->kind != ST_NEVER) {
            sb_puts(&sb, " -> ");
            st_print(f->elem, &sb);
        }

        cerr(c, n->line, "%s", sb.data);
    }

    return f->elem ? f->elem : dyn_type(x, n, "a callback without a declared return type");
}

static SType* call_value(Ctx* x, Node* n, SType* callee, SType** args, Node** nodes, int argc, const char* name, CCall* call) {
    Checker* c = x->c;
    switch (callee->kind) {
        case ST_OVERLOAD: {
            Cands cs = {{callee->funcs}, 1};
            cands_add(&cs, global_funcs(c, callee->funcs->name));

            return resolve_and_call(x, n, callee->funcs->name, &cs, args, nodes, argc, call);
        }
        case ST_FUNC: return call_func_value(x, n, callee, args, nodes, argc, name);
        case ST_TYPE:
            if (callee->decl) {
                return construct_record(x, n, callee->decl, args, argc);
            }

            return dyn_type(x, n, "a type called like a function");
        case ST_BLOCK:
            if (argc > 1 || (argc == 1 && callee->elem && !st_fit(callee->elem, args[0], NULL))) {
                const char* scope = x->inst && x->inst->fn && x->inst->fn->name ? x->inst->fn->name->chars : "the scope";
                cerr(c, n->line, "the Block of '%s' takes a value of type %s but got %s", scope, tname(callee->elem),
                     argc ? tname(args[0]) : "nothing");
            }

            if (callee->nargs) {
                return callee->args[0]; // what the block yields, known per call of the scope
            }

            return st_prim(ST_VOID); // a Callback kept for later: its value is not known, so it gives none
        case ST_DYN:
        case ST_FUNCTION:
        case ST_UNKNOWN:
        case ST_PARAM:
            for (int i = 0; i < argc; ++i) {
                if (nodes[i] && args[i]->kind == ST_FUNCTION && nodes[i]->kind == N_FUNC) {
                    check_lambda(x, nodes[i], NULL);
                }
            }

            return dyn_type(x, n, "a call of a value whose signature is not known");
        case ST_NEVER: return callee;
        default: cerr(c, n->line, "a value of type %s cannot be called", tname(callee));
    }
}

// A tag call (<f ...>...</f>): pick f() / f({}) and f([...]) / f({}, [...]) from the candidates'
// signatures, before the call is checked like any other (parser.h, tag_lower)
static void lower_tag(Ctx* x, Node* n) {
    Checker* c = x->c;
    Node* cn = n->as.call.callee;
    Signature** sigs = NULL;
    int nsigs = 0, cap = 0;
    bool ctor = false;
    bool known = true;
    const CFuncSet* sets[2] = {NULL, NULL};
    if (cn->kind == N_IDENT) {
        const CSym* sym = scope_lookup(x->scope, cn->as.str);
        if (sym && sym->kind == SYM_TYPE) {
            ctor = true;
        } else if (sym && sym->kind != SYM_FUNCS) {
            known = false; // a variable holding a function: its signature is not a list of candidates
        } else {
            sets[0] = sym ? sym->funcs : NULL;
            sets[1] = global_funcs(c, cn->as.str);
        }
    } else {
        SType* t = check_expr(x, cn, NULL);
        if (t->kind == ST_OVERLOAD) {
            sets[0] = t->funcs;
        } else if (t->kind == ST_TYPE) {
            ctor = true;
        } else {
            known = false;
        }
    }

    for (int s = 0; s < 2; ++s) {
        for (int i = 0; sets[s] && i < sets[s]->count; ++i) {
            GROW(sigs, nsigs, cap);
            sigs[nsigs++] = sets[s]->items[i]->sig;
        }
    }

    StrBuf err;
    sb_init(&err);
    const bool ok = tag_lower(c->vm, n, sigs, nsigs, ctor, known, &err);
    free(sigs);
    if (!ok) {
        cerr(c, n->line, "%s", err.data);
    }

    sb_free(&err);
}

static SType* check_call(Ctx* x, Node* n) {
    Checker* c = x->c;
    if (n->as.call.tag) {
        lower_tag(x, n);
    }

    Node* cn = n->as.call.callee;
    NodeList* al = &n->as.call.args;
    const int argc = al->count;
    SType** args = pp_calloc((size_t)(argc + 1), sizeof(SType*));
    Node** nodes = pp_calloc((size_t)(argc + 1), sizeof(Node*));
    CCall* call = x->inst ? &info_put(x->inst, n)->call : pp_calloc(1, sizeof(CCall));
    if (cn->kind == N_IDENT) {
        ObjString* name = cn->as.str;
        const CSym* sym = scope_lookup(x->scope, name);
        CFuncSet* g = global_funcs(c, name);
        if (!sym && !g) {
            cerr(c, n->line, "undefined name '%s'", name->chars);
        }

        if (!sym || sym->kind == SYM_FUNCS) {
            check_args(x, al, args, nodes, 0);
            Cands cs = {{0}, 0};
            cands_add(&cs, sym ? sym->funcs : NULL);
            cands_add(&cs, g);
            record_info(x, cn, st_prim(ST_FUNCTION));
            SType* r = resolve_and_call(x, n, name, &cs, args, nodes, argc, call);
            if (call->func && call->func->builtin && is_name(name, "type_of") && argc == 1) {
                fold_type_of(x, n, args[0]);
            }

            if (call->func && call->func->builtin && is_name(name, "defined") && argc == 1 && al->items[0]->kind == N_STRING) {
                // whether a name is visible is known from the program's text: the call becomes true or false
                ObjString* asked = al->items[0]->as.str;
                const bool visible = scope_lookup(x->scope, asked) || global_funcs(c, asked);
                n->kind = N_BOOL;
                n->as.bval = visible;
            }

            return r;
        }

        check_args(x, al, args, nodes, 0);
        SType* callee = sym_value_type(x, sym);
        record_info(x, cn, callee);

        return call_value(x, n, callee, args, nodes, argc, name->chars, call);
    }

    SType* callee = check_expr(x, cn, NULL);
    check_args(x, al, args, nodes, 0);
    const char* name = cn->kind == N_FIELD ? cn->as.field.name->chars : "function";
    SType* r = call_value(x, n, callee, args, nodes, argc, name, call);
    if (callee->kind == ST_OVERLOAD && call->func && call->func->builtin && is_name(callee->funcs->name, "type_of")) {
        fold_type_of(x, n, args[0]);
    }

    return r;
}

static SType* check_method(Ctx* x, Node* n) {
    Checker* c = x->c;
    NodeList* al = &n->as.method.args;
    const int argc = al->count + 1;
    SType** args = pp_calloc((size_t)(argc + 1), sizeof(SType*));
    Node** nodes = pp_calloc((size_t)(argc + 1), sizeof(Node*));
    CCall* call = x->inst ? &info_put(x->inst, n)->call : pp_calloc(1, sizeof(CCall));
    args[0] = check_expr(x, n->as.method.recv, NULL);
    nodes[0] = n->as.method.recv;
    Node* cn = n->as.method.callee;
    if (cn->kind == N_IDENT) {
        ObjString* name = cn->as.str;
        Cands cs;
        call_cands(x, &cs, name, args[0]);
        if (!cs.count) {
            const CSym* sym = scope_lookup(x->scope, name);
            if (sym && sym->kind == SYM_VAR) {
                check_args(x, al, args, nodes, 1);

                return call_value(x, n, sym->type, args, nodes, argc, name->chars, call);
            }

            if (args[0]->kind == ST_DYN || args[0]->kind == ST_UNKNOWN || args[0]->kind == ST_OBJECT) {
                check_args(x, al, args, nodes, 1);

                return dyn_type(x, n, "a dot call on a value whose type is not known");
            }

            cerr(c, n->line, "no function '%s' to call on a value of type %s", name->chars, tname(args[0]));
        }

        check_args(x, al, args, nodes, 1);
        SType* r = resolve_and_call(x, n, name, &cs, args, nodes, argc, call);
        if (call->func && call->func->builtin && is_name(name, "type_of") && argc == 1) {
            fold_type_of(x, n, args[0]);
        }

        return r;
    }

    SType* callee = check_expr(x, cn, NULL);
    check_args(x, al, args, nodes, 1);

    return call_value(x, n, callee, args, nodes, argc, cn->kind == N_FIELD ? cn->as.field.name->chars : "function", call);
}



// ---------------------------------------------------------------- patterns

static CFunc* ctor_by_name(Ctx* x, ObjString* name) {
    const CSym* sym = scope_lookup(x->scope, name);
    if (!sym || sym->kind != SYM_FUNCS) {
        return NULL;
    }

    for (int i = sym->funcs->count - 1; i >= 0; --i) {
        if (sym->funcs->items[i]->kind == CF_CTOR) {
            return sym->funcs->items[i];
        }
    }

    return NULL;
}

static void bind_var(Ctx* x, ObjString* name, SType* t, bool mut) {
    CSym* sym = scope_define(x->c, x->scope, name, SYM_VAR, 0);
    sym->type = t;
    sym->is_const = !mut;
}

static void check_pattern(Ctx* x, Pattern* p, SType* subject, bool mut) {
    Checker* c = x->c;
    switch (p->kind) {
        case P_WILD: return;
        case P_LITERAL: check_expr(x, p->lit, subject); return;
        case P_BIND: {
            CFunc* unit = ctor_by_name(x, p->name);
            if (unit && func_nparams(unit) == 0) {
                return; // `none` without parentheses is the constructor, not a new name
            }

            bind_var(x, p->name, subject, mut);

            return;
        }
        case P_VARIANT: {
            Node* cn = p->ctor;
            CFunc* ctor = NULL;
            if (cn->kind == N_IDENT) {
                if (is_name(cn->as.str, "tuple")) {
                    for (int i = 0; i < p->nsubs; ++i) {
                        SType* et = subject->kind == ST_TUPLE && subject->nargs == p->nsubs ? subject->args[i]
                                    : subject->kind == ST_DYN ? subject
                                                              : st_prim(ST_UNKNOWN);
                        check_pattern(x, p->subs[i], et, mut);
                    }

                    return;
                }

                ctor = ctor_by_name(x, cn->as.str);
            } else {
                const SType* t = check_expr(x, cn, NULL);
                if (t->kind == ST_OVERLOAD && t->funcs->count == 1 && t->funcs->items[0]->kind == CF_CTOR) {
                    ctor = t->funcs->items[0];
                } else if (t->kind == ST_ENUM) {
                    return; // a constant variant: Color:RED
                }
            }

            if (!ctor) {
                for (int i = 0; i < p->nsubs; ++i) {
                    check_pattern(x, p->subs[i], dyn_type(x, cn, "a pattern the checker does not understand"), mut);
                }

                return;
            }

            if (p->nsubs != func_nparams(ctor) && p->has_parens) {
                cerr(c, p->line, "'%s' takes %d value(s) but the pattern has %d", ctor->name->chars, func_nparams(ctor),
                     p->nsubs);
            }

            for (int i = 0; i < p->nsubs; ++i) {
                SType* pt;
                if (subject->kind == ST_ENUM && subject->decl == ctor->en) {
                    pt = variant_payload(c, subject, ctor->variant, i);
                } else if (subject->kind == ST_DYN) {
                    pt = subject;
                } else {
                    SBindings gen;
                    func_generics(ctor, &gen);
                    pt = param_type(x, ctor, i, &gen);
                    if (pt->kind == ST_PARAM) {
                        pt = st_prim(ST_UNKNOWN);
                    }
                }

                check_pattern(x, p->subs[i], pt, mut);
            }

            return;
        }
    }
}



// ---------------------------------------------------------------- if / match

static void require_bool(Ctx* x, SType* t, const char* what, int line) {
    if (t->kind != ST_BOOL && t->kind != ST_DYN && t->kind != ST_NEVER) {
        cerr(x->c, line, "%s must be a bool, got %s", what, tname(t));
    }
}

static SType* check_branchy(Ctx* x, Node* n, bool as_value, SType* expect) {
    const bool saved_value = x->in_value;
    const bool saved_scope_value = x->in_scope_value;
    if (as_value) {
        x->in_value = true;
        x->in_scope_value = false;
    }

    CScope* saved = x->scope;
    SType* result = NULL;
    if (n->kind == N_IF) {
        x->scope = scope_new(saved, NULL, NULL);
        if (n->as.if_.pat) {
            SType* subject = check_expr(x, n->as.if_.cond, NULL);
            check_pattern(x, n->as.if_.pat, subject, n->as.if_.pat_mut);
        } else {
            require_bool(x, check_expr(x, n->as.if_.cond, NULL), "if condition", n->line);
        }

        SType* then_t = check_block(x, n->as.if_.then_b, true, expect);
        x->scope = saved;
        SType* else_t = st_prim(ST_VOID);
        if (n->as.if_.else_b) {
            Node* e = n->as.if_.else_b;
            else_t = e->kind == N_IF || e->kind == N_MATCH ? check_branchy(x, e, as_value, expect) : check_block(x, e, true, expect);
        }

        result = n->as.if_.else_b ? join(then_t, else_t) : st_prim(ST_VOID);
    } else {
        SType* subject = check_expr(x, n->as.match.subject, NULL);
        for (int i = 0; i < n->as.match.narms; ++i) {
            MatchArm* arm = &n->as.match.arms[i];
            x->scope = scope_new(saved, NULL, NULL);
            check_pattern(x, arm->pat, subject, false);
            if (arm->guard) {
                require_bool(x, check_expr(x, arm->guard, NULL), "match guard", arm->guard->line);
            }

            x->dropped = NULL;
            SType* t = arm->body_is_block ? check_block(x, arm->body, true, expect) : check_expr(x, arm->body, expect);
            if (as_value && arm->body_is_block) {
                want_yield(x, t);
            }

            result = join(result, t);
            x->scope = saved;
        }

        if (!result) {
            result = st_prim(ST_VOID);
        }
    }

    x->scope = saved;
    x->in_value = saved_value;
    x->in_scope_value = saved_scope_value;

    return result;
}



// ---------------------------------------------------------------- scope calls

static SType* check_scope_call(Ctx* x, Node* n, bool as_value) {
    Checker* c = x->c;
    ObjString* name = n->as.scall.name;
    const CSym* sym = scope_lookup(x->scope, name);
    CFuncSet* g = global_funcs(c, name);
    if ((!sym || sym->kind != SYM_FUNCS) && !g) {
        cerr(c, n->line, "undefined scope '%s'", name->chars);
    }

    Cands cs = {{0}, 0};
    cands_add(&cs, sym && sym->kind == SYM_FUNCS ? sym->funcs : NULL);
    cands_add(&cs, g);
    NodeList* al = &n->as.scall.args;
    const int argc = al->count + 1;
    SType** args = pp_calloc((size_t)(argc + 1), sizeof(SType*));
    SType* block = st_new(ST_BLOCK);
    block->elem = st_prim(ST_DYN);
    args[0] = block;
    CScope* saved = x->scope;
    CScope* body_scope = scope_new(saved, NULL, NULL);
    for (int i = 0; i < al->count; ++i) {
        Node* a = al->items[i];
        if (a->kind == S_LET) {
            SType* declared = a->as.let.type ? resolve_type(x, a->as.let.type, x->scope, NULL) : NULL;
            SType* t = check_expr(x, a->as.let.init, declared);
            args[i + 1] = declared ? declared : t;
            CSym* v = scope_define(c, body_scope, a->as.let.name, SYM_VAR, a->line);
            v->type = args[i + 1];
            v->is_const = a->as.let.is_const;
        } else {
            args[i + 1] = check_expr(x, a, NULL);
        }
    }

    CCall* call = x->inst ? &info_put(x->inst, n)->call : pp_calloc(1, sizeof(CCall));
    const bool dynamic = any_dyn(args + 1, argc - 1);
    CFunc* f = NULL;
    SBindings b;
    if (!dynamic) {
        bool dispatch;
        f = pick(x, &cs, args, argc, &b, &dispatch);
        if (!f) {
            no_overload(x, name, &cs, args, argc, n->line);
        }

        SBindings gen;
        func_generics(f, &gen);
        SType* first = subst(param_type(x, f, 0, &gen), &b);
        if (first->kind == ST_BLOCK) {
            block->elem = first->elem;
            block->variadic = first->variadic;
        }
    }

    // the block first: the value it yields is what code() returns in the scope function,
    // which is checked (instantiated) for this kind of block
    x->scope = body_scope;
    if (block->elem && block->elem->kind != ST_VOID) {
        bind_var(x, c->s_scope, block->elem, false);
    }

    const bool saved_value = x->in_value;
    const bool saved_scope_value = x->in_scope_value;
    if (as_value) {
        x->in_value = true;
        x->in_scope_value = true;
    }

    const bool saved_fn = x->in_scope_fn;
    x->in_scope_fn = false;
    SType* body_value = check_block(x, n->as.scall.body, false, NULL);
    x->in_scope_fn = saved_fn;
    x->in_value = saved_value;
    x->in_scope_value = saved_scope_value;
    x->scope = saved;
    if (dynamic) {
        return dyn_type(x, n, "a scope call with a dynamic argument");
    }

    block->nargs = 1;
    block->args = pp_calloc(2, sizeof(SType*));
    block->args[0] = body_value;

    return call_result(x, f, args, argc, &b, call, n);
}



// ---------------------------------------------------------------- expressions

SType* check_expr(Ctx* x, Node* n, SType* expect) {
    Checker* c = x->c;
    SType* t = NULL;
    switch (n->kind) {
        case N_INT: t = st_prim(ST_INT); break;
        case N_FLOAT: t = st_prim(ST_FLOAT); break;
        case N_STRING: t = st_prim(ST_STRING); break;
        case N_BOOL: t = st_prim(ST_BOOL); break;
        case N_IDENT: {
            const CSym* sym = scope_lookup(x->scope, n->as.str);
            if (!sym) {
                CFuncSet* g = global_funcs(c, n->as.str);
                if (g) {
                    t = overload_type(g);
                    break;
                }

                if (n->as.str == c->s_scope || is_name(n->as.str, "scope")) {
                    cerr(c, n->line, "'scope' can only be used inside the block of a scope call");
                }

                cerr(c, n->line, "undefined name '%s'", n->as.str->chars);
            }

            t = sym_value_type(x, sym);
            if (sym->kind == SYM_FUNCS && expect && expect->kind == ST_FUNC) {
                // a function name passed as a callback: check the member that fits
                for (int i = 0; i < sym->funcs->count; ++i) {
                    if (func_fits_type(x, sym->funcs->items[i], expect)) {
                        func_value_type(x, sym->funcs->items[i]);
                        break;
                    }
                }
            }

            break;
        }
        case N_ARRAY: {
            SType* want = expect && expect->kind == ST_ARRAY ? expect->elem : NULL;
            SType* elem = NULL;
            for (int i = 0; i < n->as.array.items.count; ++i) {
                SType* e = check_expr(x, n->as.array.items.items[i], want);
                if (e->kind == ST_VOID) {
                    cerr(c, n->as.array.items.items[i]->line, "array element has no value (void)");
                }

                elem = join(elem, e);
            }

            if (!elem) {
                elem = want && want->kind != ST_PARAM ? want : st_prim(ST_NEVER);
            } else if (want && st_fit(want, elem, NULL) && want->kind != ST_UNKNOWN && want->kind != ST_PARAM) {
                elem = want; // [1, 2] as a float[] holds floats
            }

            t = st_array(elem);
            break;
        }
        case N_OBJECT: {
            SType* r = st_new(ST_RECORD);
            const int nf = n->as.object.values.count;
            r->nfields = nf;
            r->fnames = pp_calloc((size_t)(nf + 1), sizeof(ObjString*));
            r->ftypes = pp_calloc((size_t)(nf + 1), sizeof(SType*));
            SType* rec = expect && expect->kind == ST_RECORD ? st_record_fill(c, expect) : NULL;
            check_literal_fields(x, n, rec ? expect : NULL);
            for (int i = 0; i < nf; ++i) {
                SType* want = NULL;
                for (int k = 0; rec && k < rec->nfields; ++k) {
                    if (k != rec->rest - 1 &&
                        (same_name_req(rec->fnames[k], n->as.object.keys[i]) || record_key_names(n->as.object.keys[i], rec->fnames[k]))) {
                        want = rec->ftypes[k];
                    }
                }

                if (!want && rec && rec->rest) {
                    // a field for the rest map: a V of ...Map<string, V>
                    SType* rt = rec->ftypes[rec->rest - 1];
                    want = record_key_names(n->as.object.keys[i], rec->fnames[rec->rest - 1]) ? rt
                           : rt && rt->kind == ST_NATIVE && rt->nargs > 1                         ? rt->args[1]
                                                                                                  : NULL;
                }

                ObjString* saved_name = lambda_name;
                lambda_name = n->as.object.keys[i];
                r->fnames[i] = n->as.object.keys[i];
                r->ftypes[i] = check_expr(x, n->as.object.values.items[i], want);
                lambda_name = saved_name;
                if (r->ftypes[i]->kind == ST_VOID) {
                    cerr(c, n->line, "object field '%s' has no value (void)", n->as.object.keys[i]->chars);
                }
            }

            // a literal where a Map<string, V> is expected (a field of a literal, an array element) is a map
            t = is_map_type(expect) && st_fit(expect, r, NULL) ? record_to_map(x, n, expect, r) : r;
            break;
        }
        case N_CALL: t = check_call(x, n); break;
        case N_METHOD: t = check_method(x, n); break;
        case N_FIELD: t = field_of(x, n, check_expr(x, n->as.field.obj, NULL), n->as.field.name); break;
        case N_INDEX: {
            CCall* call = x->inst ? &info_put(x->inst, n)->call : pp_calloc(1, sizeof(CCall));
            t = check_index(x, n, n->as.index.obj, &n->as.index.args, NULL, call);
            break;
        }
        case N_CHAIN: t = check_chain(x, n); break;
        case N_FUNC: t = check_lambda(x, n, expect); return t;
        case N_IF:
        case N_MATCH: t = check_branchy(x, n, true, expect); break;
        case N_BLOCK: t = check_block(x, n, true, expect); break;
        case N_SCOPECALL: t = check_scope_call(x, n, true); break;
        case N_AS: {
            check_expr(x, n->as.as_.expr, NULL);
            t = option_of(c, resolve_type(x, n->as.as_.type, x->scope, NULL));
            break;
        }
        default: t = dyn_type(x, n, "an expression kind the checker does not know"); break;
    }

    return record_info(x, n, t);
}



// ---------------------------------------------------------------- blocks

static void check_unused_functions(Ctx* x, CScope* s) {
    for (int i = 0; i < s->count; ++i) {
        const CSym* sym = &s->syms[i];
        if (sym->kind != SYM_FUNCS) {
            continue;
        }

        for (int k = 0; k < sym->funcs->count; ++k) {
            CFunc* f = sym->funcs->items[k];
            if (f->kind == CF_USER && f->body && f->ninsts == 0 && f->closure == s) {
                func_value_type(x, f);
            }
        }
    }
}

// the value of a statement: only `yield expr` gives one (and an expression statement is checked here too)
static SType* stmt_value(Ctx* x, Node* s, SType* expect) {
    if (s->kind != S_EXPR) {
        check_stmt(x, s);
        x->dropped = NULL;

        return st_prim(ST_VOID);
    }

    const bool yield = s->as.expr.yield;
    Node* e = s->as.expr.expr;
    SType* t;
    if (e->kind == N_IF || e->kind == N_MATCH) {
        t = record_info(x, e, check_branchy(x, e, false, yield ? expect : NULL));
    } else if (e->kind == N_SCOPECALL) {
        t = record_info(x, e, check_scope_call(x, e, false));
    } else {
        t = check_expr(x, e, yield ? expect : NULL);
    }

    // for the hint "end the block with yield": only the value of the block's last statement counts
    x->dropped = !yield && t && t->kind != ST_VOID && t->kind != ST_DYN && t->kind != ST_UNKNOWN ? s : NULL;

    return yield ? t : st_prim(ST_VOID);
}

// a block used as a value gave nothing: when it ends with a bare expression, the fix is `yield`
static void want_yield(Ctx* x, SType* got) {
    if (got && got->kind == ST_VOID && x->dropped) {
        cerr(x->c, x->dropped->line, "this block is used as a value but gives none: write 'yield' before its last expression");
    }
}

SType* check_block(Ctx* x, Node* block, bool new_scope, SType* expect) {
    CScope* saved = x->scope;
    if (new_scope) {
        x->scope = scope_new(saved, NULL, NULL);
    }

    SType* last = st_prim(ST_VOID);
    const int count = block->as.block.stmts.count;
    for (int i = 0; i < count; ++i) {
        Node* s = block->as.block.stmts.items[i];
        last = i == count - 1 ? stmt_value(x, s, expect) : (check_stmt(x, s), st_prim(ST_VOID));
    }

    check_unused_functions(x, x->scope);
    x->scope = saved;

    return last;
}



// ---------------------------------------------------------------- declarations

void declare_function(Ctx* x, Node* n) {
    Checker* c = x->c;
    Signature* sig = n->as.func.sig;
    ObjString* name = sig->name;
    if (n->as.func.decl) {
        // the definition of an earlier declaration fills in the same function
        const CSym* sym = scope_lookup_here(x->scope, name);
        for (int i = 0; sym && sym->kind == SYM_FUNCS && i < sym->funcs->count; ++i) {
            CFunc* f = sym->funcs->items[i];
            if (f->sig == n->as.func.decl) {
                f->body = n->as.func.body;
                f->declared_only = false;
                f->sig = sig;
                f->closure = x->scope;
                if (x->inst) {
                    info_put(x->inst, n)->call.func = f;
                }

                return;
            }
        }
    }

    if (n->as.func.lib) {
        char err[512];
        Extern* e = extern_new(sig, n->as.func.lib->chars, n->as.func.native->chars, err, sizeof err);
        if (!e) {
            cerr(c, n->line, "%s", err);
        }

        free(e); // the targets make their own: each keeps its library handle
    } else if (n->as.func.native) {
        const char* lib = module_lib_path(x->module);
        const NativeImpl* impls = lib ? vm_native_impls(c->vm, lib) : NULL;
        if (!impls) {
            cerr(c, n->line,
                 "native function '%s' needs a C implementation, but this module has none registered\n"
                 "  (native functions are declared in library modules whose C half calls vm_register_native_impls)",
                 name->chars);
        }

        bool found = false;
        for (int i = 0; impls[i].name && !found; ++i) {
            found = str_equals(n->as.func.native, impls[i].name);
        }

        if (!found) {
            cerr(c, n->line, "module '%s' has no native implementation named '%s'", x->module->name->chars,
                 n->as.func.native->chars);
        }
    }

    CFunc* f = func_new(c, n->as.func.native ? CF_NATIVE : CF_USER, name, sig, x->scope);
    f->impl_name = n->as.func.native;
    f->extern_lib = n->as.func.lib;
    if (x->inst) {
        info_put(x->inst, n)->call.func = f;
    }

    f->body = n->as.func.body;
    f->declared_only = !n->as.func.body && !n->as.func.native;
    f->global = n->as.func.global;
    f->module = x->module;
    scope_add_func(c, x->scope, name, f);
    if (f->global) {
        global_add(c, name, f);
    }
}

static TDecl* tdecl_new(Ctx* x, TDeclKind kind, ObjString* name) {
    TDecl* d = pp_calloc(1, sizeof(TDecl));
    d->kind = kind;
    d->name = name;
    d->scope = x->scope;
    d->module = x->module;
    d->id = x->c->next_id++;

    return d;
}

static void define_type(Ctx* x, TDecl* d) {
    CSym* sym = scope_define(x->c, x->scope, d->name, SYM_TYPE, 0);
    sym->tdecl = d;
    sym->is_const = true;
}

static void check_enum(Ctx* x, Node* n) {
    TDecl* d = tdecl_new(x, TD_ENUM, n->as.enum_decl.name);
    d->node = n;
    d->generics = n->as.enum_decl.generics;
    d->defaults = n->as.enum_decl.defaults;
    d->ngenerics = n->as.enum_decl.ngenerics;
    d->value_type = n->as.enum_decl.value_type;
    d->nvariants = n->as.enum_decl.nvariants;
    d->variants = pp_calloc((size_t)(d->nvariants + 1), sizeof(CVariant));
    for (int i = 0; i < d->nvariants; ++i) {
        const VariantDecl* v = &n->as.enum_decl.variants[i];
        d->variants[i].name = v->name;
        d->variants[i].is_ctor = v->is_ctor;
        d->variants[i].payload = v->payload;
        d->variants[i].npayload = v->npayload;
        if (v->value) {
            check_expr(x, v->value, NULL);
        }
    }

    define_type(x, d);
    for (int i = 0; i < d->nvariants; ++i) {
        if (!d->variants[i].is_ctor) {
            continue;
        }

        CFunc* f = func_new(x->c, CF_CTOR, d->variants[i].name, NULL, x->scope);
        f->en = d;
        f->variant = i;
        f->module = x->module;
        scope_add_func(x->c, x->scope, d->variants[i].name, f);
    }
}

static void check_interface(Ctx* x, Node* n) {
    Checker* c = x->c;
    const CSym* prev = scope_lookup_here(x->scope, n->as.iface.name);
    if (prev && prev->kind == SYM_TYPE && prev->tdecl && prev->tdecl->kind == TD_IFACE) {
        cerr(c, n->line, "interface '%s' is already declared", n->as.iface.name->chars);
    }

    TDecl* d = tdecl_new(x, TD_IFACE, n->as.iface.name);
    d->generics = n->as.iface.generics;
    d->ngenerics = n->as.iface.ngenerics;
    d->reqs = n->as.iface.reqs;
    d->bodies = n->as.iface.bodies;
    d->nreqs = n->as.iface.nreqs;
    d->parents = n->as.iface.parents;
    d->nparents = n->as.iface.nparents;
    define_type(x, d);
}

static void record_impl_fn(Checker* c, SType* target, ObjString* name, CFunc* f) {
    GROW(c->impl_fns, c->nimpl_fns, c->capimpl_fns);
    CImplFn* fn = &c->impl_fns[c->nimpl_fns++];
    fn->target = target;
    fn->name = name;
    fn->fn = f;
}

// the parameter types of requirement r of interface d, with the interface's name standing for target
static CScope* self_scope(Ctx* x, TDecl* d, SType* target, SType** args, int nargs) {
    CScope* s = scope_new(d->scope, d->module, NULL);
    CSym* self = scope_define(x->c, s, d->name, SYM_TYPE, 0);
    self->type = target;
    for (int i = 0; i < d->ngenerics; ++i) {
        CSym* g = scope_define(x->c, s, d->generics[i], SYM_TYPE, 0);
        g->type = i < nargs ? args[i] : st_prim(ST_UNKNOWN);
    }

    return s;
}

static void implement(Ctx* x, Node* n, TDecl* d, SType** iargs, int niargs, SType* target, ObjString** generics, int ngenerics) {
    Checker* c = x->c;
    for (int i = 0; i < c->nimpls; ++i) {
        if (c->impls[i].iface == d && st_equal(c->impls[i].target, target)) {
            return; // already implements it (through an extending interface)
        }
    }

    GROW(c->impls, c->nimpls, c->capimpls);
    CImpl* im = &c->impls[c->nimpls++];
    im->target = target;
    im->generics = generics;
    im->ngenerics = ngenerics;
    im->iface = d;
    im->args = iargs;
    im->nargs = niargs;
    CScope* ss = self_scope(x, d, target, iargs, niargs);
    for (int r = 0; r < d->nreqs; ++r) {
        Signature* req = d->reqs[r];
        const int np = req->nparams;
        SType** want = pp_calloc((size_t)(np + 1), sizeof(SType*));
        for (int i = 0; i < np; ++i) {
            want[i] = resolve_type(x, req->params[i].type, ss, NULL);
        }

        Cands cs;
        call_cands(x, &cs, req->name, target);
        SBindings b;
        bool dispatch;
        CFunc* f = cs.count ? pick(x, &cs, want, np, &b, &dispatch) : NULL;
        // a catch-all (any, untyped) does not count: the function has to be written for the type
        if (f && f->kind == CF_NATIVE && f->builtin) {
            bool strict = true;
            for (int i = 0; i < np && f->sig; ++i) {
                const Type* pt = f->sig->params[i].type;
                if (!pt || (pt->kind == TY_PRIM && pt->prim == PRIM_ANY)) {
                    strict = false;
                }
            }

            if (!strict) {
                f = NULL;
            }
        }

        if (f) {
            record_impl_fn(c, target, req->name, f);
            continue;
        }

        if (d->bodies && d->bodies[r]) {
            CFunc* def = func_new(c, CF_USER, req->name, req, ss);
            def->body = d->bodies[r];
            def->module = d->module;
            def->extra_generics = generics;
            def->nextra = ngenerics;
            record_impl_fn(c, target, req->name, def);
            if (!type_decl_of(target)) {
                scope_add_func(c, x->scope, req->name, def);
            }

            continue;
        }

        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%s(", req->name->chars);
        for (int i = 0; i < np; ++i) {
            if (i) {
                sb_puts(&sb, ", ");
            }

            st_print(want[i], &sb);
        }

        sb_putc(&sb, ')');
        if (req->ret) {
            sb_puts(&sb, " -> ");
            st_print(resolve_type(x, req->ret, ss, NULL), &sb);
        }

        cerr(c, n->line, "type '%s' does not implement interface '%s': missing function %s", tname(target), d->name->chars,
             sb.data);
    }

    // an interface that extends others: the type implements them too
    for (int p = 0; p < d->nparents; ++p) {
        SType* parent = resolve_type(x, d->parents[p], ss, NULL);
        if (parent->kind == ST_IFACE) {
            implement(x, n, parent->decl, parent->args, parent->nargs, target, generics, ngenerics);
        }
    }
}

static void check_impl(Ctx* x, Node* n) {
    Checker* c = x->c;
    SBindings gen;
    memset(&gen, 0, sizeof gen);
    gen.count = n->as.impl.ngenerics;
    for (int i = 0; i < gen.count && i < PP_MAX_BINDINGS; ++i) {
        gen.names[i] = n->as.impl.generics[i];
    }

    SType* iface = resolve_type(x, n->as.impl.iface, x->scope, &gen);
    SType* target = resolve_type(x, n->as.impl.target, x->scope, &gen);
    if (iface->kind == ST_DYN || target->kind == ST_DYN) {
        dyn_type(x, n, "an impl of a type the checker cannot resolve");

        return;
    }

    if (iface->kind != ST_IFACE) {
        cerr(c, n->line, "'%s' is not an interface", tname(iface));
    }

    if (target->kind == ST_IFACE) {
        cerr(c, n->line, "'%s' is an interface; only types can implement an interface (to extend '%s', write 'impl %s for %s' inside interface '%s')",
             target->decl->name->chars, iface->decl->name->chars, iface->decl->name->chars, target->decl->name->chars,
             target->decl->name->chars);
    }

    implement(x, n, iface->decl, iface->args, iface->nargs, target, n->as.impl.generics, n->as.impl.ngenerics);
}

static void check_precedence(Ctx* x, Node* n) {
    Checker* c = x->c;
    ObjString* op = n->as.prec.op;
    const int rel = n->as.prec.rel;
    const double other = prec_of(c, n->as.prec.other);
    const double cur = prec_of(c, op);
    double np = other;
    if (rel > 0 && cur > other) {
        return;
    }

    if (rel < 0 && cur < other) {
        return;
    }

    if (rel != 0) {
        double best = rel > 0 ? other + 10 : other - 10;
        for (int i = -1; i < c->nprecs; ++i) {
            double q;
            if (i < 0) {
                q = 65.0;
            } else {
                if (same_name_req(c->precs[i].op, op)) {
                    continue;
                }

                q = c->precs[i].prec;
            }

            if (rel > 0 && q > other && q < best) {
                best = q;
            }

            if (rel < 0 && q < other && q > best) {
                best = q;
            }
        }

        np = (other + best) / 2;
    }

    set_prec(c, op, np);
}



// ---------------------------------------------------------------- statements

static void check_let(Ctx* x, Node* n) {
    Checker* c = x->c;
    ObjString* name = n->as.let.name;
    SType* declared = n->as.let.type ? resolve_type(x, n->as.let.type, x->scope, NULL) : NULL;
    SType* t;
    if (n->as.let.init) {
        ObjString* saved = lambda_name;
        lambda_name = name;
        SType* v = check_expr(x, n->as.let.init, declared);
        lambda_name = saved;
        if (v->kind == ST_VOID) {
            cerr(c, n->line, "cannot initialize '%s' with a void value", name->chars);
        }

        if (declared) {
            if (!st_fit(declared, v, NULL)) {
                cerr(c, n->line, "cannot initialize '%s': expected %s but got %s", name->chars, tname(declared), tname(v));
            }

            v = record_to_map(x, n->as.let.init, declared, v);

            // a lambda keeps its own (named) parameters: it was typed from the declared callback type
            t = declared;
            if (declared->kind == ST_FUNC && v->kind == ST_FUNC && v->fnames) {
                t = st_new(ST_FUNC);
                *t = *v;
                t->alias = declared->alias;
            }

        } else if (v->kind == ST_NEVER) {
            t = v;
        } else {
            t = v;
        }
    } else if (n->as.let.is_const) {
        t = st_prim(ST_BOOL);
    } else {
        t = declared ? declared : dyn_type(x, n, "a variable declared without a type or a value");
    }

    CSym* sym = scope_define(c, x->scope, name, SYM_VAR, n->line);
    sym->type = t;
    sym->is_const = n->as.let.is_const;
    if (n->as.let.init && !declared && t->kind != ST_NEVER && contains_never(t)) {
        sym->pending = true;
        sym->init_info = x->inst ? info_put(x->inst, n->as.let.init) : NULL;
    }
}

static void check_assign(Ctx* x, Node* n) {
    Checker* c = x->c;
    Node* target = n->as.assign.target;
    ObjString* op = n->as.assign.op;
    SType* cur;
    CCall* call = x->inst ? &info_put(x->inst, n)->call : pp_calloc(1, sizeof(CCall));
    if (target->kind == N_IDENT) {
        const CSym* sym = scope_lookup(x->scope, target->as.str);
        if (!sym) {
            cerr(c, n->line, "undefined name '%s'", target->as.str->chars);
        }

        if (sym->kind != SYM_VAR || (sym->is_const && !op)) {
            cerr(c, n->line, "cannot assign to constant '%s'", target->as.str->chars);
        }

        cur = record_info(x, target, sym->type);
    } else if (target->kind == N_FIELD) {
        SType* obj = check_expr(x, target->as.field.obj, NULL);
        if (obj->kind == ST_MODULE) {
            cerr(c, n->line, "cannot assign field '%s' on a value of type module", target->as.field.name->chars);
        }

        cur = record_info(x, target, field_of(x, target, obj, target->as.field.name));
    } else if (target->kind == N_INDEX) {
        if (!op) {
            check_index(x, n, target->as.index.obj, &target->as.index.args, n->as.assign.value, call);

            return;
        }

        CCall* get = x->inst ? &info_put(x->inst, target)->call : pp_calloc(1, sizeof(CCall));
        cur = record_info(x, target, check_index(x, target, target->as.index.obj, &target->as.index.args, NULL, get));
    } else {
        cerr(c, n->line, "cannot assign to this expression");
    }

    if (!op) {
        SType* v = check_expr(x, n->as.assign.value, cur);
        if (v->kind == ST_VOID) {
            cerr(c, n->line, "cannot assign a void value");
        }

        if (target->kind == N_IDENT && contains_never(cur)) {
            SType* r = refine_var(x, target, v);
            cur = r ? r : cur;
        }

        if (!st_fit(cur, v, NULL) && cur->kind != ST_NEVER) {
            cerr(c, n->line, "cannot assign to '%s': expected %s but got %s",
                 target->kind == N_IDENT ? target->as.str->chars : target->kind == N_FIELD ? target->as.field.name->chars : "[]",
                 tname(cur), tname(v));
        }

        record_to_map(x, n->as.assign.value, cur, v);

        return;
    }

    // target op= value: an in-place op= that accepts (current, value), else target = op(current, value)
    SType* v = check_expr(x, n->as.assign.value, NULL);
    SType* args[2] = {cur, v};
    if (any_dyn(args, 2)) {
        dyn_type(x, n, "a compound assignment with a dynamic operand");

        return;
    }

    Cands cs;
    call_cands(x, &cs, n->as.assign.opeq, cur);
    SBindings b;
    bool dispatch;
    CFunc* f = cs.count ? pick(x, &cs, args, 2, &b, &dispatch) : NULL;
    if (f) {
        call->dispatch = dispatch;
        call_result(x, f, args, 2, &b, call, n);

        return;
    }

    if (target->kind == N_IDENT) {
        const CSym* sym = scope_lookup(x->scope, target->as.str);
        if (sym->is_const) {
            cerr(c, n->line, "cannot assign to constant '%s'", target->as.str->chars);
        }
    }

    call_cands(x, &cs, op, cur);
    f = cs.count ? pick(x, &cs, args, 2, &b, &dispatch) : NULL;
    if (!f) {
        cerr(c, n->line, "'%s' needs an overload of '%s' (in place) or of '%s' accepting (%s, %s); there is none",
             n->as.assign.opeq->chars, n->as.assign.opeq->chars, op->chars, tname(cur), tname(v));
    }

    CCall* opcall = x->inst ? &info_put(x->inst, n->as.assign.value)->call : call;
    (void)opcall;
    call->dispatch = dispatch;
    SType* r = call_result(x, f, args, 2, &b, call, n);
    if (!st_fit(cur, r, NULL) && cur->kind != ST_NEVER && r->kind != ST_DYN) {
        cerr(c, n->line, "'%s' gives %s, which cannot be stored in %s", n->as.assign.opeq->chars, tname(r), tname(cur));
    }

    if (target->kind == N_INDEX && x->inst) {
        // c[k] = c[k] op v: the store is a []= call of its own
        NodeList* keys = &target->as.index.args;
        const int argc = keys->count + 2;
        SType** sargs = pp_calloc((size_t)argc + 1, sizeof(SType*));
        const NodeInfo* oi = instance_info(x->inst, target->as.index.obj);
        sargs[0] = oi ? oi->type : st_prim(ST_DYN);
        for (int i = 0; i < keys->count; ++i) {
            const NodeInfo* ki = instance_info(x->inst, keys->items[i]);
            sargs[i + 1] = ki ? ki->type : st_prim(ST_DYN);
        }

        sargs[argc - 1] = r;
        CCall* store = &info_put(x->inst, n)->call2;
        Cands scs;
        call_cands(x, &scs, c->s_index_set, sargs[0]);
        SBindings sb;
        bool sd;
        CFunc* sf = scs.count ? pick(x, &scs, sargs, argc, &sb, &sd) : NULL;
        if (sf && !(sf->kind == CF_NATIVE && sf->builtin)) {
            store->dispatch = sd;
            call_result(x, sf, sargs, argc, &sb, store, n);
        } else {
            store->func = sf;
        }
    }
}

// the type arguments an interface (Iterable<T>, KeyIterable<K, V>) has for type t, NULL when t does not implement it
static SType** iface_args_of(Ctx* x, TDecl* iface, SType* t) {
    if (!iface) {
        return NULL;
    }

    SType* want = st_new(ST_IFACE);
    want->decl = iface;
    want->nargs = iface->ngenerics;
    want->args = pp_calloc((size_t)(iface->ngenerics + 1), sizeof(SType*));
    SBindings b;
    memset(&b, 0, sizeof b);
    b.count = iface->ngenerics;
    for (int i = 0; i < iface->ngenerics; ++i) {
        SType* p = st_new(ST_PARAM);
        p->name = iface->generics[i];
        want->args[i] = p;
        b.names[i] = iface->generics[i];
    }

    if (!st_fit(want, t, &b)) {
        return NULL;
    }

    SType** r = pp_calloc((size_t)(iface->ngenerics + 1), sizeof(SType*));
    for (int i = 0; i < iface->ngenerics; ++i) {
        r[i] = b.types[i] ? b.types[i] : st_prim(ST_UNKNOWN);
    }

    return r;
}

static bool is_pair(const SType* t) {
    return t && t->kind == ST_TUPLE && t->nargs == 2;
}

// `for k, v` over values that are pairs (a Generator / Iter of tuple<K, V>): the pair's two types
static SType* pair_types(Ctx* x, Node* n, SType* item, SType** key, const char* what) {
    if (!is_pair(item)) {
        cerr(x->c, n->line, "for k, v over %s of %s: it needs pairs (a KeyIterable); use .enumerate() to count the items",
             what, tname(item));
    }

    *key = item->args[0];

    return item->args[1];
}

// The types `for` gives: the item, and *key for the first of two variables (pairs). A type of
// the language is iterated directly; any other type through the interfaces Iterable<T> /
// KeyIterable<K, V> (*proto: the backends call the prelude's iterate() / iterate_pairs()).
static SType* iter_types(Ctx* x, Node* n, SType* it, SType** key, bool pairs, bool* proto) {
    Checker* c = x->c;
    *key = st_prim(ST_INT);
    const char* count_hint = "for i, x over a %s: only a KeyIterable (an array, a map, pairs) has keys; use .enumerate() "
                             "to count the items";
    switch (it->kind) {
        case ST_RANGE:
            if (pairs) {
                cerr(c, n->line, count_hint, "range");
            }

            if (!it->elem || st_is_numeric(it->elem)) {
                return it->elem ? it->elem : st_union(st_prim(ST_INT), st_prim(ST_FLOAT));
            }

            // a Range<T> of anything else is iterated through impl Iterable<T> for Range<T>
            if (st_is_dyn(it->elem) || it->elem->kind == ST_PARAM || it->elem->kind == ST_UNKNOWN) {
                return dyn_type(x, n, "iterating a Range whose element type is not known");
            }

            *proto = true;
            {
                SType** t = iface_args_of(x, c->iterable, it);
                if (!t) {
                    cerr(c, n->line, "cannot iterate over %s: it does not implement Iterable<%s> (write "
                                     "generator iter(%s r) -> %s { ... } and impl Iterable<%s> for %s)",
                         tname(it), tname(it->elem), tname(it), tname(it->elem), tname(it->elem), tname(it));
                }

                return t[0];
            }
        case ST_ARRAY: return it->elem;
        case ST_STRING:
            if (pairs) {
                cerr(c, n->line, count_hint, "string");
            }

            return st_prim(ST_STRING);
        case ST_TUPLE: {
            SType* r = NULL;
            for (int i = 0; i < it->nargs; ++i) {
                r = join(r, it->args[i]);
            }

            return r;
        }
        case ST_NATIVE:
            if (it->decl->which == NT_MAP) {
                if (!pairs) {
                    cerr(c, n->line, "for x over a map: use map.keys() or map.values() (for k, v in map gives both)");
                }

                *key = it->nargs ? it->args[0] : st_prim(ST_DYN);

                return it->nargs > 1 ? it->args[1] : st_prim(ST_DYN);
            }

            if (it->decl->which == NT_SET) {
                if (pairs) {
                    cerr(c, n->line, count_hint, "set");
                }

                return it->nargs ? it->args[0] : st_prim(ST_DYN);
            }

            if (it->decl->which == NT_GENERATOR) {
                SType* e = it->nargs ? it->args[0] : st_prim(ST_DYN);

                return pairs ? pair_types(x, n, e, key, "a Generator") : e;
            }

            {
                SType* e = native_element(c, it->decl);

                return e ? e : dyn_type(x, n, "iterating a native class");
            }
        case ST_RECORD:
        case ST_OBJECT:
        case ST_ENUM:
        case ST_IFACE: {
            *proto = true;
            if (pairs) {
                SType** kv = iface_args_of(x, c->key_iterable, it);
                if (kv) {
                    *key = kv[0];

                    return kv[1];
                }
            }

            SType** t = iface_args_of(x, c->iterable, it);
            if (!t) {
                t = iface_args_of(x, c->iter, it);
            }

            if (t) {
                return pairs ? pair_types(x, n, t[0], key, tname(it)) : t[0];
            }

            if (it->kind == ST_RECORD || it->kind == ST_OBJECT) {
                cerr(c, n->line, "cannot iterate over a record (%s): use fields(x) to go through its fields, or implement "
                                 "Iterable<T> for its type", tname(it));
            }

            cerr(c, n->line, "cannot iterate over a value of type %s: it does not implement Iterable<T>%s", tname(it),
                 pairs ? " or KeyIterable<K, V>" : "");
        }
        case ST_UNION: {
            // every member must be iterable; the item is any of theirs
            SType* r = NULL;
            SType* k = NULL;
            for (int i = 0; i < it->nargs; ++i) {
                SType* mk;
                r = join(r, iter_types(x, n, it->args[i], &mk, pairs, proto));
                k = join(k, mk);
            }

            *key = k;

            return r;
        }
        case ST_DYN:
        case ST_NEVER:
            *key = it;

            return it;
        case ST_UNKNOWN:
        case ST_PARAM:
            *key = dyn_type(x, n, "iterating a value whose type is not known");

            return *key;
        default: cerr(c, n->line, "cannot iterate over a value of type %s", tname(it));
    }
}

// for over a type that implements the interfaces: the call the compiling targets make for a value
// that is not a built-in collection, iterate($for) / iterate_pairs($for), checked here so that the
// instance exists. $for is the iterated value (not a name a program can write).
static Node* synth_ident(VM* vm, const char* name, int line) {
    Node* id = pp_calloc(1, sizeof(Node));
    id->kind = N_IDENT;
    id->line = line;
    id->as.str = str_cintern(vm, name);

    return id;
}

static void protocol_call(Ctx* x, Node* n, SType* it, bool pairs) {
    Checker* c = x->c;
    VM* vm = c->vm;
    if (!n->as.for_.proto) {
        Node* call = pp_calloc(1, sizeof(Node));
        call->kind = N_CALL;
        call->line = n->line;
        call->as.call.callee = synth_ident(vm, pairs ? "iterate_pairs" : "iterate", n->line);
        call->as.call.ic = pp_calloc(1, sizeof(InlineCache));
        GROW(call->as.call.args.items, call->as.call.args.count, call->as.call.args.cap);
        call->as.call.args.items[call->as.call.args.count++] = synth_ident(vm, "$for", n->line);
        n->as.for_.proto = call;
    }

    CScope* saved = x->scope;
    x->scope = scope_new(c->builtins, x->module, x->inst);
    bind_var(x, str_cintern(vm, "$for"), it, false);
    check_expr(x, n->as.for_.proto, NULL);
    x->scope = saved;
}

static void check_for(Ctx* x, Node* n) {
    SType* it = check_expr(x, n->as.for_.iter, NULL);
    SType* key;
    const bool pairs = n->as.for_.var2 != NULL;
    bool proto = false;
    SType* item = iter_types(x, n, it, &key, pairs, &proto);
    if (proto && it->kind != ST_UNION && !st_is_dyn(it)) {
        const bool kv = pairs && iface_args_of(x, x->c->key_iterable, it);
        protocol_call(x, n, it, kv);
    }

    CScope* saved = x->scope;
    x->scope = scope_new(saved, NULL, NULL);
    if (n->as.for_.var2) {
        bind_var(x, n->as.for_.var1, key, n->as.for_.mut);
        bind_var(x, n->as.for_.var2, item, n->as.for_.mut);
    } else {
        bind_var(x, n->as.for_.var1, item, n->as.for_.mut);
    }

    x->loops++;
    const bool saved_value = x->in_value;
    const bool saved_scope_value = x->in_scope_value;
    x->in_value = false;
    x->in_scope_value = false;
    check_block(x, n->as.for_.body, false, NULL);
    x->in_scope_value = saved_scope_value;
    x->in_value = saved_value;
    x->loops--;
    x->scope = saved;
}

static PP_NORETURN void leave_error(Ctx* x, Node* n) {
    if (x->in_scope_value) {
        cerr(x->c, n->line, "'break', 'continue', 'return' and lift() cannot leave the block of a scope call that is used as a value");
    }

    cerr(x->c, n->line, "'break', 'continue' and 'return' cannot be used inside an if/match that is used as a value");
}

static void check_return(Ctx* x, Node* n) {
    Checker* c = x->c;
    if (x->in_value) {
        leave_error(x, n);
    }

    if (x->inst && x->inst->fn && x->inst->fn->sig && x->inst->fn->sig->generator) {
        // emit v / return v in a generator: v is one of its values (T of Generator<T>)
        SType* want = is_generator(c, x->ret_want) && x->ret_want->nargs ? x->ret_want->args[0] : NULL;
        if (!n->as.ret.value) {
            return;
        }

        SType* t = check_expr(x, n->as.ret.value, want);
        if (want && want->kind != ST_UNKNOWN && want->kind != ST_DYN && !st_fit(want, t, NULL)) {
            cerr(c, n->line, "value of generator '%s': expected %s but got %s", x->inst->fn->name->chars, tname(want), tname(t));
        }

        return;
    }

    SType* t = n->as.ret.value ? check_expr(x, n->as.ret.value, x->ret_want) : st_prim(ST_VOID);
    const char* fname = x->inst && x->inst->fn && x->inst->fn->name ? x->inst->fn->name->chars : "<top level>";
    if (!x->inst || !x->inst->fn) {
        return;
    }

    if (!x->ret_want) {
        x->ret_got = join(x->ret_got, t);
        x->returned = true;

        return;
    }

    if (x->ret_want->kind == ST_UNKNOWN) {
        return;
    }

    if (x->ret_want->kind == ST_VOID) {
        if (n->as.ret.value && t->kind != ST_VOID && t->kind != ST_NEVER && t->kind != ST_DYN) {
            cerr(c, n->line, "return value of '%s': expected void but got %s", fname, tname(t));
        }

        return;
    }

    if (!st_fit(x->ret_want, t, NULL)) {
        cerr(c, n->line, "return value of '%s': expected %s but got %s", fname, tname(x->ret_want), tname(t));
    }

    record_to_map(x, n->as.ret.value, x->ret_want, t);
}

static void check_import(Ctx* x, Node* n) {
    Checker* c = x->c;
    CModule* m = load_module(x, n, n->line);
    if (n->as.import.destructure) {
        for (int i = 0; i < n->as.import.nnames; ++i) {
            ObjString* name = n->as.import.names[i];
            CSym sym;
            if (!cmodule_export(c, m, name, &sym)) {
                cerr(c, n->line, "module '%s' does not export '%s'", m->name->chars, name->chars);
            }

            if (sym.kind == SYM_FUNCS) {
                for (int k = 0; k < sym.funcs->count; ++k) {
                    scope_add_func(c, x->scope, name, sym.funcs->items[k]);
                }
            } else {
                CSym* s = scope_define(c, x->scope, name, sym.kind, n->line);
                ObjString* keep = s->name;
                *s = sym;
                s->name = keep;
            }

            if (n->as.import.reexport) {
                module_add_export(x->module, name, name);
            }
        }

        return;
    }

    CSym* s = scope_define(c, x->scope, n->as.import.alias, SYM_MODULE, n->line);
    s->module = m;
    s->is_const = true;
    if (n->as.import.reexport) {
        module_add_export(x->module, n->as.import.alias, n->as.import.alias);
    }
}

void check_stmt(Ctx* x, Node* n) {
    Checker* c = x->c;
    switch (n->kind) {
        case S_EXPR: stmt_value(x, n, NULL); return;
        case S_LET: check_let(x, n); return;
        case S_ASSIGN: check_assign(x, n); return;
        case S_FUNCDECL: declare_function(x, n); return;
        case S_TYPE: {
            TDecl* d = tdecl_new(x, n->as.type_decl.def ? TD_ALIAS : TD_NATIVE, n->as.type_decl.name);
            d->generics = n->as.type_decl.generics;
            d->ngenerics = n->as.type_decl.ngenerics;
            d->def = n->as.type_decl.def;
            if (d->kind == TD_NATIVE) {
                d->which = is_name(d->name, "Map") ? NT_MAP : is_name(d->name, "Set") ? NT_SET : NT_CLASS;
                const bool prelude = x->module == c->builtins->module;
                if (prelude && is_name(d->name, "Generator")) {
                    d->which = NT_GENERATOR;
                }

                const char* lib = module_lib_path(x->module);
                const bool has_c = prelude || (lib && (vm_native_impls(c->vm, lib) || vm_native_type(c->vm, lib, d->name->chars)));
                if (!has_c) {
                    cerr(c, n->line, "native type '%s' can only be declared in a library module with a C half", d->name->chars);
                }
            }

            define_type(x, d);

            return;
        }
        case S_ENUM: check_enum(x, n); return;
        case S_IMPORT: check_import(x, n); return;
        case S_EXPORT:
            if (n->as.export_.decl) {
                check_stmt(x, n->as.export_.decl);
            }

            for (int i = 0; i < n->as.export_.count; ++i) {
                module_add_export(x->module, n->as.export_.aliases[i], n->as.export_.names[i]);
            }

            return;
        case S_INTERFACE: check_interface(x, n); return;
        case S_IMPL: check_impl(x, n); return;
        case S_PRECEDENCE: check_precedence(x, n); return;
        case S_FOR: check_for(x, n); return;
        case S_WHILE: {
            require_bool(x, check_expr(x, n->as.while_.cond, NULL), "while condition", n->line);
            x->loops++;
            const bool saved_value = x->in_value;
            x->in_value = false;
            check_block(x, n->as.while_.body, true, NULL);
            x->in_value = saved_value;
            x->loops--;

            return;
        }
        case S_BREAK:
        case S_CONTINUE:
            if (x->in_value) {
                leave_error(x, n);
            }

            return;
        case S_RETURN: check_return(x, n); return;
        case N_BLOCK: check_block(x, n, true, NULL); return;
        case S_DEFER: check_block(x, n->as.defer.body, true, NULL); return;
        default: check_expr(x, n, NULL); return;
    }
}



// ---------------------------------------------------------------- type_of

// type_of(x) is the static type of x, known at compile time: the call becomes a string literal.
// In a generic body the type can differ between instances; such a call stays a call (and the
// interpreter answers with the runtime type, like get_type).
static void fold_type_of(Ctx* x, Node* n, SType* t) {
    Checker* c = x->c;
    if (st_is_dyn(t)) {
        add_fold(c, n, x->inst, NULL);

        return;
    }

    add_fold(c, n, x->inst, tname(t));
}

// The parameter types f was declared with, and its instance for them (NULL for natives, or when a
// parameter is generic or untyped). Dynamic dispatch picks such an instance by the runtime values.
CInstance* checker_declared_instance(Checker* c, CFunc* f, SType** sargs, int sargc, SType*** params_out, int* nparams_out) {
    Ctx x = {0};
    x.c = c;
    x.scope = f->closure ? f->closure : c->builtins;
    x.module = f->module;
    const int np = func_nparams(f);
    SType** params = pp_calloc((size_t)np + 1, sizeof(SType*));
    SBindings gen;
    func_generics(f, &gen);
    SBindings bound = gen;
    for (int i = 0; i < np; ++i) {
        params[i] = param_type(&x, f, i, &gen);
    }

    for (int i = 0; i < np && i < sargc && sargs; ++i) {
        if (!st_has_param(params[i])) {
            continue;
        }

        if (sargs[i]->kind == ST_IFACE) {
            // <K, V>impl Index<K, V> for Map<K, V> called on an Index<string, int>: K = string, V = int
            for (int k = 0; k < c->nimpls; ++k) {
                const CImpl* im = &c->impls[k];
                if (im->iface != sargs[i]->decl || !st_equal(im->target, params[i])) {
                    continue;
                }

                for (int a = 0; a < im->nargs && a < sargs[i]->nargs; ++a) {
                    if (im->args[a]->kind != ST_PARAM) {
                        continue;
                    }

                    for (int g = 0; g < bound.count; ++g) {
                        if (bound.names[g] == im->args[a]->name) {
                            bound.types[g] = sargs[i]->args[a];
                        }
                    }
                }
            }
        } else {
            st_fit(params[i], sargs[i], &bound);
        }
    }

    bool concrete = true;
    for (int i = 0; i < np; ++i) {
        params[i] = subst(params[i], &bound);
        if (st_has_param(params[i]) || params[i]->kind == ST_UNKNOWN || st_is_dyn(params[i])) {
            concrete = false;
        }
    }

    *params_out = params;
    *nparams_out = np;
    if (f->kind != CF_USER || !f->body || !concrete) {
        return NULL;
    }

    const char* saved = c->file;
    CInstance* inst = instantiate(&x, f, params, np, &bound, f->sig ? f->sig->line : 0);
    c->file = saved;

    return inst;
}

void checker_hooks(Checker* c) {
    hook_checker = c;
    st_callable_fits_hook = fits_callable;
}
