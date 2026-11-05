//
// Created by CaptSiro on 11/5/2026.
//
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vm.h"

static Value float_to_int(VM* vm, double d, const char* what) {
    if (isnan(d) || isinf(d) || d > 9.2e18 || d < -9.2e18) {
        rt_error(vm, 0, "%s: result does not fit in an int", what);
    }

    return int_val((int64_t)d);
}

static double math_num(Value v) {
    return v.kind == VAL_INT ?
        (double)v.as.i
        : v.as.f;
}

static Value math_pi(VM* vm, int argc, Value* a) {
    return float_val(3.14159265358979323846);
}

static Value math_e(VM* vm, int argc, Value* a) {
    return float_val(2.71828182845904523536);
}

static Value math_abs(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT) {
        return int_val(a[0].as.i < 0 ? -a[0].as.i : a[0].as.i);
    }

    return float_val(fabs(a[0].as.f));
}

static Value math_floor(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT
        ? a[0]
        : float_to_int(vm, floor(a[0].as.f), "floor");
}

static Value math_ceil(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT
        ? a[0]
        : float_to_int(vm, ceil(a[0].as.f), "ceil");
}

static Value math_round(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT
        ? a[0]
        : float_to_int(vm, round(a[0].as.f), "round");
}

static Value math_sqrt(VM* vm, int argc, Value* a) {
    return float_val(sqrt(math_num(a[0])));
}

static Value math_pow(VM* vm, int argc, Value* a) {
    return float_val(pow(math_num(a[0]), math_num(a[1])));
}

/* ------------------------------------------------- one number in, one float out
 *
 * Trigonometry, hyperbolic functions, exponents and logarithms. Every one
 * takes an int or a float and returns a float; outside the domain the result
 * is NaN (acos(2), log(-1)) or an infinity (log(0)), as in C and JavaScript.
 */

#define MATH_UNARY(name)                                                       \
    static Value math_##name(VM* vm, int argc, Value* a) {                     \
        return float_val(name(math_num(a[0])));                                \
    }

MATH_UNARY(sin)
MATH_UNARY(cos)
MATH_UNARY(tan)
MATH_UNARY(asin)
MATH_UNARY(acos)
MATH_UNARY(atan)
MATH_UNARY(sinh)
MATH_UNARY(cosh)
MATH_UNARY(tanh)
MATH_UNARY(asinh)
MATH_UNARY(acosh)
MATH_UNARY(atanh)
MATH_UNARY(exp)
MATH_UNARY(expm1)
MATH_UNARY(log)
MATH_UNARY(log1p)
MATH_UNARY(log2)
MATH_UNARY(log10)
MATH_UNARY(cbrt)

#undef MATH_UNARY

/* atan2(y, x): the angle of the point (x, y), in -pi...=pi */
static Value math_atan2(VM* vm, int argc, Value* a) {
    return float_val(atan2(math_num(a[0]), math_num(a[1])));
}

/* ------------------------------------------------- rounding and sign */

static Value math_trunc(VM* vm, int argc, Value* a) {
    return a[0].kind == VAL_INT
        ? a[0]
        : float_to_int(vm, trunc(a[0].as.f), "trunc");
}

/* sign(int) -> -1, 0 or 1; sign(float) -> -1.0 or 1.0, or x itself for 0.0, -0.0 and NaN */
static Value math_sign(VM* vm, int argc, Value* a) {
    if (a[0].kind == VAL_INT) {
        const int64_t i = a[0].as.i;
        return int_val((i > 0) - (i < 0));
    }

    const double f = a[0].as.f;
    if (f > 0) {
        return float_val(1.0);
    }

    if (f < 0) {
        return float_val(-1.0);
    }

    return a[0];
}

/* fround(x): the nearest 32-bit float, as a float */
static Value math_fround(VM* vm, int argc, Value* a) {
    return float_val((double)(float)math_num(a[0]));
}

/*
 * The nearest 16-bit (half precision) float. Rounds once, straight from the
 * double (ties to even), so there is no double rounding through float. Half
 * floats have 10 mantissa bits, exponents -14...=15 and subnormals down to
 * 2^-24; 65520 and above round to infinity.
 */
