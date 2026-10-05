# include/

Everything in this folder is visible to every Oak program in the project, so
you can keep shared C and Oak files in one place instead of next to each
source file:

```oak
include "shout.h";                 // C header  -> #include <shout.h>, -Iinclude added for you
include "shout.c" as extern C;     // C source  -> compiled and linked automatically
import "greet.oak";                // Oak module-> parsed as part of the program
```

Search order (first hit wins):

1. next to the file that contains the `include`/`import`
2. the current working directory
3. every `-I<dir>` on the command line (or in `flags` in `oak.cfg`)
4. `include/` next to the input file
5. `include/` in the working directory
6. `include/` next to the `oakc` executable

Directories that do not exist are skipped, so a project without an
`include/` folder behaves exactly as before. Steps 3-6 are also passed to
the C compiler as `-I`, so `#include <x.h>` headers found here work too.

Try it: `oakc examples/include_dir.oak -o include_dir && ./include_dir`.

| File | Used by |
| --- | --- |
| `shout.h`, `shout.c` | `examples/include_dir.oak` (header + `as extern C`) |
| `greet.oak` | `examples/include_dir.oak` (`import`) |
| `memory.h`, `memory.c`, `memory.oak` | any program importing `memory.oak` (blocks, cells, pointers) |
| `math.oak` | any program importing `math.oak` (libm: 41 functions + `math_*` helpers; no `.c` shim needed, see its header for the tcc note) |
| `sys.c`, `sys.oak` | any program importing `sys.oak` (file open/read/write) |
