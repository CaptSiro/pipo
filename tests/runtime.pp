import io
import "./modules/geometry"
import "./modules/geometry.pp" -> g
import "./modules/geometry" -> { vec, +, length, Vec }
const a = vec(3, 4)
io:print_nl(a.length(), geometry:ORIGIN, g:length(a + vec(1, 1)))
io:print_nl(1 + 2, "a" + "b")
// closures
function counter() {
    let n = 0
    return function() { n += 1; return n }
}
const c = counter()
c(); c()
io:print_nl(c())
// GC stress
let total = 0
for i in ...300000 {
    const s = str(i) + "x"
    const o = { k: [i, s] }
    total += #o:k
}
io:print_nl(total)
let big = []
for i in ...100000 { push(big, { i: i }) }
io:print_nl(#big, big[-1]:i)
// recursion
function depth(int n) -> int { if n == 0 { return 0 } return 1 + depth(n - 1) }
io:print_nl(depth(3000))
// strings
io:print_nl(split("a,b,c", ","), join([1, 2, 3], "-"), upper("abc"), "héllo".chars(), 'x' * 3)
let obj = { a: 1 }
obj:b = 2
obj["c"] = 3
io:print_nl(keys(obj), obj)
io:print_nl(type_of(1), type_of(1.5), type_of(a), type_of(some(1)), type_of(io:print_nl))
