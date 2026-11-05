// Internal functions of the PiPo interpreter (src/builtins.c, src/prelude.c).
// This file is a stub bundled with the IDE plugin: it only describes the
// signatures, so names resolve and documentation shows. It is never run.

// ---- types from the prelude

// any number: an int or a float
type number = int || float

// T? is sugar for Option<T>
enum Option<T> {
    some(T),
    none()
}

// the result of an operation that can fail: success(value) or fail(reason)
enum Error<S, F = string> {
    success(S),
    fail(F)
}

// ---- operators

// addition of numbers, concatenation of strings
native function +(number a, number b) -> number
native function +(string a, string b) -> string
// subtraction; with one operand, negation
native function -(number a, number b) -> number
native function -(number a) -> number
// multiplication; string * int repeats the string
native function *(number a, number b) -> number
native function *(string s, int n) -> string
native function /(number a, number b) -> number
// remainder
native function %(number a, number b) -> number
// power
native function **(number a, number b) -> number
// deep equality
native function ==(any a, any b) -> bool
native function !=(any a, any b) -> bool
// compares numbers or strings
native function <(any a, any b) -> bool
native function >(any a, any b) -> bool
native function <=(any a, any b) -> bool
native function >=(any a, any b) -> bool
native function !(bool a) -> bool
native function &&(bool a, bool b) -> bool
native function ||(bool a, bool b) -> bool
// the length of a string (bytes), array, object, range, tuple, map or set
native function #(any collection) -> int
// exclusive range: ...5 is 0, 1, 2, 3, 4; 2...5 is 2, 3, 4
native function ...(int end) -> range
native function ...(int start, int end) -> range
// inclusive range: ...=5 is 0 to 5; 2...=5 is 2 to 5
native function ...=(int end) -> range
native function ...=(int start, int end) -> range
// x[i]: an array item, a byte of a string, an object field, a tuple item
native function [](any[] array, int index) -> any
native function [](string s, int index) -> string
native function [](object o, string key) -> any
// x[i] = v
native function []=(any[] array, int index, any value)
native function []=(object o, string key, any value)

// ---- conversion and reflection

// the string form of a value (the same as io:print uses)
native function str(any value) -> string
// from a float (truncates), bool, numeric string or int-valued enum; an error otherwise
native function int(any value) -> int
// from an int or a numeric string
native function float(any value) -> float
// none() instead of an error when s is not a number
native function parse_int(string s) -> int?
native function parse_float(string s) -> float?
// "int", "string", "array", "Person", "Option", ...
native function type_of(any value) -> string
// the value associated with an enum variant: enum string Messages { NOTICE = "Notice" }
native function enum_value(any variant) -> any

// ---- arrays

native function push(any[] array, any item)
// removes and returns the last item
native function pop(any[] array) -> any?
native function insert(any[] array, int index, any item)
// removes the item at index and returns it
native function remove(any[] array, int index) -> any
// a new array: a followed by b
native function concat(any[] a, any[] b) -> any[]
native function concat(string a, string b) -> string
// a new array: the items of array, count times
native function multiply(any[] array, int count) -> any[]
native function repeat(any[] array, int count) -> any[]
native function repeat(string s, int count) -> string
// a new array / string from start up to (not including) end
native function slice(any[] array, int start) -> any[]
native function slice(any[] array, int start, int end) -> any[]
native function slice(string s, int start) -> string
native function slice(string s, int start, int end) -> string
native function contains(any[] array, any item) -> bool
native function contains(string s, string sub) -> bool
native function index_of(any[] array, any item) -> int?
native function index_of(string s, string sub) -> int?
// a new array / string in reverse order
native function reverse(any[] array) -> any[]
native function reverse(string s) -> string
// a new sorted array; compare returns a number (negative: a first) or a bool (true: a first)
native function sort(any[] array) -> any[]
native function sort(any[] array, function compare) -> any[]
native function join(any[] array, string separator) -> string
// a new array with f applied to every item
native function map(any[] array, function f) -> any[]
// a new array with the items for which predicate returns true
native function filter(any[] array, function predicate) -> any[]
native function reduce(any[] array, function f, any initial) -> any
// a shallow copy of an array, object, map or set; a tuple is returned unchanged
native function copy(any value) -> any
// a tuple of the arguments: tuple(1, "a") is a tuple<int, string>
native function tuple(any first, ...) -> any

// ---- objects

native function keys(object o) -> string[]
native function values(object o) -> any[]
native function has(object o, string key) -> bool
native function remove_key(object o, string key) -> bool

// ---- strings

native function split(string s, string separator) -> string[]
native function upper(string s) -> string
native function lower(string s) -> string
native function trim(string s) -> string
native function replace(string s, string from, string to) -> string
native function starts_with(string s, string prefix) -> bool
native function ends_with(string s, string suffix) -> bool
// the code points of s, each as a string
native function chars(string s) -> string[]
// the code point of the first character
native function ord(string s) -> int
// the character of a code point
native function chr(int code) -> string
// formats like C printf: format("%d + %.2f = %s", 1, 2.5, "x")
// conversions: d i (int), u o x X (unsigned), f F e E g G a A (number), c (code point), s (any), %%
native function format(string format, ...) -> string

// ---- math (the rest is in the math package)

native function min(any a, any b) -> any
native function max(any a, any b) -> any
native function clamp(number x, any a, any b) -> any

// ---- misc

// a range with a custom step; any float makes a float range
native function range(number start, number end, number step) -> range
// aborts with a runtime error
native function panic(any message)
native function assert(bool condition)
native function assert(bool condition, any message)
// monotonic seconds, for timing
native function clock() -> float
// is a name visible from the current scope
native function defined(string name) -> bool
// calls f(args...), so cb.call(x) works
native function call(function f, ...) -> any

// ---- Option / Error helpers (written in PiPo in src/prelude.c)

// true for some(...), false for none()
<T>function is_some(T? o) -> bool {
    return match o { some(_) -> true, _ -> false }
}

// true for none()
<T>function is_none(T? o) -> bool {
    return match o { none() -> true, _ -> false }
}

// the value inside some(...); a runtime error for none()
<T>function unwrap(T? o) -> T {
    match o {
        some(x) -> { return x }
        none() -> { panic("called unwrap() on none()") }
    }
}

// the value inside some(...), or fallback for none()
<T>function unwrap_or(T? o, T fallback) -> T {
    match o {
        some(x) -> { return x }
        none() -> { return fallback }
    }
}

// true for success(...)
<S, F>function is_success(Error<S, F> r) -> bool {
    return match r { success(_) -> true, _ -> false }
}

// true for fail(...)
<S, F>function is_fail(Error<S, F> r) -> bool {
    return match r { fail(_) -> true, _ -> false }
}

// the value inside success(...); a runtime error for fail(...)
<S, F>function unwrap(Error<S, F> r) -> S {
    match r {
        success(x) -> { return x }
        fail(reason) -> { panic("called unwrap() on fail(" + str(reason) + ")") }
    }
}

// the value inside success(...), or fallback for fail(...)
<S, F>function unwrap_or(Error<S, F> r, S fallback) -> S {
    match r {
        success(x) -> { return x }
        fail(_) -> { return fallback }
    }
}

// the value inside some(...); none() is returned from the calling function
native <T>function lift(T? o) -> T

// the value inside success(...); fail(...) is returned from the calling function
native <S, F>function lift(Error<S, F> r) -> S
