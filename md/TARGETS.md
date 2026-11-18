# Targets

`pp --target=<target> [options] <file.pp> [args...]` decides what happens to a
program after it passed the static checker ([STATIC_TYPING.md](STATIC_TYPING.md)).
`pp --target=list` prints them.

| target | what it does | status |
|--------|--------------|--------|
| `interp` (default) | runs the checked tree in the tree-walking interpreter | the whole language |
| `check` | only checks the static types (exit 0, or 65 with the first error) | the whole language |
| `bytecode`, `x86` | compiles to bytecode and runs it in the bytecode VM, for the PC | the whole language |
| `wasm` | compiles to WebAssembly: `<out>.wasm`, `<out>.mjs`, `<out>.html` | a subset, see below |

Options come before the script; what follows the script is the program's own
arguments (`os:args()`):

| option | meaning |
|--------|---------|
| `-o <path>` | where a compiling target writes its output, without extension (default: next to the script) |
| `--report` | print how much of the program the checker typed statically, and why the rest is not |
| `--no-check` | `interp` only: skip the checker |
| `--dump` | `bytecode` only: print the compiled bytecode before running it |

All three runners are tested with the same expected outputs: `ctest` runs every
`tests/*.pp` on the interpreter and on the bytecode VM, and every `tests/wasm/*.pp`
compiled to WebAssembly under node (when node is installed).

```
pp examples/proposal.pp                  # interpreter
pp --target=bytecode examples/proposal.pp
pp --target=wasm -o out/demo tests/wasm/basics.pp
node out/demo.mjs                         # or serve out/ and open demo.html
```

A construct a compiling target does not handle yet is refused when the program
is compiled, with its line, never at runtime:

```
tests/ranges.pp:9: error: the wasm target has no float ranges yet
```

## bytecode: the PC target

`src/bytecode.h`, `src/bytecode.c` (the compiler), `src/bcvm.c` (the VM).

* **What it compiles.** The checker's *instances*: every function body typed for
  concrete parameter types (generic and untyped functions get one instance per
  set of argument types). Every call, operator and index was resolved by the
  checker, so the compiler emits direct calls (`CALL f`), calls of C functions
  (`CALL_NATIVE`) and typed operations (`ADD_I`, `ADD_F`, `LT_S`, ...) instead of
  overload lookups. Variables are resolved to slots.
* **Shared runtime.** Values, objects, the garbage collector and every native
  module (`io`, `os`, `fs`, `std`, `math`, `types`, ...) are the interpreter's own;
  only the execution of `.pp` code is new. A native that calls back into
  `.pp` code (`map`, `sort`, `call`) calls compiled closures through
  `vm->closure_call`.
* **The machine.** A stack machine on `vm->stack`: the globals of every module
  at the bottom, then one frame per call (its locals, parameters first, then its
  temporaries). Code is an array of 32-bit words. `OP_LINE` keeps the current
  line for messages and is where the collector may run. Runtime errors print the
  same `file:line: error: message` and traceback as the interpreter.
* **Closures.** A variable that a nested function (lambda, local function) uses
  lives in a cell; the closure captures the cell. Loop variables get a fresh
  cell per iteration, so closures keep the value of their iteration, as in the
  interpreter.
* **Dynamic dispatch.** A call whose argument is an interface, a union or
  `unknown` may need a different overload for each runtime value. The checker
  lists every candidate that could fit (`CCall.cands`, including the functions
  that implement an interface requirement); the compiler instantiates them and
  emits `OP_DISPATCH`, which calls the first candidate (most specific first)
  whose parameter types the values fit.
* **Speed.** About 20 times faster than the interpreter on calls and loops
  (`fib(30)` plus a 3 million step loop: 2.4 s interpreted, 0.11 s compiled).

`--dump` shows the code:

```
function 1 fib (1 params, 1 locals, 0 captures)
      0  LINE         3
      2  LOAD         0
      4  INT          2
      6  LT_I
      7  JUMP_FALSE   14
      ...
```

