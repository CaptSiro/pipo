/*
 * The C half of lib/std.pp (a mixed module): bodies of its `native` functions.
 */
#include <math.h>

#include "vm.h"

/*
 * has(number x, Range r): is x one of the elements a for loop over r visits,
 * i.e. x == start + k*step for some 0 <= k < #r. Int ranges are exact. When
 * the range or x is a float, k is rounded to the nearest element and x must be
 * within a tiny fraction of the step from it, so 0.3 in range(0, 1, 0.1) holds
 * even though 3 * 0.1 is 0.30000000000000004.
 */
static Value std_has_range(VM* vm, int argc, Value* a) {
    Value x = a[0];
    ObjRange* r = AS_RANGE(a[1]);
    int64_t len = range_length(r);
    if (len == 0) return bool_val(false);

    if (x.kind == VAL_INT && !range_is_float(r)) {
        int64_t start = r->start.as.i, step = r->step.as.i, d = x.as.i - start;
        if (d % step != 0) return bool_val(false);
        int64_t k = d / step;
        return bool_val(k >= 0 && k < len);
    }

    double xf = num_as_double(x), start = num_as_double(r->start), step = num_as_double(r->step);
    if (!isfinite(xf)) return bool_val(false);
    double k = round((xf - start) / step);
    if (k < 0 || k >= (double)len) return bool_val(false);
    double elem = num_as_double(range_at(r, (int64_t)k));
    return bool_val(fabs(elem - xf) <= fabs(step) * 1e-9);
}

/**
 * Checks only start and end, does not take in count step property.
 * The bounds follow the range's direction and end: 0...5 is [0, 5),
 * 5...1 is (1, 5], 5...=1 is [1, 5] and range(0, 1, 0.25) is [0, 1).
 */
static Value std_in_range(VM* vm, int argc, Value* a)
{
    const Value x = a[0];
    ObjRange* r = AS_RANGE(a[1]);
    const double xf = num_as_double(x);
    if (!isfinite(xf))
    {
        return bool_val(false);
    }

    const double start = num_as_double(r->start);
    const double step = num_as_double(r->step);
    double end = num_as_double(r->end);
    if (r->inclusive)
    {
        /* ...= stored end one step past the last element: undo that, the end is included */
        end -= step;
        return bool_val(step > 0 ? xf >= start && xf <= end : xf <= start && xf >= end);
    }
    return bool_val(step > 0 ? xf >= start && xf < end : xf <= start && xf > end);
}

static const NativeImpl STD[] = {
    {"std_in_range", std_in_range},
    {"std_has_range", std_has_range},
    {NULL, NULL},
};

void mod_std_register(VM* vm) { vm_register_native_impls(vm, "std", STD); }
