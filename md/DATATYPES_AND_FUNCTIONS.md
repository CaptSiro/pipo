# Datatypes, Bound Functions and Interfaces

This document describes how behaviour is attached to datatypes. It is the
foundation that interfaces will be built on later.

## The core idea

There are no classes and no methods stored inside types. A programmer
defines a **datatype**, then **binds functions to it** by writing ordinary
functions whose **first parameter** has that type:

```
type Rect = {
    number width,
    number height,
}

function area(Rect r) -> number {
    return r:width * r:height
}

function scale(Rect r, number factor) -> Rect {
    return Rect({ width: r:width * factor, height: r:height * factor })
}
```

Both calling styles mean exactly the same thing:

```
area(r)          // classic
r.area()         // dot syntax: the receiver becomes the first argument

scale(r, 2)
r.scale(2)
```

`r.f(a, b)` is rewritten to `f(r, a, b)`. The function is looked up by name
in the scope of the call. Nothing is stored on the value itself. So:

* any module can add "methods" to any type, including built-in ones:
  `function shout(string s) -> string { return upper(s) + "!" }` makes
  `"hi".shout()` work;
* a bound function is a normal function. It can be passed around, imported
  and overloaded.

`.` is only for calling, and there are no exceptions. Attributes use `:`,
so a bound function never collides with a field of the same name:

| syntax | means |
|--------|-------|
| `x.name(args)` | `name(x, args)`, a bound function |
| `x.mod:name(args)` | `mod:name(x, args)`, a function bound in another module |
| `x:name` | the field `name` of an object, or the export `name` of a module |
| `x:name(args)` | call the function stored in field / export `name`, without `x` as an argument |

Functions bound in another module do not have to be imported one by one:

```
import "./geometry"
const v = geometry:vec(3, 4)
io:print(v.geometry:length())      // geometry:length(v)
```

## Overloading is the dispatch mechanism

Every function name refers to an **overload set**. Declaring a function
with a name that already exists adds a candidate instead of replacing it:

```
function describe(Rect r) -> string   { return "rect" }
function describe(Circle c) -> string { return "circle" }
function describe(int n) -> string    { return "number " + str(n) }
```

At each call, every candidate whose arity matches is scored against the
actual arguments. The highest total score wins. Per parameter:

| score | parameter type matches the argument because...                  |
|-------|------------------------------------------------------------------|
| 5     | the object was declared as exactly this record type (nominal)    |
| 4     | exact primitive or enum type (`int` for an int, `Option` ...)    |
| 3     | the record type matches structurally, function, empty typed array|
| 2     | generic parameter, `int` widened to `float`, plain `object`      |
| 1     | `any` / untyped parameter                                        |
| 0     | no match, so the candidate is discarded                          |

A union (`int || float`) scores one less than its best member, and an
array scores `2 + score of its first element`. When two candidates tie, the
one declared **later** wins, so a redefinition shadows an older one.

Operators are ordinary function names, so the same mechanism gives operator
overloading:

```
function +(Vec a, Vec b) -> Vec { return vec(a:x + b:x, a:y + b:y) }
```

The built-in `+(number a, number b)` and `+(string a, string b)` stay in the
set. A declaration in an inner scope (or module) extends a copy of the set
visible from the outer scope, so local additions do not leak outwards.

## Nominal and structural records

`type Person = { string name, number age }` defines a record type.

* An object literal assigned to a variable declared as `Person`, or returned
  from a function declared `-> Person`, is **tagged** as a `Person`. Calling the
  type (`Person({...})`) checks the value and tags it as well.
* A tagged object matches `Person` nominally (score 5), and later field
  assignments are type checked against the record (`p:age = "x"` fails, and
  so does `p:unknown = 1`).
* An untagged object still matches `Person` **structurally** (score 3) if it
  has every field with a matching type. Extra fields are allowed.

The difference in scores makes this work as expected:

```
type Named = { string name }
type Person = { string name, number age }

function hello(Named n)  { io:print("named", n:name) }
function hello(Person p) { io:print("person", p:name) }

const Person john = { name: "John", age: 22 }
hello(john)              // person - nominal match beats structural
hello({ name: "Anon" })  // named  - only Named matches
```

## Enums carry behaviour the same way

```
enum Shape {
    circle(float),
    square(float),
}

function area(Shape s) -> float {
    return match s {
        circle(r) -> 3.14159 * r * r
        square(a) -> a * a
    }
}

io:print(circle(2.0).area())
```

## Interfaces (planned, not implemented yet)

Interfaces are meant to work much like Rust traits: a named set of
function signatures that a datatype can satisfy. The binding model above
already provides the dispatch, so an interface only has to name a
requirement and let it be used as a type. Notes for that design:

* **A requirement is a signature with `Self` in the first position**, for
  example `area(Self) -> number`. A type satisfies it when an overload exists
  whose first parameter accepts that type.
* **Explicit vs implicit implementation.** Rust requires `impl Trait for Type`.
  The implicit check ("the functions exist") falls out of the current
  dispatcher for free. An explicit `impl` block could be sugar that declares
  the functions, checks they are complete, and records the relationship so
  errors are reported at the declaration instead of at the first call.
* **Interfaces as types.** `function total(Shape[] items)` would need a new
  type kind in `type_match` (`src/types.c`). Its score should rank between
  the nominal/exact scores (4–5) and generics (2), so concrete overloads
  still win.
* **Generic bounds.** `<T: Shape>function largest(T[] items) -> T` adds a
  constraint check to generic bindings (`Binding` in `src/vm.h`).
* **Default implementations** would be candidates with an interface-typed
  first parameter, which concrete overloads outrank automatically.

## Current limitations

* Overload resolution inspects only the **first element** of an array
  argument (declarations such as `const int[] xs = ...` check every element).
* Generic parameter names (`T`) can only be used in signatures, not in
  `let T x` inside the body.
* There is no ambiguity error. Ties are resolved by declaration order.
