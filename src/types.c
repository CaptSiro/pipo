/*
 * Runtime type matching.
 *
 * type_match() answers "does value v fit type t?" and, at the same time, how
 * well it fits. The score drives overload resolution: the candidate whose
 * parameters fit the arguments best (highest summed score) wins.
 *
 *   0   no match
 *   1   any / untyped
 *   2   generic parameter, int widened to float, plain `object`
 *   3   structural record match, function, callback type, empty typed array
 *   4   exact primitive / enum match, interface implemented by the value's type
 *   5   nominal record match (object declared as that type)
 *
 * Callback types and interfaces also need to compare a type with a type
 * ("does a function taking `float` fit `int -> void`?"). That is
 * type_accepts() further down. It is permissive where nothing is known
 * (untyped, `any`, unbound generics): calls are checked at runtime anyway.
 */
#include "vm.h"

static Binding* find_binding(Bindings* b, ObjString* name) {
    if (!b) return NULL;
    for (int i = 0; i < b->count; i++)
        if (b->items[i].name == name) return &b->items[i];
    return NULL;
}

static bool is_callable_obj(Obj* o) {
    return o->kind == OBJ_FUNCTION || o->kind == OBJ_NATIVE || o->kind == OBJ_OVERLOAD ||
           o->kind == OBJ_VARIANT_CTOR || o->kind == OBJ_TYPEDEF;
}

bool same_kind(Value a, Value b) {
    if (a.kind != b.kind) return false;
    if (a.kind != VAL_OBJ) return true;
    Obj* x = a.as.o;
    Obj* y = b.as.o;
    if (x->kind != y->kind) return is_callable_obj(x) && is_callable_obj(y);
    if (x->kind == OBJ_OBJECT) return ((ObjObject*)x)->tag == ((ObjObject*)y)->tag;
    if (x->kind == OBJ_ENUM_VALUE) return ((ObjEnumValue*)x)->en == ((ObjEnumValue*)y)->en;
    return true;
}

static int prim_match(Prim p, Value v) {
    switch (p) {
        case PRIM_INT: return v.kind == VAL_INT ? 4 : 0;
        case PRIM_FLOAT: return v.kind == VAL_FLOAT ? 4 : v.kind == VAL_INT ? 2 : 0;
        case PRIM_STRING: return IS_KIND(v, OBJ_STRING) ? 4 : 0;
        case PRIM_BOOL: return v.kind == VAL_BOOL ? 4 : 0;
        case PRIM_VOID: return v.kind == VAL_VOID ? 4 : 0;
        case PRIM_RANGE: return IS_KIND(v, OBJ_RANGE) ? 4 : 0;
        case PRIM_FUNCTION: return is_callable(v) ? 3 : 0;
        case PRIM_OBJECT: return IS_KIND(v, OBJ_OBJECT) ? 2 : 0;
        case PRIM_TYPE: return IS_KIND(v, OBJ_TYPEDEF) || IS_KIND(v, OBJ_ENUM) || IS_KIND(v, OBJ_INTERFACE) ? 3 : 0;
        case PRIM_ANY: return 1;
    }
    return 0;
}

static void make_bindings(VM* vm, Bindings* nb, ObjString** names, Type** defaults, int n, ObjEnv* owner_env,
                          Type* app, ObjEnv* app_env, Bindings* app_b) {
    if (n > PP_MAX_BINDINGS) rt_error(vm, app->line, "too many generic parameters (max %d)", PP_MAX_BINDINGS);
    if (app->nargs > n)
        rt_error(vm, app->line, "type '%s' takes %d type argument(s) but %d were given", app->name->chars, n, app->nargs);
    nb->count = n;
    for (int i = 0; i < n; i++) {
        Binding* bd = &nb->items[i];
        memset(bd, 0, sizeof *bd);
        bd->name = names[i];
        if (i < app->nargs) {
            bd->type = app->args[i];
            bd->env = app_env;
            bd->parent = app_b;
        } else if (defaults && defaults[i]) {
            bd->type = defaults[i];
            bd->env = owner_env;
        }
    }
}

static void qualified_name(Type* t, StrBuf* sb) {
    for (int i = 0; i < t->nquals; i++) sb_printf(sb, "%s:", t->quals[i]->chars);
    sb_puts(sb, t->name->chars);
}

