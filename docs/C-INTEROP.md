# Using C Libraries from Oak (FFI Guide)

Oak has no package manager — its interop story *is* its ecosystem: call any
C library directly. This guide goes from "call `printf`" to "wrap SDL2 like
`examples/` does", plus the rules, the traps and the debugging workflow.

## Table of contents

1. [The mental model](#1-the-mental-model)
2. [Type mapping](#2-type-mapping)
3. [`include` — pulling in headers](#3-include--pulling-in-headers)
4. [`extern fn` — declaring functions](#4-extern-fn--declaring-functions)
5. [Why prototypes clash, and the shim pattern](#5-why-prototypes-clash-and-the-shim-pattern)
6. [Passing structs, arrays, callbacks](#6-passing-structs-arrays-callbacks)
7. [Worked example 1: libc (`printf`, `math.h`)](#7-worked-example-1-libc)
8. [Worked example 2: wrapping a full library (raylib)](#8-worked-example-2-wrapping-a-full-library-raylib)
9. [Linking: forwarding flags to gcc](#9-linking-forwarding-flags-to-gcc)
10. [Debugging FFI problems](#10-debugging-ffi-problems)
11. [Shim authoring checklist](#11-shim-authoring-checklist)
12. [Memory: ptr, defer and include/memory.oak](#12-memory-ptr-defer-and-includememoryoak)

---

## 1. The mental model

The Oak compiler does **not** parse C headers. Your declaration in Oak *is*
the contract:

1. You `include "some.h";` so the **generated C file** contains that
   `#include` (the real definitions land there).
2. You declare `extern fn name(...) -> ret;`. The compiler emits its own
   `extern <ret> <name>(...);` prototype built from **Oak's C type spellings**
   and calls the function like a normal C call — no dlopen, no trampolines,
   zero overhead.
3. You forward the library to `gcc` at link time (`-- -lfoo`).

Therefore two failure modes exist, and everything in this guide helps you
avoid them:

- **Prototype clash** — your emitted declaration contradicts the real
  header (`conflicting types for 'SDL_Init'`). Fix: don't declare the real
  name; declare a `oak_*` shim (§5).
- **Link error** — `undefined reference to 'foo'`. Fix: `-- -lfoo` (§9).

## 2. Type mapping

| Oak type | Emitted C | Use for |
| --- | --- | --- |
| `i8` `u8` | `int8_t` `uint8_t` | bytes, small flags |
| `i16` `u16` | `int16_t` `uint16_t` | 16-bit C types |
| `i32` | `int32_t` | `int`, `Uint32`-as-value, flags, counts, bool-like C ints |
| `u32` | `uint32_t` | `unsigned`, `size_t` on 32-bit, bit masks |
| `i64` | `int64_t` | `long long`, `int64_t`, sizes and timestamps |
| `u64` | `uint64_t` | `size_t` on 64-bit, `uint64_t`, full-range masks |
| `f64` | `double` | `double`; **not** `float` (declare shim or accept promotion) |
| `bool` | `bool` (`<stdbool.h>`) | `_Bool` returns; C's `int` bools better as `i32` |
| `string` | `const char *` (typedef `OakStr`) | `const char *` params — string **literals pass fine** |
| `ptr` / `ptr<T>` | `void *` | Any C pointer/handle; compare with `null` |
| `()` return | `void` | Functions whose return you ignore |
| `C "text"` | `text` verbatim | Everything else: `char *`, `float`, `struct Foo *`, ... |

Rules:

- **Structs and arrays cannot cross the boundary.** `extern fn` params and
  returns must be *scalarish*: any integer type, `f64`, `bool`, `string`,
  `ptr`, `C"..."`.
  (`extern fn parameters must be scalars...`)
- **Every pointer is `void *` in the header too.** Oak has exactly one pointer
  spelling, so a C prototype that says `const void *` or `int32_t *` will not
  match the `extern` declaration Oak emits and C will reject the mismatch.
  Take a narrower `T *` in a `oak_*` shim instead, and hand Oak a `ptr`.
- Numbers convert to each other implicitly (C's usual arithmetic
  conversions), so a `u32` argument satisfies a `C "size_t"` parameter in the
  generated C. Only literals are range-checked.
- `f64` is `double`. If the C function takes `float`, either declare the
  param `C "float"` (only usable as param/return text) or shim it with a
  `double`-taking wrapper.
- Strings handed *into* C must not be mutated — Oak string literals are
  `const char *`; a C function that writes into them is UB. Copy first
  (via shim) if a library needs a mutable buffer.
- Strings *returned* by C are adopted as-is. If the library returns a
  pointer into static storage (like `strerror`), don't `free` it and don't
  keep it after the next call. If it returns malloc'd memory, Oak won't
  free it — call `free` yourself via `extern fn` if it matters.

## 3. `include` — pulling in headers and C sources

`include` has three forms:

```oak
// 1. C header emitted in generated C file
include "stdio.h";      // -> #include <stdio.h>
include "math.h";       // -> #include <math.h>

// 2. C source file compiled & linked automatically (no -- compiler flags required!)
include "example.c" as extern C;

// 3. Oak module imported safely (alias for `import "example.oak";`)
include "example.oak" as Oak;
```

### Where these names are searched

All three forms — and `import` — resolve names in the same order, first hit
wins:

1. next to the file that contains the statement
2. the current working directory
3. every `-I<dir>` passed to `oakc` (command line or `flags` in `oak.cfg`)
4. `include/` next to the input file
5. `include/` in the working directory
6. `include/` next to the `oakc` executable

Directories that do not exist are skipped, so a project without an
`include/` folder behaves exactly as it did before. Steps 3-6 are also
handed to the C compiler as `-I`, which is what lets a bare header name
work with no flags at all:

```sh
# include/mylib.h, include/mylib.c and include/util.oak all exist
oakc app.oak -o app        # -Iinclude is added to the gcc line for you
```

Keep shared project files in `include/` and the command line stays clean.
See `include/README.md` and `examples/include_dir.oak`.

### Form 1: `include "header.h";`
- Emitted **once**, de-duplicated, at the top of the generated C file,
  after the compiler's own six includes (`stdio stdint stdlib string
  limits stdbool` — so `printf`, `malloc`, `memcpy` already exist without
  any `include`).
- Classification is **syntactic**: a header string with no `/`, no `\` and
  not starting with `.` becomes `<...>`; anything else stays `"..."`.
  - `stdio.h` → `<stdio.h>` ✓ system header
  - `"raylib_shim.h"` → `<raylib_shim.h>` ← **angle brackets even though
    it's your file!** Angle includes only search `-I` paths, so you must
    pass `-Ipath/to/dir` on the gcc line (§9). Alternatively give the
    include a path form: `include "./raylib_shim.h";` keeps quotes and
    resolves relative to the generated `.c` file's directory. A header in
    the project's `include/` needs no flag at all: `-Iinclude` is added
    for you automatically.
- `include` also has **no effect on Oak itself** — it is purely a C
  preprocessor directive for the generated file. Types from the header
  are unknown to Oak unless you use raw `C "..."` spellings.

### Form 2: `include "file.c" as extern C;`
- Resolved relative to the including Oak file (or CWD), then the include
  search path: every `-I<dir>` and the project `include/` folder.
- Automatically compiled and linked with the program by the C compiler (GCC, TCC, Clang).
- Deduplicated via canonical path resolution across all files in the project.
- No manual compiler command-line flags or `--` arguments needed.

### Form 3: `include "file.oak" as Oak;`
- Exactly equivalent to `import "file.oak";`.
- Cycle-safe: circular inclusions (e.g. A includes B, B includes A) terminate cleanly and deduplicate definitions via canonical paths.

## 4. `extern fn` — declaring functions

```oak
extern fn puts(s: string) -> i32;              // int puts(const char *)
extern fn sqrt(x: f64) -> f64;                 // double sqrt(double)
extern fn SDL_Quit();                          // void (omit ->)
extern fn printf(fmt: string, ...) -> i32;     // variadic
extern fn atexit(cb: C "void (*)(void)") -> i32;  // function pointer via raw type
```

Grammar: `extern fn name(p: T, ...) -> R;` — top level, **ends with `;`,
no body**. `...` may appear at the end of the parameter list (extern only)
and means "extra scalar arguments pass through untouched".

Checks performed by Oak (errors are yours to fix *before* linking):

- Every parameter must be scalarish (`i32 f64 bool string ptr C"..."`),
  and the return likewise (or omitted). Arrays/structs/`()` as *parameter*
  are rejected: `extern fn parameters must be scalars, but 'x' is T[]`.
- Arity: too few/too many fixed args is an error; with `...`, **at least**
  the fixed count, extra args must be scalars
  (`these extra arguments must be scalars, got T`).
- You may not define a body, and externs are exempt from the aliasing rule
  (they never mutate Oak structs — they can't receive one).
- Oak emits `extern <C-ret> <name>(<C-params>);` for every extern — that is
  exactly why header clashes happen (§5).

Calling conventions/ABI are plain C: `i32`→`int` (32-bit in eax),
`f64`→xmm0, `string/ptr`→register pointer. Win64 vs SysV differences are
gcc's problem, not yours — always link through gcc.

## 5. Why prototypes clash, and the shim pattern

**The problem.** If you write:

```oak
include "SDL2/SDL.h";
extern fn SDL_Init(flags: i32) -> i32;
```

the generated C contains both SDL's real prototype
`int SDL_Init(Uint32)` and Oak's `int32_t SDL_Init(int32_t)`. GCC compares
*whole translation units*: `conflicting types for 'SDL_Init'` (and worse,
`Uint32` vs `int32_t`, `void` vs `int` return for `SDL_Quit`, `SDL_Window *`
vs `void *`, `SDL_main` redefining `main`, `float` vs `double`, by-value
`Vector3`/`Color` structs...). It will not compile.

**The rule: never declare the library's real function names in Oak if you
`include` its header.** Instead declare *your own* names:

```c
/* my_shim.h — signatures only use Oak's emitted spellings */
#ifndef MY_SHIM_H
#define MY_SHIM_H
#include <stdint.h>
typedef const char *OakStr;        /* matches codegen's typedef */
int32_t  oak_sdl_init(void);
void    *oak_sdl_window(OakStr title, int32_t w, int32_t h);
double   oak_rl_time(void);
#endif
```

```c
/* my_shim.c — the only place the real header appears */
#include "SDL2/SDL.h"
int32_t oak_sdl_init(void) { return (int32_t)SDL_Init(SDL_INIT_VIDEO); }
void *oak_sdl_window(OakStr t, int32_t w, int32_t h) {
    return SDL_CreateWindow(t, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
}
```

```oak
include "my_shim.h";            // or "./my_shim.h" to keep quotes
extern fn oak_sdl_init() -> i32;
extern fn oak_sdl_window(title: string, w: i32, h: i32) -> ptr;
```

Build: `oakc app.oak -o app -- my_shim.c -IMyDir -lSDL2`.

Why this always works: shim signatures use *exactly* the spellings Oak
emits (`int32_t`, `double`, `const char *`, `void *`), so Oak's prototype
and your header agree byte-for-byte (C11 allows the duplicate `OakStr`
typedef; identical redeclaration of a function is fine). The real library
header is only ever seen by `my_shim.c`, compiled by gcc directly.

This is not optional style — it is **the** mechanism behind the working
`examples/raylib` wrapper. See §8.

**Variadics exception:** functions like `printf` that you *do not* include
a conflicting header for can be declared directly, because Oak's emitted
prototype is compatible (`int printf(const char *, ...)`); `stdio.h` is
already included by the generated file and matches. Use `...` for the tail.

## 6. Passing structs, arrays, callbacks

**Structs by value are impossible** (`extern fn parameters must be
scalars`). Three patterns, in order of preference:

1. **Opaque handle + accessors** (what SDL/raylib wrappers do): C side
   owns the struct, returns `void *`, exposes field readers as functions.

   ```c
   Camera3D *oak_rl_cam_new(double px, ..., double fovy);  /* mallocs */
   void      oak_rl_begin_3d(void *cam);                   /* derefs */
   ```

2. **Field-by-field parameters**: for small POD structs, take the fields as
   `i32`/`f64` and let the shim assemble the struct:

   ```oak
   extern fn oak_rl_rect(x: i32, y: i32, w: i32, h: i32) -> ptr;
   ```

3. **Raw buffer pointer**: if both sides agree, allocate the byte layout
   yourself (`C "..."` type + extern `malloc`) — last resort, easy to get
   wrong across compilers.

**Arrays** likewise cannot cross (`T[]` is an Oak-internal `OakArr *`).
Pass `ptr` to the underlying buffer instead — extend the shim with a getter
(`void *oak_buf(void *arr)`), or design the C API around lengths:
`oak_write(const int32_t *data, int32_t n)` and add an `arr_data` helper to
your shim if you need it.

**Callbacks** (qsort-style function pointers): Oak has no first-class
functions. Two workarounds:

- **Poll instead of push**: expose an API that *asks* Oak for data each
  frame/step (this is why game loops work: `while sdl_poll(...)`) — by far
  the most common shape.
- **Trampoline in the shim**: the shim owns a `static` hook that calls back
  into Oak via a fixed Oak function you declare:

  ```c
  static int g_hook;
  void oak_set_hook(int32_t fn_id) { g_hook = fn_id; }
  void bridge(void) { if (g_hook) oak_my_callback(); } /* oak_my_callback is extern fn */
  ```

  Do this only with Oak functions that have scalar signatures.

**Function pointers as data** are representable as raw types for storage/
pass-through, but you cannot *call* them from Oak (`(*p)(x)` doesn't parse)
— invoke them only inside the shim.

## 7. Worked example 1: libc

No shim needed: system headers' prototypes match Oak's spellings well
enough for these.

```oak
include "stdio.h";
include "math.h";

extern fn printf(fmt: string, ...) -> i32;
extern fn puts(s: string) -> i32;
extern fn sqrt(x: f64) -> f64;
extern fn fabs(x: f64) -> f64;
extern fn strlen(s: string) -> C "unsigned long long";

fn main() {
    printf("x=%d y=%.2f s=%s\n", 42, 3.5, "hi");   // varargs, scalars only
    puts("one line");
    print(sqrt(16.0));                              // 4
    print(fabs(-2.5));                              // 2.5
    print(len("hello"));                            // Oak's len, not strlen
    var n = strlen("hello");                        // raw type value
    printf("%llu\n", n);
}
```

Build: `oakc demo.oak -o demo` (math symbols are inlined/`-lm` only needed
for non-inlined cases on some platforms: `oakc demo.oak -o demo -- -lm`).

Notes:

- `printf`'s extra arguments must be scalars (`i32/f64/string/ptr/bool`);
  no arrays/structs — same restriction as everywhere.
- Format/arg mismatch is **your** responsibility (C's too): gcc can't see
  through Oak's call. `%d`↔`i32`, `%f/%g`↔`f64`, `%s`↔`string`,
  `%llu`↔`C "unsigned long long"`.
- `main` uses `if/else` structure, no bare `return;` (LANGUAGE §16).

## 8. Worked example 2: wrapping a full library (raylib)

The production pattern, as used by `examples/raylib/`. It is the recipe for
*any* library — SDL, zlib, curl, OpenGL, a C library of your own:

**Step 1 — decide the Oak-facing API** (ergonomic, scalar-only):
`rl_window(w,h,title)`, `rl_begin_3d(cam)`, `rl_cube_spin(...)`, ...
Everything Oak-visible is `i32/f64/string/ptr`; every library struct stays
in C behind a `ptr`.

**Step 2 — write `mylib_shim.h`** with `oak_*` prototypes spelled exactly
as Oak emits them:

```c
#ifndef OAK_RAYLIB_SHIM_H
#define OAK_RAYLIB_SHIM_H
#include <stdint.h>
#include "raylib.h"            /* real header, seen ONLY by shim TUs */
typedef const char *OakStr;    /* identical to codegen's typedef */

void   oak_rl_window(int32_t w, int32_t h, OakStr title);
double oak_rl_time(void);
void  *oak_rl_cam_new(double px, double py, double pz,
                      double tx, double ty, double tz, double fovy);
void   oak_rl_begin_3d(void *cam);
void   oak_rl_cube_spin(double w, double h, double l, double angle,
                        double ax, double ay, double az,
                        int32_t r, int32_t g, int32_t b, int32_t a);
#endif
```

**Step 3 — write `mylib_shim.c`**: convert types both ways (`(double)`
↔ `(float)` casts, `mk(r,g,b)` building a `Color`), own allocation for
returned handles (`malloc` a `Camera3D`), free functions as needed:

```c
#include "mylib_shim.h"
void *oak_rl_cam_new(double px, double py, double pz,
                     double tx, double ty, double tz, double fovy) {
    Camera3D *c = malloc(sizeof *c);
    c->position = (Vector3){(float)px, (float)py, (float)pz};
    c->target   = (Vector3){(float)tx, (float)ty, (float)tz};
    c->up       = (Vector3){0, 1, 0};
    c->fovy     = (float)fovy;
    c->projection = CAMERA_PERSPECTIVE;
    return c;
}
```

**Step 4 — write `mylib.oak`**: `include` the shim header, declare the
`extern fn`s, wrap them in friendlier Oak functions (default alpha `255`,
`bool` from `i32`, key-code helpers, resource cleanup bundles).

**Step 5 — a demo** that `import`s your wrapper, plus a build line:

```sh
oakc examples/raylib/demo_spin.oak -o demo_spin \
     -- examples/raylib/raylib_shim.c -Iexamples/raylib -lraylib -lgdi32 -lwinmm -lm
```

Design points worth copying:

- **Constructors/destructors** come in pairs returning/taking `ptr`
  (`oak_rl_cam_new` / `oak_rl_cam_free`); Oak calls them explicitly — no
  finalizers exist.
- **Rotation/transform math that needs `Matrix`** goes in the shim
  (`rlPushMatrix/rlRotatef` around `DrawCube`) instead of exposing matrices.
- **Event polling** is `i32`-returning (`oak_rl_poll(ev) -> i32`) with an
  event-object `ptr` allocated once — the classic poll loop, no callbacks.
- **Constants** (key codes, event kinds) are Oak functions returning `i32`
  (`fn rl_key_escape() -> i32 { return 256; }`) because Oak has no globals.
- Guard everything with include guards; keep the shim header self-contained
  (`stdint.h` first — `raylib.h`/SDL headers don't guarantee it).

## 9. Linking: forwarding flags to gcc

Oak's own CLI is tiny; everything after `--` goes verbatim onto the `gcc`
command that compiles the generated C:

```
oakc prog.oak -o prog -- <anything gcc understands>
gcc -std=c99 -O2 -o prog prog.oak.c <anything gcc understands>
```

Recognized **before** the input file (also forwarded): `-l -L -I -D -O -W
-f -m -std...`. Everything else before `--` must be an Oak flag or it's
`unknown option`.

Common recipes:

| Goal | Build line tail |
| --- | --- |
| libm | `-- -lm` (gcc/clang; **not** TCC on Windows — it has no separate libm) |
| shim source + header dir + lib | `-- mylib_shim.c -Ipath/to/dir -lmylib` |
| SDL2 (MinGW) | `-- mylib/mylib_shim.c -Imylib -lSDL2` |
| raylib (MinGW) | `-- examples/raylib/raylib_shim.c -Iexamples/raylib -lraylib -lgdi32 -lwinmm -lm` |
| pkg-config flags | `-- $(pkg-config --cflags --libs foo)` (shell expands first) |
| debuggable build | `oakc p.oak --emit-c p.c` then edit/compile manually |
| TinyCC | `oakc p.oak -o p --cc tcc -- mylib_shim.c -Imylib` |

The last row is the same shim workflow with the bundled TCC as the backend
(`--cc tcc`, or `cc = tcc` in `oak.cfg`). Verified working: the generated C,
libc, and shim `.c` files that use standard headers — that is everything on
this page except third-party link libraries.

Measured TCC limits (not guesses):

- **No separate libm on Windows** — `-lm` fails with `library 'm' not found`.
- **MinGW-built archives are unreadable** — linking a MinGW `libraylib.a`
  gives `error: invalid object file`. Keep gcc for such a project, or build
  the library from source with TCC.
- **MinGW's header tree breaks TCC** — adding `-IC:/msys64/mingw64/include`
  makes TCC pick up GCC-only CRT headers, which hard-error with
  `#error VARARGS not implemented for this compiler`. Point `-I` at a
  directory containing just the library's own headers instead: raylib's
  `raylib.h` + `rlgl.h` + `raymath.h` then compile cleanly under TCC.

Details that bite:

- **Join your flags**: write `-lm`, not `-l m` (the two-token form makes
  `m` look like a second input file). Same for `-I dir` → `-Idir`.
- **Shim `.c` files are ordinary translation units** — pass their paths
  after `--` and gcc compiles+links them together with the generated C.
- **`-I` matters for angle includes**: bare `include "x.h"` becomes
  `#include <x.h>`, which only searches `-I` paths (§3). Use
  `include "./x.h";` if you want quote-include behaviour instead.
- **DLLs**: linking `-lfoo` against an import library doesn't ship the DLL;
  the `.dll` must be findable at runtime (same dir as the exe or on PATH).
- The generated file is deleted after a successful link unless `--keep-c`
  or `--emit-c` — always use `--emit-c` while debugging FFI.

## 10. Debugging FFI problems

Workflow: **`--emit-c out.c` → read the C → compile it yourself with the
same flags → fix**.

```sh
oakc prog.oak -o prog --emit-c prog.c -- myshim.c -Imydir -lfoo
gcc -std=c99 -O2 -o prog prog.c myshim.c -Imydir -lfoo   # reproduces gcc's full error
```

| Symptom | Diagnosis | Fix |
| --- | --- | --- |
| `conflicting types for 'X'` | Oak's emitted prototype ≠ header's prototype (signature clash of *real* name) | Rename to `oak_*` shim (§5) |
| `'X' redeclared as different kind of symbol` / `SDL_main` | Header redefines `main` (SDL) | `#define SDL_MAIN_HANDLED` in the shim header before the include; keep `main` out of Oak-visible names |
| `implicit declaration of 'X'` | Function used in shim but its header not included *there* | Include the real header in `mylib_shim.c` |
| `unknown type name 'int32_t'` in your shim | Header included before `<stdint.h>` | `#include <stdint.h>` first in the shim header |
| `fatal error: x.h: No such file` / `#include <x.h>` not found | Bare include became angle form; no `-I` | Add `-I<dir>` after `--`, or `include "./x.h";` |
| `undefined reference to 'X'` | Declaration ok, library not linked | Add `-lX` (and `-L<path>` for custom prefixes) |
| `undefined reference to 'X__imp_...'` / wrong arch | Linking 64-bit code against 32-bit lib (or vice versa) | Match toolchain: same gcc arch for headers, libs, DLLs |
| `gcc failed` with no useful line | Rare: cmd buffer/paths with quotes | Simplify paths; keep flags short |
| Program compiles, crashes at first FFI call | Wrong DLL version at runtime / handle misuse | Check DLL next to exe; print `ptr` results vs `null` |
| Values slightly wrong (`float` args) | Declared `f64` but C wants `float` | Shim takes `double`, casts `(float)` inside |
| Garbage from a vararg call | printf format/arg mismatch | Match `%` specifiers to `i32/f64/string/ptr` exactly |

Runtime debugging is ordinary C debugging: `--emit-c` the file, `gcc -g`,
break in gdb. Oak adds no indirection — if the C is wrong, find which
`extern fn`/raw type produced it.

## 11. Shim authoring checklist

Before calling a wrapper done:

- [ ] No Oak `extern fn` declares a **real library function name** when
      that library's header is included anywhere in the generated TU.
- [ ] Shim header: include guard, `#include <stdint.h>` first, real header
      after, `typedef const char *OakStr;` (identical to codegen's).
- [ ] Every shim signature uses only: `int32_t`, `double`, `OakStr`,
      `void *`, and `void` returns — the exact spellings Oak emits.
      (If you must deviate, the *header* and the *Oak declaration* must
      still agree with each other AND with nothing else in the TU.)
- [ ] Float conversions happen inside the shim (`(float)` casts); colors,
      vectors, matrices assembled there, never passed by value from Oak.
- [ ] Pointers returned to Oak were allocated somewhere that has a matching
      `_free`; `null` returns are checked in Oak (`if p == null`).
- [ ] String pointers: who owns them? (Literals: no free. malloc'd: caller
      frees via `extern fn free(...)` or shim `_free`.)
- [ ] Handles/events allocated once and freed once; no hidden global state
      without an explicit `oak_*_init`.
- [ ] Build line passes: shim `.c`, `-I` dir (if bare include used), and
      every `-l` — verified from a clean directory.
- [ ] Demo exercises: init → one frame/call → cleanup path; compile with
      `--emit-c` once to eyeball the generated prototypes.

## 12. Memory: `ptr`, `defer` and `include/memory.oak`

The FFI needs somewhere to put bytes, and C libraries hand back pointers to
their own data. Oak has both halves of that:

```oak
var x: i32 = 42;
var p: ptr = ptr(x);   // ptr(x)  ->  &x      (emitted as void * = (&x);)
print(defer(p));        // defer(p) -> *x      (emitted as (*(int32_t *)(p)))
defer(p) = 7;           // and it is a place, so it can be assigned
```

A bare `ptr` is a `void *`: it accepts and returns any C pointer, which is
what makes it usable straight from `extern fn`. `ptr<T>` additionally records
the pointee so `defer` knows what to load; the type is written out at each
`defer` in the generated C, and `defer(p) = v` emits
`(*(int32_t *)(p)) = v`.

What is deliberately missing: pointer arithmetic (`p + 1`), `&x` as an
operator, and dereferencing a bare `ptr`. Offsets are the C side's job.

### include/memory.oak

`include/memory.oak` is a ready-made library built on exactly that, and it
pulls in its own C implementation, so one import is all a program needs:

```oak
import "memory.oak";

var buf: ptr = mem_alloc(8);      // malloc, with the size remembered
if mem_is_null(buf) { return; }   // null is the failure signal
mem_write(buf, 0, 64, 1234567890123);
print(mem_read(buf, 0, 64));      // 1234567890123
print(mem_read(buf, 8, 64));      // -1: past the end, not a crash
mem_free(buf);                    // null-safe, so cleanup needs no guard
```

The three files are the usual interop trio:

| File | Role |
| --- | --- |
| `include/memory.h` | prototypes, written in the exact types Oak emits (`void *`, `uint64_t`, ...) |
| `include/memory.c` | the implementation: a header-per-block allocator, bounds-checked cell reads/writes, and an opaque growable buffer |
| `include/memory.oak` | the Oak API (`mem_*`, `buf_*`, `swap_i64`) and the `extern fn` declarations |

Because each block remembers its own size, `mem_size` is exact and
`mem_read`/`mem_write` can refuse an out-of-range cell instead of trusting the
offset � the one place where a memory library can be honest about safety
while still being a thin wrapper.

`examples/memory_ptr.oak` exercises all of it: blocks, pointers into Oak
variables, the wide integer types, and the buffer handle.

