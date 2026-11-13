# Internal Functions

These are implemented in C (`src/builtins.c`) and live in the root scope.
All of them are overload sets, so user code can add overloads, for example
`function str(Vec v) -> string { ... }`.

Almost all of them are written in C. A few small helpers are written in the
language itself instead: `src/prelude.c` holds a short program that runs
before yours and defines them. For you they behave exactly like the others.

## Operators

| name        | signatures                                          |
|-------------|-----------------------------------------------------|
| `+`         | `(number, number)`, `(string, string)`              |
| `-`         | `(number, number)`, `(number)` unary                |
| `*`         | `(number, number)`, `(string s, int n)` repeat      |
| `/ % **`    | `(number, number)`                                  |
| `== !=`     | `(any, any)` deep equality                          |
| `< > <= >=` | numbers or strings                                  |
| `!`         | `(bool)`                                            |
| `#`         | length of a string (bytes), array, object, range of numbers, tuple, map or set |
| `...`       | `(int end)`, `(int start, int end)`, `(number start, number end)` exclusive range; `<T>(T start, T end) -> Range<T>` for anything else |
| `...=`      | the same, inclusive |
| `[]`        | `x[i]`: `(any[] array, int index)`, `(string s, int index)`, `(object o, string key)`, tuples |
| `[]=`       | `x[i] = v`: `(any[] array, int index, any value)`, `(object o, string key, any value)` |

## Input / output

