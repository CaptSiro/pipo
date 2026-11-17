# PiPo

A small statically typed language (`.pp` files) in the spirit of
JavaScript / Python / Lua, with type-based overloading, Rust-like enums and
`match`, and functions bound to datatypes through their first parameter.
Every expression has a static type, checked before the program runs; the one
dynamic type is `unknown`, opened with `as`. Programs run in an interpreter,
compile to bytecode for the PC, or compile to WebAssembly for the web.

```
import io

type Person = { string name, number age }

function greet(Person p) -> string {
    return "Hello, " + p:name
}

const Person john = { name: "John Pork", age: 22 }
io:print(john.greet())

<T>function +(T[] a, T[] b) -> T[] { return a.concat(b) }
io:print([1] + [2, 3])
```

* Static typing, `unknown`, `as`, `type_of` / `get_type`, and why everything can have a type: [md/STATIC_TYPING.md](md/STATIC_TYPING.md)
* Targets (`--target=interp|check|bytecode|wasm`): [md/TARGETS.md](md/TARGETS.md)
* Web pages in PiPo (a design draft: markup, styles, reactivity): [md/WEB.md](md/WEB.md)
* Coding style: [md/CODING_STYLE.md](md/CODING_STYLE.md)
* Build and implementation notes: [md/IMPLEMENTATION.md](md/IMPLEMENTATION.md)
* Language reference: [md/LANGUAGE.md](md/LANGUAGE.md)
* Datatypes, bound functions, interfaces: [md/DATATYPES_AND_FUNCTIONS.md](md/DATATYPES_AND_FUNCTIONS.md)
* Internal functions: [md/BUILTINS.md](md/BUILTINS.md)
* Syntax (grammar): [md/SYNTAX.md](md/SYNTAX.md)
* Modules, `import` / `export`, the `io` module: [md/PACKAGES.md](md/PACKAGES.md)
* Calling DLLs with `extern` (draft; GLFW + OpenGL work): [md/EXTERN.md](md/EXTERN.md)
* Best practices for `.pp` code: [md/BEST_PRACTICES.md](md/BEST_PRACTICES.md)
* Editor support (CLion / JetBrains plugin, TextMate grammar): [md/SYNTAX_HIGHLIGHTING.md](md/SYNTAX_HIGHLIGHTING.md)
* Original idea: [md/PROPOSAL.md](md/PROPOSAL.md)
* Draft: extensible syntax from `.pp` files (brackets, lazy parameters, macros): [md/PROPOSAL_SYNTAX.md](md/PROPOSAL_SYNTAX.md)
* Superseded draft: replacing `x[i]` with an `@` operator: [md/PROPOSAL_AT.md](md/PROPOSAL_AT.md)

Quick start (Windows, CLion toolchain): see [md/IMPLEMENTATION.md](md/IMPLEMENTATION.md), then

```
build\pp.exe examples\proposal.pp                    # check, then interpret
build\pp.exe --target=bytecode examples\proposal.pp  # check, compile, run on the bytecode VM
build\pp.exe --target=wasm tests\wasm\basics.pp     # basics.wasm + basics.mjs + basics.html
build\pp.exe            # REPL
```
