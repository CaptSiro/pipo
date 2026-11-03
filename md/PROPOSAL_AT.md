# Proposal: replace `x[i]` with the `@` operator

**Status:** superseded by [PROPOSAL_SYNTAX.md](PROPOSAL_SYNTAX.md), which keeps `x[i]` but
moves its meaning into `.pp` overloads of `[]` / `[]=`. `@` remains possible as an
ordinary user-defined operator. Kept for the analysis of precedence and alternatives.

## Summary

Drop the indexing syntax `x[i]` / `x[i] = v` and use an ordinary, overloadable
binary operator `@` instead. Read it as "at":

```
xs@0                 // xs at 0
xs@-1                // the last element
grid@y@x             // (grid@y)@x
ages@"john"          // a std:map
entry@1              // a tuple
people@0:name        // a field of the first element
xs@i = 10            // assignment: @(xs, i, 10)
counts@word += 1     // @(counts, word, @(counts, word) + 1)
```

Array literals `[1, 2, 3]` and array types `T[]` stay as they are. Only
indexing changes.

## Motivation

Almost everything in the language is a function name. `+`, `==`, `in`, `#` and
`...` are overload sets that any module can extend, and `.` is only sugar for
putting the receiver first. Indexing is the exception:

* **It cannot be extended.** `x[i]` is the AST node `N_INDEX`, which the
  interpreter evaluates with hard-coded cases for arrays, strings, objects and
  (since `std:map` and tuples) maps and tuples (`get_index` / `set_index` in
  `src/interp.c`). A user type such as a matrix or a ring buffer cannot take part.
  Adding `m[k]` for `std:map` meant editing the evaluator, not writing an
  overload.
* **No function name can express it.** An operator name is one run of symbols,
  and `[` ... `]` wraps its argument (a circumfix form). The language has no way to
  declare a name made of a first part, the arguments, and a second part.
* **It makes whitespace significant.** `xs[0]` indexes but `f [1]` passes an
  array to `f`. The rule exists only because `[` has two meanings after an
  expression.

With `@`, indexing is a function like the rest: `@(xs, 0)`, `xs.@(0)`, passing
`@` as a value (`keys.map(k -> m@k)`), and adding overloads.

## Can the lexer split `array@5`?

It already does, at no extra cost. Identifiers are letters, digits, `_` and
non-ASCII bytes (`is_ident_char` in `src/lexer.c`), and `@` is one of the
operator characters, so `array@5` is not one identifier. It lexes as `array`
`@` `5`, exactly like `a+5`. This works today with a user-defined `@`:

```
<T>function @(T[] xs, int i) -> T { return xs[i] }
const grid = [[1, 2], [3, 4]]
io:print_nl(xs@0, xs@-1, grid@1@0)        // 10 30 3
```

`xs@-1` works because a run of operator characters (`@-`) is split by the
operators the lexer knows. `@` becomes a built-in operator, so it is always
known, even in files that don't declare it.

