import io

// callback types are named types
type ToInt = float -> int
const ToInt cb = f -> int(f)
const ToInt using_builtin = int
function to_int_custom(float f) -> int {
    return int(f)
}
const ToInt using_function = to_int_custom
io:print(cb(3.7), using_builtin(2.5), using_function(-1.5))

// a lambda takes the parameter and return types of its callback type,
// so cb behaves like (float f) -> int: an int argument is widened to float
io:print(cb(2))

// calling: cb(x), or cb.call(x)
io:print(cb.call(9.99), call(using_function, 1.5))

type Pair = (int, int) -> int
const Pair add = (a, b) -> a + b
const Pair mul = (int a, int b) -> {
    const product = a * b
    product            // a block body gives its last expression, like a match arm
}
const Pair early = (a, b) -> {
    if a > b { return a }
    b
}
io:print(add(2, 3), mul(4, 5), early(7, 1), early(1, 7))

type Thunk = () -> string
const Thunk hello = () -> "hello"
io:print(hello())

type Logger = (...) -> void
const Logger log = io:print
log("logged", 1, 2)

// generic callback types
<T>type Consumer = T -> void
<T>function for_each(T[] array, Consumer<T> consumer) -> void {
    for a in array {
        consumer(a)
    }
}
[1, 2, 3].for_each(io:print)
[1, 2, 3].for_each(x -> io:print(x * 10))
[1, 2, 3].for_each((x) -> io:print(x + 100))
[1, 2, 3].for_each((x) -> {
    io:print("block", x)
})

type Mapper<A, B> = A -> B
<A, B>function map_all(A[] xs, Mapper<A, B> f) -> B[] {
    let B[] out = []
    for x in xs {
        out.push(f(x))
    }
    return out
}
io:print(map_all([1, 2], x -> str(x) + "!"))

// overloads are chosen by the shape of the callback
function kind(ToInt f) -> string { return "ToInt" }
function kind(Thunk f) -> string { return "Thunk" }
io:print(kind(() -> "x"), kind(to_int_custom), kind(x -> 1))

// closures capture variables
let total = 0
type Step = int -> void
const Step bump = n -> { total += n }
bump(5)
bump(2)
io:print(total)

// callbacks in records, built-ins taking functions
type Button = { string label, Thunk on_click }
const Button b = { label: "ok", on_click: () -> "clicked " + "ok" }
io:print(b:on_click(), [3, 1, 2].sort((a, b) -> a < b), [1, 2, 3].map(x -> x * x))
io:print(type_of(cb), cb == cb)

// `->` in a match arm still separates the pattern from the body
const n = 3
io:print(match n {
    x if x > 2 -> "big"
    x -> "small"
})
