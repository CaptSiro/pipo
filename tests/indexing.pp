import io
import std
import "./modules/board" -> { board, rows }

// ---- x[i] calls [](x, i), x[i] = v calls []=(x, i, v): ordinary overloads
const xs = [10, 20, 30]
xs[0] = 11
io:print_nl(xs[0], xs[-1], xs.[](2), "abc"[1], { a: 1 }["a"], tuple(1, "t")[1])
xs.[]=(1, 21)
io:print_nl(xs)

// more indices, and new overloads for existing types
type Matrix = { int cols, float[] cells }
function [](Matrix m, int row, int col) -> float { return m:cells[row * m:cols + col] }
function []=(Matrix m, int row, int col, float value) -> void { m:cells[row * m:cols + col] = value }
const Matrix mx = { cols: 2, cells: [1.0, 2.0, 3.0, 4.0] }
mx[1, 0] = 30.0
mx[0, 1] += 0.5
io:print_nl(mx[0, 0], mx[0, 1], mx[1, 0], mx:cells)

<T>function [](T[] items, range r) -> T[] { return items.slice(r:start, r:end) }
io:print_nl(xs[0...2], xs[1...3], xs[2])

// ---- a type implementing std:Index, from another module: only get and put written there
const b = board(3, 2)
b[tuple(0, 0)] = "#"
b[tuple(2, 1)] = "@"
io:print_nl(
    b[tuple(2, 1)],
    b.get(tuple(5, 5)),
    rows(b)
)

// an interface parameter takes any Index: a map, a Board
<K, V>function show(std:Index<K, V> c, K key) -> string { return str(c[key]) }
io:print_nl(show(std:map({ a: 1 }), "a"), show(b, tuple(0, 0)))

// ---- std:Map: [] and []= through its impl of Index
const counts = std:map()
for w in ["a", "b", "a", "c", "a"] {
    if !counts.has(w) { counts[w] = 0 }
    counts[w] += 1
}
io:print_nl(counts, counts["a"])

// a generic impl: the type's own generics bind the interface's
<T>type Stack = { T[] items }
<T>function get(Stack<T> s, int i) -> T? {
    if i < 0 || i >= #s:items { return none() }
    return some(s:items[#s:items - 1 - i])
}
<T>function put(Stack<T> s, int i, T value) -> void { s:items[#s:items - 1 - i] = value }
<T>impl std:Index<int, T> for Stack<T>
const Stack<string> st = { items: ["bottom", "middle", "top"] }
st[0] = "TOP"
io:print_nl(st[0], st[2], st.get(7), st:items)

// ---- target op= value: an in-place op= when one accepts the values, else target = target op value
<T>function +=(T[] items, T[] more) -> void { for m in more { items.push(m) } }
const ys = [1]
ys += [2, 3]                          // in place: works on a const, the binding does not change
let n = 1
n += 1                                // no +=(int, int): n = n + 1
let s = "ab"
s *= 3                                // s = s * 3
io:print_nl(ys, n, s)

const nested = [[1], [2]]
nested[0] += [9]                      // +=(nested[0], [9]) mutates the inner array, no []= needed
nested[1][0] += 40                    // []=(nested[1], 0, nested[1][0] + 40)
io:print_nl(nested)

let x = 3
x <= 5                                // <= accepts (int, int): a comparison, not x = x < 5
io:print_nl(x)

// the container and the index are evaluated once
let calls = 0
function pick() -> int[] {
    calls += 1
    return xs
}
function at() -> int {
    calls += 10
    return 0
}
pick()[at()] += 100
io:print_nl(xs[0], calls)
