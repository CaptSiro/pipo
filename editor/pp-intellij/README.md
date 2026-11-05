# PiPo plugin for CLion and other JetBrains IDEs

A native IntelliJ Platform plugin for `.pp` files. Unlike the TextMate bundle in
`../pp-textmate`, it parses the code, so it knows what each name refers to.

## What it does

| feature | notes |
|---|---|
| Highlighting | Keywords, literals, escapes, `format` specifiers. Semantic colors for declarations, calls, types, interfaces, type parameters, parameters, locals, top-level constants, fields, enum variants and modules. Configurable in *Settings, Editor, Color Scheme, PiPo*. |
| Errors while typing | Syntax errors from a port of `src/parser.c`, with the interpreter's messages. Also the checks it makes after parsing: imports first and exports last, declarations without a definition, untyped lambdas in variables, bad escapes, literals that are too large. |
| Unresolved names | The *PiPo, Unresolved reference* inspection covers names, types, modules, module members and enum variants. Object fields and dot calls are not checked, since only running the code can tell. |
| Navigation | Go to declaration, find usages, highlight usages and rename. Resolution follows the interpreter's scoping, overload sets included, and works across `import "./file"`, `import name` (from `lib/`), submodules, `-> { a, b }`, re-exports, `module:symbol`, `Enum:VARIANT`, record fields and object keys. Built-in functions and the native modules `io`, `os` and `pp` resolve to bundled read-only stubs. |
| Completion | Names in scope with every overload, module exports after `mod:`, variants after `Enum:`, fields after `value:`, types in type positions, module names after `import`, file paths in `import "./..."`, and keywords. |
| Documentation | *Ctrl+Q* and *Ctrl+hover* show the signature and the `//` lines written directly above a declaration. Every overload of a call is listed. |
| Parameter info | *Ctrl+P* lists every overload. In `recv.f(...)` the receiver counts as the first parameter. |
| Structure | Structure view, breadcrumbs, *Navigate, Symbol*, folding of blocks, literals, comments and imports, and TODO comments. |
| Editing | Brace matching, auto-closing quotes, *Ctrl+/* comments, and live templates (`fn`, `fnr`, `gfn`, `for`, `fori`, `while`, `if`, `ife`, `iflet`, `match`, `enum`, `rec`, `cb`, `iface`, `pr`). |
| Formatter | Re-indents code and puts spaces around `,` `=` `->` and before a block's `{`. It never joins or splits lines and never touches the spaces that change meaning (`f[x]` vs `f [x]`). The tests check this on every file of the repository. |
| Running | A *PiPo* run configuration (right-click a file, *Run*, or use the gutter icon of a script). It uses the project's `pp`: the newest `build/pp.exe` or `cmake-build-*/pp.exe`, or `pp` on the PATH, or the one set in *Settings, Languages & Frameworks, PiPo*. Error locations (`file.pp:12:5:`) in any console are clickable. |

## Install

1. Build it (below), or take `build/distributions/pp-intellij-<version>.zip`.
2. In CLion open *Settings, Plugins*, click the gear icon, choose *Install Plugin from Disk...*,
   select the zip and restart.
3. If you added the TextMate bundle before, remove it in *Settings, Editor, TextMate Bundles*.
   Otherwise both claim `*.pp`.

## Build

You need JDK 25 or newer. The JBR bundled with CLion works:

```
set JAVA_HOME=D:\JetBrains\CLion 2026.2.3\jbr      (PowerShell: $env:JAVA_HOME = "...")
gradlew buildPlugin          the zip lands in build/distributions/
gradlew test                 parses and highlights every .pp file of the repository
gradlew runClion             starts CLion with the plugin, in a separate sandbox
```

`gradle.properties` names the local IDEs it uses:

* `platformPath` is the IDE the plugin compiles and is tested against. It is IntelliJ IDEA,
  because CLion's backend services do not shut down cleanly inside the test framework.
  The plugin uses only platform APIs and runs in any JetBrains IDE of the 2026.2 line or newer.
* `clionPath` is the CLion that `runClion` starts.

## Tests

`gradlew test` runs four suites:

* `PipoParsingTest` parses every `.pp` file under `tests/`, `lib/` and `examples/`, plus the
  bundled stubs, and expects no syntax error. It also checks how the lexer splits operator runs.
* `PipoHighlightingTest` highlights the whole repository in one project, so imports resolve.
  It expects no error in any regular file and no unresolved name in `tests/` and `lib/`.
  For every `err_*` test it runs `build/pp.exe` and requires an error exactly where the
  interpreter reports a syntax error.
* `PipoFeaturesTest` covers resolve, completion, rename, find usages, formatting,
  documentation and the structure view on small examples.
* `PipoSafetyTest` checks that reformatting any repository file keeps its tree and line
  breaks, and that the lexer restarts consistently mid-file, as the editor does after an edit.

## Layout

```
src/main/java/com/pipo/lang/
  lexer/       PipoLexer: a port of src/lexer.c, operator splitting included
  parser/      PipoParser: a port of src/parser.c onto PsiBuilder, with error recovery
  psi/         PSI classes: declarations, references, helpers
  resolve/     scopes (PipoResolver), imports (PipoModules), bundled stubs (PipoBuiltins)
  highlight/   colors, annotator (semantic colors and checks), unresolved-reference inspection
  editor/      braces, comments, folding, find usages, documentation, parameter info, ...
  completion/  completion
  structure/   structure view
  format/      formatter and code style settings
  run/         run configuration, gutter icon, console links
  settings/    interpreter and library directories
src/main/resources/pipo/
  builtins.pp  signatures of the internal functions (src/builtins.c, src/prelude.c)
  modules/     the native modules without a .pp file: io, os, pp
```

When the interpreter gains a built-in function or a native module function, add its
signature to `builtins.pp` or `modules/*.pp`. Library modules written in `.pp` files
(`lib/std.pp`, `lib/fs.pp`, ...) need nothing: the plugin reads them directly.
