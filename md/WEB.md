# Web: components, markup and reactivity (draft 2)

A design, not an implementation. It describes `lib/web.pp`, a library for
writing web pages in PiPo, the one calling paradigm it needs from the language,
and the road from there to server and UI written in PiPo alone.

This draft answers the [designer notes](#designer-notes) on the first one:

| note | answer |
|------|--------|
| built-in markup does not match PiPo; solve it in a general way | markup is a **calling paradigm** (`<f a={x}>...</f>` calls `f`), like `a + b` calls `+` and `c[i]` calls `[]`. No name in the language knows about HTML ([tag calls](#tag-calls-a-calling-paradigm)) |
| CSS nesting uses `&`; replacing it breaks nesting | the stylesheet text becomes the **body** of one generated rule, and `&` is left to CSS nesting ([styles](#styles)) |
| no `else` on scopes; `Show` / `For` components instead | control flow is components: `<Show>`, `<For>`, `<Switch>` ([control flow](#control-flow-components)) |
| io, fs and other PC modules cannot work in wasm; the goal is server and UI in PiPo | modules state the targets they run on, platform files pick an implementation per target, and the same components render on the server ([targets](#modules-and-targets), [full stack](#toward-server-and-ui-in-pipo)) |

What stays from the first draft: SolidJS-style fine-grained reactivity with
**explicit dependencies** (`_memo [deps] { ... }`, no implicit tracking),
statically typed props and attributes, and as much as possible in `web.pp`.

Contents: [an example](#a-whole-component) · [tag calls](#tag-calls-a-calling-paradigm) ·
[the other options](#the-other-options-from-the-notes) · [elements and attributes](#elements-and-attributes) ·
[components](#components) · [control flow](#control-flow-components) ·
[reactivity](#reactivity) · [styles](#styles) · [the web.pp API](#the-webpp-api) ·
[the host](#what-the-host-and-the-wasm-target-need) · [targets](#modules-and-targets) ·
[full stack](#toward-server-and-ui-in-pipo) · [language changes](#language-changes) · [plan](#plan)



## A whole component

```pipo
import web -> { signal, $, Signal, Node, Event, css, mount, Show, For, when }
import web:html -> { main, h1, form, input, button, ul, li, span, p, footer }

type TodoItem = { int id, string title, bool done }

const ITEM = css("item", "
    display: flex;
    gap: .5rem;
    padding: .25rem 0;
    &.done span { text-decoration: line-through; opacity: .6 }
")

function toggled(TodoItem t) -> TodoItem {
    return TodoItem({ id: t:id, title: t:title, done: !t:done })
}

function TodoApp() -> Node {
    const Signal<TodoItem[]> items = signal([])
    const draft = signal("")
    let next_id = 0

    // derived values: what they depend on first, then how to compute them
    const left = _memo [items] {
        yield #($items.filter(t -> !t:done))
    }
    const summary = _memo [left, items] {
        yield str($left) + " of " + str(#($items)) + " left"
    }
    const all_done = _memo [left] {
        yield $left == 0
    }

    // a side effect, also with its dependencies spelled out
    _effect [left] {
        web:set_title(str($left) + " todos")
    }

    function add(Event e) -> void {
        e.prevent_default()
        if $draft == "" {
            return
        }

        next_id += 1
        items += TodoItem({ id: next_id, title: $draft, done: false })    // in place: +=(Signal<T[]>, T)
        draft.set("")
    }

    function toggle(int id) -> void {
        items.update(list -> list.map(t -> if t:id == id { yield toggled(t) } else { yield t }))
    }

    return <main>
        <h1>Todos</h1>
        <form on_submit={add}>
            <input placeholder="What next?" bind={draft}/>
            <button disabled={_memo [draft] { yield $draft == "" }}>Add</button>
        </form>
        <ul>
            <For each={items} key={t -> t:id}>
                {t -> <li class={[ITEM, when(t:done, "done")]}>
                    <input type="checkbox" checked={t:done} on_change={_ -> toggle(t:id)}/>
                    <span>{t:title}</span>
                </li>}
            </For>
        </ul>
        <Show when={all_done}>
            {() -> <p>All done!</p>}
        </Show>
        <footer>{summary}</footer>
    </main>
}

mount(<TodoApp/>, "#app")
```

What to notice:

* `<main> ... </main>` is a **tag call**: it calls the function `main` that was
  imported from `web:html`, with one record argument that holds the attributes
  and the children. `<TodoApp/>` calls `TodoApp()`. A tag is a function name
  like any other, looked up where it is written.
* `<For>` and `<Show>` are components of `web.pp`, not syntax. Their children are
  lambdas, so `For` decides when to build an item and `Show` when to build its
  content (and when to throw it away).
* `{summary}` puts a **memo** in the page: the text follows it. `{$summary}` would
  put its **current value**: the text never changes. The difference is in the
  static type (`Memo<string>` vs `string`), so it is visible and checked.
* `_memo [items] { yield ... }` and `_effect [left] { ... }` are scope calls: the
  dependency array is the argument, the block is the code, and `yield` gives the
  memo's value ([LANGUAGE.md](LANGUAGE.md#block-values-yield)). `$items` only
  *reads*; a value that is read but not listed does not trigger a rerun.
* `items += item` and `draft.set("")` write. `+=` is an in-place `op=` function of
  `web.pp` ([`target op= value`](LANGUAGE.md#target-op-value)).
* `#($items)`: a prefix operator binds tighter than a `.` call, so `$` and `#`
  together need the parentheses.



## Tag calls: a calling paradigm

PiPo already has several ways to write a call, and each is defined by the
call it stands for:

| written | calls |
|---------|-------|
| `f(a, b)` | `f(a, b)` |
| `a.f(b)` | `f(a, b)` |
| `a + b`, `-a` | `+(a, b)`, `-(a)` |
| `f a` | `f(a)` |
| `c[i]`, `c[i] = v` | `[](c, i)`, `[]=(c, i, v)` |
| `_s a { ... }` | `_s(block, a)` |
| **`<f a={x}>...</f>`** | **`f({ a: x, children: [...] })`** |

A tag call is the last row: one more way to write a call, for calls whose
arguments are named and that nest.

### The rule

| written | calls |
|---------|-------|
| `<f/>` | `f()` |
| `<f a="text" b={expr} c/>` | `f({ a: "text", b: expr, c: true })` |
| `<f>child child</f>` | `f({ children: [child, child] })` |
| `<f a={x}>child</f>` | `f({ a: x, children: [child] })` |
| `<m:f/>`, `<m:sub:f>...</m:sub:f>` | `m:f()`, `m:sub:f({ children: [...] })`: a `:` path, like any name |

Children:

| child | is |
|-------|----|
| text between tags | a string literal; whitespace is collapsed like in HTML, and a run of whitespace that contains a newline disappears |
| `{ expression }` | the expression, of any type |
| a tag call | its result |

That is the whole feature. In particular:

* **The tag is a function name**, resolved by the usual lookup and overloading.
  `<div>` calls whatever `div` is in scope. There is no list of HTML elements in
  the language, no lower case / upper case rule and no `element("div", ...)`.
* **Attributes are a record literal**, so the function's parameter types them
  with the checker's usual record conversion: a misspelled or missing attribute
  is a compile time error with the usual message, and an attribute of the wrong
  type is an error at that attribute.
* **Children are one field**, `children`, an array literal. Its element type
  comes from the field's declared type (`Child[]`, `View[]`, `ItemView<T>[]`), so a
  component says what it accepts as children, and a component without a
  `children` field cannot be given any.
* Attribute names are identifiers, like field names (`aria_label`, `on_click`).
  The function maps them to whatever it needs (`web:html` writes `aria-label`).
* The end tag repeats the name: `</m:f>`. `<f></f>` is `<f/>`.
* Outside tags, `{` and `}` mean what they always mean. Inside text, `{` starts an
  expression; a literal brace is written `{"{"}`. There are no HTML entities:
  text is UTF-8 (`&`, `©` are written as they are).

### Parsing

In operand position, `<` directly followed by a name starts a tag call. Today a
prefix `<` would be a call of a user-defined prefix operator `<`; that one must
now be written `< x` or `<(x)`. Unchanged:

* `a < b` and `a <b` (a binary operator: `<` after an operand);
* `<T>function`, `<K, V>impl`, `export global <T>function`, ...: a generic
  declaration. At the start of a statement, `<Name>` (or `<A, B>`) followed by
  `function`, `generator`, `scope`, `type`, `impl` or `interface` is a list of
  type parameters. A tag call never contains `,` between `<` and `>`, and its
  attributes are `name=...`, so the two never look alike past the first word.

The tokenizer gets a mode for text between tags (no string escapes, `{` switches
back to expressions). The parser turns the tag into an ordinary call node, so the
checker, the interpreter and all targets see `f({...})`; nothing else changes.
About 250 lines in `lexer.c` / `parser.c`, plus the editor grammars.

### Not only HTML

Because the tag is a function, the same syntax builds any tree that has named
arguments and nested children:

```pipo
// a router: Route and Router are plain functions of an app
<Router fallback={NotFound}>
    <Route path="/" view={Home}/>
    <Route path="/todo/:id" view={TodoPage}/>
</Router>

// SVG, from web:svg
<svg:svg width={200} height={100}>
    <svg:circle cx={50} cy={50} r={radius}/>
</svg:svg>

// a terminal UI, a PDF layout, a test fixture: whatever the functions build
<tui:column gap={1}>
    <tui:text bold>Status</tui:text>
    <tui:progress value={done} max={total}/>
</tui:column>
```



## The other options from the notes

| option | verdict |
|--------|---------|
| 1. full markup | **this design**, but as a calling paradigm instead of an HTML feature |
| 2. `html(<div>...</div>)`, a macro | needs a macro system: token trees, `quote`, running PiPo while compiling, hygiene, error positions inside the macro input. Several times the work, and the checker only sees the expansion. A tag call is exactly what such a macro would expand to, so macros can come later without changing any component |
| 3. `` html`<div>...${x}</div>` ``, a template string | the markup is a string: parsed at runtime, untyped holes (`${x}` is `unknown`), errors at runtime. Typing it would need the macro system of option 2 |
| 4. `div(class="x")[p["Hello"], ...]` | works almost today: `div({ class: "x" })` is a call and `[]` is an overloadable function with several indices. It is option 1 without the end tags; it reads worse for deep trees and needs named arguments (`class = "x"`) to look like the notes |
| 5. `div{class="x", [...]}` | a record-like call syntax. `Type{...}` already means "make a record of this type", so this would give it a second meaning |

The tag call is option 1 written as option 4: `<div class="x"><p>Hello</p></div>`
is `div({ class: "x", children: [p({ children: ["Hello"] })] })`.



## Elements and attributes

`web:html` exports one function per HTML element, each with its own attribute
record, so `<a href>` and `<input checked>` are typed per element:

```pipo
<T>type Prop = T || Signal<T> || Memo<T>      // a fixed value, or one the attribute follows

export type InputAttrs = {
    Prop<string>? id,
    Class? class,
    Prop<string>? type,
    Prop<string>? placeholder,
    Prop<string>? value,
    Prop<bool>? checked,
    Prop<bool>? disabled,
    Signal<string>? bind,               // two-way: the input sets it, it sets the input
    Handler? on_input,
    Handler? on_change,
    Ref? ref,
    std:Map<string, string>? data       // data-* attributes
}

export function input() -> Node
export function input(InputAttrs a) -> Node
```

The module is generated from a table of elements and attributes (the same
table can generate the editor's completion). What an attribute value means
follows from its type:

| written | means |
|---------|-------|
| `id="x"`, `disabled` | a fixed attribute (`disabled` alone is `true`) |
| `title={s}`, `s: Signal<string>` or `Memo<string>` | the attribute follows `s` |
| `title={$s}` | set once from the current value |
| `disabled={b}`, `b: Signal<bool>` or `Memo<bool>` | present while `b` is true |
| `on_click={handler}` | `handler: (Event) -> void`; runs in a batch |
| `bind={s}` | two-way, for `input`, `textarea`, `select` (`Signal<string>`, `Signal<bool>` for checkboxes) |
| `class={[ITEM, when(done, "done")]}` | a class list: strings, and `when(condition, name)` parts that follow a `Prop<bool>` |
| `style={s}` | a style text, fixed or following a signal |
| `ref={r}` | `r: Ref` gets the DOM node, for focus, measuring, canvas |

Children of elements are `Child`:

```pipo
type Child = string || int || float || Node || Node[] || Reactive
```

A `Reactive` child (a signal or memo of any type) is a text node that follows
`str($x)`. A `Node[]` child is how a component returns several nodes without a
wrapper (there are no fragments: an array literal is already a value).



## Components

```pipo
export type ButtonProps = { string label, Memo<bool> busy, Handler on_press }

export function Button(ButtonProps p) -> Node {
    return <button class={BUTTON} disabled={p:busy} on_click={p:on_press}>
        <Show when={p:busy}>
            {() -> <Spinner/>}
        </Show>
        {p:label}
    </button>
}

// used as
<Button label="Save" busy={saving} on_press={_ -> save()}/>
```

* A component is a function from one props record to a `Node`. It runs once.
  Without props it takes no parameters (`<TodoApp/>` calls `TodoApp()`).
* Components are written in `PascalCase` and elements in lower case. That is a
  convention that makes the two easy to tell apart, not a rule: both are
  functions, and the language does not look at the case.
* Optional props are `Option` fields (`string? hint`); the record literal must
  name every other field, as in any record conversion.
* A prop that should stay live is typed `Signal<T>`, `Memo<T>`, `Prop<T>` or
  `Reactive`, so the caller decides what is live by what it passes.
* A component that takes children declares the field: `Child[] children` (any
  content), `View[] children` (built lazily), or a type of its own.



## Control flow components

Markup that appears, disappears or repeats is built **lazily**: the component
gets a lambda, calls it when the markup is needed, and owns what it built (see
[owners](#lifetimes-owners)). The lambda is an ordinary child expression.

```pipo
<Show when={signed_in} fallback={() -> <a href="/login">Sign in</a>}>
    {() -> <p>Hello {name}</p>}
</Show>

<For each={todos} key={t -> t:id}>        // keyed by id: an item keeps its DOM when the list changes
    {t -> <li>{t:title}</li>}
</For>

<Switch on={route}>
    <Case value="/">{() -> <Home/>}</Case>
    <Case value="/about">{() -> <About/>}</Case>
    <Default>{() -> <NotFound/>}</Default>
</Switch>
```

```pipo
export type View = () -> Node
export <T>type ItemView = T -> Node

export type ShowProps = { Prop<bool> when, View? fallback, View[] children }
export function Show(ShowProps p) -> Node

export <T, K>type ForProps = { Signal<T[]> || Memo<T[]> each, Keyer<T, K>? key, ItemView<T>[] children }
export <T, K>function For(ForProps<T, K> p) -> Node
```

* `Show` builds `children[0]()` when `when` becomes true and disposes it when it
  becomes false; `fallback` the other way around. While the condition does not
  change, nothing is rebuilt.
* `For` builds one item per element. Items whose key stays keep their nodes and
  owner; new keys are built; removed keys are disposed; a key whose value changed
  is rebuilt. Without `key`, items are keyed by position.
* `Switch` / `Case` / `Default` choose by `==` on a `Prop<T>`. `Case` returns a
  description that `Switch` reads (a record, not a DOM node), which is fine
  because tags are just calls.
* Static control flow needs nothing: `{if wide { yield <Wide/> } else { yield <Narrow/> }}`
  and `{names.map(n -> <li>{n}</li>)}` run once, when the component runs.
* Why lambdas and not plain children: plain children are values, built before
  `Show` is even called, so a hidden branch would be built (and its effects would
  run) anyway. The lambda is the one explicit word that says "later", in the
  same spirit as the explicit dependency lists.



## Reactivity

### Values

| type | made by | read | write |
|------|---------|------|-------|
| `Signal<T>` | `signal(initial)` | `$s` | `s.set(v)`, `s.update(f)`, `s += x` and the other `op=` |
| `Memo<T>` | `_memo [deps] { ... yield v }` | `$m` | (derived) |
| `Resource<T>` | `_resource [deps] { ... }` | `$r: Load<T>` | `r.refetch()` |

All of them implement `interface Reactive`, so one dependency array can mix them:
`[count, name, user]` is a `Reactive[]`.

### No implicit tracking

`$s` is `<T>function $(Signal<T> s) -> T`: it returns the value and does nothing
else. Dependencies exist only where they are written:

```pipo
_effect [count] {
    io:print_nl($count, $name)       // reruns when count changes; name is only read
}
```

No tracking scope, no `untrack`, no surprises when a read moves into a helper or
behind an `if`. A listed dependency that is not read only costs a rerun. A
**debug check** (development builds) can warn when an effect reads a signal it
does not list, by recording `$` reads during a run; it reports, it never
subscribes.

One consequence to design around: a dependency list is fixed when the memo is
made, so it cannot name signals that appear later (one per list item). Keep list
items as plain records in one `Signal<T[]>` and replace an item to change it
(`toggled(t)` in the example), or give each item component its own memos over
the item it got.

### How it runs

* Every reactive value is a node in a graph with its subscribers and a
  **height** (0 for a signal, 1 + the highest dependency for a memo).
* `s.set(v)` does nothing if `v == $s` (structural `==`); otherwise it stores `v`,
  marks the subscribers dirty and schedules a flush.
* The flush runs dirty memos in height order (so a memo never sees a half
  updated input), then effects in creation order. A memo whose new value equals
  the old one does not mark its own subscribers.
* `_batch { ... }` delays the flush to the end of the block; event handlers run in
  an implicit batch, so a handler that writes three signals updates the DOM once.
* Effects run once when created, then on changes.

### Lifetimes: owners

A component runs once, but what it creates (effects, memos, listeners) must go
away when its markup leaves the page. Everything created while a component or a
`Show` / `For` lambda runs belongs to the **current owner**; disposing an owner
disposes what it owns.

* `_root { ... }` makes a new owner and gives back its `Disposer`;
* `_cleanup { ... }` registers code to run when the current owner is disposed;
* `Show`, `For` and `Switch` make an owner per branch / item.

The current owner decides only *lifetime*, never *what reruns*. Disposing also
unpins the closures the page held (see [the host](#what-the-host-and-the-wasm-target-need)),
which is what lets the wasm collector free them.



## Styles

```pipo
const CARD = css("card", "
    padding: 1rem;
    border-radius: .5rem;
    background: var(--surface);

    & > h2 { margin: 0 }
    &:hover { outline: 1px solid var(--accent) }
    &.selected { border: 2px solid var(--accent) }
    @media (width < 600px) { padding: .5rem }
")

<section class={CARD}>...</section>
```

* `css(name, text) -> string` returns a class name unique to the stylesheet
  (`card-3f9a`: the name and a hash of the text) and adds one rule,
  `.card-3f9a { <text> }`. The text is the **body** of that rule: declarations at
  its top apply to the element, and nested rules use `&` exactly as CSS nesting
  defines it (the parent selector, here `.card-3f9a`). Nothing in the text is
  replaced, so `&`, `@media`, `@container` and plain nested selectors
  (`.title { ... }` for descendants) all mean what the CSS specification says.
* **Hoisting.** When both arguments are literals, the wasm target evaluates
  `css` while compiling, writes the rule into `<out>.css`, links it from
  `index.html`, and compiles the call to the class name. A computed text is
  added at runtime to a `<style>` element instead, once per distinct text.
* **Old browsers.** CSS nesting is supported by every current browser. For older
  ones, the compiler can flatten the hoisted rules (`.card-3f9a > h2 { ... }`)
  while writing the `.css` file; the source stays the same.
* `global_css(text)` adds text as it is (resets, fonts, variables).
* Dynamic styling uses `class` lists with `when(...)` and `style` bindings.
  There is no CSS-in-PiPo object syntax: CSS property names are not PiPo names,
  and a typed CSS model is a project of its own.



## The web.pp API

Signatures as they would be written in `lib/web.pp`.

### Reactivity

```pipo
export interface Reactive {
    function graph_node(Reactive) -> GraphNode       // the node subscribers attach to
    function text(Reactive) -> string                // str of the current value, for text children
}

export type Disposer = () -> void
<T>type Update = T -> T

export <T>function signal(T initial) -> Signal<T>
export global <T>function $(Signal<T> s) -> T
export global <T>function $(Memo<T> m) -> T
export <T>function set(Signal<T> s, T value) -> void
export <T>function update(Signal<T> s, Update<T> f) -> void
export global function +=(Signal<int> s, int n) -> void          // and -=, *=, ... for int / float
export global function +=(Signal<string> s, string tail) -> void
export global <T>function +=(Signal<T[]> s, T item) -> void      // push, then notify

export <R>scope _memo(Callback<void, R> code, Reactive[] deps) -> Memo<R>
export scope _effect(Callback<void> code, Reactive[] deps) -> Disposer
export scope _batch(Block<void> code) -> void
export scope _root(Callback<void> code) -> Disposer
export scope _cleanup(Callback<void> code) -> void
export <T>function readonly(Signal<T> s) -> Memo<T>              // hand out without the setter
```

### Async data

```pipo
export <T>enum Load<T> { loading, ready(T), failed(string) }

export <T>scope _resource(Callback<Fetcher<T>> code, Reactive[] deps) -> Resource<T>
export <T>function $(Resource<T> r) -> Load<T>
export <T>function refetch(Resource<T> r) -> void

export scope _fetch(Callback<Error<Response>> code, string url) -> void
export function text(Response r) -> string
export <T>function json(Response r) -> Error<T>                  // decoded and checked against T
```

`json<T>` decodes into a statically typed value: the wasm target already has a
type descriptor for every type, so the host can check the parsed JSON against
`T` and fail with a path (`$.items[3].id: expected int`) instead of letting a
wrong shape in.

### Elements, components, the page

```pipo
export type Node = { int handle }                    // a DOM node, owned by the host
export type Handler = (Event) -> void
export type Class = Prop<string> || ClassPart[]
export function when(Prop<bool> on, string name) -> ClassPart

export function Show(ShowProps p) -> Node
export <T, K>function For(ForProps<T, K> p) -> Node
export <T>function Switch(SwitchProps<T> p) -> Node

export function mount(Node root, string selector) -> Disposer
export function css(string name, string text) -> string
export function global_css(string text) -> void
export function set_title(string title) -> void

// web:html: one function per element (generated), e.g.
export function div() -> Node
export function div(DivAttrs a) -> Node
```

### Events, timers, the page

```pipo
export type Event = { int handle }
export function prevent_default(Event e) -> void
export function value(Event e) -> string             // event.target.value
export function key(Event e) -> string               // KeyboardEvent.key

export scope _every(Callback<void> code, int ms) -> Disposer      // owned: stops with its owner
export scope _after(Callback<void> code, int ms) -> Disposer
export scope _frame(Callback<float> code) -> Disposer             // requestAnimationFrame; scope: the time

export function location() -> Signal<string>          // the path, follows history
export function navigate(string path) -> void
export function local(string key, string initial) -> Signal<string>   // backed by localStorage
```



## What the host and the wasm target need

`web.pp` is PiPo; under it is a small set of natives in the generated `.mjs`
(the mechanism of [TARGETS.md](TARGETS.md#natives-through-the-host)):

| native | JavaScript |
|--------|------------|
| `dom_create(tag) -> int` | `document.createElement`, the element stored in a handle table |
| `dom_text(s) -> int`, `dom_set_text(h, s)` | text nodes |
| `dom_attr(h, name, value)`, `dom_remove_attr(h, name)` | attributes |
| `dom_prop(h, name, value)` | properties (`value`, `checked`) |
| `dom_class(h, name, on)`, `dom_style(h, text)` | classes, styles |
| `dom_insert(parent, child, before)`, `dom_remove(h)` | tree changes |
| `dom_listen(h, event, closure) -> int`, `dom_unlisten(id)` | events: the handler is a PiPo closure |
| `dom_query(selector) -> int?` | `mount` |
| `timer(ms, repeat, closure) -> int`, `frame(closure) -> int`, `cancel(id)` | timers |
| `fetch(url, closure)` | `fetch(...).then(...)`, the closure gets an `Error<Response>` |
| `storage_get`, `storage_set`, `history_push`, `on_popstate` | page state |

The element functions of `web:html` are PiPo over these: they create the node,
apply each attribute that is present (a reactive one through an `_effect` owned
by the current owner) and append the children.

What the wasm target must add:

* **Calls from JavaScript into PiPo closures** go through `enter()` (exists), which
  counts the depth so the collector runs only between events. Event handlers are
  wrapped in a batch.
* **Pinning.** A closure handed to `dom_listen`, `timer` or `fetch` is pinned (`pin`,
  exists) and unpinned when its owner is disposed. DOM nodes live in the host's
  handle table; disposing an owner frees their handles. Nothing on the page keeps
  wasm memory alive except through a pin, so the conservative collector is enough.
* `--web`: write `index.html` with a `<div id="app">`, the `.css` file and a
  module script, and serve the folder during development (`pp serve`).
* What `web.pp` uses must compile to wasm: generic records, interfaces with
  generic impls, closures, scopes, generators and `std:Map` / `std:Set` for keyed
  lists (all supported).
* Later: `externref` for DOM nodes instead of an integer handle table, and WasmGC
  to share one collector with the page.



## Modules and targets

A wasm program runs in a page: there is no file system, no process, no
standard input. Today such an import fails late (at the first native the wasm
target cannot call). It should fail at the import, with a reason, and a library
should be able to offer one interface with an implementation per target.

**Targets of a module.** A module states where it can run, in its first line:

```pipo
target pc                 // lib/fs.pp, lib/os.pp: the interpreter, the bytecode VM, native code
target web                // lib/web.pp: wasm in a page
                          // no line: anywhere (the default)
```

Importing a module on a target it does not list is a compile time error at the
`import`: `module 'fs' runs on the pc target, not on wasm (it needs a file system)`.
`io` stays available everywhere: on the web, `io:print` writes to the console and
`io:read_line` is not provided (an error at its call, as for any missing native).

**Platform files.** `import "./storage"` looks for `storage.<target>.pp` before
`storage.pp`, so a module can have `storage.web.pp` (backed by `localStorage`)
and `storage.pc.pp` (backed by files). Each must export the same names with the
same types; the checker compares them when both exist. This is the same idea as
React Native's `.ios.js` / `.android.js` files: no conditional compilation in
the language, just a lookup rule.

`pp:target`, a constant with the current target's name, covers the small cases
(`if pp:target == "web" { ... }`); both branches are checked, so they may only use
modules that exist on every target the file is built for.



## Toward server and UI in PiPo

The end goal from the notes: the server and the UI written in PiPo, sharing
types and code. What this design already gives, and what is left:

1. **Shared code.** A module without a `target` line (types, validation,
   formatting) is imported by both sides unchanged.
2. **Server rendering.** Components only call element functions. On the pc
   target, `web:html` is a platform file whose element functions build an HTML
   string tree instead of DOM nodes (`web:html.pc.pp`), and signals are read
   once. The same `<TodoApp/>` renders to a string on the server. The client
   then **hydrates**: the element functions walk the existing DOM instead of
   creating it, attaching listeners and effects.
3. **Typed data across the wire.** `json<T>` on both sides checks the shape
   against the same `T`, so a type change that breaks the protocol is found
   where the data is decoded, with a path.
4. **A server module.** An `http` module for the pc target: routes are functions,
   requests and responses are records, a handler returns `Error<Response>`. Tag
   calls make routing tables readable (`<Route path="/api/todos" get={list_todos}/>`).
5. **Calls from the client to the server.** Later: a module marked
   `target server` whose exported functions the client imports as **stubs**
   with the same signatures that `fetch` and decode with `json<T>`. Every call
   across the network is asynchronous, so a stub returns `Resource<T>` (or takes
   a callback), never `T`. Generators ([ITER.md](ITER.md)) are the natural base
   for a later `async` style: a generator that returns pending requests and is
   resumed with their results.

The runtime model on the server is that of Node.js / Deno, as the notes ask:
one process, an **event loop**, and non-blocking natives (sockets, files,
timers) that take a callback, which is the same shape as the page (handlers
called from the host between which the collector runs). The bytecode VM is the
natural host for it; `fs` and `os` keep their blocking functions for scripts and
gain callback versions for servers.

Steps 1-3 need nothing beyond this document. Steps 4 and 5 are their own designs.



## Language changes

Everything else is library code.

### 1. Tag calls (required)

[Above](#tag-calls-a-calling-paradigm). A new calling paradigm in the lexer and
parser; the result is an ordinary call node.

### 2. A value type for kept blocks: `Callback<T, R>` (required)

`web.pp` keeps blocks to **rerun** them (a memo's body), so it needs the type of
what they `yield`:

```pipo
<R>scope _memo(Callback<void, R> code, Reactive[] deps) -> Memo<R>
const n = _memo [items] { yield #($items) }      // R = int, n is a Memo<int>
```

`Callback<T>` stays as it is (`R = void`). The checker already knows what a
`Block` yields per scope call; `Callback<T, R>` lets a scope's signature name it.

### 3. Generic scopes (required)

`<T>scope ...` must bind `T` from its arguments through the same overload
machinery as functions (`_resource [user] { ... }` making a `Resource<T>`).

### 4. `[]` typed by the parameter (small)

`_effect [] { ... }` (run once) needs `[]` to take its type from the parameter
(`Reactive[]`). The checker does this for `push` and `std:map()` (first use
decides); it has to do it for arguments too.

### 5. Targets of modules and platform files (required for the full stack)

[Above](#modules-and-targets): the `target` line, the import error, and the
`<name>.<target>.pp` lookup.

Not needed any more, compared with the first draft: `else` after a scope block
(control flow is components), the special markup names (`element`, `attribute`,
`text`, `child`, `on`, `bind`, ...), and `with` for records.



## Plan

1. Language: `Callback<T, R>`, generic scopes, `[]` typed by the parameter
   (changes 2-4). Small, testable with the interpreter alone.
2. Tag calls in the lexer and parser (change 1) and the editor grammars, with a
   test library whose element functions build strings (server rendering comes
   for free).
3. `lib/web.pp` reactivity (signals, memos, effects, batches, owners) and
   `Show` / `For` / `Switch` over a fake DOM, run and tested on the interpreter
   and bytecode targets before any browser is involved.
4. Module targets and platform files (change 5).
5. Wasm: the DOM natives in the host, pinning, `--web` output, `css` hoisting
   (maps and sets: done).
6. Examples: counter, todo list, a fetch with `_resource`, a router.
7. Server rendering and hydration; then the `http` module.



# Designer notes

* The core idea of the Pipo language is to ensure great developer experience and almost unparallel expressiveness with 
the least number of features implemented. Now it should be clear that built-in support for markup language does not 
match the identity of Pipo. I would like to either see a system like multiple function calling paradigms or scopes that
solve a problem in general way. The system should allow markup-ish syntax directly into the language. The markup-ish syntax
could be represented by:

```pipo
// Sorted by personal bias

// 1 Full markup support
const element = <div class="hello-world">
    <p>Hello world!</p>
    <button onClick={() -> {}}>My button</button>
</div>

// 2 macro style markup
const element = html(
    <div class="hello-world">
        <p>Hello world!</p>
        <button onClick={() -> {}}>My button</button>
    </div>
)

// 3 JS-template style markup
const element = html`
    <div class="hello-world">
        <p>Hello world!</p>
        <button onClick="${() -> {}}">My button</button>
    </div>
`

// 4 Function-ish based markup
const element = div(class="hello-world")[
    p["Hello world!"],
    button(onClick=() -> {})["My button"]
]

// 5 Struct-ish based markup
const element = div{class="hello-world", [
    p{["hello-world"]},
    button{onClick=()->{}, "My button"}
]}
```

* The CSS system seems kinda odd... How is the CSS nesting gonna work if all & are gonna be replaced with autogenerated 
CSS class? The & is used in the CSS nesting structure as a token
* No, scopes are not gonna get `else` clause. Making _show and _each into Show and For components would have been a better idea
* It should be noted that if compiled to WASM then it is expected that io, fs, and other strictly PC application modules 
are not going to work. The end-end-end goal is to have complete support for server as well as UI thats all built via Pipo.
Similar architecture to NodeJS/Deno. It is not really relevant right now... but it will be down the line


# Designer notes (2)

* The new function calling convention looks good, but it needs to be thoroughly explored if some generics dont break with
this change. The language is in very early development such that the generic syntax can be changed easily. The typing of 
the function also needs to be explored because having hundreds of functions named after html elements would pollute the 
namespace, the simple solution would be to make a generic <element tag="..."> function. Also, the default html elements
can have many different (basically infinite if we are talking about data-* attributes). The type system needs to be able
to accommodate it. I would love a strictly typed interface such as the one already present in the WEB.md but I am scared
that the default HTML elements could break it. The simplest option would be to make markup calling only available for
functions with signature: `function element(std:map<string, unknown> props, unknown[] children) -> [anything]` which is 
an option but then it would be nice if Pipo had built-in function that would take std:map<string, unknown> and a type and 
it would return Error<type>. Error if parsed, type instance if valid... This option removes the type safety of the 
components which I value greatly.
* If the parser could determine 100% tag calling instead of generics or programmer defining functions with names `<` 
and `>` then the calling convention looks good and it has use cases beyond just HTML. It could be used for general app 
settings and thus the language itself could be used as an settings file for example `settings.xml` could be transformed to
`settings.pp` and harness all powers of Pipo language
* Things that needs to be solved before the markup/tag call convention is accepted:
  1. Strongly typed with general fallback to preferably `std:map<string, unknown>`
  2. The parser must be able to distinguish between generics and user defined function names such as `<` or `>` or `="`
* I dont like this: "text between tags | a string literal; whitespace is collapsed like in HTML, and a run of whitespace 
that contains a newline disappears". What about <pre> element that needs the whitespace 
* This section "Not only HTML" If I am not mistaken, kebab-case function names are permitted by the language and they are 
distinguished in case of `minus(var1, var2) vs var1-var2` minus operation