#include "check_int.h"



// The static checker: scopes, type resolution, modules and the entry points.
// Expressions and statements are in check_expr.c. See md/STATIC_TYPING.md.

extern bool (*st_implements_hook)(TDecl* iface, SType** args, int nargs, SType* got, SBindings* b);
extern bool (*st_callable_fits_hook)(SType* want, SType* got);
extern SType* (*st_fill_hook)(SType* t);

static Checker* the_checker;



// ---------------------------------------------------------------- errors

PP_NORETURN void cerr(Checker* c, int line, const char* fmt, ...) {
    fflush(stdout);
    fprintf(stderr, "%s:%d: error: ", c->file ? c->file : "<input>", line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    c->failed = true;
    longjmp(c->jb, 1);
}

static void note_reason(Checker* c, const char* reason, int line) {
    for (int i = 0; i < c->nreasons; ++i) {
        if (strcmp(c->reasons[i].reason, reason) == 0) {
            c->reasons[i].count++;

            return;
        }
    }

    GROW(c->reasons, c->nreasons, c->capreasons);
    DynReason* r = &c->reasons[c->nreasons++];
    r->reason = reason;
    r->count = 1;
    r->file = c->file;
    r->line = line;
}

SType* dyn_type(Ctx* x, Node* n, const char* reason) {
    note_reason(x->c, reason, n ? n->line : 0);
    if (x->inst && !x->inst->dyn_reason) {
        x->inst->dyn_reason = reason;
        x->inst->dyn_line = n ? n->line : 0;
    }

    return st_prim(ST_DYN);
}

void count_type(Ctx* x, SType* t) {
    if (st_is_dyn(t)) {
        x->c->dyn++;
        if (x->inst) {
            x->inst->dyn++;
        }
    } else {
        x->c->typed++;
        if (x->inst) {
            x->inst->typed++;
        }
    }
}



// ---------------------------------------------------------------- scopes

CScope* scope_new(CScope* parent, CModule* m, CInstance* inst) {
    CScope* s = pp_calloc(1, sizeof(CScope));
    s->parent = parent;
    s->module = m ? m : (parent ? parent->module : NULL);
    s->inst = inst ? inst : (parent ? parent->inst : NULL);

    return s;
}

static bool same_name(const ObjString* a, const ObjString* b) {
    return a == b || (a->len == b->len && memcmp(a->chars, b->chars, (size_t)a->len) == 0);
}

CSym* scope_lookup_here(CScope* s, ObjString* name) {
    for (int i = s->count - 1; i >= 0; --i) {
        if (same_name(s->syms[i].name, name)) {
            return &s->syms[i];
        }
    }

    return NULL;
}

CSym* scope_lookup(CScope* s, ObjString* name) {
    for (; s; s = s->parent) {
        CSym* sym = scope_lookup_here(s, name);
        if (sym) {
            return sym;
        }
    }

    return NULL;
}

CSym* scope_define(Checker* c, CScope* s, ObjString* name, SymKind kind, int line) {
    CSym* sym = scope_lookup_here(s, name);
    if (!sym) {
        GROW(s->syms, s->count, s->cap);
        sym = &s->syms[s->count++];
    }

    memset(sym, 0, sizeof *sym);
    sym->name = name;
    sym->kind = kind;

    return sym;
}

static void set_add(CFuncSet* set, CFunc* f) {
    for (int i = 0; i < set->count; ++i) {
        if (set->items[i] == f) {
            return;
        }
    }

    GROW(set->items, set->count, set->cap);
    set->items[set->count++] = f;
}

static CFuncSet* set_new(ObjString* name) {
    CFuncSet* set = pp_calloc(1, sizeof(CFuncSet));
    set->name = name;

    return set;
}

// a function declared in scope s joins the overload set of its name; a set from an outer
// scope (or the builtins) is copied first, so the outer functions stay candidates
void scope_add_func(Checker* c, CScope* s, ObjString* name, CFunc* f) {
    CSym* here = scope_lookup_here(s, name);
    if (here && here->kind == SYM_FUNCS) {
        set_add(here->funcs, f);

        return;
    }

    CFuncSet* set = set_new(name);
    const CSym* outer = s->parent ? scope_lookup(s->parent, name) : NULL;
    if (outer && outer->kind == SYM_FUNCS) {
        for (int i = 0; i < outer->funcs->count; ++i) {
            set_add(set, outer->funcs->items[i]);
        }
    }

    set_add(set, f);
    CSym* sym = scope_define(c, s, name, SYM_FUNCS, 0);
    sym->funcs = set;
    sym->is_const = true;
}

CFuncSet* global_funcs(Checker* c, ObjString* name) {
    for (int i = 0; i < c->nglobals; ++i) {
        if (same_name(c->globals[i]->name, name)) {
            return c->globals[i];
        }
    }

    return NULL;
}

void global_add(Checker* c, ObjString* name, CFunc* f) {
    CFuncSet* set = global_funcs(c, name);
    if (!set) {
        set = set_new(name);
        GROW(c->globals, c->nglobals, c->capglobals);
        c->globals[c->nglobals++] = set;
    }

    set_add(set, f);
}

CFunc* func_new(Checker* c, CFuncKind kind, ObjString* name, Signature* sig, CScope* closure) {
    CFunc* f = pp_calloc(1, sizeof(CFunc));
    f->kind = kind;
    f->name = name;
    f->sig = sig;
    f->closure = closure;
    f->module = closure ? closure->module : NULL;
    f->id = c->next_id++;

    return f;
}



// ---------------------------------------------------------------- node info

static uint32_t ptr_hash(const void* p) {
    uintptr_t v = (uintptr_t)p;
    v ^= v >> 17;
    v *= 0xed5ad4bbu;
    v ^= v >> 11;

    return (uint32_t)v;
}

NodeInfo* instance_info(CInstance* inst, Node* n) {
    if (!inst || !inst->capslots) {
        return NULL;
    }

    uint32_t i = ptr_hash(n) & (uint32_t)(inst->capslots - 1);
    for (;;) {
        InfoSlot* s = &inst->slots[i];
        if (!s->node) {
            return NULL;
        }

        if (s->node == n) {
            return s->info;
        }

        i = (i + 1) & (uint32_t)(inst->capslots - 1);
    }
}

static void slot_insert(CInstance* inst, Node* n, NodeInfo* info) {
    uint32_t i = ptr_hash(n) & (uint32_t)(inst->capslots - 1);
    while (inst->slots[i].node) {
        i = (i + 1) & (uint32_t)(inst->capslots - 1);
    }

    inst->slots[i].node = n;
    inst->slots[i].info = info;
    inst->nslots++;
}

// The info of node n, created when missing. The NodeInfo itself never moves (the table only
// holds pointers), so a caller may keep it while checking other nodes.
NodeInfo* info_put(CInstance* inst, Node* n) {
    NodeInfo* found = instance_info(inst, n);
    if (found) {
        return found;
    }

    if ((inst->nslots + 1) * 2 > inst->capslots) {
        const int old_cap = inst->capslots;
        InfoSlot* old = inst->slots;
        inst->capslots = old_cap ? old_cap * 2 : 64;
        inst->slots = pp_calloc((size_t)inst->capslots, sizeof(InfoSlot));
        inst->nslots = 0;
        for (int k = 0; k < old_cap; ++k) {
            if (old[k].node) {
                slot_insert(inst, old[k].node, old[k].info);
            }
        }

        free(old);
    }

    NodeInfo* info = pp_calloc(1, sizeof(NodeInfo));
    info->node = n;
    slot_insert(inst, n, info);

    return info;
}

CInstance* cinstance_new(Checker* c, CFunc* fn, CModule* m) {
    CInstance* inst = pp_calloc(1, sizeof(CInstance));
    inst->fn = fn;
    inst->module = m;
    inst->id = c->ninsts;
    GROW(c->all_insts, c->ninsts, c->capinsts);
    c->all_insts[c->ninsts++] = inst;

    return inst;
}



// ---------------------------------------------------------------- types

static SType* named_decl_type(Checker* c, TDecl* d, SType** args, int nargs);

SType* enum_type(Checker* c, TDecl* en, SType** args, int nargs) {
    SType* t = st_new(ST_ENUM);
    t->decl = en;
    t->nargs = en->ngenerics;
    if (t->nargs) {
        t->args = pp_calloc((size_t)t->nargs, sizeof(SType*));
        for (int i = 0; i < t->nargs; ++i) {
            t->args[i] = i < nargs && args[i] ? args[i] : NULL;
        }
    }

    // missing arguments take their defaults (Error<S, F = string>), else they are not known yet
    for (int i = 0; i < t->nargs; ++i) {
        if (t->args[i]) {
            continue;
        }

        if (en->defaults && en->defaults[i]) {
            Ctx x = {0};
            x.c = c;
            t->args[i] = resolve_type(&x, en->defaults[i], en->scope, NULL);
        } else {
            t->args[i] = st_prim(ST_NEVER);
        }
    }

    return t;
}

SType* generator_of(Checker* c, SType* elem) {
    if (!c->generator) {
        return st_prim(ST_DYN);
    }

    SType* t = st_new(ST_NATIVE);
    t->decl = c->generator;
    t->nargs = 1;
    t->args = pp_calloc(2, sizeof(SType*));
    t->args[0] = elem;

    return t;
}

bool is_generator(Checker* c, const SType* t) {
    return t && t->kind == ST_NATIVE && t->decl && t->decl->which == NT_GENERATOR;
}

SType* option_of(Checker* c, SType* t) {
    if (!c->option) {
        return st_prim(ST_DYN);
    }

    return enum_type(c, c->option, &t, 1);
}

static SBindings decl_bindings(TDecl* d, SType** args, int nargs) {
    SBindings b;
    memset(&b, 0, sizeof b);
    b.count = d->ngenerics < PP_MAX_BINDINGS ? d->ngenerics : PP_MAX_BINDINGS;
    for (int i = 0; i < b.count; ++i) {
        b.names[i] = d->generics[i];
        b.types[i] = i < nargs ? args[i] : st_prim(ST_UNKNOWN);
    }

    return b;
}

SType* variant_payload(Checker* c, SType* en, int variant, int k) {
    TDecl* d = en->decl;
    if (variant < 0 || variant >= d->nvariants || k >= d->variants[variant].npayload) {
        return st_prim(ST_DYN);
    }

    SBindings b = decl_bindings(d, en->args, en->nargs);
    Ctx x = {0};
    x.c = c;

    return resolve_type(&x, d->variants[variant].payload[k], d->scope, &b);
}

SType* st_record_fill(Checker* c, SType* t) {
    if (!t || t->kind != ST_RECORD || !t->decl || t->fnames) {
        return t;
    }

    const Type* def = t->decl->def;
    if (!def || def->kind != TY_RECORD) {
        return t;
    }

    SBindings b = decl_bindings(t->decl, t->args, t->nargs);
    Ctx x = {0};
    x.c = c;
    ObjString** names = pp_calloc((size_t)(def->nfields + 1), sizeof(ObjString*));
    SType** types = pp_calloc((size_t)(def->nfields + 1), sizeof(SType*));
    // set first: a record that contains itself (type Node = { Node[] children }) finds it filled
    t->fnames = names;
    t->ftypes = types;
    t->nfields = def->nfields;
    t->rest = def->rest;
    for (int i = 0; i < def->nfields; ++i) {
        names[i] = def->fnames[i];
        types[i] = st_prim(ST_DYN);
    }

    for (int i = 0; i < def->nfields; ++i) {
        types[i] = resolve_type(&x, def->ftypes[i], t->decl->scope, &b);
    }

    return t;
}

static SType* fill_hook(SType* t) {
    return st_record_fill(the_checker, t);
}

static SType* named_decl_type(Checker* c, TDecl* d, SType** args, int nargs) {
    switch (d->kind) {
        case TD_ENUM: return enum_type(c, d, args, nargs);
        case TD_NATIVE:
        case TD_IFACE: {
            SType* t = st_new(d->kind == TD_NATIVE ? ST_NATIVE : ST_IFACE);
            t->decl = d;
            t->nargs = nargs;
            t->args = args;
            if (d->kind == TD_IFACE && nargs < d->ngenerics) {
                // a bare interface name inside its own declaration or with missing arguments: any
                t->nargs = d->ngenerics;
                t->args = pp_calloc((size_t)d->ngenerics, sizeof(SType*));
                for (int i = 0; i < d->ngenerics; ++i) {
                    t->args[i] = i < nargs ? args[i] : st_prim(ST_UNKNOWN);
                }
            }

            return t;
        }
        case TD_ALIAS: {
            if (d->def && d->def->kind == TY_RECORD) {
                SType* t = st_new(ST_RECORD);
                t->decl = d;
                t->nargs = d->ngenerics;
                t->args = pp_calloc((size_t)(d->ngenerics + 1), sizeof(SType*));
                for (int i = 0; i < d->ngenerics; ++i) {
                    t->args[i] = i < nargs ? args[i] : st_prim(ST_UNKNOWN);
                }

                return t;
            }

            SBindings b = decl_bindings(d, args, nargs);
            Ctx x = {0};
            x.c = c;
            SType* t = resolve_type(&x, d->def, d->scope, &b);
            if (t->kind == ST_FUNC && !d->ngenerics) {
                SType* named = st_new(ST_FUNC);
                *named = *t;
                named->alias = d->name;

                return named;
            }

            return t;
        }
    }

    return st_prim(ST_DYN);
}

SType* named_record(Checker* c, TDecl* d) {
    return named_decl_type(c, d, NULL, 0);
}

static SType* resolve_name(Ctx* x, Type* t, CScope* s, SBindings* gen) {
    Checker* c = x->c;
    if (!t->nquals) {
        for (int i = 0; gen && i < gen->count; ++i) {
            if (same_name(gen->names[i], t->name)) {
                if (gen->types[i]) {
                    return gen->types[i];
                }

                SType* p = st_new(ST_PARAM);
                p->name = gen->names[i];

                return p;
            }
        }

        if (same_name(t->name, c->s_unknown)) {
            return st_prim(ST_UNKNOWN);
        }

        if (same_name(t->name, c->s_block) || same_name(t->name, c->s_callback)) {
            SType* b = st_new(ST_BLOCK);
            b->variadic = same_name(t->name, c->s_callback);
            b->elem = t->nargs ? resolve_type(x, t->args[0], s, gen) : st_prim(ST_VOID);

            return b;
        }
    }

    CSym* sym = NULL;
    if (t->nquals) {
        CSym ms;
        const CSym* first = scope_lookup(s, t->quals[0]);
        if (!first || first->kind != SYM_MODULE) {
            return dyn_type(x, NULL, "a type from a module that is not imported");
        }

        CModule* m = first->module;
        for (int i = 1; i < t->nquals; ++i) {
            if (!cmodule_export(c, m, t->quals[i], &ms) || ms.kind != SYM_MODULE) {
                return dyn_type(x, NULL, "a type from a module that is not imported");
            }

            m = ms.module;
        }

        static CSym found;
        if (!cmodule_export(c, m, t->name, &found)) {
            return dyn_type(x, NULL, "a type a module does not export");
        }

        sym = &found;
    } else {
        sym = scope_lookup(s, t->name);
    }

    if (!sym || sym->kind != SYM_TYPE) {
        return dyn_type(x, NULL, "a type name the checker cannot find");
    }

    if (!sym->tdecl) {
        return sym->type ? sym->type : st_prim(ST_DYN);
    }

    const TDecl* d = sym->tdecl;
    const int need = d->ngenerics;
    int least = need;
    for (int i = 0; d->defaults && i < need; ++i) {
        if (d->defaults[i] && i < least) {
            least = i;
        }
    }

    if (t->nargs && (t->nargs > need || t->nargs < least)) {
        StrBuf qn;
        sb_init(&qn);
        for (int i = 0; i < t->nquals; ++i) {
            sb_printf(&qn, "%s:", t->quals[i]->chars);
        }

        sb_puts(&qn, t->name->chars);
        cerr(c, t->line, "type '%s' takes %d type argument(s) but %d were given", qn.data, need, t->nargs);
    }

    SType** args = NULL;
    if (t->nargs) {
        args = pp_calloc((size_t)t->nargs, sizeof(SType*));
        for (int i = 0; i < t->nargs; ++i) {
            args[i] = resolve_type(x, t->args[i], s, gen);
        }
    }

    return named_decl_type(c, sym->tdecl, args, t->nargs);
}

SType* resolve_type(Ctx* x, Type* t, CScope* s, SBindings* gen) {
    if (!t) {
        return st_prim(ST_UNKNOWN);
    }

    switch (t->kind) {
        case TY_PRIM:
            switch (t->prim) {
                case PRIM_INT: return st_prim(ST_INT);
                case PRIM_FLOAT: return st_prim(ST_FLOAT);
                case PRIM_STRING: return st_prim(ST_STRING);
                case PRIM_BOOL: return st_prim(ST_BOOL);
                case PRIM_VOID: return st_prim(ST_VOID);
                case PRIM_RANGE: {
                    if (!t->inner) {
                        return st_prim(ST_RANGE);
                    }

                    SType* r = st_new(ST_RANGE);
                    r->elem = resolve_type(x, t->inner, s, gen);

                    return r;
                }
                case PRIM_FUNCTION: return st_prim(ST_FUNCTION);
                case PRIM_OBJECT: return st_prim(ST_OBJECT);
                case PRIM_ANY: return st_prim(ST_UNKNOWN);
                case PRIM_TYPE: {
                    SType* ty = st_new(ST_TYPE);

                    return ty;
                }
            }

            return st_prim(ST_DYN);
        case TY_NAME: return resolve_name(x, t, s, gen);
        case TY_ARRAY: return st_array(resolve_type(x, t->inner, s, gen));
        case TY_UNION: return st_union(resolve_type(x, t->a, s, gen), resolve_type(x, t->b, s, gen));
        case TY_RECORD: {
            SType* r = st_new(ST_RECORD);
            r->nfields = t->nfields;
            r->rest = t->rest;
            r->fnames = pp_calloc((size_t)(t->nfields + 1), sizeof(ObjString*));
            r->ftypes = pp_calloc((size_t)(t->nfields + 1), sizeof(SType*));
            for (int i = 0; i < t->nfields; ++i) {
                r->fnames[i] = t->fnames[i];
                r->ftypes[i] = resolve_type(x, t->ftypes[i], s, gen);
            }

            return r;
        }
        case TY_FUNC: {
            SType* f = st_new(ST_FUNC);
            f->variadic = t->variadic;
            f->nargs = t->nargs;
            f->args = pp_calloc((size_t)(t->nargs + 1), sizeof(SType*));
            for (int i = 0; i < t->nargs; ++i) {
                f->args[i] = resolve_type(x, t->args[i], s, gen);
            }

            f->elem = t->inner ? resolve_type(x, t->inner, s, gen) : NULL;

            return f;
        }
        case TY_TUPLE: {
            SType* tp = st_new(ST_TUPLE);
            tp->nargs = t->nargs;
            tp->args = pp_calloc((size_t)t->nargs, sizeof(SType*));
            for (int i = 0; i < t->nargs; ++i) {
                tp->args[i] = resolve_type(x, t->args[i], s, gen);
            }

            return tp;
        }
    }

    return st_prim(ST_DYN);
}

// replaces bound generic parameters
SType* subst(SType* t, SBindings* b) {
    if (!t || !b || !b->count) {
        return t;
    }

    switch (t->kind) {
        case ST_PARAM:
            for (int i = 0; i < b->count; ++i) {
                if (b->names[i] == t->name) {
                    return b->types[i] ? b->types[i] : st_prim(ST_NEVER);
                }
            }

            return t;
        case ST_ARRAY: return st_array(subst(t->elem, b));
        case ST_RANGE: {
            if (!t->elem) {
                return t;
            }

            SType* r = st_new(ST_RANGE);
            r->elem = subst(t->elem, b);

            return r;
        }
        case ST_BLOCK: {
            SType* r = st_new(ST_BLOCK);
            r->variadic = t->variadic;
            r->elem = subst(t->elem, b);

            return r;
        }
        case ST_UNION: {
            SType* r = st_prim(ST_NEVER);
            for (int i = 0; i < t->nargs; ++i) {
                r = st_union(r, subst(t->args[i], b));
            }

            return r;
        }
        case ST_TUPLE:
        case ST_ENUM:
        case ST_NATIVE:
        case ST_IFACE:
        case ST_FUNC: {
            SType* r = st_new(t->kind);
            *r = *t;
            r->args = pp_calloc((size_t)(t->nargs + 1), sizeof(SType*));
            for (int i = 0; i < t->nargs; ++i) {
                r->args[i] = subst(t->args[i], b);
            }

            if (t->kind == ST_FUNC && t->elem) {
                r->elem = subst(t->elem, b);
            }

            return r;
        }
        case ST_RECORD: {
            SType* r = st_new(ST_RECORD);
            *r = *t;
            if (t->decl) {
                r->args = pp_calloc((size_t)(t->nargs + 1), sizeof(SType*));
                for (int i = 0; i < t->nargs; ++i) {
                    r->args[i] = subst(t->args[i], b);
                }

                r->fnames = NULL;
                r->ftypes = NULL;
                r->nfields = 0;

                return r;
            }

            r->ftypes = pp_calloc((size_t)(t->nfields + 1), sizeof(SType*));
            for (int i = 0; i < t->nfields; ++i) {
                r->ftypes[i] = subst(t->ftypes[i], b);
            }

            return r;
        }
        default: return t;
    }
}



// ---------------------------------------------------------------- interfaces

// the wanted type arguments of an interface against the ones an impl (or a parent) gives: binds generics in b.
// A side the impl leaves open (an unbound parameter of the impl) accepts anything.
static bool iface_args_fit(SType** want, int nwant, SType** have, int nhave, SBindings* b) {
    for (int i = 0; i < nwant && i < nhave; ++i) {
        if (!want[i] || !have[i] || st_has_param(have[i])) {
            continue;
        }

        if (b) {
            if (!st_fit(want[i], have[i], b)) {
                return false;
            }
        } else if (!st_has_param(want[i]) && !st_fit(want[i], have[i], NULL)) {
            return false;
        }
    }

    return true;
}

bool st_implements(Checker* c, TDecl* iface, SType** args, int nargs, SType* got, SBindings* b) {
    if (got->kind == ST_IFACE) {
        // an interface value fits a parent interface
        if (got->decl == iface) {
            return iface_args_fit(args, nargs, got->args, got->nargs, b);
        }

        for (int i = 0; i < got->decl->nparents; ++i) {
            Ctx x = {0};
            x.c = c;
            SBindings gb;
            memset(&gb, 0, sizeof gb);
            gb.count = got->decl->ngenerics < PP_MAX_BINDINGS ? got->decl->ngenerics : PP_MAX_BINDINGS;
            for (int k = 0; k < gb.count; ++k) {
                gb.names[k] = got->decl->generics[k];
                gb.types[k] = k < got->nargs ? got->args[k] : NULL;
            }

            const SType* p = resolve_type(&x, got->decl->parents[i], got->decl->scope, &gb);
            if (p->kind == ST_IFACE && p->decl == iface) {
                return iface_args_fit(args, nargs, p->args, p->nargs, b);
            }
        }

        return false;
    }

    for (int i = 0; i < c->nimpls; ++i) {
        CImpl* im = &c->impls[i];
        if (im->iface != iface) {
            continue;
        }

        SBindings ib;
        memset(&ib, 0, sizeof ib);
        ib.count = im->ngenerics;
        for (int k = 0; k < im->ngenerics && k < PP_MAX_BINDINGS; ++k) {
            ib.names[k] = im->generics[k];
        }

        if (st_fit(im->target, got, &ib) >= 2 || (im->target->kind != ST_UNKNOWN && st_equal(im->target, got))) {
            SType** have = pp_calloc((size_t)(im->nargs + 1), sizeof(SType*));
            for (int k = 0; k < im->nargs; ++k) {
                have[k] = im->args[k] ? subst(im->args[k], &ib) : NULL;
            }

            // a trial on a copy: a failed impl must not leave bindings behind
            SBindings trial;
            if (b) {
                trial = *b;
            }

            if (iface_args_fit(args, nargs, have, im->nargs, b ? &trial : NULL)) {
                if (b) {
                    *b = trial;
                }

                return true;
            }
        }
    }

    return false;
}

static bool implements_hook(TDecl* iface, SType** args, int nargs, SType* got, SBindings* b) {
    return st_implements(the_checker, iface, args, nargs, got, b);
}



// ---------------------------------------------------------------- precedence

double prec_of(Checker* c, ObjString* op) {
    for (int i = 0; i < c->nprecs; ++i) {
        if (same_name(c->precs[i].op, op)) {
            return c->precs[i].prec;
        }
    }

    return 65.0;
}

void set_prec(Checker* c, ObjString* op, double p) {
    for (int i = 0; i < c->nprecs; ++i) {
        if (same_name(c->precs[i].op, op)) {
            c->precs[i].prec = p;

            return;
        }
    }

    GROW(c->precs, c->nprecs, c->capprecs);
    c->precs[c->nprecs].op = op;
    c->precs[c->nprecs].prec = p;
    c->nprecs++;
}



// ---------------------------------------------------------------- modules

static CModule* module_alloc(Checker* c, ObjString* name, const char* path) {
    CModule* m = pp_calloc(1, sizeof(CModule));
    m->name = name;
    m->path = path;
    GROW(c->modules, c->nmodules, c->capmodules);
    c->modules[c->nmodules++] = m;

    return m;
}

static void add_export(CModule* m, ObjString* name, ObjString* src) {
    for (int i = 0; i < m->nexports; ++i) {
        if (same_name(m->exp_names[i], name)) {
            m->exp_src[i] = src;

            return;
        }
    }

    if (m->nexports >= m->capexports) {
        m->capexports = m->capexports ? m->capexports * 2 : 8;
        m->exp_names = pp_realloc(m->exp_names, sizeof(ObjString*) * (size_t)m->capexports);
        m->exp_src = pp_realloc(m->exp_src, sizeof(ObjString*) * (size_t)m->capexports);
    }

    m->exp_names[m->nexports] = name;
    m->exp_src[m->nexports] = src;
    m->nexports++;
}

void module_add_export(CModule* m, ObjString* name, ObjString* src) {
    add_export(m, name, src);
}

bool cmodule_export(Checker* c, CModule* m, ObjString* name, CSym* out) {
    for (int i = 0; i < m->nexports; ++i) {
        if (same_name(m->exp_names[i], name)) {
            const CSym* sym = scope_lookup_here(m->scope, m->exp_src[i]);
            if (!sym) {
                return false;
            }

            *out = *sym;

            return true;
        }
    }

    return false;
}

static SType* value_stype(Value v) {
    switch (v.kind) {
        case VAL_BOOL: return st_prim(ST_BOOL);
        case VAL_INT: return st_prim(ST_INT);
        case VAL_FLOAT: return st_prim(ST_FLOAT);
        case VAL_OBJ:
            if (IS_KIND(v, OBJ_STRING)) {
                return st_prim(ST_STRING);
            }

            return st_prim(ST_DYN);
        default: return st_prim(ST_VOID);
    }
}

static CModule* native_module(Checker* c, ObjModule* rt, const char* key);

// the functions, constants and submodules of a runtime environment, as symbols
static void import_env(Checker* c, CScope* s, ObjEnv* env, bool builtin) {
    for (int i = 0; i < env->vars.count; ++i) {
        const Entry* e = &env->vars.entries[i];
        const Value v = e->value;
        if (IS_KIND(v, OBJ_OVERLOAD)) {
            const ObjOverload* set = (ObjOverload*)v.as.o;
            for (int k = 0; k < set->count; ++k) {
                if (set->items[k]->kind != OBJ_NATIVE) {
                    continue;
                }

                ObjNative* nat = (ObjNative*)set->items[k];
                CFunc* f = func_new(c, CF_NATIVE, e->key, nat->sig, c->builtins);
                f->native = nat;
                f->builtin = builtin;
                f->module = s->module; // its types resolve among the builtins, but it belongs to its module
                scope_add_func(c, s, e->key, f);
            }

            continue;
        }

        if (IS_KIND(v, OBJ_NATIVE)) {
            ObjNative* nat = (ObjNative*)v.as.o;
            CFunc* f = func_new(c, CF_NATIVE, e->key, nat->sig, c->builtins);
            f->native = nat;
            f->builtin = builtin;
            f->module = s->module;
            scope_add_func(c, s, e->key, f);
            continue;
        }

        if (builtin) {
            continue; // the prelude's own declarations come from checking its source
        }

        if (IS_KIND(v, OBJ_MODULE)) {
            CSym* sym = scope_define(c, s, e->key, SYM_MODULE, 0);
            sym->module = native_module(c, (ObjModule*)v.as.o, NULL);
            sym->is_const = true;
            continue;
        }

        CSym* sym = scope_define(c, s, e->key, SYM_VAR, 0);
        sym->type = value_stype(v);
        sym->is_const = (e->flags & VAR_CONST) != 0;
    }
}

static CModule* native_module(Checker* c, ObjModule* rt, const char* key) {
    for (int i = 0; i < c->nmodules; ++i) {
        if (c->modules[i]->rt == rt) {
            return c->modules[i];
        }
    }

    CModule* m = module_alloc(c, rt->name, key ? key : "native");
    m->native = true;
    m->rt = rt;
    m->checked = true;
    m->scope = scope_new(c->builtins, m, NULL);
    import_env(c, m->scope, rt->env, false);
    for (int i = 0; i < rt->exports.count; ++i) {
        add_export(m, rt->exports.entries[i].key, AS_STR(rt->exports.entries[i].value));
    }

    return m;
}

static CModule* find_module(Checker* c, const char* path) {
    for (int i = 0; i < c->nmodules; ++i) {
        if (strcmp(c->modules[i]->path, path) == 0) {
            return c->modules[i];
        }
    }

    return NULL;
}

static void check_module_body(Checker* c, CModule* m, Node* program);

// a .pp module, or a native one; NULL when there is none
static CModule* open_module(Ctx* x, const char* path, bool is_file, int line, bool required) {
    Checker* c = x->c;
    if (!is_file) {
        NativeModuleInit init = vm_native_module(c->vm, path);
        if (init) {
            char key[256];
            snprintf(key, sizeof key, "native:%s", path);
            CModule* m = find_module(c, key);
            if (m) {
                return m;
            }

            ObjModule* rt = init(c->vm);
            m = native_module(c, rt, NULL);
            m->path = pp_strdup(key);

            return m;
        }
    }

    char* abs = vm_find_module_file(c->vm, path, is_file, c->file);
    if (!abs) {
        if (!required) {
            return NULL;
        }

        if (is_file) {
            cerr(c, line, "cannot find file \"%s\"", path);
        }

        cerr(c, line, "cannot find module '%s'", path);
    }

    CModule* m = find_module(c, abs);
    if (m) {
        free(abs);

        return m; // cyclic imports see a partial module, like at runtime
    }

    size_t len;
    char* src = pp_read_file(abs, &len);
    if (!src) {
        cerr(c, line, "cannot read module '%s'", abs);
    }

    Node* program = parse_program(c->vm, src, abs);
    if (!program) {
        c->failed = true;
        longjmp(c->jb, 1);
    }

    const char* base = abs;
    for (const char* p = abs; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }

    int blen = (int)strlen(base);
    if (blen > 3 && strcmp(base + blen - 3, ".pp") == 0) {
        blen -= 3;
    }

    m = module_alloc(c, str_intern(c->vm, base, blen), abs);
    m->program = program;
    m->scope = scope_new(c->builtins, m, NULL);
    check_module_body(c, m, program);

    return m;
}

CModule* load_module(Ctx* x, Node* n, int line) {
    Checker* c = x->c;
    const int nsubs = n->as.import.nsubs;
    CModule* m = open_module(x, n->as.import.path->chars, n->as.import.is_file, line, nsubs == 0);
    if (nsubs == 0) {
        return m;
    }

    StrBuf path, shown;
    sb_init(&path);
    sb_init(&shown);
    sb_puts(&path, n->as.import.path->chars);
    sb_printf(&shown, n->as.import.is_file ? "\"%s\"" : "%s", n->as.import.path->chars);
    for (int i = 0; i < nsubs; ++i) {
        ObjString* seg = n->as.import.subs[i];
        sb_printf(&path, "/%s", seg->chars);
        CSym sym;
        if (m && cmodule_export(c, m, seg, &sym)) {
            if (sym.kind != SYM_MODULE) {
                const char* what = sym.kind == SYM_FUNCS ? "function" : sym.kind == SYM_TYPE ? "type" : "value";
                cerr(c, line, "%s:%s is a %s, not a module (use import %s -> { %s })", shown.data, seg->chars, what,
                     shown.data, seg->chars);
            }

            m = sym.module;
        } else {
            CModule* sub = open_module(x, path.data, n->as.import.is_file, line, false);
            if (!sub) {
                cerr(c, line, "cannot find module %s:%s (%s, and there is no file %s.pp)", shown.data, seg->chars,
                     m ? "it is not exported by its parent" : "the parent module does not exist", path.data);
            }

            m = sub;
        }

        sb_printf(&shown, ":%s", seg->chars);
    }

    sb_free(&path);
    sb_free(&shown);

    return m;
}

static void check_module_body(Checker* c, CModule* m, Node* program) {
    const char* saved_file = c->file;
    c->file = m->path;
    CInstance* inst = cinstance_new(c, NULL, m);
    m->top = inst;
    inst->checking = true;
    // the precedences a module sets apply to the module, as the runtime's do from where they run
    Ctx x = {0};
    x.c = c;
    x.scope = m->scope;
    m->scope->inst = inst;
    x.inst = inst;
    x.module = m;
    x.ret_want = st_prim(ST_UNKNOWN);
    for (int i = 0; i < program->as.block.stmts.count; ++i) {
        check_stmt(&x, program->as.block.stmts.items[i]);
    }

    // functions written but never called are checked too, with their declared types
    for (int i = 0; i < m->scope->count; ++i) {
        const CSym* sym = &m->scope->syms[i];
        if (sym->kind != SYM_FUNCS) {
            continue;
        }

        for (int k = 0; k < sym->funcs->count; ++k) {
            CFunc* f = sym->funcs->items[k];
            if (f->kind == CF_USER && f->body && f->ninsts == 0 && f->module == m) {
                func_value_type(&x, f);
            }
        }
    }

    for (int i = 0; i < m->nexports; ++i) {
        if (!scope_lookup_here(m->scope, m->exp_src[i])) {
            cerr(c, 0, "'%s' is exported but never defined at the top level of the module", m->exp_src[i]->chars);
        }
    }

    inst->checking = false;
    inst->checked = true;
    m->checked = true;
    c->file = saved_file;
}

// a library module's path below lib/ without .pp ("std", "os/fs"): the name its C half is registered under
const char* module_lib_path(CModule* m) {
    if (!m || !m->path) {
        return NULL;
    }

    if (strcmp(m->path, "<prelude>") == 0) {
        return PP_PRELUDE_NATIVES; // Map and Set: the prelude's natives
    }

    const char* rel = NULL;
    for (const char* s = m->path; *s; ++s) {
        if ((s[0] == '/' || s[0] == '\\') && strncmp(s + 1, "lib", 3) == 0 && (s[4] == '/' || s[4] == '\\')) {
            rel = s + 5;
        }
    }

    if (!rel) {
        return NULL;
    }

    static char path[256];
    snprintf(path, sizeof path, "%s", rel);
    const size_t n = strlen(path);
    if (n > 3 && strcmp(path + n - 3, ".pp") == 0) {
        path[n - 3] = '\0';
    }

    for (char* s = path; *s; ++s) {
        if (*s == '\\') {
            *s = '/';
        }
    }

    return path;
}



// ---------------------------------------------------------------- entry

Checker* checker_new(VM* vm) {
    Checker* c = pp_calloc(1, sizeof(Checker));
    c->vm = vm;
    the_checker = c;
    st_implements_hook = implements_hook;
    st_fill_hook = fill_hook;
    checker_hooks(c);
    c->s_and = str_cintern(vm, "&&");
    c->s_or = str_cintern(vm, "||");
    c->s_index = str_cintern(vm, "[]");
    c->s_index_set = str_cintern(vm, "[]=");
    c->s_scope = str_cintern(vm, "scope");
    c->s_block = str_cintern(vm, "Block");
    c->s_callback = str_cintern(vm, "Callback");
    c->s_unknown = str_cintern(vm, "unknown");
    c->s_in = str_cintern(vm, "in");
    for (int i = 0; i < vm->prec.count; ++i) {
        set_prec(c, vm->prec.entries[i].key, vm->prec.entries[i].value.as.f);
    }

    CModule* prelude = module_alloc(c, str_cintern(vm, "prelude"), "<prelude>");
    c->builtins = scope_new(NULL, prelude, NULL);
    prelude->scope = c->builtins;
    import_env(c, c->builtins, vm->builtins, true);
    if (setjmp(c->jb)) {
        fprintf(stderr, "internal error: the prelude does not pass the checker\n");

        return c;
    }

    Node* program = parse_program(vm, PP_PRELUDE, "<prelude>");
    check_module_body(c, prelude, program);
    const CSym* opt = scope_lookup(c->builtins, str_cintern(vm, "Option"));
    const CSym* err = scope_lookup(c->builtins, str_cintern(vm, "Error"));
    c->option = opt && opt->kind == SYM_TYPE ? opt->tdecl : NULL;
    c->error = err && err->kind == SYM_TYPE ? err->tdecl : NULL;
    static const char* names[] = {"Generator", "Iterable", "Iter", "KeyIterable"};
    TDecl** slots[] = {&c->generator, &c->iterable, &c->iter, &c->key_iterable};
    for (int i = 0; i < 4; ++i) {
        const CSym* s = scope_lookup(c->builtins, str_cintern(vm, names[i]));
        *slots[i] = s && s->kind == SYM_TYPE ? s->tdecl : NULL;
    }

    return c;
}

bool check_program(Checker* c, Node* program, const char* file) {
    if (setjmp(c->jb)) {
        return false;
    }

    CModule* m = module_alloc(c, str_cintern(c->vm, "main"), file);
    m->scope = scope_new(c->builtins, m, NULL);
    c->main = m;
    m->program = program;
    check_module_body(c, m, program);
    apply_folds(c);

    return !c->failed;
}

void add_fold(Checker* c, Node* n, CInstance* inst, const char* name) {
    for (int i = 0; i < c->nfolds; ++i) {
        if (c->folds[i].node == n && c->folds[i].inst == inst) {
            c->folds[i].name = name;

            return;
        }
    }

    GROW(c->folds, c->nfolds, c->capfolds);
    c->folds[c->nfolds].node = n;
    c->folds[c->nfolds].inst = inst;
    c->folds[c->nfolds].name = name;
    c->nfolds++;
}

// type_of(x) calls whose static type is the same in every instance become string literals
void apply_folds(Checker* c) {
    for (int i = 0; i < c->nfolds; ++i) {
        Node* n = c->folds[i].node;
        const char* name = c->folds[i].name;
        bool same = name != NULL;
        for (int k = 0; same && k < c->nfolds; ++k) {
            if (c->folds[k].node == n && (!c->folds[k].name || strcmp(c->folds[k].name, name) != 0)) {
                same = false;
            }
        }

        if (same && n->kind != N_STRING) {
            n->kind = N_STRING;
            n->as.str = str_cintern(c->vm, name);
        }
    }
}

Node* checker_module_ast(Checker* c, const char* abs_path) {
    for (int i = 0; i < c->nmodules; ++i) {
        if (c->modules[i]->program && strcmp(c->modules[i]->path, abs_path) == 0) {
            return c->modules[i]->program;
        }
    }

    return NULL;
}

CInstance* checker_main(Checker* c) {
    return c->main ? c->main->top : NULL;
}

int checker_typed_count(Checker* c) {
    return c->typed;
}

int checker_dyn_count(Checker* c) {
    return c->dyn;
}

void checker_report(Checker* c, FILE* out) {
    const int total = c->typed + c->dyn;
    fprintf(out, "%d expression(s) checked: %d statically typed, %d dynamic (%.1f%% typed)\n", total, c->typed, c->dyn,
            total ? 100.0 * c->typed / total : 100.0);
    for (int i = 0; i < c->nreasons; ++i) {
        const DynReason* r = &c->reasons[i];
        fprintf(out, "  %5d  %s (first: %s:%d)\n", r->count, r->reason, r->file ? r->file : "?", r->line);
    }
}
