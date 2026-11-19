// Runtime type matching.
//
// type_match() answers "does value v fit type t?" and, at the same time, how
// well it fits. The score drives overload resolution: the candidate whose
// parameters fit the arguments best (highest summed score) wins.
//
//   0   no match
//   1   any / untyped
//   2   generic parameter, int widened to float, plain `object`
//   3   structural record match, function, callback type, empty typed array
//   4   exact primitive / enum match, interface implemented by the value's type
//   5   nominal record match (object declared as that type)
//
// Callback types and interfaces also need to compare a type with a type
// ("does a function taking `float` fit `int -> void`?"). That is
// type_accepts() further down. It is permissive where nothing is known
// (untyped, `any`, unbound generics): calls are checked at runtime anyway.
#include "vm.h"



static Binding* find_binding(Bindings* b, ObjString* name) {
    if (!b) {
        return NULL;
    }

    for (int i = 0; i < b->count; ++i) {
        if (b->items[i].name == name) {
            return &b->items[i];
        }
    }

    return NULL;
}

static bool is_callable_obj(Obj* o) {
    return o->kind == OBJ_FUNCTION || o->kind == OBJ_NATIVE || o->kind == OBJ_OVERLOAD ||
           o->kind == OBJ_VARIANT_CTOR || o->kind == OBJ_TYPEDEF || o->kind == OBJ_BLOCK || o->kind == OBJ_CLOSURE;
}

bool same_kind(Value a, Value b) {
    if (a.kind != b.kind) {
        return false;
    }

    if (a.kind != VAL_OBJ) {
        return true;
    }

    Obj* x = a.as.o;
    Obj* y = b.as.o;
    if (x->kind != y->kind) {
        return is_callable_obj(x) && is_callable_obj(y);
    }

    if (x->kind == OBJ_OBJECT) {
        return ((ObjObject*)x)->tag == ((ObjObject*)y)->tag;
    }

    if (x->kind == OBJ_ENUM_VALUE) {
        return ((ObjEnumValue*)x)->en == ((ObjEnumValue*)y)->en;
    }

    if (x->kind == OBJ_TUPLE) {
        return ((ObjTuple*)x)->count == ((ObjTuple*)y)->count;
    }

    if (x->kind == OBJ_INSTANCE) {
        return ((ObjInstance*)x)->type == ((ObjInstance*)y)->type;
    }

    return true;
}

bool type_is_option(const Type* t) {
    return t && t->kind == TY_NAME && t->name && str_equals(t->name, "Option");
}

bool record_key_names(const ObjString* key, const ObjString* field) {
    if (key == field || (key->len == field->len && memcmp(key->chars, field->chars, (size_t)key->len) == 0)) {
        return true;
    }

    if (key->len != field->len || !memchr(key->chars, '-', (size_t)key->len)) {
        return false;
    }

    for (int i = 0; i < key->len; ++i) {
        const char k = key->chars[i] == '-' ? '_' : key->chars[i];
        if (k != field->chars[i]) {
            return false;
        }
    }

    return true;
}

// the declared field (not the rest field) a key writes, or -1
static int record_field_of(const Type* record, const ObjString* key) {
    for (int i = 0; i < record->nfields; ++i) {
        if (i != record->rest - 1 && record_key_names(key, record->fnames[i])) {
            return i;
        }
    }

    return -1;
}

// the entry of object o for field name: by its name, or written with '-' (aria-label for aria_label)
static Entry* object_field(ObjObject* o, ObjString* name) {
    Entry* e = table_find(&o->fields, name);
    for (int i = 0; !e && i < o->fields.count; ++i) {
        Entry* c = &o->fields.entries[i];
        if (c->key && record_key_names(c->key, name)) {
            e = c;
        }
    }

    return e;
}

// V of a rest field's ...Map<string, V>
static Type* rest_value_type(const Type* record) {
    const Type* m = record->ftypes[record->rest - 1];

    return m->kind == TY_NAME && m->nargs == 2 ? m->args[1] : NULL;
}

void record_convert(VM* vm, ObjObject* o, const Type* record) {
    if (!record || record->kind != TY_RECORD) {
        return;
    }

    vm_push(vm, obj_val(o));
    // a-b written for the field a_b: under the field's name
    for (int i = 0; i < o->fields.count; ++i) {
        Entry* e = &o->fields.entries[i];
        if (!e->key || !memchr(e->key->chars, '-', (size_t)e->key->len)) {
            continue;
        }

        const int f = record_field_of(record, e->key);
        if (f >= 0) {
            const Value v = e->value;
            table_remove(&o->fields, e->key);
            table_set(&o->fields, record->fnames[f], v);
            i = -1; // the table changed: start over (each renamed key no longer has a '-')
        }
    }

    for (int i = 0; i < record->nfields; ++i) {
        if (i != record->rest - 1 && type_is_option(record->ftypes[i]) && !table_find(&o->fields, record->fnames[i])) {
            table_set(&o->fields, record->fnames[i], make_none(vm));
        }

        // a record given for a Map<string, V> field: a map (the checker rewrote it already; this is for --no-check)
        Entry* e = table_find(&o->fields, record->fnames[i]);
        if (e && IS_KIND(e->value, OBJ_OBJECT) && i != record->rest - 1) {
            e->value = record_as_map(vm, record->ftypes[i], e->value, vm->builtins);
        }
    }

    if (record->rest) {
        // the fields the type does not name move into the rest map
        ObjString* rname = record->fnames[record->rest - 1];
        const Entry* have = table_find(&o->fields, rname);
        ObjMap* m = have && IS_KIND(have->value, OBJ_MAP) ? AS_MAP(have->value) : NULL;
        if (!m) {
            m = map_new(vm, OBJ_MAP);
            table_set(&o->fields, rname, obj_val(m));
        }

        for (int i = 0; i < o->fields.count; ++i) {
            Entry* e = &o->fields.entries[i];
            if (!e->key || e->key == rname || record_field_of(record, e->key) >= 0) {
                continue;
            }

            const Value k = obj_val(e->key);
            map_put(m, k, hash_key(vm, k, 0), e->value);
            table_remove(&o->fields, e->key);
            i = -1;
        }
    }

    vm->sp--;
}

Value record_as_map(VM* vm, Type* t, Value v, ObjEnv* env) {
    Value tv;
    if (!t || t->kind != TY_NAME || !IS_KIND(v, OBJ_OBJECT) || AS_OBJECT(v)->tag || !resolve_type_value(vm, t, env, &tv) ||
        !IS_KIND(tv, OBJ_NATIVE_TYPE) || ((ObjNativeType*)tv.as.o)->which != NT_MAP) {
        return v;
    }

    const ObjObject* o = AS_OBJECT(v);
    vm_push(vm, v);
    ObjMap* m = map_new(vm, OBJ_MAP);
    vm_push(vm, obj_val(m));
    for (int i = 0; i < o->fields.count; ++i) {
        const Value k = obj_val(o->fields.entries[i].key);
        map_put(m, k, hash_key(vm, k, 0), o->fields.entries[i].value);
    }

    vm->sp -= 2;

    return obj_val(m);
}

// ---- decode(m, T)

static bool decode_record(VM* vm, ObjMap* m, ObjTypeDef* td, Value* out, StrBuf* err);

