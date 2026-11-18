// tagged aliases on the wasm target: overloads by tag, through a union (boxed with the tag) and siblings
import io

type A = int
type B = A
type C = B

function c(int x) -> C {
    return x
}

function name(A x) -> string { return "A" }
function name(B x) -> string { return "B" }
function name(int x) -> string { return "int" }
function name(string s) -> string { return "string" }
function name(A || string x) -> string { return "other" }

const intabc = c(5)
const A asA = intabc
io:print_nl(name(intabc))
io:print_nl(name(asA))
io:print_nl(name(3))

function show(A || string u) -> string {
    return name(u)
}

io:print_nl(show(intabc))
io:print_nl(show(asA))
io:print_nl(show("x"))
io:print_nl(show(7))

type Meters = float
type Seconds = float
function unit(Meters m) -> string { return "m" }
function unit(Seconds s) -> string { return "s" }
const Meters d = 3.0
const Seconds t = 2.0
io:print_nl(unit(d) + " " + unit(t))
