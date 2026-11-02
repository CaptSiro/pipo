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
io:print(v.length())               // the same: Vec was declared in geometry
```

A dot call also looks at the functions **exported by the module that declared
the receiver's type**, so the module prefix can usually be left out. Those
candidates are merged with the ones in scope, and the scorer picks as usual.
Operators and other functions that should work everywhere can be declared
`global` instead; see [PACKAGES.md](PACKAGES.md#global-functions).

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

## Interfaces

An interface is a named, **closed** list of functions a type must have. The
syntax and rules are in [LANGUAGE.md](LANGUAGE.md#interfaces). How it maps
onto the binding model above:

* **A requirement is a signature in which the interface's own name stands for
  the implementing type**, e.g. `function area(Shape) -> number` inside
  `interface Shape`. `impl Shape for Circle` looks for an overload of `area`
  whose parameter is written for `Circle`. Catch-all parameters (untyped,
  `any`, an unbound generic) do not count, otherwise the built-in `str(any)`
  would satisfy every `str` requirement.
* **Explicit implementation.** `impl` is a statement that checks the
  requirements at the moment it runs and records the relationship. It does not
  declare functions; they are ordinary overloads. Because a type can gain
  overloads at any time but an interface cannot gain requirements, a passing
  `impl` stays valid.
* **Interfaces as types.** `function total(Shape s)` accepts any value whose
  type has an `impl`. It scores 4 in `type_match` (`src/types.c`), so a
  parameter declared with the concrete record type (5) still wins.
* **Default implementations** are bound functions too: when `impl` finds no
  function for a requirement that has a body, it creates one from the body
  with the interface name replaced by the implementing type and records it on
  the type. A function written for the type always wins over the default.
* **Extension** (`impl Equals for Comparator` inside `Comparator`) makes the
  child's `impl` run the parent's `impl` for the same type, so the type ends
  up recorded in both interfaces, and `Comparator` is accepted wherever
  `Equals` is expected.
* **Not implemented yet:** generic bounds (`<T: Shape>function largest(T[] items)`).

## Current limitations

* Overload resolution inspects only the **first element** of an array
  argument (declarations such as `const int[] xs = ...` check every element).
* Generic parameter names (`T`) can only be used in signatures, not in
  `let T x` inside the body.
* There is no ambiguity error. Ties are resolved by declaration order.
