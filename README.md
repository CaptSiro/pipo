# PiPo

A small dynamically checked language (`.pp` files) in the spirit of
JavaScript / Python / Lua, with type-based overloading, Rust-like enums and
`match`, and functions bound to datatypes through their first parameter.

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

* Build and implementation notes: [md/IMPLEMENTATION.md](md/IMPLEMENTATION.md)
* Language reference: [md/LANGUAGE.md](md/LANGUAGE.md)
* Datatypes, bound functions, interfaces: [md/DATATYPES_AND_FUNCTIONS.md](md/DATATYPES_AND_FUNCTIONS.md)
* Internal functions: [md/BUILTINS.md](md/BUILTINS.md)
* Syntax (grammar): [md/SYNTAX.md](md/SYNTAX.md)
* Modules, `import` / `export`, the `io` module: [md/PACKAGES.md](md/PACKAGES.md)
* Syntax highlighting for CLion: [md/SYNTAX_HIGHLIGHTING.md](md/SYNTAX_HIGHLIGHTING.md)
* Original idea: [md/PROPOSAL.md](md/PROPOSAL.md)

Quick start (Windows, CLion toolchain): see [md/IMPLEMENTATION.md](md/IMPLEMENTATION.md), then

```
build\pp.exe examples\proposal.pp
build\pp.exe            # REPL
```
