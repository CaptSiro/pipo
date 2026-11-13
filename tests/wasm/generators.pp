import io
// generators on the wasm target: a step function per generator, its locals in the generator's frame

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
io:print_nl(f.next(), f.next(), f.next(), f.next())
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
for a, b in grid(1, 3) {
    io:print(a + b, "")
}

io:print_nl()

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

// a generator that closes over variables of its function
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
io:print_nl([3, 4, 5].map(x -> x * 2).to_array(), [1, 2, 3, 4].filter(x -> x % 2 == 0).to_array())

// a type of the program that implements Iter: `for` reads it through iterate()
type Countdown = { int from }

function next(Countdown c) -> int? {
    if c:from <= 0 {
        return none()
    }

    c:from -= 1
    return some(c:from + 1)
}

impl Iter<int> for Countdown

for n in Countdown({ from: 3 }) {
    io:print(n, "")
}

io:print_nl(Countdown({ from: 4 }).map(x -> x * 10).to_array())

// a generator kept as a value: interleaved readers
const g = fib()
const h = fib()
io:print_nl(g.next(), g.next(), h.next(), g.next(), h.next())

// a parameter a lambda captures lives in a cell; scope calls and nested generator loops inside a generator
type IntFn = int -> int
generator scaled(int k, int[] xs) -> int {
    const IntFn by = x -> x * k
    for x in xs {
        emit by(x)
        k += 1
    }
}
io:print_nl(scaled(2, [1, 1, 1]).to_array())

scope _twice(Block<int> code) -> int {
    return code(1) + code(2)
}

generator scoped() -> int {
    let total = 0
    for i in ...3 {
        total += _twice {
            yield scope * 10 + i
        }
        emit total
    }
}
io:print_nl(scoped().to_array())

generator outer() -> int {
    for x in scaled(1, [5, 6]) {
        for y in scoped() {
            if y > 40 {
                break
            }
            emit x + y
        }
    }
}
io:print_nl(outer().to_array())

// strings are iterated by character
io:print_nl("abc".map(c -> c + c).to_array())