/* finds the value (typedef / enum / interface) a named type refers to; a:b:Name goes through module exports */
bool resolve_type_value(VM* vm, Type* t, ObjEnv* env, Value* out) {
    if (t->direct) {
        *out = obj_val(t->direct);
        return true;
    }
    if (t->nquals) {
        Entry* m = env_find(env, t->quals[0], NULL);
        if (!m) {
            StrBuf sb;
            sb_init(&sb);
            qualified_name(t, &sb);
            rt_error(vm, t->line, "unknown module '%s' in type '%s'", t->quals[0]->chars, sb.data);
        }
        Value cur = m->value;
        for (int i = 0; i < t->nquals; i++) {
            if (!IS_KIND(cur, OBJ_MODULE)) rt_error(vm, t->line, "'%s' is not a module", t->quals[i]->chars);
            ObjModule* mod = (ObjModule*)cur.as.o;
            bool last = i + 1 == t->nquals;
            ObjString* next = last ? t->name : t->quals[i + 1];
            if (!module_member(mod, next, &cur)) {
                if (!last) rt_error(vm, t->line, "module '%s' does not export '%s'", mod->name->chars, next->chars);
                return false;
            }
        }
        *out = cur;
        return true;
    }
    Entry* e = env_find(env, t->name, NULL);
    if (!e) return false;
    *out = e->value;
    return true;
}

static PP_NORETURN void unknown_type(VM* vm, Type* t) {
    if (t->nquals)
        rt_error(vm, t->line, "module '%s' does not export a type '%s'", t->quals[t->nquals - 1]->chars, t->name->chars);
    rt_error(vm, t->line, "unknown type '%s'", t->name->chars);
}

static Value resolve_or_fail(VM* vm, Type* t, ObjEnv* env) {
    Value tv;
    if (!resolve_type_value(vm, t, env, &tv)) unknown_type(vm, t);
    return tv;
}

static bool is_record_typedef(Value tv) {
    return IS_KIND(tv, OBJ_TYPEDEF) && ((ObjTypeDef*)tv.as.o)->def->kind == TY_RECORD;
}

static int match_interface(VM* vm, ObjInterface* it, Type* app, Value v, ObjEnv* env, Bindings* b);

static int match_named(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep) {
    Binding* bd = t->nquals || t->direct ? NULL : find_binding(b, t->name);
    if (bd) {
        if (bd->type) return type_match(vm, bd->type, v, bd->env, bd->parent, deep);
        if (bd->bound) return same_kind(bd->sample, v) ? 2 : 0;
        bd->sample = v;
        bd->bound = true;
        return 2;
    }
    Value tv = resolve_or_fail(vm, t, env);
    if (IS_KIND(tv, OBJ_TYPEDEF)) {
        ObjTypeDef* td = (ObjTypeDef*)tv.as.o;
        if (IS_KIND(v, OBJ_OBJECT) && AS_OBJECT(v)->tag == td) return 5;
        Bindings nb;
        make_bindings(vm, &nb, td->generics, NULL, td->ngenerics, td->env, t, env, b);
        return type_match(vm, td->def, v, td->env, &nb, deep);
    }
    if (IS_KIND(tv, OBJ_ENUM)) {
        ObjEnum* en = (ObjEnum*)tv.as.o;
        if (!IS_KIND(v, OBJ_ENUM_VALUE) || AS_ENUMV(v)->en != en) return 0;
        if (en->ngenerics) {
            Bindings nb;
            make_bindings(vm, &nb, en->generics, en->defaults, en->ngenerics, en->env, t, env, b);
            ObjEnumValue* ev = AS_ENUMV(v);
            Variant* var = &en->variants[ev->variant];
            for (int i = 0; i < ev->count && i < var->npayload; i++)
                if (!type_match(vm, var->payload[i], ev->payload[i], en->env, &nb, deep)) return 0;
        }
        return 4;
    }
    if (IS_KIND(tv, OBJ_INTERFACE)) return match_interface(vm, (ObjInterface*)tv.as.o, t, v, env, b);
    rt_error(vm, t->line, "'%s' is not a type", t->name->chars);
}

