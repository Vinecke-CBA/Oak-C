# The Oak Language — Complete Reference

Everything you need to write correct Oak programs: lexical structure, types,
expressions, statements, functions, modules, builtins, the full grammar, the
error-message catalogue and the known limitations.

## Table of contents

1. [Hello, world & toolchain](#1-hello-world--toolchain)
2. [Lexical structure](#2-lexical-structure)
3. [Types](#3-types)
4. [Variables and scoping](#4-variables-and-scoping)
5. [Expressions](#5-expressions)
6. [Statements and control flow](#6-statements-and-control-flow)
7. [Functions](#7-functions)
8. [Structs](#8-structs)
9. [Arrays](#9-arrays)
10. [Strings](#10-strings)
11. [Modules (`import`)](#11-modules-import)
12. [Builtin functions](#12-builtin-functions)
13. [The aliasing rule](#13-the-aliasing-rule)
14. [Full grammar (EBNF)](#14-full-grammar-ebnf)
15. [Error message catalogue](#15-error-message-catalogue)
16. [Known limitations and quirks](#16-known-limitations-and-quirks)

---

## 1. Hello, world & toolchain

```oak
fn main() {
    print("Hello, world!");
}
```

```sh
oakc hello.oak -o hello && ./hello
```

Facts that shape everything below:

- Oak compiles **to C, then through `gcc -O2`**. Your program runs at native
  speed; `i32` really is `int32_t`, a struct really is a C struct.
- Every program must define `fn main()` — no arguments, no return value.
- Static typing with local inference: `var x = 1;` infers `i32`. Annotations
  exist but are rarely needed (required only where inference has nothing to
  go on, e.g. empty arrays).
- Memory is **never reclaimed**. Strings and arrays allocate on the heap and
  live until the process exits. Fine for tools/games; not for long-running
  servers.
- The whole program is **one flat namespace** across all imported files.

## 2. Lexical structure

**Whitespace** is insignificant except as token separation. Newlines do not
terminate statements — every statement ends with `;`.

**Comments:** `// to end of line`. There are no block comments.

**Identifiers:** `[A-Za-z_][A-Za-z0-9_]*`.

**Keywords** (cannot be used as identifiers):

```
fn  struct  var  if  else  while  return  true  false  null
i32  f64  bool  string  ptr  extern  import  include
```

`print`, `len` and `push` are *not* keywords — they are builtins recognized
by name during typechecking (and you may not define `fn print(...)`).

**Integer literals:** decimal digits only, `123`. No hex, octal, underscores
or sign (write `-5` as unary minus). Literals are typed `i32`; there is **no
overflow checking** — a literal larger than 2 147 483 647 compiles and
silently truncates in C.

**Float literals:** `1`, `1.5`, `0.0`. A dot must be followed by a digit
(`1.` is a parse error). **No exponent notation** — `1e-4` does not lex;
write `0.0001`.

**String literals:** `"..."` with escapes `\n \t \r \\ \"`. An unknown
escape is a compile error (`unknown escape '\x'`), an unterminated literal is
`unterminated string literal`. Strings are UTF-8 byte arrays; bytes pass
through untouched (no Unicode handling in the language).

**Operators:**

```
+  -  *  /  %            arithmetic
==  !=  <  >  <=  >=      comparison
&&  ||  !                logic (bool only)
=                        assignment (statements only)
.  ,  :  ;  (  )  {  }  [  ]  ->  ...   punctuation
```

There is **no** bitwise `& | ^ ~ << >>`, no `++/--`, no compound `+=`
(unless you count the fact that none of these exist at all).

## 3. Types

| Type | C equivalent | Literal | Notes |
| --- | --- | --- | --- |
| `i32` | `int32_t` | `42` | 32-bit signed. All integer math |
| `f64` | `double` | `42.0` | IEEE-754 double |
| `bool` | `bool` | `true` `false` | Only `if`/`while` conditions and logic ops |
| `string` | `const char *` | `"hi"` | Immutable heap byte string (see §10) |
| `ptr` | `void *` | `null` | Untyped pointer, **FFI only** (see C-INTEROP.md) |
| `T[]` | `OakArr *` | `[1, 2]` | Dynamic array of `T` (see §9) |
| `struct` | `struct Name` | `Name { ... }` | See §8 |
| `()` | `void` | — | "no value"; what functions without `->` return |
| `C "text"` | emitted verbatim | — | Raw C type escape hatch, e.g. `C "char *"` |

**Unit `()`** cannot be stored: `var x = f();` where `f` returns `()`
errors with `cannot store () in a variable`. `print(...)` returns `()`.

**`ptr` and `null`:** `null` has type `ptr`. Comparing `p == null` is the
idiom for handling failed C constructors. Oak never dereferences, allocates
or does arithmetic on `ptr` — it exists purely to shuttle C pointers around;
see C-INTEROP.md.

**Raw types `C "..."`:** any C type spelling goes where a type is expected,
quoted:

```oak
extern fn malloc(n: i32) -> C "void *";
var p: C "char *" = null;
```

The text is pasted into the generated C untouched (so it must compile there).
Raw values can be passed around but not operated on: no arithmetic, no
comparison (the typechecker rejects `TY_RAW` in both).

**`f64`/`i32` mixing:** arithmetic and comparisons may mix them; the result
promotes to `f64` if either side is `f64` (C promotion does the work):

```oak
var x = 3 + 4.5;    // f64, 7.5
if x > 4 { ... }    // ok
```

**The one coercion:** an **integer literal** is accepted wherever `f64` is
expected (the literal is retyped to `f64`, C converts it). This applies to
variable initialisers with annotations, assignments, call arguments, `return`
values, struct-literal fields and array elements. It applies to *nothing*
else — a `var x: f64 = someI32;` is an error; only `42` works, not `x`.

**Equality (`==`/`!=`):** allowed when both sides have the same type,
including `i32↔f64` mixed numerics (promotes), `bool`, `string` (compares
bytes via `strcmp`), `ptr` (pointer identity — use for `null` checks), and
`struct` (compares member-by-member *in C*, so scalar fields compare by
value but `string` fields compare by **pointer** — see quirks). Arrays and
raw types cannot be compared: `cannot compare T values`.

**Ordered comparison (`< > <= >=`):** numerics (mixed `i32/f64` fine) and
`string` (lexicographic `strcmp`). Not `bool`, `ptr`, struct or array.

## 4. Variables and scoping

```oak
var x = 1;             // i32, inferred
var y: f64 = 1;        // annotation; int literal coerces
var a: i32[] = [];     // annotation required for empty array
x = x + 1;             // plain assignment, no `:=`, no `+=`
```

- `var` **requires** an initialiser. There is no declaration-without-value.
- The annotation (`: T`) is optional; inference reads the initialiser.
- **Scoping is block-based.** A `{ }` block (bodies of `if`, `while`,
  functions, or a bare `{ }` statement) opens a scope. `scope` in the
  typechecker is a depth number; symbols record the depth they were added at.
  - Re-declaring the same name in the **same** scope → `duplicate name 'x'`.
  - Shadowing a name from an **outer** scope is allowed (inner C declaration
    shadows in the emitted code too).
  - After a block ends, the scope depth pops and outer names are visible again.
- Locals become plain C locals (`int32_t x = ...;`) — zero overhead, same
  addresses as C would give.
- Everything is mutable; there are no `const`, `let` or final bindings.

## 5. Expressions

### Operator precedence (loosest → tightest)

| Level | Operators | Associativity |
| --- | --- | --- |
| 1 | `\|\|` | left |
| 2 | `&&` | left |
| 3 | `== !=` | left |
| 4 | `< > <= >=` | left |
| 5 | `+ -` | left |
| 6 | `* / %` | left |
| 7 | unary `-` `!` | right |
| 8 | postfix: `f(...)`, `a.b`, `a[i]`, `a[lo:hi]`, struct literal `{...}` | left |

`( )` may always be used to re-group. Examples: `1 + 2 * 3` is `7`;
`a && b || c` is `(a && b) || c`; `-x * y` is `(-x) * y`.

### Arithmetic (`+ - * /`)

- Both sides `i32` → `i32`; either side `f64` → `f64` (C promotion).
- `/` truncates toward zero for `i32` (C semantics); `f64` is normal
  division. **`i32` division by zero crashes the program** (C UB → hardware
  trap on most platforms).
- Errors otherwise: `arithmetic needs i32 or f64, got X and Y`.

### `%` (modulo)

`i32` only on both sides (`'%' requires i32`), C semantics (sign follows the
dividend: `-7 % 3 == -1`).

### Comparison `< > <= >=`

Numerics (mixed ok) or strings (lexicographic). Result is `bool`.

### Equality `== !=`

See §3. Result is `bool`.

### Logic `&& || !`

`bool` only — there is **no truthiness**; `if x { }` where `x: i32` is an
error (`if condition must be bool`). `&&` and `||` short-circuit (the C
emitter writes them literally).

### String `+`

Two strings concatenate into a freshly allocated string. `string + i32` is an
error. Concatenation in a loop is O(n²) and leaks every intermediate.

### Unary `-` and `!`

`-` needs a numeric operand (`unary - requires i32 or f64`), `!` needs
`bool` (`! requires bool`). No unary `+`.

### Indexing `a[i]`

- `i` must be exactly `i32` (`index must be i32, got f64` — an int *literal*
  is fine, a `f64` variable is not).
- **Array:** yields the element as an assignable **place** — `a[i] = v`
  works. Negative indices count from the end (`a[-1]` is the last element).
  Out of range (after wrapping negatives) **aborts the process** with
  `oak: index N out of range for length M`.
- **String:** yields a 1-character `string` *copy* (`s[1]` is `"e"`), with
  the same negative/abort semantics. **Not assignable** — `s[0] = 'x'` fails
  with `invalid assignment target`.
- Anything else: `cannot index T`.

### Slicing `s[lo:hi]`

Strings only (`slicing is only supported on strings, got T`). Bounds are
optional and each must be `i32` when present:

```oak
s[1:3]   // chars 1..2 (lo inclusive, hi exclusive)
s[2:]    // from 2 to end
s[:2]    // start to 2
s[:]     // whole copy
s[-2:]   // last two (negatives wrap)
```

Bounds are clamped to `[0, len]` at runtime (never aborts), `hi < lo`
yields `""`. The result is a newly allocated copy.

### Array literals `[1, 2, 3]`

- All elements must share one type (plus the int→f64 literal coercion);
  otherwise `array elements must all be X, got Y`.
- The element type is inferred from the **first** element.
- Empty `[]` has nothing to infer: `cannot infer the element type of an
  empty array literal (write e.g. var a: i32[] = [];).` With an annotation,
  `var a: i32[] = [];` works — the annotation supplies the element type.
- `()` elements are rejected (`cannot store () in an array`).
- The literal becomes one heap allocation; arrays are **not** C stack arrays.

### Struct literals `Point { x: 1, y: 2 }`

- Every field must be listed exactly once (`missing field`, `duplicate
  field`, `no field 'x' on Point`, `type mismatch for field`).
- Field syntax is `name: value` separated by commas (trailing comma ok).
- **Ambiguity rule:** a `{` directly after an `if`/`while` condition always
  opens the *body*, even if the condition ends with an identifier. A struct
  literal in a condition therefore needs parentheses:
  `if (Counter { n: 5 }).n == 5 { ... }`. Parentheses, call arguments and
  field values re-enable literal parsing.

### Calls `f(a, b)`

- Arity must match (`'f' expects N argument(s), got M`), except `extern fn`
  declared with `...` which accepts **extra** scalar arguments.
- Each argument must match the parameter type (plus literal coercion):
  `argument type mismatch: expected X, got Y`.
- Builtins `print`, `len`, `push` are intercepted before normal lookup.
- Struct arguments get special ABI treatment (passed by pointer) — see §13.

### Place expressions (assignment targets)

Valid: `x`, `p.x` (whose base is a place), `a[i]` on **arrays**. Invalid:
calls, literals, slices, string indexing, computed chains like `f().x`.
Violation → `invalid assignment target`.

## 6. Statements and control flow

```oak
var x = expr;           // declaration + init
place = expr;           // assignment
if cond { ... } else if cond { ... } else { ... }
while cond { ... }
{ ... }                 // bare block (opens a scope)
expr;                   // expression statement (usually a call)
return expr;            // from a value-returning function
return;                 // from a () function — see quirks
```

- Conditions must be `bool`.
- `if`/`while` bodies are blocks — no single-statement form, no semicolons
  around them.
- There is **no `for`, `break`, `continue` or `switch`**. Emulate with
  `while` + `bool` flags (see `examples/bench`), or recursion.
- The `else` branch may chain `if` (`else if`) or hold a block.

**Return rules:**

- A function with `-> T` must contain at least one `return expr;` with a
  type-compatible expression (`function 'f' must return T`,
  `return type mismatch: expected X, got Y`). The compiler checks *existence*,
  not that every path returns — falling off the end compiles and the C
  compiler emits undefined behaviour at runtime. Keep returns explicit.
- A `()` function may `return;` or just fall off the end.
- `fn main()` cannot take arguments or return a value; the emitted C `main`
  gets an automatic `return 0;`.


## 7. Functions

```oak
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn greet(name: string) {        // no `->` means returns ()
    print("hi " + name);
}
```

- Declared at **top level** only (no nesting, no closures, no lambdas).
- Parameters: `name: T`, comma-separated; any non-unit type: scalars,
  structs, arrays, strings, `ptr`, raw types.
- Missing `-> T` ⇒ returns `()`.
- Names are global across the whole program (all imported files). Duplicates:
  `duplicate function 'f'`; redefining a builtin: `'print' is a builtin`.
- **Call convention is C.** Non-struct arguments are copied by value.
  Struct parameters are passed **by pointer** internally, which is why a
  callee can mutate them (`p.x = p.x + 1` changes the caller's copy) and why
  the aliasing rule (§13) exists. Arrays and strings are heap handles: the
  handle is copied, the contents are shared; arrays can `push`/assign
  through it, strings are immutable so sharing is invisible.
- Recursion is fine — `fn fib(n: i32) -> i32 { ... fib(n - 1) ... }`.
- Each function emits as `static <ret> name(...)` in the generated C (so the
  C compiler can inline across your whole program), except `main` which
  becomes `int main(void)`.

## 8. Structs

```oak
struct Point {
    x: i32,
    y: i32,
}

fn main() {
    var p = Point { x: 1, y: 2 };
    var q = p;          // COPY — plain C struct assignment
    q.x = 99;
    print(p.x);         // still 1
}
```

- `struct Name { field: type, ... }` (commas optional — newline separation
  works). Top level only.
- Struct literals must list **every** field, no more, no less.
- Field access: `p.x`, chains `p.inner.deep` work (each level a struct).
- **Value semantics on assignment**: `var q = p;` copies. **Reference
  semantics in calls**: struct params arrive by pointer so the callee
  mutates the caller's value (§7). This combination is the language's single
  "aha" — see also §13.
- Recursive structs are rejected (`recursive struct 'S'`): there is no
  indirection type usable from Oak source to break the cycle.
- Duplicate struct names across files: `duplicate struct 'S'`.
- Structs containing arrays/strings work; copying shares the heap data.
- `==` on structs compiles to C member-wise comparison: fine for scalar
  fields; `string` fields compare **pointers**, not contents — compare the
  fields explicitly when you need content equality.

## 9. Arrays

```oak
fn main() {
    var a = [10, 20, 30];   // i32[]
    print(len(a));          // 3
    print(a[0]);            // 10
    print(a[-1]);           // 30 — negative = from the end
    a[1] = 99;
    push(a, 40);            // grows
    print(len(a));          // 4

    var f = [1.5, 2.5];     // f64[]
    var e: string[] = [];   // empty needs the annotation
}
```

- Type is `T[]` (nestable: `i32[][]`).
- Backed by a heap block (`OakArr { len, cap, esize, data }`) with **runtime
  bounds checking on every access** and negative-index wrapping. Out of
  range ⇒ process abort: `oak: index N out of range for length M`.
- `len(a)` is O(1); `push(a, v)` doubles capacity when full (amortized
  O(1)). Both are free-function builtins — there is no `a.len()` method
  syntax.
- `var b = a;` **shares** the same array (handle copy). Index assignment
  writes through to the shared buffer.
- Element-type rules follow literal inference (§5).
- Array helpers are emitted into your C file only when you use arrays
  (on-demand runtime — INTERNALS.md).

## 10. Strings

```oak
var s = "abc" + "def";      // concat -> fresh heap copy
print(s == "abcdef");       // content comparison
print(s[1]);                // "b" (1-char copy)
print(s[1:4]);              // "bcd"
print(len(s));              // 7 (bytes)
```

- Representation: `const char *`, NUL-terminated, no length prefix — `len`
  is `strlen`, so **embedded NUL bytes truncate**.
- Immutable: `s[i] = ...` is a compile error. Index/slice/`+` all return
  freshly allocated copies which are **never freed** (by design — §16).
- Equality and ordering compare bytes (`strcmp`), no locale.
- `string` is an FFI scalar (pass it to `printf`), and a valid struct
  field, parameter and return type.
- Escapes: `\n \t \r \\ \"` only; anything else is a compile error.
- The aliasing rule does not apply to strings — `f(s, s)` is fine.

## 11. Modules (`import`)

```oak
// math.oak
fn add(a: i32, b: i32) -> i32 { return a + b; }

// app.oak
import "math.oak";
fn main() { print(add(2, 3)); }
```

```sh
oakc app.oak -o app
```

Rules:

- `import "path";` at **top level only**.
- Resolution: tried as given first (relative to the compiler's working
  directory), then **relative to the importing file's directory**. For
  subdirectories use the relative form: `import "lib/util.oak";` inside
  `src/app.oak` finds `src/lib/util.oak`.
- Imports are **recursive** (an imported file's imports load too) and
  **load-once** (the same path is a no-op — cycles terminate, double
  definition cannot come from loading twice).
- There are **no namespaces**: every loaded file merges into one program
  with one flat namespace. Struct and function names must be unique across
  the entire import graph (`duplicate function`, `duplicate struct`).
- `import` introduces no qualifier — call `add(...)` directly.
- Resolution order does not affect semantics; functions are looked up
  globally after parsing completes.
- Parse errors in any loaded file abort before typechecking. `include` and
  `extern fn` declarations accumulate program-wide.

## 12. Builtin functions

Recognized by name during typechecking/codegen; you may not define your own
with these names (`fn len(...)` ⇒ `'len' is a builtin`).

| Builtin | Signature | Semantics |
| --- | --- | --- |
| `print(x)` | scalar → `()` | One line to stdout + newline. `i32`→`%d`, `f64`→`%g`, `bool`→`true/false`, `string`→`%s`, `ptr`→`%p`. Exactly 1 argument. Struct/array/`()`: `print cannot print T`. |
| `len(x)` | `string` or `T[]` → `i32` | `strlen` / stored length, O(1). Exactly 1 argument. |
| `push(a, v)` | `T[], T` → `()` | Appends; capacity doubles. Int literal coerces to `f64` elements. Exactly 2 arguments; `push expects an array`, `cannot push X into Y[]`. |

There is no format string — for formatted output use `extern fn printf`
(C-INTEROP.md). `print` of `f64` uses `%g` (so `1000000.0` prints `1e+06`).

## 13. The aliasing rule

**Within one call expression, a struct variable may appear at most once
among struct-typed parameters.**

```oak
f(p, p);        // error: p cannot be used more than once in the same call
var q = p;
f(p, q);        // ok
f(p, p.x + 1);  // ok — only the first argument is "the variable p"
```

Why: struct parameters are received **by pointer** (that is what makes
in-call mutation work), so passing the same variable twice would alias one
object twice in a single frame. The checker tracks the *root variable* of
each argument (`root_sym`), so `a.b`, `a.b[i]`, slices and `f(a)` that trace
back to variable `a` still count as `a`. Non-struct parameters (`i32`,
`f64`, `bool`, `string`, `T[]`, `ptr`) are never subject to the rule.

Error text: `%s cannot be used more than once in the same call`.
Enforced by a dedicated pass after typechecking (INTERNALS.md §7).

## 14. Full grammar (EBNF)

Notation: `a?` optional, `a*` zero-or-more, `a | b` choice, `"x"` literal.
This mirrors `src/parse.c` exactly.

```ebnf
program     = { item } ;
item        = struct_decl | fn_decl | extern_decl | import_decl | include_decl ;

struct_decl = "struct" IDENT "{" { field_decl } "}" ;
field_decl  = IDENT ":" type [ "," ] ;

fn_decl     = "fn" IDENT "(" [ params ] ")" [ "->" type ] block ;
extern_decl = "extern" "fn" IDENT "(" [ params ] ")" [ "->" type ] ";" ;
params      = param { "," param } [ "," ] | "..." ;
param       = "..." | IDENT ":" type ;         (* "..." only in extern *)
import_decl = "import" STRING ";" ;
include_decl= "include" STRING ";" ;

type        = type_atom { "[" "]" } ;           (* suffix = array *)
type_atom   = "i32" | "f64" | "bool" | "string" | "ptr"
            | IDENT                              (* struct name *)
            | "C" STRING ;                       (* raw C type *)

stmt        = var_stmt | assign_stmt | if_stmt | while_stmt
            | return_stmt | block | expr ";" ;
var_stmt    = "var" IDENT [ ":" type ] "=" expr ";" ;
assign_stmt = place "=" expr ";" ;
if_stmt     = "if" expr block [ "else" ( if_stmt | block ) ] ;
while_stmt  = "while" expr block ;
return_stmt = "return" [ expr ] ";" ;
block       = "{" { stmt } "}" ;
place       = IDENT | place "." IDENT | postfix "[" expr "]" ;

expr        = level1 ;                          (* see precedence table §5 *)
level1      = level2 { "||" level2 } ;
level2      = level3 { "&&" level3 } ;
level3      = level4 { ("=="|"!=") level4 } ;
level4      = level5 { ("<"|">"|"<="|">=") level5 } ;
level5      = level6 { ("+"|"-") level6 } ;
level6      = unary { ("*"|"/"|"%") unary } ;
unary       = ("-"|"!") unary | postfix ;
postfix     = primary { call | field | index | slice } ;
call        = "(" [ expr { "," expr } [ "," ] ] ")" ;
field       = "." IDENT ;
index       = "[" expr "]" ;
slice       = "[" [ expr ] ":" [ expr ] "]" ;
primary     = INT | FLOAT | "true" | "false" | "null" | STRING
            | IDENT [ "(" args ")" | "{" struct_fields "}" ]
            | "(" expr ")"
            | "[" [ expr { "," expr } [ "," ] ] "]" ;
struct_fields = IDENT ":" expr { "," IDENT ":" expr } [ "," ] ;
```

Struct-literal caveat: `IDENT "{"` only parses as a literal when the parser
is not in *condition position* (see §5).

## 15. Error message catalogue

All errors print `file:line:col: error: message`; the compiler exits 1 after
the current phase. Grouped by the phase that raises them:

**Lexer** (`src/lex.c`)

| Message | Cause |
| --- | --- |
| `unknown escape '\c'` | Escape outside `\n \t \r \\ \"` |
| `unterminated string literal` | Missing closing `"` |
| `unexpected '&'` | Single `&` — use `&&` |
| `unexpected '\|'` | Single `\|` — use `\|\|` |
| `unexpected character 'c'` | `@ # $ ?` or a lone operator char |

**Parser** (`src/parse.c`) — recovery continues inside the file, so cascades
after the first error are normal.

| Message | Cause |
| --- | --- |
| `expected X` | Syntax expectation: `'{' '}' '(' ')' '[' ']' ';' ':' ',' '=' '->'`, `type`, `expression`, `identifier`, `field name`, `parameter name`, `function name`, `struct name`, `module file name`, `C header name`, `condition body`... |
| `invalid assignment target` | LHS of `=` isn't a place (call, literal, string index, `f().x`) |
| `'...' is only allowed on extern fn` | Variadic parameter on a normal function |
| `expected struct, fn, extern fn, import or include` | Junk at top level |
| `cannot infer the element type of an empty array literal (write e.g. var a: i32[] = [];)` | `[]` without annotation |

**Typechecker** (`src/typecheck.c`)

| Message | Cause |
| --- | --- |
| `unknown type 'T'` | Struct name not declared in the program |
| `unknown struct 'T'` / `unknown function 'f'` | Not defined in the import graph |
| `duplicate name 'x'` / `duplicate function 'f'` / `duplicate struct 'S'` | Same scope / same program |
| `'%s' is a builtin` | Named a fn `print`, `len` or `push` |
| `cannot store () in a variable` / `cannot store () in an array` | Storing a void expression |
| `variable 'x' is T but initialised with U` | Annotation mismatch (no coercion applies) |
| `type mismatch in assignment: T vs U` | Incompatible assignment |
| `array elements must all be T, got U` | Mixed literal element types |
| `index must be i32, got T` / `slice bound must be i32, got T` | Non-i32 index/bound |
| `cannot index T` / `slicing is only supported on strings, got T` | Wrong base type |
| `no field 'x' on S` / `duplicate field 'x'` / `missing field 'x' in S literal` / `type mismatch for field 'x': expected T, got U` | Struct literal problems |
| `field access on non-struct` | `x.y` on a non-struct (often a cascade) |
| `recursive struct 'S'` | Cycle through struct fields |
| `print takes exactly one argument` / `len takes exactly one argument` / `push takes an array and a value` | Builtin arity |
| `len expects a string or an array, got T` / `push expects an array, got T` / `cannot push U into T[]` / `print cannot print T` / `cannot push ()` | Builtin argument types |
| `argument type mismatch: expected T, got U` | Call argument type |
| `'f' expects N argument(s), got M` / `'f' expects at least N...` | Arity (varargs accept extras) |
| `these extra arguments must be scalars, got T` | Variadic extra arg is struct/array/unit |
| `arithmetic needs i32 or f64, got T and U` | `+ - * /` on other types |
| `'%' requires i32` | Modulo on floats |
| `cannot compare T and U` / `cannot compare T values` | `== !=` mismatch, or array/raw/unit operands |
| `&& and \|\| require bool` / `! requires bool` / `unary - requires i32 or f64` | Operand types |
| `if condition must be bool` / `while condition must be bool` | No truthiness |
| `return type mismatch: expected T, got U` | `return` expression type |
| `function must return T` / `function 'f' must return T` | Value function lacks `return expr` |
| `extern fn parameters must be scalars, but 'x' is T` / `extern fn cannot return T` | FFI type not scalar (see C-INTEROP.md) |
| `main takes no arguments` / `main cannot return a value` / `missing fn main()` | Entry-point rules |

**Alias analysis** (`src/alias.c`)

| Message | Cause |
| --- | --- |
| `%s cannot be used more than once in the same call` | Same struct variable in two struct parameters of one call (§13) |

**Generated code** (reported by gcc, or at runtime)

| Message | Cause |
| --- | --- |
| `gcc failed` | Generated C didn't compile — rerun with `--emit-c out.c` and read gcc's diagnostics (usually an FFI prototype clash or a bad raw type) |
| `conflicting types for 'x'` (gcc) | Your `extern fn`/raw C prototype disagrees with the real header — use the shim pattern (C-INTEROP.md) |
| `'return' with no value, in function returning non-void` (gcc) | Bare `return;` inside `fn main()` — see §16 |
| `oak: index N out of range for length M` | Runtime bounds check aborted the process |
| `oak: out of memory` | Allocation failure inside generated code |

## 16. Known limitations and quirks

Things a careful programmer must know — current behaviour, not aspirations:

**Control flow**

- No `for`, `break`, `continue`, `switch`, `goto`. Use `while` + flags, or
  recursion.
- No early `return;` inside `fn main()`: the emitter writes a bare C
  `return;` into `int main(void)`, which gcc rejects
  (`'return' with no value...`). Structure main with `if/else` chains or a
  `running` flag (see `examples/raylib/demo_spin.oak`). Bare `return;` in
  ordinary `()` functions is fine.
- A value-returning function can fall through without returning (only
  *existence* of a return is checked) — silent UB at runtime.

**Types & values**

- Integer literals are unchecked: `9999999999` compiles and truncates.
- No exponent floats (`1e-4` doesn't lex). No hex/octal/underscores.
- `%` on negative operands follows C (sign of dividend).
- `i32` division/modulo by zero crashes (hardware trap).
- Struct `==` compares `string` fields by pointer, not content.
- Array equality is not allowed at all.
- No integer width other than `i32`; no `u8`, `i64`, `usize`.
- Raw `C "..."` values can only be moved around — no arithmetic or
  comparison; used for pointers/handles.

**Memory**

- Nothing is ever freed: every string `+`, slice, `s[i]`, and any temp
  string from an FFI call leaks. Arrays' buffers leak too. For long-running
  loops, reuse buffers (via C) instead of concatenating per-iteration.
- No `free` in the language; to release an FFI allocation, call the C free
  through `extern fn`.

**Concurrency:** none — single thread, no atomics, no channels. FFI calls
that block (e.g. `sleep`) block the whole program.

**Modules**

- Flat namespace across all imported files; prefix names manually
  (`util_add`, `math_add`) to avoid `duplicate function`.
- No re-export/visibility: every function is callable from everywhere.
- Import paths are not canonicalized (`a/../b.oak` and `b.oak` would be
  distinct load keys) — avoid `..` in import paths.

**FFI sharp edges** (details in C-INTEROP.md)

- Structs and arrays **cannot** cross the FFI boundary (`extern fn`
  parameters must be scalars). Use `ptr` + a C shim.
- Oak's emitted `extern` prototypes can clash with a real C header
  (`conflicting types`) — the fix is to declare `oak_*` shim names instead
  of the library's names.
- `include "header.h"` with a bare name compiles as `#include <header.h>`,
  so shim headers in your project need `-Ipath` on the gcc line.

**Emitter limitations**

- Statement-level temporaries: struct arguments that aren't variables are
  spilled into `_oak_tN` locals emitted *before* the statement — you never
  see this, but it means side-effect order inside one call is
  left-to-right only (which matches C anyway).
- `print` has no formatting; use `printf` via FFI for precision.

These are all *documented behaviour* — if you need different behaviour,
INTERNALS.md shows exactly where each rule lives.
