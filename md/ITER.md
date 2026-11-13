# Iteration: `Iter` and generator functions

Implemented on the interpreter and the bytecode VM (the wasm target refuses
generators at compile time for now, see [how it runs](#how-generators-run)).
This describes:

* the prelude's interfaces `Iterable<T>`, `Iter<T>` and `KeyIterable<K, V>`, the
  protocol `for` uses, and what implements them;
* **generator functions**: `generator name(...) -> T { ... }` declares a function
  whose call builds a `Generator<T>` (an `Iter<T>`) without running anything;
  `emit value` produces the next value and pauses there;
* the lazy functions (`map`, `filter`, `take`, ...), the only set there is.

The [design notes](#design-notes) at the end are the requirements; each one is
answered in [how the notes were applied](#how-the-design-notes-were-applied).

Contents: [the example](#the-example) · [the interfaces](#the-interfaces) ·
[what implements them](#what-implements-them) · [`for`](#for-loops) ·
[generators](#generator-functions) · [lazy functions](#the-lazy-functions) ·
[how it runs](#how-generators-run) · [the notes](#how-the-design-notes-were-applied) ·
[decisions to revisit](#decisions-to-revisit)



## The example

```pipo
generator fib() -> int {
    let int a = 0
    let int b = 1

    while true {
        emit b                   // produce b and pause; the next next() continues on the next line
        const c = a + b
        a = b
        b = c
    }
}

const Generator<int> f = fib()   // builds the generator: nothing in fib's body has run yet
f.next()                         // some(1)
f.next()                         // some(1)
f.next()                         // some(2)

for x in fib() {                 // a Generator<int> is an Iter<int>, so for takes it
    if x > 100 {
        break
    }

    io:print(x, "")              // 1 1 2 3 5 8 13 21 34 55 89
}

fib().take(10).to_array()        // [1, 1, 2, 3, 5, 8, 13, 21, 34, 55]
```

`next()` runs the body until the next `emit` and gives `some(value)`, or `none()`
once the body has ended. `none()` then stays: a finished generator never runs
again. `tests/generators.pp` runs every example of this page on both targets.



## The interfaces

Declared in the prelude, so they need no import:

```pipo
// a collection that can give a fresh Iter, as many times as asked
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
```

* An **`Iter`** has a position. Reading it moves the position, so two loops over
  the same `Iter` share it: the second continues where the first stopped (like
  Python's iterators and Rust's `Iterator`).
* An **`Iterable`** (an array, a range, a set) is not used up: it gives a fresh
  `Iter` each time. A function that only reads values takes an `Iterable<T>`,
  which accepts both kinds, because `Iter` extends `Iterable` with a default
  `iter()` that returns the `Iter` itself. A type only writes `next` to be usable
  everywhere.
* **`KeyIterable`** is separate because only some collections have keys. Its
  name starts like `Iterable` on purpose: completing `Iterab...` offers both.

```pipo
function sum(Iterable<int> xs) -> int {
    let total = 0
    for x in xs {
        total += x
    }

    return total
}

sum([1, 2, 3])          // an array
sum(0...=100)           // a range
sum(fib().take(10))     // a generator
```



## What implements them

| type | `Iterable<...>` | `KeyIterable<...>` | where |
|------|-----------------|--------------------|-------|
| `T[]` | `T` | `int, T` (index, item) | prelude: `iter`, `pairs` are generators |
| `Range<int>` / `Range<float>` / `Range<string>` | `int` / `float` / `string` | | prelude; any other `Range<T>` through its own `impl Iterable<T> for Range<T>` |
| `string` | `string` (code points) | | prelude |
| `Set<T>` | `T` | | the prelude |
| `Map<K, V>` | | `K, V` | the prelude: `pairs`; keys and values alone through `m.keys()` / `m.values()` |
| `Generator<T>` | `T` (it is an `Iter<T>`) | | prelude: `next` calls the native `generator_next` |
| a record | | | not iterable: `fields(r)` gives its fields |

A user type implements them like any interface; a generator is the easy way to
write `iter` or `pairs`:

```pipo
<T>type Tree = { T value, T[] children }

<T>generator values(Tree<T> t) -> T {
    emit t:value
    for c in t:children {
        emit c
    }
}

<T>function iter(Tree<T> t) -> Iter<T> {
    return t.values()
}

<T>impl Iterable<T> for Tree<T>

for v in tree {
    io:print_nl(v)
}
```

A hand-written `Iter` is a record that keeps its position:

```pipo
type Countdown = { int left }

function next(Countdown c) -> int? {
    if c:left == 0 {
        return none()
    }

    c:left -= 1

    return some(c:left + 1)
}

impl Iter<int> for Countdown

for n in Countdown({ left: 3 }) {     // 3 2 1
    io:print(n, "")
}
```

`KeyIterable` for a type of one's own:

```pipo
type Inventory = { string[] names, int[] counts }

generator pairs(Inventory inv) -> tuple<string, int> {
    for i, n in inv:names {
        emit tuple(n, inv:counts[i])
    }
}

impl KeyIterable<string, int> for Inventory

for name, count in inventory { ... }
```



## `for` loops

| loop | accepts | variables |
|------|---------|-----------|
| `for x in c` | an `Iterable<T>` | `x: T` |
| `for k, v in c` | a `KeyIterable<K, V>`: an array or a tuple (index, item), a map (key, value), a generator / `Iter` of `tuple<K, V>`, a type that implements `KeyIterable` | `k: K`, `v: V` |

Everything else is an error found by the checker, with the fix in the message:

| written | error |
|---------|-------|
| `for i, x in a_set` (a range, a string, a set, a generator of non-pairs) | `for i, x over a set: only a KeyIterable (an array, a map, pairs) has keys; use .enumerate() to count the items` |
| `for x in a_map` | `for x over a map: use map.keys() or map.values() (for k, v in map gives both)` |
| `for x in a_record` | `cannot iterate over a record (P): use fields(x) to go through its fields, or implement Iterable<T> for its type` |

```pipo
for i, x in xs { }                       // an array: index and item
for k, v in ages { }                     // a map: its entries
for k in ages.keys() { }                 // keys alone, values alone: explicit
for i, ch in "héllo".enumerate() { }     // counting anything else: explicit
for name, value in fields(point) { }     // a record's fields: explicit
```

**Decisions:**
* **The fast path is chosen by the value, not by its static type.** The built-in
  collections (arrays, ranges, strings, tuples, maps, sets, native classes) and
  generators are iterated directly by every target: an index over an array, a
  counter over a range, a resume of a generator. Only a value of another type
  goes through the interfaces, by calling the prelude's `iterate(c)` /
  `iterate_pairs(c)` (a generator over `c.iter()` / `c.pairs()`). The test is made
  when the loop starts, so a function declared `-> Iterable<int>` that returns an
  array is still looped over as an array.
* An `Iter` is not reset by `for`: a generator read by two loops gives each value
  once in total.
* Patterns in `for` (`for tuple(a, b) in pairs`, like `if tuple(x, y) = p`) would
  fit, but are a separate change.



## Generator functions

```pipo
generator name(params) -> T { body }         // a generator; calling it gives a Generator<T>
<T>generator name(...) -> T { ... }          // generic, like functions
export generator ...                         // exported, like functions
global <T>generator ...                      // global, like functions
```

* `generator` is a contextual keyword, like `scope`: it starts a declaration only
  where it is followed by a name and `(`, so `generator` stays usable as a name.
* The type after `->` is the type of the **values**, and it is required. The call
  gives a `Generator<T>`; the generator is a function value of type
  `(params) -> Generator<T>`, so it is passed and stored like any function and
  fits an interface requirement that returns `Iter<T>`.
* **Calling it runs nothing.** The arguments are bound, and the body waits for
  the first `next()`.
* In the body:

  | statement | meaning |
  |-----------|---------|
  | `emit value` | gives `value` and pauses right after the statement |
  | `return` | ends the generator: this `next()` and every later one give `none()` |
  | `return value` | gives `value` as the last one and ends: `emit value` then `return` |
  | the end of the body | like `return` |

* The values are checked against `T` (`value of generator 'g': expected int but
  got string`) and converted like a return value (`emit 1` in a `-> float`
  generator gives `1.0`).
* A runtime error inside the body is raised by the `next()` that ran it.
* `Generator<T>` is a reference: passing it shares the position, and `==`
  compares identity. Calling `next()` on a generator from its own body is an
  error (`generator 'g' is already running`).

### What a generator body cannot do

Pausing keeps **one** frame, the generator's own body (a *stackless* generator,
as in JavaScript, Python and C#). So, all syntax errors:

* `emit` / `return` inside the block of a scope call: it would have to pause the
  scope function too.

  ```
  generator lines_of(string path) -> string {
      _file const f = fs:open(path) {
          emit f.read_line()     // error: a generator cannot emit from the block of a scope call
      }
  }
  ```

  Read into a local first, or loop over an `Iter` the scope gives.
* `lift()`: it would return `none()` / `fail()` from `next()`. Use `match`.
* `emit` outside a generator's own body (in a nested function or lambda).

`defer` stays where it is, only in scope functions. A generator that is not run to
its end runs no cleanup, so a resource it reads should belong to a scope call
around the loop that reads the generator.

`break`, `continue`, loops, `if` / `match` statements (also with patterns),
nested functions, closures over the enclosing function, and other generators all
work as usual.



## The lazy functions

Written in the prelude as generators over `Iterable<T>`. They are the **only**
`map` / `filter` / `reduce`: the old eager array natives are gone, so
`xs.map(f)` is a generator, and `xs.map(f).to_array()` is the array.

```pipo
<T, R>generator map(Iterable<T> source, Mapper<T, R> f) -> R {
    for x in source {
        emit f(x)
    }
}

<T>generator take(Iterable<T> source, int n) -> T {
    if n <= 0 {
        return
    }

    let left = n
    for x in source {
        emit x
        left -= 1
        if left == 0 {
            return              // reads no more than n values from its source
        }
    }
}
```

| function | gives |
|----------|-------|
| `map(f)`, `filter(keep)`, `take(n)`, `skip(n)`, `chain(other)` | a `Generator` |
| `enumerate()`, `zip(other)` | `Generator<tuple<int, T>>`, `Generator<tuple<A, B>>` (as long as the shorter) |
| `to_array()` | `T[]` (reads everything) |
| `reduce(f, start)` | one value (`reduce(xs, +, 0)` works with an operator) |
| `join(sep)` on a generator | its values joined like an array's |
| `fields(record)` | `Generator<tuple<string, V>>`, `V` the fields' joined type |

Each one reads its source only as far as it is read itself:
`noisy().map(x -> x * x).filter(x -> x % 2 == 1).take(3)` pulls 5 values from
`noisy()`, not all of them.



## How generators run

* **Interpreter.** A generator keeps the path of statements it paused in: for each
  block the statement it is at and its scope, for each loop its position (the
  value iterated, the index), for each `if` / `match` the branch taken and its
  bindings. `next()` walks the body again along that path, without running
  anything before the pause point, and continues after the `emit`. Only
  statements that contain an `emit` or `return` go through this walk; every other
  statement runs through the ordinary executor. Since `emit` and `return` are
  statements, and are errors inside an `if` / `match` used as a value and inside
  scope blocks, a pause can only happen between statements of blocks, loops and
  branch statements, which is all the path has to describe.
* **Bytecode.** `OP_SUSPEND` (the `emit`) copies the frame's slots and the
  instruction pointer into the generator and pops the frame; `next()` copies them
  back on the stack and runs from there. `OP_GEN_END` ends it (`return`, with or
  without a last value). A call of a generator function builds the generator in
  place of the frame. Loop positions are locals, so they are saved with the slots.
  `OP_JUMP_ITERABLE` is the runtime test of the fast path: a `for` over a value
  whose static type is an interface jumps straight to the direct loop when the
  value is a built-in collection or a generator, and calls `iterate()` otherwise.
* **Wasm.** WebAssembly cannot pause a function, so the body becomes a state
  machine: a **step function** that runs it to its next value. Every local of
  the step lives in the generator's frame between steps (loaded when it starts,
  stored when it pauses). The `emit`s are numbered in source order; resuming at
  `emit` k enters the body again with k in a `resume` local, and the statements
  on the way to that `emit` are routed without running anything else, like the
  interpreter's path: statements of a block that do not hold k are skipped, a
  loop skips its test, an `if` / `match` takes the branch that holds k without
  testing. Structured control flow is kept (no jump table into the middle of
  loops), at the cost of one test per statement of a block that holds an
  `emit`. A `for` over a non-built-in `Iterable` always goes through
  `iterate()` here: a boxed `Iterable<int>` that holds an array reaches
  `iter(T[])` by dispatch, which is correct but slower than the other targets'
  direct loop. The stack-switching proposal would make the lowering
  unnecessary. Details in [TARGETS.md](TARGETS.md#generators-as-step-functions).



## How the design notes were applied

| note | what was done |
|------|---------------|
| one set of `Iter` / lazy functions, breaking changes are fine | the eager `map`, `filter`, `reduce` natives were removed; the prelude's lazy ones are the only ones. Tests that printed `xs.map(f)` now print `xs.map(f).to_array()` |
| keep `for i, x in xs`, `for x in xs`, `for k, v in map`; records only through `fields()`; maps only through `keys()` / `values()` alone | exactly these, as checker errors with the fix in the message ([`for`](#for-loops)) |
| `Keyed` renamed `KeyIterable` | done |
| `for k, v in not_key_iterable` is an error; `.enumerate()` must be used | an error of the **checker** (whether a value has keys depends on its type, which the parser does not know), with the message pointing at `.enumerate()` |
| `emit` keyword; `return` with a last value or nothing | `emit` is a reserved keyword; `return value` is `emit value` + `return` |
| `OP_SUSPEND` for bytecode | done; the bytecode target runs generators by saving the frame, not through a lowering |
| built-in iteration bypasses `Iter`, decided by the concrete type | decided by the runtime value at the start of the loop, so `for x in f()` with `f() -> Iterable<int>` returning an array takes the array loop |



## Decisions to revisit

* **`generator` needs `-> T`.** Inferring `T` from the `emit`s like a function's
  return type would be possible; requiring it keeps a generator's type visible in
  its declaration.
* **`map` / `filter` names.** Being the only ones, they shadow nothing, but code
  that wants an array writes `.to_array()`. An eager `xs.map_array(f)` would be a
  second set, which the notes rule out.
* **An overload set passed as a generic callback** (`reduce(xs, +, 0)`) is
  accepted by the checker before the callback's generics are bound (they are bound
  by the arguments after it); a wrong set is then found when it is called.
* **No `first`, `last`, `count`, `any`, `all`, `sum` yet.** The names collide with
  functions some tests declare themselves; they can be added when the overload
  rules for them are settled.
* **Wasm keeps every local of a generator in its frame** and loads / stores
  all of them at each step. Saving only the locals that live across an `emit`
  would need a liveness analysis; generators are small so far.
* **Wasm has no fast path for an interface-typed `for`**: it always calls
  `iterate()`. A runtime test of the box's type id before the loop (like
  `OP_JUMP_ITERABLE`) would give arrays and ranges the direct loop.
* **`fields()` is not compiled to wasm**: it is a native generator, which the
  host would have to run.


# Design notes

* Make only set of `Iter`/Lazy functions. The language is not used anywhere, so breaking changes are ok. 
* Keep implicit `for i, x in xs` and `for x in xs` and `for k, v in map` but enforce explicit `for k, v in struct.fields()` 
(hoping the record == struct == type), that will make it that struct passed to for cycle is a compiler error, and 
`for v in map.values()` and `for k in map.keys()`
* Change the name of interface `Keyed` to `KeyIterable` such that writing `Iterab...` still shows both `Iterable` and `KeyIterable`
* Using `for k, v in not_key_iterable` is a syntax error, `for k, v in not_key_iterable.enumarate()` must be used
* Introduce `emit` keyword for readability and `return` can have either a last value or nothing:
```pipo
// In generator
// Hard stops the generator, like an end of an array
return 

// Emits the value of 10, and hard stops
return 10
// equivalent to (as an one liner)
emit 10
return
```
* Definitely introduce OP_SUSPEND for bytecode as an optimization
* Include built-in types iteration optimization such that `for x in xs` 
completely bypasses the Iter abstraction. Tie it to the concrete type if possible such that this
`for x in return_type_set_to_iterable_but_returns_native_array()` still uses the fast iteration