int type_match(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep) {
    if (!t) return 1;
    switch (t->kind) {
        case TY_PRIM: return prim_match(t->prim, v);
        case TY_ARRAY: {
            if (!IS_KIND(v, OBJ_ARRAY)) return 0;
            ObjArray* a = AS_ARRAY(v);
            if (a->count == 0) return 3;
            /* dispatch only inspects the first element; declarations (deep) check all */
            int s = type_match(vm, t->inner, a->items[0], env, b, deep);
            if (!s) return 0;
            if (deep)
                for (int i = 1; i < a->count; i++)
                    if (!type_match(vm, t->inner, a->items[i], env, b, deep)) return 0;
            return 2 + s;
        }
        case TY_UNION: {
            int x = type_match(vm, t->a, v, env, b, deep);
            int y = type_match(vm, t->b, v, env, b, deep);
            int m = x > y ? x : y;
            return m > 1 ? m - 1 : m;
        }
        case TY_RECORD: {
            if (!IS_KIND(v, OBJ_OBJECT)) return 0;
            ObjObject* o = AS_OBJECT(v);
            for (int i = 0; i < t->nfields; i++) {
                Entry* e = table_find(&o->fields, t->fnames[i]);
                if (!e || !type_match(vm, t->ftypes[i], e->value, env, b, deep)) return 0;
            }
            return 3;
        }
        case TY_FUNC:
            if (!is_callable(v)) return 0;
            return callable_fits(vm, v.as.o, t->args, t->nargs, t->variadic, t->inner, env, b, NULL, false) ? 3 : 0;
        case TY_NAME: return match_named(vm, t, v, env, b, deep);
    }
    return 0;
}

static bool is_void_type(Type* t) {
    return t && t->kind == TY_PRIM && t->prim == PRIM_VOID;
}

/* ------------------------------------------------- type against type */

typedef struct {
    Type* t;
    ObjEnv* env;
    Bindings* b;
} TRef;

static TRef tref(Type* t, ObjEnv* env, Bindings* b) {
    TRef r;
    r.t = t;
    r.env = env;
    r.b = b;
    return r;
}

enum { RES_TYPE, RES_SAMPLE, RES_ANY };

/* follows generic parameters to what they are bound to */
static int follow(TRef* r, Value* sample) {
    for (int guard = 0; guard < 64; guard++) {
        Type* t = r->t;
        if (!t || (t->kind == TY_PRIM && t->prim == PRIM_ANY)) return RES_ANY;
        if (t->kind != TY_NAME || t->nquals || t->direct) return RES_TYPE;
        Binding* bd = find_binding(r->b, t->name);
        if (!bd) return RES_TYPE;
        if (bd->type) {
            *r = tref(bd->type, bd->env, bd->parent);
            continue;
        }
        if (bd->bound) {
            *sample = bd->sample;
            return RES_SAMPLE;
        }
        return RES_ANY;
    }
    return RES_ANY;
}

static bool type_accepts(VM* vm, TRef p, TRef a, bool strict);

/* child is parent or extends it, directly or through other interfaces */
bool interface_extends(ObjInterface* child, ObjInterface* parent) {
    if (child == parent) return true;
    for (int i = 0; i < child->nparents; i++)
        if (interface_extends(child->parents[i], parent)) return true;
    return false;
}

static bool interface_covers(VM* vm, ObjInterface* it, TRef a) {
    for (int i = 0; i < it->nimpls; i++)
        if (type_accepts(vm, tref(it->impls[i].target, it->impls[i].env, NULL), a, true)) return true;
    return false;
}

/* a callback of type a can be passed where type p is expected */
static bool fn_type_accepts(VM* vm, TRef p, TRef a) {
    if (p.t->variadic || a.t->variadic) return true;
    if (p.t->nargs != a.t->nargs) return false;
    for (int i = 0; i < p.t->nargs; i++)
        if (!type_accepts(vm, tref(a.t->args[i], a.env, a.b), tref(p.t->args[i], p.env, p.b), false)) return false;
    Type* pr = p.t->inner;
    if (!pr || (pr->kind == TY_PRIM && pr->prim == PRIM_VOID) || !a.t->inner) return true;
    return type_accepts(vm, tref(pr, p.env, p.b), tref(a.t->inner, a.env, a.b), false);
}

static Type* record_of(Type* t, Value named, ObjEnv** env) {
    if (t->kind == TY_RECORD) return t;
    if (is_record_typedef(named)) {
        *env = ((ObjTypeDef*)named.as.o)->env;
        return ((ObjTypeDef*)named.as.o)->def;
    }
    return NULL;
}

/*
 * Does a parameter of type p accept every value of type a? With `strict`, a
 * parameter that accepts anything (untyped, `any`) does not count: an interface
 * wants a function written for the type, not a catch-all.
 */
