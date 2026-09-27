#include "raylib_shim.h"
#include <rlgl.h>
#include <stdlib.h>

static Color mk(int32_t r, int32_t g, int32_t b, int32_t a) {
    Color c;
    c.r = (unsigned char)r;
    c.g = (unsigned char)g;
    c.b = (unsigned char)b;
    c.a = (unsigned char)a;
    return c;
}

/* --- window / timing --- */
void oak_rl_window(int32_t w, int32_t h, OakStr title) {
    InitWindow(w, h, title);
}
void oak_rl_close(void) { CloseWindow(); }
int32_t oak_rl_should_close(void) { return WindowShouldClose() ? 1 : 0; }
void oak_rl_target_fps(int32_t fps) { SetTargetFPS(fps); }
double oak_rl_frame_time(void) { return (double)GetFrameTime(); }
double oak_rl_time(void) { return (double)GetTime(); }

/* --- 2D drawing --- */
void oak_rl_begin(void) { BeginDrawing(); }
void oak_rl_end(void) { EndDrawing(); }
void oak_rl_clear(int32_t r, int32_t g, int32_t b, int32_t a) {
    ClearBackground(mk(r, g, b, a));
}
void oak_rl_text(OakStr s, int32_t x, int32_t y, int32_t size,
                 int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawText(s, x, y, size, mk(r, g, b, a));
}
void oak_rl_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                 int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawRectangle(x, y, w, h, mk(r, g, b, a));
}
void oak_rl_circle(int32_t cx, int32_t cy, double radius,
                   int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawCircle(cx, cy, (float)radius, mk(r, g, b, a));
}
void oak_rl_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                 int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawLine(x0, y0, x1, y1, mk(r, g, b, a));
}
void oak_rl_fps(int32_t x, int32_t y) { DrawFPS(x, y); }

/* --- input --- */
int32_t oak_rl_key_down(int32_t key) { return IsKeyDown(key) ? 1 : 0; }
int32_t oak_rl_key_pressed(int32_t key) { return IsKeyPressed(key) ? 1 : 0; }
int32_t oak_rl_mouse_x(void) { return GetMouseX(); }
int32_t oak_rl_mouse_y(void) { return GetMouseY(); }
int32_t oak_rl_mouse_pressed(int32_t button) {
    return IsMouseButtonPressed(button) ? 1 : 0;
}
double oak_rl_mouse_wheel(void) { return (double)GetMouseWheelMove(); }

/* --- 3D --- */
void *oak_rl_cam_new(double px, double py, double pz,
                     double tx, double ty, double tz, double fovy) {
    Camera3D *c = (Camera3D *)malloc(sizeof(Camera3D));
    c->position = (Vector3){(float)px, (float)py, (float)pz};
    c->target = (Vector3){(float)tx, (float)ty, (float)tz};
    c->up = (Vector3){0.0f, 1.0f, 0.0f};
    c->fovy = (float)fovy;
    c->projection = CAMERA_PERSPECTIVE;
    return c;
}
void oak_rl_cam_free(void *cam) { free(cam); }
void oak_rl_begin_3d(void *cam) { BeginMode3D(*(Camera3D *)cam); }
void oak_rl_end_3d(void) { EndMode3D(); }
void oak_rl_grid(int32_t slices, double spacing) {
    DrawGrid(slices, (float)spacing);
}
void oak_rl_cube(double x, double y, double z, double w, double h, double l,
                 int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawCube((Vector3){(float)x, (float)y, (float)z},
             (float)w, (float)h, (float)l, mk(r, g, b, a));
}
void oak_rl_cube_wires(double x, double y, double z, double w, double h, double l,
                       int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawCubeWires((Vector3){(float)x, (float)y, (float)z},
                  (float)w, (float)h, (float)l, mk(r, g, b, a));
}
void oak_rl_cube_spin(double w, double h, double l, double angle,
                      double ax, double ay, double az,
                      int32_t r, int32_t g, int32_t b, int32_t a) {
    rlPushMatrix();
    rlRotatef((float)angle, (float)ax, (float)ay, (float)az);
    DrawCube((Vector3){0, 0, 0}, (float)w, (float)h, (float)l, mk(r, g, b, a));
    DrawCubeWires((Vector3){0, 0, 0}, (float)w, (float)h, (float)l, mk(20, 20, 20, 255));
    rlPopMatrix();
}
void oak_rl_sphere(double x, double y, double z, double radius,
                   int32_t r, int32_t g, int32_t b, int32_t a) {
    DrawSphere((Vector3){(float)x, (float)y, (float)z}, (float)radius,
               mk(r, g, b, a));
}
