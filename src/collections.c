/*
 * Tuples and the hash table behind std:Map and std:Set.
 *
 * Keys are compared with values_equal, so the hash must agree with it:
 * 1 == 1.0, so an integral float hashes like the int it equals. Only values
 * that cannot change are hashable (bool, int, float, string, enum values and
 * tuples of hashable values). Arrays and objects are rejected: changing one
 * after it was put in a map would lose the entry.
 */
#include <math.h>

#include "vm.h"

ObjTuple* tuple_new(VM* vm, int count) {
    ObjTuple* t = (ObjTuple*)alloc_obj(vm, sizeof(ObjTuple) + sizeof(Value) * (size_t)count, OBJ_TUPLE);
    t->count = count;
    for (int i = 0; i < count; i++) {
        t->items[i] = void_val();
    }

    return t;
}

/* ----------------------------------------------------------------- hashing */

static uint32_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;

    return (uint32_t)x;
}

static uint32_t combine(uint32_t h, uint32_t x) {
    return (h ^ x) * 16777619u + 0x9e3779b9u;
}

bool value_hash(Value v, uint32_t* out) {
    switch (v.kind) {
        case VAL_VOID: return false;
        case VAL_BOOL: *out = v.as.b ? 0x51ed27u : 0x2f1c3du; return true;
        case VAL_INT: *out = mix64((uint64_t)v.as.i); return true;
        case VAL_FLOAT: {
            const double d = v.as.f;
            /* 2.0 == 2, so it must hash like 2 */
            if (d == floor(d) && d >= -9.2e18 && d <= 9.2e18) {
                *out = mix64((uint64_t)(int64_t)d);
                return true;
            }

            uint64_t bits;
            memcpy(&bits, &d, sizeof bits);
            *out = mix64(bits ^ 0x7ff0000000000001ULL);
            return true;
        }
        case VAL_OBJ: break;
    }

    switch (v.as.o->kind) {
        case OBJ_STRING: *out = AS_STR(v)->hash; return true;
        case OBJ_TUPLE: {
            const ObjTuple* t = AS_TUPLE(v);
            uint32_t h = 0x7e57u + (uint32_t)t->count;
            for (int i = 0; i < t->count; i++) {
                uint32_t x;
                if (!value_hash(t->items[i], &x)) {
                    return false;
                }

                h = combine(h, x);
            }

            *out = h;
            return true;
        }
        case OBJ_ENUM_VALUE: {
            const ObjEnumValue* ev = AS_ENUMV(v);
            uint32_t h = combine(mix64((uint64_t)(uintptr_t)ev->en), (uint32_t)ev->variant);
            for (int i = 0; i < ev->count; i++) {
                uint32_t x;
                if (!value_hash(ev->payload[i], &x)) {
                    return false;
                }

                h = combine(h, x);
            }

            *out = h;
            return true;
        }
        default: return false;
    }
}

uint32_t hash_key(VM* vm, Value key, int line) {
    uint32_t h;
    if (!value_hash(key, &h)) {
        const char* why = IS_KIND(key, OBJ_ARRAY) || IS_KIND(key, OBJ_OBJECT) || IS_KIND(key, OBJ_MAP) || IS_KIND(key, OBJ_SET)
                              ? " (it can change after it is stored; use a tuple instead)"
                              : "";
        const char* name = value_type_name(key);
        const bool vowel = strchr("aeiouAEIOU", name[0]) != NULL;
        rt_error(vm, line, "%s %s cannot be a map key or set element%s", vowel ? "an" : "a", name, why);
    }

    return h;
}

/* -------------------------------------------------------------- hash table */

ObjMap* map_new(VM* vm, ObjKind kind) {
    ObjMap* m = (ObjMap*)alloc_obj(vm, sizeof(ObjMap), kind);
    return m;
}

/* drops dead entries and rebuilds the index with room for at least `need` entries */
static void map_rebuild(ObjMap* m, int need) {
    if (m->live != m->count) {
        int w = 0;
        for (int r = 0; r < m->count; r++) {
            if (m->entries[r].live) {
                m->entries[w++] = m->entries[r];
            }
        }

        m->count = w;
    }

    int cap = 8;
    while (cap * 3 < need * 4 + 4) {
        cap *= 2; /* keep the load below 3/4 */
    }

    free(m->index);
    m->index = pp_malloc(sizeof(int32_t) * (size_t)cap);
    for (int i = 0; i < cap; i++) {
        m->index[i] = -1;
    }

    m->index_cap = cap;
    for (int i = 0; i < m->count; i++) {
        uint32_t s = m->entries[i].hash & (uint32_t)(cap - 1);
        while (m->index[s] >= 0) {
            s = (s + 1) & (uint32_t)(cap - 1);
        }

        m->index[s] = i;
    }
}

MapEntry* map_find(ObjMap* m, Value key, uint32_t hash) {
    if (!m->index_cap) {
        return NULL;
    }

    const uint32_t mask = (uint32_t)(m->index_cap - 1);
    for (uint32_t s = hash & mask;; s = (s + 1) & mask) {
        const int32_t i = m->index[s];
        if (i < 0) {
            return NULL;
        }

        MapEntry* e = &m->entries[i];
        if (e->live && e->hash == hash && values_equal(e->key, key)) {
            return e;
        }
    }
}