static bool type_accepts(VM* vm, TRef p, TRef a, bool strict) {
    Value ps, as;
    int pk = follow(&p, &ps);
    int ak = follow(&a, &as);
    if (pk == RES_ANY) return !strict;
    if (ak == RES_ANY) return true;
    if (ak == RES_SAMPLE) return type_match(vm, p.t, as, p.env, p.b, false) > 0;
    if (pk == RES_SAMPLE) return type_match(vm, a.t, ps, a.env, a.b, false) > 0;

    Type* pt = p.t;
    Type* at = a.t;
    if (at->kind == TY_UNION)
        return type_accepts(vm, p, tref(at->a, a.env, a.b), strict) && type_accepts(vm, p, tref(at->b, a.env, a.b), strict);
    Value av = void_val();
    if (at->kind == TY_NAME) {
        av = resolve_or_fail(vm, at, a.env);
        if (IS_KIND(av, OBJ_TYPEDEF) && !is_record_typedef(av)) {
            ObjTypeDef* td = (ObjTypeDef*)av.as.o;
            Bindings nb;
            make_bindings(vm, &nb, td->generics, NULL, td->ngenerics, td->env, at, a.env, a.b);
            return type_accepts(vm, p, tref(td->def, td->env, &nb), strict);
        }
    }
    if (pt->kind == TY_UNION)
        return type_accepts(vm, tref(pt->a, p.env, p.b), a, strict) || type_accepts(vm, tref(pt->b, p.env, p.b), a, strict);
    if (pt->kind == TY_NAME) {
        Value pv = resolve_or_fail(vm, pt, p.env);
        if (IS_KIND(pv, OBJ_TYPEDEF) && !is_record_typedef(pv)) {
            ObjTypeDef* td = (ObjTypeDef*)pv.as.o;
            Bindings nb;
            make_bindings(vm, &nb, td->generics, NULL, td->ngenerics, td->env, pt, p.env, p.b);
            return type_accepts(vm, tref(td->def, td->env, &nb), a, strict);
        }
        if (IS_KIND(pv, OBJ_TYPEDEF) || IS_KIND(pv, OBJ_ENUM)) return at->kind == TY_NAME && av.as.o == pv.as.o;
        if (IS_KIND(pv, OBJ_INTERFACE)) {
            /* a Comparator can be passed where an Equals is expected when Comparator extends Equals */
            if (at->kind == TY_NAME && IS_KIND(av, OBJ_INTERFACE) &&
                interface_extends((ObjInterface*)av.as.o, (ObjInterface*)pv.as.o))
                return true;
            return interface_covers(vm, (ObjInterface*)pv.as.o, a);
        }
        rt_error(vm, pt->line, "'%s' is not a type", pt->name->chars);
    }
    switch (pt->kind) {
        case TY_PRIM:
            if (at->kind == TY_PRIM)
                return pt->prim == at->prim || (pt->prim == PRIM_FLOAT && at->prim == PRIM_INT);
            if (pt->prim == PRIM_FUNCTION) return at->kind == TY_FUNC;
            if (pt->prim == PRIM_OBJECT) return at->kind == TY_RECORD || is_record_typedef(av);
            if (pt->prim == PRIM_TYPE) return false;
            return false;
        case TY_ARRAY:
            return at->kind == TY_ARRAY && type_accepts(vm, tref(pt->inner, p.env, p.b), tref(at->inner, a.env, a.b), false);
        case TY_RECORD: {
            ObjEnv* renv = a.env;
            Type* rec = record_of(at, av, &renv);
            if (!rec) return false;
            for (int i = 0; i < pt->nfields; i++) {
                int k = -1;
                for (int j = 0; j < rec->nfields; j++)
                    if (rec->fnames[j] == pt->fnames[i] || str_equals(rec->fnames[j], pt->fnames[i]->chars)) k = j;
                if (k < 0) return false;
                if (!type_accepts(vm, tref(pt->ftypes[i], p.env, p.b), tref(rec->ftypes[k], renv, a.b), false)) return false;
            }
            return true;
        }
        case TY_FUNC: return at->kind == TY_FUNC && fn_type_accepts(vm, p, a);
        default: return false;
    }
}

/* -------------------------------------------- callbacks and interfaces */

