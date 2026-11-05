# Editor Support

There are two ways to get `.pp` support in an editor:

| | `editor/pp-intellij/` | `editor/pp-textmate/` |
|---|---|---|
| what | a JetBrains plugin (CLion, IntelliJ IDEA, ...) | a TextMate grammar |
| editors | JetBrains IDEs 2026.2+ | JetBrains IDEs, VS Code, Sublime Text |
| highlighting | lexical and semantic (it knows declarations, calls, types, parameters, ...) | regular expressions only |
| errors while typing | yes, with the interpreter's messages | no |
| go to declaration, find usages, rename | yes, across imports | no |
| completion, documentation, parameter info | yes | no |
| run configuration, clickable error locations | yes | no |

Use only one of them in a JetBrains IDE: both claim `*.pp`.

## The JetBrains plugin

See [editor/pp-intellij/README.md](../editor/pp-intellij/README.md) for the full feature list,
how to build it and how it is tested. In short:

1. Build `editor/pp-intellij/build/distributions/pp-intellij-<version>.zip` with `gradlew buildPlugin`
   (JDK 25+, for example the JBR bundled with CLion).
2. *Settings, Plugins*: click the gear icon, choose *Install Plugin from Disk...*, select the zip, restart.
3. *Settings, Languages & Frameworks, PiPo* sets the interpreter (auto-detected from `build/` or
   `cmake-build-*/` otherwise) and extra library directories.

The plugin bundles signature stubs for the internal functions and for the native modules
`io`, `os` and `pp` (`src/main/resources/pipo/`). Keep them in step when functions are added in C.

## The TextMate grammar

A minimal TextMate grammar lives in `editor/pp-textmate/`:

```
editor/pp-textmate/
├── package.json                  registers the .pp extension
└── syntaxes/pp.tmLanguage.json   the grammar (scope: source.pp)
```

The same folder works in any editor that reads TextMate grammars or
VS Code-style extension folders: CLion and other JetBrains IDEs, VS Code,
Sublime Text.

### CLion / JetBrains IDEs

1. Open **Settings → Editor → TextMate Bundles** and click **+**.
2. Select the folder `editor/pp-textmate`.
3. If `.pp` files still open as plain text, look in **Settings → Editor →
   File Types**. If `*.pp` is listed under another file type, remove it there.

After you change the grammar, remove the bundle and add it again, or restart
the IDE. JetBrains caches TextMate bundles.

### What is highlighted

| scope | matches |
|-------|---------|
| `comment.*` | `// ...` and `/* ... */` |
| `string.*`, `constant.character.escape` | `"..."` and `'...'` with escapes |
| `constant.numeric` | `42`, `1_000`, `0xFF`, `0b101`, `3.14`, `1e9` |
| `constant.language` | `true`, `false` |
| `keyword.control` | `if else for in while break continue return match` |
| `keyword.other` | `import export global native function const let enum type interface impl precedence over under as` |
| `storage.type` | `int float string bool void any range object number` |
| `entity.name.type` | capitalised names: `Person`, `Error`, `Option` |
| `entity.name.function` | the name in `function name(` / `function +(` |
| `entity.name.function.call` | `name(` |
| `entity.name.namespace` | the module / object before `:`, as in `io:print` |
| `punctuation.accessor` | `:` and `.` before a name |
| `keyword.operator` | operator runs, `->`, `...`, `...=` |

Matching is purely by regular expressions, so contextual words such as
`type` or `as` are coloured as keywords even when used as variable names.
The plugin does not have this limitation.
