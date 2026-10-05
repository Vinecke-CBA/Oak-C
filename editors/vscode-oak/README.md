# Oak for Visual Studio Code

Syntax highlighting and editor behaviour for `.oak` files. Pure static
JSON — no build step, no npm, no `node_modules`.

```
vscode-oak/
├── package.json                  extension manifest (language + grammar)
├── language-configuration.json   comments, brackets, indent rules, folding
├── syntaxes/oak.tmLanguage.json  the TextMate grammar
└── tasks.example.json            copy to .vscode/tasks.json to build files
```

## What you get

**Highlighting** for everything the language actually has:

| Construct | Highlighted as |
| --- | --- |
| `fn`, `struct`, `var`, `if`, `else`, `while`, `for`, `in`, `return`, `extern`, `defer` | keyword |
| `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `f64`, `bool`, `string`, `ptr` | type |
| `ptr(…)`, `defer(…)` before `(` | pointer operator (distinct from plain `ptr` type use) |
| `C` in `C "unsigned long long"` | storage modifier (the string stays a string) |
| `true`, `false`, `null` | constant |
| `print`, `len`, `push` before `(` | builtin function |
| function / struct / `var` names after the keyword | entity / variable name |
| call names like `foo(` (not a keyword or builtin) | function name |
| `name:` in params, struct fields and struct literals | property |
| `import "x.oak"`, `include "x.h"`, `include "x.c" as extern C`, `include "x.oak" as Oak` | keyword + path string + `as` clause |
| `"…"` with `\` escapes | string |
| `0xFF`, `0b1010`, `1_000_000`, `1.5`, `42` | hex / binary / decimal / float numbers |
| `..` ranges, `...` variadics, `->`, comparisons, logic | operator / range |
| `//` comments | comment |

**Editor behaviour** (from `language-configuration.json`): `//` line-comment
toggling, bracket matching, auto-closing and surrounding pairs, 4-space
indent, indent rules that also continue after a trailing `,` (Oak's
multi-line call style), identifier-aware double-click selection, and
`// region` / `// endregion` folding.

**Build task** (`tasks.example.json`): Ctrl+Shift+B compiles the current
file with `oakc`. It uses `"problemMatcher": "$gcc"`, which also parses
Oak's own `file:line:col: error:` messages — so compiler errors show up as
red squiggles in the editor.

## Install (no npm needed)

Copy the folder into your extensions directory and restart VS Code:

```powershell
Copy-Item -Recurse -Force <repo>\editors\vscode-oak "$env:USERPROFILE\.vscode\extensions\oak-0.2.0"
```

Then open any `.oak` file — the language is picked up from the `.oak`
extension automatically. Check it took with `code --list-extensions | findstr oak`.

Optional, if you want a real `.vsix` (needs Node):

```sh
npx @vscode/vsce package     # -> oak-0.2.0.vsix
code --install-extension oak-0.2.0.vsix
```

## Build the file you're editing

Copy `tasks.example.json` to `.vscode/tasks.json` in your project (or in
the Oak repo) and press Ctrl+Shift+B. Adjust `command` if `oakc` isn't on
your `PATH`.

## Notes / limits

- **No language server** — no autocomplete, go-to-definition or inline
  errors. Those need a real LSP; this is highlighting plus editor
  ergonomics, which is why the whole thing is four JSON files.
- String **escapes are not coloured separately**: the grammar matches a
  whole string with one pattern instead of a begin/end state machine, so
  a stray unterminated `"` can't run the highlight to end-of-file. If you'd
  rather have escape colouring, swap `#strings` for a `begin`/`end` pair.
- **Numbers are matched honestly**: `1.5` and `42` highlight, but `1e5` is
  not valid Oak (the lexer has no exponent support), so the grammar doesn't
  pretend otherwise.
- The `publisher` in `package.json` is `Vinecke-CBA`; change it if you
  publish under a different account, since it forms the extension id.
- After editing the grammar: **Developer: Reload Window**.