* **Scope calls.** The block of `_name args { ... }` is compiled like a lambda
  (a closure of the body, checked as part of the enclosing instance), and so is
  a `defer` body. `SCOPE_CALL` runs the scope function on a nested run loop under
  a `setjmp`, with two stack slots for the call: the value of an early return and
  the current defer closure (`DEFER` replaces it). `return`, `break`, `continue`
  and a failing `lift()` inside a block become `SCOPE_EXIT`, which longjmps back
  to its call; there the defer block runs and the exit continues as if it were
  written at the call (in an enclosing block it leaves that block's call too).
* **Tagged aliases** (`type Meters = float`, LANGUAGE.md) cost nothing in calls the
  checker resolved. `Value.tag` (32 bits in what was padding) holds the checker's
  tag id only where it is read: `SETTAG tag k` writes it when a value of a known type
  enters an open one (an unknown, a union, an interface, the branch of an if / match
  of a union type) and on the arguments of a `DISPATCH`. Dispatch tables list deeper
  tags first; `DISPATCH` takes the first candidate whose tagged parameters the values
  have (or are below), then, if none, one that takes them down a chain. A program
  with no tagged alias emits no `SETTAG`.

## wasm: the web target

`src/wasm.c` writes three files:

* `<out>.wasm`: the program as a WebAssembly module (no dependencies, written byte by byte);
* `<out>.mjs`: a JavaScript module that runs it in node (`node out.mjs`) or in a
  browser, and provides printing and string operations;
* `<out>.html`: a page that runs the module and shows its output (serve the
  folder over HTTP, browsers do not load modules from `file://`).

Every value has a WebAssembly type fixed at compile time, which is what the
static checker makes possible:

| PiPo | WebAssembly |
|------|-------------|
| `int` | `i64` |
| `float` | `f64` |
| `bool` | `i32` |
| `string` | `i32` address of `[length][UTF-8 bytes]` |
| `T[]` | `i32` address of `[length][capacity][data]`, data: one 8-byte slot per element |
| records | `i32` address of `[shape][slots]`; the shape lists the field names and types |
| tuples | `i32` address of `[slots]` |
| enum values (`Option`, `Error`, ...) | `i32` address of `[variant][count][payload slots]` |
| ranges | `i32` address of `[start][end][step][inclusive]` |
| functions, blocks | `i32` address of `[table slot][scope record][captured cells]` |
| `unknown`, unions, interfaces | `i32` address of a box `[type id][-][value slot]` |

Printing goes through a table of type descriptors generated from the static
types, so `io:print_nl` prints any value exactly like the interpreter does.

Supported now:

* functions (also generic ones, per instance) and recursion, module globals,
  imports of `.pp` modules and module constants;
* `int` / `float` / `bool` / `string` arithmetic and comparisons, `str`, `+` on
  strings, `==` / `!=` on any value (structurally);
* `if` / `while` / `for` over int ranges, arrays, strings (by character),
  generators, `Map` (`for k, v`) and `Set` (with `break` / `continue`),
  `match` (variants, literals, bindings, guards), `lift()`;
* **`Map` and `Set`** (below): every function of `Map` / `Set`, `m[k]`,
  `m[k] = v`, `m[k] op= v`, `#`, `==`, `copy`, `str`, `in`, set operators;
* **generators** (below), and with them the prelude's iteration: `Iterable` /
  `Iter` / `KeyIterable`, a `for` over a type that implements them (through
  `iterate()`), the lazy `map` / `filter` / `take` / `skip` / `enumerate` /
  `zip` / `chain`, `to_array`, `reduce`;
* arrays (literals, `#`, `[]`, `[] =`, `[] op=`, `push`, `pop`, `insert`,
  `remove`), records (also `Type({...})`), tuples, enums, `Option`;
* **function values**: lambdas, closures (a captured variable lives in a cell,
  one per loop iteration), local and recursive local functions, functions passed
  by name. Every compiled function takes the function value it was called
  through as its first parameter, so all of them can be called through the
  funcref table with `call_indirect`;
* **scope calls**, `defer` and the early exits of a block (below);
* **`unknown`, unions and interfaces as values**: a box with the value's type id.
  `x as T` tests the id against `T`; a call that needs dynamic dispatch tests
  the boxed arguments against each candidate (most specific first) and calls the
  first that fits, the values unboxed to its parameter types. The type tests are
  generated last, from every type that was boxed somewhere in the program. A
  candidate that cannot fit is not compiled at all: `iter(string)` for an
  `Iterable<int>`, or `iter(Set<T>)`, whose parameter type this target cannot
  make values of;
* natives the host runs (below): `sort`, `map`, `filter`, `reduce`, `join`,
  `contains`, `index_of`, `slice`, `split`, `upper`, `replace`, `parse_int`,
  `format`, `get_type`, string indexing, `in`, the `math` module, ...;
* `range(start, end, step)` of ints, `enum_value` (the declaration's literal
  values, a table in the data segment), a tuple indexed by a computed key (the
  elements' joined type), operators that need dynamic dispatch, `x op= y`
  through any operator function;
* `io:print`, `io:print_nl`, `int()`, `float()`, `panic`. Runtime errors
  (division by zero, an index out of range, no matching arm) stop the program
  with the interpreter's message and exit code 1.

One difference from the other targets: an array passed where an array of boxes
is expected (`MenuOption[]` as `View[]`, `int[]` as `float[]`) is **copied**
with its elements converted, so the callee's changes to it are not seen by the
caller.

Not yet: `extern` functions (a browser has no DLLs), `object` (open records), typed arrays, float ranges, `Range<T>` of non-numbers,
natives and overloaded names used as function values (`reduce(xs, +, 0)`),
`fields()` (a generator the host would have to run), the `os` / `fs` / `pp`
modules (there is no file system in a browser; node could have one). Each is
refused at compile time. A map whose types the program never states
(`const counts = map()`, then `counts[k] += 1`) has no static type for
its values and is refused like any dynamic code. Of the 30 `tests/*.pp`
programs that run to the end, 10 compile to WebAssembly and print the same as
the interpreter (`generators` stops only at `reduce(xs, +, 0)`, `collections`
at printing the type `Map` itself); one more (`inline_exports`) differs only in printing `4.0` for a
`float` field given the int `4`, which the interpreter keeps as an int; the
rest stop at one of these.

### Generators as step functions

WebAssembly cannot pause a function, so a generator is lowered (see
[ITER.md](ITER.md#how-generators-run)). A generator is an object
`[step][state][value][frame]`: `step` is the table slot of its **step
function**, `(generator) -> i32`, which runs the body to its next value and
returns 1 (the value in the `value` slot) or 0 (the body ended). Calling the
generator function only allocates the object and puts the closure and the
arguments into the frame.

The step function is the body compiled as an ordinary function, with every
local living in the frame between steps: the step loads all of them on entry
and stores them when it pauses. `emit` stores the value, sets `state` to the
number of the pause and leaves through a block around the body. Resuming at
pause `k` enters the body again with a `resume` local set to `k`, and the code
on the way to that `emit` is routed without running anything else:

* in a block that holds pauses, each statement is guarded by
  `resume == 0 || k is in it` (the pauses are numbered in source order, so
  "in it" is a range test);
* a `while` / `for` skips its test and its next item (the iterator's position
  is a local, so it is in the frame);
* an `if` / `match` takes the branch that holds the pause without evaluating
  the condition, the subject or the patterns (the bindings are locals too);
* the `emit` itself clears `resume`, and the body goes on after it.

`state` -1 is done (every later step returns 0) and -2 is running: a step that
finds it running fails with the interpreter's message (`next()` from the
generator's own body). The frame is collected memory like any other object.

### Map and Set in the heap

A map or a set is an object of the collected heap, so wasm code holds it like
any value and the collector traces through it:
`[n][cap][keys][vals][index][icap][kt][vt]`. The `n` entries are kept in
insertion order, their keys and values in 8-byte slots of the types `kt` / `vt`
(type descriptor ids; a set has `vt` -2 and no values), with an open-addressing
`index` of entry numbers. Removing an entry shifts the later ones down and
rebuilds the index, so the order stays the interpreter's.

The operations are the host's (the `std_map_*` / `std_set_*` natives of the
bridge): JavaScript hashes and compares the keys by their type descriptor
(strings by their bytes, tuples and enum values by their parts, an integral
float like the int), and fails like the interpreter for a key that can change
(an array, a record, a map). The map keeps its own `kt` / `vt`, and each call
converts between them and the types of the call (boxing, unboxing, int to
float), so a map made by `map()` (a `Map<never, never>`, types not known)
takes them from the first call that states them, and `map({ a: 1, b:
"x" })` stores its mixed values boxed. A `for` over a map or a set iterates a
snapshot of its entries (`entries()`) or items (`values()`) as an array.
`m[k]` and `m[k] = v` go through `Index` like on the other targets; the
dispatch of a call on a boxed `Index` can now reach a native of the bridge
(`get`, `put`).

### Scope calls without unwinding

WebAssembly has no `setjmp`. A scope call allocates a record `[defer][previous]`
and makes it the innermost (`g_scope`); the block's closure keeps the record. A
block that leaves early sets two globals, the exit kind and the record it
belongs to (and a `return`'s value in `g_ret_*`), and returns. After every call
that can come back with an exit (a call of a `Block`, or of a function that is
passed one) the code checks the global and returns as well, so the frames of the
scope function unwind one by one. The scope call then runs the defer closure
(with the exit put aside) and, when the exit is its own, continues it: `return`
returns the value, `break` / `continue` jump in the loop around the call. An
exit that belongs to an outer scope call keeps returning toward it.

### Natives through the host

A native without wasm code is run by the JavaScript host: the compiled code
writes the arguments with their static types into a buffer, calls
`native(signature, buffer)`, and reads the result from the slot after them. The
host converts between memory and JavaScript values with the type descriptors
(`toJs` / `fromJs`: strings, arrays, tuples, `Option`, boxes; a function value
becomes a JavaScript function that calls back into the table), then calls its
`NATIVES[name]`. The same conversion is what a web library needs to talk to the
DOM, see [WEB.md](WEB.md).

### Memory and garbage collection

The heap belongs to the host: `alloc(size)` calls `gc_alloc`, which takes a
block from a free list of that size or from the top of the heap (growing the
memory). Every object's size is recorded. `collect()` is a **conservative mark &
sweep**:

* the roots are the module's `i32` globals (exported as `g<n>`) and the objects
  JavaScript pinned (`pin(p)` / `unpin(p)`: an event handler, a value on the
  page);
* every aligned 4-byte word of a reached object that is the address of an object
  marks that object. Ints and floats that look like addresses keep garbage alive
  (safe), so no object needs a layout description;
* unmarked objects go to the free list of their size.

It only runs while **no wasm code is running** (`enter()` counts the calls from
JavaScript; the collection happens when the outermost one returns and enough was
allocated since the last one). That is what keeps it simple: the locals of
running functions, which a collector cannot see in WebAssembly, then hold
nothing. For a program in a page, where every piece of work is an event handler,
this frees everything that is not reachable from globals or the page. For a long
computation in one call, nothing is freed until the call returns (a loop that
allocates 3.5 MB of temporary strings leaves 88 bytes after `main`).

**Is that enough?** For the web target, yes for now. Two upgrades, when needed:

1. *Collecting during a call*: a **shadow stack**. The compiler spills the
   pointer-typed locals that are live across a call (a call can allocate) into a
   stack in linear memory, which becomes a root. The static types say which
   locals hold pointers, so only those are spilled. Then `gc_alloc` may collect
   anywhere.
2. **WasmGC** (struct and array reference types, collected by the browser's own
   collector): no heap code at all and cycles between wasm and JavaScript
   objects are collected too. It needs a different code generator (references
   instead of `i32` addresses) and node 22 / current browsers. The static types
   map onto it directly (a record type is a struct type, `T[]` an array of `T`).
