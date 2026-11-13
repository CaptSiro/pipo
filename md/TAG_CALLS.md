# Tag calls (draft 4)

**Implemented** (2026-11-13) in the parser, the checker and the interpreter, with the
IntelliJ plugin and the TextMate grammar. The rules for users are in
[LANGUAGE.md](LANGUAGE.md#tag-calls). Status of the design below:

| | |
|-|-|
| tags, the four call forms, the `/>` / body hints, kebab attribute names, string text, `m:f` paths, tags on record types | done (`tests/tags.pp`, `tests/err_tag_*.pp`) |
| `T?` fields may be left out of a record literal (`none()`) | done, for record literals in general |
| the bytecode VM | works too: tags become ordinary calls before it compiles them (`tests/tags.pp` runs on it); wasm is not tested |
| rest fields `...Map<string, V> name` and `decode(m, T)` | done, in the interpreter and the bytecode VM (`tests/rest_fields.pp`, `tests/decode.pp`); a record type with a rest field is converted where an argument, a typed declaration or `Type({...})` gives a value its type (on the bytecode VM: named record types only) |
| a kebab name filling `a_b` or going to the rest field (rules 1 and 2 below) | done: tag attributes keep their kebab names, and record conversion maps them |
| a record where a `Map<string, V>` is expected (rule 3, level 3: `element(Map<string, unknown> attrs)`) | done, everywhere a value is converted (arguments, declarations, assignments, returns, fields), on every target: the checker rewrites the value into `map(value)` (`tests/map_params.pp`) |
| generic inference through record fields (`<For each={...}>`), lambdas in record and array literals typed from the parameter | done (`tests/record_inference.pp`) |
| unknown attributes of a closed record, with `"did you mean"` | done, for record literals in general: `Props has no field 'lable' (did you mean 'label'?)` (when the misspelling also leaves a required field missing, the error is still `no overload of 'f' accepts (...)`) |

One rule became more precise while implementing: `<f>...</f>` without attributes
calls `f([...])` only when an `f` takes **exactly one array**; otherwise it is
`f({}, [...])`. A `T?` attribute is given as `size={some(3)}`, like any `T?` value.

A design, not an implementation. It takes the tag call of [WEB.md](WEB.md#tag-calls-a-calling-paradigm)
(draft 2), works through the second round of [designer notes](WEB.md#designer-notes-2),
and, in this draft, the [design notes](#design-notes) at the end of this file.
WEB.md stays as it was; where this file and WEB.md disagree about tag calls,
this file is the newer one.

**What changed since draft 3**

* The calling convention is now **`f(attributes, children)`**: two arguments,
  with the children in their own parameter. Draft 3 put them in a `children`
  field. The reasons are in [calling convention](#calling-convention-one-argument-or-two).
* Prefix `<` **and `>`** are reserved: a function `<` or `>` with one parameter
  is a syntax error. This is **implemented** (`tests/err_unary_lt.pp`,
  `tests/err_unary_gt.pp`; binary `<` / `>` stay overloadable, `tests/angle_binary.pp`).
* `Map` and `Set` (and `Index`, `Length`) are **built in** now, not `std:Map`:
  the rest field below relies on them. **Implemented**: they live in the prelude.
* Every function must state its return type (`-> void` when it returns
  nothing). **Implemented**. Tags depend on it: the checker reads from the
  signature alone whether a function takes children and what kind.

| note | answer |
|------|--------|
| generics must not break | every place `<` appears today was listed and checked. A tag starts only in **operand position**, with `<` directly followed by a name, which no generic ever is ([parsing](#parsing-100-deterministic)) |
| user-defined `<`, `>`, `="` | `="` is not an operator (`"` is not an operator character). Prefix `<` and `>` are reserved and the parser rejects them, the same way it rejects `.` and `,` in names ([reserved](#what-is-reserved)) |
| hundreds of element functions pollute the namespace | they live in the module `web:html` and are reached through it: `import web:html -> h`, then `<h:div>`, like an XML namespace ([elements](#html-elements-without-polluting-the-namespace)) |
| infinite attributes (`data-*`), strict typing with a fallback to a map | the type of the **attributes parameter** decides: a closed record (strict), a record with a **rest field** (strict for the fields it names, collects the rest), or `Map<string, unknown>` (dynamic). `decode(m, T) -> Error<T>` turns a map back into a typed record ([typing](#typing-the-attributes)) |
| whitespace collapsing breaks `<pre>` | text is a **string literal**. Nothing is collapsed, trimmed or guessed ([text](#text-is-a-string-literal)) |
| kebab-case names | identifiers cannot contain `-` (`function my-name()` is a syntax error, `a-b` is always `a - b`). **Attribute** names may be kebab-case, because no expression can appear there ([attribute names](#attribute-names-may-be-kebab-case)) |
| `function(attributes, children)` ([design notes](#design-notes)) | **adopted**. It is better for PiPo on every point that matters here; the research is [below](#calling-convention-one-argument-or-two) |

Contents: [the rule](#the-rule) · [calling convention](#calling-convention-one-argument-or-two) ·
[text](#text-is-a-string-literal) · [parsing](#parsing-100-deterministic) ·
[reserved](#what-is-reserved) · [typing the attributes](#typing-the-attributes) ·
[typing the children](#typing-the-children) · [attribute names](#attribute-names-may-be-kebab-case) ·
[elements](#html-elements-without-polluting-the-namespace) · [settings](#beyond-html-settings-files) ·
[rejected](#considered-and-rejected) · [language changes](#language-changes) · [plan](#plan)



## The rule

A tag call is one more way to write a call, alongside `a + b`, `a.f(b)`, `c[i]`
and `_s a { ... }`. The attributes are the first argument and the body is the
second:

| written | calls |
|---------|-------|
| `<f/>` | `f()`, or `f({})` when no `f` takes zero arguments |
| `<f a="text" b=3 c={expr} d/>` | `f({ a: "text", b: 3, c: expr, d: true })` |
| `<f a={x}>"text" {y} <g/></f>` | `f({ a: x }, ["text", y, g()])` |
| `<f>"text"</f>` | `f(["text"])`, or `f({}, ["text"])` when no `f` takes only children |
| `<f a={x}></f>` | `f({ a: x }, [])`: an empty body is still a body |
| `<m:f/>`, `<m:sub:f>...</m:sub:f>` | `m:f()`, `m:sub:f([...])` |

```
tag         = '<' path attribute* ('/>' | '>' child* '</' path '>')     the closing path repeats the opening one
path        = ident (':' ident)*
attribute   = attr_name ('=' attr_value)?                               no value: true
attr_name   = ident ('-' ident)*                                        no spaces inside
attr_value  = string | int | float | 'true' | 'false' | '{' expression '}'
child       = string | tag | '{' expression '}'
```

* The tag is a **function name**, found by the usual lookup and overload
  resolution. The language has no list of HTML elements and no rule about upper
  or lower case.
* `/>` and `>...</f>` are two different calls: **without** a body there is no
  children argument, and **with** a body there is one, even an empty one. The
  tag's form says how many arguments the call has, and the function's signature
  says how many it accepts.
* An attribute value that is not a literal is written in braces: `n={count + 1}`.
  This is what keeps `>` inside a value from ending the tag: `when={a > b}`.
  A negative number is an expression: `x={-1}`.
* The language knows no attribute or field name. In draft 3, the parser
  produced a `children` field. Now the children are a positional argument, so
  the parser produces nothing but a call.



## Calling convention: one argument or two

The question from the design notes is whether a tag should call
`f({ ...attributes, children: [...] })`, with one argument, or
`f(attributes, children)`, with two.

### What other systems do

| system | shape | how children are typed |
|--------|-------|------------------------|
| React / JSX (`createElement(type, props, ...children)`) | separate at the call, **merged** into `props.children` before the component sees them | `ReactNode`: almost anything. TypeScript cannot say "exactly one `<li>`" or "no children" in a way the checker enforces well |
| Solid, Preact | merged, like React | the same |
| Flutter (`Column(children: [...])`, `Center(child: ...)`) | merged: children are named arguments | typed well (`List<Widget>`, `Widget`), but "children" and "child" are conventions every widget repeats |
| Vue `h(tag, props, children)`, Mithril `m(sel, attrs, children)`, hyperscript | **separate** | untyped (JavaScript) |
| Elm (`div : List (Attribute msg) -> List (Html msg) -> Html msg`) | **separate**: two lists, always both | typed. Elements without children still take `[]`, the most common complaint (`input [ ... ] []`) |
| Jetpack Compose (`Column(modifier) { ... }`), SwiftUI (`VStack(spacing: 2) { ... }`), kotlinx.html (`div(classes = "x") { ... }`) | **separate**: options as arguments, content as a trailing block | typed. A view without content simply has no block parameter, and passing one is a compile error |

Two families. **Merged** comes from JavaScript, which has no overloads and no
static types, so one props object was the only shape that could vary per
component. **Separate** is what the statically typed systems settled on (Elm,
Compose, SwiftUI, kotlinx.html), because there "takes content or not" is part
of the signature and the compiler checks it. Elm shows the one cost of
separate: if every element must take both lists, the empty list gets noisy.
Compose and SwiftUI avoid that with overloads: a view without content has no
content parameter.

### What matters for PiPo

| | one argument (`children` field) | two arguments (`f(attrs, children)`) |
|-|----------------------------------|---------------------------------------|
| names the language knows | the parser must produce a field called `children`: a reserved name inside every props type, which goes against "no name in the language knows about HTML" | none: children are positional, like the block of a scope call |
| "takes children or not" | a field that may or may not exist; `<input>"x"</input>` is an error about a missing field | the **arity**: `<input>"x"</input>` finds no `input(attrs, children)`. The signature says it, and overloads already resolve by arity |
| the tag's form | `<f/>` and `<f></f>` mean the same | `<f/>` (no children argument) and `<f></f>` (an empty list) are different calls, and the difference is visible in the signature |
| dynamic attributes, typed children | with `Map<string, unknown>` as the parameter, the children become a map entry of type `unknown` | `element(Map<string, unknown> attrs, Child[] children)`: attributes are dynamic and children stay typed. This is exactly the signature in the designer notes |
| rest fields | an attribute called `children` and the body collide; the rest field has to skip a name | no collision: `children` is an attribute like any other |
| generics (`<For each={items}>{t -> ...}</For>`) | `T` must be inferred **across fields of one record literal** (from `each` to `children`), which the checker cannot do yet | `T` is bound by the first argument, then the lambdas of the second are typed from `ItemView<T>[]`. That is the left-to-right callback inference the checker already does for `[1, 2].for_each(x -> ...)` |
| forwarding (a wrapper component) | one value to pass on | two values to pass on |
| optional children | a field marked `?` | two overloads (`f(A)` and `f(A, C[])`) or always a body (`<f></f>`) |
| PiPo's other calling paradigms | | the same split as a scope call: `_s a { block }` keeps the arguments and the block apart |

### Verdict

**Two arguments.** Merged children would be the one place where the parser
invents a name. Its only advantages are fewer overloads and one value to
forward, and both are small in a language that resolves overloads by arity
anyway. Separate children make "takes content or not" part of the signature, as
in the typed systems above, without Elm's empty-list noise, because a function
that takes no children simply has no such parameter.

The order is **attributes first**, as they are written (`<f attrs>children</f>`).
That order is also what lets generic parameters bind from the attributes before
the children's lambdas are typed. A scope call puts the block first
(`_s(block, a)`). Tags do not follow it, because their children are typed from
what the attributes bound, not the other way around.

### The example from the design notes

The notes' example, with a few slips fixed (`age` for `int`, `Entity<unknown>[]`
for `Entity<unknown>`, `</parent>` for the last `<parent/>`). Record field
shorthand (`Entity{ value: person, children }`) **already exists**: a key
without `:` takes the variable of the same name.

```pipo
type Person = { int age, string name }
<T>type Entity = { T value, Entity<T>[] children }

function parent(Person person, Entity<Person>[] children) -> Entity<Person> {
    return Entity{ value: person, children }
}

function child(Person person) -> Entity<Person> {
    return Entity{ value: person, children: [] }
}

const p = <parent age={32} name="Tom">
    <child age={5} name="James"/>

    <child age={5} name="James">          // error
    </child>

    <parent age={32} name="Tom"/>         // error
</parent>
```

The two errors, as the checker would give them:

```
file.pp:12: error: <child> is written with a body, but no 'child' takes children
  candidates:
    child(Person person) -> Entity<Person>
  write <child .../> (no body), or add an overload child(Person, ...[] children)

file.pp:15: error: <parent/> is written without a body, but every 'parent' takes children
  candidates:
    parent(Person person, Entity<Person>[] children) -> Entity<Person>
  write <parent ...></parent> (an empty body gives [])
```

Both come from ordinary overload resolution: there is simply no candidate with
that number of arguments. The tag only adds the hint about `/>` versus a body,
because it knows which form was written.



## Text is a string literal

```pipo
<p>"Hello, " {name} "!"</p>
<button on_click={save}>"Save"</button>
<pre>"fn main() {
    print(1)
}"</pre>
<pre>{source_code}</pre>
```

Text between tags is a PiPo string: quoted, with the usual escapes, and kept
**exactly**. There is no whitespace rule at all, so `<pre>`, `<textarea>`,
`white-space: pre-wrap` and code blocks get exactly what is written. Whitespace and
newlines **between** children (outside the quotes) are only layout, like
whitespace between the elements of an array literal. Comments are allowed
there too.

Why not raw text (`<p>Hello {name}</p>`):

* raw text needs a whitespace rule. Any rule (JSX collapses lines, HTML
  collapses runs, Swift strips the closing indentation) is wrong for some
  element, and the note on `<pre>` is exactly that. A string has no rule.
* raw text needs a second lexer mode that the lexer cannot enter by itself: it
  would have to know that `<` started a tag and not a comparison, which only the
  parser knows. With strings, the body of a tag is ordinary PiPo tokens: the
  lexer does not change at all.
* raw text makes `<`, `>`, `{`, `}`, `"` special inside text and needs escapes for
  them (`{"{"}`, entities). In a string only `"` and `\` are special, as everywhere
  else in the language.
* the cost is two quotes per piece of text. In UI code most text is short
  labels or comes from variables (`{t:title}`), so the cost is small.

Raw text can be added later without breaking anything, because today an
unquoted word inside a tag is a syntax error.



## Parsing: 100% deterministic

### The rule

A tag starts when, **in operand position** (where an expression or a prefix
operator may start), the parser sees an operator token that is exactly `<`,
followed **with no space** by an identifier.

Inside the tag, the parser reads `>`, `/>` and `</` by taking characters off
operator tokens (`eat_op_prefix`, which the parser already uses for `>>` in
`Map<K, Set<V>>`). So it does not matter how the lexer grouped `></`, `/><` or
an operator the file declared.

### Every place `<` appears today

| where | example | why it is never a tag |
|-------|---------|-----------------------|
| binary operator | `a < b`, `a <b`, `a<b>c` | `<` comes after an operand, not in operand position |
| operator run | `a <= b`, `x << 2`, `<=>`, `<|` | the token is longer than `<` |
| generic declaration | `<T>function`, `<K, V>impl`, `export global <T>function` | see [below](#generics-against-tags) |
| generic type | `Signal<T>`, `Map<K, Set<V>>`, `tuple<A, B>` | read by the type parser, never in operand position |
| classic call | `<(a, b)` | `<` is followed by `(`, not a name |
| name position | `function <(a, b)`, `[1].<(x)`, `export { < }`, `precedence < over +` | read as a name, not as an operand |
| prefix operator | `<x` (a function `<` with one parameter) | the only real conflict, and now a syntax error ([reserved](#what-is-reserved)) |
| new line | `a`, then `<div/>` on the next line | an operator on a new line already ends the statement, so the new line is a new statement that starts with an operand |

### Generics against tags

A generic declaration is the only other construct that can start with
`<Name>`. Today it appears only at the **start of a statement** (`<T>function`,
`export <T>type`). An anonymous generic function in an expression
(`const f = <T>function id(T x) -> T { ... }`) does not exist: the parser reads
it as the operator `<`, and the checker answers `undefined name 'T'`.

At the start of a statement, the token **after the closing `>`** tells the two
apart, and the two sets do not overlap:

| after `<Name ...>` | it is |
|--------------------|-------|
| `function`, `generator`, `scope`, `type`, `interface`, `impl`, `enum` | type parameters |
| a string, `{`, `<` (a child tag), `</` | a tag with children |
| nothing: `<Name/>` | a tag, because `/>` never closes type parameters |

Type parameters are names separated by `,`, and a tag never contains `,` before
its `>`. Attributes contain `=`, which type parameters never do. So `<A, B>` is
always generics and `<a href="x">` is always a tag, from the second token on.

If anonymous generic functions are added later, they keep working: they are
`<T>` followed by `function`, which is in the first row. Explicit type arguments
on a call, if they are ever added (`json<User>(r)`), come **after** a name. That
is binary position, so they never start an operand.

### Inside the tag

Once a tag has started, the parser knows exactly what may come next, and any
other token is an error that names the tag:

* in the attribute list: a name, `=`, a literal or `{`, `>` or `/>`;
* in the body: a string, `{`, `<` + name (a child tag) or `</` (the end);
* after `</`: the same path as the opening tag, then `>`. `<a><b></a>` is
  `expected '</b>' (to close <b> at 3:4) but found '</a>'`.

`{` inside a tag always starts an expression that ends at the matching `}`, so
a record literal as a value is written `style={{ color: "red" }}`.



## What is reserved

All of these are syntax errors at the declaration, checked by the parser
(`check_declared_fn_name` and `check_angle_arity` in `src/parser.c`). The IDE
plugin reports the same errors.

| declaration | why |
|-------------|-----|
| a name containing `.` (`..`, `+..`), except `...` and `...=` | `.` is a dot call: with a function `..`, `x..f()` would read as `x .. f()`. The ranges stay overloadable |
| `,` | separates arguments and elements: `[1, 2]` must stay a list |
| `<` with one parameter | a `<` in operand position starts a tag call |
| `>` with one parameter | reserved with `<`, so the pair stays symmetric and `>` never needs a second rule |

Binary `<` and `>` (two parameters), and longer operators that start with them
(`<|`, `<=>`, `->>`), are unchanged. `="` is not an operator: `"` is not an
operator character, so `a="x"` is always a name, `=` and a string.

If an operator that starts with `<` is declared in **another** module, this
file's lexer does not know it and splits it (`<-x` is read as `<` `-x`). That
already means it must be written with spaces (`a <- b`). With prefix `<`
reserved, `<-x` is now an error that says so, not a silent call of a different
operator.

**Is `function <(Arg1)` the only thing that can break tags?** Yes. Every other
`<` in the table above sits in a position where an operand cannot start, and
inside a tag every token is one of a fixed few. Prefix `<` was the only overlap,
and it is now rejected.



## Typing the attributes

The first argument is the attribute list, converted to the type of the
function's first parameter. That type decides how strict the conversion is.

### 1. Closed record: strict

```pipo
export type ButtonProps = { string label, Prop<bool>? busy, Handler on_press }
export function Button(ButtonProps p) -> Node

<Button label="Save" on_press={save}/>        // ok: busy is optional
<Button lable="Save" on_press={save}/>        // error: ButtonProps has no field 'lable' (did you mean 'label'?)
<Button on_press={save}/>                     // error: missing field 'label' of ButtonProps
<Button label={3} on_press={save}/>           // error at label={3}: expected string, got int
```

This is the ordinary record conversion of a record literal, with each error
placed at its attribute.

### 2. Open record: a rest field

A record type may end with **one rest field**: `...Map<string, V> name`.
A record literal converted to it puts every field the type does not name into
that map, and each such value must convert to `V`.

```pipo
export type DivAttrs = {
    Prop<string>? id,
    Class? class,
    Handler? on_click,
    ...Map<string, Prop<string>> attrs        // everything else: data-*, aria-*, new attributes
}

<h:div id="main" data-user-id={uid} aria-live="polite" hx-get="/items"/>
//  id: "main", attrs: { "data-user-id": uid, "aria-live": "polite", "hx-get": "/items" }
```

* The fields the type names are checked as strictly as in a closed record
  (`id={3}` is still an error). A name the type does not know is not an error:
  it becomes an entry in `attrs`, which is the point of an open type.
* The rest field is a feature of **record conversion**, not of tags: the plain
  literal `div({ id: "x", role: "button" })` does the same. That keeps tags a
  pure calling paradigm.
* `V` may be `unknown` for a field that takes anything.
* `Map` is built in (it was `std:Map`). A record type is a language feature,
  and a language feature must not depend on a library import.

### 3. Map parameter: dynamic

```pipo
function element(Map<string, unknown> attrs, Child[] children) -> Node

<element tag="my-widget" size={3} label="x">"text"</element>
//  element(map with "tag": "my-widget", "size": 3, "label": "x", ["text"])
```

When the first parameter is `Map<string, V>`, the attributes become map
entries (each value converted to `V`). This is the fully general form from the
designer notes: every attribute is accepted, and the children keep their own
type in the second parameter.

### Getting the types back: `decode`

```pipo
<T>function decode(Map<string, unknown> m, type T) -> Error<T>      // T: a record type

function Card(Map<string, unknown> attrs, Child[] children) -> Node {
    const p = decode(attrs, CardProps).lift()    // CardProps, or the fail(..) is returned
    ...
}

match decode(attrs, CardProps) {
    success(p) -> render(p),
    fail(msg) -> io:print_nl(msg)                 // "CardProps: field 'title': expected string, got int"
}
```

`decode` converts the map field by field with the rules of `as` (`3` into a
`float` field is `3.0`, `unknown` values are opened, an `Option` field may be
missing). It fills a rest field with what is left, and fails with the path of
the first mismatch. The second argument is a type written in argument position,
which the checker reads like the right side of `x as T`; the result type
follows from it. `decode` is also what `json<T>` in WEB.md needs, so one
feature serves both.

### Choosing a level

| first parameter | unknown attribute | wrong type | use |
|-----------------|-------------------|------------|-----|
| closed record | compile error | compile error | components |
| record with `...rest` | goes to `rest` | compile error for named fields | HTML elements |
| `Map<string, unknown>` | accepted | runtime, through `decode` | generic hosts, custom elements, plugins |

Components keep full type safety, which the notes value most. The dynamic form
is for code that really does not know its attributes; it is not the default.



## Typing the children

The second argument is an array literal of the children, typed by the second
parameter like any array literal passed to a function:

| second parameter | accepts |
|------------------|---------|
| none (no overload with two parameters) | no body: `<f/>` only |
| `Child[]` (strings, numbers, nodes, reactive values) | any content |
| `View[]` (`() -> Node`) | lazy content: `<Show>{() -> <p>"hi"</p>}</Show>` |
| `ItemView<T>[]` (`T -> Node`) | one lambda per item: `<For each={xs}>{x -> <li>{x}</li>}</For>` |
| `Section[]`, `Entity<Person>[]`, a union | only those: `<Settings>` accepts `<Server/>` and `<Database/>`, nothing else |
| `unknown[]` | anything, opened with `as` |

A component that accepts children optionally declares both overloads:

```pipo
function Card(CardProps p) -> Node {
    return Card(p, [])
}

function Card(CardProps p, Child[] children) -> Node { ... }
```

### Overloads, step by step

The tag's form chooses which arities are tried, in this fixed order. Inside
each step, overloads are resolved as for any call:

| written | tries |
|---------|-------|
| `<f/>` | `f()`, then `f({})` |
| `<f a=1/>` | `f({ a: 1 })` |
| `<f>...</f>` | `f([...])`, then `f({}, [...])` |
| `<f a=1>...</f>` | `f({ a: 1 }, [...])` |

### Generics

`<For each={items} key={t -> t:id}>{t -> <li>{t:title}</li>}</For>` is
`For({ each: items, key: t -> t:id }, [t -> <li>{t:title}</li>])`.

```pipo
export <T, K>type ForProps = { Signal<T[]> || Memo<T[]> each, Keyer<T, K>? key }
export <T, K>function For(ForProps<T, K> p, ItemView<T>[] children) -> Node
```

`T` is bound by the first argument (`each: Signal<TodoItem[]>` gives
`T = TodoItem`), and then the lambdas in the second are typed with
`ItemView<TodoItem>`. The second half is the existing left-to-right callback
inference. The first half, binding `T` through a field of a record literal
converted to `ForProps<T, K>`, is still a checker addition
([language changes](#language-changes)). It is smaller than draft 3's, which
needed inference from one field of a record into another.



## Attribute names may be kebab-case

An **identifier** cannot contain `-`: `function my-name()` is a syntax error
today and `a-b` is always `a - b`. That stays.

**Attribute names** are different: in an attribute list nothing is an
expression, so `data-user-id` can only be a name. An attribute name is
identifiers joined by `-` with no spaces (`aria-label`, `http-equiv`, `hx-get`).
Where it goes:

1. to the field of the same name with `-` read as `_` (`accept-charset` into
   `accept_charset`, `on-click` into `on_click`), if the record has one;
2. otherwise to the rest field, under its kebab name (`"data-user-id"`);
3. otherwise, for a map parameter, an entry under its kebab name;
4. otherwise it is an error: `ButtonProps has no field 'data-x' and no rest field`.

Tag names stay identifiers, because they are function names that are looked
up: `<my-widget>` is not a tag. Custom elements are written
`<h:element tag="my-widget">`.



## HTML elements without polluting the namespace

```pipo
import web:html -> h                         // one name in scope: h

<h:div class={CARD}>
    <h:h2>{title}</h:h2>
    <h:input placeholder="Name" bind={name}/>
    <h:element tag="sl-button" variant="primary">"Go"</h:element>
</h:div>
```

```pipo
import web:html -> { div, h2, p }            // or import only what the file uses

<div class={CARD}><h2>{title}</h2></div>
```

* `web:html` exports one function per element, generated from a table. Each
  takes its own **open** record: attributes specific to the element are typed
  (`href` on `a`, `checked` on `input`), the generator repeats the global
  attributes in every record, and everything else goes to `attrs`.
* **Void elements** (`input`, `br`, `img`, `meta`) get only the one-parameter
  overload, so `<h:input>"x"</h:input>` is a compile error, which HTML itself
  can only state in prose. Other elements get both overloads (`<h:div/>` and
  `<h:div>...</h:div>`).
* Nothing is global. A module's exports are only visible through the module
  name or an import list, so the hundreds of element functions cost one name
  (`h`), or exactly the names a file imports. `<h:div>` reads like an XML
  namespace and works the same way.
* `element` is the one generic function from the notes:
  `function element({ string tag, ...Map<string, Prop<string>> attrs } a, Child[] children) -> Node`,
  plus the overload without children. It is typed where it can be (the tag
  name, the children) and open everywhere else. The per-element functions are
  thin wrappers over it.



## Beyond HTML: settings files

A record type is already callable with a record (`TodoItem({ ... })`), so a
self-closing tag on a **type** builds a record with no function written at all.
A tag with children calls a function that puts them where they belong:

```pipo
// settings.pp
import "./schema" -> { settings, Server, Database, Feature, CORES, db_url }

const ENV = "production"

export const config = <settings env={ENV}>
    <Server host="0.0.0.0" port=8080 workers={CORES * 2}/>
    <Database url={db_url(ENV)} pool=10/>
    <Feature name="new-checkout" enabled/>
    <Feature name="dark-mode" enabled={ENV != "test"}/>
</settings>
```

```pipo
// schema.pp
export type Server = { string host, int port, int? workers }
export type Database = { string url, int pool }
export type Feature = { string name, bool? enabled }
export type Section = Server || Database || Feature
export type Config = { string env, Section[] sections }

export function settings({ string env } a, Section[] sections) -> Config {
    return Config{ env: a:env, sections }
}
```

A typo in a key, a string where an `int` belongs, or a section that is not a
`Section` is a compile error with a line number, which `settings.xml` cannot
give. The file can also compute, import and reuse, which XML cannot do. The
same holds for routing tables, test fixtures, build descriptions and UI trees
outside the web (`<tui:column>`).



## Considered and rejected

| option | why not |
|--------|---------|
| children as a `children` field, `f({ ...attrs, children })` (draft 3) | [above](#calling-convention-one-argument-or-two): the parser would invent a field name, "takes children" would not be part of the arity, and generic components would need inference across fields of one record |
| Elm style, always `f(attrs, children)` | every element without content would need `[]` (`<input ...></input>`); overloads give `<input/>` instead |
| only `function element(Map<string, unknown> attrs, unknown[] children)` | loses static typing of components; kept as [level 3](#3-map-parameter-dynamic), not the only form |
| raw text with a whitespace rule | [above](#text-is-a-string-literal): every rule is wrong for some element, and it needs a lexer mode |
| tags decided by spacing alone (`<x` tag, `< x` operator) | deterministic, but the difference is one invisible character; reserving prefix `<` makes the meaning visible |
| changing the generic syntax (`function[T]`, `function<T>`) | not needed: generics and tags never meet once the token after `>` is looked at. The notes allow it, but nothing forces it |
| kebab-case identifiers | would make `a-b` mean two things; kebab names are needed only for attributes, where there is no expression |



## Language changes

Done (in the interpreter, the bytecode VM and the IDE plugin):

* **Reserved names**: no `.` or `,` in a declared name; `<` and `>` cannot take
  one parameter.
* **Return types required**: every `function`, `native function`, interface
  requirement and scope states `-> T` (`-> void` when it returns nothing);
  lambdas are exempt. Without it, the checker could not read from a signature
  alone whether a function accepts children.
* **`Map`, `Set`, `Index`, `Length` built in**: declared in the prelude, with
  the C bodies registered as `<prelude>` natives; `std:Map` is now `Map`.

To do:

1. **Tag calls** in the parser (about 200 lines; the lexer does not change).
   The parser produces an ordinary call node with one or two arguments, so the
   checker, the interpreter and all targets see `f({...}, [...])`. The checker
   adds the `/>` / body hint to the "no overload" error of a tag.
2. **Rest fields** in record types: `...Map<string, V> name`, filled by record
   conversion (literals, tags, `decode`).
3. **Record literal to map** conversion when the parameter is `Map<string, V>`
   (for record literals in general, which is simpler to explain than a rule
   only for tags).
4. **`decode(m, T) -> Error<T>`**, a built-in whose second argument is a type.
5. **Generic inference through record fields**, for `<For each={...}>`.
6. Editor grammars: tags, kebab attribute names, string children.



## Plan

1. Rest fields and `decode` (changes 2, 4): useful without tags, testable alone (design: Add `with(T, Partial<T>) -> T` which CREATES a new instance of T and sets certain properties)
2. Tag calls (changes 1, 3), with `err_tag_*` tests for every row of
   [the `<` table](#every-place--appears-today), for both error hints (a body
   given to a function without children, `/>` on a function that needs them),
   and a test library whose element functions build strings.
3. Generic inference through record fields (change 5), with `For` as the test.
4. Editor grammars (change 6).
5. Continue with the plan of WEB.md from its step 3 (reactivity in `web.pp`).



### Design notes

* Add a proper section why function signature `function(attributes, children)` is not desired? This style of signature
blends nicely with the idea that attributes define an option object and the content is a list of children clearly separated
by `>` token. Either way, it would add clear signaling that the function takes or does not take children:

```pipo
type Person = { int age, string name }
<T>type Entity = {
    T value,
    Entity<unknown> children
}

function parent(Person person, unknown[] children) -> Entity {
    return Entity{
        value: person,
        children // make this syntax if it is not already present in the the Pipo language
    }
}

function child(Person person) -> Entity {
    return Entity{
        value: person,
        children: []
    }
}

const p = <parent int={32} name="Tom">
    <child int={5} name="James" />

    <child int={5} name="James">
        // clear error
    </child>

    <parent int={32} name="Tom"/> // also clear error
<parent/>
```