// one value of the map for a field of type t; false (and err) when it does not fit
static bool decode_value(VM* vm, Type* t, Value v, ObjEnv* env, Value* out, StrBuf* err) {
    if (type_is_option(t) && t->nargs == 1) {
        if (IS_KIND(v, OBJ_ENUM_VALUE) && AS_ENUMV(v)->en == vm->option_enum) {
            if (!type_match(vm, t, v, env, NULL, true)) {
                StrBuf ts;
                sb_init(&ts);
                type_to_string(t, &ts);
                sb_printf(err, "expected %s but got %s", ts.data, value_type_name(v));
                sb_free(&ts);

                return false;
            }

            *out = v;

            return true;
        }

        Value inner;
        if (!decode_value(vm, t->args[0], v, env, &inner, err)) {
            return false;
        }

        *out = make_some(vm, inner); // a plain value for a T? field is some(value)

        return true;
    }

    if (t->kind == TY_PRIM && t->prim == PRIM_FLOAT && v.kind == VAL_INT) {
        *out = float_val((double)v.as.i);

        return true;
    }

    Value tv;
    if (t->kind == TY_NAME && IS_KIND(v, OBJ_MAP) && resolve_type_value(vm, t, env, &tv) && IS_KIND(tv, OBJ_TYPEDEF) &&
        ((ObjTypeDef*)tv.as.o)->def->kind == TY_RECORD) {
        return decode_record(vm, AS_MAP(v), (ObjTypeDef*)tv.as.o, out, err); // a nested record
    }

    if (!type_match(vm, t, v, env, NULL, true)) {
        StrBuf ts;
        sb_init(&ts);
        type_to_string(t, &ts);
        sb_printf(err, "expected %s but got %s", ts.data, value_type_name(v));
        sb_free(&ts);

        return false;
    }

    *out = v;

    return true;
}

static bool decode_record(VM* vm, ObjMap* m, ObjTypeDef* td, Value* out, StrBuf* err) {
    const Type* rec = td->def;
    ObjObject* o = object_new(vm);
    o->tag = td;
    vm_push(vm, obj_val(o));
    bool* used = pp_calloc((size_t)(m->count + 1), sizeof(bool));
    bool ok = true;
    for (int i = 0; ok && i < rec->nfields; ++i) {
        if (i == rec->rest - 1) {
            continue;
        }

        int k = -1;
        for (int j = 0; j < m->count && k < 0; ++j) {
            if (m->entries[j].live && IS_KIND(m->entries[j].key, OBJ_STRING) && record_key_names(AS_STR(m->entries[j].key), rec->fnames[i])) {
                k = j;
            }
        }

        if (k < 0) {
            if (type_is_option(rec->ftypes[i])) {
                table_set(&o->fields, rec->fnames[i], make_none(vm));
                continue;
            }

            sb_printf(err, "%s: field '%s' is missing", td->name->chars, rec->fnames[i]->chars);
            ok = false;
            break;
        }

        used[k] = true;
        Value v;
        StrBuf sub;
        sb_init(&sub);
        if (!decode_value(vm, rec->ftypes[i], m->entries[k].value, td->env, &v, &sub)) {
            sb_printf(err, "%s: field '%s': %s", td->name->chars, rec->fnames[i]->chars, sub.data);
            ok = false;
        } else {
            table_set(&o->fields, rec->fnames[i], v);
        }

        sb_free(&sub);
    }

    if (ok && rec->rest) {
        ObjMap* restm = map_new(vm, OBJ_MAP);
        table_set(&o->fields, rec->fnames[rec->rest - 1], obj_val(restm));
        Type* vt = rest_value_type(rec);
        for (int j = 0; ok && j < m->count; ++j) {
            if (!m->entries[j].live || used[j]) {
                continue;
            }

            Value v;
            StrBuf sub;
            sb_init(&sub);
            if (!IS_KIND(m->entries[j].key, OBJ_STRING)) {
                sb_printf(err, "%s: a field name must be a string, got %s", td->name->chars, value_type_name(m->entries[j].key));
                ok = false;
            } else if (vt && !decode_value(vm, vt, m->entries[j].value, td->env, &v, &sub)) {
                sb_printf(err, "%s: field '%s': %s", td->name->chars, AS_STR(m->entries[j].key)->chars, sub.data);
                ok = false;
            } else {
                map_put(restm, m->entries[j].key, m->entries[j].hash, vt ? v : m->entries[j].value);
            }

            sb_free(&sub);
        }
    } else {
        for (int j = 0; ok && j < m->count; ++j) {
            if (m->entries[j].live && !used[j]) {
                StrBuf ks;
                sb_init(&ks);
                value_format(&ks, m->entries[j].key, false, 0);
                sb_printf(err, "%s has no field %s", td->name->chars, ks.data);
                sb_free(&ks);
                ok = false;
            }
        }
    }

    free(used);
    vm->sp--;
    *out = obj_val(o);

    return ok;
}

Value record_decode(VM* vm, Value map, Value type, int line) {
    if (!IS_KIND(type, OBJ_TYPEDEF) || ((ObjTypeDef*)type.as.o)->def->kind != TY_RECORD) {
        rt_error(vm, line, "decode(m, T): T must be a record type, got %s", value_type_name(type));
    }

    if (!IS_KIND(map, OBJ_MAP)) {
        rt_error(vm, line, "decode(m, T): m must be a Map<string, unknown>, got %s", value_type_name(map));
    }

    StrBuf err;
    sb_init(&err);
    Value r;
    const bool ok = decode_record(vm, AS_MAP(map), (ObjTypeDef*)type.as.o, &r, &err);
    if (ok) {
        sb_free(&err);
        vm_push(vm, r);
        const Value s = make_success(vm, r);
        vm->sp--;

        return s;
    }

    const Value msg = obj_val(str_new(vm, err.data, (int)err.len));
    sb_free(&err);
    vm_push(vm, msg);
    const Value f = make_fail(vm, msg);
    vm->sp--;

    return f;
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
        case PRIM_TYPE:
            return IS_KIND(v, OBJ_TYPEDEF) || IS_KIND(v, OBJ_ENUM) || IS_KIND(v, OBJ_INTERFACE) || IS_KIND(v, OBJ_NATIVE_TYPE) ? 3 : 0;
        case PRIM_ANY: return 1;
    }

    return 0;
}

