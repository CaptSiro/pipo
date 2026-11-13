import io
import std
// Map and Set on the wasm target: objects of the heap, hashed and compared by the host

const Map<string, int> ages = map([tuple("ann", 31), tuple("bob", 40)])
ages.put("cid", 25)
ages["dan"] = 52
ages["ann"] += 1
io:print_nl(ages, #ages, ages["bob"], ages.get("zed"), ages.get("cid"), "bob" in ages, ages.has("eve"))
io:print_nl(ages.keys(), ages.values(), ages.entries())
for name, age in ages {
    io:print(name, age, "")
}

io:print_nl()
io:print_nl(ages.remove("bob"), ages.remove("bob"), ages)
ages.put("bob", 1)
io:print_nl(ages, get_type(ages), str(ages))

// keys of any immutable type: ints, tuples, enum values; insertion order is kept
enum Color { red, green, blue }

const Map<tuple<int, int>, string> grid = map()
grid[tuple(0, 1)] = "a"
grid[tuple(2, 3)] = "b"
grid[tuple(0, 1)] = "c"
io:print_nl(grid, grid[tuple(2, 3)])
const Map<Color, int> votes = map()
for c in [Color:red, Color:blue, Color:red] {
    votes[c] = match votes.get(c) {
        some(n) -> n + 1
        none() -> 1
    }
}

io:print_nl(votes)

// many entries: the index grows
const Map<int, int> squares = map()
for i in ...1000 {
    squares[i] = i * i
}

let sum = 0
for i in ...1000 {
    sum += squares[i]
}

io:print_nl(#squares, sum, squares[999])
for i in ...990 {
    squares.remove(i)
}

io:print_nl(squares)

// sets
const s = set([3, 1, 3, 2])
io:print_nl(s, #s, 2 in s, s.has(7), s.add(7), s.add(7), s.remove(1), s.remove(1), s)
const t = set(2...6)
io:print_nl(s | t, s & t, s - t, s.values(), set(s) == s, s == t)
for x in set(["b", "a", "b"]) {
    io:print(x, "")
}

io:print_nl()

// copies, equality, and the lazy functions over them
const copy = map(ages)
copy["ann"] = 0
io:print_nl(ages["ann"], copy["ann"], copy == ages, map(ages) == ages)
io:print_nl(s.map(x -> x * 10).to_array(), ages.keys().filter(k -> k != "cid").to_array())
for k, v in ages.pairs() {
    io:print(k + "=" + str(v), "")
}

io:print_nl()
io:print_nl(std:in(2, 0...3), std:has(0...3, 3))

// a missing key through [] is an error
io:print_nl(ages["zed"])
