#include <math.h>

#include "vm.h"

uint32_t hash_chars(const char* s, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

Obj* alloc_obj(VM* vm, size_t size, ObjKind kind) {
    Obj* o = pp_calloc(1, size);
    o->kind = kind;
    o->next = vm->objects;
    vm->objects = o;
    vm->num_objects++;
    return o;
}

/* ----------------------------------------------------------------- strings */

ObjString* str_new(VM* vm, const char* s, int len) {
    ObjString* str = (ObjString*)alloc_obj(vm, sizeof(ObjString) + (size_t)len + 1, OBJ_STRING);
    str->len = len;
    if (len) memcpy(str->chars, s, (size_t)len);
    str->chars[len] = '\0';
    str->hash = hash_chars(str->chars, len);
    return str;
}

ObjString* str_cnew(VM* vm, const char* s) {
    return str_new(vm, s, (int)strlen(s));
}

ObjString* str_intern(VM* vm, const char* s, int len) {
    uint32_t h = hash_chars(s, len);
    Entry* e = table_find_chars(&vm->interned, s, len, h);
    if (e) return e->key;
    ObjString* str = pp_calloc(1, sizeof(ObjString) + (size_t)len + 1);
    str->obj.kind = OBJ_STRING;
    str->obj.permanent = true;
    str->len = len;
    if (len) memcpy(str->chars, s, (size_t)len);
    str->chars[len] = '\0';
    str->hash = h;
    table_set(&vm->interned, str, void_val());
    return str;
}

ObjString* str_cintern(VM* vm, const char* s) {
    return str_intern(vm, s, (int)strlen(s));
}

bool str_equals(ObjString* a, const char* s) {
    size_t n = strlen(s);
    return (size_t)a->len == n && memcmp(a->chars, s, n) == 0;
}

/* ------------------------------------------------------------ constructors */

ObjArray* array_new(VM* vm, int cap) {
    ObjArray* a = (ObjArray*)alloc_obj(vm, sizeof(ObjArray), OBJ_ARRAY);
    if (cap > 0) {
        a->items = pp_malloc(sizeof(Value) * (size_t)cap);
        a->cap = cap;
    }
    return a;
}

void array_push(ObjArray* a, Value v) {
    GROW(a->items, a->count, a->cap);
    a->items[a->count++] = v;
}

ObjObject* object_new(VM* vm) {
    ObjObject* o = (ObjObject*)alloc_obj(vm, sizeof(ObjObject), OBJ_OBJECT);
    table_init(&o->fields);
    return o;
}

ObjRange* range_new(VM* vm, Value start, Value end, Value step) {
    ObjRange* r = (ObjRange*)alloc_obj(vm, sizeof(ObjRange), OBJ_RANGE);
    if (start.kind == VAL_FLOAT || end.kind == VAL_FLOAT || step.kind == VAL_FLOAT) {
        start = float_val(num_as_double(start));
        end = float_val(num_as_double(end));
        step = float_val(num_as_double(step));
    }
    r->start = start;
    r->end = end;
    r->step = step;
    r->inclusive = false;
    return r;
}

Value range_at(ObjRange* r, int64_t k) {
    if (range_is_float(r)) return float_val(r->start.as.f + (double)k * r->step.as.f);
    return int_val(r->start.as.i + k * r->step.as.i);
}

int64_t range_length(ObjRange* r) {
    if (range_is_float(r)) {
        double start = r->start.as.f, end = r->end.as.f, step = r->step.as.f;
        if (step == 0 || !(step > 0 ? end > start : end < start)) return 0;
        /* n = number of k >= 0 with start + k*step before end; the division can round
           across a whole number, so fix n up against the elements range_at produces */
        double n = ceil((end - start) / step);
#define BEFORE_END(k) (step > 0 ? start + (k) * step < end : start + (k) * step > end)
        while (n > 0 && !BEFORE_END(n - 1)) n--;
        while (BEFORE_END(n)) n++;
#undef BEFORE_END
        return (int64_t)n;
    }
    int64_t start = r->start.as.i, end = r->end.as.i, step = r->step.as.i;
    if (step > 0) return end > start ? (end - start + step - 1) / step : 0;
    if (step < 0) return end < start ? (start - end - step - 1) / -step : 0;
    return 0;
}

ObjEnv* env_new(VM* vm, ObjEnv* parent) {
    ObjEnv* e = (ObjEnv*)alloc_obj(vm, sizeof(ObjEnv), OBJ_ENV);
    e->parent = parent;
    table_init(&e->vars);
    return e;
}

ObjOverload* overload_new(VM* vm, ObjString* name) {
    ObjOverload* s = (ObjOverload*)alloc_obj(vm, sizeof(ObjOverload), OBJ_OVERLOAD);
    s->name = name;
    return s;
}

void overload_add(ObjOverload* set, Obj* fn) {
    for (int i = 0; i < set->count; i++)
        if (set->items[i] == fn) return;
    GROW(set->items, set->count, set->cap);
    set->items[set->count++] = fn;
}

ObjFunction* function_new(VM* vm, Signature* sig, Node* body, ObjEnv* closure) {
    ObjFunction* f = (ObjFunction*)alloc_obj(vm, sizeof(ObjFunction), OBJ_FUNCTION);
    f->sig = sig;
    f->body = body;
    f->closure = closure;
    return f;
}

ObjNative* native_new(VM* vm, Signature* sig, NativeFn fn) {
    ObjNative* n = (ObjNative*)alloc_obj(vm, sizeof(ObjNative), OBJ_NATIVE);
    n->sig = sig;
    n->fn = fn;
    n->env = NULL;
    return n;
}

ObjEnumValue* enumv_new(VM* vm, ObjEnum* en, int variant, int count) {
    ObjEnumValue* v = (ObjEnumValue*)alloc_obj(vm, sizeof(ObjEnumValue) + sizeof(Value) * (size_t)count, OBJ_ENUM_VALUE);
    v->en = en;
    v->variant = variant;
    v->count = count;
    for (int i = 0; i < count; i++) v->payload[i] = void_val();
    return v;
}

ObjModule* module_new(VM* vm, const char* name, const char* path, ObjEnv* parent) {
    ObjModule* m = (ObjModule*)alloc_obj(vm, sizeof(ObjModule), OBJ_MODULE);
    table_init(&m->exports);
    m->name = str_cnew(vm, name);
    m->path = str_cnew(vm, path);
    m->natives = NULL;
    m->env = env_new(vm, parent);
    return m;
}

void module_export(ObjModule* m, ObjString* name, ObjString* alias) {
    table_set(&m->exports, alias, obj_val(name));
}

/* value of an exported symbol; exports are live bindings into the module scope */
bool module_member(ObjModule* m, ObjString* name, Value* out) {
    Entry* ex = table_find(&m->exports, name);
    if (!ex) return false;
    Entry* e = table_find(&m->env->vars, AS_STR(ex->value));
    if (!e) return false;
    *out = e->value;
    return true;
}

/* ------------------------------------------------------------------ tables */

void table_init(Table* t) {
    memset(t, 0, sizeof *t);
}

void table_free(Table* t) {
    free(t->entries);
    free(t->index);
    table_init(t);
}

static void table_reindex(Table* t) {
    if (t->count < 8) {
        free(t->index);
        t->index = NULL;
        t->index_cap = 0;
        return;
    }
    int cap = 16;
    while (cap < t->count * 2) cap *= 2;
    if (cap != t->index_cap) {
        free(t->index);
        t->index = pp_malloc(sizeof(int32_t) * (size_t)cap);
        t->index_cap = cap;
    }
    for (int i = 0; i < cap; i++) t->index[i] = -1;
    uint32_t mask = (uint32_t)cap - 1;
    for (int i = 0; i < t->count; i++) {
        uint32_t h = t->entries[i].key->hash & mask;
        while (t->index[h] != -1) h = (h + 1) & mask;
        t->index[h] = i;
    }
}

static int table_find_index(Table* t, const char* s, int len, uint32_t hash, ObjString* ptr) {
    if (t->index) {
        uint32_t mask = (uint32_t)t->index_cap - 1;
        uint32_t h = hash & mask;
        for (;;) {
            int32_t idx = t->index[h];
            if (idx < 0) return -1;
            ObjString* k = t->entries[idx].key;
            if (k == ptr || (k->hash == hash && k->len == len && memcmp(k->chars, s, (size_t)len) == 0))
                return idx;
            h = (h + 1) & mask;
        }
    }
    for (int i = 0; i < t->count; i++) {
        ObjString* k = t->entries[i].key;
        if (k == ptr || (k->hash == hash && k->len == len && memcmp(k->chars, s, (size_t)len) == 0)) return i;
    }
    return -1;
}

Entry* table_find(Table* t, ObjString* key) {
    int i = table_find_index(t, key->chars, key->len, key->hash, key);
    return i < 0 ? NULL : &t->entries[i];
}

Entry* table_find_chars(Table* t, const char* s, int len, uint32_t hash) {
    int i = table_find_index(t, s, len, hash, NULL);
    return i < 0 ? NULL : &t->entries[i];
}

Entry* table_set(Table* t, ObjString* key, Value v) {
    Entry* e = table_find(t, key);
    if (e) {
        e->value = v;
        return e;
    }
    GROW(t->entries, t->count, t->cap);
    Entry* ne = &t->entries[t->count++];
    ne->key = key;
    ne->value = v;
    ne->type = NULL;
    ne->flags = 0;
    if (t->index && t->count * 2 <= t->index_cap) {
        uint32_t mask = (uint32_t)t->index_cap - 1;
        uint32_t h = key->hash & mask;
        while (t->index[h] != -1) h = (h + 1) & mask;
        t->index[h] = t->count - 1;
    } else if (t->count >= 8) {
        table_reindex(t);
    }
    return &t->entries[t->count - 1];
}

bool table_remove(Table* t, ObjString* key) {
    int i = table_find_index(t, key->chars, key->len, key->hash, key);
    if (i < 0) return false;
    memmove(&t->entries[i], &t->entries[i + 1], sizeof(Entry) * (size_t)(t->count - i - 1));
    t->count--;
    table_reindex(t);
    return true;
}

/* ---------------------------------------------------------------- equality */

bool values_equal(Value a, Value b) {
    if (a.kind == VAL_INT && b.kind == VAL_FLOAT) return (double)a.as.i == b.as.f;
    if (a.kind == VAL_FLOAT && b.kind == VAL_INT) return a.as.f == (double)b.as.i;
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case VAL_VOID: return true;
        case VAL_BOOL: return a.as.b == b.as.b;
        case VAL_INT: return a.as.i == b.as.i;
        case VAL_FLOAT: return a.as.f == b.as.f;
        case VAL_OBJ: break;
    }
    Obj* x = a.as.o;
    Obj* y = b.as.o;
    if (x == y) return true;
    if (x->kind != y->kind) return false;
    switch (x->kind) {
        case OBJ_STRING: {
            ObjString* s = (ObjString*)x;
            ObjString* t = (ObjString*)y;
            return s->len == t->len && s->hash == t->hash && memcmp(s->chars, t->chars, (size_t)s->len) == 0;
        }
        case OBJ_ARRAY: {
            ObjArray* p = (ObjArray*)x;
            ObjArray* q = (ObjArray*)y;
            if (p->count != q->count) return false;
            for (int i = 0; i < p->count; i++)
                if (!values_equal(p->items[i], q->items[i])) return false;
            return true;
        }
        case OBJ_OBJECT: {
            ObjObject* p = (ObjObject*)x;
            ObjObject* q = (ObjObject*)y;
            if (p->fields.count != q->fields.count) return false;
            for (int i = 0; i < p->fields.count; i++) {
                Entry* e = table_find(&q->fields, p->fields.entries[i].key);
                if (!e || !values_equal(p->fields.entries[i].value, e->value)) return false;
            }
            return true;
        }
        case OBJ_RANGE: {
            ObjRange* p = (ObjRange*)x;
            ObjRange* q = (ObjRange*)y;
            return values_equal(p->start, q->start) && values_equal(p->end, q->end) && values_equal(p->step, q->step);
        }
        case OBJ_ENUM_VALUE: {
            ObjEnumValue* p = (ObjEnumValue*)x;
            ObjEnumValue* q = (ObjEnumValue*)y;
            if (p->en != q->en || p->variant != q->variant || p->count != q->count) return false;
            for (int i = 0; i < p->count; i++)
                if (!values_equal(p->payload[i], q->payload[i])) return false;
            return true;
        }
        case OBJ_TUPLE: {
            ObjTuple* p = (ObjTuple*)x;
            ObjTuple* q = (ObjTuple*)y;
            if (p->count != q->count) return false;
            for (int i = 0; i < p->count; i++)
                if (!values_equal(p->items[i], q->items[i])) return false;
            return true;
        }
        case OBJ_MAP:
        case OBJ_SET: {
            /* the same keys (and values), in any order */
            ObjMap* p = (ObjMap*)x;
            ObjMap* q = (ObjMap*)y;
            if (p->live != q->live) return false;
            for (int i = 0; i < p->count; i++) {
                MapEntry* e = &p->entries[i];
                if (!e->live) continue;
                MapEntry* f = map_find(q, e->key, e->hash);
                if (!f || (x->kind == OBJ_MAP && !values_equal(e->value, f->value))) return false;
            }
            return true;
        }
        default: return false;
    }
}

