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
 *   3   structural record match, function, empty typed array
 *   4   exact primitive / enum match
 *   5   nominal record match (object declared as that type)
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
        case PRIM_TYPE: return IS_KIND(v, OBJ_TYPEDEF) || IS_KIND(v, OBJ_ENUM) ? 3 : 0;
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

/* finds the value (typedef / enum) a named type refers to; qualified names go through module exports */
bool resolve_type_value(VM* vm, Type* t, ObjEnv* env, Value* out) {
    if (t->qual) {
        Entry* m = env_find(env, t->qual, NULL);
        if (!m) rt_error(vm, t->line, "unknown module '%s' in type '%s:%s'", t->qual->chars, t->qual->chars, t->name->chars);
        if (!IS_KIND(m->value, OBJ_MODULE)) rt_error(vm, t->line, "'%s' is not a module", t->qual->chars);
        return module_member((ObjModule*)m->value.as.o, t->name, out);
    }
    Entry* e = env_find(env, t->name, NULL);
    if (!e) return false;
    *out = e->value;
    return true;
}

static int match_named(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep) {
    Binding* bd = t->qual ? NULL : find_binding(b, t->name);
    if (bd) {
        if (bd->type) return type_match(vm, bd->type, v, bd->env, bd->parent, deep);
        if (bd->bound) return same_kind(bd->sample, v) ? 2 : 0;
        bd->sample = v;
        bd->bound = true;
        return 2;
    }
    Value tv;
    if (!resolve_type_value(vm, t, env, &tv)) {
        if (t->qual) rt_error(vm, t->line, "module '%s' does not export a type '%s'", t->qual->chars, t->name->chars);
        rt_error(vm, t->line, "unknown type '%s'", t->name->chars);
    }
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
        case TY_NAME: return match_named(vm, t, v, env, b, deep);
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
            if (t->qual) sb_printf(sb, "%s:", t->qual->chars);
            sb_puts(sb, t->name->chars);
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
            sb_putc(sb, ' ');
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
