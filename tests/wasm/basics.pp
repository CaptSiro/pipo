import io

// numbers, strings and functions
function fib(int n) -> int {
    if n < 2 {
        return n
    }

    return fib(n - 1) + fib(n - 2)
}

function average(float[] xs) -> float {
    let total = 0.0
    for x in xs {
        total += x
    }

    return total / float(#xs)
}

io:print_nl("fib(20) =", fib(20), 7 / 2, 7 % -3, 2 ** 10, 1.5 * 4, 10.0 / 4)
io:print_nl(average([1.0, 2.0, 4.5]), "a" + "b" + str(42), "abc" < "abd", #"hello")

// loops
let sum = 0
for i in 1...=100 {
    if i % 3 == 0 {
        continue
    }

    sum += i
}

let n = 10
while n > 0 {
    n -= 3
}

io:print_nl(sum, n)
for i, word in ["zero", "one", "two"] {
    io:print(i, word, "")
}

io:print_nl()

// records, arrays, enums, match
type Person = { string name, int age }

function older(Person p, int years) -> Person {
    return { name: p:name, age: p:age + years }
}

const Person john = { name: "John Pork", age: 22 }
io:print_nl(older(john, 1), john:name)
const names = ["b", "a"]
names.push("c")
io:print_nl(names, #names, names[-1], [[1, 2], [3]])

enum Shape { circle(float), rect(float, float), none_shape() }

function area(Shape s) -> float {
    return match s {
        circle(r) -> 3.0 * r * r
        rect(w, h) -> w * h
        _ -> 0.0
    }
}

io:print_nl(area(circle(1.0)), area(rect(2.0, 3.5)), area(none_shape()), rect(1.0, 2.0))
const maybe = some(5)
if some(v) = maybe {
    io:print_nl("got", v, maybe, none())
}

const label = match 3 {
    1 -> "one"
    3 -> "three"
    _ -> "many"
}

io:print_nl(label, tuple(1, "x"), 0...3, true && !false)
