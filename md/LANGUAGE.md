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
* Keywords: `import export global function return yield emit const let if else for in
  while break continue match enum true false`. `type`, `interface`, `impl`,
  `precedence`, `over`, `under`, `as`, `native`, `scope` and `generator` are
  contextual and can still be used as names.

### Operators are names

Any run of `+ - * / % < > = ! & | ^ ~ # ? @ $ \` and `..` is a symbolic name.
`=` (assignment) and `->` are reserved. `&&` and `||` are short-circuiting
and cannot be overloaded: `function &&(...)` is a syntax error. The keywords
cannot name a function either (`'if' is a keyword and cannot be a function name`),
except `in`, which is the operator `x in y`.

`.` and `,` are never part of a declared name. `.` is reserved for dot calls
anywhere in a name: with a function `..`, `x..f()` would read as `x .. f()`, and
`a.b` already means `b(a)`. `,` separates arguments and elements, so a function
`,` would make `[1, 2]` ambiguous. `function ..`, `function +..`, `function ,`
are syntax errors. The ranges `...` and `...=` are the exception: they are
operators like any other and can be overloaded for your own types
(`function ...(Day a, Day b) -> Day[]`). Whether `..` can become a name too is
left open for later.

`<` and `>` cannot be declared with **one** parameter: a prefix `<` or `>` does
not exist. A `<` in operand position is reserved for tag calls
([TAG_CALLS.md](TAG_CALLS.md)), and `>` is reserved with it so the pair stays
symmetric. `function <(Money a, Money b) -> bool` (two parameters) is an
ordinary comparison and stays overloadable; `function <(int x) -> int` is a
syntax error.

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

### Tag calls

A tag is one more way to write a call, for calls with named arguments and nested
content (markup, configuration, trees). The design is in [TAG_CALLS.md](TAG_CALLS.md).

| written | calls |
|---------|-------|
| `<f/>` | `f()`, or `f({})` when no `f` takes zero arguments |
| `<f a="text" b=3 c={expr} d/>` | `f({ a: "text", b: 3, c: expr, d: true })` |
| `<f a={x}>"text" {y} <g/></f>` | `f({ a: x }, ["text", y, g()])` |
| `<f>"text"</f>` | `f(["text"])` when an `f` takes exactly one array, else `f({}, ["text"])` |
| `<f a={x}></f>` | `f({ a: x }, [])`: an empty body is still a body |
| `<m:f/>`, `<m:sub:f>...</m:sub:f>` | `m:f()`, `m:sub:f([...])` |

```
const page = <h:div class={some("card")}>
    <h:p>"Hello, " {name} "!"</h:p>
    <h:br/>
</h:div>
```

* The tag is a function name (or a module path, or a record type: `<Person age={1} name="x"/>`
  is `Person({ ... })`), found like any call. The language knows no HTML.
* Attribute values are literals (a string, a number, `true`, `false`) or `{expression}`; an
  attribute without a value is `true`. Attribute names may be kebab-case: `aria-label` is
  the field `aria_label`.
* Text is a string literal, kept exactly: there is no whitespace rule. Between children,
  whitespace and comments are layout.
* `/>` and a body are different calls. A body given to a function without children, or `/>`
  on one that needs them, is an error that says which to write.
* A `<` in operand position followed by a name with no space is a tag; `a < b` and `a <b`
  (after an operand) stay comparisons. A function `<` or `>` with one parameter is
  therefore a syntax error.

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
| `unknown` (or `any`)    | any value, boxed with its type; opened with `x as T` ([STATIC_TYPING.md](STATIC_TYPING.md)) |
| `object function type` | any record (fields are `unknown`) / callable / type |
| `Range<T>`              | an interval of `T`: `Range<int>`, `Range<float>`, `Range<string>`, `Range<Day>`; bare `Range` is any range ([Ranges](#ranges)) |
| `T[]`                   | array of T                                   |
| `tuple<A, B, ...>`      | tuple: exactly these elements, in this order (at least one) |
| `Map<K, V>`, `Set<T>` | built-in hash map / set; bare `Map` is any map; made by `map(...)` / `set(...)` |
| `types:Int8Array` ... `types:Float64Array` | fixed-length typed arrays from `lib/types.pp`, made by `types:int8array(length)` ... (see [PACKAGES.md](PACKAGES.md#the-types-module)) |
| `T?`                    | `Option<T>`                                  |
| `A \|\| B`              | union                                        |
| `{ string name, int age }` | record                                    |
| `Name<A, B>`            | user type / enum / interface with type arguments |
| `a:b:Name`              | type exported by module `b` inside package `a` |
| `number`                | prelude alias for `int \|\| float`           |

`type Name = ...` and `type Pair<A, B> = { A first, B second }` define types.

### Tagged aliases

`type X = T` where `T` is not a record, a union or a function type (`int`, `float`, `string`,
`bool`, `Range`, an array, a tuple, an enum such as `T?`, `Map`, a native type, or another tagged
alias) makes a **tagged alias**: a value of `X` *is* a `T` (same representation, no wrapper, no
cost), but the checker sees it as an `X`:

```
type Meters = float
type Seconds = float
function str(Meters m) -> string { return str(m + 0.0) + " m" }   // m + 0.0 is a float

