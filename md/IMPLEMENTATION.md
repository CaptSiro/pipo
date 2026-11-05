# Implementation Notes

A tree-walking interpreter in C11 with no dependencies.

## Building on Windows

With CLion, open the folder; it picks up `CMakeLists.txt`. From a terminal,
the MinGW toolchain bundled with CLion works:

```powershell
$clion = "$env:LOCALAPPDATA\Programs\CLion\bin"
$env:PATH = "$clion\mingw\bin;$clion\cmake\win\x64\bin;$clion\ninja\win\x64;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\pp.exe examples\proposal.pp
cd build; ctest --output-on-failure
```

MSVC also works (`cmake -S . -B build` from a Developer prompt). The build
raises the stack size to 64 MB for deep recursion, enables C99 `printf` on
MinGW and links statically, so `pp.exe` runs without MinGW DLLs.
Windows-specific code (UTF-8 console, `_fullpath`, `GetModuleFileNameA`,
`QueryPerformanceCounter`) sits behind `#ifdef _WIN32`, with POSIX fallbacks.

## Source layout

| file | role |
|------|------|
| `src/lexer.c` | tokens, newline/space flags, operator-run splitting |
| `src/parser.c` | recursive descent parser to AST (`src/ast.h`), types, patterns, native signature parser |
| `src/interp.c` | evaluator: scopes, calls, overload resolution, match, modules, entry points |
| `src/types.c` | runtime type matching and scoring, type-vs-type checks for callbacks and interfaces, type printing |
| `src/value.c` | values, objects, hash tables, equality, formatting |
| `src/collections.c` | tuples, value hashing, the hash table behind `std:Map` / `std:Set` and their constructors |
| `src/gc.c` | mark & sweep collector |
| `src/builtins.c` | internal (global) functions |
| `src/modules/io.c` | the native `io` module |
| `src/modules/os.c` | the native `os` module |
| `src/modules/fs.c` | C half of `lib/fs.pp`: `File`, `Directory`, `InputStream`, `OutputStream`; the worked example in [NATIVE_MODULES.md](NATIVE_MODULES.md) |
| `src/modules/types.c` | C half of `lib/types.pp`: the typed arrays (`Int8Array` ... `Float64Array`) |
| `src/modules/pp.c` | the native `pp` module: location of the running script |
| `src/modules/std.c` | C half of `lib/std.pp`: `in(number, range)`, `has(range, number)`, the `std:Map` / `std:Set` functions and constructors |
| `src/prelude.c` | prelude written in the language (`number`, `Option`, `Error`, helpers) |
| `src/main.c` | CLI and REPL |

## Key design points

* **Values** are a tagged union (`void`, bool, int64, double, object pointer).
  Heap objects share an `Obj` header and are linked into one list for the GC.
* **Scopes** are heap objects (`ObjEnv`) holding an insertion-ordered hash
  table. A block only gets its own scope if it declares something. Each loop
  iteration gets a fresh scope, so closures capture per-iteration variables.
* **Every function name is an overload set** (`ObjOverload`). Native
  functions have signatures parsed from the language's own syntax, so they
  are resolved by the same scorer as user functions (`score_candidate` in
  `interp.c`, `type_match` in `types.c`).
* **Inline caches**: call sites remember the chosen candidate for argument
  kinds made only of primitives and strings, which skips re-scoring in hot
  loops. Caches are flushed on every GC.
* **Operator chains** `a + b * c - d` are stored flat. The tree is built on
  first evaluation from the current precedence table and rebuilt when
  `precedence` changes it (a global version counter), which is how runtime
  precedence works.
* **Control flow** (`break`, `continue`, `return`) propagates as a status code
  from statement execution. Runtime errors `longjmp` to the entry point (or
  the REPL loop).
* **GC safety rule**: collection only happens at statement boundaries. Any
  value alive across a statement execution must be on the value stack
  (`vm_push`), on the scope stack, or reachable from a scope or module. All
  evaluation code follows this. Interned names and literals are permanent.

## Code style

These rules apply to the C sources and, where they make sense, to `.pp` code
too (`lib/`, `examples/`, the prelude in `src/prelude.c`): braces at the end
of the line, no one-line `if` / `for` / `while` bodies, no one-line function
bodies, and statements grouped with blank lines. In `.pp`, short `match` arms
(`some(x) -> { return x }`) and lambdas (`x -> { total += x }`) are
expressions and may stay on one line, like `case X: return y;` in a C
`switch`. The test scripts in `tests/` may stay compact, since some of them
test exactly that syntax, and the `.err` files depend on their line numbers.

### C

* Use `const` if possible: for local variables that are never reassigned
  (`const int n = ...`), and for pointers whose target is not modified through
  them (`const ObjString* s = ...`)
* Curly brackets at the end of a line and not on the next line:
```
// wrong
int f() 
{
    return 0;
}

// right
int f() {
    return 0;
}
```
* The same bracket rule applies to all statements that use brackets
* Do not use one-line if statements. For example, this is wrong: `if (condition) statement;`, correct:
```
if (condition) {
    statement;
}
```
* Do not use one-line function statements. For example, this is wrong: `int f() { return 0; }`, correct:
```
int f() {
    return 0;
}
```
* Group statements with the use of new lines
```
// wrong
static Value io_write_file(VM* vm, int argc, Value* a) {
    const char* path = AS_STR(a[0])->chars;
    ObjString* content = AS_STR(a[1]);
    FILE* f = fopen(path, "wb");
    if (!f) return fail_errno(vm, "open for writing", path);
    size_t n = fwrite(content->chars, 1, (size_t)content->len, f);
    bool ok = n == (size_t)content->len && fclose(f) == 0;
    if (!ok) return fail_errno(vm, "write", path);
    return make_success(vm, int_val((int64_t)n));
}

// correct
static Value io_write_file(VM* vm, int argc, Value* a) {
    const char* path = AS_STR(a[0])->chars;
    const ObjString* content = AS_STR(a[1]);
    
    FILE* f = fopen(path, "wb");
    if (!f) {
        return fail_errno(vm, "open for writing", path);
    }
    
    size_t n = fwrite(content->chars, 1, (size_t)content->len, f);
    bool ok = n == (size_t)content->len && fclose(f) == 0;
    if (!ok) {
        return fail_errno(vm, "write", path);
    }
    
    return make_success(vm, int_val((int64_t)n));
}
```

## Known limitations / next steps

* Performance is roughly CPython-class for calls (`fib(27)` ~0.3 s). A
  bytecode compiler with resolved variable slots would be the next big step,
  and would also help a future compile-to-web target.
* No `try`/catch. No string interpolation. No destructuring `let`.
* Interfaces have no generic bounds yet; see
  [DATATYPES_AND_FUNCTIONS.md](DATATYPES_AND_FUNCTIONS.md#interfaces).
* A lambda gets the types of its callback type by being copied with a new
  signature (`adopt_callback_types` in `types.c`). The copied types are
  resolved once ("concrete" types, `Type.direct`) and deduplicated in
  `vm->ctypes`, so passing lambdas in a loop allocates no new types.
* Operators declared only in another module must be written with spaces
  around them when adjacent to other operator characters.
