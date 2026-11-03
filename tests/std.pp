import io
import std

// ---- in(T, T[]) and in(string, string)
io:print_nl(2 in [1, 2, 3], 4 in [1, 2, 3], 1 in [], "b" in ["a", "b"])
io:print_nl("ell" in "hello", "x" in "hello", "" in "hello", "hello" in "hell")

// ---- in(number, range): bounds only, the step is ignored
// exclusive end
io:print_nl(0 in 0...5, 4 in 0...5, 5 in 0...5, -1 in 0...5, 4.5 in 0...5)
// inclusive end
io:print_nl(5 in 0...=5, 5.5 in 0...=5, 5 in 5...=5, 4 in 5...=5)
// counting down: 5...1 is (1, 5], 5...=1 is [1, 5]
io:print_nl(5 in 5...1, 1 in 5...1, 1 in 5...=1, 0.5 in 5...=1, 3.5 in 5...=1)
// a step does not matter for in: 2 is not an element of range(0, 10, 3), but it is between 0 and 10
io:print_nl(2 in range(0, 10, 3), 10 in range(0, 10, 3), 0.6 in range(0, 1, 0.25), 1 in range(0, 1, 0.25))
io:print_nl(0.5 in range(1.0, 0.0, -0.25), 0 in range(1.0, 0.0, -0.25), 0 in 0...0)
// in binds looser than + and ..., tighter than == and &&
io:print_nl(1 + 1 in 0...2, 2 in 0...3 == true, !(4 in 0...4) && true)

// ---- has(number, range): is x one of the elements a for loop visits
io:print_nl(has(8, range(2, 20, 3)), has(9, range(2, 20, 3)), has(20, range(2, 20, 3)), has(2, range(2, 20, 3)))
io:print_nl(has(3, 5...=1), has(1, 5...=1), has(0, 5...=1), has(5, 0...5), has(0, 0...0))
io:print_nl(has(0.5, range(0, 1, 0.25)), has(0.6, range(0, 1, 0.25)), has(1, range(0, 1, 0.25)))
// floats: 3 * 0.1 is 0.30000000000000004, still counted as the element 0.3
io:print_nl(has(0.3, range(0, 1, 0.1)), has(0.35, range(0, 1, 0.1)), has(2.0, 0...5), has(2.5, 0...5))
io:print_nl(has(1.0 / 0.0, 0...5), has(-0.75, range(0, -2, -0.25)), has(-2, range(0, -2, -0.25)))

// ---- the operators are ordinary functions
io:print_nl(std:in(2, 0...3), std:has(3, 0...3), [0, 3, 6].map(x -> x in 0...5))
io:print_nl(in(3, 0...3), in(2, 0...3), in(2, [1, 2]), in("ell", "hello"), in(0.5, range(0, 1, 0.25)))
const check = in
io:print_nl(check("a", "cat"), !in(5, 0...5), in(1, 0...3) && in(1, [1]))

// ---- ...n starts at 0, and its end is excluded
io:print_nl(3 in ...5, 0 in ...5, 5 in ...5, -1 in ...5, 4.9 in ...5, 5 in ...=5)
if 3 in ...5 { io:print_nl("3 is in ...5") }

// ---- interfaces
type Money = { int cents }
function str(Money m) -> string { return "$" + str(m:cents / 100) + "." + str(m:cents % 100) }
function ==(Money a, Money b) -> bool { return a:cents == b:cents }
function <(Money a, Money b) -> bool { return a:cents < b:cents }
function <=(Money a, Money b) -> bool { return a:cents <= b:cents }
impl std:Str for Money
impl std:Comparator for Money

function show(std:Str s) -> string { return "<" + str(s) + ">" }
function same(std:Equals a, std:Equals b) -> bool { return a == b }

const Money a = { cents: 150 }
const Money b = { cents: 275 }
io:print_nl(show(a), show(b))
// != > >= come from the defaults; Comparator brings Equals with it
io:print_nl(a == a, a != b, a > b, b > a, a >= a, b >= a, a >= b)
io:print_nl(same(a, b), same(b, Money({ cents: 275 })))

// ---- callback types
const std:Predicate<int> even = n -> n % 2 == 0
const std:Producer<int, string> label = n -> "#" + str(n)
let int total = 0
const std:Consumer<int> add = n -> { total = total + n }
for n in 1...=4 { add(n) }
io:print_nl(even(4), even(7), label(3), total)
