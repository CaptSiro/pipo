import io

// a named function passed to a generic callback parameter binds the callback's result type
function double(int x) -> int { return x * 2 }
function label(int x) -> string { return "#" + str(x) }
function label(string s) -> string { return "'" + s + "'" }
function is_even(int x) -> bool { return x % 2 == 0 }
<T>function twice(T x) -> T[] { return [x, x] }

io:print_nl([1, 2, 3].map(double).to_array())
io:print_nl([1, 2].map(str).to_array())
io:print_nl([1, 2].map(label).to_array(), ["a"].map(label).to_array())
io:print_nl((1...=6).filter(is_even).map(double).to_array())
io:print_nl([1, 2].map(twice).to_array())
io:print_nl(type_of([1, 2].map(label)), type_of(["x"].map(twice)))
io:print_nl(reduce([1, 2, 3].map(double), +, 0))

const string[] names = [1, 2].map(label).to_array()
io:print_nl(names.join(" "))

// one user overload next to the built-in natives of the same name: each value finds its own
type Vec = { int x }
function str(Vec v) -> string { return "Vec(" + str(v:x) + ")" }
io:print_nl([Vec{ x: 1 }].map(str).to_array(), [3].map(str).to_array())

// a set passed where a non-generic callback type is expected
type Label = int -> string
function apply(int x, Label f) -> string { return f(x) }
io:print_nl(apply(5, label))