static Signature* obj_sig(Obj* o) {
    switch (o->kind) {
        case OBJ_FUNCTION: return ((ObjFunction*)o)->sig;
        case OBJ_NATIVE: return ((ObjNative*)o)->sig;
        case OBJ_VARIANT_CTOR: return ((ObjVariantCtor*)o)->sig;
        default: return NULL;
    }
}

static ObjEnv* obj_env(VM* vm, Obj* o) {
    switch (o->kind) {
        case OBJ_FUNCTION: return ((ObjFunction*)o)->closure;
        case OBJ_VARIANT_CTOR: return ((ObjVariantCtor*)o)->en->env;
        default: return vm->builtins;
    }
}

/*
 * Can callable c be called with arguments of types args[] and give a result
 * that fits ret? For an overload set, one fitting member is enough.
 *
 * Callback mode (impl_mode false): a missing return type is accepted and a
 * `void` callback ignores the result.
 * Interface mode: parameters typed `self` (the interface name) must be written
 * for the implementing type, and the return type must be declared and fit.
 */
bool callable_fits(VM* vm, Obj* c, Type** args, int nargs, bool variadic, Type* ret, ObjEnv* fenv, Bindings* fb,
                   ObjString* self, bool impl_mode) {
    if (c->kind == OBJ_OVERLOAD) {
        ObjOverload* set = (ObjOverload*)c;
        for (int i = 0; i < set->count; i++)
            if (callable_fits(vm, set->items[i], args, nargs, variadic, ret, fenv, fb, self, impl_mode)) return true;
        return false;
    }
    if (c->kind == OBJ_TYPEDEF) return !impl_mode && (variadic || nargs == 1);
    Signature* sig = obj_sig(c);
    if (!sig) return false;
    if (variadic) return impl_mode ? sig->variadic : true;
    if (sig->variadic ? nargs < sig->nparams : nargs != sig->nparams) return false;
    if (sig->ngenerics > PP_MAX_BINDINGS) return false;
    Bindings cb;
    cb.count = sig->ngenerics;
    for (int i = 0; i < sig->ngenerics; i++) {
        memset(&cb.items[i], 0, sizeof(Binding));
        cb.items[i].name = sig->generics[i];
    }
    ObjEnv* cenv = obj_env(vm, c);
    for (int i = 0; i < sig->nparams; i++) {
        Type* at = args[i];
        bool strict = impl_mode && self && at && at->kind == TY_NAME && !at->nquals && at->name == self;
        if (!type_accepts(vm, tref(sig->params[i].type, cenv, &cb), tref(at, fenv, fb), strict)) return false;
    }
    if (impl_mode) {
        if (!ret) return true;
        if (is_void_type(ret)) return !sig->ret || is_void_type(sig->ret);
        if (!sig->ret) return false;
        return type_accepts(vm, tref(ret, fenv, fb), tref(sig->ret, cenv, &cb), false);
    }
    if (!ret || is_void_type(ret) || !sig->ret) return true;
    return type_accepts(vm, tref(ret, fenv, fb), tref(sig->ret, cenv, &cb), false);
}

/* ------------------------------------------ lambdas adopt callback types */

/*
 * `const ToInt cb = f -> int(f)` gives the lambda the parameter types of
 * ToInt, as if it were written `(float f) -> int(f)`. The types are copied out
 * of the callback type into "concrete" types: generic parameters replaced by
 * what they are bound to in this call, names resolved to the typedef / enum /
 * interface they mean (Type.direct). That way they stay valid wherever the
 * lambda is called. Concrete types are deduplicated in vm->ctypes, so passing
 * lambdas in a loop does not keep allocating.
 */

static void free_type_shallow(Type* t) {
    free(t->args);
    free(t->fnames);
    free(t->ftypes);
    free(t);
}

/* children are interned first, so comparing child pointers is enough */
static Type* intern_type(VM* vm, Type* t) {
    for (int i = 0; i < vm->nctypes; i++) {
        Type* o = vm->ctypes[i];
        if (o->kind != t->kind || o->prim != t->prim || o->direct != t->direct || o->nargs != t->nargs ||
            o->variadic != t->variadic || o->inner != t->inner || o->a != t->a || o->b != t->b || o->nfields != t->nfields)
            continue;
        bool same = true;
        for (int k = 0; k < t->nargs && same; k++) same = o->args[k] == t->args[k];
        for (int k = 0; k < t->nfields && same; k++) same = o->fnames[k] == t->fnames[k] && o->ftypes[k] == t->ftypes[k];
        if (same) {
            free_type_shallow(t);
            return o;
        }
    }
    GROW(vm->ctypes, vm->nctypes, vm->capctypes);
    vm->ctypes[vm->nctypes++] = t;
    return t;
}

