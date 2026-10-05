/* sdl2_shim.h — the C half of the Oak SDL2 wrapper (examples/sdl2/sdl2.oak).
 *
 * Why this file exists
 * --------------------
 * Oak emits its own prototype for every `extern fn`, using Oak's C types
 * (int32_t = i32, double = f64, bool = bool, const char * = string,
 * void * = ptr). Declaring SDL's functions directly would put a second,
 * incompatible declaration of the same symbol next to SDL_video.h & co:
 *
 *     error: conflicting types for 'SDL_CreateWindow'  (int32_t vs SDL_WindowFlags)
 *
 * Three things make a shim unavoidable for SDL2 specifically:
 *   1. sized enums (SDL_WindowFlags, SDL_RendererFlags, SDL_bool, ...) that
 *      Oak has no equivalent for — every Oak integer is int32_t;
 *   2. structs passed by value (SDL_Rect, SDL_Point, SDL_Event, ...), which
 *      Oak cannot build at all — it only shuttles opaque void * handles;
 *   3. <SDL2/SDL.h> #defines `main` to `SDL_main`, declared as
 *      `int SDL_main(int, char **)` — Oak's `int main(void)` would not compile
 *      in a translation unit that sees SDL.h.
 *
 * So SDL.h is included *only* by sdl2_shim.c, and Oak only ever talks to the
 * `sdl2_*` functions declared here, which use exactly the types Oak generates.
 *
 * These names ARE the Oak API: sdl2.oak declares one `extern fn` per function
 * below, so there is no second naming layer to keep in sync.
 *
 * Types used: int32_t / double / bool / const char * / void * — nothing else,
 * because those are what Oak's i32 / f64 / bool / string / ptr compile to.
 */
#ifndef OAK_SDL2_SHIM_H
#define OAK_SDL2_SHIM_H

#include <stdint.h>
#include <stdbool.h>

/* ---- core: init, quit, errors, hints, formatting ---- */
int32_t sdl2_init(int32_t flags);
void sdl2_quit(void);
int32_t sdl2_init_subsystem(int32_t flags);
void sdl2_quit_subsystem(int32_t flags);
const char *sdl2_error(void);
void sdl2_clear_error(void);
int32_t sdl2_version(void); /* major*1000000 + minor*1000 + patch */
const char *sdl2_video_driver(void);
int32_t sdl2_num_video_drivers(void);
const char *sdl2_video_driver_name(int32_t index);
void sdl2_set_hint(const char *name, const char *value);
const char *sdl2_get_hint(const char *name);
/* printf into a rotating set of static buffers, so the result can be handed
   straight to Oak as a `string` (window titles & co). 8 live at a time. */
const char *sdl2_format(const char *fmt, ...);
int32_t sdl2_num_allocations(void);

/* ---- displays ---- */
int32_t sdl2_num_displays(void);
int32_t sdl2_display_width(int32_t index);
int32_t sdl2_display_height(int32_t index);
int32_t sdl2_display_usable_width(int32_t index);  /* minus taskbar / docks */
int32_t sdl2_display_usable_height(int32_t index);
double sdl2_display_dpi(int32_t index);

/* ---- windows (void * handles) ---- */
void *sdl2_create_window(const char *title, int32_t x, int32_t y,
                         int32_t w, int32_t h, int32_t flags);
void sdl2_destroy_window(void *win);
void sdl2_set_window_title(void *win, const char *title);
const char *sdl2_window_title(void *win);
void sdl2_set_window_size(void *win, int32_t w, int32_t h);
int32_t sdl2_window_width(void *win);
int32_t sdl2_window_height(void *win);
int32_t sdl2_window_pixel_width(void *win); /* real pixels on HighDPI */
int32_t sdl2_window_pixel_height(void *win);
void sdl2_set_window_position(void *win, int32_t x, int32_t y);
int32_t sdl2_window_x(void *win);
int32_t sdl2_window_y(void *win);
int32_t sdl2_window_id(void *win);
int32_t sdl2_window_flags(void *win);
void *sdl2_window_from_id(int32_t id);
void sdl2_show_window(void *win);
void sdl2_hide_window(void *win);
void sdl2_maximize_window(void *win);
void sdl2_minimize_window(void *win);
void sdl2_restore_window(void *win);
void sdl2_raise_window(void *win);
void sdl2_set_window_bordered(void *win, bool on);
void sdl2_set_window_resizable(void *win, bool on);
void sdl2_set_window_on_top(void *win, bool on);
void sdl2_set_window_keyboard_grab(void *win, bool on);
void sdl2_set_window_mouse_grab(void *win, bool on);
void sdl2_set_window_mouse_rect(void *win, int32_t x, int32_t y,
                                int32_t w, int32_t h); /* w < 0 clears it */
