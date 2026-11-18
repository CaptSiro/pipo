// `type X = T` over a non-record, non-union type is a tagged alias: the same value as T, seen as X.
// Overloads for X win over the ones for T; T is used where X has none.
import io
import std

type Alias = int
function alias(int x) -> Alias {
    return x
}

function str(Alias a) -> string {
    return "Alias:" + str(a + 0)
}

impl std:Str for Alias

io:print_nl(5.alias().str())        // Alias:5
io:print_nl(str(5))                 // 5: a plain int stays an int
const Alias a = 7
io:print_nl(str(a))                 // Alias:7
const int plain = a
io:print_nl(str(plain))             // 7: typed as int again
io:print_nl(str(a + 1))             // 8: arithmetic gives an int
const b = a
io:print_nl(str(b))                 // Alias:7: inferred, keeps the tag

// chains: C is a B is an A is an int
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
io:print_nl(name(intabc))           // B: C has no overload, B is the closest
const A asA = intabc
io:print_nl(name(asA))              // A
io:print_nl(name(3))                // int

// through a union the tag goes along and the overload is picked at runtime
function show(A || string u) -> string {
    return name(u)
}

io:print_nl(show(intabc))           // B
io:print_nl(show(asA))              // A
io:print_nl(show("x"))              // string
const (C || string)[] mixed = [intabc, "y"]
io:print_nl(name(mixed[0]) + " " + name(mixed[1]))   // B string

// through an interface
function describe(std:Str s) -> string {
    return s.str()
}

io:print_nl(describe(a))            // Alias:7

// generics keep the tag
<T>function same(T x) -> T {
    return x
}

io:print_nl(name(same(intabc)))     // B
io:print_nl(name(same(asA)))        // A

// branches of different tags give a union (C || A): the overload follows the value's own tag
const flag = plain > 0
const joined = if flag { intabc } else { asA }
io:print_nl(name(joined))           // B: the value is the C
io:print_nl(type_of(joined))        // C || A
const based = if flag { asA } else { 4 }
io:print_nl(name(based))            // A

// siblings over one base: each keeps its own overloads
type Meters = float
type Seconds = float
function unit(Meters m) -> string { return "m" }
function unit(Seconds s) -> string { return "s" }
function unit(float f) -> string { return "float" }
const Meters d = 3.0
const Seconds t = 2.0
io:print_nl(unit(d) + " " + unit(t) + " " + unit(1.5))   // m s float
io:print_nl(str(d + t))             // 5.0: float + float

// the elements of an A[] are A's
const A[] as_ = [1, 2]
io:print_nl(name(as_[0]))           // A
for x in as_ {
    io:print_nl(name(x))            // A
}

// arrays, maps and options can be tagged too
type Path = string[]
function str(Path p) -> string {
    return p.join("/")
}

const Path p = ["usr", "lib"]
io:print_nl(str(p))                 // usr/lib
io:print_nl(str(#p))                // 2

type Scores = Map<string, int>
function total(Scores s) -> int {
    let sum = 0
    for k, v in s {
        sum += v
    }

    return sum
}

const Scores sc = map({ a: 1, b: 2 })
io:print_nl(str(total(sc)))         // 3

type MaybeName = string?
function greet(MaybeName n) -> string {
    return match n { some(s) -> "hi " + s, none -> "hi you" }
}

io:print_nl(greet(some("ann")))     // hi ann
io:print_nl(greet(none()))          // hi you

// `as` opens an unknown as a tagged alias
const unknown u = intabc
const maybe = u as A
io:print_nl(name(maybe.unwrap()))   // A