bool is_callable(Value v) {
    if (!IS_OBJ(v)) return false;
    switch (v.as.o->kind) {
        case OBJ_FUNCTION:
        case OBJ_NATIVE:
        case OBJ_OVERLOAD:
        case OBJ_VARIANT_CTOR:
        case OBJ_TYPEDEF:
        case OBJ_NATIVE_TYPE: return true;
        default: return false;
    }
}

const char* value_type_name(Value v) {
    switch (v.kind) {
        case VAL_VOID: return "void";
        case VAL_BOOL: return "bool";
        case VAL_INT: return "int";
        case VAL_FLOAT: return "float";
        case VAL_OBJ: break;
    }
    switch (v.as.o->kind) {
        case OBJ_STRING: return "string";
        case OBJ_ARRAY: return "array";
        case OBJ_OBJECT: {
            ObjObject* o = AS_OBJECT(v);
            return o->tag ? o->tag->name->chars : "object";
        }
        case OBJ_RANGE: return "range";
        case OBJ_FUNCTION:
        case OBJ_NATIVE:
        case OBJ_OVERLOAD:
        case OBJ_VARIANT_CTOR: return "function";
        case OBJ_ENUM:
        case OBJ_TYPEDEF: return "type";
        case OBJ_INTERFACE: return "interface";
        case OBJ_ENUM_VALUE: return AS_ENUMV(v)->en->name->chars;
        case OBJ_MODULE: return "module";
        case OBJ_ENV: return "scope";
        case OBJ_TUPLE: return "tuple";
        case OBJ_MAP: return "map";
        case OBJ_SET: return "set";
        case OBJ_NATIVE_TYPE: return "type";
    }
    return "?";
}