void sdl2_set_window_fullscreen(void *win, int32_t flags);
void sdl2_set_window_minimum_size(void *win, int32_t w, int32_t h);
void sdl2_set_window_maximum_size(void *win, int32_t w, int32_t h);
void sdl2_set_window_opacity(void *win, double opacity); /* < 0 resets it */
bool sdl2_window_has_focus(void *win);      /* SDL_WINDOW_INPUT_FOCUS bit */
bool sdl2_window_has_mouse_focus(void *win);


/* ---- renderers ---- */
void *sdl2_create_renderer(void *win, int32_t index, int32_t flags);
void sdl2_destroy_renderer(void *ren);
const char *sdl2_renderer_name(void *ren);
int32_t sdl2_num_render_drivers(void);
const char *sdl2_render_driver_name(int32_t index);
bool sdl2_render_target_supported(void *ren);
void sdl2_set_draw_color(void *ren, int32_t r, int32_t g, int32_t b, int32_t a);
void sdl2_render_clear(void *ren);
void sdl2_render_present(void *ren);
void sdl2_render_flush(void *ren);
void sdl2_set_render_vsync(void *ren, bool on);
void sdl2_set_viewport(void *ren, int32_t x, int32_t y, int32_t w, int32_t h); /* w<0 = whole target */
void sdl2_set_logical_size(void *ren, int32_t w, int32_t h);
void sdl2_set_integer_scale(void *ren, bool on);
void sdl2_set_render_scale(void *ren, double sx, double sy);
int32_t sdl2_output_width(void *ren);
int32_t sdl2_output_height(void *ren);
void sdl2_set_render_target(void *ren, void *tex); /* null = back buffer */
void sdl2_set_blend_mode(void *ren, int32_t mode);


