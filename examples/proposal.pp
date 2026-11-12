// The example from md/PROPOSAL.md, made runnable.

// line comment
/* block comment */

// imports
import io
import fs
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
io:print(+([], []))     // classic
io:print([].+([]))      // dot-syntax: the receiver becomes the first argument
io:print(-[])           // unary-syntax: the function takes exactly one argument
io:print([] + [])       // binary-syntax: the function takes exactly two arguments

// precedence of newly created functions can be set at runtime
<T>function *(T[] array, int count) -> T[] {
    return array.multiply(count)
}

precedence * over +
io:print(['b'] + ['a'] * 5)
// ["b", "a", "a", "a", "a", "a"]

// variables
const int answer = 42
let float ratio = 0.5
let string late
late = "assigned later"
const DEBUG                      // works like #define DEBUG
io:print(answer, ratio, late, DEBUG)

if answer > 40 {
    io:print("big")
} else if answer > 10 {
    io:print("medium")
} else {
    io:print("small")
}

for i in ...10 {
    io:print(i, "")                 // i is from 0 to 9
}
io:print()

for let i in 10...=1 {
    i *= 2                       // `let` makes the loop variable mutable
    io:print(i, "")
}
io:print()

const array = ["x", "y", "z"]
for let i in ...#array {
    io:print(i, "")                 // # gives the number of elements
}
io:print()

for x in array {
    io:print(x, "")
}
io:print()

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
io:print()

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
io:print("match:", x)

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
    io:print("found", found)
}
if some(found) = find_even([1, 3]) {
    io:print("unreachable")
} else {
    io:print("nothing even")
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
            io:print("ok!", age)
        }
        fail(reason) -> {
            io:print("failed:", reason)
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

io:print(Properties:VALUE, enum_value(Properties:VALUE), enum_value(Messages:WARNING))

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
    io:print(p) // pretty JSON print
}

// binding functions to a datatype: the first parameter decides
function greet(Person p) -> string {
    return "Hello, " + p:name + "!"
}
function birthday(Person p) {
    p:age += 1
}

john.birthday()
io:print(john.greet(), john:age)

// using the imported module
const r = rect(3, 4)
io:print(r.describe(), "area", r.area(), shapes:UNIT == renamed:rect(1, 1))

// scopes: functions that take a block of code. Their names start with '_' (and only theirs),
// the first parameter is the block: Block<T> (runs only during the call) or Callback<T> (can be kept)
interface Resource {
    function close(Resource) -> void
}

impl Resource for fs:InputStream
impl Resource for fs:OutputStream

scope _resource(Block<void> code, Resource r) {
    defer { r.close() } // the cleanup: runs when the scope ends, also when the block leaves early
    code()
}

const tmp = fs:file("./proposal.tmp")

// `const name = value` binds name for the block only, like `if some(x) = ...`
if success(output) = tmp.open_write() {
    _resource const out = output {
        out.write("first line\nsecond line\n")
    }
}

// return / break / continue / lift in the block act on the code around the scope call
function head(fs:File f, int length) -> Error<string> {
    _resource const input = lift(f.open_read()) {
        return success(input.read_text(length)) // input is closed before head returns
    }

    return fail("unreachable")
}

io:print(head(tmp, 10))
tmp.delete()

// the block is called with a value: `scope` (here the thread id of an omp-like parallel for)
scope _parallel(Block<int> code, int threads) {
    for id in ...threads {
        code(id)
    }
}

_parallel 2 {
    // nested blocks have their own `scope`, store the outer one in a variable
    const outer = scope
    _parallel 2 {
        io:print("thread", outer, scope)
    }
}

// `scope` is a plain value, so it can be lifted out of the block
scope _with(Block<string> code, string value) {
    code(value)
}

let string lifted
_with "string" {
    lifted = scope
}
io:print(lifted)

// a Callback outlives the scope call (it cannot return / break / continue out of anything)
let Callback<string>[] listeners = []
scope _on(Callback<string> code, string event) {
    listeners.push(code)
}

_on "click" {
    io:print("clicked:", scope)
}

for listener in listeners {
    listener("button")
}

