# Coding Style

The rules for the C sources in `src/` and, where they make sense, for `.pp`
code (`lib/`, `examples/`, the prelude in `src/prelude.c`).

They come from two places:

* the rules written down for this project (they used to live in
  [IMPLEMENTATION.md](IMPLEMENTATION.md#code-style)), and
* habits that are the same in every one of the older C projects in
  `C:\programming\c` (`aurum`, `wobbuffet`, `latios`, `vulpix`, `spheal`).

Where the two disagree, the rule written for this project wins. Those cases
are listed at the end, in [Deviations from the older projects](#deviations-from-the-older-projects).

`tools/style_check.py` checks the rules a script can check. Run it before a
commit:

```
python tools/style_check.py
```



## Layout

* Indent with 4 spaces. No tabs.
* Curly brackets go at the end of the line, never on the next one. This is
  for functions, `if`, `else`, loops, `switch`, `struct`, `enum` and
  initialisers alike:

```c
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

* Every `if`, `else`, `for`, `while` and `do` has a block, even for one
  statement:

```c
// wrong
if (condition) statement;

// right
if (condition) {
    statement;
}
```

* No function body on one line:

```c
// wrong
int f() { return 0; }

// right
int f() {
    return 0;
}
```

* A `switch` case that is a single `return` / jump may stay on one line,
  `case PRIM_ANY: return 1;`. A case with more statements, or one that
  declares a variable, gets its own block: `case OP_ADD: { ... break; }`.



## Grouping with blank lines

* Statements that belong together form a group. Groups are separated by one
  blank line: the declarations that set something up, the check that follows
  them, the work, the result.
* A `return` that follows other statements of its block is separated from
  them by a blank line. A comment that belongs to the `return` stays directly
  above it, and the blank line goes above the comment.

```c
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

// right
static Value io_write_file(VM* vm, int argc, Value* a) {
    const char* path = AS_STR(a[0])->chars;
    const ObjString* content = AS_STR(a[1]);

    FILE* f = fopen(path, "wb");
    if (!f) {
        return fail_errno(vm, "open for writing", path);
    }

    const size_t n = fwrite(content->chars, 1, (size_t)content->len, f);
    const bool ok = n == (size_t)content->len && fclose(f) == 0;
    if (!ok) {
        return fail_errno(vm, "write", path);
    }

    return make_success(vm, int_val((int64_t)n));
}
```

* Top-level sections of a file are separated by **three** blank lines: the
  include block, then the declarations (macros, types, prototypes), then the
  definitions. In a header the same goes for the end: three blank lines
  before the closing `#endif`.

```c
#ifndef PP_BYTECODE_H
#define PP_BYTECODE_H

#include "vm.h"



typedef enum {
    OP_CONST,
    OP_ADD_INT,
} OpCode;

void chunk_init(Chunk* chunk);



#endif // PP_BYTECODE_H
```



## Naming

| what | style | example |
|------|-------|---------|
| functions, variables, parameters, fields | `snake_case` | `vm_run_file`, `next_gc` |
| functions of a "module" or a type | prefixed with it | `sb_append`, `table_find`, `chunk_write` |
| types (`struct`, `enum`, `typedef`) | `PascalCase` | `ObjString`, `InlineCache` |
| enum constants | `UPPER_SNAKE_CASE` with a common prefix | `OBJ_STRING`, `TY_ARRAY`, `OP_ADD_INT` |
| macros and their parameters | `UPPER_SNAKE_CASE` | `GROW(ARR, COUNT, CAP)` |
| header guards | `PP_<FILE>_H`, repeated after `#endif` | `#endif // PP_VM_H` |

* Objects owned by the garbage collector are made with `<type>_new` and never
  freed by hand. A struct that lives inside another one (or on the stack) is set
  up with `<type>_init` and released with `<type>_free` (`sb_init`, `sb_free`).



## Pointers and `const`

* The `*` sits on the type: `const char* name`, `VM* vm`.
* Use `const` whenever possible: for local variables that are never assigned
  again (`const int n = ...`), and for pointers whose target is not changed
  through them (`const ObjString* s = ...`).



## Loops

* A counting loop uses the prefix increment: `for (int i = 0; i < n; ++i)`.



## Comments

* Comments are `//` comments, also when they run over several lines.
* `/* */` is only used where `//` cannot be: inside a line, between code
  (`f(a /* the base */, b)`), and inside a macro whose lines end with `\`.
* A comment says why, or what a declaration means. Field comments sit at the
  end of the field's line.



## `.pp` code

The layout rules apply to `.pp` code as well: brackets at the end of the line,
no one-line `if` / `for` / `while` bodies, no one-line function bodies, and
statements grouped with blank lines. Short `match` arms
(`some(x) -> { return x }`) and lambdas (`x -> { total += x }`) are expressions
and may stay on one line, like `case X: return y;` in a C `switch`.

The test scripts in `tests/` may stay compact, since some of them test exactly
that syntax, and the `.err` files depend on their line numbers.



## Deviations from the older projects

The older projects agree on a few things this project does differently, on
purpose:

| older projects | this project | why |
|----------------|--------------|-----|
| `snake_case_t` types, `snake_case_e` enums | `PascalCase` types | the project's own rules and every type in it use PascalCase; it keeps types apart from functions in a code base this large |
| `type *name` | `Type* name` | written down as a rule for this project (see the example above) |
| `// Created by ...` file headers (CLion's template) | none | git records it |
| `_create` / `_free(T**)` that sets the pointer to `NULL` | `_new` for GC objects, `_init` / `_free(T*)` for embedded structs | GC objects are never freed by hand |

Everything else above is shared by both: K&R brackets, blocks everywhere,
the blank line before `return`, three blank lines between sections, `//`
comments, `++i`, upper case macros and macro parameters, prefixed enum
constants, `PROJECT_FILE_H` guards with the name repeated after `#endif`.