const Meters d = 3.0       // a float converts to Meters
str(d)                     // "3.0 m": str(Meters) wins over str(float)
str(2.5)                   // "2.5": a plain float stays a float
const float f = d          // typed as float again: str(f) is "3.0"
d + 1.0                    // a float: operators of T give T unless one is declared for Meters
speed(t, d)                // error when speed takes (Meters, Seconds): tags of one base don't mix
```

* **Overloads.** An overload for `X` beats one for `T` (and is used for `T` only when nothing else
  takes the value: converting `T` to `X` is the weakest fit). `impl Iface for X` covers `X`, not
  `T`.
* **Chains.** `type B = A` over a tagged `A` puts `B` below `A`: a `B` fits where an `A` or a `T`
  is expected, and the overload closest to the value's tag wins (for a `C` of `type C = B`:
  `f(C)`, then `f(B)`, `f(A)`, `f(T)`). A value converts up and down its chain, never to another
  chain (`Meters` to `Seconds`).
* **Joins.** Branches of different tags (`if c { aC } else { anA }`) give the union `C || A`;
  the overload then follows the value's own tag at runtime.
* **Inference** keeps the tag (`const b = d` is a `Meters`), generics bind it (`T = Meters`).
* **Cycles** are an error, used or not: `type A = B` with `type B = A` is
  `cycle detected in type 'A': A -> B -> A`; so is `type Tree = Tree[]` (a record is how a type
  contains itself).

**Cost.** None where the static type is known: the checker picks the overload, so a call on a
`Meters` compiles to the same direct call as one on a `float`. A value only carries its tag where
its static type is lost (an `unknown`, a union, an interface, a call dispatched at runtime): the
bytecode VM keeps a 32-bit tag in what was padding of every value and writes it at those points,
wasm boxes already record the static type. Records stay nominal records (`type R = { ... }`);
unions and function types stay plain names for what they stand for.

### Record literals

A record literal converted to a record type (an argument, a typed declaration, `Type({ ... })`):

* may leave out the fields whose type is `T?`: they are `none()` (`type Opts = { int? size }`
  accepts `{}`). Values for them are still written `some(x)`;
* may write a field `a_b` as `a-b` (that is how tag attributes such as `aria-label` arrive);
* may **not** name a field the type does not have: `Props has no field 'lable' (did you mean
  'label'?)`. Only literals are checked this way; a value that already has a record type
  still matches structurally.

**Rest fields.** A record type may end with one rest field, `...Map<string, V> name`. The
fields a literal has and the type does not name go into that map, each one a `V`:

```
type DivAttrs = { string? id, ...Map<string, string> attrs }
div({ id: some("main"), role: "button" })         // attrs is Map{"role": "button"}
<div id={some("x")} data-user-id="42"/>           // attrs is Map{"data-user-id": "42"}
```

**A record as a map.** Where a `Map<string, V>` is expected (a parameter, a declaration,
an assignment, a return, a field), a record that is not of a named record type is a map of
its fields, each one a `V`. This is the most general form of a function taking named values,
and the first step before a typed version:

```
function element(Map<string, unknown> attrs) -> string { ... }
<element tag="my-widget" size={3} data-user-id="u1"/>   // attrs: Map{"tag": ..., "size": 3, "data-user-id": "u1"}
count({ a: 1, b: 2 })                                    // count(Map<string, int> m)
```

It is the weakest fit, so an overload that takes the record itself (`object`, a record type)
wins. A value of a named record type stays a record.

**Generics through record fields.** A generic record type binds its parameters from the
literal's fields, and lambdas in the literal (or in an array literal) take their types from
it: with `<T, K>function For(ForProps<T, K> p, ItemView<T>[] children)`,
`<For each={todos} key={t -> t:id}>{t -> t:title}</For>` binds `T = Todo` from `each`,
types `t` in both lambdas as a `Todo`, and binds `K = int` from the key's result.

**`decode(m, T)`** goes the other way: it checks a `Map<string, unknown>` against the record
type `T` and gives an `Error<T>`: `success(record)`, or `fail(why)` with the path of the first
mismatch (`Page: field 'card': Card: field 'title': expected string but got int`). A missing
`T?` field is `none()`, a plain value for a `T?` field becomes `some(value)`, an int fills a
float field, a nested map fills a field of a record type, the leftovers fill the rest field
(or are an error without one).

Records are nominal when declared and structural otherwise. See
[DATATYPES_AND_FUNCTIONS.md](DATATYPES_AND_FUNCTIONS.md).

## Functions

```
function name(int a, b) -> int { ... }      // b is untyped
<T>function first(T[] xs) -> T? { ... }     // generic
const f = function(x) -> int { return x * 2 }   // anonymous / closure
```

* **Every function states its return type**, `-> void` when it returns
  nothing. `function f() { }` is a syntax error (`function 'f' needs a return
  type`). The rule covers named and anonymous functions, declarations,
  `native` functions, interface requirements and scopes (`scope _s(...) -> T`);
  generators already name the type of their values. Lambdas (`x -> x * 2`) are
  exempt: they take their type from where they are used. **Why:** a signature
  is the contract the checker, the targets and the reader rely on; inferring
  it from the body hides it, and a typo in a `return` silently changes it.
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
    return product                   // a block body is a function body: it returns
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
for key, value in map { }         // a map's entries; for k in map.keys() / for v in map.values()
for name, value in fields(r) { }  // a record's fields
for i, ch in "héllo".enumerate() { } // anything else is counted with enumerate()
for ch in "héllo" { }             // code points
for i in range(0, 10, 3) { }
for x in range(0, 1, 0.25) { }    // 0.0 0.25 0.5 0.75
for x in fib() { }                // a generator, or any type that implements Iterable<T>
```