static Type* new_ctype(TypeKind k) {
    Type* t = pp_calloc(1, sizeof(Type));
    t->kind = k;
    return t;
}

static Type* prim_ctype(VM* vm, Prim p) {
    Type* t = new_ctype(TY_PRIM);
    t->prim = p;
    return intern_type(vm, t);
}

static Type* direct_ctype(VM* vm, Obj* o, ObjString* name) {
    Type* t = new_ctype(TY_NAME);
    t->direct = o;
    t->name = name;
    return intern_type(vm, t);
}

/* a generic bound to "the kind of this value" becomes that kind's type */
static Type* sample_type(VM* vm, Value v) {
    switch (v.kind) {
        case VAL_VOID: return NULL;
        case VAL_BOOL: return prim_ctype(vm, PRIM_BOOL);
        case VAL_INT: return prim_ctype(vm, PRIM_INT);
        case VAL_FLOAT: return prim_ctype(vm, PRIM_FLOAT);
        case VAL_OBJ: break;
    }
    switch (v.as.o->kind) {
        case OBJ_STRING: return prim_ctype(vm, PRIM_STRING);
        case OBJ_RANGE: return prim_ctype(vm, PRIM_RANGE);
        case OBJ_ARRAY: return intern_type(vm, new_ctype(TY_ARRAY)); /* element type unknown: any[] */
        case OBJ_OBJECT: {
            ObjTypeDef* tag = AS_OBJECT(v)->tag;
            return tag ? direct_ctype(vm, (Obj*)tag, tag->name) : prim_ctype(vm, PRIM_OBJECT);
        }
        case OBJ_ENUM_VALUE: return direct_ctype(vm, (Obj*)AS_ENUMV(v)->en, AS_ENUMV(v)->en->name);
        case OBJ_FUNCTION:
        case OBJ_NATIVE:
        case OBJ_OVERLOAD:
        case OBJ_VARIANT_CTOR: return prim_ctype(vm, PRIM_FUNCTION);
        case OBJ_TYPEDEF:
        case OBJ_ENUM:
        case OBJ_INTERFACE: return prim_ctype(vm, PRIM_TYPE);
        default: return NULL;
    }
}

/* NULL means "any" */
static Type* concretize(VM* vm, Type* t, ObjEnv* env, Bindings* b, int depth) {
    if (!t || depth > 32) return NULL;
    switch (t->kind) {
        case TY_PRIM: return t->prim == PRIM_ANY ? NULL : prim_ctype(vm, t->prim);
        case TY_NAME: {
            if (t->direct) return t; /* already concrete */
            TRef r = tref(t, env, b);
            Value s;
            int k = follow(&r, &s);
            if (k == RES_ANY) return NULL;
            if (k == RES_SAMPLE) return sample_type(vm, s);
            if (r.t != t) return concretize(vm, r.t, r.env, r.b, depth + 1);
            Value tv = resolve_or_fail(vm, t, env);
            Type* c = new_ctype(TY_NAME);
            c->name = t->name;
            c->direct = tv.as.o;
            if (t->nargs) {
                c->args = pp_malloc(sizeof(Type*) * (size_t)t->nargs);
                c->nargs = t->nargs;
                for (int i = 0; i < t->nargs; i++) c->args[i] = concretize(vm, t->args[i], env, b, depth + 1);
            }
            return intern_type(vm, c);
        }
        case TY_ARRAY: {
            Type* c = new_ctype(TY_ARRAY);
            c->inner = concretize(vm, t->inner, env, b, depth + 1);
            return intern_type(vm, c);
        }
        case TY_UNION: {
            Type* x = concretize(vm, t->a, env, b, depth + 1);
            Type* y = concretize(vm, t->b, env, b, depth + 1);
            if (!x || !y) return NULL;
            Type* c = new_ctype(TY_UNION);
            c->a = x;
            c->b = y;
            return intern_type(vm, c);
        }
        case TY_RECORD: {
            Type* c = new_ctype(TY_RECORD);
            c->nfields = t->nfields;
            c->fnames = pp_malloc(sizeof(ObjString*) * (size_t)(t->nfields ? t->nfields : 1));
            c->ftypes = pp_malloc(sizeof(Type*) * (size_t)(t->nfields ? t->nfields : 1));
            for (int i = 0; i < t->nfields; i++) {
                c->fnames[i] = t->fnames[i];
                c->ftypes[i] = concretize(vm, t->ftypes[i], env, b, depth + 1);
            }
            return intern_type(vm, c);
        }
        case TY_FUNC: {
            Type* c = new_ctype(TY_FUNC);
            c->variadic = t->variadic;
            c->nargs = t->nargs;
            c->args = pp_malloc(sizeof(Type*) * (size_t)(t->nargs ? t->nargs : 1));
            for (int i = 0; i < t->nargs; i++) c->args[i] = concretize(vm, t->args[i], env, b, depth + 1);
            c->inner = concretize(vm, t->inner, env, b, depth + 1);
            return intern_type(vm, c);
        }
    }
    return NULL;
}

