# Best Practices for PiPo code

How to write `.pp` programs and modules that read like the rest of the
project. [CODING_STYLE.md](CODING_STYLE.md) has the layout rules (shared with
the C sources); this page is about the code itself.

Nothing here was written down as a rule before 2026-11-17. The practices are
**inferred** from what the project already does: the conventions in
[PROPOSAL.md](PROPOSAL.md#code-conventions), the library modules in `lib/`,
the guidance in [NATIVE_MODULES.md](NATIVE_MODULES.md), the examples and
tests, and a real program written in PiPo (a cash register). Where those
disagree, this page says which one it follows and why. Treat it as a draft to
correct, not as law.

## Contents

1. [Files and modules](#1-files-and-modules)
2. [Names](#2-names)
3. [Variables and types](#3-variables-and-types)
4. [Functions](#4-functions)
5. [Errors](#5-errors)
6. [Resources](#6-resources)
7. [Collections and iteration](#7-collections-and-iteration)
8. [Paths and files](#8-paths-and-files)
9. [Bindings to C libraries (extern)](#9-bindings-to-c-libraries-extern)
10. [Comments](#10-comments)

## 1. Files and modules

* A file is **imports, then code, then exports** (the parser enforces the
  order). Library modules first, then local files:

  ```
  import io
  import fs
  import "./item"
  import "./ui"
  ```

* Separate the import block from the code by **three blank lines**, like the
  include block of a C file. Inside the code, three blank lines also separate
  big sections (the C API of a binding, its constants, its wrappers); one
  blank line separates functions.
* A module starts with a header comment: `// ---- name: what it is`, then a
  short usage example, then what a user must know (what fails, what is
  copied). See `lib/fs.pp`, `lib/types.pp`.
* Group a long module into sections with `// ---- section` comment lines.
* Export with `export` in front of the declaration when most of the module is
  public (`lib/`), or with an `export { ... }` list at the end when only a few
  names are (application files). Keep helpers private: no `export`.
* A type and the functions that work on it live in the same module: dot calls
  (`order.total()`) then find them without a module prefix.
* Split a program into files by topic (`item.pp`, `stock.pp`, `ui.pp`,
  `main.pp`). `main.pp` holds the top-level code that runs the program.

## 2. Names

| what | style | example |
|------|-------|---------|
| types, interfaces, enums | PascalCase | `Item`, `MenuOption`, `fs:File` |
| functions, variables, parameters, fields | snake_case | `get_stock`, `read_text`, `bytes_written` |
| enum variants | UPPER_SNAKE_CASE | `MenuOption:ADD` |
| fixed settings and C constants | UPPER_SNAKE_CASE | `const WIDTH = 800`, `gl:COLOR_BUFFER_BIT` |
| module values that are computed | snake_case | `math:pi`, `fs:separator` |
| constructor functions | the type's name in snake_case or flatcase | `fs:file(path)`, `types:int8array(4)` |
| scopes | `_` + snake_case | `_resource` |

* A type is never called (`fs:File(...)` is an error). Make values with a
  constructor function, or with a record literal `Item{ name: "x", cost: 1 }`.
* Do not repeat the module in names: `glfw:window`, not `glfw:glfw_window`;
  `gl:COLOR_BUFFER_BIT`, not `gl:GL_COLOR_BUFFER_BIT`. The prefix is already
  there at every use.
* Predicates read as questions: `is_file`, `exists`, `should_close`,
  `is_pressed`.

## 3. Variables and types

* Prefer `const`. Use `let` only for what changes.
* Let the checker infer local types (`const n = 0`). Write the type when the
  value alone does not say it: empty collections (`let item:Order order = []`),
  record literals (`const Person john = { ... }`), and module-level values
  others read.
* Give names to shapes with `type`: `type Order = Item[]`,
  `type Window = { c:ptr handle }`. A named type documents intent and gets its
  own overloads (`str(Order)`).
* Stay out of `unknown`. It turns off static checking and the compiling
  targets refuse code that needs it ([STATIC_TYPING.md](STATIC_TYPING.md)).
  Use unions, interfaces or generics instead.
* Use `T?` for "may be missing" and `Error<T>` for "may fail" (section 5),
  never a sentinel like `-1` or `""` in new APIs.

## 4. Functions

* The first parameter is the receiver of dot calls, so put the value the
  function is "about" first: `function total(Order order) -> float` is called
  `order.total()`.
* Overload by type rather than inventing names: `file(string)`,
  `file(string, string)`, `file(Directory, string)`.
* Keep functions short and named for what they do. `main.pp` reads as a list
  of steps (`triangle_program()`, `triangle_vertex_array()`, `draw(...)`).
* Use `global function` only for operators and conversions that must work
  wherever the type goes (`global function str(Order order) -> string`,
  `global function ==(Rect a, Rect b) -> bool`), and implement the matching
  interface (`impl types:Str for Order`).
* Lambdas for short callbacks: `order.reduce((a, x) -> a + x:cost, 0.0)`.

## 5. Errors

From [NATIVE_MODULES.md](NATIVE_MODULES.md#12-errors), and the same for `.pp`
code:

| situation | use |
|-----------|-----|
| the outside world said no (a missing file, a failed C call, bad input) | return `Error<T>`: `fail("cannot open ...")` / `success(value)` |
| the program itself is wrong (an impossible state, a broken invariant) | `panic(message)` |
| a simple query about something missing | a plain answer: `exists()` is false, `size()` is 0 |

* Pass failures up with `lift()` instead of matching at every level:

  ```
  function triangle_program() -> Error<int> {
      const vertex = gl:compile_shader(gl:VERTEX_SHADER, read_shader("triangle.vert").lift()).lift()
      ...
  }
  ```

* Handle them once, at the top, with `match`:

  ```
  match run() {
      success(frames) -> io:print_nl("drew " + str(frames) + " frames")
      fail(message) -> panic(message)
  }
  ```

* `unwrap()` is for scripts, tests and values that cannot fail; a program
  that ships should say what went wrong.
* Error messages say what was attempted and on what: `cannot open 'x': No
  such file or directory`, `glfwCreateWindow failed (...)`.

## 6. Resources

* Anything that must be released (a stream, a window, a C handle) gets an
  explicit `close()` / `destroy()`, and the code that opens it closes it. The
  garbage collector is only a safety net, and a late one
  ([NATIVE_MODULES.md](NATIVE_MODULES.md#10-closing-through-the-gc)).
* When open and close belong around a block, write a scope with `defer`:

  ```
  scope _resource(Block<void> code, Resource r) -> void {
      defer { r.close() }
      code()
  }
  ```

* Release in reverse order of acquiring (delete the GL program, destroy the
  window, terminate GLFW).

## 7. Collections and iteration

* `for x in xs`; `for i, x in xs` when the index is needed; `for i in ...n` for
  counting.
* `map` / `filter` / `take` are lazy: end the chain with `.to_array()` or
  `reduce` when an array is needed ([ITER.md](ITER.md)).
* Write a `generator` for a sequence computed step by step, rather than
  building an array to return.
* Use `Map<K, V>` / `Set<T>` (built in) for lookups, not arrays of pairs.
* Use `types:*Array` for raw numbers that C, files or GPUs read; `T[]`
  otherwise.

## 8. Paths and files

* Build paths to files that ship with the code with `os:join_pp(...)`. It is
  relative to the file the call is written in, so the program works from any
  working directory.
* Read whole files with `fs:file(path).read_text()` (an `Error<string>`);
  stream large ones with `open_read()` and close the stream.
* Keep data that is not code (shaders, stock lists) in its own files next to
  the program, not in long string literals.

## 9. Bindings to C libraries (extern)

From the GLFW / OpenGL binding ([EXTERN.md](EXTERN.md)):

* **One binding module per library** (`glfw.pp`, `gl.pp`), and the DLL next to
  it: `extern "glfw3"` looks in the declaring file's folder first.
* **Declare only what you use.** A library is loaded on the first call, so
  unused declarations cost nothing, but each one is a C signature to keep
  right by hand.
* **Two layers.** Keep the C functions private under their C names
  (`extern "glfw3" function glfwInit() -> c:i32`), and export PiPo functions
  with PiPo types: `bool` for C's int flags, `Error<T>` for C's failure
  codes and NULL results, records for handles.
* When the C function needs no adapting, export it directly under a PiPo name
  with the second string: `export extern "opengl32" "glClear" function clear(c:u32 mask) -> void`.
* **Wrap handles in a record type** (`type Window = { c:ptr handle }`), so a
  window cannot be passed where a buffer offset is expected, and dot calls
  work (`window.swap_buffers()`).
* Spell C widths with the `c` module (`c:i32`, `c:u32`, `c:f32`). Write
  `c:f32` for C `float`: plain `float` is a C `double`.
* Out-parameters are typed arrays of length 1, hidden in a wrapper that
  returns the value (`gen_buffer() -> int`).
* Name C constants after the C name without its prefix, in UPPER_SNAKE_CASE,
  with the hex value in a comment: `export const ARRAY_BUFFER = 34962 // 0x8892`.

## 10. Comments

* Comments are `//`. A comment says **why**, or what a declaration means to
  its caller, not what the next line does.
* Document every exported function with a line above it when its name and
  types do not say everything: what fails, what is copied, units.
* Put the doc of a module at its top (section 1), with a usage example a
  reader can copy.
