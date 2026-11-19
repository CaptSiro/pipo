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

// the block of a scope call (the first parameter of a scope): code() / code(value) runs it with `scope`
// set to the value; it can only run while the scope call runs, and may return / break / continue / lift
native type Block<T>

// the block of a scope call that can be kept and called after the call ended; it cannot return,
// break, continue or lift out of anything
native type Callback<T>

// ---- Map<K, V> and Set<T>: built-in hash containers (moved from std)
// Keys and elements must be values that cannot change. map() / set() make empty ones; map([tuple("a", 1)]),
// map({ a: 1 }), map(other), set([1, 2, 2]), set(0...5), set(other) copy from a value. Both keep insertion order.
native type Map<K, V>
native type Set<T>

function map() -> Map
<K, V>function map(tuple<K, V>[] entries) -> Map<K, V>
function map(object fields) -> Map
<K, V>function map(Map<K, V> other) -> Map<K, V>
function set() -> Set
<T>function set(T[] items) -> Set<T>
function set(Range<int> items) -> Set<int>
function set(Range<float> items) -> Set<float>
<T>function set(Set<T> other) -> Set<T>

<K, V>function get(Map<K, V> m, K key) -> V?
<K, V>function put(Map<K, V> m, K key, V value) -> void
<K, V>function has(Map<K, V> m, K key) -> bool
<K, V>function remove(Map<K, V> m, K key) -> V?
<K, V>function keys(Map<K, V> m) -> K[]
<K, V>function values(Map<K, V> m) -> V[]
<K, V>function entries(Map<K, V> m) -> tuple<K, V>[]
<K, V>function clear(Map<K, V> m) -> void
<K, V>function in(K key, Map<K, V> m) -> bool
// add returns false when the item was already there, remove when it was not
<T>function add(Set<T> s, T item) -> bool
<T>function has(Set<T> s, T item) -> bool
<T>function remove(Set<T> s, T item) -> bool
<T>function values(Set<T> s) -> T[]
<T>function clear(Set<T> s) -> void
<T>function union(Set<T> a, Set<T> b) -> Set<T>
<T>function intersection(Set<T> a, Set<T> b) -> Set<T>
<T>function difference(Set<T> a, Set<T> b) -> Set<T>
<T>function in(T item, Set<T> s) -> bool
<T>function |(Set<T> a, Set<T> b) -> Set<T>
<T>function &(Set<T> a, Set<T> b) -> Set<T>
<T>function -(Set<T> a, Set<T> b) -> Set<T>

// what c[key] and c[key] = value call; a type that implements Index only needs get and put
<K, V>interface Index {
    function get(Index collection, K key) -> V?
    function [](Index collection, K key) -> V
    function put(Index collection, K key, V value) -> void
    function []=(Index collection, K key, V value) -> void
}

interface Length {
    function length(Length collection) -> int
    function #(Length collection) -> int
}

// ---- Range<T>: an interval of any T (md/RANGE.md). Numbers are built in (0...5, 0.5...=2, range(...));
// for anything else these make one. `in` needs < and <= of T, `for` needs impl Iterable<T> for Range<T>.
<T>function ...(T start, T end) -> Range<T>
<T>function ...=(T start, T end) -> Range<T>
<T>function in(T x, Range<T> r) -> bool
// "a"...="e": the code points from start to end
function iter(Range<string> r) -> Generator<string>

// ---- iteration (md/ITER.md)

// what a call of a `generator` function gives; next() runs it to its next `emit`
native type Generator<T>

// a collection that can give a fresh Iter, as many times as asked; `for x in c` reads one
<T>interface Iterable {
    function iter(Iterable c) -> Iter<T>
}

// a source of values that is used up as it is read; every Iter is an Iterable of itself
<T>interface Iter {
    function next(Iter it) -> T?
    function iter(Iter it) -> Iter<T> {
        return it
    }
    impl Iterable<T> for Iter
}

// a collection whose items have keys: what `for key, value in c` reads
<K, V>interface KeyIterable {
    function pairs(KeyIterable c) -> Iter<tuple<K, V>>
}

