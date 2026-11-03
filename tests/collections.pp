import io
import std

// ---- std:map<K, V>
let std:map<string, int> ages = std:map()
ages.put("john", 22)
ages.put("anna", 31)
ages["bob"] = 40
io:print_nl(ages, #ages, ages["anna"], ages.find("x"), ages.find("john"), ages.find("x").unwrap_or(0))
io:print_nl("john" in ages, "zed" in ages, ages.has("bob"), ages.keys(), ages.values())
for name, age in ages { io:print_nl(name, age) }
for name in ages { io:print_nl(name) }
io:print_nl(ages.entries())
io:print_nl(ages.remove("john"), ages.remove("john"), ages)
ages["anna"] += 1
io:print_nl(ages["anna"], type_of(ages), str(ages), format("%s", ages))

// constructed from pairs, an object or another map; equality ignores order
const std:map<string, int> fromobj = std:map({ a: 1, b: 2 })
io:print_nl(fromobj, fromobj == std:map([tuple("b", 2), tuple("a", 1)]), fromobj == std:map(), copy(fromobj) == fromobj)

// keys: numbers (1 == 1.0), enum values, tuples
let std:map<int, string> m = std:map()
m.put(1, "one")
io:print_nl(m.find(1), m[1], m.has(2))
const keyed = std:map()
keyed.put(some(1), "s1")
keyed.put(none(), "n")
io:print_nl(keyed.find(some(1)), keyed[none()], keyed.find(some(2)))
const grid = std:map([tuple(tuple(0, 0), "origin")])
io:print_nl(grid[tuple(0, 0)], grid.find(tuple(1, 1)).unwrap_or("?"))

// a bare std:map accepts any map; lambdas get tuple<K, V> elements
function size_of(std:map m) -> int { return #m }
io:print_nl(size_of(m), size_of(fromobj))
io:print_nl(fromobj.entries().map(p -> p[0] + "=" + str(p[1])), fromobj.entries().map(p -> match p { tuple(k, _) -> k }))

// clear, copy
const c = copy(fromobj)
c.put("z", 26)
io:print_nl(#fromobj, #c)
c.clear()
io:print_nl(c, #c, c.find("a"))
c.put("again", 1)
io:print_nl(c)

// many entries: grow, remove, add again
const big = std:map()
for i in ...100000 { big.put(i, i * 2) }
for i in ...100000 { if i % 2 == 0 { big.remove(i) } }
for i in ...1000 { big.put(i, -i) }
let sum = 0
for k, v in big { sum += v }
io:print_nl(#big, big.find(1), big.find(2), big.find(99999), sum)

// ---- std:set<T>
const std:set<int> s = std:set([1, 2, 2, 3])
io:print_nl(s, #s, 2 in s, 9 in s, s.has(3), type_of(s))
io:print_nl(s | std:set([3, 4]), s & std:set([2, 3, 9]), s - std:set([1]))
io:print_nl(s.union(std:set([0])), s.intersection(std:set()), s.difference(s))
io:print_nl(s.add(3), s.add(10), s.remove(1), s.remove(1), s, s.values())
for x in std:set(0...5) { io:print(x) }
io:print_nl()
for i, x in std:set(["a", "b"]) { io:print_nl(i, x) }
io:print_nl(std:set(["x", "y"]) == std:set(["y", "x"]), std:set([1]) == std:map([tuple(1, 1)]))

<T>function first(std:set<T> items) -> T { return items.values()[0] }
io:print_nl(std:set(["x", "y"]).first())

// ---- the types themselves
io:print_nl(std:map, std:set, type_of(std:map))
function str(std:map m) -> string { return "a map of " + str(#m) }
impl std:Str for std:map
function show(std:Str x) -> string { return x.str() }
io:print_nl(show(std:map({ a: 1 })))