`for` iterates an `Iterable<T>` (see [Generators and iteration](#generators-and-iteration)).
The form with two variables needs keys: an array or a tuple (index, item), a map
(key, value), or a `KeyIterable` / a generator of pairs. Over anything else
(a range, a string, a set) it is an error that suggests `.enumerate()`. A map
alone (`for x in map`) and a record are errors too: write `map.keys()`,
`map.values()` or `fields(record)`.

**Decision:** conditions must be `bool`. There is no truthiness.

### Ranges

`a...b` excludes the end and `a...=b` includes it. Both count down when
`a > b`. `...n` starts at 0. With ints they make a `Range<int>`; with a float
(`0.5...=2.5`) a `Range<float>` that steps by 1.0. `range(start, end, step)`
takes numbers: all ints give an int range, and any float makes all three
floats. The k-th element of a float range is `start + k*step`, so rounding
errors don't add up across the steps. `r:start`, `r:end`, `r:step` and
`r:inclusive` read a range's fields; `end` is the end as written, so
`(0...=5):end` is `5` and `(0...=5):inclusive` is true. Ranges print as written
(`0...=5`, `"a"...="e"`).

**`Range<T>` of anything else.** `...` and `...=` also make a range of any other
type: `"a"...="e"` is a `Range<string>`, `mon...=fri` a `Range<Day>`. Such a
range is an interval: it has `start`, `end` and `inclusive` but no step and no
length. What else it can do comes from `T`:

| | needs | built in for |
|-|-------|--------------|
| `x in r` | `<` and `<=` of `T` (a user type: `impl Comparator`) | numbers, strings |
| `for x in r` | `impl Iterable<T> for Range<T>` | numbers, strings (code points: `"a"...="e"` is a, b, c, d, e) |

```
type Day = { int n }
// ==, <, <= for Day, then:
impl std:Comparator for Day                   // gives `in`

generator iter(Range<Day> r) -> Day {         // gives `for`
    for i in r:start:n...r:end:n { emit Day{ n: i } }
    if r:inclusive { emit r:end }
}

impl Iterable<Day> for Range<Day>
```

A string range's bounds must be one code point each to iterate (`"aa"..."c"` is
an error at the loop). `"cc" in "a"...="e"` is true: `in` compares strings, so
it asks "between", not "one of". `for` over a range whose type has no
`Iterable` is a compile error that says what to write. `...` and `...=` are
operators like any other, so a type can instead return a type of its own
(`function ...(Date a, Date b) -> DateSpan`); a specific overload wins over the
generic one. The design is in [RANGE.md](RANGE.md).

`x in y` calls the function `in(x, y)`, which can also be called directly (`in(3, 0...3)`) or passed as a value (`const check = in`). `lib/std.pp` defines it for arrays
(`2 in [1, 2]`), strings (`"ell" in "hello"`) and ranges (`3 in 0...5`). For
ranges it checks only the bounds, in the range's direction: `0...5` is
`[0, 5)`, `5...=1` is `[1, 5]`, and `2.5 in 0...5` is true. `has(r, x)` asks
whether `x` is one of the elements a `for` loop visits instead, so
`2 in range(0, 10, 3)` is true but `has(range(0, 10, 3), 2)` is false. A module
can add more `in` overloads, like any other operator.

## Scopes

A scope is a function that takes a block of code, so a library can define its
own control flow. Names starting with `_` belong to scopes only: functions and
variables cannot use them, and every `_name` in code is a scope call.

```
scope _resource(Block<void> code, Resource r) {
    defer { r.close() }          // cleanup: runs when the scope function ends
    code()                       // runs the block
}

_resource const f = fs:file("./a.txt") {
    io:print_nl(f.read_text())   // f exists only in the block, like `if some(x) = ...`
}

scope _times(Block<int> code, int n) {
    for i in ...n {
        code(i)                  // the block sees the value as `scope`
    }
}

_times 3 { io:print_nl(scope) }
_times(3) { io:print_nl(scope) }
```

* The first parameter of a scope is its block, `Block<T>` or `Callback<T>`
  (`Block` alone is `Block<void>`). The call's arguments fill the other
  parameters, and overloads are resolved as for functions. An argument written
  `const name = value` (or `let`) passes the value and binds `name` in the
  block.
* `code()` / `code(value)` runs the block in a fresh scope where `scope` is the
  value. It returns what the block gives with `yield` (see [Block values](#block-values-yield)),
  or nothing when the block has no `yield`. To reach the value of an outer
  block from a nested one, store it first: `const outer = scope`.
* `return`, `break` and `continue` in a `Block` act on the code around the
  scope call, as they would inside an `if`. `lift()` returns from the
  enclosing function the same way. The scope function stops at once, its
  `defer` runs, and then the exit continues.
* `defer { ... }` is only allowed directly in a scope's body. There is a single
  cleanup slot per call, and each `defer` replaces it. The current one runs
  when the scope function ends, normally or by an early exit. The cleanup
  cannot `return`, `break` or `continue`.
* A `Block` can only run while its scope call runs: calling it later is an
  error. A `Callback` can be kept (`handlers.push(code)`) and called after the
  scope call ended, so it cannot `return`, `break`, `continue` or lift out of
  anything.
* A scope call is an expression: its value is what the scope function
  returned (`const x = _twice 3 { yield scope * 10 }`). Used as a value, its block
  cannot leave early.
* A module exports a scope like a function (`export scope _bind(...)`), and
  other modules call it through the module: `gl:_bind vao { ... }`,
  `ui:shapes:_clip(r) { ... }`. Write the path without spaces. Importing the
  name (`import "./gl" -> { _bind }`) also works.

## match

```
const label = match value {
    0 -> "zero"
    -1 -> "minus one"
    some(n) if n > 10 -> "big"
    some(n) -> { const d = n * 2; yield str(d) }   // a block arm gives its value with yield
    Color:RED -> "red"
    none -> "nothing"            // a payload-less constructor works without ()
    x -> "anything else: " + str(x)          // a bare name binds
    _ -> "wildcard"
}
```

`if` and `match` are expressions. An arm written without braces (`0 -> "zero"`)
is an expression and gives its value. The block of a match arm gives a value only
with `yield`; the blocks of an `if` give their last expression, like a ternary
(`if c { 1 } else { 2 }`).
Inside a statement-level `match`/`if`, `return`, `break` and `continue` work
normally. They are an error when the `match`/`if` is used as a value.
A `match` with no matching arm is a runtime error.

### Block values: `yield`

`yield expr` ends the block of a match arm or a scope call and makes `expr` that
block's value. It is how such a block gives a value; the last expression of such
a block is **not** its value. `if` is exempt: its branches give their last
expression, so `if` stays usable as a ternary.

```
const parity = if n % 2 == 0 { "even" } else { "odd" }      // an if: no yield

const label = match n {
    0 -> "zero"                              // an expression arm needs no yield
    x if x > 5 -> {
        const big = x * 100
        yield "big " + str(big)
    }
    _ -> "small"
}

const total = _with 2 {                      // code(2) in _with returns 21
    let sum = 0
    for i in ...scope { sum += i + 10 }
    yield sum
}

const sign = match n {                       // an if inside an arm is yielded as a whole
    0 -> 0
    _ -> { yield if n > 0 { 1 } else { -1 } }
}
```

**Decisions:**
* `yield` must be the **last** statement of the block it is written in, and
  that block must be the block of a match arm or a scope call. Anywhere else (an
  `if`, a function or lambda body, a loop body, a plain `{ }`) it is a syntax
  error. It does not leave the block early and does not reach an outer block:
  to give a value from a nested `if`, yield the `if` itself.
* **`if` is exempt** because it is the ternary operator (`if c { a } else { b }`),
  and because a `yield` inside an `if` inside a loop would have no clear target
  (the loop has no value to give).
* A block without `yield` gives nothing. Used as a value, that is a type error
  which points at the block's last expression when there is one:
  `this block is used as a value but gives none: write 'yield' before its last expression`.
* Functions and lambdas return with `return`. A lambda with an expression body
  (`x -> x * 2`) gives that expression; a lambda with a block body is a
  function body and needs `return`.
* `yield` at the end of a statement-level `match` / scope call is allowed; the
  value is unused.
* Why a keyword and not the last expression: a value is never produced by
  accident (a trailing call that happens to return something), the place a
  block's value comes from is visible when reading, and moving or adding a
  statement at the end of a block cannot change its value.

## Generators and iteration

A generator function is declared like a function, with `generator` instead of
`function`; the type after `->` is the type of its values. Calling it builds a
`Generator<T>` without running anything; `next()` runs the body to its next
`emit`:

```
generator fib() -> int {
    let int a = 0
    let int b = 1
    while true {
        emit b                    // gives b and pauses here
        const c = a + b
        a = b
        b = c
    }
}

const Generator<int> f = fib()    // nothing has run yet
f.next()                          // some(1), then some(1), some(2), ...
for x in fib().take(10) { }       // a generator is an Iter<int>
fib().map(x -> x * 2).take(3).to_array()   // [2, 2, 4]: map / filter / take are lazy
```

* `emit value` gives the next value and pauses. `return` ends the generator
  (`next()` gives `none()` from then on); `return value` gives a last value and
  ends it, the same as `emit value` followed by `return`. The end of the body
  ends it too.
* `for` iterates any `Iterable<T>`: arrays, ranges, strings, tuples, sets,
  generators, and types that implement the prelude's interfaces `Iter<T>`
  (`next(it) -> T?`), `Iterable<T>` (`iter(c) -> Iter<T>`) or `KeyIterable<K, V>`
  (`pairs(c) -> Iter<tuple<K, V>>`, for `for k, v`). The built-in collections are
  iterated directly, also when their static type is an interface.
* A generator keeps one frame: it cannot `emit` or `return` from the block of a
  scope call, and cannot use `lift()`.
* Every target runs generators: the interpreter and the bytecode VM pause the
  body; the wasm target compiles it into a step function that resumes at its
  last `emit` ([TARGETS.md](TARGETS.md)).

The whole design, with the reasons, is in [ITER.md](ITER.md).

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
`lift(x)` unwraps `some`/`success` and returns `none()`/`fail(..)` early from
the calling function (see BUILTINS.md).

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
<T>impl Index<int, T> for Stack<T>          // indexing a Stack<string> gives strings
<K, V>impl Index<K, V> for Map<K, V>            // built in (the prelude)
```

The impl's generics are bound by the value being checked, so a parameter typed
`Index<string, int>` accepts a map whose keys are strings and values ints. A
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

<T>function [](T[] items, Range<int> r) -> T[] { return items.slice(r) }   // ...= includes the end
xs[1...3]                                  // a slice
```

* Like a binary operator, `c[a]` also sees the functions of the module that
  declared `c`'s type and the ones `impl` recorded for it, so a type that
  implements `Index` can be indexed in every module.
* `[]` and `[]=` are names only where a name is expected: after `function`, after
  `.` (`xs.[](0)`, `xs.[]=(0, 9)`), after `:`, and in import / export lists. In
  an expression, `[]` is an empty array, so `[](xs, 0)` calls an array. Write
  `xs.[](0)` instead.
* A type with `[]` but no `[]=` is read-only: `t[0] = 1` on a tuple is an error.
* When no user code declares a `[]` / `[]=` anywhere, indexing a built-in
  collection skips the overload lookup.

### `Index`

The prelude declares what indexing means for collections that have keys
(built in, no import):

```
<K, V>interface Index {
    function get(Index collection, K key) -> V?
    function put(Index collection, K key, V value) -> void
    function [](Index collection, K key) -> V { ... }              // get(key), or an error naming the key
    function []=(Index collection, K key, V value) -> void { ... } // put(key, value)
}
<K, V>impl Index<K, V> for Map<K, V>
```

A type only writes `get` and `put`; `[]` and `[]=` come from the defaults:

```
type Board = { int width, string[] cells }
function get(Board b, tuple<int, int> at) -> string? { ... }
function put(Board b, tuple<int, int> at, string value) -> void { ... }
impl Index<tuple<int, int>, string> for Board

board[tuple(2, 1)] = "@"
<K, V>function show(Index<K, V> c, K key) -> string { return str(c[key]) }
```

A missing key is the error `Map has no key "b"`.

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

Two hash containers are built in (written in C, declared in the prelude; no
import needed):

```
let Map<string, int> ages = map()
ages.put("john", 22)
ages["anna"] = 31                   // the same as put (Index)
ages["anna"]                        // 31; a missing key is an error
ages.get("bob")                    // none(): get returns an Option
ages.get("bob").unwrap_or(0)       // 0
ages["anna"] += 1                   // 32
"john" in ages                      // true, also ages.has("john")
ages.remove("john")                 // some(22), none() if it was not there
for name, age in ages { }           // for name in ages.keys(), for age in ages.values()
ages.keys(), ages.values()          // arrays, in insertion order
ages.entries()                      // [tuple("anna", 31)]

const Set<int> seen = set([1, 2, 2])     // Set{1, 2}
seen.add(3)                         // true; false when it was already there
seen.remove(1)                      // true; false when it was not there
2 in seen, seen.has(2)
a | b, a & b, a - b                 // union, intersection, difference (also a.union(b), ...)
```

| | `Map<K, V>` | `Set<T>` |
|-|-----------------|--------------|
| create | `map()`, `map([tuple(k, v), ...])`, `map({ a: 1 })`, `map(other)` | `set()`, `set([...])`, `set(0...10)`, `set(other)` |
| read | `get(m, k) -> V?`, `m[k]`, `has`, `k in m` | `has`, `x in s` |
| change | `put(m, k, v)`, `m[k] = v`, `remove(m, k) -> V?`, `clear` | `add(s, x) -> bool`, `remove(s, x) -> bool`, `clear` |
| all | `keys`, `values`, `entries -> tuple<K, V>[]`, `#m`, `for k, v in m` | `values -> T[]`, `#s`, `for x in s`, `for i, x in s.enumerate()` |

**Decisions:**
* `Map`, `Set` and their functions are **built in**, like `Option` and `Error`:
  they work in every file without an import, as dot calls and as plain calls
  (`put(m, k, v)`). They used to live in `lib/std.pp`; they moved because the
  language itself relies on them (a record's rest field is a `Map`, see
  [TAG_CALLS.md](TAG_CALLS.md)).
* **Keys must be values that cannot change**: bool, int, float, string, enum
  values and tuples of those. An array, object, map or set as a key is a runtime
  error that suggests a tuple. Keys are compared with `==`, so `1` and `1.0` are
  the same key.
* Both keep **insertion order** (printing, `for`, `keys()`). Putting an existing
  key keeps its place. `==` ignores the order. They print as `Map{"a": 1}` and
  `Set{1, 2}`, and `type_of` gives `"Map"` / `"Set"`.
* Like arrays, dispatch looks only at the first entry, and a declaration
  (`const Map<string, int> m = ...`) checks them all. A generic `K` is bound
  by the first key, so `m.get(1.0)` and `m[1.0]` on a map of ints find no
  overload, just as `1.0 in [1]` doesn't.
* Adding or removing entries while a `for` loop walks the same map is allowed,
  but which of the new entries the loop visits is not specified.
* The types are declared in the prelude with `native type Map<K, V>`, so they are
  written `Map<K, V>` (types are PascalCase). A type is not callable:
  `Map()` and `Map{}` are errors. Values come from the constructor
  functions `map(...)` and `set(...)` (constructors are snake_case or
  flatcase). `map(...)` with one argument (or none) makes a map; `map(source, f)`
  with two is the lazy map over an `Iterable`. They are overloads of one name.
* Interfaces work as for other types: `impl types:Str for Map`,
  `impl types:Str for tuple<int, int>`. A map implements `Index`, which is
  where `m[k]` and `m[k] = v` come from.

## Arithmetic

* `int op int` gives an `int`, with 64-bit wrap-around. Mixing in a float gives a float.
* `int ** int` is an `int`; a negative exponent is an error (`2.0 ** -1` is `0.5`).
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
import io, os, "./geometry" -> geo, math -> { pi }, "./ui":shapes   // several modules, comma separated

export type Vec = { float x, float y } // export in front of a declaration, anywhere in the code
export function length(Vec v) -> float { ... }

export { vec, ORIGIN -> zero }          // or list what importers may see, last in the file
export SCALE -> factor
```

One `import` can name several modules separated by commas. Each one is written exactly as it
would be on a line of its own (a path, `:submodules`, then `-> alias` or `-> { names }`), and the
line means the same as those separate imports, in that order. A comma at the end of a line
continues the list on the next one; `export import a, b` re-exports both.

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

Any file can call a function of a DLL / shared library with `extern`: the
library, optionally the C symbol, then a declaration without a body. The `c`
module's aliases (`c:i32`, `c:u32`, `c:f32`, `c:ptr`, ...) give the C widths;
the library is loaded on the first call, from the declaring file's folder or the
system's search path:

```
import c
extern "glfw3" function glfwInit() -> c:i32
extern "opengl32" "glClearColor" function clear_color(c:f32 r, c:f32 g, c:f32 b, c:f32 a) -> void
```

See [EXTERN.md](EXTERN.md) for the types, the loading rules and the limits.

See [PACKAGES.md](PACKAGES.md) for lookup rules and the `io` module.

## Errors

Type errors are found before the program runs, also in code that never runs
(see [STATIC_TYPING.md](STATIC_TYPING.md)); they print `file:line: error: message`
and exit with code 65, like syntax errors. Runtime errors print the same and a
stack traceback, then exit with code 1. There is no `try` yet.
Recoverable failures use `Error<S, F>` values.
