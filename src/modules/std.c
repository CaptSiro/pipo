/*
 * The C half of lib/std.pp (a mixed module): bodies of its `native` functions,
 * including the functions of std:Map and std:Set.
 */
#include <math.h>

#include "vm.h"

/*
 * has(range r, number x): is x one of the elements a for loop over r visits,
 * i.e. x == start + k*step for some 0 <= k < #r. Int ranges are exact. When
 * the range or x is a float, k is rounded to the nearest element and x must be
 * within a tiny fraction of the step from it, so 0.3 in range(0, 1, 0.1) holds
 * even though 3 * 0.1 is 0.30000000000000004.
 */
static Value std_range_has(VM* vm, int argc, Value* a) {
    ObjRange* r = AS_RANGE(a[0]);
    const Value x = a[1];
    const int64_t len = range_length(r);
    if (len == 0) {
        return bool_val(false);
    }

    if (x.kind == VAL_INT && !range_is_float(r)) {
        int64_t start = r->start.as.i, step = r->step.as.i, d = x.as.i - start;
        if (d % step != 0) {
            return bool_val(false);
        }

        const int64_t k = d / step;
        return bool_val(k >= 0 && k < len);
    }

    double xf = num_as_double(x), start = num_as_double(r->start), step = num_as_double(r->step);
    if (!isfinite(xf)) {
        return bool_val(false);
    }

    const double k = round((xf - start) / step);
    if (k < 0 || k >= (double)len) {
        return bool_val(false);
    }

    const double elem = num_as_double(range_at(r, (int64_t)k));

    return bool_val(fabs(elem - xf) <= fabs(step) * 1e-9);
}

/**
 * Checks only start and end, does not take in count step property.
 * The bounds follow the range's direction and end: 0...5 is [0, 5),
 * 5...1 is (1, 5], 5...=1 is [1, 5] and range(0, 1, 0.25) is [0, 1).
 */
static Value std_in_range(VM* vm, int argc, Value* a) {
    const Value x = a[0];
    const ObjRange* r = AS_RANGE(a[1]);
    const double xf = num_as_double(x);
    if (!isfinite(xf)) {
        return bool_val(false);
    }

    const double start = num_as_double(r->start);
    const double step = num_as_double(r->step);
    double end = num_as_double(r->end);
    if (r->inclusive) {
        /* ...= stored end one step past the last element: undo that, the end is included */
        end -= step;
        return bool_val(step > 0 ? xf >= start && xf <= end : xf <= start && xf >= end);
    }

    return bool_val(step > 0 ? xf >= start && xf < end : xf <= start && xf > end);
}

/* ------------------------------------------------- std:Map and std:Set
 *
 * The tables themselves are in src/collections.c. Overload resolution has
 * already checked the receiver (Map<K, V> / Set<T>) before these run.
 */

/* the constructors: std:map(), std:map(from), std:set(), std:set(from) (see native_type_construct) */
static Value map_new_fn(VM* vm, int argc, Value* a) {
    return native_type_construct(vm, vm->t_map, argc, a, 0);
}

static Value set_new_fn(VM* vm, int argc, Value* a) {
    return native_type_construct(vm, vm->t_set, argc, a, 0);
}

static Value map_get_fn(VM* vm, int argc, Value* a) {
    const MapEntry* e = map_find(AS_MAP(a[0]), a[1], hash_key(vm, a[1], 0));
    return e ? make_some(vm, e->value) : make_none(vm);
}


static Value map_put_fn(VM* vm, int argc, Value* a) {
    map_put(AS_MAP(a[0]), a[1], hash_key(vm, a[1], 0), a[2]);
    return void_val();
}

/* has(map, key) and has(set, item) */
static Value map_has(VM* vm, int argc, Value* a) {
    return bool_val(map_find(AS_MAP(a[0]), a[1], hash_key(vm, a[1], 0)) != NULL);
}

static Value map_remove_fn(VM* vm, int argc, Value* a) {
    Value old;
    if (!map_remove(AS_MAP(a[0]), a[1], hash_key(vm, a[1], 0), &old)) {
        return make_none(vm);
    }

    return make_some(vm, old);
}

