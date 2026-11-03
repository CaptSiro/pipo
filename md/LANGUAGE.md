# Language Reference (as implemented)

This describes what the interpreter in `src/` actually does. The original
idea is in [PROPOSAL.md](PROPOSAL.md) and the formal grammar is in
[SYNTAX.md](SYNTAX.md). Places where the proposal left something open, and
the choice that was made, are marked **Decision**.

```
import io
io:print("hello world")
```

Source files use the `.pp` extension. Run one with `pp file.pp`, or start a REPL
with `pp`.

## Lexical structure

* `// line` and `/* block */` comments.
* Statements end at a newline. `;` is optional and does nothing.
* Identifiers: letters, digits, `_` and any non-ASCII bytes.
* Numbers: `42`, `1_000`, `0xFF`, `0b1010`, `3.14`, `1e-9`. Integers are
  64-bit, floats are doubles.
* Strings: `"..."` or `'...'`, escapes `\n \t \r \0 \\ \" \' \xHH \u{1F600}`.
  Strings may span lines.
* Keywords: `import export global function return const let if else for in while
  break continue match enum true false`. `type`, `interface`, `impl`,
  `precedence`, `over`, `under`, `as` and `native` are contextual and can still
  be used as names.

### Operators are names

Any run of `+ - * / % < > = ! & | ^ ~ # ? @ $ \` and `..` is a symbolic name.
`=` (assignment) and `->` are reserved. `&&` and `||` are short-circuiting
and cannot be overloaded.

**Decision:** a run of operator characters is split using the operators the
lexer knows about: built-ins, plus every `function <op>` and
`precedence <op> ...` in the same file. `...#array` becomes `...` `#`.
Operators declared only in another module are not known while this file is
lexed, so write them with spaces around them (`a <> b`).

### `target op= value`

As a statement, `x op= y` (with `x` a variable, `a:field` or `c[i]`) first looks for
a function named `op=` that accepts `(x, y)`, searched like a binary operator. If
one exists, it runs **in place** and nothing is written back. Otherwise the
statement means `x = x op y`:

```
<T>function +=(T[] items, T[] more) -> void { for m in more { items.push(m) } }
const ys = [1]
ys += [2, 3]          // in place: +=(ys, [2, 3]); works on a const, the binding does not change
let n = 1
n += 1                // there is no +=(int, int): n = n + 1
counts[word] += 1     // []=(counts, word, [](counts, word) + 1); counts and word evaluated once
x <= 5                // <= accepts (int, int): a comparison, never x = x < 5
```

**Decisions:**
* The choice is made per call by **overload matching**, not by whether the name
  exists, so an `op=` written for arrays does not break `n += 1` for ints.
* It only applies to whole statements. Assignment is not an expression, so
  `if x <= 5` and `f(a != b)` are always operator calls.
* `==` never decomposes: the part before `=` would be `=`. `<=`, `>=`, `!=` and
  `...=` exist as built-in functions, so they are always comparisons (or ranges).
* The right side is the whole expression, as in assignment: `a += b == c` is
  `a += (b == c)`.
* With neither an `op=` nor an `op` that fits, the error names both:
  `'-=' needs an overload of '-=' (in place) or of '-' accepting (object, int)`.

## Calling functions

```
+([], [])        // classic
[].+([])         // dot syntax: receiver becomes the first argument
-x               // unary: an operator in prefix position
success value    // unary: an identifier followed by an argument on the same line
a + b            // binary
```

## `.` calls, `:` reads attributes

`.` is reserved for function calls. Attributes are read with `:`:

```
person:name                 // object field
person:name = "Bob"         // assign a field
person["name"]              // field by computed key
io:print                    // exported symbol of a module
Color:RED, Option:some      // enum variant
obj:callback(1)             // call a function stored in a field
```

`receiver.callee(args)` calls `callee` with the receiver inserted as the
first argument. `callee` can be a plain name or a `:` path:

```
xs.push(4)                          // push(xs, 4)
4.module:function(module:constant)  // module:function(4, module:constant)
```

`x.name` without parentheses is a syntax error that suggests `:` instead.

