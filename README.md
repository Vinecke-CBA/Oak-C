# Oak

A tiny language that compiles to C, then to a native binary with gcc.

The compiler is written in C. There is no interpreter.

## Build

```
gcc -std=c99 -Wall -Wextra -O2 -o oakc src/oak.c src/lex.c src/parse.c src/typecheck.c src/alias.c src/codegen.c src/main.c
```

Or `make` if you have it. `make test` compiles and runs every example.

## Run

```
oakc examples/hello.oak -o hello
hello
```

Options:

- `-o binary` output executable (default `a.exe` on Windows, `a.out` elsewhere)
- `--emit-c file.c` keep the generated C
- `--keep-c` keep the temporary `.oak.c` file
- `-l/-L/-I/-D/-O/-W/-f/-m/-std...` forwarded to `gcc` (e.g. `-lm`)
- `-- <flags...>` everything after `--` is forwarded to `gcc` verbatim

```sh
oakc app.oak -o app -- -lm -I./include
```

## A five minute tour

New here? Read top to bottom — each part builds on the last:
part 1 (hello → structs) is the base language, part 2 adds arrays/strings,
part 3 splits code across files, part 4 calls C.

### Part 1: hello, functions, structs

```
fn main() {
    print("Hello world!");
}
```

`main` is required, takes no arguments and returns nothing. `print` accepts
`i32`, `f64`, `bool`, `string` and `ptr`, and adds a newline.

### Variables and numbers

```oak
fn main() {
    var x = 1;            // i32
    var pi = 3.5;         // f64 (an int literal also works where f64 is wanted)
    var y = x + 2 * 3;    // + - * / % work on numbers
    x = x + 1;            // plain assignment
    print(x);             // 2
    print(y);             // 7
    print(pi);            // 3.5
}
```

Every local is declared with `var` and an initialiser; the type is inferred.
Add `: T` when the compiler cannot infer it (notably for an empty array):
`var a: i32[] = [];`. Integer literals coerce to `f64` automatically.

### Booleans and control flow

```
fn main() {
    var done = false;
    if !done {
        print(1);
    } else {
        print(2);
    }

    var i = 0;
    while i < 3 {
        print(i);          // 0 1 2
        i = i + 1;
    }
}
```

Comparisons `== != < > <= >=` produce a `bool`; `&&` `||` `!` combine them.
`if` and `while` need a `bool`. `else if` works by chaining.

One parsing rule to know: a `{` straight after a condition always opens the
body, never a struct literal, so a struct literal used as a condition needs
parentheses. See `examples/control_flow.oak`.

### Functions

```
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn greet(name: string) {
    print("hi " + name);
}

fn main() {
    print(add(2, 3));      // 5
    greet("there");        // hi there
}
```

A missing `-> T` means the function returns nothing. A function that returns a
value needs a `return`, and the returned expression must match the type. Oak
checks that a `return` exists and that its type is right; it does not prove that
every path returns.

### Structs

```
struct Point {
    x: i32,
    y: i32,
}

fn bump(p: Point) {
    p.x = p.x + 1;
}

fn main() {
    var p = Point { x: 1, y: 2 };
    bump(p);
    print(p.x);            // 2
}
```

Struct literals must list every field. Field access is always `p.x`, never a
pointer or a reference.

`bump(p)` updates the caller's `p`: a struct argument is exclusive for the
duration of the call. Assignment is a copy:

```
var p = Point { x: 1, y: 2 };
var q = p;      // q is a copy
q.x = 99;
print(p.x);     // 1
```

### Strings

```
fn main() {
    var name = "world";
    var msg = "Hello, " + name;     // concatenation
    print(msg);                     // Hello, world

    if name == "world" {            // compares contents, not pointers
        print("match");
    }

    var s = "";                     // the empty string is fine
    s = s + "still dynamic";        // grows; nothing to declare
    print(s);
}
```

Strings are dynamic: you never write a length and you never manage a buffer by
hand. `+` concatenates, `==` and `!=` compare contents, `print` writes one out.
Escapes `\n \t \r \\ \"` work as usual.

Strings can be struct fields, parameters and return values:

```
struct User {
    name: string,
    age: i32,
}

fn label(u: User) -> string {
    return u.name + "!";
}

fn main() {
    var u = User { name: "Ana", age: 34 };
    print(label(u));
}
```

Strings are immutable values. Passing one around shares the underlying bytes
because they are never changed, so unlike structs there is no restriction on
using the same string variable twice in one call. New string bytes come from
`malloc` and are never freed, the same way Oak never frees anything else.

### The one aliasing rule

In a single call, a struct variable may be used as at most one argument.

```
f(p, p);     // error
var q = p;
f(p, q);     // ok
```

Struct arguments are exclusive for the duration of the call (implemented as C
pointers). That is for speed and safety. It is not something you declare. The
rule never applies to `i32`, `bool` or `string`, only to structs.

## Language reference

