import io
import std

// ---- type_of is the static type, get_type the type of the value underneath
const unknown u = 42
io:print_nl(type_of(u), get_type(u), type_of(1.5), get_type([1, 2]))
const number n = 3
io:print_nl(type_of(n), get_type(n), type_of([[1], [2, 3]]), type_of({ a: 1, b: "x" }))
io:print_nl(type_of(some(1)), type_of(tuple(1, "a")), type_of(0...3), type_of(range(0, 1, 0.5)))

// ---- unknown holds anything; `as` gets a typed value back out (an Option)
function describe(unknown value) -> string {
    if some(i) = value as int {
        return "int " + str(i + 1)
    }

    if some(s) = value as string {
        return "string of " + str(#s)
    }

    return "something else: " + get_type(value)
}

io:print_nl(describe(41), describe("abc"), describe([1]))
io:print_nl(3 as float, "x" as int, n as int)

const unknown[] bag = [1, "two", 3.0]
let int ints = 0
for item in bag {
    if some(i) = item as int {
        ints += i
    }
}

io:print_nl(ints, #bag)

// ---- an object is a record whose fields are unknown values; it can take new fields
let object o = { a: 1 }
o:b = "two"
io:print_nl((o:a as int).unwrap() + 1, o:b, keys(o))

// ---- empty literals take their type from their first use
let xs = []
xs.push(1.5)
let m = std:map()
m.put("a", 1)
io:print_nl(type_of(xs), type_of(m), m["a"] + 1)

// ---- range<int>: a range that counts in ints
function first_two(range<int> r) -> int[] {
    return [r:start, r:start + r:step]
}

io:print_nl(first_two(0...10), first_two(range(5, 0, -2)))

// ---- generic functions are checked per instantiation
<T>function twice(T x) -> T[] {
    return [x, x]
}

io:print_nl(twice(1), twice("a"), type_of(twice(1)), type_of(twice(true)))
