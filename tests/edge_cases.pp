import io
import math -> { abs, floor, ceil, round, sqrt, pow }
let x=-1
io:print_nl(x, 3-1, 3 - -1, !true, !(1 == 2))
match x { -1 -> io:print_nl("neg one"), _ -> io:print_nl("other") }
function nested() -> Error<Error<int>> { return success(success(1)) }
io:print_nl(nested())
for i in ...10 {
    match i {
        3 -> { break }
        _ -> { io:print(i, "") }
    }
}
io:print_nl()
const fns = []
for i in ...3 { push(fns, function() { return i }) }
io:print_nl(fns.map(function(f) { return f() }))
let arr = [1, 2]
arr[0] += 10
io:print_nl(arr)
function <>(string a, string b) -> string { return a + " <> " + b }
precedence <> under +
io:print_nl("a" <> "b" + "c")
const obj = { hello: function(string n) { return "hi " + n } }
io:print_nl(obj:hello("bob"))
const r = [1, 2, 3, 4, 5, 6]
    .filter(function(v) { return v % 2 == 0 })
    .map(function(v) { return v * v })
io:print_nl(r)
io:print_nl(#(...10), #(1...=10), range(0, 10, 3))
for c in range(10, 0, -3) { io:print(c, "") }
io:print_nl()
function only_ints(int a) -> int { return a }
io:print_nl(only_ints(3))
io:print_nl(1 < 2 && 2 < 3 || false, 5 >= 5)
const s = "line1\nline2\ttab \u{1F600}"
io:print_nl(s)
io:print_nl([1, [2, 3], { a: [] }])
io:print_nl(match some(4) { some(n) if n > 3 -> "big", some(n) -> "small", none -> "none" })
io:print_nl(match none() { some(n) -> "some", none -> "none" })
let int? maybe = none()
maybe = some(3)
io:print_nl(maybe)
type Pair<A, B> = { A first, B second }
const Pair<int, string> pr = { first: 1, second: "one" }
io:print_nl(pr:first, pr:second, type_of(pr))
io:print_nl(min(3, 1), max(2.5, 1), abs(-3), floor(2.7), sqrt(16), pow(2, 0.5))
io:print_nl(str(1.5) + "!", int("42") + 1, float("2.5"), int(3.9))
io:print_nl(index_of([5, 6, 7], 7), index_of("hello", "z"), contains("hello", "ell"), slice("hello", 1, -1))
