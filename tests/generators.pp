import io
import std
// generators (md/ITER.md): `emit v` gives a value and pauses, `return` ends, `return v` gives a last value

generator fib() -> int {
    let int a = 0
    let int b = 1
    while true {
        emit b
        const c = a + b
        a = b
        b = c
    }
}

// calling it builds the generator; nothing runs until next()
const Generator<int> f = fib()
io:print_nl(f.next(), f.next(), f.next(), f.next(), f)
for x in fib() {
    if x > 100 {
        break
    }

    io:print(x, "")
}

io:print_nl()
io:print_nl(fib().take(10).to_array())

// return ends it; return v gives v as the last value; next() after the end stays none()
generator upto(int n) -> int {
    for i in ...n {
        if i == 3 {
            return i * 100
        }

        emit i
    }
}

const u = upto(2)
io:print_nl(upto(10).to_array(), u.next(), u.next(), u.next(), u.next())

generator stop_early() -> string {
    emit "a"
    return
    emit "never"
}

io:print_nl(stop_early().to_array())

// pauses inside nested loops, if / match statements and pattern ifs; break and continue as usual
generator grid(int w, int h) -> tuple<int, int> {
    for y in ...h {
        let x = 0
        while x < w {
            x += 1
            if x == 2 {
                continue
            }

            match x {
                1 -> { emit tuple(x, y) }
                _ -> {
                    if some(z) = (x * 10).some() {
                        emit tuple(z, y)
                    }
                }
            }
        }
    }
}

io:print_nl(grid(3, 2).to_array())

// a generic generator, generators reading generators, values converted to the declared type
<T>generator repeat_each(Iterable<T> source, int times) -> T {
    for x in source {
        for k in ...times {
            emit x
        }
    }
}

generator halves() -> float {
    emit 1
    emit 0.5
}

io:print_nl(repeat_each(["a", "b"], 2).to_array(), repeat_each(fib().take(3), 2).to_array(), halves().to_array())

// a generator that closes over a variable of its function
function counter_from(int start) -> Generator<int> {
    let step = 10
    generator count() -> int {
        let v = start
        while true {
            emit v
            v += step
        }
    }

    return count()
}

io:print_nl(counter_from(5).take(3).to_array())

// the lazy functions read their source only as far as they are read
let pulled = 0
generator noisy() -> int {
    for i in 1...=1000 {
        pulled += 1
        emit i
    }
}

io:print_nl(noisy().map(x -> x * x).filter(x -> x % 2 == 1).take(3).to_array(), pulled)
io:print_nl(["a", "b", "c"].enumerate().to_array(), [1, 2].zip(["x", "y", "z"]).to_array())
io:print_nl([1, 2].chain(3...=4).to_array(), (0...10).skip(7).to_array(), reduce(1...=4, (a, b) -> a * b, 1))
io:print_nl(reduce([1, 2, 3], +, 0), "hey".map(c -> upper(c)).join("-"))

// for k, v: arrays and maps have keys, a generator of pairs too; anything else counts with enumerate()
for i, s in ["x", "y"] {
    io:print(i, s, "")
}

for i, s in "hé".enumerate() {
    io:print(i, s, "")
}

io:print_nl()
const Map<string, int> ages = map({ ann: 31, bob: 40 })
for name, age in ages {
    io:print(name, age, "")
}

for name in ages.keys() {
    io:print(name, "")
}

for age in ages.values() {
    io:print(age, "")
}

io:print_nl()
for i, x in set([7, 8]).enumerate() {
    io:print(i, x, "")
}

io:print_nl(set([1, 2]).map(x -> x * 3).to_array())

// a record's fields only through fields()
type Point = { int x, int y }
for name, value in fields(Point({ x: 1, y: 2 })) {
    io:print(name, value, "")
}

io:print_nl()

// user types: an Iter (only next), an Iterable (iter), a KeyIterable (pairs)
type Countdown = { int left }
function next(Countdown c) -> int? {
    if c:left == 0 {
        return none()
    }

    c:left -= 1

    return some(c:left + 1)
}

impl Iter<int> for Countdown

<T>type Tree = { T value, T[] children }
<T>generator values(Tree<T> t) -> T {
    emit t:value
    for c in t:children {
        emit c
    }
}

<T>function iter(Tree<T> t) -> Iter<T> {
    return t.values()
}

<T>impl Iterable<T> for Tree<T>

type Inventory = { string[] names, int[] counts }
generator pairs(Inventory inv) -> tuple<string, int> {
    for i, n in inv:names {
        emit tuple(n, inv:counts[i])
    }
}

impl KeyIterable<string, int> for Inventory

for n in Countdown({ left: 3 }) {
    io:print(n, "")
}

const Tree<string> tree = { value: "root", children: ["a", "b"] }
for v in tree {
    io:print(v, "")
}

for name, count in Inventory({ names: ["nails", "screws"], counts: [10, 20] }) {
    io:print(name, count, "")
}

io:print_nl()
io:print_nl(Countdown({ left: 4 }).map(n -> n * 2).to_array(), tree.enumerate().to_array())

// a value typed Iterable<T> that is an array at runtime is still iterated directly
function evens() -> Iterable<int> {
    return [0, 2, 4]
}

for e in evens() {
    io:print(e, "")
}

io:print_nl()

// a generator is used up as it is read: a second loop continues where the first stopped
const g = fib()
for x in g {
    if x > 3 {
        break
    }
}

io:print_nl(g.next(), g.next())
