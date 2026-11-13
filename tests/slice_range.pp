import io

// slice(xs, start, end): the end is excluded; slice(xs, r): the end follows the range (... excludes, ...= includes)
const xs = [10, 20, 30, 40, 50]
io:print_nl(xs.slice(1, 3), xs.slice(1...3), xs.slice(1...=3), xs.slice(...2), xs.slice(...=2))

// negative indices count from the back, as with ints; the bounds count, not the direction
io:print_nl(xs.slice(1...=-1), xs.slice(-2...=-1), xs.slice(-3...-1), xs.slice(3...0), xs.slice(0...=99))

// strings, a typed Range<int>, a bare Range
const Range<int> r = 2...=4
const Range any = 0...2
io:print_nl("hello".slice(1...=3), "hello".slice(1...3), "hello".slice(r), xs.slice(r), xs.slice(any))