Types: `i32`, `f64`, `bool`, `string`, `ptr` (+ `null`), `T[]`, `C "..."`,
`struct`, and `()` for "no value".

| Feature | Syntax |
| --- | --- |
| function | `fn name(a: i32) -> i32 { ... }` |
| extern | `extern fn sqrt(x: f64) -> f64;` (+ `...` for variadic) |
| import | `import "other.oak";` |
| include | `include "stdio.h";` |
| raw C type | `C "char *"` (any C spelling, quoted) |
| array | `[1, 2, 3]`, `a[i]`, `a[i] = v`, `len(a)`, `push(a, v)` |
| string idx/slice | `s[i]`, `s[lo:hi]` (either bound may be omitted), `len(s)` |
| struct | `struct S { a: i32, b: string, }` |
| literal | `1`, `true`, `false`, `"text"` |
| local | `var x = expr;` or `var a: i32[] = [];` |
| assignment | `x = expr;`, `p.x = expr;`, or `a[i] = expr;` |
| call | `f(a, b)`, `print(x)`, `len(a)`, `push(a, v)` |
| field | `p.x` |
| if | `if cond { } else if cond { } else { }` |
| while | `while cond { }` |
| return | `return expr;` or `return;` |
| comment | `// to end of line` |

Operators, tightest first: unary `-` `!`, then `* / %`, then `+ -`, then
`< > <= >=`, then `== !=`, then `&&`, then `||`.

Arithmetic and ordered comparison work on `i32`/`f64` (mixed is fine, C
promotes). `== !=` also work on `bool` and `string`. `+` on two strings
concatenates. `%` is `i32` only. `&& || !` are `bool` only.

Builtins: `print(x)` for `i32`/`f64`/`bool`/`string`/`ptr`,
`len(s)` / `len(a)`, `push(a, v)` for arrays.

### Arrays, indexing, slicing (mini tutorial part 2)

```oak
fn main() {
    var a = [1, 2, 3];   // i32[]
    print(len(a));       // 3
    print(a[0]);         // 1
    print(a[-1]);        // 3, negative counts from the end
    a[1] = 99;           // elements are assignable
    push(a, 4);          // grows
    print(a[3]);         // 4

    var s = "hello";
    print(s[1]);         // "e" (a 1-char string copy)
    print(s[1:3]);       // "el" (lo inclusive, hi exclusive, both optional)
    print(len(s));       // 5
}
```

Out-of-range indexing aborts with `oak: index ... out of range`.
Slice bounds clamp. Assigning to a string character is an error; assign to
array elements only. Empty literals need an annotation:
`var a: i32[] = [];`.

### Modules: multi-file programs (mini tutorial part 3)

`math.oak`:

```oak
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

`app.oak`:

```oak
import "math.oak";

fn main() {
    print(add(2, 3));    // 5
}
```

```sh
oakc app.oak -o app
```

Imports resolve relative to the importing file, recurse, and are loaded once
(duplicate imports are ignored). All files share one program, so keep
struct/function names unique across them.

### C interop: `include` + `extern fn` + raw types (mini tutorial part 4)

```oak
include "stdio.h";
include "math.h";

extern fn puts(s: string) -> i32;
extern fn sqrt(x: f64) -> f64;

fn main() {
    puts("hi");          // prints "hi" via C
    print(sqrt(16.0));   // prints 4 via Oak
}
```

```sh
oakc app.oak -o app -- -lm
```

- `include "header";` emits `#include`. Bare names (`"stdio.h"`) become
  `<stdio.h>`; paths (`"./x.h"`, `"a/b.h"`) stay quoted.
- `extern fn name(args...) -> ret;` declares a C function (no body, ends
  with `;`). The compiler emits an `extern` prototype and calls it directly.
- Params/returns must be scalars: `i32`, `f64`, `bool`, `string`, `ptr`,
  or a raw `C "..."` type. Structs/arrays cannot cross the boundary.
- `ptr` holds any C pointer; `null` is a null `ptr`. Print a `ptr` with
  `print(p)` (`%p`).
- Raw C types: write `C "char *"` (any C spelling, quoted) wherever a type
  goes — e.g. `extern fn malloc(n: i32) -> C "void *";`.
- Variadic C functions: `extern fn printf(fmt: string, ...) -> i32;`.
  Extra args must be scalars and pass through as-is.

## Not supported

Pointers/references in source (use `ptr`/`null` only via FFI), generics,
free/GC (strings/arrays produced at runtime are never freed, same as before).

## Examples

- `examples/hello.oak` prints a string
- `examples/bump.oak` changes a struct through a function argument
- `examples/copy_ok.oak` shows assignment copying a struct
- `examples/bad_twice.oak` is rejected by the aliasing rule
- `examples/control_flow.oak` if/while, including the struct literal rule
- `examples/strings.oak` concatenation, comparison, escapes, struct fields
- `examples/raylib/` a full C-library wrapper (shim + `raylib.oak` + 3D demo)
- `examples/bench/bench.oak` arithmetic/loop benchmark vs. the C equivalent