static Value set_remove(VM* vm, int argc, Value* a) {
    return bool_val(map_remove(AS_MAP(a[0]), a[1], hash_key(vm, a[1], 0), NULL));
}

static Value set_add(VM* vm, int argc, Value* a) {
    ObjMap* s = AS_MAP(a[0]);
    const int before = s->live;
    map_put(s, a[1], hash_key(vm, a[1], 0), void_val());

    return bool_val(s->live != before);
}

/* the keys of a map, the items of a set: in insertion order */
static Value map_keys(VM* vm, int argc, Value* a) {
    ObjMap* m = AS_MAP(a[0]);
    ObjArray* r = array_new(vm, m->live);
    for (int i = 0; i < m->count; i++) {
        if (m->entries[i].live) {
            r->items[r->count++] = m->entries[i].key;
        }
    }

    return obj_val(r);
}

static Value map_values(VM* vm, int argc, Value* a) {
    ObjMap* m = AS_MAP(a[0]);
    ObjArray* r = array_new(vm, m->live);
    for (int i = 0; i < m->count; i++) {
        if (m->entries[i].live) {
            r->items[r->count++] = m->entries[i].value;
        }
    }

    return obj_val(r);
}

/* [tuple(key, value), ...]; no GC can run inside a native, so the half-built array is safe */
static Value map_entries(VM* vm, int argc, Value* a) {
    ObjMap* m = AS_MAP(a[0]);
    ObjArray* r = array_new(vm, m->live);
    for (int i = 0; i < m->count; i++) {
        if (!m->entries[i].live) {
            continue;
        }

        ObjTuple* t = tuple_new(vm, 2);
        t->items[0] = m->entries[i].key;
        t->items[1] = m->entries[i].value;
        r->items[r->count++] = obj_val(t);
    }

    return obj_val(r);
}

static Value map_clear_fn(VM* vm, int argc, Value* a) {
    map_clear(AS_MAP(a[0]));
    return void_val();
}

/* union / intersection / difference build a new set, in the order of a, then b */
static Value set_combine(VM* vm, Value* a, int mode) {
    const ObjMap* x = AS_MAP(a[0]);
    ObjMap* y = AS_MAP(a[1]);
    ObjMap* r = map_new(vm, OBJ_SET);
    for (int i = 0; i < x->count; i++) {
        const MapEntry* e = &x->entries[i];
        if (!e->live) {
            continue;
        }

        const bool in_y = map_find(y, e->key, e->hash) != NULL;
        if (mode == 0 || (mode == 1 && in_y) || (mode == 2 && !in_y)) {
            map_put(r, e->key, e->hash, void_val());
        }
    }

    if (mode == 0) {
        for (int i = 0; i < y->count; i++) {
            if (y->entries[i].live) {
                map_put(r, y->entries[i].key, y->entries[i].hash, void_val());
            }
        }
    }

    return obj_val(r);
}

static Value set_union(VM* vm, int argc, Value* a) {
    return set_combine(vm, a, 0);
}

static Value set_intersection(VM* vm, int argc, Value* a) {
    return set_combine(vm, a, 1);
}

static Value set_difference(VM* vm, int argc, Value* a) {
    return set_combine(vm, a, 2);
}

static const NativeImpl STD[] = {
    {"std_in_range", std_in_range},
    {"std_range_has", std_range_has},
    {"std_map_new", map_new_fn},
    {"std_set_new", set_new_fn},
    {"std_map_get", map_get_fn},
    {"std_map_put", map_put_fn},
    {"std_map_has", map_has},
    {"std_map_remove", map_remove_fn},
    {"std_map_keys", map_keys},
    {"std_map_values", map_values},
    {"std_map_entries", map_entries},
    {"std_map_clear", map_clear_fn},
    {"std_set_add", set_add},
    {"std_set_remove", set_remove},
    {"std_set_union", set_union},
    {"std_set_intersection", set_intersection},
    {"std_set_difference", set_difference},
    {NULL, NULL},
};

void mod_std_register(VM* vm) {
    vm_register_native_impls(vm, "std", STD);
}
