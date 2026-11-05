# Writing Native Modules

This guide shows how to write a library module whose functions and types are
implemented in C. The example throughout is the `fs` module: `lib/fs.pp` and
`src/modules/fs.c`. It covers most of what a module needs:

* an object type implemented in C (`File`, `InputStream`), how it is allocated
  and freed, and how it prints and compares;
* constructor functions, ordinary functions, overloads and constants;
* opening, reading, writing and closing files, including what happens when
  the garbage collector closes a file that was never closed;
* using a type from another module (`types:Int8Array` for file contents).

[PACKAGES.md](PACKAGES.md) is the reference for imports, exports and the
built-in modules. This page is the walkthrough.

## Contents

1. [Kinds of modules](#1-kinds-of-modules)
2. [The files of a module](#2-the-files-of-a-module)
3. [The .pp half: declarations](#3-the-pp-half-declarations)
4. [The C half: native functions](#4-the-c-half-native-functions)
5. [A native object type](#5-a-native-object-type)
6. [Allocating: constructor functions](#6-allocating-constructor-functions)
7. [Freeing, and how the GC sees instances](#7-freeing-and-how-the-gc-sees-instances)
8. [Reading and writing a whole file](#8-reading-and-writing-a-whole-file)
9. [Streams: opening, reading, writing, closing](#9-streams-opening-reading-writing-closing)
10. [Closing through the GC](#10-closing-through-the-gc)
11. [Constants](#11-constants)
12. [Errors](#12-errors)
13. [Testing](#13-testing)
14. [Checklist](#14-checklist)

## 1. Kinds of modules

| kind | the module is | example |
|------|---------------|---------|
| plain | a `.pp` file in `lib/` | — |
| native | built entirely in C with `vm_register_native_module` | `io`, `os`, `pp` (`src/modules/*.c`) |
| **mixed** | a `.pp` file in `lib/` that leaves some bodies (and types) to C | `std`, `types`, `fs` |

A **mixed** module is usually the best choice. The `.pp` file declares
everything the user sees: types, signatures, constants, exports and doc
comments. The C file holds only the bodies. Overload resolution, argument
checks and return checks then work like they do for functions written in the
language, and the C code never has to check types itself.

## 2. The files of a module

For `fs`:

| file | role |
|------|------|
| `lib/fs.pp` | the module: `native type`, `native function`, `export const` |
| `src/modules/fs.c` | the C half: the instance structs, the hooks, the function bodies, `mod_fs_register` |
| `src/vm.h` | declares `void mod_fs_register(VM* vm);` |
| `src/interp.c` | `vm_new` calls `mod_fs_register(vm)`, next to `mod_io_register` |
| `tests/fs.pp`, `tests/fs.out` | the test script and its expected output |

CMake compiles every `src/modules/*.c` and copies `lib/` next to `pp.exe` on
every build, so new files need no CMake changes.

Registration connects the two halves by the **library module name**:

```c
/* src/modules/fs.c, at the end */
void mod_fs_register(VM* vm) {
    vm_register_native_types(vm, "fs", TYPES); /* the NativeClass table: File, Directory, ... */
    vm_register_native_impls(vm, "fs", FS);    /* the NativeImpl table: function bodies */
}
```

`"fs"` names `lib/fs.pp`. A module in a folder uses `:`, so `"os:fs"` would
name `lib/os/fs.pp`. Only library modules (`import fs`) get a C half. A file
imported by path cannot declare `native` items.

Don't register a pure native module under the same name. It would be found
first, and `lib/fs.pp` would never load.

## 3. The .pp half: declarations

### Types

```
export native type File
export native type Directory
export native type InputStream
export native type OutputStream
```

`native type Name` binds a type that a `NativeClass` registered for this
module (section 5). Types are PascalCase. A native type is **not callable**:
`fs:File("x")` and `fs:File{}` are errors. Values come from constructor
functions (section 6).

### Functions

A `native function` has a signature and no body:

```
export native function exists(string path) -> bool          // C body named "exists"
export native "f_read" function read(File f) -> Error<types:Int8Array>
export native "in_read" function read(InputStream stream, types:Int8Array buffer) -> int
```

* The C body is looked up by the string after `native` (`"f_read"`), or by the
  function's own name when there is none. Give overloads their own string when
  they need different C functions. `read(File)` and `read(InputStream)` are
  unrelated, for example.
* Several declarations can share one C function. `path(File)` and
  `path(Directory)` are both `"p_path"`, and the three `file(...)` constructors
  are all `"file"`. The C function then tells the overloads apart by `argc` and
  by the kind of each argument.
* Declarations without `export` are private helpers (`name_separator`, see
  section 11).
* Types from other modules work after an import: `import types` at the top of
  `fs.pp` lets the signatures use `types:Int8Array`.

What the interpreter does around every call:

1. **Overload resolution** picks the declaration from the argument types. So
   when `f_read` runs, `a[0]` *is* a `File`. A call that matches no declaration
   fails before reaching C.
2. **Conversion**: an `int` passed to a `float` parameter arrives as a float.
3. **Return check**: the result is checked against the declared return type
   (`Error<types:Int8Array>`). A mismatch is a runtime error, which catches
   bugs in the C code.

**Dot calls** need no extra work: `f.read()` finds `read` because the receiver's
type was declared by `fs`, which exports `read`.

## 4. The C half: native functions

Every body has the same signature, `NativeFn`:

```c
typedef Value (*NativeFn)(VM* vm, int argc, Value* args);
```

```c
/* exists(string path) -> bool */
static Value fs_exists(VM* vm, int argc, Value* a) { return bool_val(path_exists(AS_STR(a[0])->chars)); }
```

**Reading arguments** (types already checked by the declaration):

| declared as | read it with |
|-------------|--------------|
| `int` / `float` / `bool` | `a[i].as.i` / `a[i].as.f` / `a[i].as.b` |
| `string` | `AS_STR(a[i])->chars` (NUL-terminated), `->len` (bytes) |
| `T[]` | `AS_ARRAY(a[i])->items`, `->count` |
| a native type | cast the object: `((ObjPath*)a[i].as.o)->path`, wrapped in `fs.c` as `PATH_OF(a[i])` |
| `types:Int8Array` | `((ObjTypedArray*)a[i].as.o)->data`, `->length` |
| `string \|\| X` (several overloads, one body) | test the kind first: `IS_KIND(a[1], OBJ_STRING)` |

**Making results:**

| result | C |
|--------|---|
| nothing (`-> void`) | `void_val()` |
| `bool`, `int`, `float` | `bool_val(b)`, `int_val(i)`, `float_val(d)` |
| `string` | `obj_val(str_cnew(vm, cstr))`, `obj_val(str_new(vm, ptr, len))` |
| `T[]` | `ObjArray* r = array_new(vm, cap);` then `r->items[r->count++] = v;` and `obj_val(r)` |
| `T?` | `make_some(vm, v)`, `make_none(vm)` |
| `Error<S>` | `make_success(vm, v)`, `make_fail(vm, message_value)` |
| a native type | section 6 |
| `types:Int8Array` | `typed_array_new(vm, TA_I8, length)` (declared in `vm.h`) |

The strings are copied, so a C buffer can be freed right after `str_new`.

**The GC does not run during a native function.** It only runs between
statements. That is why `list_dir` can fill an array with new `File` objects
before anything references the array:

```c
ObjArray* r = array_new(vm, n);
for (int i = 0; i < n; i++) {
    ...
    if (mode == 1 && !d) r->items[r->count++] = new_file(vm, full);
}
return make_success(vm, obj_val(r));
```

A C function that calls back into the language (`vm_call`) can run statements,
and statements can collect garbage. `fs` never does that. If yours does, push
the values you keep onto the VM stack (`vm_push`) first, as `os:args` shows.

## 5. A native object type

A native type has two parts: a C struct for its instances, and a `NativeClass`
describing the type to the interpreter.

### The instance struct

```c
typedef struct {
    ObjInstance base; /* must come first: the GC header and the instance's type */
    char* path;       /* owned: malloc'd here, freed by path_free */
} ObjPath;

typedef struct {
    ObjInstance base;
    FILE* fp;         /* owned; NULL once closed */
    char* path;       /* owned */
} ObjStream;
```

`File` and `Directory` share `ObjPath`, and both stream types share
`ObjStream`. Everything after `base` belongs to your module.

**Rule:** an instance holds only plain C data. The collector never looks inside
instances, so an `ObjString*` (or any other object) stored in one would not be
kept alive, and could be freed while the instance still points to it. `fs`
copies the path out of the argument string (`pp_strdup`) rather than keeping
the string.

### The class

```c
#define PATH_CLASS(n, ctor) \
    {.name = n, .size = sizeof(ObjPath), .constructor = ctor, .free = path_free, .format = path_format, \
     .equals = path_equals}
#define STREAM_CLASS(n, ctor) \
    {.name = n, .size = sizeof(ObjStream), .constructor = ctor, .free = stream_free, .format = stream_format}

static const NativeClass TYPES[] = {
    PATH_CLASS("File", "fs:file"),
    PATH_CLASS("Directory", "fs:directory"),
    STREAM_CLASS("InputStream", "file.open_read"),
    STREAM_CLASS("OutputStream", "file.open_write"),
    {.name = NULL},                       /* end of the table */
};
```

| field | meaning | if left out |
|-------|---------|-------------|
| `name` | what `native type` binds, and `type_of(x)` | required |
| `size` | `sizeof` the instance struct | required |
| `constructor` | named in the error for `fs:File(...)`: `make a value with fs:file(...)` | the error names no function |
| `free` | releases what the instance owns (section 7) | nothing to release |
| `format` | printing: `io:print`, `str(x)`, inside arrays | `<File>` |
| `equals` | `a == b` for two instances of the class | identity |
| `copy` | `copy(x)` | `x` itself |
| `length` | `#x`, and the number of steps of `for` | `#x` is an error |
| `get` / `set` | `x[i]` / `x[i] = v`; `for` uses `length` + `get` | not indexable |
| `data` | a pointer the hooks may share | — |

The hooks `fs` uses:

```c
/* io:print(f) shows File("data/order.txt") */
static void path_format(StrBuf* sb, ObjInstance* self) {
    sb_printf(sb, "%s(\"%s\")", self->type->name->chars, ((ObjPath*)self)->path);
}

/* f == g: called only for two instances of the same class, so a File never equals a Directory */
static bool path_equals(ObjInstance* a, ObjInstance* b) { return strcmp(((ObjPath*)a)->path, ((ObjPath*)b)->path) == 0; }
```

The typed arrays in `src/modules/types.c` use the rest of the hooks (`length`,
`get`, `set`, `copy`), and are the example to read for an indexable type.

### Where the types live

`vm_register_native_types` creates one `ObjNativeType` per class when the
interpreter starts. They are permanent (never collected) and exist before
`lib/fs.pp` is loaded. `native type File` only binds the name `File` in the
module, and records `fs` as the module that declared it, so dot calls find
`fs`'s functions. C code gets a type by name:

```c
static ObjNativeType* fs_type(VM* vm, const char* name) { return vm_native_type(vm, "fs", name); }
```

## 6. Allocating: constructor functions

`instance_new` allocates `size` bytes, zeroes everything after the GC header,
sets the type and links the object into the GC's list. The fields are then
yours to fill:

```c
static Value path_new(VM* vm, const char* type, char* path /* taken */) {
    ObjPath* o = (ObjPath*)instance_new(vm, fs_type(vm, type));
    o->path = path;
    return obj_val(o);
}
```

A type is never called, so making values is the job of ordinary functions,
named after the type in snake_case or flatcase (see the code conventions in
[PROPOSAL.md](PROPOSAL.md#code-conventions)). `fs.pp` declares the
overloads:

```
export native function file(string path) -> File
export native function file(string parent, string name) -> File
export native function file(Directory parent, string name) -> File
```

All three share one C body, which tells them apart by `argc` and the kind of
the first argument:

```c
static Value path_make(VM* vm, const char* type, int argc, Value* a) {
    if (argc == 1) return path_new(vm, type, pp_strdup(AS_STR(a[0])->chars));
    const char* base = IS_KIND(a[0], OBJ_STRING) ? AS_STR(a[0])->chars : PATH_OF(a[0]);
    return path_new(vm, type, path_join(base, AS_STR(a[1])->chars));
}

static Value fs_file(VM* vm, int argc, Value* a) { return path_make(vm, "File", argc, a); }
```

`dir.file("x")` is the same function: a dot call passes the receiver as the
first argument, so it matches `file(Directory parent, string name)`.

Other functions return instances too, for example `parent() -> Directory?`
(`make_some(vm, new_dir(vm, ...))`) and `absolute()`. Streams are made only by
`open_read` / `open_write` / `open_append` (section 9).

## 7. Freeing, and how the GC sees instances

The collector is mark and sweep (`src/gc.c`). It runs **between statements**,
and only once the number of objects reaches a threshold: 200 000, or twice the
number that survived the last collection, whichever is larger. When it finds an instance that
nothing reaches, it calls the class's `free` hook and then frees the object:

```c
static void path_free(ObjInstance* self) { free(((ObjPath*)self)->path); }
```

* Release only what the instance **owns**. Never free `self`; the collector
  does that.
* `free` has no `VM*` and must not call into the language or create objects.
* The collector does not mark anything inside instances. Store no object
  pointers (section 5).
* When the program ends, the interpreter exits **without** freeing objects, so
  `free` hooks do not run at exit. The operating system takes back memory.
  For C library `FILE*`s, see section 10.

## 8. Reading and writing a whole file

### `read()` and `read_text()`

```
export native "f_read" function read(File f) -> Error<types:Int8Array>
export native function read_text(File f) -> Error<string>
```

Both use `slurp`, which opens the file, reads it into a growing buffer and
closes it before returning, so the file never stays open, whether the read
succeeds or fails:

```c
static char* slurp(const char* path, int64_t* out_len, const char** what) {
    *what = "open";
    if (path_is_dir(path)) {
        errno = EISDIR;
        return NULL;
    }
    FILE* fp = fopen(path, "rb");                   /* binary: no newline translation on Windows */
    if (!fp) return NULL;
    *what = "read";
    size_t cap = 1 << 16, len = 0;
    char* buf = pp_malloc(cap);
    for (;;) {
        if (len == cap) {
            cap *= 2;
            buf = pp_realloc(buf, cap);
        }
        size_t got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) break;
    }
    bool bad = ferror(fp) != 0;
    int err = errno;                                /* save it: fclose may change errno */
    fclose(fp);
    if (bad) {
        free(buf);
        errno = err;
        return NULL;
    }
    *out_len = (int64_t)len;
    return buf;
}
```

The functions then turn the buffer into a value of the declared type, or the
failure into an `Error`:

```c
static Value f_read(VM* vm, int argc, Value* a) {
    int64_t len;
    const char* what;
    char* buf = slurp(PATH_OF(a[0]), &len, &what);
    if (!buf) return fail_errno(vm, what, PATH_OF(a[0]));   /* fail("cannot open 'x': No such file or directory") */
    ObjTypedArray* r = typed_array_new(vm, TA_I8, len);      /* a types:Int8Array, made from C */
    memcpy(r->data, buf, (size_t)len);
    free(buf);
    return make_success(vm, obj_val(r));
}
```

`typed_array_new` belongs to the `types` module. Any C file can use it
because the type exists from start-up (section 5), and `fs.pp` imports
`types` so that the return type `types:Int8Array` resolves.

### `write()` and `append()`

```
export native "f_write" function write(File f, types:Int8Array data) -> Error<int>
export native "f_write" function write(File f, string text) -> Error<int>
export native function append(File f, types:Int8Array data) -> Error<int>
export native function append(File f, string text) -> Error<int>
```

Two overloads per function share one body. `bytes_of` checks which kind
arrived:

```c
static const void* bytes_of(VM* vm, Value v, size_t* len) {
    if (IS_KIND(v, OBJ_STRING)) {
        *len = (size_t)AS_STR(v)->len;
        return AS_STR(v)->chars;
    }
    ObjTypedArray* t = (ObjTypedArray*)v.as.o;
    *len = (size_t)t->length;
    return t->data;
}

static Value write_whole(VM* vm, Value* a, const char* mode) {   /* "wb" for write, "ab" for append */
    const char* path = PATH_OF(a[0]);
    size_t len;
    const void* data = bytes_of(vm, a[1], &len);
    FILE* fp = fopen(path, mode);
    if (!fp) return fail_errno(vm, "open", path);
    size_t put = fwrite(data, 1, len, fp);
    int err = errno;
    if (fclose(fp) != 0 || put != len) {                         /* fclose flushes: a full disk may show only here */
        errno = err ? err : EIO;
        return fail_errno(vm, "write", path);
    }
    return make_success(vm, int_val((int64_t)put));
}
```

## 9. Streams: opening, reading, writing, closing

Streams keep a file open across calls, like Java's `FileInputStream` and
`FileOutputStream`. From the language side:

```
const input = f.open_read().unwrap()
const buf = types:int8array(4096)       // a fixed buffer, filled again and again
let n = input.read(buf)
while n != -1 {
    io:write(buf.text(0, n))
    n = input.read(buf)
}
input.close()
```

### Opening

Opening can fail for reasons outside the program, so it returns an `Error`.
On success, the new stream owns the `FILE*`:

```c
static Value open_with(VM* vm, Value file, const char* mode, const char* type) {
    const char* path = PATH_OF(file);
    if (path_is_dir(path)) {
        errno = EISDIR;
        return fail_errno(vm, "open", path);
    }
    FILE* fp = fopen(path, mode);
    if (!fp) return fail_errno(vm, "open", path);
    return make_success(vm, stream_new(vm, type, fp, path));
}

static Value f_open_read(VM* vm, int argc, Value* a) { return open_with(vm, a[0], "rb", "InputStream"); }
static Value f_open_write(VM* vm, int argc, Value* a) { return open_with(vm, a[0], "wb", "OutputStream"); }
static Value f_open_append(VM* vm, int argc, Value* a) { return open_with(vm, a[0], "ab", "OutputStream"); }
```

### The open / closed state

`fp` is `NULL` once the stream is closed. Every function that uses the file
goes through `open_stream`, so using a closed stream is a runtime error (a bug
in the program, not an `Error` value):

```c
static FILE* open_stream(VM* vm, Value v) {
    ObjStream* s = STREAM(v);
    if (!s->fp) rt_error(vm, 0, "%s of '%s' is closed", s->base.type->name->chars, s->path);
    return s->fp;
}
```

### Reading

`read(stream, buffer)` freads straight into the typed array's storage. It
returns the number of bytes read, or -1 at the end of the file. The
`(buffer, offset, length)` overload shares the body:

```c
static Value in_read(VM* vm, int argc, Value* a) {
    FILE* fp = open_stream(vm, a[0]);
    ObjTypedArray* buf = (ObjTypedArray*)a[1].as.o;
    int64_t off = 0, n = buf->length;
    if (argc > 2) {
        off = a[2].as.i;
        n = a[3].as.i;
        check_span(vm, "read", off, n, buf->length);   /* rt_error when outside the buffer */
    }
    if (n == 0) return int_val(0);
    size_t got = fread((char*)buf->data + off, 1, (size_t)n, fp);
    if (got == 0 && ferror(fp)) rt_error(vm, 0, "cannot read '%s': %s", STREAM(a[0])->path, strerror(errno));
    return int_val(got == 0 ? -1 : (int64_t)got);
}
```

The buffer's length never changes, so the bounds checked here stay valid.

### Writing

`write(stream, data)` writes a string or an `Int8Array`, reusing `bytes_of`.
Data goes into the C library's buffer. It reaches the file when the buffer
fills, on `flush()`, on `close()`, or at the end of the program:

```c
static void out_put(VM* vm, Value s, const void* data, size_t n) {
    FILE* fp = open_stream(vm, s);
    if (fwrite(data, 1, n, fp) != n) rt_error(vm, 0, "cannot write '%s': %s", STREAM(s)->path, strerror(errno));
}
```

### Closing

```c
static Value s_close(VM* vm, int argc, Value* a) {
    ObjStream* s = STREAM(a[0]);
    if (s->fp) {
        int r = fclose(s->fp);
        s->fp = NULL;                               /* closed for good: open_stream refuses it from now on */
        if (r != 0) rt_error(vm, 0, "cannot close '%s': %s", s->path, strerror(errno));
    }
    return void_val();                              /* closing twice does nothing */
}
```

`fp` is set to `NULL` before reporting the error, so the stream is closed
either way, and the GC will not close it again.

## 10. Closing through the GC

A stream that is dropped while still open is closed by its `free` hook when
the collector frees it, much like a Java finalizer or `Cleaner`:

```c
static void stream_free(ObjInstance* self) {
    ObjStream* s = (ObjStream*)self;
    if (s->fp) fclose(s->fp);   /* still open: the program never called close() */
    free(s->path);
}
```

This is a safety net, not a way to manage files:

* **It can come late.** The collector only runs once many objects have been
  allocated. A loop that opens files without `close()` runs out of handles
  first. On Windows, 3000 unclosed `open_read()` calls fail with `Too many open
  files` long before a collection.
* **Errors are lost.** If `fclose` fails here, there is no caller left to
  report it to.
* **At exit**, the interpreter frees no objects, so `stream_free` never runs.
  The C library flushes and closes every open `FILE*` when the process exits
  normally (also after a runtime error, which calls `exit`). So an
  `OutputStream` that was never closed still has its data written, unless the
  process is killed.

So: call `close()` when done. The GC only catches what was missed. A new
resource type follows the same pattern: give the struct an "open" handle, set
it to empty in `close`, and release it in `free` only when it is still open.

Why not run the collector when `fopen` fails with `EMFILE` and then retry? A
native function may be running in the middle of an expression whose values
are not yet on any root (an array literal being built, for example).
Collecting there could free them, so natives never start a collection.

## 11. Constants

A mixed module's constants are written in its `.pp` file. When a value depends
on the platform (or anything else only C knows), a **private** native function
computes it once, while the module loads:

```
// lib/fs.pp
native "name_separator" function name_separator() -> string
native "list_separator" function list_separator() -> string
export const separator = name_separator()        // "\" on Windows, "/" elsewhere
export const path_separator = list_separator()   // ";" on Windows, ":" elsewhere
```

```c
/* src/modules/fs.c */
static Value fs_name_separator(VM* vm, int argc, Value* a) { return obj_val(str_cnew(vm, PP_SEP_STR)); }
static Value fs_list_separator(VM* vm, int argc, Value* a) { return obj_val(str_cnew(vm, PP_LIST_SEP_STR)); }
```

The helpers are not exported, so users see only `fs:separator` and
`fs:path_separator`. The constants cannot be assigned:
`fs:separator = "x"` is an error.

A **pure native** module (no `.pp` file) defines constants directly in its
init function. `src/modules/os.c` does this for `os:platform`:

```c
def_const_str(vm, m->env, "platform", PP_PLATFORM);   /* a string constant */
def_const(vm, m->env, "max_int", int_val(INT64_MAX)); /* any Value */
```

## 12. Errors

| situation | use | example |
|-----------|-----|---------|
| the outside world said no: missing file, no permission, disk full | return an `Error` value | `fail_errno(vm, "open", path)` → `fail("cannot open 'x': No such file or directory")` |
| the program used the module wrongly | `rt_error(vm, 0, fmt, ...)`: prints `file:line: error: ...` with a traceback and exits; it does not return | reading a closed stream, an offset outside the buffer |
| a simple query on something missing | answer like Java does | `exists()` false, `size()` 0, `delete()` false |

Line `0` means the line of the current call. Build `errno` messages right
away, because other library calls (`fclose`, `free`) may change `errno`:

```c
static Value fail_errno(VM* vm, const char* what, const char* path) {
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "cannot %s '%s': %s", what, path, strerror(errno));
    Value msg = obj_val(str_new(vm, sb.data, (int)sb.len));
    sb_free(&sb);
    return make_fail(vm, msg);
}
```

## 13. Testing

Each `tests/<name>.pp` is a test. Its standard output must equal
`tests/<name>.out`. When `tests/<name>.err` exists, the script must fail
instead, and its error output must contain that text. Run them all with
`ctest` in the build directory.

* `tests/fs.pp` works in a scratch folder next to itself
  (`fs:directory(pp:dir(), "fs_test_tmp")`) and deletes it at the end, so it
  does not depend on the working directory. It prints names and booleans
  rather than absolute paths, so the output is the same on every machine.
* Error behaviour has small scripts of its own:
  * `err_fs_closed_stream.pp` reads after `close()`; its `.err` is `is closed`.
  * `err_fs_type_call.pp` calls `fs:File(...)`; its `.err` is
    `type 'File' cannot be called; make a value with fs:file(...)`.

## 14. Checklist

1. `lib/<name>.pp`: `native type`s, `native function` declarations with
   implementation names, constants, `export`s, doc comments.
2. `src/modules/<name>.c`: instance structs starting with `ObjInstance`, the
   hooks, the bodies, a `NativeClass` table and a `NativeImpl` table, each
   ending with an empty entry.
3. `mod_<name>_register`: `vm_register_native_types` and/or
   `vm_register_native_impls` with the library module name. Declare it in
   `src/vm.h` and call it from `vm_new` in `src/interp.c`.
4. Instances own plain C memory only, and their `free` hook releases it. A
   resource (a file, a socket) gets an explicit `close`, and `free` closes it
   only when the program never did.
5. Types are PascalCase and not callable. Constructor functions are snake_case
   or flatcase.
6. Outside failures are `Error` values. Misuse is `rt_error`.
7. A test script with its `.out`, and an `err_*` script for each error you
   promise.
8. A section in [PACKAGES.md](PACKAGES.md) for users.