/* -------------------------------------------------------------- formatting */

void format_float(StrBuf* sb, double d) {
    if (isnan(d)) {
        sb_puts(sb, "nan");
        return;
    }
    if (isinf(d)) {
        sb_puts(sb, d > 0 ? "inf" : "-inf");
        return;
    }
    char buf[64];
    for (int prec = 15; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) break;
    }
    sb_puts(sb, buf);
    if (!strpbrk(buf, ".eE")) sb_puts(sb, ".0");
}

static void format_quoted(StrBuf* sb, ObjString* s) {
    sb_putc(sb, '"');
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        switch (c) {
            case '"': sb_puts(sb, "\\\""); break;
            case '\\': sb_puts(sb, "\\\\"); break;
            case '\n': sb_puts(sb, "\\n"); break;
            case '\t': sb_puts(sb, "\\t"); break;
            case '\r': sb_puts(sb, "\\r"); break;
            default:
                if (c < 0x20) sb_printf(sb, "\\u%04x", c);
                else sb_putc(sb, (char)c);
        }
    }
    sb_putc(sb, '"');
}

static bool is_complex(Value v, int depth) {
    if (depth > 32) return true;
    if (IS_KIND(v, OBJ_OBJECT)) return AS_OBJECT(v)->fields.count > 0;
    if (IS_KIND(v, OBJ_ARRAY)) {
        ObjArray* a = AS_ARRAY(v);
        for (int i = 0; i < a->count; i++)
            if (is_complex(a->items[i], depth + 1)) return true;
    }
    return false;
}