Not global. Printing and reading come from the `io` module
(`import io`, then `io:print(...)`). See [PACKAGES.md](PACKAGES.md#the-io-module).

## Conversion and reflection

| function                                           |                                                                                   |
|----------------------------------------------------|-----------------------------------------------------------------------------------|
| `str(x)`                                           | string form (the same as `print` uses)                                            |
| `int(x)`                                           | from float (truncates), bool, numeric string or int-valued enum. Errors otherwise |
| `float(x)`                                         | from int or numeric string                                                        |
| `parse_int(s) -> int?`, `parse_float(s) -> float?` | `none()` instead of an error                                                      |
| `type_of(x) -> string`                             | the **static** type: `int`, `int[]`, `Person`, `int?`, `unknown`, ... (known when checked; `x` is not evaluated) |
| `get_type(x) -> string`                            | the type of the **value**: `int`, `string`, `array`, `Person`, `Option`, `Map`, ... |
| `enum_value(v)`                                    | value associated with an enum variant                                             |

## Arrays

`push(a, x)`, `pop(a) -> T?`, `insert(a, i, x)`, `remove(a, i) -> item`,
`concat(a, b)`, `multiply(a, n)` / `repeat(a, n)`, `slice(a, start[, end])` (end excluded), `slice(a, r)`,
`contains(a, x)`, `index_of(a, x) -> int?`, `reverse(a)`, `sort(a)`,
`sort(a, compare)`, `join(a, sep)`, `copy(x)`.

`tuple(first, ...)` builds a tuple (`tuple<int, string>` for `tuple(1, "a")`); see
[LANGUAGE.md](LANGUAGE.md#tuples). `copy` also copies maps and sets, and returns a
tuple unchanged. `Map` and `Set`, their constructors `map(...)` / `set(...)` and the `Index` /
`Length` interfaces are built in (declared in the prelude, C bodies in
`src/modules/std.c`); see [LANGUAGE.md](LANGUAGE.md#maps-and-sets).

`sort`, `reverse`, `concat` and `slice` return new arrays.

`slice(a, start, end)` excludes `end`. `slice(a, r)` takes a range of int indices
and follows it: `xs.slice(1...3)` is items 1 and 2, `xs.slice(1...=3)` items 1 to 3.
Negative indices count from the back in both (`xs.slice(1...=-1)` drops the first
item, `"hello".slice(-3...=-1)` is `"llo"`). The bounds count, not the direction
(`xs.slice(3...0)` is empty, like `slice(xs, 3, 0)`), and a range with a step
other than 1 (`range(0, 4, 2)`) is an error.
A `sort` comparator returns a number (negative means a comes first) or a bool
(true means a comes first).

## Iteration

Written in the prelude, for any `Iterable<T>` (arrays, ranges, strings, sets, maps
through `pairs()`, generators, and user types that implement it); see
[ITER.md](ITER.md).

| function | gives |
|----------|-------|
| `map(c, f)`, `filter(c, keep)` | a `Generator` (lazy) |
| `take(c, n)`, `skip(c, n)`, `chain(a, b)` | a `Generator` (lazy) |
| `enumerate(c)` | a `Generator<tuple<int, T>>` (`for i, x in c.enumerate()`) |
| `zip(a, b)` | a `Generator<tuple<A, B>>`, as long as the shorter |
| `to_array(c)` | `T[]`: reads everything |
| `reduce(c, f, start)` | the accumulated value (`reduce(xs, +, 0)`) |
| `join(g, sep)` | a generator's values joined like an array's |
| `next(g) -> T?` | a generator's next value |
| `iter(c)`, `pairs(c)` | an `Iter` over the values / the `tuple(key, value)` pairs |
| `fields(record)` | `Generator<tuple<string, V>>`: a record's fields (`for name, value in fields(r)`) |

`map` and `filter` are lazy: `xs.map(f)` is a generator, and nothing is computed
until it is read. `xs.map(f).to_array()` gives the array.

## Objects

`keys(o) -> string[]`, `values(o)`, `has(o, key)`, `remove_key(o, key)`.

## Strings

`split(s, sep)`, `upper(s)`, `lower(s)`, `trim(s)`, `replace(s, from, to)`,
`starts_with(s, p)`, `ends_with(s, p)`, `chars(s)` (code points), `ord(s)`,
`chr(code)`, `concat(a, b)`, `repeat(s, n)`, `slice(s, start[, end])`, `slice(s, r)`,
`contains(s, sub)`, `index_of(s, sub) -> int?`, `reverse(s)`.

`format(fmt, ...) -> string` formats like C `printf`: `format("%d + %.2f = %s", 1, 2.5, "x")`.
Specs are `%[flags][width][.precision][length]conversion`, and each one is
checked against its argument:

| conversion          | argument                                                      |
|---------------------|---------------------------------------------------------------|
| `d i`               | int                                                           |
| `u o x X`           | int, printed as unsigned 64-bit (`%u` of `-1` is 2^64 - 1)    |
| `f F e E g G a A`   | float or int                                                  |
| `c`                 | int code point (UTF-8 encoded) or a 1-character string        |
| `s`                 | any value, printed like `str()`; precision cuts to that many bytes |
| `%%`                | a literal `%`                                                 |

Flags are `- + space 0 #`. Width and precision can be `*`, taken from the next
(int) argument. Length modifiers (`l`, `ll`, `h`, ...) are accepted and ignored.
A wrong argument type, too few or too many arguments, `%n`, `%p` or an unknown
conversion is a runtime error.

## Math

`min`, `max` and `clamp` are internal. Everything else is in the `math`
package (`lib/math.pp`, `src/modules/math.c`), used as `import math` and
`math:sqrt(2)`:

* constants `pi`, `e`, `sqrt2`
* rounding: `floor`, `ceil`, `round`, `trunc` (all return `int`), `abs`, `sign`,
  `fround` / `f16round` (nearest 32-bit / 16-bit float)
* `sqrt`, `cbrt`, `pow`, `exp`, `expm1`, `log` (natural), `log1p`, `log2`, `log10`
* `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2(y, x)` (radians) and the
  hyperbolic `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`
* `hypot(x, y)`, `hypot(x, y, z)`, `hypot(number[])`; `sum_precise(number[])`,
  a sum rounded only once
* `clz32`, `imul`: 32-bit int helpers, as in JavaScript
* `random()`: a float in `[0, 1)`; `random_int(end)`, `random_int(start, end)`:
  an int in `[0, end)` / `[start, end)`. Both use one global generator, seeded
  differently on every run.
* `seed(int) -> Random`: a generator of its own, giving the same numbers for
  the same seed; `r.value()` (a float in `[0, 1)`), `r.interval(end)`,
  `r.interval(start, end)`, `r.interval_float(end)`, `r.interval_float(start, end)`,
  `r.item(array)`, `r.seed(int)` (start over), `copy(r)` (a second
  generator that continues like `r`)

Outside a function's domain the result is NaN or an infinity, as in C.

## Option / Error

`some(x)`, `none()`, `success(x)`, `fail(x)` create the values.
The helpers below work on both `Option` (`T?`) and `Error` values. They are
the ones written in the language itself (in `src/prelude.c`).

| function                 | returns                                                                  |
|--------------------------|--------------------------------------------------------------------------|
| `is_some(x)`             | `true` for `some(...)`, `false` for `none()`                             |
| `is_none(x)`             | `true` for `none()`                                                      |
| `is_success(x)`          | `true` for `success(...)`                                                |
| `is_fail(x)`             | `true` for `fail(...)`                                                   |
| `unwrap(x)`              | the value inside `some(...)` / `success(...)`; a runtime error otherwise |
| `unwrap_or(x, fallback)` | the value inside, or `fallback` for `none()` / `fail(...)`               |

```
parse_int("42").unwrap()        // 42
parse_int("abc").unwrap_or(0)   // 0
```

`lift(x)` (native) returns the value inside `some(...)` / `success(...)`. On
`none()` / `fail(...)` it **returns that value from the function that called
`lift`**, like `return x` written in its place, so the function's return type
must accept it (an `Option` for `none()`, an `Error` for `fail(...)`).

```
function get_stock(string file) -> Error<Item[]> {
    const text = fs:file(file).read_text().lift()   // fail(..) is returned from get_stock
    ...
}
```

`lift` must be called directly in the body of a function (or lambda, which it
returns from). Calling it at the top level of a file, or passing it as a value
(`xs.map(lift).to_array()`), is a runtime error once it meets `none()` / `fail(...)`.

## Misc

| function                  |                                          |
|---------------------------|------------------------------------------|
| `range(start, end, step)` | range with a custom step; numbers, any float makes a float range |
| `decode(m, T)` | a `Map<string, unknown>` checked and converted into the record type `T`: `success(record)` or `fail(why)` ([LANGUAGE.md](LANGUAGE.md#record-literals)) |
| `panic(message)`          | aborts with a runtime error              |
| `assert(cond[, message])` |                                          |
| `clock() -> float`        | monotonic seconds, for timing            |
| `defined(name) -> bool`   | is a name visible from the current scope |
| `call(f, args...)`        | calls `f(args...)`, so `cb.call(x)` works |

## Adding an internal function

```c
static Value n_double(VM* vm, int argc, Value* args) {
    return int_val(args[0].as.i * 2);
}
/* in builtins_register(): */
def(vm, "double", "(int x) -> int", n_double);
```

The signature string uses the language's own syntax, generics included
(`"<T>(T[] xs) -> T"`). Overload resolution checks the arguments before the C
function runs. `"(...)"` declares a variadic function. A native that calls
back into user code (`vm_call`) must push any objects it allocated onto the
value stack (`vm_push`), because a callback can trigger garbage collection.