static double to_half(double x) {
    if (isnan(x) || isinf(x) || x == 0) {
        return x;
    }

    const double ax = fabs(x);
    if (ax >= 65520.0) {
        return copysign(INFINITY, x);
    }

    int exponent;
    frexp(ax, &exponent); /* ax = m * 2^exponent, m in [0.5, 1) */
    exponent -= 1;        /* now m in [1, 2) */
    if (exponent < -14) {
        exponent = -14; /* subnormal: a fixed step of 2^-24 */
    }

    /* step is the distance between neighbouring half floats; scaling by a power of 2 is exact */
    const int step = exponent - 10;
    const double r = ldexp(nearbyint(ldexp(ax, -step)), step);

    return copysign(r, x);
}

/* f16round(x): the nearest 16-bit float, as a float */
static Value math_f16round(VM* vm, int argc, Value* a) {
    return float_val(to_half(math_num(a[0])));
}

/* ------------------------------------------------- 32-bit integer helpers, as in JavaScript
 *
 * Both use the low 32 bits of their arguments, like JavaScript's ToUint32 /
 * ToInt32: clz32(-1) is 0 and clz32(1 << 32) is 32.
 */

/* clz32(x): the number of leading zero bits of x as an unsigned 32-bit int */
static Value math_clz32(VM* vm, int argc, Value* a) {
    uint32_t x = (uint32_t)a[0].as.i;
    if (x == 0) {
        return int_val(32);
    }

    int n = 0;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }

    return int_val(n);
}

/* imul(a, b): a * b as 32-bit ints, wrapping around like C's int32_t */
static Value math_imul(VM* vm, int argc, Value* a) {
    const uint32_t product = (uint32_t)a[0].as.i * (uint32_t)a[1].as.i;
    return int_val((int64_t)(int32_t)product);
}

/* ------------------------------------------------- hypot and sum_precise */

/* hypot(x, y), hypot(x, y, z): the length of a vector, without overflow on the way */
static Value math_hypot2(VM* vm, int argc, Value* a) {
    return float_val(hypot(math_num(a[0]), math_num(a[1])));
}

static Value math_hypot3(VM* vm, int argc, Value* a) {
    return float_val(hypot(hypot(math_num(a[0]), math_num(a[1])), math_num(a[2])));
}

/* hypot(values): the same for any number of values; hypot([]) is 0.0 */
static Value math_hypot_array(VM* vm, int argc, Value* a) {
    const ObjArray* values = AS_ARRAY(a[0]);
    double r = 0;
    for (int i = 0; i < values->count; i++) {
        r = hypot(r, math_num(values->items[i]));
    }

    return float_val(r);
}

/*
 * sum_precise(values): the sum of the values as if computed exactly, then
 * rounded once to the nearest float (Shewchuk's algorithm, the one behind
 * Python's math.fsum). sum_precise([0.1, 0.2]) is 0.30000000000000004 like
 * 0.1 + 0.2, but sum_precise([1e20, 1, -1e20]) is 1.0, not 0.0.
 *
 * The partials are non-overlapping floats, smallest first, whose exact sum is
 * the sum of the values seen so far. Infinities and NaN are summed apart:
 * inf + -inf is NaN. Ints are converted to floats first, so ints above 2^53
 * lose their lowest bits. A partial sum above the largest float is a runtime
 * error, even when later values would bring the total back into range.
 */
