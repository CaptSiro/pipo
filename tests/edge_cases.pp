import io
let x=-1
io:print(x, 3-1, 3 - -1, !true, !(1 == 2))
match x { -1 -> io:print("neg one"), _ -> io:print("other") }
function nested() -> Error<Error<int>> { return success(success(1)) }
io:print(nested())
for i in ...10 {
    match i {
        3 -> { break }
        _ -> { io:write(i, "") }
    }
}
io:print()
const fns = []
for i in ...3 { push(fns, function() { return i }) }
io:print(fns.map(function(f) { return f() }))
let arr = [1, 2]
arr[0] += 10
io:print(arr)
function <>(string a, string b) -> string { return a + " <> " + b }
precedence <> under +
io:print("a" <> "b" + "c")
const obj = { hello: function(string n) { return "hi " + n } }
io:print(obj:hello("bob"))
const r = [1, 2, 3, 4, 5, 6]
    .filter(function(v) { return v % 2 == 0 })
    .map(function(v) { return v * v })
io:print(r)
io:print(#(...10), #(1...=10), range(0, 10, 3))
for c in range(10, 0, -3) { io:write(c, "") }
io:print()
function only_ints(int a) -> int { return a }
io:print(only_ints(3))
io:print(1 < 2 && 2 < 3 || false, 5 >= 5)
const s = "line1\nline2\ttab \u{1F600}"
io:print(s)
io:print([1, [2, 3], { a: [] }])
io:print(match some(4) { some(n) if n > 3 -> "big", some(n) -> "small", none -> "none" })
io:print(match none() { some(n) -> "some", none -> "none" })
let int? maybe = none()
maybe = some(3)
io:print(maybe)
type Pair<A, B> = { A first, B second }
const Pair<int, string> pr = { first: 1, second: "one" }
io:print(pr:first, pr:second, type_of(pr))
io:print(min(3, 1), max(2.5, 1), abs(-3), floor(2.7), sqrt(16), pow(2, 0.5))
io:print(str(1.5) + "!", int("42") + 1, float("2.5"), int(3.9))
io:print(index_of([5, 6, 7], 7), index_of("hello", "z"), contains("hello", "ell"), slice("hello", 1, -1))
