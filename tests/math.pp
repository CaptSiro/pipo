import io
import math

io:print_nl(math:trunc(2.7), math:trunc(-2.7), math:trunc(5), type_of(math:trunc(1.5)))
io:print_nl(math:sign(-5), math:sign(0), math:sign(9), math:sign(-2.5), math:sign(0.0), math:sign(-0.0), math:sign(3.0))
io:print_nl(math:fround(0.1), math:fround(5), math:f16round(0.1), math:f16round(1.337), math:f16round(65519), math:f16round(65520), math:f16round(-0.00000001), math:f16round(0.0000001))
io:print_nl(math:cbrt(27), math:exp(1) == math:e, math:expm1(1e-10), math:log1p(1e-10))
io:print_nl(math:log(math:e), math:log2(1024), math:log10(1000), math:log(0), math:log(-1))
io:print_nl(math:sin(0), math:cos(0), math:tan(0), math:asin(1) * 2 == math:pi, math:acos(1), math:atan(1) * 4 == math:pi, math:acos(2))
io:print_nl(math:atan2(1, 1) * 4 == math:pi, math:atan2(0, -1) == math:pi, math:atan2(-1, 0))
io:print_nl(math:sinh(0), math:cosh(0), math:tanh(100), math:asinh(0), math:acosh(1), math:atanh(0))
io:print_nl(math:hypot(3, 4), math:hypot(2, 3, 6), math:hypot([1, 2, 2]), math:hypot([]), math:hypot(1e300, 1e300))
io:print_nl(math:sum_precise([0.1, 0.2]), math:sum_precise([1e20, 1, -1e20]), math:sum_precise([]), math:sum_precise([1, 2, 3]))
io:print_nl(math:sum_precise([0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1]), 0.1 + 0.1 + 0.1 + 0.1 + 0.1 + 0.1 + 0.1 + 0.1 + 0.1 + 0.1)
io:print_nl(math:sum_precise([1e308, 1e308 * 10, 1]), math:sum_precise([1e308 * 10, -1e308 * 10]))
io:print_nl(math:sum_precise([1, 1e-16, 1e-16]), math:sum_precise([1e16, 1, 1e-16 ]))
io:print_nl(math:clz32(1), math:clz32(0), math:clz32(-1), math:clz32(4294967296), math:clz32(65535))
io:print_nl(math:imul(2147483647, 2), math:imul(3, 4), math:imul(-5, 12), math:imul(4294967295, 5))
const r = math:random()
io:print_nl(r >= 0 && r < 1, type_of(r), math:random() != math:random())

// ---- Random: a seeded generator gives the same numbers every run
const r = math:seed(42)
const a = [r.value(), r.value(), r.interval(10), r.interval(1, 7)]
r.seed(42)
const b = [r.value(), r.value(), r.interval(10), r.interval(1, 7)]
io:print_nl(a == b, a, type_of(r), r)
const other = math:seed(43)
io:print_nl(other.value() != math:seed(42).value())
const c = copy(r)
io:print_nl(c.value() == r.value(), c.interval(1000) == r.interval(1000))

// every value of a small range comes up, nothing outside it
const counts = [0, 0, 0, 0, 0, 0]
for i in ...6000 { counts[r.interval(1, 7) - 1] += 1 }
io:print_nl(#counts.filter(n -> n > 800) == 6, #counts)
let ok = true
for i in ...1000 {
    const x = math:random_int(-3, 3)
    const y = math:random_int(5)
    const f = math:random()
    if x < -3 || x >= 3 || y < 0 || y >= 5 || f < 0 || f >= 1 { ok = false }
}
io:print_nl(ok, math:random_int(1), r.interval(-9223372036854775807, 9223372036854775807) != 0)
io:print_nl(math:seed(0).value(), math:seed(-1).interval(100))

// interval_float: [start, end), also where end - start does not fit in a float
let in_range = true
for i in ...1000 {
    const f = r.interval_float(-1.5, 2)
    const g = r.interval_float(3)
    if f < -1.5 || f >= 2 || g < 0 || g >= 3 { in_range = false }
}
const huge = r.interval_float(-1e308, 1e308)
io:print_nl(in_range, huge >= -1e308 && huge < 1e308, math:seed(7).interval_float(10, 20), type_of(r.interval_float(1, 2)))

// ---- item, lerp, linear, map; the range versions end at the last element of a ...= range
io:print_nl("item:", r.item([10, 20, 30]), r.item([]), r.item(["a"]))
io:print_nl("lerp:", math:lerp(0, 10, 0.5), math:lerp(0, 10, 1), math:lerp(0.1, 0.7, 1), math:lerp(0...10, 0.25), math:lerp(0...=10, 1), math:lerp(10...0, 0.25), math:lerp(0, 10, 1.5))
io:print_nl("linear:", math:linear(0, 10, 0), math:linear(0, 10, 10), math:linear(0, 10, 5), math:linear(0...=10, 2.5), math:linear(10...0, 2.5))
io:print_nl("map:", math:map(5, 0, 10, 0, 100), math:map(5, 0...10, 0...100), math:map(10, 0...=10, 0...=100), math:map(2.5, 0, 10, 100, 0), math:map(0.5, range(0, 1, 0.25), 10...=20))
