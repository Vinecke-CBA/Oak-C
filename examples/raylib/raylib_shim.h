// C shim for the Oak raylib wrapper (examples/raylib/raylib.oak).
//
// Why this exists: Oak emits its own `extern` prototype for every `extern fn`
// using Oak's C types (int32_t = i32, double = f64, const char * = string,
// void * = ptr). Declaring raylib functions directly would collide with
// raylib.h (float vs double, Vector3 by value, Color by value, bool...).
// So Oak only declares the `oak_rl_*` wrappers below, which use exactly the
// types Oak generates, and the wrappers call real raylib here.
//
// Build (pass this .c to gcc via the `--` flag, and -lraylib):
//   oakc examples/raylib/demo_spin.oak -o demo_spin -- examples/raylib/raylib_shim.c -lraylib -lgdi32 -lwinmm
#ifndef OAK_RAYLIB_SHIM_H
#define OAK_RAYLIB_SHIM_H

#include <stdint.h>
#include "raylib.h"

typedef const char *OakStr;

/* --- window / timing --- */
void oak_rl_window(int32_t w, int32_t h, OakStr title);
void oak_rl_close(void);
int32_t oak_rl_should_close(void);
void oak_rl_target_fps(int32_t fps);
double oak_rl_frame_time(void);
double oak_rl_time(void);

/* --- 2D drawing (colors are r,g,b,a bytes) --- */
void oak_rl_begin(void);
void oak_rl_end(void);
void oak_rl_clear(int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_text(OakStr s, int32_t x, int32_t y, int32_t size,
                 int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                 int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_circle(int32_t cx, int32_t cy, double radius,
                   int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                 int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_fps(int32_t x, int32_t y);

/* --- input --- */
int32_t oak_rl_key_down(int32_t key);
int32_t oak_rl_key_pressed(int32_t key);
int32_t oak_rl_mouse_x(void);
int32_t oak_rl_mouse_y(void);
int32_t oak_rl_mouse_pressed(int32_t button);
double oak_rl_mouse_wheel(void);

/* --- 3D --- */
void *oak_rl_cam_new(double px, double py, double pz,
                     double tx, double ty, double tz, double fovy);
void oak_rl_cam_free(void *cam);
void oak_rl_begin_3d(void *cam);
void oak_rl_end_3d(void);
void oak_rl_grid(int32_t slices, double spacing);
void oak_rl_cube(double x, double y, double z, double w, double h, double l,
                 int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_cube_wires(double x, double y, double z, double w, double h, double l,
                       int32_t r, int32_t g, int32_t b, int32_t a);
/* Rotate around (ax,ay,az) by angle degrees, then draw a solid cube at origin.
   Lets Oak spin objects without touching raylib's Model/Matrix structs. */
void oak_rl_cube_spin(double w, double h, double l, double angle,
                      double ax, double ay, double az,
                      int32_t r, int32_t g, int32_t b, int32_t a);
void oak_rl_sphere(double x, double y, double z, double radius,
                   int32_t r, int32_t g, int32_t b, int32_t a);

#endif
