# Web: components, markup and reactivity (draft)

A design, not an implementation. It describes `lib/web.pp`, a library for
writing web pages in PiPo, and the few language changes it needs. A component
is one `.pp` file with its markup, logic and styles; `pp --target=wasm --web`
turns the program into `app.wasm`, `app.mjs`, `app.css` and `index.html`.

The goals, from the request:

* **Markup that looks like HTML**, not `element("div", ...)` calls and not nested
  scope calls (`_div class: "x" { _span { ... } }` was tried and rejected).
* **Styles** next to the component, hoisted to a `.css` file when possible.
* **SolidJS-style fine-grained reactivity**: components run once, and a change
  updates only the DOM that depends on it. Unlike Solid, **no implicit
  tracking**: `effect` and `memo` take the reactive values they depend on first,
  then the code.
* **As little language as possible**: everything that can be library code is in
  `web.pp`.

Contents: [an example](#a-whole-component) · [language changes](#language-changes) ·
[reactivity](#reactivity) · [markup](#markup) · [styles](#styles) ·
[the web.pp API](#the-webpp-api) · [the host](#what-the-host-and-the-wasm-target-need) ·
[macros instead?](#alternative-a-macro-system) · [plan](#plan)



## A whole component

```pipo
import web -> { signal, $, Signal, Node, Event, css, mount }

type TodoItem = { int id, string title, bool done }

const ITEM = css("item", "
    & { display: flex; gap: .5rem; padding: .25rem 0 }
    &.done span { text-decoration: line-through; opacity: .6 }
")

function TodoApp() -> Node {
    const Signal<TodoItem[]> items = signal([])   // [] typed by the declared type (change 4)
    const draft = signal("")
    let next_id = 0

    // derived values: what they depend on first, then how to compute them
    const left = _memo [items] { #($items.filter(t -> !t:done)) }
    const summary = _memo [left, items] { str($left) + " of " + str(#($items)) + " left" }
    const all_done = _memo [left] { $left == 0 }

    // a side effect, also with its dependencies spelled out
    _effect [left] {
        web:set_title(str($left) + " todos")
    }

    function add(Event e) {
        e.prevent_default()
        if $draft == "" {
            return
        }

        next_id += 1
        items += TodoItem({ id: next_id, title: $draft, done: false }) // in place: +=(Signal<T[]>, T)
        draft.set("")
    }

    function toggle(int id) {
        items.update(list -> list.map(t -> if t:id == id { t.with({ done: !t:done }) } else { t }))
    }

    return <main>
        <h1>Todos</h1>
        <form on:submit={add}>
            <input placeholder="What next?" bind:value={draft}/>
            <button disabled={_memo [draft] { $draft == "" }}>Add</button>
        </form>
        <ul>
            {_each items, t -> t:id {
                <li class={ITEM} class:done={scope:done}>
                    <input type="checkbox" checked={scope:done} on:change={_ -> toggle(scope:id)}/>
                    <span>{scope:title}</span>
                </li>
            }}
        </ul>
        {_show all_done {
            <p>All done!</p>
        }}
        <footer>{summary}</footer>
    </main>
}

mount(<TodoApp/>, "#app")
```

What to notice:

* `<main> ... </main>` is a **markup literal**: an expression that the parser
  turns into ordinary calls (`element`, `attribute`, `on`, `child`, ...), which the
  checker then types and resolves by overloading like any call. Nothing in the
  language knows about HTML.
* `{summary}` puts a **signal** in the page: the text follows it. `{$summary}`
  would put its **current value**: the text never changes. The difference is in
  the static type (`Memo<string>` vs `string`), so it is visible and checked.
* `_memo [items] { ... }` and `_effect [left] { ... }` are **scope calls**: the
  dependency array is the argument, the code is the block. Inside, `$items`
  only *reads*; it does not subscribe to anything. A value that is read but not
  listed does not trigger a rerun, which is exactly what you asked for (and
  what Solid needs `untrack` for).
* `items += item` and `draft.set("")` write. `+=` is an in-place `op=` function
  of `web.pp` (see [`target op= value`](LANGUAGE.md#target-op-value)), so it
  needs no language support.
* `_each items, t -> t:id { ... }` is a keyed list, `_show all_done { ... }`
  conditional markup; both are scopes too.
* `#($items)`: a prefix operator binds tighter than a `.` call (`-a.b()` is
  `-(a.b())`), so `$` and `#` together need the parentheses.



## Language changes

Five changes, in order of necessity. Everything else is `web.pp`.

### 1. Markup literals (required)

An expression may be a markup literal. In operand position, `<` directly
followed by a name (or `>`) starts one; it ends at the matching close tag or at
`/>`. Inside, text is literal, `{ expression }` is PiPo, and attributes are
`name="text"`, `name={expression}`, `name` (true) or `space:name={expression}`.

The parser desugars it, so the checker, the interpreter and every target see
plain calls:

| markup | becomes |
|--------|---------|
| `<div a="x" b={e} c ns:d={f}>...</div>` | `element("div", [attribute("a", "x"), attribute("b", e), attribute("c", true), ns("d", f)], [...children])` |
| `<my-widget/>` (lower case or with `-`) | `element("my-widget", [], [])` |
| `<Card title="x">...</Card>` (capitalized) | `Card({ title: "x" }, [...children])`, or `Card({ title: "x" })` without children |
| `<Card {...props}/>` | `Card(props)` |
| `<>...</>` | `fragment([...children])` |
| text between tags | `text("...")` (whitespace collapsed like HTML: a run with a newline disappears) |
| `{ expression }` as a child | `child(expression)` |

* The names (`element`, `attribute`, `text`, `child`, `fragment`, and the
  namespaces `on`, `bind`, `class`, `style`, `ref`, ...) are looked up where the
  literal is written, like any call: `web.pp` declares them `global`, so
  `import web` is enough. Another library can provide its own `element` that
  builds strings (server-side rendering), SVG or a test tree; the literal does
  not care.
* **Components are functions.** `<Card title="x"/>` calls `Card` with a record
  literal of the attributes, so the props are **statically typed** by the
  component's parameter (`function Card(CardProps p, Node[] children) -> Node`):
  a missing or misspelled prop is a compile time error, with the usual message.
* Attribute values are typed by **overloading**: `attribute(string, string)`,
  `attribute(string, bool)`, `attribute(string, Signal<string>)`,
  `attribute(string, Memo<bool>)`, ... A `Signal<int>` where `web.pp` only
  accepts strings and bools is an error at the attribute.
* Children are typed the same way: `child(string)`, `child(int)`, `child(Node)`,
  `child(Node[])`, `child(Option<Node>)`, `<T>child(Signal<T>)`, ...
* Ambiguity: a prefix `<` operator (operators are names) would collide. Markup
  wins when `<` is immediately followed by a letter or `>`; a user-defined prefix
  `<` must be written `< x` or `<(x)`. `a < b`, `a <b` (binary) and `<T>function`
  (at statement start, followed by `>` then `function`/`type`/`scope`) are
  unchanged.
* The tokenizer needs a mode for markup text (no string escapes, `{` switches
  back to expressions). That is the bulk of the work: about 300 lines in
  `parser.c` and `lexer.c`, plus the editor grammars.

Why not scopes for HTML: they make every element a call with a block,
`_div class: "x" { _span { text "hi" } }`. That is uniform but noisy, children
are statements instead of values, and a static tree is impossible to see.
Markup literals read like HTML and still compile to typed calls.

### 2. A result type for kept blocks: `Callback<T, R>` (required)

Today a `Callback<T>` (a block kept after its scope call) gives nothing: its
value is unknown when the checker types the scope call. `web.pp` keeps blocks to
**rerun** them (a memo's body, a list item's markup), so it needs their
value.

* `Callback<T, R>` / `Block<T, R>`: the second argument is the block's value
  type; `R` is bound from the block's last expression, like a generic
  callback's result:

  ```
  <R>scope _memo(Callback<void, R> code, Reactive[] deps) -> Memo<R>
  const n = _memo [items] { #$items }      // R = int, n is a Memo<int>
  ```

* `Callback<T>` stays as it is (`R = void`).

### 3. Generic scopes, and several values for a block (required)

`<T>scope ...` must bind `T` from its arguments (`_each items` with
`items: Signal<T[]>` makes the block's `scope` a `T`). If generic scopes do
not already work through the same overload machinery as functions, this
makes them.

### 4. An empty dependency list and `void` values (small)

`_effect [] { ... }` (run once) needs `[]` to take its type from the parameter
(`Reactive[]`). The checker already does this for `push` and `std:map()` (first
use decides); it has to do it for arguments too.

### 5. Optional: `with` for records

`t.with({ done: true })` (a copy with some fields replaced) is a generic
function over closed records: `<R>function with(R base, partial-of-R changes) -> R`.
A "partial" record type does not exist yet; without it `web.pp` cannot write
`with`, and code builds the record again. Nice to have, not needed.

Nothing else: event handlers are lambdas with types from `Handler`, `+=` on a
signal is an `op=` function, `$` is a prefix operator (`$` is an operator
character), asynchronous work uses callbacks (`_fetch url { ... }`).



## Reactivity

### Values

| type | made by | read | write |
|------|---------|------|-------|
| `Signal<T>` | `signal(initial)` | `$s` | `s.set(v)`, `s.update(f)`, `s += x` and the other `op=` |
| `Memo<T>` | `_memo [deps] { ... }` | `$m` | (derived) |
| `Resource<T>` | `_resource [deps] { fetch... }` | `$r: Load<T>` | `r.refetch()` |
| `Store<T>` | `store(record)` | `$st:field` | `st:field.set(v)` (each field is a signal) |

All of them implement `interface Reactive` (generic `impl Reactive for Signal<T>`),
so one dependency array can mix them: `[count, name, user]` is a `Reactive[]`.

### No implicit tracking

`$s` is `<T>function $(Signal<T> s) -> T`: it returns the value and does
nothing else. Dependencies exist only where they are written:

```
_effect [count] {
    io:print_nl($count, $name)       // reruns when count changes; name is only read
}
```

Consequences: no "tracking scope", no `untrack`, no surprises when a read moves
into a helper function or behind an `if`; a dependency that is listed but not
read only costs a rerun. A **debug check** (development builds) can warn when
an effect reads a signal it does not list, by recording `$` reads during a run;
it reports, it never subscribes.

### How it runs

* Every reactive value is a node in a graph: its subscribers (memos, effects,
  DOM bindings) and a **height** (0 for a signal, 1 + the highest dependency for
  a memo).
* `s.set(v)` does nothing if `v == $s` (structural `==`); otherwise it stores
  `v`, marks the subscribers dirty and schedules a flush.
* The flush runs dirty memos in height order (so a memo never sees a half
  updated input: "glitch free"), then effects in creation order. A memo whose
  new value equals the old one does not mark its own subscribers.
* `_batch { ... }` delays the flush to the end of the block; event handlers run
  in an implicit batch, so a handler that writes three signals updates the DOM
  once.
* Effects run once when created (as in Solid), then on changes.

### Lifetimes: owners

A component runs once, but what it creates (effects, memos, DOM listeners)
must go away when its markup leaves the page (a `_show` turning false, an item
removed from `_each`). As in Solid, everything created while a component or a
control flow block runs belongs to the **current owner**, and disposing an owner
disposes what it owns:

* `_root { ... }` makes a new owner and gives back its `Disposer`;
* `_cleanup { ... }` registers code to run when the current owner is disposed
  (clear a timer, close a socket);
* `_show`, `_each`, `_switch` make an owner per branch / item.

The current owner is implicit context, but it decides only *lifetime*, never
*what reruns*, so it does not bring back the implicit-dependency problem.
Disposing also unpins the closures the page held (see [the host](#what-the-host-and-the-wasm-target-need)),
which is what lets the wasm collector free them.



## Markup

### Elements and bindings

| in markup | means |
|-----------|-------|
| `class="a b"`, `id="x"` | a static attribute |
| `title={s}` with `s: Signal<string>` / `Memo<string>` | the attribute follows `s` |
| `title={$s}` | set once from the current value |
| `disabled={b}` with `b: Signal<bool>` | present while `b` is true |
| `on:click={handler}` | `handler: (Event) -> void`; runs in a batch |
| `bind:value={s}` | two-way: `s` sets the input, input events set `s` (`Signal<string>`, `Signal<int>`, `Signal<bool>` for `checked`) |
| `class:done={b}` | the class `done` toggles with `b` |
| `style:width={w}` | one style property follows `w` |
| `ref={r}` | `r: Ref` gets the DOM node, for focus, measuring, canvas |
| `{x}` with `x: Signal<T>` or `Memo<T>` | a text node that follows `str($x)` |
| `{node}`, `{nodes}` | a `Node` or `Node[]` |

### Control flow

Control flow in markup uses scopes whose block is markup:

```
{_show signed_in {                  // signed_in: Signal<bool> or Memo<bool>
    <p>Hello {(($user).unwrap()):name}</p>
} else {
    <a href="/login">Sign in</a>
}}

{_each todos, t -> t:id {           // keyed by id: an item keeps its DOM when the list changes
    <li>{scope:title}</li>          // scope: the item (T)
}}

{_switch route {
    "/" -> <Home/>
    "/about" -> <About/>
    _ -> <NotFound/>
}}
```

* `_show condition { markup }` (`condition`: a `Signal<bool>` or `Memo<bool>`,
  usually a `_memo`) swaps the branch only when the condition changes. The
  `else { ... }` part needs a small parser addition: a scope call may take
  `else { ... }` after its block, passed to the scope as an
  `Option<Callback<void, Node>>` argument. Without it, write two `_show`s.
* `_each list, key { markup }`: `list` is a `Signal<T[]>` or `Memo<T[]>`, `key` a
  `(T) -> K` lambda (optional: without it items are keyed by position). Items
  whose key stays keep their nodes and owner; new keys run the block; removed
  keys are disposed. The item is `scope`; when the same key gets a changed value,
  the item's block reruns.
* `_switch value { pattern -> markup ... }` is a reactive `match`: its arms are
  ordinary match arms, reevaluated when `value` changes. It needs the block to
  be a match body, which is the one place where the scope machinery would be
  stretched; until then, `_show` chains do the job.

Static control flow needs nothing: `{if wide { <Wide/> } else { <Narrow/> }}` and
`{items.map(i -> <li>{i}</li>)}` are evaluated once, when the component runs.

### Components

```
export type ButtonProps = { string label, Signal<bool> busy, Handler on_press }

export function Button(ButtonProps p) -> Node {
    return <button class={BUTTON} disabled={p:busy} on:click={p:on_press}>
        {_show p:busy { <Spinner/> }}
        {p:label}
    </button>
}

// used as
<Button label="Save" busy={saving} on_press={_ -> save()}/>
```

* A component is a function from its props (one record) and optionally its
  children (`Node[]`) to a `Node`. It runs once.
* Optional props are `Option<T>` fields; the record literal of the attributes
  must name every non-`Option` field (the checker's usual rule for record
  conversion).
* Props that should stay reactive are typed `Signal<T>` / `Memo<T>` (or the
  interface `Reactive`), so the caller decides what is live by what it passes.



## Styles

```
const CARD = css("card", "
    & { padding: 1rem; border-radius: .5rem; background: var(--surface) }
    & > h2 { margin: 0 }
    &:hover { outline: 1px solid var(--accent) }
")

<section class={CARD}>...</section>
```

* `css(name, text) -> string` returns a class name unique to the stylesheet
  (`card-3f9a`: the name plus a hash of the text) and registers the text with
  `&` replaced by `.card-3f9a`. The stylesheet is plain CSS: nesting, media
  queries and variables work as the browser supports them.
* **Hoisting.** `css` is a native of `web.pp` (`native "web_css"`). When both
  arguments are literals, the wasm target evaluates it while compiling, writes
  the rule into `<out>.css`, links it from `index.html`, and compiles the call
  to the class name string. The compiler already recognizes natives by their C
  name (that is how it routes natives to the host), so this needs no language
  feature. A `css` call with a computed text is injected at runtime into a
  `<style>` element instead, once per distinct text.
* `global_css(text)` is the same without a class (resets, fonts, variables).
* Dynamic styling uses `class:name={b}` and `style:prop={value}` bindings;
  there is no CSS-in-PiPo object syntax (CSS property names are not PiPo names,
  and a typed CSS model is a project of its own).



## The web.pp API

Signatures as they would be written in `lib/web.pp`. `Reactive`, `Signal`,
`Memo` and the markup functions are `global`, so `import web` makes markup work.

### Reactivity

```
export interface Reactive {
    function graph_node(Reactive) -> GraphNode        // the node subscribers attach to
}

export type Disposer = () -> void
<T>type Mapper<T> = T -> T

export <T>function signal(T initial) -> Signal<T>
export <T>global function $(Signal<T> s) -> T
export <T>global function $(Memo<T> m) -> T
export <T>function set(Signal<T> s, T value) -> void
export <T>function update(Signal<T> s, Mapper<T> f) -> void
export global function +=(Signal<int> s, int n) -> void       // and -=, *=, ... for int / float
export global function +=(Signal<string> s, string tail) -> void
export <T>global function +=(Signal<T[]> s, T item) -> void   // push, then notify

export <R>scope _memo(Callback<void, R> code, Reactive[] deps) -> Memo<R>
export scope _effect(Callback<void> code, Reactive[] deps) -> Disposer
export scope _batch(Block<void> code) -> void
export scope _root(Callback<void> code) -> Disposer
export scope _cleanup(Callback<void> code) -> void

export <T>function store(T record) -> Store<T>               // a signal per field
export <T>function readonly(Signal<T> s) -> Memo<T>          // hand out without the setter
```

### Async data

```
export <T>enum Load<T> { loading, ready(T), failed(string) }

// reruns `code` when a dependency changes; code gets a Fetcher and answers through it
export <T>scope _resource(Callback<Fetcher<T>> code, Reactive[] deps) -> Resource<T>
export <T>function $(Resource<T> r) -> Load<T>
export <T>function refetch(Resource<T> r) -> void

export scope _fetch(Callback<Error<Response>> code, string url) -> void
export function text(Response r) -> string
export <T>function json(Response r) -> Error<T>              // decoded and checked against T
```

`json<T>` decodes into a statically typed value: the wasm target already has a
type descriptor for every type (`T[...]` in the generated `.mjs`), so the host
can check the parsed JSON against `T` and fail with a path (`$.items[3].id:
expected int`) instead of letting a wrong shape in. The interpreter does the
same with `type_match`.

### Markup targets (what the literals call)

```
export type Node = { int handle }                 // a DOM node, owned by the host
export type Attr = { int kind, string name, unknown value }

global function element(string tag, Attr[] attrs, Node[] children) -> Node
global function fragment(Node[] children) -> Node
global function text(string s) -> Node
global function child(string s) -> Node
global function child(int n) -> Node
global function child(float x) -> Node
global function child(Node n) -> Node
global function child(Node[] ns) -> Node
global function child(Option<Node> n) -> Node
<T>global function child(Signal<T> s) -> Node     // a text node bound to str($s)
<T>global function child(Memo<T> m) -> Node

global function attribute(string name, string value) -> Attr
global function attribute(string name, bool present) -> Attr
global function attribute(string name, Signal<string> value) -> Attr
global function attribute(string name, Memo<string> value) -> Attr
global function attribute(string name, Signal<bool> present) -> Attr
global function attribute(string name, Memo<bool> present) -> Attr

export type Handler = (Event) -> void
global function on(string event, Handler h) -> Attr
global function bind(string property, Signal<string> s) -> Attr
global function bind(string property, Signal<int> s) -> Attr
global function bind(string property, Signal<bool> s) -> Attr
global function class(string name, Signal<bool> on) -> Attr
global function class(string name, Memo<bool> on) -> Attr
global function style(string property, Signal<string> value) -> Attr
global function ref(string unused, Ref r) -> Attr

// control flow
export <T, K>scope _each(Callback<T, Node> code, Signal<T[]> items, Keyer<T, K> key) -> Node
export <T>scope _each(Callback<T, Node> code, Signal<T[]> items) -> Node
export scope _show(Callback<void, Node> code, Signal<bool> when) -> Node
export scope _show(Callback<void, Node> code, Memo<bool> when) -> Node

// the page
export function mount(Node root, string selector) -> Disposer
export function css(string name, string text) -> string
export function global_css(string text) -> void
export function set_title(string title) -> void
```

### Events, timers, the page

```
export type Event = { int handle }
export function prevent_default(Event e) -> void
export function value(Event e) -> string          // event.target.value
export function key(Event e) -> string            // KeyboardEvent.key

export scope _every(Callback<void> code, int ms) -> Disposer   // owned: stops with its owner
export scope _after(Callback<void> code, int ms) -> Disposer
export scope _frame(Callback<float> code) -> Disposer          // requestAnimationFrame; scope: the time

export function location() -> Signal<string>       // the path, follows history
export function navigate(string path) -> void
export function local(string key, string initial) -> Signal<string>  // backed by localStorage
```



## What the host and the wasm target need

`web.pp` is PiPo; under it is a small set of natives in the generated `.mjs`
(the same mechanism as `NATIVES` today, see [TARGETS.md](TARGETS.md#natives-through-the-host)):

| native | JavaScript |
|--------|------------|
| `dom_create(tag) -> int` | `document.createElement`, the element stored in a handle table |
| `dom_text(s) -> int`, `dom_set_text(h, s)` | text nodes |
| `dom_attr(h, name, value)`, `dom_remove_attr(h, name)` | attributes |
| `dom_prop(h, name, value)` | properties (`value`, `checked`) |
| `dom_class(h, name, on)`, `dom_style(h, prop, value)` | classes, styles |
| `dom_insert(parent, child, before)`, `dom_remove(h)` | tree changes |
| `dom_listen(h, event, closure) -> int`, `dom_unlisten(id)` | events: the handler is a PiPo closure |
| `dom_query(selector) -> int?` | `mount` |
| `timer(ms, repeat, closure) -> int`, `frame(closure) -> int`, `cancel(id)` | timers |
| `fetch(url, closure)` | `fetch(...).then(...)`, the closure gets an `Error<Response>` |
| `storage_get`, `storage_set`, `history_push`, `on_popstate` | page state |

The markup functions are written in PiPo over these: `element` creates the
node, applies the attributes (a binding attribute creates an `_effect` owned by
the current owner) and appends the children.

What the wasm target must add:

* **Calls from JavaScript into PiPo closures** go through `enter()` (it exists),
  which counts the depth so the collector runs only between events. Event
  handlers are wrapped in a batch.
* **Pinning.** A closure handed to `dom_listen`, `timer` or `fetch` is pinned
  (`pin`, exists) and unpinned when the listener is removed, which happens when
  its owner is disposed. DOM nodes live in the host's handle table; disposing an
  owner frees their handles. Nothing on the page keeps wasm memory alive except
  through a pin, so the conservative collector (TARGETS.md) is enough.
* `--web`: write `index.html` with a `<div id="app">`, the `.css` file and a
  module script, and serve the folder during development (`pp serve`).
* What `web.pp` itself uses from the language must compile to wasm: generic
  records, interfaces with generic impls, closures, scopes (all supported now),
  plus `std:Map` for keyed lists (not yet: maps are the next wasm feature; a
  sorted array works meanwhile).
* Later: `externref` for DOM nodes instead of an integer handle table, and
  WasmGC to share one collector with the page.



## Alternative: a macro system

Rust-style macros were mentioned as acceptable. A draft, to compare:

```
macro html(tokens) -> Node { ... }        // a function from a token tree to an AST, run while parsing
const page = html! { <div class="a">{count}</div> }
```

* A macro is PiPo code run by the compiler: it gets the tokens between the
  braces and returns syntax (`quote { element(...) }`). Markup would be a
  library macro, and other DSLs (SQL, CSS, routes) could follow.
* It needs: a token tree type, `quote` / unquote, running PiPo code at compile
  time (the interpreter is in the compiler, so this is possible), hygiene for
  names the macro introduces, and error positions that point into the
  macro input.
* It is several times the work of the markup literal, and the static checker
  would see only the expansion (error messages about generated calls).

**Recommendation:** add the markup literal now; it is small, and its
desugaring (calls by name) is exactly what a `html!` macro would produce, so it
can later be re-expressed as the first built-in macro without changing any
component.



## Plan

1. Language: `Callback<T, R>` / `Block<T, R>`, generic scopes, `[]` typed by the
   parameter (changes 2-4). Small, testable with the interpreter alone.
2. `lib/web.pp` reactivity part (signals, memos, effects, batches, owners) with
   a fake DOM: runs and is tested on the interpreter and bytecode targets
   before any browser is involved.
3. Markup literals in the lexer and parser (change 1), the editor grammars, and
   a test renderer that builds strings (server-side rendering comes for free).
4. Wasm: maps, the DOM natives in the host, pinning, `--web` output, `css`
   hoisting.
5. Examples: counter, todo list, a fetch with `_resource`, a router.



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