# Syntax Highlighting

A minimal TextMate grammar lives in `editor/pp-textmate/`:

```
editor/pp-textmate/
├── package.json                  registers the .pp extension
└── syntaxes/pp.tmLanguage.json   the grammar (scope: source.pp)
```

The same folder works in any editor that reads TextMate grammars or
VS Code-style extension folders: CLion and other JetBrains IDEs, VS Code,
Sublime Text.

## CLion / JetBrains IDEs

1. Open **Settings → Editor → TextMate Bundles** and click **+**.
2. Select the folder `editor/pp-textmate`.
3. If `.pp` files still open as plain text, look in **Settings → Editor →
   File Types**. If `*.pp` is listed under another file type, remove it there.

After you change the grammar, remove the bundle and add it again, or restart
the IDE. JetBrains caches TextMate bundles.

## What is highlighted

| scope | matches |
|-------|---------|
| `comment.*` | `// ...` and `/* ... */` |
| `string.*`, `constant.character.escape` | `"..."` and `'...'` with escapes |
| `constant.numeric` | `42`, `1_000`, `0xFF`, `0b101`, `3.14`, `1e9` |
| `constant.language` | `true`, `false` |
| `keyword.control` | `if else for in while break continue return match` |
| `keyword.other` | `import export function const let enum type precedence over under as` |
| `storage.type` | `int float string bool void any range object number` |
| `entity.name.type` | capitalised names: `Person`, `Error`, `Option` |
| `entity.name.function` | the name in `function name(` / `function +(` |
| `entity.name.function.call` | `name(` |
| `entity.name.namespace` | the module / object before `:`, as in `io:print` |
| `punctuation.accessor` | `:` and `.` before a name |
| `keyword.operator` | operator runs, `->`, `...`, `...=` |

Matching is purely by regular expressions, so contextual words such as
`type` or `as` are coloured as keywords even when used as variable names.
