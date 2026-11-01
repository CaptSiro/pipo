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
* Keywords: `import export function return const let if else for in while
  break continue match enum true false`. `type`, `precedence`, `over`, `under`
  and `as` are contextual and can still be used as names.

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
| `Name<A, B>`            | user type / enum with type arguments         |
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
```

Only exported symbols are visible from outside. A module bound to a name is
used like an object: `geometry:vec(1, 2)`, `const geometry:Vec v = ...`.
See [PACKAGES.md](PACKAGES.md) for lookup rules and the `io` module.

## Errors

Runtime errors print `file:line: error: message` and a stack traceback, then
exit with code 1. Syntax errors exit with code 65. There is no `try` yet.
Recoverable failures use `Error<S, F>` values.
