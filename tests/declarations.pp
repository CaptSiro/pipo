import io

// a declaration states a function's signature; the definition follows in the same block
// (this pair is only a syntax showcase, it is never called)
function a(int) -> float
function b(int x) -> float {
    a(x)
}
function a(int y) -> float {
    b(y)
}

// mutual recursion reads in order
function is_odd(int) -> bool
function is_even(int n) -> bool {
    if n == 0 { return true }
    return is_odd(n - 1)
}
function is_odd(int n) -> bool {
    if n == 0 { return false }
    return is_even(n - 1)
}
io:print(is_even(10), is_odd(10))

// an impl checked before the definition is written: the declaration counts
interface Named {
    function name(Named) -> string
}
type Dog = { string called }
function name(Dog) -> string
impl Named for Dog
function name(Dog d) -> string { return "dog " + d:called }
function greet(Named n) -> string { return "hi " + n.name() }
io:print(greet(Dog{ called: "Rex" }))

// a declared function can be passed around before it is defined;
// the value gets the body once the definition has run
type Scale = int -> int
function twice(int) -> int
const Scale f = twice
function twice(int x) -> int { return x * 2 }
io:print(f(21))

// inside a function body too, names of the parameters may differ
function outer(int n) -> int {
    function step(int, int) -> int
    function run(int i) -> int { return step(i, 0) }
    function step(int i, int acc) -> int {
        if i == 0 { return acc }
        return step(i - 1, acc + i)
    }
    return run(n)
}
io:print(outer(4))
