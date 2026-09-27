# Oak Internals — How the Compiler Works

A deep tour of the compiler for contributors: every phase, every data
structure, the invariants you must preserve, and concrete recipes for
changing the language. Companion to the source in `src/` (~4,000 lines of
C99, no dependencies beyond libc).

## Table of contents

1. [Big picture](#1-big-picture)
2. [The driver (`main.c`)](#2-the-driver-mainc)
3. [Memory & interning (`oak.c`)](#3-memory--interning-oakc)
4. [Lexer (`lex.c`)](#4-lexer-lexc)
5. [Parser (`parse.c`)](#5-parser-parsec)
6. [Typechecker (`typecheck.c`)](#6-typechecker-typecheckc)
7. [Alias analysis (`alias.c`)](#7-alias-analysis-aliasc)
8. [Code generator (`codegen.c`)](#8-code-generator-codegenc)
9. [Extension recipes](#9-extension-recipes)
10. [Sharp edges & testing](#10-sharp-edges--testing)

---

## 1. Big picture

```
 source.oak ──lex──► tokens ──parse──► AST (arena) ──typecheck──► typed AST
                                                                     │
 binary    ◄──gcc── C file ◄──codegen── typed AST ◄──aliascheck──────┘
```

One process, one binary (`oakc`), phases in `main.c` order:

1. **`parse_file`** (driver) — recursively loads the file and its `import`s,
   each through `lex_init` + `parse_program`. Produces one merged `Program`.
2. **`typecheck`** — resolves types, names, scopes; checks every semantic
   rule; annotates each `Expr` with its `Type` and each `var` with its
   symbol id.
3. **`aliascheck`** — the struct-argument rule (§7).
4. **`codegen`** — streams readable C to a `FILE*`.
5. **gcc** — invoked with `system()`; temp C removed unless asked to keep.

Each phase bails out (`exit 1`) if `c->errors != 0`. There is no IR:
**the AST is the IR** — typecheck mutates nodes in place, codegen reads
them. Any node invariant a later phase assumes must be established earlier
(that is what the recipes in §9 are about).

Key shared header: `src/oak.h` — `TokKind`, `Token`, `Type`/`TypeKind`,
`Expr`/`ExprKind`, `Stmt`/`StmtKind`, `Param`, `Field`, `StructDef`,
`FnDef`, `Program`, `Sym`, `Comp`, `Lexer`, plus prototypes. Read it first;
it is the whole architecture in one file.

**`Comp` is the universe**: filename/src/len for the current file, the
`Arena`, the intern table, error counter, the merged `Program prog`, the
symbol table (`syms/nsyms/scope/func_sym_base`), import bookkeeping
(`imports`, `loaded`), and `no_protos`. Every function takes `Comp *c`
(except pure helpers).

## 2. The driver (`main.c`)

### CLI parsing

Recognizes `-o`, `--emit-c`, `--keep-c`, `-h/--help`, and `--` (everything
after is `passthru`, pushed verbatim). Before the input file, dash-flags
matching `l L I D O W f m -std*` are also forwarded; anything else errors.
After input, dash-flags pass through too. Flags are collected with
`ptrlist_push` into `ccflags`.

The gcc line is assembled as:

```c
snprintf(cmd, ..., "gcc -std=c99 -O2 -o \"%s\" \"%s\"", out_bin, cpath);
/* then " %s" for each forwarded flag */
```

`cpath` = `--emit-c` value or `input + ".c"` (`foo.oak.c`), deleted after
success unless kept. `system()`'s nonzero status → `gcc failed`.

### Recursive import loading

```c
parse_file(c, path, referrer_dir)
  ├─ candidate = path exists ? path
  │            : referrer_dir ? join(referrer_dir, path) : error "cannot open"
  ├─ if candidate ∈ c->loaded → return          /* load-once, cycle-safe */
  ├─ mark loaded (ptrlist_push of the malloc'd candidate string)
  ├─ read file, build a TEMPORARY Comp `tmp`
  │     tmp.arena = c->arena ...                /* struct copies of grown state */
  │     tmp.prog  = c->prog; tmp.imports = ...; tmp.loaded = ...
  │     tmp.src/len/filename/errors = this file's
  ├─ lex_init(&lx, &tmp); parse_program(&lx)
  ├─ SYNCHRONIZE BACK: c->arena/interns/prog/imports/loaded/errors = tmp.* 
  ├─ snapshot c->imports[start..] = this file's new imports (copied out)
  ├─ restore c->src/len/filename; free(src buffer)
  └─ for each new import: parse_file(c, imp, dir_of(candidate))
```

**Why the temp `Comp`:** `parse_program` takes `Lexer*` (which points at
one `Comp`) and the lexer/AST must see *this file's* source/line numbers,
while the *program* state must accumulate across files. `Comp` is copied so
`l->c->src` is the current file, and every grown structure (arena bump
pointer, intern table, program arrays, import list, error count) is copied
back afterwards. **If you add any new mutable field to `Comp` that parsing
touches, you must copy it in and sync it back here** — see §10.

Path helpers: `dir_of` (last `/` or `\`, `"."` if none), `join_path`
(`dir + '/' + rel`), `file_exists` (fopen probe). `loaded` dedups by exact
string — paths are *not* canonicalized (documented quirk).

## 3. Memory & interning (`oak.c`)

**Allocator wrappers**: `xmalloc/xrealloc/xstrdup` — abort on OOM. Every
growable list in the compiler uses `ptrlist_push(arr, n, cap, p)`: doubles
capacity from 8, plain `malloc`'d pointer arrays. Lists that outlive their
phase (program arrays, `loaded`, `ccflags`) are intentionally never freed —
the compiler is a short-lived process.

**Arena** (`Arena { char *mem; size_t used, cap; }`): all AST nodes, types,
statements, `FnDef`/`StructDef`, interned strings. Starts at 64 KiB,
8-byte aligned bump allocation (`arena_alloc`), `arena_strndup` for text.
One arena per compiler run, freed at exit (`arena_free`), never individually
freed — that's what makes AST ownership trivial.

> ⚠ **Known landmine:** when the arena grows, `arena_alloc` mallocs a *new*
> buffer, copies, and `free`s the old one — **all previously returned
> pointers dangle**. In practice programs fit in 64 KiB of AST for small
> programs but *large programs can crash nondeterministically*. If you hit
> mysterious parse-time corruption on big files, this is why. The correct
> fix (when you tackle it): a chunked arena (linked list of blocks; never
> move old chunks) — ~20 lines in `arena_alloc/arena_free`, no other code
> changes since callers already treat pointers as stable.

**Interning** (`intern(c, s, n)`): linear scan of `c->interns[]`, else
arena-copy + append. Returns a **canonical pointer**; the whole compiler
compares identifiers with `==`, never `strcmp`
(`fns[i]->name == name`, symbol lookup, struct names, keyword recognition
in the lexer uses `strcmp` only because it compares against literals).

Consequences you must respect:

- Never build an identifier string by other means (`xstrdup`) and expect
  comparisons to work — always `intern`.
- The linear scan is O(#idents) — fine to ~10k identifiers; a hash table is
  a drop-in optimization inside `intern` only.
- `type_eq` for structs compares `struct_id`; for raw types it does
  `strcmp` on the C text (raw types are *not* interned).

**Errors** — `comp_error(c, line, col, fmt, ...)`: prints
`filename:line:col: error: ...` using **`c->filename`**, then increments
`c->errors`. It does **not** long-jump: phases keep going and may emit
cascades (the parser has rudimentary recovery: `expect` returns the wrong
token, top-level skips one token). `main.c` checks `c->errors` between
phases.

Type constructors (`type_i32()` etc.) return `Type` **by value** — a small
POD `{kind, struct_id, elem*, raw}`. Types are values, not pointers;
`TY_ARRAY` is the exception whose `elem` points into the arena (arrays of
arrays chain through arena pointers; `type_eq` recurses).

> ⚠ `type_name(c, t)` for arrays/raw returns a pointer to a **static
> buffer** — calling it twice in one `printf` for two arrays yields the
> same (last-written) text. Scalar names are string literals (safe). Keep
> that in mind when improving diagnostics.

## 4. Lexer (`lex.c`)

Hand-written scanner, **one token of lookahead** (`Lexer { c, pos, line,
col, tok }`); `lex_init` primes `tok` with the first token, `lex_next`
advances. The lexer reads the file from `c->src`/`c->len` — the driver
swaps those per file.

`lex_skip`: spaces/tabs/CR (col++), `\n` (line++, col=1), `//` comments to
EOL. No block comments, no line continuation.

`lex_next` dispatches on first char:

- **Identifiers/keywords**: `[A-Za-z_][A-Za-z0-9_]*` → `intern` →
  `strcmp`-chain maps 17 keywords (`fn struct var if else while return true
  false i32 f64 bool string ptr null extern import include`); unknown →
  `TOK_IDENT`. **Adding a keyword = one `else if` here + a `TokKind` in
  oak.h** (see §9) — and it's a breaking change for code using it as a
  variable.
- **Numbers**: digit run; optional `.` + digit run → `TOK_FLOAT` via
  `strtod` (no exponent scanning — `1e5` lexes as `1` then ident `e5`,
  which then fails in the parser); else `TOK_INT` via manual accumulate
  into `int64_t` (no overflow check — documented quirk).
- **Strings**: `"` … `"`, per-char escape processing into a malloc'd buf
  (`\n \t \r \\ \"`, unknown → error, EOF → unterminated), result
  `intern`ed as `TOK_STR.sval`. Identifiers and strings share the intern
  table — `sval`/`ident` pointers are canonical.
- **Operators/punctuation**: single/multi-char dispatch producing
  `TOK_ELLIPSIS` (`..` followed by `.` — note `..` alone is *not* valid),
  `->`, `==`, `!=`, `<=`, `>=`, `&&`, `||`, `.` `[` `]` `(` `)` `{` `}`
  `,` `:` `;` `+ - * / %`. Lone `&`/`|` error out (Oak has no bitwise ops
  or references). Everything else → `unexpected character` + resync.
- **EOF**: `TOK_EOF` (driver of `parse_program`'s loop).

Every token carries `line`/`col` captured *before* the scan (start
position). `Token` also has `ident/sval/ival/fval` union-ish fields (all
always zeroed by `make_tok`).

## 5. Parser (`parse.c`)

Recursive descent, ~600 lines. Node constructors `new_expr/new_stmt`
arena-allocate and zero-fill, set `kind`, `line`, `col`, `root_sym = -1`,
`type = type_unit()` (unit = "unchecked"; typecheck overwrites it).

**Helpers**: `at(l,k)` (peek), `take(l)` (advance, return old token),
`expect(l,k,what)` (error + still advance on mismatch — recovery),
`parse_expr/parse_block/parse_stmt` forward declarations.

### Top level (`parse_program`)

Loop until `TOK_EOF`, dispatch:

| Leading token | Action |
| --- | --- |
| `struct` | `parse_struct` → push to `p->structs` |
| `fn` | `parse_fn(l, false)` → push to `p->fns` |
| `extern` | `take`, `parse_fn(l, true)`, **overwrite `f->line/col` with the `extern` keyword position**, push |
| `import` | expects `STRING` `;` → push path onto **`l->c->imports`** (the Comp, not Program — the driver consumes & clears these) |
| `include` | expects `STRING` `;` → `add_include` (dedup by `strcmp` into `p->includes`) |
| other | `expected struct, fn, extern fn, import or include`, skip one token |

`parse_struct`: `struct` IDENT `{` then loop `IDENT` `:` `type` with an
*optional* `,` (so newline separation works) → `StructDef` with interned
name, arena `Field[]`.

`parse_fn(is_extern)`: `fn` IDENT `(` params `)` [`->` type] then body.
Params: `...` sets `is_vararg` and breaks (non-extern + vararg → error);
otherwise IDENT `:` type. Body: extern → `;`, else `parse_block`.
`f->is_main = !is_extern && strcmp(name.ident, "main") == 0`
(`is_extern`, `is_vararg` stored alongside `params/ret/body/line/col`).

### Ambiguity control: `g_no_struct_lit`

A bool (plus one stack discipline via `saved` copies). While parsing an
`if`/`while` **condition** it is set `true` so `ident {` does *not* start a
struct literal (the `{` must open the body). It is cleared again inside
`( ... )` (parenthesised expressions — `parse_expr` saves/toggles/restores),
call argument lists, struct-literal field values, and `[ ]` index/slice
expressions. That is the entire "Rust-like condition braces" rule; any new
context where `{` is ambiguous must join this save/clear/restore dance.

### Expressions

Precedence climbing: `parse_bin(l, prec)` with levels 1..6 (1=loosest;
`||` 1, `&&` 2, equality 3, relational 4, additive 5, multiplicative 6);
`parse_bin(l, 6)` tail-calls `parse_unary` (prefix `-`/`!`), which calls
`parse_postfix` via `parse_primary` + loop:

- `.IDENT` → `EX_FIELD`
- `[` → peek: if `:` right away, or an expr followed by `:` → build
  `EX_SLICE` with optional lo/hi; otherwise `EX_INDEX` (requires an expr)
- call args in `parse_primary` after an IDENT (`(` … `)`, comma-separated,
  optional trailing comma) → `EX_CALL`
- IDENT followed by `{` (when allowed) → `parse_struct_lit` → `EX_STRUCT`
  with `fnames` and `fvals` grown by `ptrlist_push`
- array literal `[e, e, ...]` → `EX_ARRAY`
- parens → inner expression **with `g_no_struct_lit` toggled off**
- literals: INT/FLOAT/`true`/`false`/`null`/STRING → matching `EX_*`

### Types (`parse_type`)

`parse_type_atom` + trailing `[` `]` loop (each wraps in an arena `Type`
and `type_array`). Atoms: keyword types → constructors; `IDENT` → **if the
name is `C` and a string follows → `type_raw(s.sval)`**, else
`named_unresolved(t.ident)`.

**The unresolved-type trick:** `named_unresolved` appends the name to a
file-global table `g_tnames[]` and returns `type_struct(-(id+1))` — a
**negative struct_id** encoding "struct named g_tnames[id], not yet
resolved". `unresolved_type_name(t)` decodes it. Typecheck's
`resolve_type` looks the name up (via interning) and rewrites the node to
the real non-negative `struct_id`. This two-phase design exists because
type annotations appear before the compiler knows which structs exist
(across files!). **All struct types before typecheck are negative ids;
codegen must never see one** — typecheck guarantees resolution (or errors).

### Statements (`parse_stmt`)

`var` IDENT [`:` type] `=` expr `;` (init mandatory); assignment: parse
expression, if `=` follows → `is_place()` check (`EX_VAR`, `EX_FIELD`
recursing, `EX_INDEX` — note parse accepts any index, *typecheck* rejects
string indexing); `if` / `while` (both wrap conditions with
`g_no_struct_lit`), `return` [expr] `;` (`s->ret.value` may be NULL),
`{` block (also reachable as a statement), else expression `;`.
Blocks: loop `parse_stmt` until `}` (blocks as `if`/`while` bodies go
through `parse_block` too).

## 6. Typechecker (`typecheck.c`)

Single pass over the merged program. State: the growing `c->syms[]` table,
`c->scope` (block depth), `c->func_sym_base` (where the current function's
symbols start), and a file-static `g_cur_fn` used by `return` checks.

**Pre-pass, before any body is checked:**

1. Duplicate struct names.
2. Resolve every field type (`resolve_type` rewrites unresolved negative
   ids in place, else `unknown type 'X'`); duplicate field names.
3. Recursive-struct check: DFS with a 3-colour state array
   (`struct_state`); a struct containing itself by value →
   `recursive struct 'X'` (Oak has no pointer-to-self, so this is the
   only cycle expressible).
4. Per function: `'X' is a builtin` (print/len/push), duplicate function
   names, resolve param/return types, **extern rules** (params and return
   must be `is_scalarish` — i32/f64/bool/string/ptr/raw — else
   `extern fn parameters must be scalars` / `extern fn cannot return T`),
   a non-unit return needs some `return` (`stmt_has_return`, a structural
   scan through if/else/while/blocks), `main` takes no arguments and
   returns nothing.
5. `missing fn main()` if there is none.
6. Then, for each non-extern function: set `func_sym_base`, `scope = 1`,
   add params (recording `is_struct_param` on each `Sym`), and
   `check_stmt(fn->body)`.

**Symbols.** `Sym { name, type, depth, is_struct_param }`; names are
interned so lookups compare with `==`. Scope handling is deliberately
simple: one flat array; lookups walk backwards, skipping entries with
`depth > scope` (no longer in scope) and stopping at the first
`depth < scope` (crossing a function boundary). Nothing is ever popped, so
dead symbols linger — which is exactly why the `root_sym` ids typecheck
stores on expressions stay unique for `alias.c` to compare.

**Expressions** — `check_expr` sets `e->root_sym = -1` first, then:

- Literals take their type; `null` is `TY_PTR`.
- `EX_VAR`: resolve or `unknown name 'X'`; sets `sym` and `root_sym`.
- `EX_FIELD`: base must be a struct; `root_sym` propagates from the base;
  else `no field 'f' on S`.
- `EX_STRUCT`: resolve `slit.struct_id`; per-field type match, plus
  `duplicate field` and `missing field 'f' in S literal`.
- `EX_INDEX`: index must be `i32`; array → element type, string →
  `string` (so `s[0] = "x"` is later rejected by `is_place`); else
  `cannot index T`.
- `EX_SLICE`: bounds must be `i32`, base must be a string
  (`slicing is only supported on strings`), result `string`.
- `EX_ARRAY`: elements unified with the annotation if there is one, else
  with the first element (`array elements must all be T, got U`); the
  empty literal is an error unless annotated (`cannot infer the element
  type of an empty array literal`).
- `EX_CALL`: the three builtins are special-cased (`print` → unit, `len`
  → i32, `push` → unit) with their own arity/type errors; otherwise
  `find_fn` (else `unknown function 'X'`), arity check
  (`'f' expects N argument(s), got M`, or "at least" for varargs),
  positional type match (`argument type mismatch: expected T, got U`),
  and extra vararg args must be scalarish.
- `EX_BIN`: ordered comparisons need two numbers or two strings
  (`cannot order X and Y`); equality needs identical types
  (`cannot compare X and Y`); `string + string` → `string`; `%` requires
  `i32`; `+ - * /` need numbers, promoting to `f64` if either side is.
- `EX_UNARY`: `!` requires `bool`, unary `-` requires a number.

**The int-literal trick.** `coerce_int_literal(e, want)` mutates the
*expression's* type from `i32` to `f64` when `f64` is expected. It is the
only implicit conversion in the language (never `f64`→`i32`), and codegen
depends on it: `ST_VAR` emits `ctype(init->type)`, so `var x: f64 = 1;`
becomes `double x = 1;` **only** because the check rewrote the literal's
type. New emitter code must keep using the expression type, not the
annotation.

**Statements**: `ST_VAR` resolves the annotation, hands an array
annotation to an `EX_ARRAY` initializer, checks the init type, rejects
`()` in a variable, then `add_sym` (duplicate → `duplicate name`);
`ST_ASSIGN` needs a place (`invalid assignment target`) and matching
types; `ST_IF`/`ST_WHILE` need `bool` conditions; `ST_RETURN` compares
against `g_cur_fn->ret` (bare `return` in a non-unit function →
`function must return T`); `ST_BLOCK` bumps and restores `scope`.

**What codegen may now assume** (typecheck's contract): every expression
has a final `type`; no negative struct ids remain; every call resolves to
a `FnDef` or is a builtin; every variable has a valid `sym`; every
assignment target is a place; extern calls involve only scalarish types.

## 7. Alias analysis (`alias.c`)

The smallest phase, and the source of the rule that surprises users most:
*`X cannot be used more than once in the same call`*.

Why it exists: struct arguments are passed **by pointer** (see §8), so
`f(a, a)` would make two parameters alias one object. Instead of tracking
aliasing in general, Oak forbids it at the call boundary.

- `aliascheck` walks every non-extern function body (`walk_stmt` →
  `walk_expr` — a plain recursive switch over the AST).
- At each `EX_CALL`: if the callee is the builtin `print`, all args are
  walked and no rule applies. Otherwise `find_fn` the callee; for each
  argument whose *parameter* type is a struct, take the argument's
  `root_sym` (set by typecheck for variables and field/index chains) and
  add it to a small growable `int` list. A repeat in the list ⇒ error.
  Arguments with `root_sym < 0` (literals, struct temporaries, call
  results) are exempt, as are struct parameters past the declared arity.
- No side effects; one small allocation per call node.

Workaround for code that needs it twice: copy first — `var b = a; f(a, b);`
copies the whole `typedef struct` by value.

## 8. Code generator (`codegen.c`)

A single streaming emitter, `CG { Comp *c; FILE *out; int indent; int tmp; }`.
No intermediate IR: every node is translated on the fly, and **expressions
are rendered as strings** — `rval()` returns a malloc'd C expression,
`place()` returns a C *lvalue* expression, `ind()` prints indentation.
(`strf` uses an 8 KiB static buffer, so absurdly large expressions could
truncate; `escape_c_string` re-escapes string bytes, with octal escapes
for non-ASCII.)

**Type mapping** — `ctype()`: `i32`→`int32_t`, `f64`→`double`,
`bool`→`bool`, `string`→`OakStr`, `ptr`→`void *`, `T[]`→`OakArr *`,
struct→its name, `()`→`void`, raw→its text verbatim.

**Emission order** in `codegen()`:

1. Fixed preamble (`stdio.h`, `stdint.h`, `stdbool.h`, `stdlib.h`,
   `string.h`, `limits.h`).
2. Every `include "..."`, as `#include <x.h>` for a bare name (no `/` or
   `\`, not starting with `.`) and `#include "x.h"` otherwise — deduped by
   the parser in source order across all files.
3. `emit_runtime` / `emit_string_runtime`: the C helpers, emitted **only
   if used** (`prog_flags` bit-set: `OakStr` typedef, `oak_alloc`,
   `oak_idx` (negative-index + bounds checking, exits on violation),
   `OakArr` typedef plus `oak_arr_new/_from/_push`, `oak_arr_slot/_len`,
   and `oak_str_concat/_eq/_cmp/_len/_at/_slice`).
4. `emit_structs`: each Oak struct as
   `typedef struct { ... } Name;`, emitted in dependency order (repeat
   passes until no struct is ready — recursion is already rejected in
   typecheck, so the loop always finishes).
5. All prototypes: `extern fn`s become `extern <ret> name(<params>);`
   (with `...` for varargs); ordinary functions get
   `static <ret> name(<params>);` forward declarations so calls in any
   order compile. `main` is emitted as `int main(void)` with no prototype.
6. All definitions, with `g.tmp` reset per function (counter for
   temporaries) and a forced `return 0;` in `main`.

**Struct params/args are pointers.** `emit_sig` writes `Name *p` for a
struct parameter; `emit_call` passes `&(place)` when the argument is a
place, otherwise materialises a temporary *as a real statement* before
the current line —

```c
Point _oak_t0 = (Point){ .x = 1, .y = 2 };
f(p, &_oak_t0);
```

and `place()` dereferences struct parameters (`(*p).field`) via
`Sym.is_struct_param`. Passing a struct *by value* is impossible in Oak
(`extern fn` params must be scalarish, §6), which is exactly why the
aliasing rule of §7 exists and why FFI wrappers use `ptr` handles.

**Expression rendering** highlights: strings → escaped C literals; floats
→ `%.17g` plus a forced `.0` so C never sees an int literal; `null` →
`NULL`; `s[i]` → `oak_str_at(s, (int64_t)i)`; `a[i]` → `oak_arr_slot`;
`a[lo:hi]` → `oak_str_slice(s, lo, hi)` with `INT64_MAX` for an open
end; array literals → `oak_arr_from(n, sizeof(T), (T[]){...})`; struct
literals → C99 compound literals `((Point){ .x = 1, .y = 2 })`;
`len` → `oak_str_len`/`oak_arr_len`; `push(a, v)` → `oak_arr_push(a,
&(T){ v })`; `print` → `printf("%d\n" | "%g\n" | "%s\n" | "%s\n" with
true/false | "%p\n", ...)`; `string + string` → `oak_str_concat`,
`==`/`!=` → `oak_str_eq`, ordering → `oak_str_cmp(...) < 0`; everything
else maps operator-for-operator via `cop()`.

**Statements** (`emit_stmt`) map 1:1: `var` → `T name = rval;`
(the *initializer's* type, see §6), `assign` → `place = rval;`, `if`/`else`
→ `if (...) { }`, `while` → `while (...) { }`, `return`, expression
statement, nested block (an explicit `{ }` in Oak emits a literal C block,
since C99 allows declarations mid-block). `emit_block_inner` unwraps a
block so `if x { ... }` never double-braces.

Because the output is plain, readable, `-O2`-compiled C, the usual
debugging trick works: `--emit-c out.c`, read/edit it, compile it by hand.

## 9. Extension recipes

Oak is small enough that every language change is mechanical. The phases
always run in the order **lex → parse → typecheck → (alias) → codegen**,
and a feature usually needs one or two of them. Examples below use "add a
`for`-like statement" / "add a builtin" as templates.

**A. New keyword** (say `const`):

1. `oak.h`: add `TOK_CONST` to `TokKind`.
2. `lex.c`: add the `strcmp(id, "const") == 0` branch.
3. `parse.c`: add a `case TOK_CONST:` in `parse_stmt` (and/or
   `parse_program`) and any `parse_...` function.
4. Nothing else *if* the construct reuses existing `Expr`/`Stmt` kinds;
   otherwise add the node kind in `oak.h` and teach `check_expr`/
   `check_stmt` and `rval`/`emit_stmt` about it.

**B. New binary operator** (say `**`): `oak.h` `TokKind`, `lex.c`
scan, `parse_bin` precedence table (level → `TokKind` list), and
`typecheck.c` `EX_BIN` (typing rule), `codegen.c` `cop()` (C spelling).
Precedence is a table, not an expression — adding one is 4 small edits.

**C. New builtin** (say `sqrt`): no lexer work. Special-case it in
`typecheck.c` `EX_CALL` (arity/type rules, result type) and in
`codegen.c` `emit_call` (render the C), plus — if it needs a new runtime
helper — a new `F_*` bit in codegen's feature flags, the flag in
`expr_flags`/`prog_flags`, and the helper text in `emit_*_runtime`. Add
the name to the `'X' is a builtin` guard list so users can't shadow it.

**D. New type** (say `char`): `oak.h` `TypeKind` + constructor, lexer
keyword, `parse_type_atom`, `type_name`, `type_eq`, `is_scalarish`/
`type_is_float` policy, `ctype` in codegen, and any special cases in
`check_expr` (literals) and `rval`.

**E. Change struct passing** (e.g. allow by-value extern params): relax the
`is_scalarish` checks in `typecheck.c`, then `emit_sig` (drop the `*`),
`emit_call` (pass `rval` instead of `&place`), and `alias.c` (the rule
exists *because* of pointer passing — with by-value passing it can go).
`Sym.is_struct_param` and `place()`'s dereference are the other places
that encode the current choice.

**F. New statement** (say `break`): `oak.h` `StmtKind`, `parse_stmt`,
`check_stmt` (walk nested bodies), `emit_stmt`, and — if it is a loop
control — loop labels in codegen (`while` emission).

**G. Warnings / better errors**: `comp_error` in `oak.c` is the single
funnel (`filename:line:col: error: …`); phases only check `c->errors`
after finishing. Suppress cascading noise by returning `type_unit()`
after reporting (the `TY_UNIT` sentinel means "already complained" and
suppresses follow-on errors).

**H. New CLI option**: parse in `main()` around the existing branches
(either side of `--`), store it in a local, and plumb it to codegen through
a `Comp` flag — `Comp.no_protos` already exists for exactly this purpose
(it makes `emit_proto` emit nothing; no CLI switch sets it yet) — or into
the `system()` command line for link-time behaviour.

Rule of thumb: **types flow forward only** — the parser may leave a type
unresolved (negative struct id), but once typecheck runs, every later
phase may read `Expr.type` without re-deriving anything.

## 10. Sharp edges & testing

**Known sharp edges** (deliberate simplifications, safe to build on, must
be respected when modifying):

- **Single global namespace**: imported files share one flat namespace;
  duplicate function/struct names are errors. There is no `::` or module
  qualification.
- **No ownership model**: strings are `const char *`; `+` allocates and
  leaks; arrays grow with `realloc` and are never freed. Leaking is the
  documented design (see LANGUAGE.md "Limitations").
- **Struct arguments are pointers** with the one-call aliasing ban (§7).
- **No integer overflow checks**, no float formatting control
  (`print(f64)` uses `%g`), and `1e5` is not a literal (the lexer has no
  exponent support — write `100000.0`).
- **`g_no_struct_lit` is global parser state**: it is a single `static
  bool` in `parse.c`, correct only because the parser is a single
  depth-first pass. Any new construct that parses `{` ambiguously must
  save/clear/restore it (see §5).
- **Error recovery is minimal**: after a syntax error the parser
  resyncs by skipping one token; expect cascades, and read the *first*
  error message.
- **`return;` in `main` is a compiler bug, not a rule**: typecheck accepts
  it (a unit function may return nothing), but codegen writes a bare
  `return;` into `int main(void)` and gcc rejects it (`'return' with no
  value`). `return 0;` is rejected too (`return type mismatch: expected
  (), got i32`). Early exit from `main` therefore has no working syntax —
  use a `while` loop with a `running` flag. Ordinary `()` functions may use
  `return;` freely. (LANGUAGE.md §16 documents this for users; it is
  listed here because the fix belongs in `emit_stmt`/`emit_sig`.)
- **`type_name` for arrays/raw** returns a pointer to a static buffer —
  two array/raw type names in one `printf` print the same text. This is
  why some type-mismatch messages look odd; fix by making `type_name`
  caller-owned if you touch those messages.
- **The arena moves on growth** (`arena_alloc` copies into a bigger block
  and frees the old one) — pointers into it dangle. Fine for small
  programs; a chunked arena is the fix if large inputs ever break
  mysteriously (see §3).
- **gcc-specific assumptions in generated C**: C99 compound literals,
  declarations after statements, `//`-free style; the driver hardcodes
  `gcc -std=c99 -O2`. Clang works as a drop-in `CC`, MSVC would need the
  command in `main.c` changed.

**Testing**

- `make test` compiles and runs every program in `examples/`
  (hello, bump, copy_ok, control_flow, strings) and asserts that
  `bad_twice.oak` (the aliasing error) is *rejected* — the last line is
  prefixed with `-` so a non-zero exit there is success.
- `gcc -Wall -Wextra` must stay clean; the Makefile already uses it.
- The FFI surface is covered by `examples/raylib` (shim + wrapper + demo),
  built with explicit `-l` flags; rebuilding it after any codegen change is
  the best regression test for `extern fn`, includes, structs-by-pointer and
  runtime helpers.
- FFI debugging loop: `oakc prog.oak --emit-c prog.c -- shim.c -I. -lfoo`,
  then compile `prog.c` by hand to see the real gcc diagnostic, or open
  it in an editor — the generated C is meant to be readable (see
  C-INTEROP.md §10).
- When changing a phase, test the smallest thing first: a one-function
  file that should compile to a few lines of C, then the examples, then
  a project demo.