void map_put(ObjMap* m, Value key, uint32_t hash, Value value) {
    MapEntry* e = map_find(m, key, hash);
    if (e) {
        e->value = value;
        return;
    }

    /* every used entry (dead ones too) holds an index slot */
    if ((m->count + 1) * 4 > m->index_cap * 3) {
        map_rebuild(m, m->live + 1);
    }

    if (m->count == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 8;
        m->entries = pp_realloc(m->entries, sizeof(MapEntry) * (size_t)m->cap);
    }

    int i = m->count++;
    m->entries[i].key = key;
    m->entries[i].value = value;
    m->entries[i].hash = hash;
    m->entries[i].live = true;
    m->live++;
    uint32_t mask = (uint32_t)(m->index_cap - 1);
    uint32_t s = hash & mask;
    while (m->index[s] >= 0) {
        s = (s + 1) & mask;
    }

    m->index[s] = i;
}

bool map_remove(ObjMap* m, Value key, uint32_t hash, Value* old) {
    MapEntry* e = map_find(m, key, hash);
    if (!e) {
        return false;
    }

    if (old) {
        *old = e->value;
    }

    e->live = false; /* its index slot stays, so probing goes past it */
    e->key = void_val();
    e->value = void_val();
    m->live--;

    return true;
}

void map_clear(ObjMap* m) {
    m->count = 0;
    m->live = 0;
    for (int i = 0; i < m->index_cap; i++) {
        m->index[i] = -1;
    }
}

void map_free(ObjMap* m) {
    free(m->entries);
    free(m->index);
}

/* ------------------------------------------------------------ construction */

/* each element of an array (or set, range) into the set s */
static void set_add_all(VM* vm, ObjMap* s, Value from, int line) {
    if (IS_KIND(from, OBJ_ARRAY)) {
        const ObjArray* a = AS_ARRAY(from);
        for (int i = 0; i < a->count; i++) {
            map_put(s, a->items[i], hash_key(vm, a->items[i], line), void_val());
        }
    } else if (IS_KIND(from, OBJ_SET)) {
        const ObjMap* o = AS_MAP(from);
        for (int i = 0; i < o->count; i++) {
            if (o->entries[i].live) {
                map_put(s, o->entries[i].key, o->entries[i].hash, void_val());
            }
        }
    } else if (IS_KIND(from, OBJ_RANGE)) {
        ObjRange* r = AS_RANGE(from);
        const int64_t len = range_length(r);
        for (int64_t k = 0; k < len; k++) {
            const Value x = range_at(r, k);
            map_put(s, x, hash_key(vm, x, line), void_val());
        }
    } else {
        rt_error(vm, line, "std:set(...) takes an array, a set or a range, got %s", value_type_name(from));
    }
}

static void map_add_all(VM* vm, ObjMap* m, Value from, int line) {
    if (IS_KIND(from, OBJ_ARRAY)) {
        const ObjArray* a = AS_ARRAY(from);
        for (int i = 0; i < a->count; i++) {
            const Value p = a->items[i];
            if (!IS_KIND(p, OBJ_TUPLE) || AS_TUPLE(p)->count != 2) {
                rt_error(vm, line, "std:map(...) takes an array of tuple(key, value) pairs, but element %d is a %s", i,
                         value_type_name(p));
            }

            const Value k = AS_TUPLE(p)->items[0];
            map_put(m, k, hash_key(vm, k, line), AS_TUPLE(p)->items[1]);
        }
    } else if (IS_KIND(from, OBJ_MAP)) {
        const ObjMap* o = AS_MAP(from);
        for (int i = 0; i < o->count; i++) {
            if (o->entries[i].live) {
                map_put(m, o->entries[i].key, o->entries[i].hash, o->entries[i].value);
            }
        }
    } else if (IS_KIND(from, OBJ_OBJECT)) {
        const Table* f = &AS_OBJECT(from)->fields;
        for (int i = 0; i < f->count; i++) {
            map_put(m, obj_val(f->entries[i].key), f->entries[i].key->hash, f->entries[i].value);
        }
    } else {
        rt_error(vm, line, "std:map(...) takes an array of tuple(key, value) pairs, a map or an object, got %s",
                 value_type_name(from));
    }
}

/* std:map(), std:map([tuple("a", 1)]), std:set(), std:set([1, 2, 2]) */
Value native_type_construct(VM* vm, ObjNativeType* t, int argc, Value* args, int line) {
    if (argc > 1) {
        rt_error(vm, line, "std:%s(...) takes at most one value to copy from, got %d", t->which == NT_MAP ? "map" : "set", argc);
    }

    ObjMap* m = map_new(vm, t->which == NT_MAP ? OBJ_MAP : OBJ_SET);
    if (argc == 1) {
        if (t->which == NT_MAP) {
            map_add_all(vm, m, args[0], line);
        } else {
            set_add_all(vm, m, args[0], line);
        }
    }

    return obj_val(m);
}