/* t with generic parameters replaced by what b binds them to and names resolved: valid in any env (NULL = any) */
Type* concrete_type(VM* vm, Type* t, ObjEnv* env, Bindings* b) {
    return concretize(vm, t, env, b, 0);
}

/*
 * If v is a lambda with untyped parameters (or no return type) and t is a
 * callback type, returns a copy of the lambda whose untyped parameters have the
 * callback's parameter types and whose return type is the callback's (unless
 * that is void: a void callback ignores the result). Otherwise returns v.
 * `name` (the variable / parameter / field) names the copy in error messages.
 */
Value adopt_callback_types(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, ObjString* name) {
    if (!t || !IS_KIND(v, OBJ_FUNCTION)) return v;
    ObjFunction* fn = (ObjFunction*)v.as.o;
    Signature* sig = fn->sig;
    if (!sig->implicit_result) return v; /* only lambdas */
    bool open = !sig->ret;
    for (int i = 0; i < sig->nparams && !open; i++) open = !sig->params[i].type;
    if (!open) return v;

    /* find the callback type behind aliases such as Consumer<T> */
    TRef r = tref(t, env, b);
    Bindings nb[8];
    int nnb = 0;
    for (;;) {
        Value s;
        if (follow(&r, &s) != RES_TYPE) return v;
        if (r.t->kind == TY_FUNC) break;
        if (r.t->kind != TY_NAME || nnb >= 8) return v;
        Value tv = resolve_or_fail(vm, r.t, r.env);
        if (!IS_KIND(tv, OBJ_TYPEDEF) || is_record_typedef(tv)) return v;
        ObjTypeDef* td = (ObjTypeDef*)tv.as.o;
        make_bindings(vm, &nb[nnb], td->generics, NULL, td->ngenerics, td->env, r.t, r.env, r.b);
        r = tref(td->def, td->env, &nb[nnb]);
        nnb++;
    }
    Type* ft = r.t;
    if (ft->variadic || ft->nargs != sig->nparams) return v;

    Signature* ns = pp_malloc(sizeof(Signature));
    *ns = *sig;
    ns->params = pp_malloc(sizeof(Param) * (size_t)(sig->nparams ? sig->nparams : 1));
    bool changed = false;
    for (int i = 0; i < sig->nparams; i++) {
        ns->params[i] = sig->params[i];
        if (!sig->params[i].type) {
            ns->params[i].type = concretize(vm, ft->args[i], r.env, r.b, 0);
            if (ns->params[i].type) changed = true;
        }
    }
    if (!sig->ret && ft->inner && !is_void_type(ft->inner)) {
        ns->ret = concretize(vm, ft->inner, r.env, r.b, 0);
        if (ns->ret) changed = true;
    }
    if (!changed) {
        free(ns->params);
        free(ns);
        return v;
    }
    if (name) ns->name = name;
    ObjFunction* nf = function_new(vm, ns, fn->body, fn->closure);
    nf->owns_sig = true;
    return obj_val(nf);
}