static void put_indent(StrBuf* sb, int n) {
    for (int i = 0; i < n; i++) sb_puts(sb, "    ");
}

static int format_depth = 0;

void value_format(StrBuf* sb, Value v, bool top, int indent) {
    switch (v.kind) {
        case VAL_VOID: sb_puts(sb, "void"); return;
        case VAL_BOOL: sb_puts(sb, v.as.b ? "true" : "false"); return;
        case VAL_INT: sb_printf(sb, "%" PRId64, v.as.i); return;
        case VAL_FLOAT: format_float(sb, v.as.f); return;
        case VAL_OBJ: break;
    }
    if (format_depth > 64) {
        sb_puts(sb, "...");
        return;
    }
    format_depth++;
    Obj* o = v.as.o;
    switch (o->kind) {
        case OBJ_STRING:
            if (top) sb_append(sb, AS_STR(v)->chars, (size_t)AS_STR(v)->len);
            else format_quoted(sb, AS_STR(v));
            break;
        case OBJ_ARRAY: {
            ObjArray* a = AS_ARRAY(v);
            if (a->count == 0) {
                sb_puts(sb, "[]");
                break;
            }
            if (!is_complex(v, 0)) {
                sb_putc(sb, '[');
                for (int i = 0; i < a->count; i++) {
                    if (i) sb_puts(sb, ", ");
                    value_format(sb, a->items[i], false, indent);
                }
                sb_putc(sb, ']');
            } else {
                sb_puts(sb, "[\n");
                for (int i = 0; i < a->count; i++) {
                    put_indent(sb, indent + 1);
                    value_format(sb, a->items[i], false, indent + 1);
                    if (i + 1 < a->count) sb_putc(sb, ',');
                    sb_putc(sb, '\n');
                }
                put_indent(sb, indent);
                sb_putc(sb, ']');
            }
            break;
        }
        case OBJ_OBJECT: {
            ObjObject* ob = AS_OBJECT(v);
            if (ob->fields.count == 0) {
                sb_puts(sb, "{}");
                break;
            }
            sb_puts(sb, "{\n");
            for (int i = 0; i < ob->fields.count; i++) {
                Entry* e = &ob->fields.entries[i];
                put_indent(sb, indent + 1);
                format_quoted(sb, e->key);
                sb_puts(sb, ": ");
                value_format(sb, e->value, false, indent + 1);
                if (i + 1 < ob->fields.count) sb_putc(sb, ',');
                sb_putc(sb, '\n');
            }
            put_indent(sb, indent);
            sb_putc(sb, '}');
            break;
        }
        case OBJ_RANGE: {
            ObjRange* r = AS_RANGE(v);
            if (range_is_float(r)) {
                sb_puts(sb, "range(");
                format_float(sb, r->start.as.f);
                sb_puts(sb, ", ");
                format_float(sb, r->end.as.f);
                sb_puts(sb, ", ");
                format_float(sb, r->step.as.f);
                sb_putc(sb, ')');
            } else if (r->step.as.i == 1 || r->step.as.i == -1)
                sb_printf(sb, "%" PRId64 "...%" PRId64, r->start.as.i, r->end.as.i);
            else
                sb_printf(sb, "range(%" PRId64 ", %" PRId64 ", %" PRId64 ")", r->start.as.i, r->end.as.i, r->step.as.i);
            break;
        }
        case OBJ_FUNCTION: sb_printf(sb, "<function %s>", ((ObjFunction*)o)->sig->name->chars); break;
        case OBJ_NATIVE: sb_printf(sb, "<native function %s>", ((ObjNative*)o)->sig->name->chars); break;
        case OBJ_OVERLOAD: {
            ObjOverload* s = (ObjOverload*)o;
            if (s->count == 1) sb_printf(sb, "<function %s>", s->name->chars);
            else sb_printf(sb, "<function %s (%d overloads)>", s->name->chars, s->count);
            break;
        }
        case OBJ_ENUM: sb_printf(sb, "<enum %s>", ((ObjEnum*)o)->name->chars); break;
        case OBJ_VARIANT_CTOR: {
            ObjVariantCtor* c = (ObjVariantCtor*)o;
            sb_printf(sb, "<variant %s.%s>", c->en->name->chars, c->en->variants[c->index].name->chars);
            break;
        }
        case OBJ_TYPEDEF: sb_printf(sb, "<type %s>", ((ObjTypeDef*)o)->name->chars); break;
        case OBJ_INTERFACE: sb_printf(sb, "<interface %s>", ((ObjInterface*)o)->name->chars); break;
        case OBJ_MODULE: sb_printf(sb, "<module %s>", ((ObjModule*)o)->name->chars); break;
        case OBJ_ENV: sb_puts(sb, "<scope>"); break;
        case OBJ_NATIVE_TYPE: sb_printf(sb, "<type std:%s>", ((ObjNativeType*)o)->name->chars); break;
        case OBJ_TUPLE: {
            ObjTuple* t = AS_TUPLE(v);
            sb_puts(sb, "tuple(");
            for (int i = 0; i < t->count; i++) {
                if (i) sb_puts(sb, ", ");
                value_format(sb, t->items[i], false, indent);
            }
            sb_putc(sb, ')');
            break;
        }
        case OBJ_MAP:
        case OBJ_SET: {
            /* map{"a": 1, "b": 2}, set{1, 2}: one line, in insertion order */
            ObjMap* m = AS_MAP(v);
            bool is_map = o->kind == OBJ_MAP;
            sb_puts(sb, is_map ? "map{" : "set{");
            bool first = true;
            for (int i = 0; i < m->count; i++) {
                MapEntry* e = &m->entries[i];
                if (!e->live) continue;
                if (!first) sb_puts(sb, ", ");
                first = false;
                value_format(sb, e->key, false, indent);
                if (is_map) {
                    sb_puts(sb, ": ");
                    value_format(sb, e->value, false, indent);
                }
            }
            sb_putc(sb, '}');
            break;
        }
        case OBJ_ENUM_VALUE: {
            ObjEnumValue* ev = AS_ENUMV(v);
            Variant* var = &ev->en->variants[ev->variant];
            if (!var->is_ctor) {
                sb_printf(sb, "%s.%s", ev->en->name->chars, var->name->chars);
            } else {
                sb_printf(sb, "%s(", var->name->chars);
                for (int i = 0; i < ev->count; i++) {
                    if (i) sb_puts(sb, ", ");
                    value_format(sb, ev->payload[i], false, indent);
                }
                sb_putc(sb, ')');
            }
            break;
        }
    }
    format_depth--;
}
