native "math_pi" function m_pi() -> float
export const pi = m_pi()

native "math_e" function m_e() -> float
export const e = m_e();

export native "math_abs" function abs(number x) -> number
export native "math_floor" function floor(number x) -> int
export native "math_ceil" function ceil(number x) -> int
export native "math_round" function round(number x) -> int

export native "math_sqrt" function sqrt(number x) -> float
export const sqrt2 = sqrt(2)

export native "math_pow" function pow(number base, number exponent) -> float

// ---- rounding: trunc drops the fraction (toward 0) and returns an int like floor, ceil and round
export native "math_trunc" function trunc(number x) -> int
// -1, 0 or 1 for an int; -1.0 or 1.0 for a float, and 0.0, -0.0 and NaN as they are
export native "math_sign" function sign(int x) -> int
export native "math_sign" function sign(float x) -> float
// the nearest 32-bit / 16-bit float, as a float: fround(0.1) is 0.10000000149011612
export native "math_fround" function fround(number x) -> float
export native "math_f16round" function f16round(number x) -> float

// ---- roots, powers and logarithms; outside the domain the result is NaN (log(-1)) or infinite (log(0))
export native "math_cbrt" function cbrt(number x) -> float
export native "math_exp" function exp(number x) -> float
// e^x - 1 and log(1 + x), exact also for x close to 0
export native "math_expm1" function expm1(number x) -> float
export native "math_log1p" function log1p(number x) -> float
// the natural logarithm (base e)
export native "math_log" function log(number x) -> float
export native "math_log2" function log2(number x) -> float
export native "math_log10" function log10(number x) -> float

// ---- trigonometry, in radians
export native "math_sin" function sin(number x) -> float
export native "math_cos" function cos(number x) -> float
export native "math_tan" function tan(number x) -> float
export native "math_asin" function asin(number x) -> float
export native "math_acos" function acos(number x) -> float
export native "math_atan" function atan(number x) -> float
// the angle of the point (x, y), in -pi...=pi; note the order: y first
export native "math_atan2" function atan2(number y, number x) -> float

// ---- hyperbolic functions
export native "math_sinh" function sinh(number x) -> float
export native "math_cosh" function cosh(number x) -> float
export native "math_tanh" function tanh(number x) -> float
export native "math_asinh" function asinh(number x) -> float
export native "math_acosh" function acosh(number x) -> float
export native "math_atanh" function atanh(number x) -> float

// ---- the length of a vector, sqrt(x^2 + y^2 + ...), without overflow on the way; hypot([]) is 0.0
export native "math_hypot2" function hypot(number x, number y) -> float
export native "math_hypot3" function hypot(number x, number y, number z) -> float
export native "math_hypot_array" function hypot(number[] values) -> float

// the sum as if computed exactly, rounded once: sum_precise([1e20, 1, -1e20]) is 1.0 (a plain sum gives 0.0).
// A partial sum above the largest float is an error.
export native "math_sum_precise" function sum_precise(number[] values) -> float

// ---- 32-bit ints, as in JavaScript: both use only the low 32 bits of their arguments
// the number of leading zero bits: clz32(1) is 31, clz32(0) is 32, clz32(-1) is 0
export native "math_clz32" function clz32(int x) -> int
// a * b wrapping around like a 32-bit int: imul(0x7fffffff, 2) is -2
export native "math_imul" function imul(int a, int b) -> int

// ---- random numbers; not for cryptography
// random() and random_int() use one global generator, seeded differently on every run.
// A float in [0, 1)
export native "math_random" function random() -> float
// an int in [0, end) / [start, end), every one equally likely; an empty range is an error
export native "math_random_int" function random_int(int end) -> int
export native "math_random_int" function random_int(int start, int end) -> int

// A Random is a generator of its own: the same seed gives the same numbers, on every platform.
// copy(r) makes a second generator that continues exactly like r.
//     const r = math:seed(42)
//     r.value()                         // a float in [0, 1)
//     r.interval(1, 7)                  // a dice roll
//     r.interval_float(-1, 1)           // a float in [-1, 1)
//     r.item([1, 2, 3, 4, 5, 6])        // a dice roll from the possible items
//     r.seed(42)                           // starts over
export native type Random
export native "math_seed_new" function seed(int seed) -> Random
export native "math_seed_set" function seed(Random r, int seed) -> void
export native "math_poll" function value(Random r) -> float
export native "math_poll_int" function interval(Random r, int end) -> int
export native "math_poll_int" function interval(Random r, int start, int end) -> int

// a float in [0, end) / [start, end); an empty range is an error
export function interval_float(Random r, number end) -> float {
    return r.interval_float(0, end)
}

export function interval_float(Random r, number start, number end) -> float {
    if !(start < end) {
        panic("interval_float(" + str(start) + ", " + str(end) + "): the range is empty, end must be greater than start")
    }

    // start * (1 - t) + end * t cannot overflow like end - start can (-1e308 to 1e308),
    // but rounding can land on end itself (or just below start): then draw again
    let x = end
    while x < start || x >= end {
        const t = r.value()
        x = start * (1 - t) + end * t
    }

    return x
}

export <T>function item(Random r, T[] array) -> T? {
    if #array == 0 {
        return none()
    }

    return some(array[r.interval(0, #array)])
}

// ---- interpolation: the range versions use the range's bounds in its direction, with the
// last element as the end of a ...= range (0...=10 ends at 10, 0...10 too, 10...0 goes from 10 to 0)

// the point t of the way from a to b: lerp(0, 10, 0.25) is 2.5; t outside [0, 1] goes past a or b.
// Exact at both ends: lerp(a, b, 0) is a and lerp(a, b, 1) is b.
export function lerp(number a, number b, number t) -> float {
    return float(1 - t) * a + t * b
}

// where a range ends: a ...= range stores its end one step past the last element
function end(range r) -> number {
    if r:inclusive {
        return r:end - r:step
    }

    return r:end
}

export function lerp(range r, number t) -> float {
    return lerp(r:start, r.end(), t)
}

// the inverse of lerp: how far x is from a to b, linear(0, 10, 2.5) is 0.25
export function linear(number a, number b, number x) -> float {
    return float(x - a) / (b - a)
}

export function linear(range r, number x) -> float {
    return linear(r:start, r.end(), x)
}

// value moved from one scale to another: map(5, 0, 10, 0, 100) is 50.0
export function map(number value, number from_a, number from_b, number to_a, number to_b) -> float {
    return lerp(to_a, to_b, linear(from_a, from_b, value))
}

export function map(number value, range from, range to) -> float {
    return map(value, from:start, from.end(), to:start, to.end())
}