static Value math_sum_precise(VM* vm, int argc, Value* a) {
    const ObjArray* values = AS_ARRAY(a[0]);
    double* partials = NULL;
    int count = 0;
    int cap = 0;
    double special = 0; /* the sum of the infinities and NaNs */
    bool has_special = false;

    for (int i = 0; i < values->count; i++) {
        double x = math_num(values->items[i]);
        if (isnan(x) || isinf(x)) {
            special += x;
            has_special = true;
            continue;
        }

        int kept = 0;
        for (int j = 0; j < count; j++) {
            double y = partials[j];
            if (fabs(x) < fabs(y)) {
                const double t = x;
                x = y;
                y = t;
            }

            const double hi = x + y;
            if (isinf(hi)) {
                free(partials);
                rt_error(vm, 0, "sum_precise: a partial sum does not fit in a float");
            }

            const double lo = y - (hi - x);
            if (lo != 0) {
                partials[kept++] = lo;
            }

            x = hi;
        }

        count = kept;
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            partials = realloc(partials, sizeof(double) * (size_t)cap);
            if (!partials) {
                rt_error(vm, 0, "sum_precise: out of memory");
            }
        }

        partials[count++] = x;
    }

    if (has_special) {
        free(partials);
        return float_val(special);
    }

    /* add the partials from the largest down, stopping once the rest cannot change the result */
    double hi = 0;
    if (count > 0) {
        int n = count - 1;
        hi = partials[n];
        double lo = 0;
        while (n > 0) {
            const double x = hi;
            const double y = partials[--n];
            hi = x + y;
            lo = y - (hi - x);
            if (lo != 0) {
                break;
            }
        }

        /*
         * Round half to even, corrected: when the rest (lo) is exactly half a
         * step and the next partial has the same sign, the true sum is a
         * little further than half, so round away from hi.
         */
        if (n > 0 && ((lo < 0 && partials[n - 1] < 0) || (lo > 0 && partials[n - 1] > 0))) {
            const double y = lo * 2;
            const double x = hi + y;
            if (y == x - hi) {
                hi = x;
            }
        }
    }

    free(partials);

    return float_val(hi);
}

/* ------------------------------------------------- random
 *
 * xoshiro256** (Blackman and Vigna): 256 bits of state, fast, and good enough
 * for games and simulations. Not for cryptography.
 *
 * A Random value (math:seed(42)) owns its own state, so the same seed always
 * gives the same numbers, on every platform. math:random() and
 * math:random_int() use one global generator, seeded on first use from the
 * clock and an address, so runs differ even within the same second.
 */

typedef struct {
    uint64_t s[4];
} RngState;

typedef struct {
    ObjInstance base;
    RngState state;
} ObjRandom;

#define RANDOM(v) ((ObjRandom*)(v).as.o)

static RngState global_rng;
static bool global_rng_seeded = false;

static uint64_t splitmix64(uint64_t* x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;

    return z ^ (z >> 31);
}

/* spreads one 64-bit seed over the whole state; never all zeros, which xoshiro cannot leave */
static void rng_seed(RngState* rng, uint64_t seed) {
    for (int i = 0; i < 4; i++) {
        rng->s[i] = splitmix64(&seed);
    }
}

static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t rng_next(RngState* rng) {
    uint64_t* s = rng->s;
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);

    return result;
}

/* a float in [0, 1): the top 53 bits, so every result is a multiple of 2^-53 */
static double rng_float(RngState* rng) {
    return (double)(rng_next(rng) >> 11) * 0x1.0p-53;
}

/*
 * an int in [start, end), every one equally likely. Plain rng % span would
 * favour small results when span does not divide 2^64, so values below
 * threshold (2^64 mod span of them) are thrown away and drawn again.
 */
static int64_t rng_int(RngState* rng, int64_t start, int64_t end) {

    const uint64_t span = (uint64_t)end - (uint64_t)start;
    const uint64_t threshold = (0 - span) % span;
    uint64_t r;
    do {
        r = rng_next(rng);
    } while (r < threshold);

    return (int64_t)((uint64_t)start + r % span);
}

/* the int arguments of random_int / interval: [0, end) or [start, end), which must not be empty */
static void int_range(VM* vm, const char* fn, const Value* args, int n, int64_t* start, int64_t* end) {
    *start = n > 1 ? args[0].as.i : 0;
    *end = args[n - 1].as.i;
    if (*start < *end) {
        return;
    }

    if (n > 1) {
        rt_error(vm, 0, "%s(%" PRId64 ", %" PRId64 "): the range is empty, end must be greater than start", fn, *start, *end);
    }

    rt_error(vm, 0, "%s(%" PRId64 "): the range is empty, end must be greater than 0", fn, *end);
}

static RngState* global(void) {
    if (!global_rng_seeded) {
        const uint64_t seed = (uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)&global_rng;
        rng_seed(&global_rng, seed);
        global_rng_seeded = true;
    }

    return &global_rng;
}

