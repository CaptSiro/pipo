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
| `#`         | length of a string (bytes), array, object or range  |
| `...`       | `(int end)`, `(int start, int end)` exclusive range |
| `...=`      | `(int end)`, `(int start, int end)` inclusive range |

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
| `type_of(x) -> string`                             | `int`, `string`, `array`, `Person`, `Option`, ...                                 |
| `enum_value(v)`                                    | value associated with an enum variant                                             |

## Arrays

`push(a, x)`, `pop(a) -> T?`, `insert(a, i, x)`, `remove(a, i) -> item`,
`concat(a, b)`, `multiply(a, n)` / `repeat(a, n)`, `slice(a, start[, end])`,
`contains(a, x)`, `index_of(a, x) -> int?`, `reverse(a)`, `sort(a)`,
`sort(a, compare)`, `join(a, sep)`, `map(a, f)`, `filter(a, f)`,
`reduce(a, f, initial)`, `copy(x)`.

`sort`, `reverse`, `concat`, `slice`, `map` and `filter` return new arrays.
A `sort` comparator returns a number (negative means a comes first) or a bool
(true means a comes first).

## Objects

`keys(o) -> string[]`, `values(o)`, `has(o, key)`, `remove_key(o, key)`.

## Strings

`split(s, sep)`, `upper(s)`, `lower(s)`, `trim(s)`, `replace(s, from, to)`,
`starts_with(s, p)`, `ends_with(s, p)`, `chars(s)` (code points), `ord(s)`,
`chr(code)`, `concat(a, b)`, `repeat(s, n)`, `slice(s, start[, end])`,
`contains(s, sub)`, `index_of(s, sub) -> int?`, `reverse(s)`.

## Math

`abs`, `min`, `max`, `floor`, `ceil`, `round` (these three return `int`),
`sqrt`, `pow`. Anything beyond this belongs in a future `math` package.

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

## Misc

| function                  |                                          |
|---------------------------|------------------------------------------|
| `range(start, end, step)` | range with a custom step                 |
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
