# Modules and Packages

## Importing

```
import io                          // a library module, bound as `io`
import io -> console               // bound as `console`
import io -> { print, write }      // copy exported symbols into this scope

import "./geometry"                // the file geometry.pp next to this file
import "./shapes/rect.pp" -> rect  // .pp is optional
import "../shared/util" -> { clamp, + }
```

* **`import name`** (an identifier) loads a **library module**. It is looked
  up as a registered native module first (like `io`), then as `name.pp` in each
  library directory: `<directory of pp.exe>/lib/`, then every directory
  in `PP_PATH` (`;`-separated on Windows, `:` elsewhere).
* **`import "path"`** (a string) loads a **file**. The path is relative to the
  importing file, or absolute.
* Without `{ }` the module is bound to a name: the module name, or the alias
  after `->`. That name behaves like an object, and its exports are read with
  `:`. For example `console:print("hi")`, `geometry:Vec` in a type,
  `shapes:Color:RED` in a pattern.
* With `{ }` the listed exports are copied into the scope. Importing a
  function, operator included, **merges** its overloads into the local
  overload set, so `import "./vec" -> { + }` keeps `1 + 2` working.
* Modules run once and are cached by absolute path. Circular imports see the
  partially initialized module, as in Python.
* Dot calls look in the receiver's module too: for `r.area()`, if the type of
  `r` was declared in a module that exports `area`, those overloads take part
  in the call even though `area` was never imported. `r.area()` is then the
  same as `r.shapes:area()`. This works for record values tagged with their
  type (`Rect({...})`, `const Rect r = ...`, `-> Rect`) and for enum values.

## Global functions

A top-level function declared with `global` is added to a VM-wide overload
set when its module is loaded. From then on the name is usable in every file,
without importing it. This is meant for operators and other functions that
should simply work wherever a value of the type shows up:

```
// shapes.pp
global function ==(Rect a, Rect b) -> bool {
    return a:width == b:width && a:height == b:height
}
export { Rect, rect, area }

// main.pp
import io -> { print }
import "./shapes" -> shapes

const r = shapes:rect(2, 3)
const s = shapes:rect(2, 2)
print(r.area())      // 6  (found in the module of Rect)
print(r == s)        // false
print(==(r, s))      // false
```

* `global` is only allowed at the top level of a file, and only before
  `function` (or a generic list: `global <T>function f(T x)`).
* A global function is also an ordinary function of its module. It can still
  be exported, and `shapes:==` keeps working.
* Global candidates are **merged** into every lookup of the name, so the
  built-in `==` overloads and any local ones keep working. When a local and a
  global candidate score the same, the local one wins.
* Visibility depends on loading, not on importing. A file sees a global
  function once any file has imported the declaring module, even if it did
  not import that module itself. Import the module in the files that rely on
  its globals to make the dependency explicit.

## Exporting

Nothing in a file is visible to importers unless it is exported. `export`
looks like `import` and can name any top-level symbol: constant, variable,
type, enum, function or operator.

```
export area                       // one symbol
export SCALE -> factor            // exported under another name
export { Rect, rect, area, + }    // several at once
```

* `export` is only allowed at the top level of a file.
* Exports are **live bindings**. Importers see the current value of the
  exported variable.
* An export may appear before the definition it names. After the module has
  run, every exported name must exist, otherwise importing it is an error.
* Exporting a function name exports every overload declared under that name
  in the module.

## The `io` module

Printing and reading lives here, not in the global scope:

```
import io
io:print("hello", 42)        // like console.log / System.out.println
```

| function | |
|----------|-|
| `print(...)` | arguments separated by spaces, then a newline. Objects are pretty JSON |
| `write(...)` | the same without the newline |
| `eprint(...)` | like print, to stderr |
| `input(...) -> string` | prints the prompt, reads one line |
| `read_file(path)` | `success(text)` or `fail(message)` |
| `write_file(path, text)` | `success(bytes_written)` or `fail(message)` |

## Planned standard packages

The interpreter core keeps only the most basic functions internal
([BUILTINS.md](BUILTINS.md)). The rest is meant to come as packages:

| package | contents (ideas) |
|---------|------------------|
| `os`    | args, env vars, exit, processes, platform info |
| `fs`    | directories, paths, file metadata |
| `math`  | trig, logarithms, constants, random |
| `time`  | wall clock, dates, sleep |
| `json`  | parse / stringify |

A package is either a **`.pp` file** in `lib/`, which CMake copies next to
`pp.exe`, or a **native module** written in C, like `src/mod_io.c`:

```c
static ObjModule* os_init(VM* vm) {
    ObjModule* m = module_new(vm, "os", "<native os>", vm->builtins);
    vm_push(vm, obj_val(m));                       /* keep it alive while building */
    def_native(vm, m->env, "exit", "(int code)", os_exit);
    /* native modules export everything they define */
    for (int i = 0; i < m->env->vars.count; i++)
        module_export(m, m->env->vars.entries[i].key, m->env->vars.entries[i].key);
    vm->sp--;
    return m;
}

void mod_os_register(VM* vm) { vm_register_native_module(vm, "os", os_init); }
```

Call the register function from `vm_new` (`src/interp.c`), next to
`mod_io_register`. `vm->argc` / `vm->argv` already hold the command line
for a future `os:args()`. A package can also be **mixed**: a `.pp` file in
`lib/` that imports a native module and wraps it with typed functions.