**Decisions:**
* A prefix *operator* binds tightly: `-a.b()` is `-(a.b())`, `#xs - 1` is
  `(#xs) - 1`.
* A prefix *identifier* takes the whole rest of the expression:
  `fail "bad: " + name` is `fail("bad: " + name)`.
* `f(x)` and `f (x)` are both calls. `xs[0]` indexes, while `f [1]` (with a
  space) passes an array to `f`.
* A binary operator must be on the same line as its left operand, unless it
  is inside `()`, `[]` or `{}` literals. A line starting with `.` continues the
  previous expression (method chains).
* An operator followed by `,` `)` `]` `}` or a newline is a value:
  `reduce(xs, +, 0)`.

### Precedence

Higher numbers bind tighter. All operators are left associative.

| level | operators              |
|-------|------------------------|
| 80    | `**`                   |
| 70    | `* / %`                |
| 65    | any other operator     |
| 60    | `+ -`                  |
| 50    | `... ...=`             |
| 40    | `< > <= >=`            |
| 35    | `in`                   |
| 30    | `== !=`                |
| 20    | `&&`                   |
| 10    | `\|\|`                 |

`precedence X over Y`, `precedence X under Y` and `precedence X as Y` change
the table **at runtime**. Each operator chain is re-associated when the
table changes. `over` leaves X alone if it already binds tighter than Y.
Otherwise it places X between Y and the next level above Y.

## Variables

```
let x = 1            // mutable
const y = 2          // immutable binding
let int z = 3        // typed: assignments are checked
let string later     // declared, must be assigned before use
const DEBUG          // a "define": the name exists and holds true
```

`defined("DEBUG")` tests whether a name is visible.
Assigning `int` to a `float` variable or parameter converts it.

## Types

| syntax                  | meaning                                      |
|-------------------------|----------------------------------------------|
| `int float string bool` | primitives                                   |
| `void`                  | no value (return type only)                  |
| `any`                   | anything (same as leaving the type out)      |
| `object range function type` | any record / range / callable / type    |
| `T[]`                   | array of T                                   |
| `tuple<A, B, ...>`      | tuple: exactly these elements, in this order (at least one) |
| `std:map<K, V>`, `std:set<T>` | hash map / set from `lib/std.pp`; bare `std:map` is any map |
| `T?`                    | `Option<T>`                                  |
| `A \|\| B`              | union                                        |
| `{ string name, int age }` | record                                    |
| `Name<A, B>`            | user type / enum / interface with type arguments |
| `a:b:Name`              | type exported by module `b` inside package `a` |
| `number`                | prelude alias for `int \|\| float`           |

`type Name = ...` and `type Pair<A, B> = { A first, B second }` define types.
Records are nominal when declared and structural otherwise. See
[DATATYPES_AND_FUNCTIONS.md](DATATYPES_AND_FUNCTIONS.md).

## Functions

```
function name(int a, b) -> int { ... }      // b is untyped
<T>function first(T[] xs) -> T? { ... }     // generic
const f = function(x) { return x * 2 }      // anonymous / closure
```

* Functions are values and closures. A `function` statement runs where it
  is written: the function exists from that line on (there is no hoisting).
  Names inside a body are looked up when the body runs, so two functions can
  call each other as long as both are written before the first call.
* **Declarations.** A function written without a body states its signature,
  like an interface requirement (parameter names are optional, so a lone name
  is a type). The definition must follow in the same block with the same
  types; the parameter names may differ:

  ```
  function a(int) -> float
  function b(int x) -> float { return a(x) }
  function a(int y) -> float { return b(y) }
  ```

  The declaration creates the function object, and the definition fills that
  same object in. So the function can be used before its body is written: an
  `impl` checked in between counts it, and a variable or call site that
  captured it gets the body. Calling it before the definition has run is an
  error. A `global` declaration needs a `global` definition.
* Return types are checked. `-> void` forbids returning a value. Omitting
  the return type allows anything.
* A function with no `return` returns `void`. Using `void` as a value is an
  error.
* Up to 4000 nested calls are allowed. Beyond that you get "stack overflow".

## Callbacks

A callback type describes a function by its parameter and return types. It
can only be written as a **named type**:

```
type ToInt = float -> int
type Pair = (int, int) -> int
type Thunk = () -> string
type Logger = (...) -> void          // any arguments
<T>type Consumer = T -> void          // same as: type Consumer<T> = T -> void

const float -> int f = ...           // syntax error: name the type first
```

Lambdas are short anonymous functions:

```
const ToInt cb = f -> int(f)
const Pair add = (a, b) -> a + b
const Pair mul = (int a, int b) -> {
    const product = a * b
    product                          // a block body gives its last expression
}
const ToInt builtin = int            // any function that fits works too
const ToInt mine = to_int_custom

cb(3.7)                              // 3
cb.call(3.7)                         // the same, through the built-in call(f, ...)

<T>function for_each(T[] items, Consumer<T> f) -> void {
    for x in items { f(x) }
}
[1, 2, 3].for_each(io:print)
[1, 2, 3].for_each(x -> io:print(x))
[1, 2, 3].for_each((x) -> { io:print(x) })
```

**Decisions:**
* A lambda stored with `const`/`let` needs a declared type: `const f = x -> x`
  is a syntax error. Passing a lambda as an argument needs no annotation.
* A value fits a callback type when it can be called with that many
  arguments, its declared parameter types accept the callback's parameter
  types, and its declared return type fits. For an overload set such as
  `io:print` or `int`, one fitting member is enough. Untyped parameters, `any`
  and generics accept anything. A `void` callback ignores the result.
* Overloads are chosen by the callback's shape: with `kind(ToInt f)` and
  `kind(Thunk f)`, `kind(() -> "x")` picks the `Thunk` one.
* A lambda takes its types from the callback type it is stored or passed as.
  `const ToInt cb = f -> int(f)` behaves like `(float f) -> int(f)` with return
  type `int`, and the copy is named after the variable, parameter or field:

  ```
  cb(2)        // 2: the int is widened to float like for any float parameter
  cb("12")     // error: no overload of 'cb' accepts (string)
               //          candidates: cb(float f) -> int
  ```

  Generic callback types use what the call bound: in `[1, 2].for_each(x -> ...)`
  with `Consumer<T>`, `x` is an `int`. A `void` callback type does not give the
  lambda a return type, so its result is simply ignored. Parameters you type
  yourself, `(int x) -> ...`, keep your type.
* Inside a match arm, `x -> ...` is the pattern `x` and its body, not a lambda.
  To return an object literal from a lambda, wrap it: `x -> ({ value: x })`.

## Control flow

```
if cond { } else if cond { } else { }
if some(x) = maybe() { }          // pattern binding, x is const
if let some(x) = maybe() { }      // x is mutable
while cond { break / continue }
for i in ...10 { }                // 0..9
for let i in 10...=1 { }          // 10..1, i mutable
for i, item in array { }          // index and item
for key, value in object { }      // also: for key in object
for ch in "héllo" { }             // code points
for i in range(0, 10, 3) { }
for x in range(0, 1, 0.25) { }    // 0.0 0.25 0.5 0.75
```

**Decision:** conditions must be `bool`. There is no truthiness.

`a...b` excludes the end and `a...=b` includes it. Both count down when
`a > b`. `...n` starts at 0. `...` and `...=` take ints. `range(start, end, step)`
takes numbers: all ints give an int range, and any float makes all three
floats. The k-th element of a float range is `start + k*step`, so rounding
errors don't add up across the steps. `r:start`, `r:end` and `r:step` read a range's fields.

`x in y` calls the function `in(x, y)`, which can also be called directly (`in(3, 0...3)`) or passed as a value (`const check = in`). `lib/std.pp` defines it for arrays
(`2 in [1, 2]`), strings (`"ell" in "hello"`) and ranges (`3 in 0...5`). For
ranges it checks only the bounds, in the range's direction: `0...5` is
`[0, 5)`, `5...=1` is `[1, 5]`, and `2.5 in 0...5` is true. `has(r, x)` asks
whether `x` is one of the elements a `for` loop visits instead, so
`2 in range(0, 10, 3)` is true but `has(range(0, 10, 3), 2)` is false. A module
can add more `in` overloads, like any other operator.

