# Oak Documentation

Complete documentation for the Oak programming language and its compiler.
Oak is a small statically-typed language that compiles to C and then to a
native binary through `gcc -O2`. There is no interpreter and no runtime —
the compiler is ~4,000 lines of C99 and the output is readable C.

## Reading order

| Doc | Audience | What you get |
| --- | --- | --- |
| [LANGUAGE.md](LANGUAGE.md) | Everyone writing Oak | Full language reference: every type, statement, expression, operator, builtin, grammar production, error message and known limitation |
| [C-INTEROP.md](C-INTEROP.md) | Anyone binding a C library | Step-by-step FFI guide: type mapping, `extern fn`, `include`, varargs, linking flags, the shim pattern, worked SDL2/raylib examples, debugging |
| [INTERNALS.md](INTERNALS.md) | Compiler contributors | How every phase works under the driver, lexer, parser, typechecker, alias analysis and code generator — plus recipes for changing the language |

Suggested path:

1. Skim this page, build the compiler, run an example.
2. Read **LANGUAGE.md** top to bottom (it doubles as a tutorial).
3. When you need libc/SDL/raylib, read **C-INTEROP.md**.
4. To hack on Oak itself, read **INTERNALS.md** before touching `src/`.

## Build and run

```sh
gcc -std=c99 -Wall -Wextra -O2 -o oakc src/oak.c src/lex.c src/parse.c \
    src/typecheck.c src/alias.c src/codegen.c src/main.c
# or: make / make test   (Makefile + examples/)

oakc examples/hello.oak -o hello && ./hello
```

Requires a C99 compiler on `PATH` — `gcc` by default, or `clang`/`cc`, or the
bundled TinyCC dropped into `dependencies/tcc/` (see `--cc` below). Works on
Linux and Windows (MinGW/MSYS2 tested).

## Compiler CLI

```
oakc <file.oak> [-o binary] [--cc name|path] [--config file]
     [--emit-c file.c] [--keep-c] [--verbose] [-- <cc flags...>]
```

- `-o binary` — output executable (default `a.exe` on Windows, `a.out` elsewhere)
- `--cc name|path` — C compiler to drive: `gcc` (default), `tcc`, `clang`,
  `cc`, or a path. `tcc` resolves to `dependencies/tcc/tcc.exe` when present,
  else `tcc` on `PATH`. A bare `clang` on Windows needs its own sysroot, which
  can be passed after `--`:
  `--cc clang -- -target x86_64-w64-windows-gnu --sysroot=C:/msys64/mingw64`
- `--config file` — read settings from `file` instead of `oak.cfg`
- `--emit-c file.c` — write the generated C to a chosen file and keep it
- `--keep-c` — keep the temporary `<input>.oak.c`
- `--verbose` — print the resolved config, compiler and full command line
- `-l -L -I -D -O -W -f -m -std...` — recognized *before* the input file and
  forwarded to the compiler invocation that links the program
- `--` — everything after it is forwarded to the compiler verbatim (link libs,
  extra `.c` files for shims, include paths)
- `-I<dir>` is *also* an Oak include path: `include`/`import` names that
  aren't found next to the file or in the working directory are looked up
  there, and `include/` (next to the input file, the working directory, or
  the `oakc` executable) is searched the same way without being asked

The compiler pipeline is: **parse (recursively, per file) → typecheck →
alias-check → emit C → invoke the C compiler**. It stops with a non-zero exit
code at the first phase that produced errors.

## oak.cfg

Optional project settings, `key = value` per line (`#`/`;` comments). Found
next to the input file, else in the working directory; `--config` overrides
discovery and command-line flags override the file.

| Key | Meaning |
| --- | --- |
| `cc` | compiler name or path (default `gcc`) |
| `std` | C standard → `-std=<value>`; `-` omits the flag |
| `opt` | optimization level → `-O<value>`; `-` omits the flag |
| `out` | default output path when `-o` is not given |
| `keep_c` | `true` to keep the generated C |
| `flags` | extra flags appended to every compile/link line |

## Repository layout

```
oak/
├── src/            compiler (C99, single binary `oakc`)
│   ├── oak.h       all shared types: Token, Type, Expr, Stmt, Comp, Lexer
│   ├── oak.c       memory (arena, interning), type helpers, errors
│   ├── lex.c       lexer
│   ├── parse.c     recursive-descent parser -> AST
│   ├── typecheck.c type resolution, scoping, semantic checks
│   ├── alias.c     struct-argument aliasing rule
│   ├── codegen.c   C emitter + runtime helpers
│   └── main.c      CLI driver, oak.cfg + compiler selection, imports
├── examples/       hello/bump/copy_ok/control_flow/strings/features
│                   for_loops/include_dir/include_extern/struct_arrays
│                   plus raylib/ sdl2/ (shim + wrapper + demo) and bench/
├── include/        shared C headers/sources and Oak modules; searched for
│                   include/import, passed to gcc as -Iinclude
├── editors/        vscode-oak/ a ready-made VS Code extension (highlighting)
├── dependencies/   optional drop-in tools (dependencies/tcc/), gitignored
├── oak.cfg         project defaults: cc / std / opt / out / keep_c / flags
├── docs/           this documentation
├── Makefile        build / clean / test / test-tcc
└── README.md       quick-start + five minute tour
```

## Status at a glance

**Works today:** i32/f64/bool/string/ptr, structs, dynamic arrays with
bounds-checked indexing and negative indices, string indexing and slicing,
`for` loops over inclusive ranges, arrays and destructured fields,
functions, multi-file `import`, C FFI (`include`, `extern fn`, raw C types,
varargs, `include "x.c" as extern C` / `as Oak`), a shared `include/`
search folder, `print`/`len`/`push` builtins, one aliasing rule for struct
arguments. See README.md for the tour and `examples/raylib/` for a full
wrapper built entirely on public FFI.

**Not in the language:** bitwise operators, generics, error values,
concurrency, destructors/GC (programs leak on purpose), namespaces (imports
share one flat namespace).
