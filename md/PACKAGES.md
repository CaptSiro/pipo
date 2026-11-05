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
* Submodules are imported with `:`. See [Submodules and packages](#submodules-and-packages).
* Dot calls look in the receiver's module too: for `r.area()`, if the type of
  `r` was declared in a module that exports `area`, those overloads take part
  in the call even though `area` was never imported. `r.area()` is then the
  same as `r.shapes:area()`. This works for record values tagged with their
  type (`Rect({...})`, `const Rect r = ...`, `-> Rect`) and for enum values.

## Submodules and packages

A module can contain other modules. A **package** is simply a module that
re-exports its submodules:

```
// ui.pp
export import "./ui/shapes"           // == import "./ui/shapes" + export shapes
export import "./ui/colors" -> palette
export import "./ui/shapes" -> { rect }   // re-export single symbols

// main.pp
import "./ui"
const ui:shapes:Rect r = ui:shapes:rect(2, 3)
r.area()                              // == ui:shapes:area(r)
ui:rect(1, 1)
```

Importing a submodule directly:

```
import os:fs                          // bound as `fs`
import os:fs -> files                 // alias
import os:fs -> { exists }            // destructure
import "./ui":shapes                  // files work the same way
```

`import a:b` resolves `b` step by step:

1. If module `a` exports `b`, and `b` is a module, that module is used.
2. Otherwise the file `b.pp` in a folder named `a` is loaded: `lib/a/b.pp`
   for a library module, or `./ui/shapes.pp` for `import "./ui":shapes`.
   The parent `a` itself does not have to exist, so a plain folder of
   modules works as a namespace.

Deeper paths (`import a:b:c`) repeat the same step. The name bound by
default is the last segment.

`export import` is only allowed at the top level, like `export`.
Dot calls already search the module that **declared** the receiver's type,
so `r.area()` finds `area` in `ui:shapes` however `r` reached the caller.
They also search the functions an `impl` recorded for the receiver's type,
exported or not (see Interfaces in LANGUAGE.md).

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

| function                 |                                                                        |
|--------------------------|------------------------------------------------------------------------|
| `print(...)`             | arguments separated by spaces, then a newline. Objects are pretty JSON |
| `write(...)`             | the same without the newline                                           |
| `eprint(...)`            | like print, to stderr                                                  |
| `input(...) -> string`   | prints the prompt, reads one line                                      |
| `read_file(path)`        | `success(text)` or `fail(message)`                                     |
| `write_file(path, text)` | `success(bytes_written)` or `fail(message)`                            |
| `nl`                     | the constant `"\n"`: `io:write("done" + io:nl)`                        |

## The `os` module

| member | |
|--------|-|
| `platform` | `"windows"`, `"linux"` or `"macos"` |
| `args() -> string[]` | the script path, then the arguments after it |
| `join(parts...) -> string` | joins with the platform separator; empty parts are skipped, an absolute part starts over |
| `join_pp(parts...) -> string` | `join(pp:dir(), parts...)`: a path next to the file the call is written in |

## The `types` module

Typed arrays: fixed-length arrays of raw numbers, like Java's `byte[]` /
`int[]` or JavaScript's `Int8Array`. `lib/types.pp` declares them and
`src/modules/types.c` implements them.

```
import types

const buf = types:int8array(4)        // Int8Array[0, 0, 0, 0]: the length never changes
buf[0] = 127
buf[-1] = -128                        // negative indices count from the end
buf[1] = 128                          // error: 128 does not fit in Int8Array (-128...=127)
buf[1] = 1.5                          // error: Int8Array holds ints, got float
types:float32array([1, 2.5])          // a copy of an array: Float32Array[1.0, 2.5]
types:int64array(buf)                 // or of another typed array
types:Int8Array(4)                    // error: a type is not callable (types:Int8Array{} neither)
```

| type | constructor | elements |
|------|-------------|----------|
| `Int8Array`, `Int16Array`, `Int32Array`, `Int64Array` | `int8array`, `int16array`, `int32array`, `int64array` | ints of 8 to 64 bits; only ints that fit |
| `Float32Array`, `Float64Array` | `float32array`, `float64array` | floats; ints are converted |
| `IntArray`, `FloatArray`, `TypedArray` | | unions of the above, for parameters |

* The constructor takes the length (the elements start at 0), an array, or a
  typed array to copy. The constructors are flatcase, matching the type names
  (the other modules use snake_case). Every value written is checked: a float or an int out of
  range is an error, never silently cut down.
* `#a`, `a[i]`, `a[i] = v`, `a[i] += v`, `for x in a`, `for i, x in a`, `copy(a)`,
  `==` (same type, length and elements) and printing (`Int8Array[1, 2]`) work
  like on arrays. Reading gives an `int` or a `float`.
* There is no `push`: the length is fixed when the array is made.

| function | |
|----------|-|
| `fill(a, v)`, `fill(a, v, start, end)` | sets every element / the ones in `[start, end)` |
| `slice(a, start)`, `slice(a, start, end)` | a new typed array of the same kind (negative indices as for arrays) |
| `to_array(a)` | the elements as `int[]` / `float[]` |
| `copy_into(src, src_pos, dst, dst_pos, length)` | like Java's `System.arraycopy`; same kinds, ranges may overlap |
| `element_size(a)` | bytes per element |
| `bytes(string) -> Int8Array` | the UTF-8 bytes of a string |
| `text(bytes)`, `text(bytes, start, end)` | bytes read back as a string |

The functions are exported, not `global`: call them as dot calls (`buf.fill(0)`,
found in the module that declared the receiver's type) or as `types:fill(buf, 0)`.

## The `fs` module

Files and directories, modelled on Java's `java.io.File` and its streams.
A `File` or `Directory` holds a path; nothing is opened or created until a
function runs, and each function looks at the file system again. Contents are
bytes in a `types:Int8Array`.

```
import fs
import types

const dir = fs:directory("data")
dir.create_all()                              // like Java's mkdirs
const f = dir.file("order.txt")               // or fs:file("data", "order.txt")
f.write("hello")                              // success(5)
f.read_text()                                 // success("hello")
f.read()                                      // success(Int8Array[104, 101, 108, 108, 111])

const input = f.open_read().unwrap()          // a stream: read a piece at a time
const buf = types:int8array(4096)
let n = input.read(buf)                       // bytes read, -1 at the end
while n != -1 {
    io:write(buf.text(0, n))
    n = input.read(buf)
}
input.close()
```

What can fail for reasons outside the program (opening, reading or writing a
whole file, listing a directory) returns an `Error` whose failure is a message
such as `cannot open 'x.txt': No such file or directory`. Simple queries answer
like Java does: `exists()` is false, `size()` is 0 and `delete()` is false.

| plain paths | |
|-------------|-|
| `separator`, `path_separator` | constants: `"\"` / `";"` on Windows, `"/"` / `":"` elsewhere |
| `exists(path)`, `is_dir(path)`, `is_file(path)` | what is at a path |
| `cwd() -> Directory` | the working directory |

| `File` and `Directory` | |
|------------------------|-|
| `file(path)`, `file(parent, name)` | the constructor (the type `File` is not callable); `parent` is a string or a `Directory`; `directory(...)` the same |
| `d.file(name)`, `d.dir(name)` | `file(d, name)`, `directory(d, name)` |
| `path()`, `name()` | the path as written, its last part |
| `parent() -> Directory?` | the path without its last part, `none()` for `"x.txt"` |
| `absolute()` | the same kind of value with an absolute path |
| `exists()`, `modified()` | exists; last change in ms since 1970 (0 when missing) |
| `rename(to)` | `to` is a string or the same kind; false when it failed |

| `File` | |
|--------|-|
| `is_file()`, `extension()`, `size()` | `"txt"` for `order.txt` (`""` for `.gitignore`); bytes |
| `read() -> Error<types:Int8Array>`, `read_text() -> Error<string>` | the whole contents |
| `write(data)`, `append(data) -> Error<int>` | `data` is a string or `types:Int8Array`; `append` creates the file |
| `create() -> Error<bool>` | an empty file; `success(false)` when it already existed |
| `delete() -> bool`, `copy_to(to) -> Error<int>` | |
| `open_read() -> Error<InputStream>`, `open_write()`, `open_append() -> Error<OutputStream>` | streams |

| `Directory` | |
|-------------|-|
| `is_dir()`, `create()`, `create_all()`, `delete()` | `create_all` makes missing parents; `delete` needs it empty |
| `list() -> Error<string[]>`, `files() -> Error<File[]>`, `dirs() -> Error<Directory[]>` | the contents, sorted by name |

| streams | |
|---------|-|
| `InputStream.read() -> int` | the next byte as 0...=255, -1 at the end |
| `InputStream.read(buffer)`, `read(buffer, offset, length) -> int` | fills a `types:Int8Array`; bytes read, -1 at the end |
| `InputStream.skip(n) -> int` | |
| `OutputStream.write(byte)`, `write(data)`, `write(buffer, offset, length)`, `flush()` | `data` is a string or `types:Int8Array` |
| `close()`, `is_open()`, `path()` | using a closed stream is an error; closing twice is not |

Close streams when done. One that is dropped without `close()` is only closed
when the garbage collector frees it, which may be much later.

## The `pp` module

Where the running code lives, like PHP's `__FILE__`, `__DIR__` and `__LINE__`:

| function | |
|----------|-|
| `file() -> string` | absolute path of the file the call is written in |
| `dir() -> string`  | its directory (the working directory in the REPL) |
| `line() -> int`    | the line of the call |

They describe the file the **call is written in**, not the entry script, so
`pp:dir()` inside an imported module is that module's directory. Paths built
from them do not depend on the directory the script was started from:

```
import os
const path = os:join_pp("order.txt")      // == os:join(pp:dir(), "order.txt")
```

## Planned standard packages

The interpreter core keeps only the most basic functions internal
([BUILTINS.md](BUILTINS.md)). The rest is meant to come as packages:

| package | contents (ideas) |
|---------|------------------|
| `os`    | env vars, exit, processes (`platform` and `args()` exist) |
| `fs`    | `File`, `Directory` and streams exist; more metadata, permissions, temp files |
| `math`  | trig, logarithms, constants, random |
| `time`  | wall clock, dates, sleep |
| `json`  | parse / stringify |

A package is either a **`.pp` file** in `lib/`, which CMake copies next to
`pp.exe`, or a **native module** written in C, like `src/modules/io.c`:

```c
static ObjModule* fs_init(VM* vm) {
    ObjModule* m = module_new(vm, "fs", "<native os:fs>", vm->builtins);
    vm_push(vm, obj_val(m));
    def_native(vm, m->env, "exists", "(string path) -> bool", fs_exists);
    module_export_all(m);
    vm->sp--;
    return m;
}

static ObjModule* os_init(VM* vm) {
    ObjModule* m = module_new(vm, "os", "<native os>", vm->builtins);
    vm_push(vm, obj_val(m));                          /* keep it alive while building */
    def_const_str(vm, m->env, "platform", "windows");  /* os:platform, read-only */
    def_const(vm, m->env, "max_int", int_val(INT64_MAX));
    def_native(vm, m->env, "args", "() -> string[]", os_args);
    def_submodule(vm, m, "fs", fs_init);               /* os:fs, also `import os:fs` */
    module_export_all(m);                              /* native modules export everything */
    vm->sp--;
    return m;
}

void mod_os_register(VM* vm) { vm_register_native_module(vm, "os", os_init); }
```

| helper | |
|--------|-|
| `def_native(vm, env, name, sig, fn)` | a function, signature in the language's syntax |
| `def_const(vm, env, name, value)` | a read-only value, e.g. `io:nl` |
| `def_const_str(vm, env, name, "text")` | shorthand for a string constant |
| `def_submodule(vm, parent, name, init)` | a native module inside another one |
| `module_export_all(m)` | export everything defined so far |

Native modules live in `src/modules/`, one file per module (CMake picks up
every `src/modules/*.c`). Declare the register function in `src/vm.h` and call
it from `vm_new` (`src/interp.c`), next to `mod_io_register`. This is how
`src/modules/os.c` is built.

### Mixed modules (.pp + C)

[NATIVE_MODULES.md](NATIVE_MODULES.md) walks through a complete one (`fs`): its
object types, allocation and freeing, constants, and file streams.

A module can also be **mixed**: the `.pp` file in `lib/` is the module. It
declares the types, interfaces, constants and exports, and leaves only some
function bodies to C with `native function`:

```
// lib/geometry.pp
export type Vec = { float x, float y }

native function hypot(float, float) -> float        // private: not exported
export function length(Vec v) -> float {
    return hypot(v:x, v:y)
}

export native function dot(Vec, Vec) -> float       // exported like any function
export native "scale_f" function scale(Vec, float) -> Vec
export native "scale_i" function scale(Vec, int) -> Vec
global native function ==(Vec, Vec) -> bool
```

```c
// src/modules/geometry.c
#include "vm.h"

static Value geo_hypot(VM* vm, int argc, Value* a) {
    return float_val(hypot(a[0].as.f, a[1].as.f));
}
/* ... */

static const NativeImpl GEOMETRY[] = {
    {"hypot", geo_hypot},
    {"dot", geo_dot},
    {"scale_f", geo_scale_f},
    {"scale_i", geo_scale_i},
    {"==", geo_eq},
    {NULL, NULL},
};

void mod_geometry_register(VM* vm) {
    vm_register_native_impls(vm, "geometry", GEOMETRY);
}
```

* `native ["impl"] [<T>]function name(params) -> T` has no body. It is only
  allowed at the top level, and combines with `export` and `global`
  (`export native function`, `global native function`).
* The C body is looked up by the function's name, or by the string after
  `native`. Use the string to give each overload its own C function. Several
  declarations may also share one C function, which then checks the arguments
  itself.
* `vm_register_native_impls` takes the **library module path**: `"geometry"`
  for `lib/geometry.pp`, `"os:fs"` for `lib/os/fs.pp`. Only library modules
  (`import geometry`) get native bodies. A file imported by path, or the main
  script, cannot declare `native` functions.
* Types in the signature are resolved in the `.pp` file, so they can name its
  own types (`Vec` above). Overload resolution picks native declarations like
  any other function.
* The C function receives the arguments after they matched the signature.
  Ints passed to `float` parameters arrive as floats, and the result is checked
  against (and converted to) the declared return type, as for a function
  written in the language. A record returned as `-> Vec` is tagged `Vec`, so
  dot calls such as `v.length()` work on it.
* Declarations without a C body fail when the module loads:
  `module 'geometry' has no native implementation named 'dot'`. C entries
  without a declaration are simply unused.
* `native type Map<K, V>` binds a type implemented in C. `Map` and `Set` are the
  interpreter's own (the containers in `src/collections.c`; the generic
  parameters must match them), and `lib/std.pp` uses them to export `std:Map` and
  `std:Set`. Any other name must be a class the module's C half registered, see
  [Native types](#native-types).
* Don't register a pure native module (`vm_register_native_module`) under the
  same name. It is found first, and the `.pp` file would be ignored.

### Native types

A mixed module can also implement **types** in C, like `lib/types.pp`
(`Int8Array`, ...) and `lib/fs.pp` (`File`, ...). The C half describes each one
with a `NativeClass` and registers them; the `.pp` file binds them by name:

```
// lib/geometry.pp
export native type Buffer                        // PascalCase, and not callable
export native function buffer(int size) -> Buffer  // the constructor: an ordinary native function
export native function size(Buffer) -> int       // functions take and return it like any type
```

```c
typedef struct {
    ObjInstance base; /* the header every instance starts with */
    int64_t size;
    char* bytes;      /* plain C data only: the GC does not look inside instances */
} ObjBuffer;

/* buffer(int size) -> Buffer: geometry.pp already checked the argument */
static Value geo_buffer(VM* vm, int argc, Value* a) {
    if (a[0].as.i < 0) rt_error(vm, 0, "buffer(size): the size must not be negative");
    ObjBuffer* b = (ObjBuffer*)instance_new(vm, vm_native_type(vm, "geometry", "Buffer")); /* zeroed */
    b->size = a[0].as.i;
    b->bytes = calloc((size_t)b->size + 1, 1);
    return obj_val(b);
}
static void buf_free(ObjInstance* self) { free(((ObjBuffer*)self)->bytes); }

static const NativeClass TYPES[] = {
    {.name = "Buffer", .size = sizeof(ObjBuffer), .constructor = "geometry:buffer", .free = buf_free},
    {.name = NULL},
};

void mod_geometry_register(VM* vm) {
    vm_register_native_types(vm, "geometry", TYPES);
    vm_register_native_impls(vm, "geometry", GEOMETRY);
}
```

* A native type cannot be called: `geometry:Buffer(16)` and `geometry:Buffer{}`
  are errors that name the `constructor` given in the class
  (`type 'Buffer' cannot be called; make a value with geometry:buffer(...)`).
  Values are made by the module's own functions with `instance_new`. The hooks
  are optional: `free` (release what the instance
  owns), `format` (printing, default `<Buffer>`), `equals` (`==` between two
  instances of the class, default identity), `copy` (`copy(x)`, default `x`
  itself), `length` (`#x`), `get` / `set` (`x[i]`, `x[i] = v`, and `for` when
  `length` and `get` are both given) and `data`, a pointer the class's hooks can
  share.
* `type_of(x)` is the class name, and dot calls find the functions exported by the
  module that bound the type, as for records.
* The types exist from registration on, before the module is imported, so C code
  can make values of another module's type: `vm_native_type(vm, "types", "Int8Array")`
  gives the type to pass to `instance_new`. `src/modules/fs.c` uses
  `typed_array_new` (declared in `vm.h`) to return file contents.
* Instances must not hold references to GC objects (strings, arrays, ...); copy
  what they need into C memory.
