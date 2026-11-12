# Static Typing

PiPo is statically typed. Before a program runs (or is compiled), the checker in
`src/check.c`, `src/check_expr.c` and `src/stype.c` gives every expression of the
program, of every module it imports and of the prelude a static type. A program
with a type error is rejected with exit code 65 before its first statement runs,
also when the error is in code that would never run:

```
function never_called() -> int {
    return "not an int"      // error: return value of 'never_called': expected int but got string
}
```

`pp --target=check file.pp` only checks. `pp --report file.pp` also prints how
much of the program was typed statically (see [Coverage](#coverage)).
`pp --no-check file.pp` skips the checker (interpreter only), as an escape hatch.

## `unknown`: the one dynamic type

`unknown` (the old `any`, which still works as a name for it) holds a value of
any type, the way a `void*` holds any pointer, except that the value remembers
its type. Anything converts to `unknown` implicitly. Getting a typed value back
out is explicit, with `as`:

```
function describe(unknown value) -> string {
    if some(i) = value as int {
        return "int " + str(i + 1)
    }

    return "something else: " + get_type(value)
}

const unknown[] bag = [1, "two", 3.0]
```

* `x as T` is `some(x)` when the value has type `T`, else `none()`. An int
  converts to a float: `3 as float` is `some(3.0)`. It also narrows unions
  (`number as int`) and interfaces (`(ord:max(a, b) as Money).unwrap():cents`).
* Reading a field of an `unknown`, calling it or doing arithmetic on it is a
  static error; `str`, `==`, printing and anything else that takes `unknown`
  work on it directly.
* `object` is a record whose fields are not known: reading `o:name` gives an
  `unknown`, and storing any field is allowed. It is the typed way to have an
  open bag of fields; a `std:Map` is the other one.

## `type_of` and `get_type`

| | `type_of(x)` | `get_type(x)` |
|-|--------------|---------------|
| answers | the **static** type of the expression | the type of the **value** underneath |
| `const unknown u = 42` | `"unknown"` | `"int"` |
| `const number n = 3` | `"int \|\| float"` | `"int"` |
| `[[1], [2, 3]]` | `"int[][]"` | `"array"` |
| `std:map()` used as a `Map<string, int>` | `"Map<string, int>"` | `"Map"` |
| evaluates `x` | no, it is known when the program is checked | yes |

`type_of` is a compile time constant: the checker replaces the call with the
string. It does not evaluate its argument (`type_of(r.next())` does not advance
`r`). In a generic function the static type can differ per instantiation; the
compiling targets use each instance's type, while the interpreter, which does
not instantiate, answers with the value's type there.

## What the checker infers

* **Variables** take the type of their initializer: `let total = 0` is an `int`,
  and `total = "zero"` is an error. An empty `[]`, `none()` or `std:map()` gets
  its element types from its first use (`xs.push(1.5)` makes `xs` a `float[]`,
  `m.put("a", 1)` makes `m` a `Map<string, int>`).
* **Untyped parameters** (`function f(x)`) are implicit generics: each call with
  new argument types checks the body again for those types (an *instance*).
  This is monomorphization, as in C++ templates or Rust generics, and is what
  makes the compiling targets possible.
* **Return types**, when not written, come from the `return`s (and from the last
  expression of a lambda). Recursion is handled by checking such a body twice.
* **Lambdas** get their parameter types from the callback type they are passed
  as or stored in; overloads are chosen by the callback's shape as before. The
  builtins that take `any` give them too: `map`, `filter`, `sort` and `reduce`
  from the array, `push` / `insert` into an array of callbacks from its element
  type.
* **Generic callback types** (`<A, B>type Mapper = A -> B`) bind their
  parameters from the lambda: `map_all([1, 2], x -> str(x))` gives `string[]`.
* **Interfaces**: a value typed with an interface can call its requirements;
  `Container<int>` binds `T = int` for `c.get(i)`.
* **Scopes**: the value a scope call's block gives back is part of the scope
  function's instance, so `code()` inside the scope function is typed too.
* `int ** int` is an `int`; a negative exponent is a runtime error now
  (`2.0 ** -1` is the float version). A type that depends on a *value* cannot be
  static.

## Rules that changed

| before (dynamic) | now (static) |
|------------------|--------------|
| a variable could change type: `let x = 1; x = "a"` | the type is fixed when declared |
| an object literal could gain fields: `obj:b = 2` | records are closed; declare the field, or the variable as `object` |
| `type_of(x)` was the runtime type | it is the static type; `get_type(x)` is the runtime type |
| `2 ** -1` was `0.5` | an error; write `2.0 ** -1` |
| errors were found when the line ran | they are found before the program starts |
| reading a field of an interface value worked when the value had it | it needs `as` first |
| `range` | still any range; `range<int>` and `range<float>` say which kind (also checked at runtime) |

## Can everything have a type?

This was the question to answer before compiling to WebAssembly, where every
local, parameter and result must have one of `i32`, `i64`, `f32`, `f64` (or a
reference type) fixed at compile time.

**Yes.** After the changes above, every test, example and library module in the
repository is typed completely: `--report` finds no dynamic expression in any of
them (the 29 test and example programs that run to the end check 9 108
expressions between them, the prelude and the libraries they import included).
The pieces that made it possible:

| language feature | why it could have blocked static types | how it is typed |
|------------------|-----------------------------------------|-----------------|
| untyped parameters | a parameter of any type | implicit generics, one instance per argument types |
| overloading by type | the runtime picks by value | the checker picks by static type, with the same scores (`st_fit` mirrors `type_match`) |
| `any` | any value | `unknown` = a boxed value with its type tag, narrowed only with `as` |
| JSON objects | fields added at runtime | closed records; `object` for open ones, whose fields are `unknown` |
| unions (`number`) | operations on "int or float" | allowed only where every member fits (`+` on `number`s gives a `number`); else `as` |
| runtime `precedence` changes | the shape of `a + b * c` changes at runtime | the checker applies them in statement order, which is when they run |
| enums with generic payloads | `none()` has no `T` | `Option<never>`: `never` fits every type, the first use decides |
| interfaces | dynamic dispatch | an interface value is a fat value: the checker knows the requirement; the runtime still dispatches (a vtable in a compiled target) |
| `lift()`, scopes, `defer` | non-local exits | typed like calls; the exits are control flow |

What remains genuinely dynamic is explicit in the program: values of type
`unknown`, the fields of an `object`, and calls through an interface or a union
where several overloads could fit the runtime value (`CCall.dispatch`). A
compiling target handles them with a tagged value and a runtime dispatch, the way
C++ handles `std::any` and virtual calls.

## Coverage

```
pp --report tests/collections.pp
745 expression(s) checked: 745 statically typed, 0 dynamic (100.0% typed)
```

When the checker cannot type something it does not guess: the expression is
*dynamic* (`ST_DYN`), no error is reported about it, and `--report` lists why.
The compiling targets refuse a dynamic expression with that reason, so a program
either compiles to fully typed code or says exactly which line keeps it from
doing so.

## Implementation

| file | role |
|------|------|
| `src/check.h` | `SType` (the static types), `NodeInfo` / `CCall` / `ChainInfo` (what the backends read) |
| `src/stype.c` | type construction, equality, printing, and `st_fit`: how well a type fits another, with generic bindings |
| `src/check.c` | scopes and symbols, type resolution, modules (also native and library ones), the prelude, entry points |
| `src/check_expr.c` | expressions and statements, overload resolution, instances, lambdas, patterns, interfaces, `type_of` folding |

* The checker walks the same AST the interpreter runs. It does not change it,
  except for folding `type_of(x)` into a string literal.
* Every function body is checked per **instance** (`CInstance`): the function
  with concrete parameter types. An instance keeps a table `Node* -> NodeInfo`
  with the static type of every expression and, for every call, operator and
  index, the target the checker resolved (`CCall`: a function instance, a
  native, a constructor, or a built-in operation such as `COP_ADD_I`).
  The backends compile instances, not functions.
* Overload resolution follows the runtime's rules (`resolve_overload` in
  `interp.c`): the same candidate sets (scope, the receiver's module, the
  receiver's `impl`s, `global`), the same scores and the same tie breaking, so
  the static choice is the one the interpreter makes for values of those types.
* Native functions are typed from their signatures; the builtins written with
  `any` (`push`, `map`, `+` on `number`, ...) get precise results in
  `builtin_result`.
* Imported files are parsed once, by the checker; the interpreter runs the same
  trees (`vm->preparsed`).