<T, R>type Mapper = T -> R
<T>type Predicate = T -> bool
<A, T>type Reducer = (A, T) -> A

// the generator's next value, or none() once it is done
<T>function next(Generator<T> g) -> T?
// a record's fields as tuple(name, value): for name, value in fields(r)
native function fields(object record) -> Generator<tuple<string, any>>
// lazy: the values of source with f applied, read only as far as they are read
<T, R>function map(Iterable<T> source, Mapper<T, R> f) -> Generator<R>
// lazy: the values of source for which keep is true
<T>function filter(Iterable<T> source, Predicate<T> keep) -> Generator<T>
// lazy: the first n values
<T>function take(Iterable<T> source, int n) -> Generator<T>
// lazy: the values after the first n
<T>function skip(Iterable<T> source, int n) -> Generator<T>
// lazy: tuple(0, first), tuple(1, second), ...
<T>function enumerate(Iterable<T> source) -> Generator<tuple<int, T>>
// lazy: tuple(a1, b1), tuple(a2, b2), ... as long as the shorter
<A, B>function zip(Iterable<A> first, Iterable<B> second) -> Generator<tuple<A, B>>
// lazy: the values of first, then of second
<T>function chain(Iterable<T> first, Iterable<T> second) -> Generator<T>
// every value, in an array
<T>function to_array(Iterable<T> source) -> T[]
// f(f(f(start, v1), v2), ...)
<A, T>function reduce(Iterable<T> source, Reducer<A, T> f, A start) -> A
// what `for` calls for a type that implements Iterable / KeyIterable
<T>function iterate(Iterable<T> source) -> Generator<T>
<K, V>function iterate_pairs(KeyIterable<K, V> source) -> Generator<tuple<K, V>>

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
// && and || are not functions: they skip their right side when the left one decides
// the length of a string (bytes), array, object, range, tuple, map or set
native function #(any collection) -> int
// exclusive range: ...5 is 0, 1, 2, 3, 4; 2...5 is 2, 3, 4
native function ...(int end) -> Range
native function ...(int start, int end) -> Range
// with a float: a float range that steps by 1.0
native function ...(number start, number end) -> Range
// inclusive range: ...=5 is 0 to 5; 2...=5 is 2 to 5
native function ...=(int end) -> Range
native function ...=(int start, int end) -> Range
native function ...=(number start, number end) -> Range
// x[i]: an array item, a byte of a string, an object field, a tuple item
native function [](any[] array, int index) -> any
native function [](string s, int index) -> string
native function [](object o, string key) -> any
// x[i] = v
native function []=(any[] array, int index, any value) -> void
native function []=(object o, string key, any value) -> void

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
// the static type: "int", "int[]", "Person", "int?", ... (x is not evaluated)
native function type_of(any value) -> string
// the type of the value: "int", "string", "array", "Person", "Option", "Map", ...
native function get_type(any value) -> string
// the value associated with an enum variant: enum string Messages { NOTICE = "Notice" }
native function enum_value(any variant) -> any

// ---- arrays

native function capacity(any[] array, int capacity) -> any[]
native function fit(any[] array) -> void
native function push(any[] array, any item) -> void
// removes and returns the last item
native function pop(any[] array) -> any?
native function insert(any[] array, int index, any item) -> void
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
// the end is excluded
native function slice(any[] array, int start, int end) -> any[]
native function slice(string s, int start) -> string
native function slice(string s, int start, int end) -> string
// the indices of r: ... excludes the end, ...= includes it; negatives count from the back
native function slice(any[] array, Range r) -> any[]
native function slice(string s, Range r) -> string
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
native function range(number start, number end, number step) -> Range
// a Map<string, unknown> checked and converted into the record type T: success(record) or fail(why)
native function decode(any map, type record) -> Error
// aborts with a runtime error
native function panic(any message) -> void
native function assert(bool condition) -> void
native function assert(bool condition, any message) -> void
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