Splitting is not the hard part. The hard part is **how tightly `@` binds**; see
[the next section](#precedence-the-real-question).

## Semantics

### Reading

`a@b` calls the overload set `@` with `(a, b)`. The interpreter's current
behaviour becomes ordinary built-in overloads:

| overload | replaces |
|----------|----------|
| `<T>@(T[] xs, int i) -> T` | `xs[i]`, negative from the end |
| `@(string s, int i) -> string` | `s[i]` (a byte, as today) |
| `@(object o, string key)` | `o["key"]` |
| `@(tuple t, int i)`, see below | `t[i]` |
| `<K, V>@(std:map<K, V> m, K key) -> V` | `m[k]` (in `lib/std.pp`) |

A tuple overload needs a parameter type for "any tuple". Today that is spelled
`any` plus a runtime check, or a bare `tuple` could mean any tuple in
parameter position only. That is an open question.

### Writing

There is no separate setter name. **Assignment to an `@` expression calls `@`
with three arguments**:

```
xs@i = v             // @(xs, i, v)
xs@i += 1            // @(xs, i, @(xs, i) + 1), xs and i evaluated once
grid@y@x = 0         // @(grid@y, x, 0)
```

```
<T>function @(T[] xs, int i, T value) -> void
<K, V>function @(std:map<K, V> m, K key, V value) -> void
```

A type with only the two-argument overload is read-only, like a tuple, which
would have no three-argument `@`. The error comes from overload resolution:
`no overload of '@' accepts (tuple, int, int)`. A dedicated message could say
"tuples cannot be changed".

### Extending

```
type Matrix = { int rows, int cols, float[] cells }
function @(Matrix m, tuple<int, int> at) -> float { return m:cells@(at@0 * m:cols + at@1) }
m@tuple(1, 2)

<T>function @(T[] xs, range r) -> T[] { return xs.slice(r:start, r:end) }
xs@(1...3)                                   // a slice

// a whole family: any operator starting with @ is an access operator
<T>function @?(T[] xs, int i) -> T? { ... }  // xs@?10 is none() instead of an error
```

## Precedence: the real question

With `@` in the normal precedence table, a user-defined `@` today gets the
default level 65. That gives the wrong answer in two common cases:

```
-xs@0       // (-xs)@0: a prefix operator binds tighter than any binary one
#names@0    // (#names)@0, where names[0] meant the first name's length
xs@i + 1    // (xs@i) + 1: fine
xs@i+1      // also (xs@i) + 1; the index i+1 needs xs@(i+1)
```

Both prefix cases fail at runtime today (`no overload of '-' accepts (array)`).

**Recommendation: `@` binds like a postfix operation, and its right side is
atomic.** `@` is parsed with `.` and `:` in the postfix loop, not in the operator
chain. Its right operand is one atom: a literal, a name, a parenthesised
expression, or a prefix operator applied to an atom (`-1`, `#ys`). Anything that
follows applies to the result:

| written | means | today's equivalent |
|---------|-------|--------------------|
| `xs@0` | `@(xs, 0)` | `xs[0]` |
| `-xs@0` | `-(xs@0)` | `-xs[0]` |
| `#names@0` | `#(names@0)` | `#names[0]` |
| `people@0:name` | `(people@0):name` | `people[0]:name` |
| `names@0.upper()` | `(names@0).upper()` | `names[0].upper()` |
| `grid@y@x` | `(grid@y)@x` | `grid[y][x]` |
| `xs@-1` | `@(xs, -1)` | `xs[-1]` |
| `xs@(i + 1)` | `@(xs, i + 1)` | `xs[i + 1]` |
| `xs@f(x)` | `(xs@f)(x)`, so write `xs@(f(x))` | `xs[f(x)]` |
| `xs@cfg:FIRST` | `(xs@cfg):FIRST`, so write `xs@(cfg:FIRST)` | `xs[cfg:FIRST]` |

This keeps the meaning of every current `x[i]`. The cost is that an index that is
more than an atom needs parentheses. That is the one ergonomic loss compared to
brackets, which delimit the index for free. Like `:`, `@` must be on the same
line as its left side.

Any operator whose name starts with `@` (`@?` above) would get the same
postfix treatment, so the whole family reads the same way. These operators
would not be affected by `precedence` declarations. That is a deliberate
exception to "precedence is runtime data", in the same way that `.` and `:` are
not in the table.

The alternative is to keep `@` in the table at a new top level (for example 90,
above `**`) and accept `-(xs@0)` / `#(names@0)`. It is simpler to implement but
changes the meaning of existing code when it is translated mechanically, so it
is not recommended.

## What it costs

* **Implementation.** Add `@` to the lexer's built-in operators. In
  `parse_postfix`, parse `@op atom` into a call of `@`, and replace the `[`
  postfix case. Assignment targets become `postfix '@' atom`, compiled to the
  three-argument call. Move `get_index` / `set_index` into built-in overloads of
  `@`. Call sites get the existing inline caches, so `xs@i` in a loop costs about
  the same as a call to `#`. It may still be measurably slower than today's
  direct `N_INDEX` and should be benchmarked.
* **Migration.** About 23 index expressions in 12 `.pp` files in this repo
  (tests, test modules, `lib/`). The rewrite is mechanical. One release could
  accept both and warn on `x[i]` before removing it.
* **The whitespace rule goes away.** With no postfix `[`, a `[` after an
  expression on the same line can only start a juxtaposed array argument, so
  `f [1]` and `f[1]` mean the same thing.
* **`@` is spent.** It can no longer be used for something else later, such as
  annotations (`@inline function f()`). A prefix `@` would still be free in
  principle, but the two would read alike.
* **`@=`.** It would lex as the compound assignment `x = x @ y`, which means
  nothing useful for indexing. It should be a syntax error that points to
  `x@i = v`.

## Alternatives considered

| alternative | why not (or not alone) |
|-------------|-------------------------|
| Keep `[]` and make it sugar for an overloadable function named `[]` / `[]=` (like Lua's `__index` or Python's `__getitem__`) | It solves extensibility with no new syntax, but keeps the special parser case and the whitespace rule, and does not generalise to user-made forms like `@?`. It is the strongest alternative. |
| `xs.at(i)`: just a function | It works today, but it is verbose, has no assignment form, and doesn't read like access. Worth keeping as a plain name next to `@`. |
| `xs:i` | `:` reads attributes by name; `o:0` versus `m:key` (field or map key?) would be ambiguous. |
| `xs#i` | `#` is already length. |
| `xs!i`, `xs.(i)` | `!` reads as negation; `.(` collides with dot-call syntax. |

## Open questions

1. Postfix-level `@` (recommended) or a top level in the precedence table?
2. How is "any tuple" written as a parameter type for `@(tuple, int)`?
3. Should out-of-range access stay an error, with `@?` for the Option form in
   `std`?
4. Should `@` overloads be `global` by default, like `==`, so a type's `@` works
   in files that don't import its module? Today that needs `global function @`
   or the receiver's-module lookup that binary operators already do.
5. Transition: warn on `x[i]` for one release, or switch in one commit?