## match

```
const label = match value {
    0 -> "zero"
    -1 -> "minus one"
    some(n) if n > 10 -> "big"
    some(n) -> { const d = n * 2; str(d) }   // block: value of last expression
    Color:RED -> "red"
    none -> "nothing"            // a payload-less constructor works without ()
    x -> "anything else: " + str(x)          // a bare name binds
    _ -> "wildcard"
}
```

`if` and `match` are expressions. A block's value is its last expression.
Inside a statement-level `match`/`if`, `return`, `break` and `continue` work
normally. They are an error when the `match`/`if` is used as a value.
A `match` with no matching arm is a runtime error.

## Enums

```
enum Option<T> { some(T), none() }            // in the prelude
enum Error<S, F = string> { success(S), fail(F) }
enum Properties { NAME, VALUE, DATASET }
enum string Messages { NOTICE = "Notice", WARNING = "Warning" }
```

**Decisions:**
* A variant written **with parentheses** (`some(T)`, `none()`) gets a
  constructor function under its own name (`some(1)`), which takes part in
  overloading. It is also reachable as `Option:some`.
* A variant **without parentheses** is a constant reached through the enum:
  `Properties:NAME`.
* `enum_value(Messages:NOTICE)` returns the associated value ("Notice").
  Plain variants are numbered from 0.

Prelude helpers: `is_some`, `is_none`, `unwrap`, `unwrap_or`, `is_success`,
`is_fail` (overloaded for both `Option` and `Error`).

## Interfaces

An interface lists the functions a type must have. Inside the declaration,
the interface's own name stands for the implementing type:

```
interface Str {
    function str(Str) -> string
}

type Box = { float volume }
function str(Box box) -> string { return "Box(" + str(box:volume) + ")" }

impl Str for Box                     // checked now: str(Box) -> string exists

function show(Str s) -> string { return s.str() }
show(Box{ volume: 2.5 })             // any value whose type implements Str

<T>interface Container {
    function get(Container self, int index) -> T    // parameter names are optional
    function size(Container) -> int
}
impl Container<int> for IntList
function total(Container<int> c) -> int { ... }
```

A requirement can have a **default implementation**, and an interface can
**extend** another one with `impl Parent for Name` inside its body:

```
interface Equals {
    function ==(Equals a, Equals b) -> bool
    function !=(Equals a, Equals b) -> bool {     // used when the type has no != of its own
        return !(a == b)
    }
}

interface Comparator {
    function <(Comparator, Comparator) -> bool
    function <=(Comparator, Comparator) -> bool
    function >(Comparator a, Comparator b) -> bool { return !(a <= b) }
    function >=(Comparator a, Comparator b) -> bool { return !(a < b) }
    impl Equals for Comparator                     // every Comparator is an Equals
}

impl Comparator for Money    // needs ==, <, <=; gets !=, >, >= from the defaults
```

An `impl` can have generic parameters of its own. They tie the type's
parameters to the interface's:

```
<T>type Stack = { T[] items }
<T>impl std:Index<int, T> for Stack<T>          // indexing a Stack<string> gives strings
<K, V>impl Index<K, V> for map<K, V>            // in lib/std.pp
```

The impl's generics are bound by the value being checked, so a parameter typed
`std:Index<string, int>` accepts a map whose keys are strings and values ints. A
default implementation copied for a generic impl gets `any` in their place.

**Decisions:**
* **Interfaces are closed.** Unlike a type, which can gain overloads at any
  time, an interface cannot get more functions after it is declared.
  Declaring the same interface again in the same scope is an error.
