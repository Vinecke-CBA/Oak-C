# Oak raylib wrapper

`raylib.oak` is an Oak wrapper around raylib 5 (window, timing, 2D drawing,
input, basic 3D). It uses only existing Oak FFI — no compiler/language
changes.

Files:
- `raylib.oak` — the wrapper: `extern fn oak_rl_*` declarations + friendly
  `rl_*` helpers (colors default to alpha 255, keys as functions, `f64`
  for floats).
- `raylib_shim.h/.c` — tiny C shim. Needed because Oak emits its own
  `extern` prototypes with Oak's C types (`int32_t`, `double`, `const char *`,
  `void *`), which would collide with `raylib.h` (`float`, `Vector3`/`Color`
  by value, ...). Oak only ever declares the `oak_rl_*` names; the shim calls
  real raylib.
- `demo_spin.oak` — rotating 3D cube + orbiting sphere + grid demo.

## Build & run (from the repo root)

```
oakc examples/raylib/demo_spin.oak -o demo_spin -- examples/raylib/raylib_shim.c -Iexamples/raylib -lraylib -lgdi32 -lwinmm -lm
demo_spin
```

`--` forwards everything after it to gcc: the shim source, `-I` so the
generated `#include <raylib_shim.h>` resolves, and the raylib/link flags.

## API quick tour

```oak
import "raylib.oak";

fn main() {
    rl_window(800, 450, "hi");
    rl_target_fps(60);
    var cam = rl_camera(6.0, 5.0, 6.0, 0.0, 1.0, 0.0, 45.0);
    while !rl_should_close() {
        rl_begin();
        rl_clear(24, 26, 38);
        rl_begin_3d(cam);
        rl_grid(10, 1.0);
        rl_cube_spin(2.0, 2.0, 2.0, rl_time() * 60.0, 0.0, 1.0, 0.0, 86, 156, 214);
        rl_end_3d();
        rl_text("text", 10, 10, 20, 255, 255, 255);
        rl_end();
    }
    rl_camera_free(cam);
    rl_close();
}
```

Covered: window/timing (`rl_window`, `rl_should_close`, `rl_target_fps`,
`rl_frame_time`, `rl_time`), 2D (`rl_begin/end`, `rl_clear`, `rl_text`,
`rl_rect`, `rl_circle`, `rl_line`, `rl_fps`), input (`rl_key_down`,
`rl_key_pressed`, `rl_mouse_*`, `rl_mouse_wheel`, key-code helpers like
`rl_key_escape()`), 3D (`rl_camera`, `rl_begin_3d/end_3d`, `rl_grid`,
`rl_cube`, `rl_cube_wires`, `rl_cube_spin`, `rl_sphere`).

Colors are `(r, g, b)` byte tuples; objects use `f64` coordinates.
Raylib structs (`Vector3`, `Camera3D`, `Color`, `Matrix`, models/meshes)
stay on the C side behind `ptr` handles — extend `raylib_shim.c` for
anything else you need.
