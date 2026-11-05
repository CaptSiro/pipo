// The example from md/PROPOSAL.md, made runnable.

// line comment
/* block comment */

// imports
import io
import "./shapes"
import "./shapes" -> renamed
import "./shapes" -> { rect, area, describe }

// custom type definitions
// semi colons can be omitted if the code line ends, semi colons do basically nothing
type number = float || int;

// function name can be basically anything as long as it is not a keyword,
// uses type overloading for resolution
<T>function +(T[] a, T[] b) -> T[] {
    return a.concat(b)
}

<T>function -(T[] a) -> T[] {
    return reverse(a)
}

// calling functions - all produce an empty array
io:print_nl(+([], []))     // classic
io:print_nl([].+([]))      // dot-syntax: the receiver becomes the first argument
io:print_nl(-[])           // unary-syntax: the function takes exactly one argument
io:print_nl([] + [])       // binary-syntax: the function takes exactly two arguments

// precedence of newly created functions can be set at runtime
<T>function *(T[] array, int count) -> T[] {
    return array.multiply(count)
}

precedence * over +
io:print_nl(['b'] + ['a'] * 5)
// ["b", "a", "a", "a", "a", "a"]

// variables
const int answer = 42
let float ratio = 0.5
let string late
late = "assigned later"
const DEBUG                      // works like #define DEBUG
io:print_nl(answer, ratio, late, DEBUG)

if answer > 40 {
    io:print_nl("big")
} else if answer > 10 {
    io:print_nl("medium")
} else {
    io:print_nl("small")
}

for i in ...10 {
    io:print(i, "")                 // i is from 0 to 9
}
io:print_nl()

for let i in 10...=1 {
    i *= 2                       // `let` makes the loop variable mutable
    io:print(i, "")
}
io:print_nl()

const array = ["x", "y", "z"]
for let i in ...#array {
    io:print(i, "")                 // # gives the number of elements
}
io:print_nl()

for x in array {
    io:print(x, "")
}
io:print_nl()

let n = 0
while true {
    n += 1
    if n % 2 == 0 {
        continue
    }
    if n > 7 {
        break
    }
    io:print(n, "")
}
io:print_nl()

// match works like Rust's match and is an expression
const var = 5
const x = match var {
    10 -> var * 5
    5 -> {
        const doubled = var * 2
        doubled + 1
    }
    _ -> 0
}
io:print_nl("match:", x)

// optional values
function find_even(int[] xs) -> int? {
    for x in xs {
        if x % 2 == 0 {
            return some(x)
        }
    }
    return none()
}

if some(found) = find_even([1, 3, 4, 5]) {
    io:print_nl("found", found)
}
if some(found) = find_even([1, 3]) {
    io:print_nl("unreachable")
} else {
    io:print_nl("nothing even")
}

// Error<S, F = string> is declared in the prelude exactly like this:
//     enum Error<S, F = string> { success(S), fail(F) }
function parse_age(string s) -> Error<int> {
    if some(age) = parse_int(s) {
        if age < 0 {
            return fail "age cannot be negative"   // unary syntax
        }
        return success(age)
    }
    return fail("not a number: " + s)
}

for input in ["22", "-1", "abc"] {
    match parse_age(input) {
        success(age) -> {
            io:print_nl("ok!", age)
        }
        fail(reason) -> {
            io:print_nl("failed:", reason)
        }
    }
}

enum Properties {
    NAME,
    VALUE,
    DATASET,
}

enum string Messages {
    NOTICE = "Notice",
    WARNING = "Warning",
    ERROR = "Error"
}

io:print_nl(Properties:VALUE, enum_value(Properties:VALUE), enum_value(Messages:WARNING))

// JSON syntax for objects and arrays
type Person = {
    string name,
    number age,
}

const Person john = {
    name: "John Pork",
    age: 22
}

const Person[] people = [john]
for p in people {
    io:print_nl(p) // pretty JSON print
}

// binding functions to a datatype: the first parameter decides
function greet(Person p) -> string {
    return "Hello, " + p:name + "!"
}
function birthday(Person p) {
    p:age += 1
}

john.birthday()
io:print_nl(john.greet(), john:age)

// using the imported module
const r = rect(3, 4)
io:print_nl(r.describe(), "area", r.area(), shapes:UNIT == renamed:rect(1, 1))