/* ---- immediate geometry (drawn in the current draw color) ---- */
void sdl2_draw_point(void *ren, int32_t x, int32_t y);
void sdl2_draw_line(void *ren, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void sdl2_draw_rect(void *ren, int32_t x, int32_t y, int32_t w, int32_t h);
void sdl2_fill_rect(void *ren, int32_t x, int32_t y, int32_t w, int32_t h);
void sdl2_draw_circle(void *ren, int32_t cx, int32_t cy, int32_t radius);
void sdl2_fill_circle(void *ren, int32_t cx, int32_t cy, int32_t radius);

/* ---- textures (always ARGB8888 so the byte order is portable) ---- */
void *sdl2_create_texture(void *ren, int32_t access, int32_t w, int32_t h);
void *sdl2_solid_texture(void *ren, int32_t w, int32_t h,
                         int32_t r, int32_t g, int32_t b, int32_t a);
void *sdl2_load_bmp(void *ren, const char *path); /* core SDL reads .bmp only */
void sdl2_destroy_texture(void *tex);
int32_t sdl2_texture_width(void *tex);
int32_t sdl2_texture_height(void *tex);
int32_t sdl2_set_texture_color(void *tex, int32_t r, int32_t g, int32_t b);
int32_t sdl2_set_texture_alpha(void *tex, int32_t a);
int32_t sdl2_set_texture_blend_mode(void *tex, int32_t mode);
int32_t sdl2_fill_texture(void *tex, int32_t r, int32_t g, int32_t b, int32_t a);
/* sx < 0 copies the whole texture; dw/dh <= 0 keeps the source size. */
void sdl2_copy(void *ren, void *tex, int32_t sx, int32_t sy,
               int32_t sw, int32_t sh, int32_t dx, int32_t dy,
               int32_t dw, int32_t dh);
/* cx/cy < 0 rotates around the middle of the destination rect. */
void sdl2_copy_ex(void *ren, void *tex, int32_t sx, int32_t sy,
                  int32_t sw, int32_t sh, int32_t dx, int32_t dy,
                  int32_t dw, int32_t dh, double angle,
                  int32_t cx, int32_t cy, int32_t flip);

/* ---- events: one heap SDL_Event per handle, fields read by accessor ---- */
void *sdl2_event_new(void);
void sdl2_event_free(void *ev);
void sdl2_pump_events(void);
int32_t sdl2_poll_event(void *ev);  /* 1 = filled, 0 = queue empty */
int32_t sdl2_wait_event(void *ev);
int32_t sdl2_wait_timeout(void *ev, int32_t ms);
void sdl2_flush_events(void);
int32_t sdl2_push_quit(void);
int32_t sdl2_ev_type(void *ev);
int32_t sdl2_ev_window_id(void *ev);
int32_t sdl2_ev_window_event(void *ev);
int32_t sdl2_ev_window_data1(void *ev);
int32_t sdl2_ev_window_data2(void *ev);
int32_t sdl2_ev_key_scancode(void *ev);
int32_t sdl2_ev_key_keycode(void *ev);
bool sdl2_ev_key_down(void *ev);
int32_t sdl2_ev_key_mod(void *ev);
const char *sdl2_ev_text(void *ev); /* TEXTINPUT/TEXTEDITING payload */
int32_t sdl2_ev_mouse_button(void *ev);
bool sdl2_ev_mouse_down(void *ev);
int32_t sdl2_ev_mouse_x(void *ev);
int32_t sdl2_ev_mouse_y(void *ev);
int32_t sdl2_ev_mouse_relx(void *ev);
int32_t sdl2_ev_mouse_rely(void *ev);
double sdl2_ev_wheel_x(void *ev);
double sdl2_ev_wheel_y(void *ev);
int32_t sdl2_ev_user_code(void *ev);
int32_t sdl2_ev_user_data1(void *ev);

/* ---- keyboard ---- */
bool sdl2_key_down(int32_t scancode);      /* polled snapshot, not events */
int32_t sdl2_scancode_letter(int32_t letter); /* 'a'..'z' (either case) */
int32_t sdl2_scancode_digit(int32_t digit);   /* 0..9 */
int32_t sdl2_scancode_key(int32_t index);     /* 1..12 -> F1..F12 */
const char *sdl2_scancode_name(int32_t scancode);
const char *sdl2_keycode_name(int32_t keycode);
int32_t sdl2_keycode_from_scancode(int32_t scancode);
int32_t sdl2_scancode_from_keycode(int32_t keycode);
bool sdl2_keyboard_focus(void);
void sdl2_text_input(bool on); /* TEXTINPUT events (needs IME support) */

/* ---- mouse ---- */
int32_t sdl2_mouse_x(void);
int32_t sdl2_mouse_y(void);
int32_t sdl2_mouse_global_x(void);
int32_t sdl2_mouse_global_y(void);
int32_t sdl2_mouse_buttons(void); /* SDL_BUTTON_*MASK bits */
bool sdl2_mouse_button_down(int32_t button);
void sdl2_show_cursor(bool show);
bool sdl2_cursor_shown(void);
void sdl2_set_mouse_relative(bool on);
void sdl2_warp_mouse(void *win, int32_t x, int32_t y);

/* ---- timers ---- */
double sdl2_ticks(void); /* ms since SDL_Init, as double so it never wraps */
void sdl2_delay(int32_t ms);
double sdl2_perf_counter(void);
double sdl2_perf_freq(void);
double sdl2_time(void); /* high-resolution seconds since SDL_Init */

/* ---- constants: one int32_t-returning function per SDL2 macro ---- */
#define OAK_SDL_CONST(name, macro) int32_t sdl2_##name(void);
#define OAK_SDL_HINT(name, macro) const char *sdl2_##name(void);
#include "sdl2_consts.def"
#undef OAK_SDL_CONST
#undef OAK_SDL_HINT

#endif /* OAK_SDL2_SHIM_H */

