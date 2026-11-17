# Extern functions: calling DLLs (draft 1)

Status: **draft, partly implemented** (2026-11-17). Sections 1–6 describe what
works now in the interpreter and the bytecode VM. Sections 7–9 are the parts
that are only designed: they need decisions before they are built.

The example that drove the design is a hello-world triangle with GLFW and
OpenGL 3.3, in `C:\programming\pipo\opengl` (see its README.md).

## Contents

1. [The declaration](#1-the-declaration)
2. [Types: how values cross into C](#2-types-how-values-cross-into-c)
3. [Finding the library](#3-finding-the-library)
4. [OpenGL](#4-opengl)
5. [How a call works (no libffi)](#5-how-a-call-works-no-libffi)
6. [Errors, targets and limits](#6-errors-targets-and-limits)
7. [Not done yet: callbacks](#7-not-done-yet-callbacks)
8. [Not done yet: structs, pointers, memory](#8-not-done-yet-structs-pointers-memory)
9. [Open questions and decisions to revisit](#9-open-questions-and-decisions-to-revisit)

## 1. The declaration

`extern` is `native` with a library name in front. It has the same shape
(`native "impl" function f(...) -> T`) and the same rules: no body, a full
signature with a return type, top level only, and it works with `export` and
`global`.

```
import c

extern "glfw3" function glfwInit() -> c:i32                       // symbol glfwInit of glfw3.dll
extern "opengl32" "glClearColor" function clear_color(c:f32 r, c:f32 g, c:f32 b, c:f32 a) -> void
export extern "opengl32" "glClear" function clear(c:u32 mask) -> void
```

```
extern = 'extern' string string? 'function' name '(' params ')' '->' type
```

* The first string is the **library**, the second, optional one the **C
  symbol**. Without it, the symbol is the function's own name.
* Unlike `native`, `extern` works in **any file**, not only in library
  modules: there is nothing to register in C.
* An extern function is an ordinary function value afterwards: it overloads
  with other functions of the same name (by PiPo types), takes dot calls
  (`"abc".strlen()`), and can be passed around (`xs.map(x -> abs(x))`).
* It cannot be generic or variadic: C has one type per parameter.

**Why a keyword and not `native "glfw3.dll:glfwInit"`.** The string after
`native` names a C body the module registered; giving it a second meaning
would make `native "x"` ambiguous to read. `extern` is the C word for "defined
elsewhere", and readers of C bindings expect it.

## 2. Types: how values cross into C

The parameter and return types are PiPo types, so the checker and the rest of
the program see ints and floats as usual. The C type follows from the type as
written:

| written | C parameter | C result |
|---------|-------------|----------|
| `int` | `int64_t` | `int64_t` |
| `float` | `double` | `double` |
| `bool` | `int`, 0 or 1 | `int`, non-zero is true |
| `string` | `const char*`, NUL-terminated UTF-8 | copied into a new string; NULL is `""` |
| `string[]` | `const char**`, an array built for the call (NULL after the last) | — |
| `types:Int8Array` ... `types:Float64Array` | pointer to the elements; C may write them | — |
| `string?`, `types:*Array?` | as above; `none()` is NULL | — |
| `c:i8` `c:u8` `c:i16` `c:u16` `c:i32` `c:u32` | that width; a value that does not fit is a runtime error | sign- or zero-extended |
| `c:i64` `c:u64` | 64 bits (a `u64` above the largest int is negative) | the bits |
| `c:f32` | `float` | `float`, widened |
| `c:f64` | `double` | `double` |
| `c:ptr` | an address in an int, 0 is NULL | the address |
| `void` | — | nothing |

The `c:` types live in `lib/c.pp` and are plain aliases (`export type u32 =
int`). Only an extern declaration looks at the name, so `c:u32` is an `int`
everywhere else. This keeps the language free of new numeric types while the
binding still says exactly what C expects.

**Memory.** Strings and typed arrays are passed by pointer, without copying.
The pointer is valid during the call: the garbage collector never runs while a
native or extern function runs, and the arguments are on the stack. C must
not keep the pointer after it returns (an OpenGL buffer upload copies;
`glVertexAttribPointer` is given an offset into a bound buffer, a `c:ptr`).

**Out-parameters** (`GLuint*`, `int*`) are typed arrays of length 1:

```
extern "opengl32" function glGenBuffers(c:i32 n, types:Int32Array buffers) -> void

export function gen_buffer() -> int {
    const ids = types:int32array(1)
    glGenBuffers(1, ids)

    return ids[0]
}
```

## 3. Finding the library

The library is loaded, and the symbol looked up, on the **first call**, not at
the declaration. A program can declare a whole API and only pay for (or fail
on) what it uses, and OpenGL needs it (section 4).

`extern "glfw3"` tries, in order:

1. `<dir>/glfw3.dll`, where `<dir>` is the folder of the file that declares
   the function (so a binding module and its DLL travel together);
2. `glfw3.dll` through the system's search (`LoadLibrary`: the folder of
   `pp.exe`, the system folders, `PATH`).

A name with an extension is used as is (`"ucrtbase.dll"`), a name with a
folder in it is relative to `<dir>` (`"bin/glfw3"`), an absolute path is
loaded directly. A DLL's own dependencies are searched next to it.

Elsewhere (`dlopen`), `"glfw"` tries `<dir>/glfw.so`, `<dir>/libglfw.so`,
then `glfw.so` and `libglfw.so` through the system's search. **Untested**: only
Windows x64 is tested so far.

## 4. OpenGL

`opengl32.dll` exports only OpenGL 1.1. Everything newer (`glGenBuffers`,
`glCreateShader`, ...) comes from the graphics driver through
`wglGetProcAddress`, and only once a context is current.

The rule that makes `extern "opengl32" function glGenBuffers(...)` work: when a
library does not export a symbol but exports `wglGetProcAddress`, that is asked
too. Combined with loading on the first call, the declarations can be written
at the top of a module, before any window exists.

This is a special case for one library. Section 9 has the general form it
could become.

## 5. How a call works (no libffi)

There is no libffi and no assembly. `src/extern.c` turns every argument into a
64-bit slot and calls the function through a pointer cast to a prototype the
C compiler knows:

* **Windows x64.** The first four arguments travel in `RCX/RDX/R8/R9` or
  `XMM0–3` by position, the rest on the stack in 8-byte slots. Sixteen
  prototypes cover the first four (each integer or `double`); the other twelve
  are integer slots holding raw bits, which is what a `double` or `float` on
  the stack looks like too. A `c:f32` in a register travels as a `double`
  whose low 32 bits are the float, which is all the callee reads. The result is
  read from `RAX`, or from `XMM0` for floats, so there are 32 prototypes in
  all.
* **System V x86-64** (Linux, macOS on Intel). Integers fill six registers in
  order and floats eight registers in order, independently; what does not fit
  goes on the stack in order. One prototype with 6 integer, 8 double and 10
  stack slots covers every mix.

The caller cleans the stack on both, so passing more slots than the callee
reads is harmless. At most 16 parameters. Other platforms (32-bit, ARM64) stop
with a runtime error at the first call.

## 6. Errors, targets and limits

| situation | when | message |
|-----------|------|---------|
| a type C has no counterpart for (`float[]`, a record) | check time | `parameter 'xs' of extern function 'f' has a type C has no counterpart for` |
| a body | parse time | `extern function 'f' cannot have a body` |
| the library is missing | first call | `cannot load library 'glfw3' for extern function 'glfwInit': ... (looked in <dir> and the system's library path)` |
| the symbol is missing | first call | `library 'msvcrt' has no function 'x'`, with a hint for `gl*` names |
| a narrow int does not fit | each call | `argument 1 of 'f' is 256, which does not fit in a C uint8_t` |

* **Targets.** The interpreter (also with `--no-check`) and the bytecode VM.
  The wasm target refuses an extern call at compile time: a browser has no
  DLLs (a JS import could stand in for one later).
* **Crashes.** A wrong declaration (a missing parameter, `c:i32` for a
  pointer) crashes the process like it would in C. The checker cannot know the
  real C signature.
* Not supported yet: callbacks (section 7), structs by value, variadic C
  functions (`printf`), reading C memory through a `c:ptr` (section 8).

## 7. Not done yet: callbacks

GLFW reports keys, resizes and errors through callbacks
(`glfwSetKeyCallback(window, fn)`). The triangle polls instead
(`glfwGetKey`, `glfwGetFramebufferSize`). Callbacks need C-callable function
pointers made at runtime for a PiPo closure.

Proposal: a callback type written with `c:` types, and a conversion that makes
a C function pointer from a closure:

```
type KeyCallback = (c:ptr, c:i32, c:i32, c:i32, c:i32) -> void
extern "glfw3" function glfwSetKeyCallback(c:ptr window, KeyCallback? callback) -> c:ptr
```

* Without libffi's closures, the interpreter would keep a fixed pool of C
  trampolines per signature shape (say 16 per shape: `tramp_0` ... `tramp_15`,
  each looking up slot k of a table of PiPo closures). Running out of slots is
  a runtime error. That covers GLFW, which registers a handful of callbacks.
* The closure must stay alive while C holds the pointer: the pool slot is a GC
  root until the callback is replaced or the process ends.
* A callback runs PiPo code inside a C call, so the collector may run there:
  the extern call must have its arguments rooted (they are, on the stack),
  and nothing may assume "no GC during natives" across it.
* C threads calling back (audio libraries) cannot run PiPo code: the VM is not
  thread safe. Such callbacks are refused, or queued for the main thread.

## 8. Not done yet: structs, pointers, memory

* **Structs.** A record type marked for C layout, `extern type Rect = { c:i32 x,
  c:i32 y }`, passed as a pointer to a temporary copy (and copied back for
  out-parameters). By value: only the sizes Windows passes in a register (1, 2,
  4, 8 bytes); System V's classification is more work.
* **A pointer type.** `c:ptr` is an int, so a window handle and a byte offset
  look the same to the checker. The GLFW binding wraps the handle in a record
  (`Window = { c:ptr handle }`), which is the recommended pattern for now. A
  distinct opaque type (`c:Pointer<Window>`) would let bindings skip the
  wrapper.
* **Reading memory.** `glGetString` returns `const GLubyte*` (that works:
  declare it `-> string`), but `glfwGetError(const char** description)` writes a
  pointer to a string. Proposal: `c:string_at(c:ptr) -> string` and
  `c:read_i32(c:ptr, int offset)`-style functions in `lib/c.pp`, documented as
  unsafe.
* **Variadic functions.** Would need the argument types at each call site;
  `printf` is not worth it while `io` exists.

## 9. Open questions and decisions to revisit

1. **The OpenGL rule (section 4) is a special case.** A general form:
   `extern(loader) function glGenBuffers(...)` where `loader` is a
   `string -> c:ptr` function (`glfw:get_proc_address`), called on the first
   call. That needs calling a C function pointer held in a value, which section
   7's machinery would bring.
2. **`bool` results are read as a C `int`.** C's `_Bool` and `GLboolean` are
   one byte, and the upper bytes of the register may be garbage: declare those
   `-> c:u8` and compare with `!= 0`. Alternative: a `c:bool8` alias.
3. **A NULL `string` result is `""`.** `-> string?` (NULL is `none()`) would be
   more honest; it is refused today to keep results simple.
4. **`c:u64` above `int`'s range comes back negative.** PiPo has no unsigned
   64-bit int.
5. **Range checks on `c:i8`...`c:u32` arguments** are errors, not wraps. GL
   enums and masks all fit, so this has not been in the way.
6. **Where the library is searched.** The declaring file's folder first, then
   the system. A project-level setting (`PP_LIBRARY_PATH`) may be needed for
   shared installs.
7. **Name of the module.** `c` is short and reads well (`c:u32`), but takes a
   one-letter name from the module namespace.
