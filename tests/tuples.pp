import io
import std

// ---- construction, reading, printing
const t = tuple(1, "a", 2.5)
io:print_nl(t, #t, t[1], t[-1], type_of(t), str(t))
const tuple<int, string, float> typed = t
const tuple<int> single = tuple(7)
io:print_nl(typed, single)

// ---- tuples are values: equality, keys of maps and sets
io:print_nl(tuple(1, "a") == tuple(1, "a"), tuple(1, 2) == tuple(1, 2, 3), tuple(1.0) == tuple(1))
const visited = std:set()
visited.add(tuple(0, 0))
io:print_nl(tuple(0, 0) in visited, tuple(0, 1) in visited, tuple(0.0, 0) in visited)

// ---- generic element types and overloads chosen by the shape
<K, V>function swap(tuple<K, V> p) -> tuple<V, K> { return tuple(p[1], p[0]) }
io:print_nl(swap(tuple("a", 1)))

function kind(tuple<int> t) -> string { return "one int" }
function kind(tuple<int, string> t) -> string { return "int and string" }
function kind(tuple<any, any> t) -> string { return "any pair" }
function kind(tuple<int, int, int> t) -> string { return "three ints" }
io:print_nl(kind(tuple(1)), kind(tuple(1, "x")), kind(tuple(1, 2)), kind(tuple(1, 2, 3)))

// ---- patterns
if tuple(x, y) = tuple(3, 4) { io:print_nl("destructured", x, y) }
const pair = tuple(1, 2)
io:print_nl(match pair { tuple(a) -> "one", tuple(a, b, c) -> "three", tuple(a, b) -> "two", _ -> "?" })
io:print_nl(match tuple("go", 3) { tuple("stop", _) -> "stop", tuple("go", n) if n > 2 -> "go far", _ -> "?" })

// ---- iteration, copy (a tuple cannot change, so copy gives it back)
for i, v in tuple("p", "q") { io:print_nl(i, v) }
io:print_nl(copy(t) == t)

// ---- interfaces
function str(tuple<int, int> p) -> string { return "point(" + str(p[0]) + ", " + str(p[1]) + ")" }
impl std:Str for tuple<int, int>
function show(std:Str s) -> string { return s.str() }
io:print_nl(show(tuple(1, 2)))