static void make_bindings(VM* vm, Bindings* nb, ObjString** names, Type** defaults, int n, ObjEnv* owner_env,
                          Type* app, ObjEnv* app_env, Bindings* app_b) {
    if (n > PP_MAX_BINDINGS) {
        rt_error(vm, app->line, "too many generic parameters (max %d)", PP_MAX_BINDINGS);
    }

    if (app->nargs > n) {
        rt_error(vm, app->line, "type '%s' takes %d type argument(s) but %d were given", app->name->chars, n, app->nargs);
    }

    nb->count = n;
    for (int i = 0; i < n; ++i) {
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
    for (int i = 0; i < t->nquals; ++i) {
        sb_printf(sb, "%s:", t->quals[i]->chars);
    }

    sb_puts(sb, t->name->chars);
}

// finds the value (typedef / enum / interface) a named type refers to; a:b:Name goes through module exports
bool resolve_type_value(VM* vm, Type* t, ObjEnv* env, Value* out) {
    if (t->direct) {
        *out = obj_val(t->direct);

        return true;
    }

    if (t->nquals) {
        const Entry* m = env_find(env, t->quals[0], NULL);
        if (!m) {
            StrBuf sb;
            sb_init(&sb);
            qualified_name(t, &sb);
            rt_error(vm, t->line, "unknown module '%s' in type '%s'", t->quals[0]->chars, sb.data);
        }

        Value cur = m->value;
        for (int i = 0; i < t->nquals; ++i) {
            if (!IS_KIND(cur, OBJ_MODULE)) {
                rt_error(vm, t->line, "'%s' is not a module", t->quals[i]->chars);
            }

            ObjModule* mod = (ObjModule*)cur.as.o;
            const bool last = i + 1 == t->nquals;
            ObjString* next = last ? t->name : t->quals[i + 1];
            if (!module_member(mod, next, &cur)) {
                if (!last) {
                    rt_error(vm, t->line, "module '%s' does not export '%s'", mod->name->chars, next->chars);
                }

                return false;
            }
        }

        *out = cur;

        return true;
    }

    const Entry* e = env_find(env, t->name, NULL);
    if (!e) {
        return false;
    }

    *out = e->value;

    return true;
}

static PP_NORETURN void unknown_type(VM* vm, Type* t) {
    if (t->nquals) {
        rt_error(vm, t->line, "module '%s' does not export a type '%s'", t->quals[t->nquals - 1]->chars, t->name->chars);
    }

    rt_error(vm, t->line, "unknown type '%s'", t->name->chars);
}

static Value resolve_or_fail(VM* vm, Type* t, ObjEnv* env) {
    Value tv;
    if (!resolve_type_value(vm, t, env, &tv)) {
        unknown_type(vm, t);
    }

    return tv;
}

static bool is_record_typedef(Value tv) {
    return IS_KIND(tv, OBJ_TYPEDEF) && ((ObjTypeDef*)tv.as.o)->def->kind == TY_RECORD;
}

static int match_interface(VM* vm, ObjInterface* it, Type* app, Value v, ObjEnv* env, Bindings* b);

// ---------------------------------------------------------------- tagged aliases

ObjTypeDef* typedef_tag(VM* vm, ObjTypeDef* td) {
    if (td->tag_state == 0) {
        td->tag_state = 1; // not a tag until shown to be one (a cycle stops here)
        const Type* d = td->def;
        ObjTypeDef* parent = NULL;
        bool tag = false;
        switch (d ? d->kind : TY_RECORD) {
            case TY_PRIM:
                tag = d->prim == PRIM_INT || d->prim == PRIM_FLOAT || d->prim == PRIM_STRING || d->prim == PRIM_BOOL ||
                      d->prim == PRIM_RANGE;
                break;
            case TY_ARRAY:
            case TY_TUPLE: tag = true; break;
            case TY_NAME: {
                Value tv;
                bool generic = false;
                for (int i = 0; !d->nquals && i < td->ngenerics; ++i) {
                    generic = generic || td->generics[i] == d->name;
                }

                if (!generic && resolve_type_value(vm, (Type*)d, td->env, &tv)) {
                    if (IS_KIND(tv, OBJ_TYPEDEF)) {
                        parent = typedef_tag(vm, (ObjTypeDef*)tv.as.o);
                        tag = parent != NULL;
                    } else {
                        tag = IS_KIND(tv, OBJ_ENUM) || IS_KIND(tv, OBJ_NATIVE_TYPE);
                    }
                }

                break;
            }
            default: break;
        }

        if (tag) {
            GROW(vm->tag_defs, vm->ntag_defs, vm->captag_defs);
            vm->tag_defs[vm->ntag_defs++] = td;
            td->tag_id = (uint32_t)vm->ntag_defs;
            td->tag_parent = parent;
            td->tag_depth = parent ? parent->tag_depth + 1 : 1;
            td->tag_state = 2;
        }
    }

    return td->tag_state == 2 ? td : NULL;
}

static ObjTypeDef* tag_def(VM* vm, uint32_t tag) {
    return tag && tag <= (uint32_t)vm->ntag_defs ? vm->tag_defs[tag - 1] : NULL;
}

// g is w or below it in its chain (NULL w: any)
static bool tag_within(const ObjTypeDef* g, const ObjTypeDef* w) {
    if (!w) {
        return true;
    }

    for (; g; g = g->tag_parent) {
        if (g == w) {
            return true;
        }
    }

    return false;
}

ObjTypeDef* type_tag(VM* vm, Type* t, ObjEnv* env, Bindings* b) {
    if (!t || t->kind != TY_NAME || (!t->nquals && !t->direct && find_binding(b, t->name))) {
        return NULL;
    }

    Value tv;
    if (!resolve_type_value(vm, t, env, &tv) || !IS_KIND(tv, OBJ_TYPEDEF)) {
        return NULL;
    }

    return typedef_tag(vm, (ObjTypeDef*)tv.as.o);
}

// a declared type whose values carry no tag: a value stored there loses its tag
static bool is_untagged_concrete(VM* vm, Type* t, ObjEnv* env, Bindings* b) {
    switch (t->kind) {
        case TY_PRIM: return t->prim != PRIM_ANY && t->prim != PRIM_OBJECT && t->prim != PRIM_FUNCTION && t->prim != PRIM_TYPE;
        case TY_ARRAY:
        case TY_TUPLE:
        case TY_RECORD:
        case TY_FUNC: return true;
        case TY_UNION: return false;
        case TY_NAME: {
            if (!t->nquals && !t->direct && find_binding(b, t->name)) {
                return false;
            }

            Value tv;
            if (!resolve_type_value(vm, t, env, &tv)) {
                return false;
            }

            if (IS_KIND(tv, OBJ_TYPEDEF)) {
                ObjTypeDef* td = (ObjTypeDef*)tv.as.o;

                return td->def && (td->def->kind == TY_RECORD || is_untagged_concrete(vm, td->def, td->env, NULL));
            }

            return IS_KIND(tv, OBJ_ENUM) || IS_KIND(tv, OBJ_NATIVE_TYPE);
        }
    }

    return false;
}

Value retag(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b) {
    if (!t || (vm->ntag_defs == 0 && t->kind != TY_NAME)) {
        return v;
    }

    const ObjTypeDef* tg = type_tag(vm, t, env, b);
    if (tg) {
        v.tag = tg->tag_id;
    } else if (v.tag && is_untagged_concrete(vm, t, env, b)) {
        v.tag = 0;
    }

    if (t->kind == TY_ARRAY && IS_KIND(v, OBJ_ARRAY)) {
        // A[]: the elements are A's, as the checker sees them (like the records of a Person[] get their tag)
        const ObjTypeDef* et = type_tag(vm, t->inner, env, b);
        ObjArray* a = AS_ARRAY(v);
        for (int i = 0; et && i < a->count; ++i) {
            a->items[i].tag = et->tag_id;
        }
    }

    if (t->kind == TY_NAME && t->nargs && IS_KIND(v, OBJ_ENUM_VALUE)) {
        // Error<A> / A?: a payload of a generic parameter's type is an A (success(handle) returned as
        // Error<glfw:Window> is a Window), like the elements of an A[]
        ObjEnumValue* ev = AS_ENUMV(v);
        const ObjEnum* en = ev->en;
        Value tv;
        const bool same = resolve_type_value(vm, t, env, &tv) && IS_KIND(tv, OBJ_ENUM) && tv.as.o == (Obj*)en;
        const Variant* var = same && ev->variant < en->nvariants ? &en->variants[ev->variant] : NULL;
        for (int i = 0; var && i < ev->count && i < var->npayload; ++i) {
            const Type* pt = var->payload[i];
            if (!pt || pt->kind != TY_NAME || pt->nquals || pt->nargs) {
                continue;
            }

            for (int k = 0; k < en->ngenerics && k < t->nargs; ++k) {
                if (en->generics[k] == pt->name || str_equals(en->generics[k], pt->name->chars)) {
                    ev->payload[i] = retag(vm, t->args[k], ev->payload[i], env, b);
                    break;
                }
            }
        }
    }

    return v;
}

int tag_penalty(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b) {
    const ObjTypeDef* w = vm->ntag_defs ? type_tag(vm, t, env, b) : NULL;
    const ObjTypeDef* g = tag_def(vm, v.tag);
    if (w == g) {
        return 0;
    }

    const int wd = w ? w->tag_depth : 0;
    const int gd = g ? g->tag_depth : 0;
    if (tag_within(g, w)) {
        return 2 * (gd - wd);
    }

    if (tag_within(w, g)) {
        return 2 * (wd - gd) + 1;
    }

    return 0;
}

static int min_score(int a, int b) {
    return a < b ? a : b;
}

// Map<K, V> / Set<T> (or bare Map, any keys and values). Like an
// array, dispatch looks at the first entry only and a declaration (deep)
// checks them all: 2 + the weakest element score, 3 when empty.
static int match_native(VM* vm, ObjNativeType* nt, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep) {
    if (nt->which == NT_MAP && IS_KIND(v, OBJ_OBJECT) && !AS_OBJECT(v)->tag) {
        // a record (not of a named record type) is a Map<string, V> of its fields (record_as_map converts it)
        const ObjObject* o = AS_OBJECT(v);
        for (int i = 0; t->nargs == 2 && i < o->fields.count; ++i) {
            if (!type_match(vm, t->args[1], o->fields.entries[i].value, env, b, deep)) {
                return 0;
            }
        }

        return 1; // the weakest fit: any overload that takes the record as it is wins
    }

    if (nt->which == NT_CLASS) {
        if (t->nargs) {
            rt_error(vm, t->line, "type '%s' takes no type arguments", nt->name->chars);
        }

        return IS_KIND(v, OBJ_INSTANCE) && AS_INSTANCE(v)->type == nt ? 5 : 0;
    }

    if (nt->which == NT_GENERATOR) {
        // the values are not made yet: only the kind can be checked
        if (t->nargs > 1) {
            rt_error(vm, t->line, "type 'Generator' takes one type argument but %d were given", t->nargs);
        }

        return IS_KIND(v, OBJ_GENERATOR) ? 4 : 0;
    }

    if (t->nargs && t->nargs != nt->ngenerics) {
        rt_error(vm, t->line, "type '%s' takes %d type argument(s) but %d were given", nt->name->chars, nt->ngenerics,
                 t->nargs);
    }

    if (!IS_KIND(v, nt->which == NT_MAP ? OBJ_MAP : OBJ_SET)) {
        return 0;
    }

    if (!t->nargs) {
        return 4;
    }

    const ObjMap* m = AS_MAP(v);
    if (m->live == 0) {
        return 3;
    }

    int best = 5;
    for (int i = 0; i < m->count; ++i) {
        const MapEntry* e = &m->entries[i];
        if (!e->live) {
            continue;
        }

        int k = type_match(vm, t->args[0], e->key, env, b, deep);
        if (!k) {
            return 0;
        }

        best = min_score(best, k);
        if (nt->which == NT_MAP) {
            int x = type_match(vm, t->args[1], e->value, env, b, deep);
            if (!x) {
                return 0;
            }

            best = min_score(best, x);
        }

        if (!deep) {
            break;
        }
    }

    return 2 + best;
}

static int match_named(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep) {
    if (!t->nquals && !t->direct && (t->name == vm->s_block || t->name == vm->s_callback)) {
        // Block<T> / Callback<T>: the body of a scope call; it takes its kind from the first parameter it is passed to
        if (t->nargs > 1) {
            rt_error(vm, t->line, "type '%s' takes at most one type argument", t->name->chars);
        }

        if (!IS_KIND(v, OBJ_BLOCK) || ((ObjBlock*)v.as.o)->is_defer) {
            return 0;
        }

        const ObjBlock* bl = (ObjBlock*)v.as.o;

        return !bl->bound || bl->callback == (t->name == vm->s_callback) ? 5 : 0;
    }

    Binding* bd = t->nquals || t->direct ? NULL : find_binding(b, t->name);
    if (bd) {
        if (bd->type) {
            return type_match(vm, bd->type, v, bd->env, bd->parent, deep);
        }

        if (bd->bound) {
            return same_kind(bd->sample, v) ? 2 : 0;
        }

        bd->sample = v;
        bd->bound = true;

        return 2;
    }

    Value tv = resolve_or_fail(vm, t, env);
    if (IS_KIND(tv, OBJ_TYPEDEF)) {
        ObjTypeDef* td = (ObjTypeDef*)tv.as.o;
        if (IS_KIND(v, OBJ_OBJECT) && AS_OBJECT(v)->tag == td) {
            return 5;
        }

        if (v.tag && td->def && td->def->kind != TY_RECORD) {
            // a value of another tag chain is not this tagged alias (a Vec3 is not a Mat4)
            const ObjTypeDef* tg = typedef_tag(vm, td);
            const ObjTypeDef* g = tag_def(vm, v.tag);
            if (tg && g && !tag_within(g, tg) && !tag_within(tg, g)) {
                return 0;
            }
        }

        Bindings nb;
        make_bindings(vm, &nb, td->generics, NULL, td->ngenerics, td->env, t, env, b);
        const bool alias = td->def && td->def->kind != TY_RECORD;
        if (alias && ++vm->alias_depth > 256) {
            vm->alias_depth = 0;
            rt_error(vm, t->line, "cycle detected in type '%s'", td->name->chars);
        }

        const int s = type_match(vm, td->def, v, td->env, &nb, deep);
        if (alias) {
            vm->alias_depth--;
        }

        // down a tag chain (an untagged int where Meters is expected): the weakest fit, as in the checker
        const ObjTypeDef* tg = alias && s ? typedef_tag(vm, td) : NULL;
        if (tg && !tag_within(tag_def(vm, v.tag), tg)) {
            return 1;
        }

        return s;
    }

    if (IS_KIND(tv, OBJ_ENUM)) {
        ObjEnum* en = (ObjEnum*)tv.as.o;
        if (!IS_KIND(v, OBJ_ENUM_VALUE) || AS_ENUMV(v)->en != en) {
            return 0;
        }

        if (en->ngenerics) {
            Bindings nb;
            make_bindings(vm, &nb, en->generics, en->defaults, en->ngenerics, en->env, t, env, b);
            ObjEnumValue* ev = AS_ENUMV(v);
            Variant* var = &en->variants[ev->variant];
            for (int i = 0; i < ev->count && i < var->npayload; ++i) {
                if (!type_match(vm, var->payload[i], ev->payload[i], en->env, &nb, deep)) {
                    return 0;
                }
            }
        }

        return 4;
    }

    if (IS_KIND(tv, OBJ_INTERFACE)) {
        return match_interface(vm, (ObjInterface*)tv.as.o, t, v, env, b);
    }

    if (IS_KIND(tv, OBJ_NATIVE_TYPE)) {
        return match_native(vm, (ObjNativeType*)tv.as.o, t, v, env, b, deep);
    }

    rt_error(vm, t->line, "'%s' is not a type", t->name->chars);
}

int type_match(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, bool deep) {
    if (!t) {
        return 1;
    }

    switch (t->kind) {
        case TY_PRIM:
            if (t->prim == PRIM_RANGE && t->inner && IS_KIND(v, OBJ_RANGE)) {
                // Range<int> / Range<float> by the kind of number; Range<T> of anything else by its start
                const ObjRange* r = AS_RANGE(v);
                const Type* in = t->inner;
                if (in->kind == TY_PRIM && (in->prim == PRIM_INT || in->prim == PRIM_FLOAT)) {
                    return range_is_numeric(r) && range_is_float(r) == (in->prim == PRIM_FLOAT) ? 4 : 0;
                }

                // Range<T> binds T from the start; Range<number>, Range<string>, Range<Day> check it
                return type_match(vm, t->inner, r->start, env, b, deep) ? 4 : 0;
            }

            return prim_match(t->prim, v);
        case TY_ARRAY: {
            if (!IS_KIND(v, OBJ_ARRAY)) {
                return 0;
            }

            const ObjArray* a = AS_ARRAY(v);
            if (a->count == 0) {
                return 3;
            }

            // dispatch only inspects the first element; declarations (deep) check all
            const int s = type_match(vm, t->inner, a->items[0], env, b, deep);
            if (!s) {
                return 0;
            }

            if (deep) {
                for (int i = 1; i < a->count; ++i) {
                    if (!type_match(vm, t->inner, a->items[i], env, b, deep)) {
                        return 0;
                    }
                }
            }

            return 2 + s;
        }
        case TY_UNION: {
            int x = type_match(vm, t->a, v, env, b, deep);
            const int y = type_match(vm, t->b, v, env, b, deep);
            const int m = x > y ? x : y;

            return m > 1 ? m - 1 : m;
        }
        case TY_RECORD: {
            if (!IS_KIND(v, OBJ_OBJECT)) {
                return 0;
            }

            ObjObject* o = AS_OBJECT(v);
            for (int i = 0; i < t->nfields; ++i) {
                if (i == t->rest - 1) {
                    continue;
                }

                const Entry* e = object_field(o, t->fnames[i]);
                if (!e && type_is_option(t->ftypes[i])) {
                    continue; // a T? field left out: none()
                }

                if (!e || !type_match(vm, t->ftypes[i], e->value, env, b, deep)) {
                    return 0;
                }
            }

            if (t->rest) {
                // the other fields go to the rest map: each must be a V (or the map itself, once converted)
                ObjString* rname = t->fnames[t->rest - 1];
                Type* vt = rest_value_type(t);
                for (int i = 0; i < o->fields.count; ++i) {
                    const Entry* e = &o->fields.entries[i];
                    if (!e->key || record_field_of(t, e->key) >= 0) {
                        continue;
                    }

                    Type* want = e->key == rname ? t->ftypes[t->rest - 1] : vt;
                    if (want && !type_match(vm, want, e->value, env, b, deep)) {
                        return 0;
                    }
                }
            }

            return 3;
        }
        case TY_FUNC:
            if (!is_callable(v)) {
                return 0;
            }

            return callable_fits(vm, v.as.o, t->args, t->nargs, t->variadic, t->inner, env, b, NULL, false) ? 3 : 0;
        case TY_NAME: return match_named(vm, t, v, env, b, deep);
        case TY_TUPLE: {
            // exactly as many elements as type arguments, each one fitting: 2 + the weakest
            if (!IS_KIND(v, OBJ_TUPLE)) {
                return 0;
            }

            const ObjTuple* tp = AS_TUPLE(v);
            if (tp->count != t->nargs) {
                return 0;
            }

            int best = 5;
            for (int i = 0; i < tp->count; ++i) {
                int x = type_match(vm, t->args[i], tp->items[i], env, b, deep);
                if (!x) {
                    return 0;
                }

                best = min_score(best, x);
            }

            return 2 + best;
        }
    }

    return 0;
}

static bool is_void_type(Type* t) {
    return t && t->kind == TY_PRIM && t->prim == PRIM_VOID;
}

// ------------------------------------------------- type against type

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

// follows generic parameters to what they are bound to
static int follow(TRef* r, Value* sample) {
    for (int guard = 0; guard < 64; ++guard) {
        const Type* t = r->t;
        if (!t || (t->kind == TY_PRIM && t->prim == PRIM_ANY)) {
            return RES_ANY;
        }

        if (t->kind != TY_NAME || t->nquals || t->direct) {
            return RES_TYPE;
        }

        const Binding* bd = find_binding(r->b, t->name);
        if (!bd) {
            return RES_TYPE;
        }

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

// child is parent or extends it, directly or through other interfaces
bool interface_extends(ObjInterface* child, ObjInterface* parent) {
    if (child == parent) {
        return true;
    }

    for (int i = 0; i < child->nparents; ++i) {
        if (interface_extends(child->parents[i], parent)) {
            return true;
        }
    }

    return false;
}

// the generic parameters of a generic impl, unbound: they accept anything until a value binds them
static void impl_bindings(Impl* im, Bindings* b) {
    b->count = im->ngenerics < PP_MAX_BINDINGS ? im->ngenerics : PP_MAX_BINDINGS;
    for (int i = 0; i < b->count; ++i) {
        memset(&b->items[i], 0, sizeof(Binding));
        b->items[i].name = im->generics[i];
    }
}

static bool interface_covers(VM* vm, ObjInterface* it, TRef a) {
    for (int i = 0; i < it->nimpls; ++i) {
        Bindings ib;
        impl_bindings(&it->impls[i], &ib);
        if (type_accepts(vm, tref(it->impls[i].target, it->impls[i].env, &ib), a, true)) {
            return true;
        }
    }

    return false;
}

// a callback of type a can be passed where type p is expected
static bool fn_type_accepts(VM* vm, TRef p, TRef a) {
    if (p.t->variadic || a.t->variadic) {
        return true;
    }

    if (p.t->nargs != a.t->nargs) {
        return false;
    }

    for (int i = 0; i < p.t->nargs; ++i) {
        if (!type_accepts(vm, tref(a.t->args[i], a.env, a.b), tref(p.t->args[i], p.env, p.b), false)) {
            return false;
        }
    }

    Type* pr = p.t->inner;
    if (!pr || (pr->kind == TY_PRIM && pr->prim == PRIM_VOID) || !a.t->inner) {
        return true;
    }

    return type_accepts(vm, tref(pr, p.env, p.b), tref(a.t->inner, a.env, a.b), false);
}

static Type* record_of(Type* t, Value named, ObjEnv** env) {
    if (t->kind == TY_RECORD) {
        return t;
    }

    if (is_record_typedef(named)) {
        *env = ((ObjTypeDef*)named.as.o)->env;

        return ((ObjTypeDef*)named.as.o)->def;
    }

    return NULL;
}

// Does a parameter of type p accept every value of type a? With `strict`, a
// parameter that accepts anything (untyped, `any`) does not count: an interface
// wants a function written for the type, not a catch-all.
static bool type_accepts(VM* vm, TRef p, TRef a, bool strict) {
    Value ps, as;
    const int pk = follow(&p, &ps);
    const int ak = follow(&a, &as);
    if (pk == RES_ANY) {
        return !strict;
    }

    if (ak == RES_ANY) {
        return true;
    }

    if (ak == RES_SAMPLE) {
        return type_match(vm, p.t, as, p.env, p.b, false) > 0;
    }

    if (pk == RES_SAMPLE) {
        return type_match(vm, a.t, ps, a.env, a.b, false) > 0;
    }

    Type* pt = p.t;
    Type* at = a.t;
    if (at->kind == TY_UNION) {
        return type_accepts(vm, p, tref(at->a, a.env, a.b), strict) && type_accepts(vm, p, tref(at->b, a.env, a.b), strict);
    }

    Value av = void_val();
    if (at->kind == TY_NAME) {
        av = resolve_or_fail(vm, at, a.env);
        if (IS_KIND(av, OBJ_TYPEDEF) && !is_record_typedef(av)) {
            const ObjTypeDef* td = (ObjTypeDef*)av.as.o;
            Bindings nb;
            make_bindings(vm, &nb, td->generics, NULL, td->ngenerics, td->env, at, a.env, a.b);

            return type_accepts(vm, p, tref(td->def, td->env, &nb), strict);
        }
    }

    if (pt->kind == TY_UNION) {
        return type_accepts(vm, tref(pt->a, p.env, p.b), a, strict) || type_accepts(vm, tref(pt->b, p.env, p.b), a, strict);
    }

    if (pt->kind == TY_NAME) {
        const Value pv = resolve_or_fail(vm, pt, p.env);
        if (IS_KIND(pv, OBJ_TYPEDEF) && !is_record_typedef(pv)) {
            const ObjTypeDef* td = (ObjTypeDef*)pv.as.o;
            Bindings nb;
            make_bindings(vm, &nb, td->generics, NULL, td->ngenerics, td->env, pt, p.env, p.b);

            return type_accepts(vm, tref(td->def, td->env, &nb), a, strict);
        }

        if (IS_KIND(pv, OBJ_TYPEDEF) || IS_KIND(pv, OBJ_ENUM)) {
            return at->kind == TY_NAME && av.as.o == pv.as.o;
        }

        if (IS_KIND(pv, OBJ_NATIVE_TYPE)) {
            // Map<K, V> accepts Map<K2, V2> when the arguments do; a bare Map accepts any map
            if (at->kind != TY_NAME || av.as.o != pv.as.o) {
                return false;
            }

            if (!pt->nargs || !at->nargs) {
                return true;
            }

            for (int i = 0; i < pt->nargs && i < at->nargs; ++i) {
                if (!type_accepts(vm, tref(pt->args[i], p.env, p.b), tref(at->args[i], a.env, a.b), false)) {
                    return false;
                }
            }

            return true;
        }

        if (IS_KIND(pv, OBJ_INTERFACE)) {
            // a Comparator can be passed where an Equals is expected when Comparator extends Equals
            if (at->kind == TY_NAME && IS_KIND(av, OBJ_INTERFACE) &&
                interface_extends((ObjInterface*)av.as.o, (ObjInterface*)pv.as.o)) {
                return true;
            }

            return interface_covers(vm, (ObjInterface*)pv.as.o, a);
        }

        rt_error(vm, pt->line, "'%s' is not a type", pt->name->chars);
    }

    switch (pt->kind) {
        case TY_PRIM:
            if (at->kind == TY_PRIM) {
                if (pt->prim == PRIM_RANGE && at->prim == PRIM_RANGE && pt->inner && at->inner) {
                    // Range<T> accepts Range<U> when T accepts U (and Range<int> is not a Range<float>)
                    const Type* pi = pt->inner;
                    const Type* ai = at->inner;
                    if (pi->kind == TY_PRIM && ai->kind == TY_PRIM) {
                        return pi->prim == ai->prim;
                    }

                    return type_accepts(vm, tref(pt->inner, p.env, p.b), tref(at->inner, a.env, a.b), false);
                }

                return pt->prim == at->prim || (pt->prim == PRIM_FLOAT && at->prim == PRIM_INT);
            }

            if (pt->prim == PRIM_FUNCTION) {
                return at->kind == TY_FUNC;
            }

            if (pt->prim == PRIM_OBJECT) {
                return at->kind == TY_RECORD || is_record_typedef(av);
            }

            if (pt->prim == PRIM_TYPE) {
                return false;
            }

            return false;
        case TY_ARRAY:
            return at->kind == TY_ARRAY && type_accepts(vm, tref(pt->inner, p.env, p.b), tref(at->inner, a.env, a.b), false);
        case TY_RECORD: {
            ObjEnv* renv = a.env;
            const Type* rec = record_of(at, av, &renv);
            if (!rec) {
                return false;
            }

            for (int i = 0; i < pt->nfields; ++i) {
                int k = -1;
                for (int j = 0; j < rec->nfields; ++j) {
                    if (rec->fnames[j] == pt->fnames[i] || str_equals(rec->fnames[j], pt->fnames[i]->chars)) {
                        k = j;
                    }
                }

                if (k < 0) {
                    return false;
                }

                if (!type_accepts(vm, tref(pt->ftypes[i], p.env, p.b), tref(rec->ftypes[k], renv, a.b), false)) {
                    return false;
                }
            }

            return true;
        }
        case TY_FUNC: return at->kind == TY_FUNC && fn_type_accepts(vm, p, a);
        case TY_TUPLE: {
            if (at->kind != TY_TUPLE || at->nargs != pt->nargs) {
                return false;
            }

            for (int i = 0; i < pt->nargs; ++i) {
                if (!type_accepts(vm, tref(pt->args[i], p.env, p.b), tref(at->args[i], a.env, a.b), false)) {
                    return false;
                }
            }

            return true;
        }
        default: return false;
    }
}

// -------------------------------------------- callbacks and interfaces

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
        // a `native function` declared in a .pp module names that module's types (Map in lib/std.pp)
        case OBJ_NATIVE: return ((ObjNative*)o)->env ? ((ObjNative*)o)->env : vm->builtins;
        case OBJ_VARIANT_CTOR: return ((ObjVariantCtor*)o)->en->env;
        default: return vm->builtins;
    }
}

// Can callable c be called with arguments of types args[] and give a result
// that fits ret? For an overload set, one fitting member is enough.
//
// Callback mode (impl_mode false): a missing return type is accepted and a
// `void` callback ignores the result.
// Interface mode: parameters typed `self` (the interface name) must be written
// for the implementing type, and the return type must be declared and fit.
bool callable_fits(VM* vm, Obj* c, Type** args, int nargs, bool variadic, Type* ret, ObjEnv* fenv, Bindings* fb,
                   ObjString* self, bool impl_mode) {
    if (c->kind == OBJ_CLOSURE) {
        return true; // compiled code: the checker made sure it fits
    }

    if (c->kind == OBJ_OVERLOAD) {
        const ObjOverload* set = (ObjOverload*)c;
        for (int i = 0; i < set->count; ++i) {
            if (callable_fits(vm, set->items[i], args, nargs, variadic, ret, fenv, fb, self, impl_mode)) {
                return true;
            }
        }

        return false;
    }

    if (c->kind == OBJ_TYPEDEF) {
        return !impl_mode && (variadic || nargs == 1);
    }

    if (c->kind == OBJ_NATIVE_TYPE) {
        return false; // not callable
    }

    const Signature* sig = obj_sig(c);
    if (!sig) {
        return false;
    }

    if (variadic) {
        return impl_mode ? sig->variadic : true;
    }

    if (sig->variadic ? nargs < sig->nparams : nargs != sig->nparams) {
        return false;
    }

    if (sig->ngenerics > PP_MAX_BINDINGS) {
        return false;
    }

    Bindings cb;
    cb.count = sig->ngenerics;
    for (int i = 0; i < sig->ngenerics; ++i) {
        memset(&cb.items[i], 0, sizeof(Binding));
        cb.items[i].name = sig->generics[i];
    }

    ObjEnv* cenv = obj_env(vm, c);
    for (int i = 0; i < sig->nparams; ++i) {
        Type* at = args[i];
        const bool strict = impl_mode && self && at && at->kind == TY_NAME && !at->nquals && at->name == self;
        if (!type_accepts(vm, tref(sig->params[i].type, cenv, &cb), tref(at, fenv, fb), strict)) {
            return false;
        }
    }

    if (impl_mode) {
        if (!ret) {
            return true;
        }

        if (is_void_type(ret)) {
            return !sig->ret || is_void_type(sig->ret);
        }

        if (!sig->ret) {
            return false;
        }

        return type_accepts(vm, tref(ret, fenv, fb), tref(sig->ret, cenv, &cb), false);
    }

    if (!ret || is_void_type(ret) || !sig->ret) {
        return true;
    }

    return type_accepts(vm, tref(ret, fenv, fb), tref(sig->ret, cenv, &cb), false);
}

// ------------------------------------------ lambdas adopt callback types

// `const ToInt cb = f -> int(f)` gives the lambda the parameter types of
// ToInt, as if it were written `(float f) -> int(f)`. The types are copied out
// of the callback type into "concrete" types: generic parameters replaced by
// what they are bound to in this call, names resolved to the typedef / enum /
// interface they mean (Type.direct). That way they stay valid wherever the
// lambda is called. Concrete types are deduplicated in vm->ctypes, so passing
// lambdas in a loop does not keep allocating.

static void free_type_shallow(Type* t) {
    free(t->args);
    free(t->fnames);
    free(t->ftypes);
    free(t);
}

// children are interned first, so comparing child pointers is enough
static Type* intern_type(VM* vm, Type* t) {
    for (int i = 0; i < vm->nctypes; ++i) {
        Type* o = vm->ctypes[i];
        if (o->kind != t->kind || o->prim != t->prim || o->direct != t->direct || o->nargs != t->nargs ||
            o->variadic != t->variadic || o->inner != t->inner || o->a != t->a || o->b != t->b || o->nfields != t->nfields ||
            o->rest != t->rest) {
            continue;
        }

        bool same = true;
        for (int k = 0; k < t->nargs && same; ++k) {
            same = o->args[k] == t->args[k];
        }

        for (int k = 0; k < t->nfields && same; ++k) {
            same = o->fnames[k] == t->fnames[k] && o->ftypes[k] == t->ftypes[k];
        }

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

// a generic bound to "the kind of this value" becomes that kind's type
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
        case OBJ_ARRAY: return intern_type(vm, new_ctype(TY_ARRAY)); // element type unknown: any[]
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
        case OBJ_INTERFACE:
        case OBJ_NATIVE_TYPE: return prim_ctype(vm, PRIM_TYPE);
        case OBJ_MAP: return direct_ctype(vm, (Obj*)vm->t_map, vm->t_map->name); // element types unknown: any map
        case OBJ_SET: return direct_ctype(vm, (Obj*)vm->t_set, vm->t_set->name);
        case OBJ_INSTANCE: return direct_ctype(vm, (Obj*)AS_INSTANCE(v)->type, AS_INSTANCE(v)->type->name);
        case OBJ_TUPLE: {
            Type* c = new_ctype(TY_TUPLE); // element types unknown: tuple<any, ...>
            c->nargs = AS_TUPLE(v)->count;
            c->args = pp_calloc((size_t)(c->nargs ? c->nargs : 1), sizeof(Type*));

            return intern_type(vm, c);
        }
        default: return NULL;
    }
}

// NULL means "any"
static Type* concretize(VM* vm, Type* t, ObjEnv* env, Bindings* b, int depth) {
    if (!t || depth > 32) {
        return NULL;
    }

    switch (t->kind) {
        case TY_PRIM: return t->prim == PRIM_ANY ? NULL : prim_ctype(vm, t->prim);
        case TY_NAME: {
            if (t->direct) {
                return t; // already concrete
            }

            TRef r = tref(t, env, b);
            Value s;
            int k = follow(&r, &s);
            if (k == RES_ANY) {
                return NULL;
            }

            if (k == RES_SAMPLE) {
                return sample_type(vm, s);
            }

            if (r.t != t) {
                return concretize(vm, r.t, r.env, r.b, depth + 1);
            }

            Value tv = resolve_or_fail(vm, t, env);
            Type* c = new_ctype(TY_NAME);
            c->name = t->name;
            c->direct = tv.as.o;
            if (t->nargs) {
                c->args = pp_malloc(sizeof(Type*) * (size_t)t->nargs);
                c->nargs = t->nargs;
                for (int i = 0; i < t->nargs; ++i) {
                    c->args[i] = concretize(vm, t->args[i], env, b, depth + 1);
                }
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
            if (!x || !y) {
                return NULL;
            }

            Type* c = new_ctype(TY_UNION);
            c->a = x;
            c->b = y;

            return intern_type(vm, c);
        }
        case TY_RECORD: {
            Type* c = new_ctype(TY_RECORD);
            c->nfields = t->nfields;
            c->rest = t->rest;
            c->fnames = pp_malloc(sizeof(ObjString*) * (size_t)(t->nfields ? t->nfields : 1));
            c->ftypes = pp_malloc(sizeof(Type*) * (size_t)(t->nfields ? t->nfields : 1));
            for (int i = 0; i < t->nfields; ++i) {
                c->fnames[i] = t->fnames[i];
                c->ftypes[i] = concretize(vm, t->ftypes[i], env, b, depth + 1);
            }

            return intern_type(vm, c);
        }
        case TY_TUPLE: {
            Type* c = new_ctype(TY_TUPLE);
            c->nargs = t->nargs;
            c->args = pp_malloc(sizeof(Type*) * (size_t)(t->nargs ? t->nargs : 1));
            for (int i = 0; i < t->nargs; ++i) {
                c->args[i] = concretize(vm, t->args[i], env, b, depth + 1);
            }

            return intern_type(vm, c);
        }
        case TY_FUNC: {
            Type* c = new_ctype(TY_FUNC);
            c->variadic = t->variadic;
            c->nargs = t->nargs;
            c->args = pp_malloc(sizeof(Type*) * (size_t)(t->nargs ? t->nargs : 1));
            for (int i = 0; i < t->nargs; ++i) {
                c->args[i] = concretize(vm, t->args[i], env, b, depth + 1);
            }

            c->inner = concretize(vm, t->inner, env, b, depth + 1);

            return intern_type(vm, c);
        }
    }

    return NULL;
}

// t with generic parameters replaced by what b binds them to and names resolved: valid in any env (NULL = any)
Type* concrete_type(VM* vm, Type* t, ObjEnv* env, Bindings* b) {
    return concretize(vm, t, env, b, 0);
}

// If v is a lambda with untyped parameters (or no return type) and t is a
// callback type, returns a copy of the lambda whose untyped parameters have the
// callback's parameter types and whose return type is the callback's (unless
// that is void: a void callback ignores the result). Otherwise returns v.
// `name` (the variable / parameter / field) names the copy in error messages.
Value adopt_callback_types(VM* vm, Type* t, Value v, ObjEnv* env, Bindings* b, ObjString* name) {
    if (!t || !IS_KIND(v, OBJ_FUNCTION)) {
        return v;
    }

    ObjFunction* fn = (ObjFunction*)v.as.o;
    Signature* sig = fn->sig;
    if (!sig->implicit_result) {
        return v; // only lambdas
    }

    bool open = !sig->ret;
    for (int i = 0; i < sig->nparams && !open; ++i) {
        open = !sig->params[i].type;
    }

    if (!open) {
        return v;
    }

    // find the callback type behind aliases such as Consumer<T>
    TRef r = tref(t, env, b);
    Bindings nb[8];
    int nnb = 0;
    for (;;) {
        Value s;
        if (follow(&r, &s) != RES_TYPE) {
            return v;
        }

        if (r.t->kind == TY_FUNC) {
            break;
        }

        if (r.t->kind != TY_NAME || nnb >= 8) {
            return v;
        }

        Value tv = resolve_or_fail(vm, r.t, r.env);
        if (!IS_KIND(tv, OBJ_TYPEDEF) || is_record_typedef(tv)) {
            return v;
        }

        ObjTypeDef* td = (ObjTypeDef*)tv.as.o;
        make_bindings(vm, &nb[nnb], td->generics, NULL, td->ngenerics, td->env, r.t, r.env, r.b);
        r = tref(td->def, td->env, &nb[nnb]);
        nnb++;
    }

    Type* ft = r.t;
    if (ft->variadic || ft->nargs != sig->nparams) {
        return v;
    }

    Signature* ns = pp_malloc(sizeof(Signature));
    *ns = *sig;
    ns->params = pp_malloc(sizeof(Param) * (size_t)(sig->nparams ? sig->nparams : 1));
    bool changed = false;
    for (int i = 0; i < sig->nparams; ++i) {
        ns->params[i] = sig->params[i];
        if (!sig->params[i].type) {
            ns->params[i].type = concretize(vm, ft->args[i], r.env, r.b, 0);
            if (ns->params[i].type) {
                changed = true;
            }
        }
    }

    if (!sig->ret && ft->inner && !is_void_type(ft->inner)) {
        ns->ret = concretize(vm, ft->inner, r.env, r.b, 0);
        if (ns->ret) {
            changed = true;
        }
    }

    if (!changed) {
        free(ns->params);
        free(ns);

        return v;
    }

    if (name) {
        ns->name = name;
    }

    ObjFunction* nf = function_new(vm, ns, fn->body, fn->closure);
    nf->owns_sig = true;

    return obj_val(nf);
}

static int match_interface(VM* vm, ObjInterface* it, Type* app, Value v, ObjEnv* env, Bindings* b) {
    if (app->nargs && app->nargs != it->ngenerics) {
        rt_error(vm, app->line, "interface '%s' takes %d type argument(s) but %d were given", it->name->chars, it->ngenerics,
                 app->nargs);
    }

    for (int i = 0; i < it->nimpls; ++i) {
        Impl* im = &it->impls[i];
        // <K, V>impl Index<K, V> for Map<K, V>: matching the value binds K and V
        Bindings ib;
        impl_bindings(im, &ib);
        Value tv;
        bool fits;
        if (im->target->kind == TY_NAME && resolve_type_value(vm, im->target, im->env, &tv) && is_record_typedef(tv)) {
            fits = IS_KIND(v, OBJ_OBJECT) && AS_OBJECT(v)->tag == (ObjTypeDef*)tv.as.o; // nominal
        } else {
            fits = type_match(vm, im->target, v, im->env, &ib, false) > 0;
        }

        if (!fits) {
            continue;
        }

        bool args_ok = true;
        for (int k = 0; k < app->nargs && args_ok; ++k) {
            TRef x = tref(im->args[k], im->env, &ib);
            TRef y = tref(app->args[k], env, b);
            args_ok = type_accepts(vm, x, y, false) && type_accepts(vm, y, x, false);
        }

        if (args_ok) {
            return 4;
        }
    }

    return 0;
}

// ------------------------------------------------------------- printing

static const char* prim_name(Prim p) {
    switch (p) {
        case PRIM_INT: return "int";
        case PRIM_FLOAT: return "float";
        case PRIM_STRING: return "string";
        case PRIM_BOOL: return "bool";
        case PRIM_VOID: return "void";
        case PRIM_RANGE: return "Range";
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
        case TY_PRIM:
            sb_puts(sb, prim_name(t->prim));
            if (t->prim == PRIM_RANGE && t->inner) {
                sb_putc(sb, '<');
                type_to_string(t->inner, sb);
                sb_putc(sb, '>');
            }

            break;
        case TY_NAME:
            if (str_equals(t->name, "Option") && t->nargs == 1) {
                type_to_string(t->args[0], sb);
                sb_putc(sb, '?');
                break;
            }

            qualified_name(t, sb);
            if (t->nargs) {
                sb_putc(sb, '<');
                for (int i = 0; i < t->nargs; ++i) {
                    if (i) {
                        sb_puts(sb, ", ");
                    }

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
            for (int i = 0; i < t->nfields; ++i) {
                if (i) {
                    sb_puts(sb, ", ");
                }

                if (i == t->rest - 1) {
                    sb_puts(sb, "...");
                }

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
                for (int i = 0; i < t->nargs; ++i) {
                    if (i) {
                        sb_puts(sb, ", ");
                    }

                    type_to_string(t->args[i], sb);
                }

                if (t->variadic) {
                    sb_puts(sb, t->nargs ? ", ..." : "...");
                }

                sb_putc(sb, ')');
            }

            sb_puts(sb, " -> ");
            type_to_string(t->inner, sb);
            break;
        case TY_TUPLE:
            sb_puts(sb, "tuple<");
            for (int i = 0; i < t->nargs; ++i) {
                if (i) {
                    sb_puts(sb, ", ");
                }

                type_to_string(t->args[i], sb);
            }

            sb_putc(sb, '>');
            break;
    }
}

void sig_to_string(Signature* sig, StrBuf* sb) {
    if (sig->ngenerics) {
        sb_putc(sb, '<');
        for (int i = 0; i < sig->ngenerics; ++i) {
            if (i) {
                sb_puts(sb, ", ");
            }

            sb_puts(sb, sig->generics[i]->chars);
        }

        sb_putc(sb, '>');
    }

    sb_printf(sb, "%s(", sig->name->chars);
    for (int i = 0; i < sig->nparams; ++i) {
        if (i) {
            sb_puts(sb, ", ");
        }

        if (sig->params[i].type) {
            type_to_string(sig->params[i].type, sb);
            if (sig->params[i].name->len) {
                sb_putc(sb, ' ');
            }
        }

        sb_puts(sb, sig->params[i].name->chars);
    }

    if (sig->variadic) {
        sb_puts(sb, sig->nparams ? ", ..." : "...");
    }

    sb_putc(sb, ')');
    if (sig->ret) {
        sb_puts(sb, " -> ");
        type_to_string(sig->ret, sb);
    }
}