/* random() -> float in [0, 1), from the global generator */
static Value math_random(VM* vm, int argc, Value* a) {
    return float_val(rng_float(global()));
}

/* random_int(end) -> int in [0, end); random_int(start, end) -> int in [start, end) */
static Value math_random_int(VM* vm, int argc, Value* a) {
    int64_t start, end;
    int_range(vm, "random_int", a, argc, &start, &end);

    return int_val(rng_int(global(), start, end));
}

/* seed(s) -> Random: a new generator; the same seed gives the same numbers */
static Value math_seed_new(VM* vm, int argc, Value* a) {
    ObjRandom* r = (ObjRandom*)instance_new(vm, vm_native_type(vm, "math", "Random"));
    rng_seed(&r->state, (uint64_t)a[0].as.i);

    return obj_val(r);
}

/* seed(r, s): starts r over, as if it was made with seed(s) */
static Value math_seed_set(VM* vm, int argc, Value* a) {
    rng_seed(&RANDOM(a[0])->state, (uint64_t)a[1].as.i);

    return void_val();
}

/* value(r) -> float in [0, 1) */
static Value math_poll(VM* vm, int argc, Value* a) {
    return float_val(rng_float(&RANDOM(a[0])->state));
}

/* interval(r, end) -> int in [0, end); interval(r, start, end) -> int in [start, end) */
static Value math_poll_int(VM* vm, int argc, Value* a) {
    int64_t start, end;
    int_range(vm, "interval", a + 1, argc - 1, &start, &end);

    return int_val(rng_int(&RANDOM(a[0])->state, start, end));
}

/* copy(r): a second generator in the same state, which then gives the same numbers as r */
static ObjInstance* random_copy(VM* vm, ObjInstance* self) {
    ObjRandom* r = (ObjRandom*)instance_new(vm, self->type);
    r->state = ((ObjRandom*)self)->state;

    return (ObjInstance*)r;
}

static const NativeClass TYPES[] = {
    {
        .name = "Random",
        .size = sizeof(ObjRandom),
        .constructor = "math:seed",
        .copy = random_copy,
    },
    {.name = NULL},
};

static const NativeImpl MATH[] = {
    {"math_pi", math_pi},
    {"math_e", math_e},
    {"math_abs", math_abs},
    {"math_floor", math_floor},
    {"math_ceil", math_ceil},
    {"math_round", math_round},
    {"math_sqrt", math_sqrt},
    {"math_pow", math_pow},
    {"math_sin", math_sin},
    {"math_cos", math_cos},
    {"math_tan", math_tan},
    {"math_asin", math_asin},
    {"math_acos", math_acos},
    {"math_atan", math_atan},
    {"math_atan2", math_atan2},
    {"math_sinh", math_sinh},
    {"math_cosh", math_cosh},
    {"math_tanh", math_tanh},
    {"math_asinh", math_asinh},
    {"math_acosh", math_acosh},
    {"math_atanh", math_atanh},
    {"math_exp", math_exp},
    {"math_expm1", math_expm1},
    {"math_log", math_log},
    {"math_log1p", math_log1p},
    {"math_log2", math_log2},
    {"math_log10", math_log10},
    {"math_cbrt", math_cbrt},
    {"math_trunc", math_trunc},
    {"math_sign", math_sign},
    {"math_fround", math_fround},
    {"math_f16round", math_f16round},
    {"math_clz32", math_clz32},
    {"math_imul", math_imul},
    {"math_hypot2", math_hypot2},
    {"math_hypot3", math_hypot3},
    {"math_hypot_array", math_hypot_array},
    {"math_sum_precise", math_sum_precise},
    {"math_random", math_random},
    {"math_random_int", math_random_int},
    {"math_seed_new", math_seed_new},
    {"math_seed_set", math_seed_set},
    {"math_poll", math_poll},
    {"math_poll_int", math_poll_int},
    {NULL, NULL},
};

void mod_math_register(VM* vm) {
    vm_register_native_types(vm, "math", TYPES);
    vm_register_native_impls(vm, "math", MATH);
}