* `impl` runs like any statement. It checks every requirement at that moment
  and fails with `missing function str(Box) -> string` otherwise. Functions
  are searched where the `impl` is written, among `global` functions, and in
  the exports of the module that declared the type. Only functions written (or
  declared, see [Functions](#functions)) before the `impl` count.
* `impl` **records** the function it found for each requirement on the type.
  A dot call `v.view()` searches those functions after the call site's scope
  and the exports of the type's module, so a module that only knows the
  interface can call the method on any value whose type implements it, even
  when the function is not exported. Plain calls (`view(v)`) are unaffected.
* A requirement parameter typed with the interface name needs a function
  **written for the type**. Catch-alls such as the built-in `str(any)` or an
  untyped parameter do not count. A required return type must be declared and
  must fit.
* Records, enums and types from other modules (`impl Str for shapes:Rect`) can
  implement interfaces. Records match nominally: the value must be tagged with
  the type (`Box{...}`, `const Box b = ...`, `-> Box`).
* An interface parameter scores below an exact record type, so
  `describe(Box b)` beats `describe(Str s)` for a `Box`.
* **Default implementations** are used only for requirements the type has no
  function for (searched as above, plus functions earlier `impl`s recorded for
  the type). The default is copied for the type: the interface name (and the
  interface's type parameters) in its signature become the impl's types, and
  its body runs in the scope where the interface was declared. It is recorded
  on the type like a found function, so `a != b` and `a.!=(b)` reach it from
  any module. For a type without a declaration (`impl Str for int`) the
  default is declared where the `impl` is written. Its parameters need names.
* **Extension.** `impl Equals for Comparator` inside `Comparator` means
  `impl Comparator for X` also runs `impl Equals for X` (after Comparator's own
  requirements are checked). If X already implements Equals, that one is kept;
  writing `impl Equals for X` after `impl Comparator for X` is an
  "already implements" error. A parameter typed `Equals` accepts a value typed
  `Comparator`. The parent must be declared before the interface; type
  arguments may use the interface's generics (`impl Bag<T> for Sack`).
* **Binary operators** search like dot calls: `a == b` also sees the exports of
  the module that declared `a`'s type and the functions `impl` recorded for it.
  That is how a default declared in one module (`!(a == b)`) finds the `==`
  written for the type in another.

## Objects and arrays

JSON syntax: `{ name: "x", "quoted key": 1, shorthand }` and `[1, 2, 3]`.
Both are reference values. Use `copy(x)` for a shallow copy.
Access is `o:field`, `o["field"]`, `xs[0]` or `xs[-1]`. `io:print`
pretty-prints objects as JSON.

### Indexing is a function

`c[a]` calls the function `[]` with `(c, a)`, and `c[a] = v` calls `[]=` with
`(c, a, v)`. The brackets are the function's name, like `+` is an operator's. The
built-in overloads cover arrays, strings, objects and tuples, and any module can
add more:

```
type Matrix = { int cols, float[] cells }
function [](Matrix m, int row, int col) -> float { return m:cells[row * m:cols + col] }
function []=(Matrix m, int row, int col, float v) -> void { m:cells[row * m:cols + col] = v }
mx[1, 0] = 3.0                             // several indices: []=(mx, 1, 0, 3.0)

<T>function [](T[] items, range r) -> T[] { return items.slice(r:start, r:end) }
xs[1...3]                                  // a slice
```

* Like a binary operator, `c[a]` also sees the functions of the module that
  declared `c`'s type and the ones `impl` recorded for it, so a type that
  implements `std:Index` can be indexed in every module.
* `[]` and `[]=` are names only where a name is expected: after `function`, after
  `.` (`xs.[](0)`, `xs.[]=(0, 9)`), after `:`, and in import / export lists. In
  an expression, `[]` is an empty array, so `[](xs, 0)` calls an array. Write
  `xs.[](0)` instead.
* A type with `[]` but no `[]=` is read-only: `t[0] = 1` on a tuple is an error.
* When no user code declares a `[]` / `[]=` anywhere, indexing a built-in
  collection skips the overload lookup.

### `std:Index`

`lib/std.pp` declares what indexing means for collections that have keys:

```
export <K, V>interface Index {
    function find(Index collection, K key) -> V?
    function put(Index collection, K key, V value) -> void
    function [](Index collection, K key) -> V { ... }              // find(key), or an error naming the key
    function []=(Index collection, K key, V value) -> void { ... } // put(key, value)
}
<K, V>impl Index<K, V> for map<K, V>
```

A type only writes `find` and `put`; `[]` and `[]=` come from the defaults:

```
type Board = { int width, string[] cells }
function find(Board b, tuple<int, int> at) -> string? { ... }
function put(Board b, tuple<int, int> at, string value) -> void { ... }
impl std:Index<tuple<int, int>, string> for Board

board[tuple(2, 1)] = "@"
<K, V>function show(std:Index<K, V> c, K key) -> string { return str(c[key]) }
```

A missing key is the error `map has no key "b" (find(key) gives an Option instead)`.

**Decision:** `#s`, `s[i]` and `slice` work on bytes. `for` and `chars()` work on
UTF-8 code points.

## Tuples

`tuple` is a built-in type whose type arguments are its elements, so they are
required: `tuple<int>`, `tuple<string, float>` and `tuple<string, number, int, float>`
are types, and plain `tuple` is a syntax error.

```
const tuple<string, int> entry = tuple("john", 22)
entry[0]                     // "john"; entry[-1] is 22
#entry                       // 2
entry[0] = "x"               // error: a tuple cannot be changed

match entry {
    tuple("root", _) -> "admin"
    tuple(name, age) if age >= 18 -> name + " (adult)"
    _ -> "?"
}
if tuple(x, y) = point { ... }
for i, item in entry { ... }

<K, V>function swap(tuple<K, V> p) -> tuple<V, K> { return tuple(p[1], p[0]) }
```

**Decisions:**
* A value fits `tuple<A, B>` only if it has **exactly** two elements and each one
  fits. Dispatch looks at every element (a tuple is short), and the score is 2
  plus the weakest element's, so `tuple<int, string>` beats `tuple<any, any>`.
* `tuple(...)` builds one from at least one value. In a pattern, `tuple(a, b)`
  matches a tuple of exactly two elements.
* Tuples are **immutable** values. `==` compares them element by element, and
  they can be map keys and set elements (`tuple(x, y)` as a grid coordinate).
  `copy(t)` returns `t`. They print as `tuple(1, "a")`.

## Maps and sets

`lib/std.pp` provides two hash containers, written in C:

```
import std

let std:map<string, int> ages = std:map()
ages.put("john", 22)
ages["anna"] = 31                   // the same as put (std:Index)
ages["anna"]                        // 31; a missing key is an error
ages.find("bob")                    // none(): find returns an Option
ages.find("bob").unwrap_or(0)       // 0
ages["anna"] += 1                   // 32
"john" in ages                      // true, also ages.has("john")
ages.remove("john")                 // some(22), none() if it was not there
for name, age in ages { }           // for name in ages: keys only
ages.keys(), ages.values()          // arrays, in insertion order
ages.entries()                      // [tuple("anna", 31)]

const std:set<int> seen = std:set([1, 2, 2])     // set{1, 2}
seen.add(3)                         // true; false when it was already there
seen.remove(1)                      // true; false when it was not there
2 in seen, seen.has(2)
a | b, a & b, a - b                 // union, intersection, difference (also a.union(b), ...)
```

| | `std:map<K, V>` | `std:set<T>` |
|-|-----------------|--------------|
| create | `std:map()`, `std:map([tuple(k, v), ...])`, `std:map({ a: 1 })`, `std:map(other)` | `std:set()`, `std:set([...])`, `std:set(0...10)`, `std:set(other)` |
| read | `find(m, k) -> V?`, `m[k]`, `has`, `k in m` | `has`, `x in s` |
| change | `put(m, k, v)`, `m[k] = v`, `remove(m, k) -> V?`, `clear` | `add(s, x) -> bool`, `remove(s, x) -> bool`, `clear` |
| all | `keys`, `values`, `entries -> tuple<K, V>[]`, `#m`, `for k, v in m` | `values -> T[]`, `#s`, `for x in s`, `for i, x in s` |

**Decisions:**
* The functions are `global` in `lib/std.pp`, so after `import std` they work in
  every file, as dot calls and as plain calls (`put(m, k, v)`).
* **Keys must be values that cannot change**: bool, int, float, string, enum
  values and tuples of those. An array, object, map or set as a key is a runtime
  error that suggests a tuple. Keys are compared with `==`, so `1` and `1.0` are
  the same key.
* Both keep **insertion order** (printing, `for`, `keys()`). Putting an existing
  key keeps its place. `==` ignores the order. They print as `map{"a": 1}` and
  `set{1, 2}`.
* Like arrays, dispatch looks only at the first entry, and a declaration
  (`const std:map<string, int> m = ...`) checks them all. A generic `K` is bound
  by the first key, so `m.find(1.0)` and `m[1.0]` on a map of ints find no
  overload, just as `1.0 in [1]` doesn't.
* Adding or removing entries while a `for` loop walks the same map is allowed,
  but which of the new entries the loop visits is not specified.
* The types are declared in `lib/std.pp` with `native type map<K, V>`, so they are
  written `std:map<K, V>`. Importing them by name (`import std -> { map }`) would
  hide the built-in `map(array, f)` in that file, so use the `std:` prefix.
* Interfaces work as for other types: `impl std:Str for std:map`,
  `impl std:Str for tuple<int, int>`. A map implements `std:Index`, which is
  where `m[k]` and `m[k] = v` come from.

## Arithmetic

* `int op int` gives an `int`, with 64-bit wrap-around. Mixing in a float gives a float.
* **Decision:** `/` on two ints truncates (like C). `%` takes the sign of the
  divisor (like Python). Division by zero is an error for ints and gives `inf` for floats.
* `==` compares structurally (arrays, objects, enum values). `1 == 1.0`.

## Modules

```
import io                               // library module (native or lib/io.pp)
import io -> console                    // alias: console:print(...)
import "./geometry"                     // file next to this one, .pp optional
import "./geometry" -> { vec, +, Vec }  // bring exports in; + extends the local overload set
import os:fs                            // submodule: export `fs` of os, or os/fs.pp
import "./ui":shapes -> { rect }        // works for files too
export import "./ui/shapes"             // import and re-export in one line (an import)

export type Vec = { float x, float y } // export in front of a declaration, anywhere in the code
export function length(Vec v) -> float { ... }

export { vec, ORIGIN -> zero }          // or list what importers may see, last in the file
export SCALE -> factor
```

A file is laid out as **imports, then code, then exports**. An `import` (or
`export import`) after any other statement, an `import` inside a block, or a
statement after an export list is a syntax error. Export lists name symbols,
so they can list anything declared above them.

`export` written in front of a declaration (`type`, `enum`, `interface`,
`function`, `global function`, `const`, `let`) is part of the code, not of the
export list: it runs the declaration and exports its name, exactly like
listing the name at the end. Exporting a function declaration
(`export function f(int) -> int`) exports every overload of `f`, since
exports are by name.

A package is a module that re-exports its submodules, so importers write
`ui:shapes:rect(1, 2)` and `const ui:shapes:Rect r = ...`.

Only exported symbols are visible from outside. A module bound to a name is
used like an object: `geometry:vec(1, 2)`, `const geometry:Vec v = ...`.

Dot syntax does not need the module prefix for functions exported by the
module that declared the receiver's type: `r.area()` works like
`r.shapes:area()`, and like `r.ui:shapes:area()` when shapes is a submodule. A top-level function declared `global` is visible in every
file once its module has been loaded:

```
global function ==(Rect a, Rect b) -> bool { ... }   // in shapes.pp

import "./shapes" -> shapes                         // in another file
shapes:rect(2, 3) == shapes:rect(2, 2)              // false, no import of == needed
```
A library module in `lib/` can leave function bodies to C. The `.pp` file
declares the signature with `native` (no body), and the interpreter binds it
to the C function the module registered:

```
export type Vec = { float x, float y }
native function hypot(float, float) -> float     // private helper, body in C
export function length(Vec v) -> float { return hypot(v:x, v:y) }
export native "vec_scale" function scale(Vec, float) -> Vec
```

See [PACKAGES.md](PACKAGES.md#mixed-modules-pp--c) for the C side.

See [PACKAGES.md](PACKAGES.md) for lookup rules and the `io` module.

## Errors

Runtime errors print `file:line: error: message` and a stack traceback, then
exit with code 1. Syntax errors exit with code 65. There is no `try` yet.
Recoverable failures use `Error<S, F>` values.