static int match_interface(VM* vm, ObjInterface* it, Type* app, Value v, ObjEnv* env, Bindings* b) {
    if (app->nargs && app->nargs != it->ngenerics)
        rt_error(vm, app->line, "interface '%s' takes %d type argument(s) but %d were given", it->name->chars, it->ngenerics,
                 app->nargs);
    for (int i = 0; i < it->nimpls; i++) {
        Impl* im = &it->impls[i];
        Value tv;
        bool fits;
        if (im->target->kind == TY_NAME && resolve_type_value(vm, im->target, im->env, &tv) && is_record_typedef(tv))
            fits = IS_KIND(v, OBJ_OBJECT) && AS_OBJECT(v)->tag == (ObjTypeDef*)tv.as.o; /* nominal */
        else
            fits = type_match(vm, im->target, v, im->env, NULL, false) > 0;
        if (!fits) continue;
        bool args_ok = true;
        for (int k = 0; k < app->nargs && args_ok; k++) {
            TRef x = tref(im->args[k], im->env, NULL);
            TRef y = tref(app->args[k], env, b);
            args_ok = type_accepts(vm, x, y, false) && type_accepts(vm, y, x, false);
        }
        if (args_ok) return 4;
    }
    return 0;
}

/* ------------------------------------------------------------- printing */

static const char* prim_name(Prim p) {
    switch (p) {
        case PRIM_INT: return "int";
        case PRIM_FLOAT: return "float";
        case PRIM_STRING: return "string";
        case PRIM_BOOL: return "bool";
        case PRIM_VOID: return "void";
        case PRIM_RANGE: return "range";
        case PRIM_FUNCTION: return "function";
        case PRIM_OBJECT: return "object";
        case PRIM_ANY: return "any";
        case PRIM_TYPE: return "type";
    }
    return "?";
}

void type_to_string(Type* t, StrBuf* sb) {
    if (!t) {
        sb_puts(sb, "any");
        return;
    }
    switch (t->kind) {
        case TY_PRIM: sb_puts(sb, prim_name(t->prim)); break;
        case TY_NAME:
            if (str_equals(t->name, "Option") && t->nargs == 1) {
                type_to_string(t->args[0], sb);
                sb_putc(sb, '?');
                break;
            }
            qualified_name(t, sb);
            if (t->nargs) {
                sb_putc(sb, '<');
                for (int i = 0; i < t->nargs; i++) {
                    if (i) sb_puts(sb, ", ");
                    type_to_string(t->args[i], sb);
                }
                sb_putc(sb, '>');
            }
            break;
        case TY_ARRAY:
            type_to_string(t->inner, sb);
            sb_puts(sb, "[]");
            break;
        case TY_UNION:
            type_to_string(t->a, sb);
            sb_puts(sb, " || ");
            type_to_string(t->b, sb);
            break;
        case TY_RECORD:
            sb_puts(sb, "{ ");
            for (int i = 0; i < t->nfields; i++) {
                if (i) sb_puts(sb, ", ");
                type_to_string(t->ftypes[i], sb);
                sb_printf(sb, " %s", t->fnames[i]->chars);
            }
            sb_puts(sb, " }");
            break;
        case TY_FUNC:
            if (t->nargs == 1 && !t->variadic && t->args[0]->kind != TY_UNION && t->args[0]->kind != TY_FUNC) {
                type_to_string(t->args[0], sb);
            } else {
                sb_putc(sb, '(');
                for (int i = 0; i < t->nargs; i++) {
                    if (i) sb_puts(sb, ", ");
                    type_to_string(t->args[i], sb);
                }
                if (t->variadic) sb_puts(sb, t->nargs ? ", ..." : "...");
                sb_putc(sb, ')');
            }
            sb_puts(sb, " -> ");
            type_to_string(t->inner, sb);
            break;
    }
}

void sig_to_string(Signature* sig, StrBuf* sb) {
    if (sig->ngenerics) {
        sb_putc(sb, '<');
        for (int i = 0; i < sig->ngenerics; i++) {
            if (i) sb_puts(sb, ", ");
            sb_puts(sb, sig->generics[i]->chars);
        }
        sb_putc(sb, '>');
    }
    sb_printf(sb, "%s(", sig->name->chars);
    for (int i = 0; i < sig->nparams; i++) {
        if (i) sb_puts(sb, ", ");
        if (sig->params[i].type) {
            type_to_string(sig->params[i].type, sb);
            if (sig->params[i].name->len) sb_putc(sb, ' ');
        }
        sb_puts(sb, sig->params[i].name->chars);
    }
    if (sig->variadic) sb_puts(sb, sig->nparams ? ", ..." : "...");
    sb_putc(sb, ')');
    if (sig->ret) {
        sb_puts(sb, " -> ");
        type_to_string(sig->ret, sb);
    }
}
