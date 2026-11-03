# Proposal: extensible syntax from `.pp` files

**Status:** draft. Supersedes [PROPOSAL_AT.md](PROPOSAL_AT.md) (`@` stays
possible as an ordinary user operator).

**Implemented so far:** the `[]` / `[]=` half of layer 2, `std:Index`, and the
statement rule for `target op= value` (an in-place `op=` when one accepts the
values, otherwise `target = target op value`). See
[LANGUAGE.md](LANGUAGE.md#indexing-is-a-function). `{}` and `()` applications,
layers 3 and 4, and the other changes are not.

The goal, from the [language spirit](PROPOSAL.md#language-spirit): a tiny fixed
core, with everything else (indexing, short-circuit logic, a ternary,
containers, embedded languages like HTML) written in `.pp` files.

## Why not general mixfix (`function [(...)]`, `function ?(...):`)

A mixfix name like `?` … `:` has to be known **while parsing**. When the parser
reads `cond ? x`, it has to decide right there whether `?` is a binary operator
or the start of a ternary. Names are only known when code runs, and an imported
module's names are not known while this file is parsed. The tokens such names
would need are also taken: `:` reads an attribute even with spaces
(`c ? x : y` is `c ? (x:y)` today), `[` already has two meanings after an
expression, and `<` … `>` is comparison. A ternary written as a function would also
evaluate both branches.

The design below replaces "user-defined grammar" with four general rules. The
parser can apply each of them without knowing any names:

| layer | rule | what it gives |
|-------|------|---------------|
| 1. operators | a run of operator characters is a function name (exists today) | `+`, `in`, `@`, `\|>`, `??`, ... |
| 2. bracket application | `x(...)`, `x[...]`, `x{...}` call the functions `()`, `[]`, `{}`; assigning appends `=` | indexing, typed literals, map / set literals |
| 3. lazy parameters | a parameter typed `lazy T` receives its argument unevaluated | `&&`, `\|\|`, a ternary, `??` in the prelude |
| 4. macros | `name!(...)` / `name![...]` / `name!{...}` passes raw tokens to a `.pp` function at load time | HTML, SQL, `assert!` with source text, code generation |

Plus two small supporting changes: right-associative operators
([section](#right-associative-operators)) and imports loaded before the rest of
a file is lexed ([section](#imports-before-the-body)).

The keywords stay as they are. Two words are added: `lazy` (contextual, in
parameter types) and `macro` / `quote` (declarations and templates for layer 4).
`&&` and `||` leave the core.

---

## Layer 2: bracket application

### The rule

An expression **immediately** followed (no space) by a bracket group is an
*application* of that bracket pair. The pair is the name of a function:

| written | calls | replaces today's |
|---------|-------|------------------|
| `x(a, b)` | `x` itself if callable, else `()(x, a, b)` | calls (unchanged for functions) |
| `x[a, b]` | `[](x, a, b)` | `N_INDEX`, hard-coded in `get_index` |
| `x{ k: v }` | `{}(x, { k: v })` | the record shorthand `Rect{...}` |

**Assigning to an application calls the same name with `=` appended, and the
value as the last argument:**

```
xs[i] = v            // []=(xs, i, v)
xs[i] += 1           // +=(xs[i], 1) in place if one accepts, else []=(xs, i, [](xs, i) + 1); xs, i read once
grid[y][x] = 0       // []=([](grid, y), x, 0)
```

`()` keeps today's rule that `f (x)` with a space is still a call; only `[` and
`{` need the no-space rule, as `xs[0]` does today.

### In the library

```
// prelude / lib/std.pp
<T>function [](T[] xs, int i) -> T { return xs.at(i) }          // native in practice
<T>function []=(T[] xs, int i, T value) -> void { xs.set_at(i, value) }
function [](string s, int i) -> string { ... }
function [](object o, string key) { ... }
function []=(object o, string key, any value) -> void { ... }
<K, V>function [](std:map<K, V> m, K key) -> V { return m.find(key).unwrap() }
<K, V>function []=(std:map<K, V> m, K key, V value) -> void { m.put(key, value) }
<T>function [](T[] xs, range r) -> T[] { return xs.slice(r:start, r:end) }   // xs[1...3]

function {}(type t, object fields) { return t(fields) }       // Rect{ w: 1 } as today,
                                                               // and std:map{ a: 1 } == std:map({ a: 1 })
native function [](type t, ...)                                // std:set[1, 2, 3] == std:set([1, 2, 3])
```

The first parameter of `{}` and `[]` above is typed `type`: it matches the type
*value* `Rect` or `std:map` itself. A parameter typed `std:map` would match map
values instead.

A tuple has `[]` but no `[]=`, so `t[0] = 1` fails with an ordinary overload
error. No special "tuples cannot be changed" check is needed in the core.

### Why this is not a special case

The core no longer knows what indexing *means*. It knows one shape, "a value
next to a bracket group", the same way it knows "a value next to an operator".
Calls, indexing and typed literals become three instances of that shape. The
record shorthand `Rect{...}` exists today as a special case in `parse_postfix`;
it becomes one overload of `{}`.

### Names in name positions only

`[]` and `{}` written in an expression are an empty array and an empty object,
as today. As **names** they appear only where the grammar expects a name:

```
function [](...)            // after `function`
xs.[](0)                    // after `.` (a dot call)
std:[]                      // after `:` in a path
export { [], []= }          // in export / import lists
```

So `reduce(xs, [], f)` still passes an empty array. To pass indexing as a value,
write `(xs, i) -> xs[i]` or `std:[]`.

---

## Layer 3: lazy parameters

```
function &&(bool a, lazy bool b) -> bool { return if a { b } else { false } }
function ||(bool a, lazy bool b) -> bool { return if a { true } else { b } }
```

A `lazy T` parameter receives its argument as an unevaluated expression,
closed over the caller's scope. Reading the parameter evaluates it **once**; the
result is kept and checked against `T`. A lazy argument that is never read is
never evaluated, which is exactly short-circuiting.

The rules that keep dispatch sound:

1. **The first parameter cannot be lazy.** Dot calls and operator lookup
   (receiver's module, `impl` functions) need the first value, so it is always
   evaluated.
2. **Overloads must agree on laziness.** Within one name and arity, every
   candidate (local, global, the receiver's module and `impl` sets merged) must
   mark the same positions lazy. Declaring a conflicting overload is an error;
   so is merging conflicting sets at a call:
   `overloads of '&&' with 2 parameters disagree on whether parameter 2 is lazy`.
   This lets the interpreter know which arguments to evaluate *before* it picks a
   candidate.
3. **A lazy argument does not take part in scoring.** It counts like an untyped
   parameter (score 1), because its value is not known yet. Overloads are chosen
   by the eager arguments.
4. A lazy parameter can be passed on as a lazy argument without being forced.

`&&` and `||` move from the core to the prelude, and become overloadable for
user types (a query builder can define `&&(Query, lazy Query)`). The interpreter
can keep a fast path for the prelude's own `bool` versions.

### A ternary without `:`

Two ordinary operators, both useful on their own:

```
<T>function ?(bool condition, lazy T value) -> T? {     // cond ? x   is some(x) or none()
    return if condition { some(value) } else { none() }
}
<T>function ??(T? option, lazy T fallback) -> T {       // opt ?? y   unwraps or falls back
    return match option { some(x) -> x, none() -> fallback }
}
precedence ? under ||
precedence ?? under ?
precedence ?? right                                     // see below

const sign = x > 0 ? "positive" ?? x < 0 ? "negative" ?? "zero"
//           (x > 0 ? "positive") ?? ((x < 0 ? "negative") ?? "zero")
const port = config.find("port") ?? 8080                // ?? works on any Option
```

* Only the chosen branch runs: `x != 0 ? 10 / x ?? 0` never divides by zero.
* `?` binds looser than `||`, so `a > 0 && b > 0 ? "both" ?? "no"` takes the
  whole condition.
* Nesting is correct only if `??` is right-associative: left-associative
  `(a ? x ?? b ? y) ?? z` would hand `x` (not an Option) to the outer `??`. That
  is why the next section exists.
* `?` in a **type** (`int?`) is a separate grammar and is not affected.
* `if c { a } else { b }` remains the general form. The operators are a library
  shorthand, not a new construct.

### Right-associative operators

`precedence ?? right` (and `precedence X left`, the default) joins the existing
`over` / `under` / `as` forms. It is runtime data in the same table, so operator
chains already re-associate when it changes. `**` would become right-associative
in the prelude the same way.

---

## Layer 4: macros

### Invocation is always marked

```
name!(tokens)    name![tokens]    name!{tokens}    module:name!{tokens}
```

An identifier immediately followed by `!` and immediately by an opening bracket
(no spaces) is a macro call. The parser does not need to know the macro. It
collects the **balanced token tree** inside the brackets without interpreting it,
and stores it as a `N_MACRO` node.

### Expansion at load time

Loading a file becomes:

1. parse the file (macro calls stay as token trees);
2. run its imports (they come first, the layout rule already guarantees this);
3. **expand every macro call**: look up `name` in the file's scope after the
   imports, call it with the token tree, and parse the tokens it returns in place
   of the call. Repeat for macro calls in the output, up to a depth of 128;
4. run the rest of the file.

A macro is therefore always defined in an *already loaded* module. A file
cannot use a macro it defines itself (that macro does not exist until the file
runs). This is the same rule Rust has for procedural macros, and it removes any
question of ordering. Every expansion error is reported when the file loads,
with the file and line of the call, even inside functions that never run.

### Writing one

A macro is a `.pp` function marked `macro`. It takes the input tokens and
returns tokens. `lib/std/syntax.pp` provides the types:

```
export enum Delim { PAREN, BRACKET, BRACE }
export type Token = { TokenKind kind, string text, bool space_before, int line }
export enum Tree { token(Token), group(Delim, Tree[]) }
```

`quote { ... }` builds tokens from a template. Inside it, `$name` inserts a
value: a `Tree[]` is inserted as it is, and a string, number or bool becomes one
literal token. `$$` writes a literal `$`.

```
// lib/test.pp
export macro expect(Tree[] input) -> Tree[] {
    const source = syntax:to_source(input)          // "a + b == 3"
    return quote {
        if !($input) { panic("assertion failed: " + $source) }
    }
}

// user code
import test -> { expect }
expect!(a + b == 3)
```

A macro is a value bound to a name like any other, so it shares the namespace
with functions. A macro named `assert` imported by name would hide the built-in
`assert` function in that file, so this one is called `expect`.

### HTML

```
import web -> { html }

const page = html!{
    <div class="card" data-id={user:id}>
        <h1>"Hello, " {user:name}</h1>
        <ul>{ items.map(i -> html!{ <li>{i}</li> }) }</ul>
        <br/>
    </div>
}
// expands to (roughly)
// web:element("div", { class: "card", "data-id": user:id }, [
//     web:element("h1", {}, ["Hello, ", user:name]), ...])
```

`html` is ordinary `.pp` code that walks the tree: `<` `div` starts an element,
`ident = value` pairs are attributes, `{...}` groups are embedded expressions
(spliced back as `Tree[]`), and `</` `div` `>` closes the element. It can
report mismatched tags at load time, with the line of the bad tag. `data-id` arrives as
`data` `-` `id` with no space between them; the macro joins tokens using
`space_before`. Macros see operator runs **unsplit** (`<br/></div>` contains the
run `/></`), because splitting depends on which operators are known, and the
macro splits them itself.

**The input must be tokens of this language.** The brackets must balance, and
free text must be a string literal (`"Hello, "`). An apostrophe in bare text
would start a string, and `//` would start a comment. Rust's `html!` crates have
the same rule. It is the price of the parser not needing to know the macro.

### Hygiene

Expanded code must not capture or break names by accident:

* **Identifiers from the input** (`$input`) keep their meaning at the call
  site.
* **Free identifiers written in the template** resolve in the **macro's
  module**, not at the call site. `web:element` in the expansion works even if the
  user never imported `element`, and a user variable named `panic` cannot break
  `expect!`.
* **Local names the template declares** (`let`, `const`, parameters, `for`
  variables, pattern bindings) are renamed for each expansion, so `let tmp` in a
  macro never clashes with the user's `tmp`.
* **Declared functions, types, enums and interfaces are not renamed**, and they
  are declared at the call site. Overloading is by name, so a `derive!(Str, Box)`
  macro *should* add `str(Box)` to the caller's `str`. Their bodies still follow
  the two rules above.

In the interpreter, a template identifier becomes an `N_IDENT` that carries the
macro module's scope, and lookup starts there. Renaming happens when `quote`
builds the tokens.

### What macros are for, and what they are not

Use them for code that needs to see source text or other code: embedded
languages, generating declarations, assertions with their expression, checking
a format string at load time. Laziness and operators do not need macros
(layer 3 and layer 1), and indexing does not either (layer 2). Macros cannot
change how unmarked code parses, by design.

---

## Imports before the body

Today an operator declared only in another module is not known while this file
is lexed, so `a<>b` must be written `a <> b`. Imports must already come first, so
the loader can lex and run the import header, then lex the body with every
imported operator known. Macros need the imports loaded before expansion anyway.
This removes a documented limitation.

---

## Ambiguity check

Every rule above, checked against what already exists:

| situation | reading | why it is unambiguous |
|-----------|---------|-----------------------|
| `f!(x)` vs `f !(x)` | macro call vs `f(!(x))` (juxtaposition) | the no-space rule; `f!x` (no bracket) stays juxtaposition |
| `a!=b`, `a!=(b)` | operator `!=` | the operator run is `!=`, not `!`; a macro needs exactly `!` |
| `xs[0]` vs `f [1]` | application vs array argument | the existing no-space rule |
| `if x{ ... }` | **a bug today**: `x{` is read as `x{...}` and the `if` misses its block | inside the head of `if`, `while`, `for` and `match`, a `{` always starts the block (like Rust's no-struct-literal rule) |
| `Rect{ w: 1 }` | `{}(Rect, { w: 1 })` | same tokens and meaning as today's shorthand, now an overload |
| `[]` / `{}` as values | empty array / object | they are names only in name positions |
| `xs[i] = v` without `[]=` | overload error | read-only types need no core check |
| `cond ? x : y` | **not supported** | `:` stays attribute access; the ternary is `? ??` |
| `int?` in a type | `Option<int>` | type grammar, separate from expressions |
| lazy and eager overloads mixed | declaration error | the agreement rule, checked when sets are merged |
| a lazy argument's type | checked when it is read | dispatch ignores it (score 1); an unread argument is never checked, as with `&&` today |
| a macro that produces an expression where a statement is expected (or the reverse) | load-time error | an expression-position call must produce exactly one expression; a statement-position call may produce statements |
| a macro that expands to itself forever | load-time error at depth 128 | |
| a macro and a function with the same name | one namespace, ordinary shadowing | invocation is marked, so the error is clear: `'assert' is a function, not a macro` |
| a macro with side effects (`io:print` while expanding) | allowed but runs at load | documented; output should depend only on the input |

---

## Cost

| part | size | notes |
|------|------|-------|
| bracket application, `[]` / `[]=` / `{}` / `()` overloads, removing `N_INDEX` | small (~1-2 days) | `get_index` / `set_index` become built-in overloads; the inline caches already exist |
| `{` in `if`/`while`/`for`/`match` heads | tiny | fixes a current bug |
| right associativity | small | one flag per precedence entry |
| `lazy` parameters, `&&` / `\|\|` into the prelude, `?` / `??` | medium (~3-5 days) | a thunk value, the agreement check in `resolve_overload`, and a fast path so `&&` stays quick |
| imports before the body | small-medium | the lexer's known-operator list fed from loaded modules |
| macros: token trees, `std:syntax`, expansion pass, `quote` | large (~1-2 weeks) | the token-tree reader is easy; `quote`, splicing and statement/expression contexts are the bulk |
| hygiene | medium, on top of macros | scope-carrying identifiers and renaming binders |

A sensible order is: bracket application (removes the biggest special case),
then `lazy` and right associativity (removes `&&` / `||` from the core and gives
the ternary), then macros.

## Open questions

1. Should `()` on a non-callable value fall back to an overload of `()`
   (callable records), or stay an error?
2. Should `{}` / `[]` on a type (`std:map{...}`, `std:set[...]`) be the
   recommended way to write container literals?
3. Should `quote` be a keyword, or a macro itself (`quote!{...}`) provided by
   `std:syntax`? The macro form would keep the keyword list unchanged, but
   needs a bootstrap.
4. Should macros get an AST API later (`syntax:parse_expression(tokens)`) or only
   tokens? Tokens are enough to start and keep the core's AST private.
