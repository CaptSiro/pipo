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
  `precedence`, `over`, `under` and `as` are contextual and can still be used
  as names.

### Operators are names

Any run of `+ - * / % < > = ! & | ^ ~ # ? @ $ \` and `..` is a symbolic name.
`=` (assignment) and `->` are reserved. `&&` and `||` are short-circuiting
and cannot be overloaded.

**Decision:** a run of operator characters is split using the operators the
lexer knows about: built-ins, plus every `function <op>` and
`precedence <op> ...` in the same file. `...#array` becomes `...` `#`.
Operators declared only in another module are not known while this file is
lexed, so write them with spaces around them (`a <> b`).

`x += y` works for any operator `op` as `x = x op y`.

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

* Functions are values and closures. Declarations are **hoisted** within
  their block, so they can be called before the line they are declared on.
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
```

**Decision:** conditions must be `bool`. There is no truthiness.

`a...b` excludes the end and `a...=b` includes it. Both count down when
`a > b`. `...n` starts at 0.

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

**Decisions:**
* **Interfaces are closed.** Unlike a type, which can gain overloads at any
  time, an interface cannot get more functions after it is declared.
  Declaring the same interface again in the same scope is an error.
* `impl` runs like any statement. It checks every requirement at that moment
  and fails with `missing function str(Box) -> string` otherwise. Functions
  are searched where the `impl` is written, among `global` functions, and in
  the exports of the module that declared the type. Function declarations are
  hoisted, so a `function` later in the same block already counts.
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

## Objects and arrays

JSON syntax: `{ name: "x", "quoted key": 1, shorthand }` and `[1, 2, 3]`.
Both are reference values. Use `copy(x)` for a shallow copy.
Access is `o:field`, `o["field"]`, `xs[0]` or `xs[-1]`. `io:print`
pretty-prints objects as JSON.

**Decision:** `#s`, `s[i]` and `slice` work on bytes. `for` and `chars()` work on
UTF-8 code points.

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

export { vec, length, Vec }             // what importers may see
export SCALE -> factor

import os:fs                            // submodule: export `fs` of os, or os/fs.pp
import "./ui":shapes -> { rect }        // works for files too
export import "./ui/shapes"             // import and re-export in one line
```

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
See [PACKAGES.md](PACKAGES.md) for lookup rules and the `io` module.

## Errors

Runtime errors print `file:line: error: message` and a stack traceback, then
exit with code 1. Syntax errors exit with code 65. There is no `try` yet.
Recoverable failures use `Error<S, F>` values.
