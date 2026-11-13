# Ranges: `range` → `Range<T>`

**Implemented (option B)**, 2026-11-13. The user-facing rules are in
[LANGUAGE.md](LANGUAGE.md#ranges); this file keeps the reasoning. Differences from
the plan below: a bare `Range` reads its bounds as numbers (the numeric helpers
of `lib/std.pp` and `lib/math.pp` take a bare `Range`); `...` / `...=` with a
float make a float range that steps by 1.0; the interpreter records
`impl ... for Range<T>` on one table shared by every `Range<T>`.

Originally a design, not an implementation. It answers the question of what the type
`range` should become, given that every other non-primitive type is PascalCase
and that `"A"...="Z"` reads naturally as a range of letters.

## Today

| written | means |
|---------|-------|
| `range` | any range (of ints or of floats) |
| `range<int>`, `range<float>` | a range that counts in ints / in floats |
| `0...5`, `0...=5`, `...5` | built-in `...` / `...=`: ints only |
| `range(0, 1, 0.25)` | any numbers and a step; any float makes all three floats |
| `r:start`, `r:end`, `r:step`, `r:inclusive` | the fields; a `...=` range stores its end one step past the last element (`(0...=5):end` is `6`) |

At runtime, a range is one native object (`ObjRange`: three number `Value`s
and a flag). The bytecode VM and the wasm target compile `for i in a...b` to a
counting loop. `range` is the only lowercase type that is not a primitive
value (`int`, `float`, `string`, `bool`) or a wildcard (`object`, `function`,
`type`, `unknown`).

Since this draft, `...` and `...=` **can be overloaded** (`tests/range_overload.pp`).
That changes the question. A user type no longer needs to fit into the built-in
range to get the syntax: `function ...(Day a, Day b) -> Days` already works.

## The two options

### A. Numbers only: `Range<T>` with `T` an `int` or a `float`

The type is renamed, and nothing else changes:

| today | A |
|-------|---|
| `range` | `Range` (= `Range<number>`) |
| `range<int>` | `Range<int>` |
| `range<float>` | `Range<float>` |
| `range<string>` | error: `Range takes int, float or number` |

* Small: a rename in the checker's type names, the docs, `lib/`, the tests and
  the editor grammars. The runtime does not change.
* `"A"...="Z"` stays an error. Letters would need their own type and an
  overload, `function ...=(string a, string b) -> Letters`, written by whoever
  wants it.
* `Range` is a generic type that accepts only two arguments. That is a special
  rule the language has nowhere else: there are no bounded generics.

### B. Any `T`: `Range<T>` is an interval, and what it does with `T` comes from `T`

`Range<T>` is one built-in generic type for **an interval of any `T`**: a
start, an end, and whether the end is included. Everything else a range can
do is an ordinary function or impl, chosen by `T`:

```pipo
<T>function ...(T start, T end) -> Range<T>          // in the prelude: any T
<T>function ...=(T start, T end) -> Range<T>

0...5                       // Range<int>
0.0...=1.0                  // Range<float>
"A"...="Z"                  // Range<string>
Day{ n: 1 }...Day{ n: 7 }   // Range<Day>: no code needed to build it
```

| what | where it comes from | for which `T` |
|------|---------------------|---------------|
| building it (`a...b`, `a...=b`) | the generic `...` / `...=` | any |
| `r:start`, `r:end`, `r:inclusive` | the type | any; `end` is the real end now: `(0...=5):end` is `5` |
| `x in r` | `<T>function in(T x, Range<T> r) -> bool`, using `<` / `<=` of `T` | any `T` with `<` and `<=`: numbers, strings, a type that implements `Comparator` |
| `for x in r` | `impl Iterable<T> for Range<T>` | built in for `int`, `float` and `string`; anything else implements it |
| `range(start, end, step)`, `r:step` | numbers only | `int`, `float` |
| `#r`, `has(r, x)`, indexing an array with a range | numbers only | `int`, `float` |

**How a user type gets a working `Range<Day>`.** It does not extend anything.
`Range<Day>` exists as soon as `Day` exists, because `Range` is generic, the
same way `Day[]` and `Day?` do. The type gives it meaning the way any type
gives meaning to a generic container, through the interfaces it implements:

```pipo
type Day = { int n }

impl Comparator for Day                   // gives `in`: Day{ n: 3 } in mon...sun
function ==(Day a, Day b) -> bool { return a:n == b:n }
function <(Day a, Day b) -> bool { return a:n < b:n }
function <=(Day a, Day b) -> bool { return a:n <= b:n }

generator iter(Range<Day> r) -> Day {     // gives `for`: for d in mon...=sun
    for i in r:start:n...r:end:n {
        emit Day{ n: i }
    }

    if r:inclusive {
        emit r:end
    }
}

impl Iterable<Day> for Range<Day>
```

This is the same mechanism the prelude already uses for
`impl Iterable<int> for range<int>` and `impl Iterable<float> for range<float>`.
B makes it the rule for every `T`, not a special case for two types.

**Strings.** `"A"...="Z"` iterates code points: `"A"`, `"B"`, ..., `"Z"`. A bound
that is not a single code point is a runtime error at the start of the loop:
`"AA"..."B"` has no next element. `"m" in "a"...="z"` compares strings, so it
is true for `"mm"` too. That is the honest meaning of "between `a` and `z`",
and `has(r, x)` (one of the elements a `for` visits) answers the regex
question exactly.

**A type that is not ordered.** `[1]...[2]` builds a `Range<int[]>`, which is
a harmless value. Using it is the error: `for` says `Range<int[]> is not
Iterable`, and `in` fails because arrays have no `<`. Today a missing `<` inside
a generic body is found when the comparison **runs**, not by the checker
(`<T>function lt(T a, T b) -> bool { return a < b }` with arrays). Checking
`<` per instance is a checker improvement that B would want, and every other
generic function would benefit from it too.

**A user type that wants its own range.** Because `...` is overloadable, a type
can still return something else (`function ...(Date a, Date b) -> DateSpan`,
with time zones and a step in days). The more specific overload wins over the
generic one, as for any call.

### Representation (B)

Numeric ranges keep the native `ObjRange`. The bytecode and wasm counting
loops stay as they are. Any other `T` uses the same object with `start` and
`end` holding any `Value` and no step. Its fields and `in` work everywhere;
`for` goes through `Iterable` (the `iterate()` path the checker already uses
for user types). The one change visible at runtime is that `end` stores the
real end, with `inclusive` as a flag. The code that compensates for the
`end + step` trick (`std:low` / `high`, `math:end`) gets simpler.

## Recommendation: B

* It answers the question about user types without new machinery. `Range<T>` is
  a generic like `T[]`, `Option<T>` and `Map<K, V>`, and a type gives it
  meaning by implementing `Comparator` and `Iterable`, which PiPo already has.
  A doesn't let user types use `Range` at all.
* It makes the name honest. A PascalCase generic that accepts every `T` is an
  ordinary type. A `Range<T>` that rejects `string` would be the only bounded
  generic in the language.
* It costs little more than A where it matters. Numbers keep their native,
  fast path, so `0...n` loops compile exactly as today. The new part (non-number
  `T`) is mostly prelude code: the generic `...`, `in`, and `Iterable` for
  strings.
* It fits the change just made: `...` / `...=` are overloadable, so the generic
  `...` is a default, not a fixed meaning.

**Decisions to confirm before implementing**

1. `range` and `range<int>` are **removed**, not kept as aliases (the language
   is early, and two spellings of one type would be noise). Bare `Range` is
   any range.
2. `(0...=5):end` becomes `5` (the real end). This is a breaking change for
   code that reads `:end`; the tests and `lib/` would be updated with it.
3. `range(start, end, step)` keeps its name. Like `map()` and `set()`, a
   constructor is snake_case.
4. String ranges iterate **code points**, not grapheme clusters, matching
   `for ch in "héllo"`.

## Work

1. Checker / types: `Range<T>` replaces the `range` primitive (`ST_RANGE` keeps
   an element type, now any type); `range` becomes an error that suggests
   `Range<T>`.
2. Runtime: `ObjRange` stores the real end; `start` / `end` may be any value.
   Numeric loops in the interpreter, bytecode and wasm are unchanged apart
   from the end.
3. Prelude: generic `...` / `...=` beside the numeric built-ins (the built-ins
   win for numbers), `<T>function in(T, Range<T>)`, `Iterable<string> for
   Range<string>`.
4. `lib/std.pp`, `lib/math.pp`, the tests, `md/`, the IntelliJ stubs and the
   TextMate grammar: `range` → `Range`.
5. Tests: `Range<string>` iteration and `in`, a user `Range<Day>` with
   `Comparator` + `Iterable`, a user `...` returning its own type, errors for
   `range<int>`, a non-iterable `Range<T>` in `for`, and a multi-character
   string bound.
6. Wasm: numeric ranges as today; `Range<T>` of other types is reported as
   unsupported at first, like the other wasm gaps listed in TARGETS.md.
