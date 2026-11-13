// the second wasm subset: function values, scopes, unknown, dispatch, the natives the host runs
import io

// ---- lambdas and closures: captured variables live in cells
type Counter = () -> int
type IntFn = (int) -> int

function counter() -> Counter {
    let n = 0
    return () -> {
        n += 1
        return n
    }
}

const next = counter()
next()
next()
io:print_nl("counter", next())

function apply_twice(IntFn f, int x) -> int {
    return f(f(x))
}

const add = 5
io:print_nl("apply", apply_twice(x -> x + add, 1), apply_twice(x -> x * x, 3))

// a local function, recursive, capturing a parameter
function sum_to(int limit) -> int {
    function go(int i) -> int {
        if i > limit {
            return 0
        }

        return i + go(i + 1)
    }

    return go(1)
}

io:print_nl("sum_to", sum_to(10))

// closures made in a loop keep their own iteration's value
let IntFn[] adders = []
for i in 0...3 {
    adders.push(x -> x + i)
}

io:print_nl("adders", adders[0](10), adders[1](10), adders[2](10))

// a function used as a value
function triple(int x) -> int {
    return x * 3
}

io:print_nl("value", apply_twice(triple, 2))

// ---- scopes: blocks, defer, early exits
scope _times(Block<int> code, int n) -> void {
    for i in ...n {
        code(i)
    }
}

_times 3 {
    io:print("t" + str(scope), "")
}

io:print_nl()

scope _guard(Block<void> code, string name) -> void {
    defer { io:print_nl("close", name) }
    io:print_nl("open", name)
    code()
}

function find(int[] xs, int wanted) -> string {
    _guard "find" {
        for x in xs {
            if x == wanted {
                return "found " + str(x)
            }
        }
    }

    return "missing"
}

io:print_nl(find([1, 2, 3], 2))
io:print_nl(find([1, 2, 3], 7))

for i in ...4 {
    _guard "loop" + str(i) {
        if i == 1 {
            continue
        }

        if i == 2 {
            break
        }

        io:print_nl("body", i)
    }
}

scope _twice(Block<int> code, int x) -> int {
    return code(code(x))
}

io:print_nl("twice", _twice 3 { yield scope * 10 })

// a block that leaves through a nested scope call
function nested_exit() -> int {
    _guard "outer" {
        _times 5 {
            if scope == 2 {
                return scope * 100
            }
        }
    }

    return -1
}

io:print_nl("nested", nested_exit())

// ---- unknown and as
const unknown[] bag = [1, "two", 3.5, [4]]
for item in bag {
    if some(i) = item as int {
        io:print_nl("int", i + 1)
    } else if some(s) = item as string {
        io:print_nl("string", s + "!")
    } else {
        io:print_nl("other", item, get_type(item))
    }
}

io:print_nl(bag, 2 as float)

// ---- interfaces: dynamic dispatch on the value's type
interface Shape {
    function area(Shape) -> float
}

type Square = { float side }
type Circle = { float r }

function area(Square s) -> float {
    return s:side * s:side
}

function area(Circle c) -> float {
    return 3.0 * c:r * c:r
}

impl Shape for Square
impl Shape for Circle

const Shape[] shapes = [Square({ side: 2.0 }), Circle({ r: 1.0 })]
let total = 0.0
for s in shapes {
    total += s.area()
}

io:print_nl("area", total)

// ---- natives the host runs
const words = ["pear", "apple", "fig"]
io:print_nl(words.sort(), words.map(w -> #w).to_array(), words.filter(w -> #w > 3).to_array(), join(words, "-"))
io:print_nl(upper("abc"), split("a,b,c", ","), "hello"[1], slice("hello", 1, -1), parse_int("42"), parse_int("x"))
io:print_nl(format("%5.2f|%-4d|%s", 3.14159, 7, "ok"), [1, 2] == [1, 2], tuple(1, "a") != tuple(1, "b"))
let nums = [3, 1, 2]
nums[0] += 10
io:print_nl(nums, nums.pop(), nums, reduce([1, 2, 3], (a, b) -> a + b, 0))
// a range's end is the end as written: a ...= range stores it one step further
const incl = 2...=6
io:print_nl(incl:start, incl:end, incl:step, incl:inclusive, (9...=3):end, (0...4):end)
// slice by a range: ... excludes the end, ...= includes it
io:print_nl(nums.slice(0...=1), "hello".slice(1...=-2), 0...=5, 5...=1)
