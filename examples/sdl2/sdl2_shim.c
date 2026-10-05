/* sdl2_shim.c — SDL2 translated behind Oak-friendly signatures.
 *
 * This is the ONLY translation unit that includes <SDL2/SDL.h>. SDL's own
 * headers would fight the prototypes Oak generates for `extern fn` (sized
 * enums, structs by value) and SDL.h also redefines `main`, which would break
 * the `int main(void)` Oak emits. Everything below takes and returns plain
 * int32_t / double / bool / const char * / void *, exactly like the
 * `extern fn` declarations in sdl2.oak, so the two sides always agree.
 *
 * Build: sdl2.oak pulls this file in with `include "sdl2_shim.c" as extern C;`
 * so you never pass it to the compiler by hand; -lSDL2 still comes from `--`
 * (or oak.cfg) because that flag belongs to your machine, not to this folder.
 */
#include "sdl2_shim.h"

#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDL2 uses Uint8 for colors; Oak hands us i32, so clamp instead of wrapping. */
static Uint8 byte_(int32_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (Uint8)v;
}

/* ---------------------------- core ---------------------------- */

int32_t sdl2_init(int32_t flags) { return SDL_Init((Uint32)flags); }
void sdl2_quit(void) { SDL_Quit(); }
int32_t sdl2_init_subsystem(int32_t flags) { return SDL_InitSubSystem((Uint32)flags); }
void sdl2_quit_subsystem(int32_t flags) { SDL_QuitSubSystem((Uint32)flags); }
const char *sdl2_error(void) { return SDL_GetError(); }
void sdl2_clear_error(void) { SDL_ClearError(); }

int32_t sdl2_version(void) {
    return SDL_MAJOR_VERSION * 1000000 + SDL_MINOR_VERSION * 1000 + SDL_PATCHLEVEL;
}

const char *sdl2_video_driver(void) {
    const char *d = SDL_GetCurrentVideoDriver();
    return d ? d : "";
}

int32_t sdl2_num_video_drivers(void) { return SDL_GetNumVideoDrivers(); }

const char *sdl2_video_driver_name(int32_t index) {
    const char *n = SDL_GetVideoDriver(index);
    return n ? n : "";
}

void sdl2_set_hint(const char *name, const char *value) {
    if (name) SDL_SetHint(name, value ? value : "");
}

const char *sdl2_get_hint(const char *name) {
    const char *v = name ? SDL_GetHint(name) : NULL;
    return v ? v : "";
}

const char *sdl2_format(const char *fmt, ...) {
    static char bufs[8][512];
    static int slot = 0;
    char *out = bufs[slot];
    size_t cap = sizeof(bufs[0]);
    va_list ap;
    slot = (slot + 1) % 8;
    if (!fmt) {
        out[0] = '\0';
        return out;
    }
    va_start(ap, fmt);
    vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    out[cap - 1] = '\0';
    return out;
}

int32_t sdl2_num_allocations(void) { return SDL_GetNumAllocations(); }

/* --------------------------- displays -------------------------- */

int32_t sdl2_num_displays(void) { return SDL_GetNumVideoDisplays(); }

int32_t sdl2_display_width(int32_t index) {
    SDL_DisplayMode m;
    if (SDL_GetCurrentDisplayMode(index, &m) != 0) return 0;
    return m.w;
}

int32_t sdl2_display_height(int32_t index) {
    SDL_DisplayMode m;
    if (SDL_GetCurrentDisplayMode(index, &m) != 0) return 0;
    return m.h;
}

double sdl2_display_dpi(int32_t index) {
    float dpi = 0.0f;
    if (SDL_GetDisplayDPI(index, NULL, &dpi, NULL) != 0) return 0.0;
    return (double)dpi;
}

int32_t sdl2_display_usable_width(int32_t index) {
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(index, &r) != 0) return 0;
    return r.w;
}

int32_t sdl2_display_usable_height(int32_t index) {
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(index, &r) != 0) return 0;
    return r.h;
}

/* ---------------------------- windows -------------------------- */

void *sdl2_create_window(const char *title, int32_t x, int32_t y,
                         int32_t w, int32_t h, int32_t flags) {
    return SDL_CreateWindow(title ? title : "", x, y, w, h, (Uint32)flags);
}

void sdl2_destroy_window(void *win) {
    if (win) SDL_DestroyWindow((SDL_Window *)win);
}

void sdl2_set_window_title(void *win, const char *title) {
    if (win) SDL_SetWindowTitle((SDL_Window *)win, title ? title : "");
}

const char *sdl2_window_title(void *win) {
    if (!win) return "";
    return SDL_GetWindowTitle((SDL_Window *)win);
}

void sdl2_set_window_size(void *win, int32_t w, int32_t h) {
    if (win) SDL_SetWindowSize((SDL_Window *)win, w, h);
}

int32_t sdl2_window_width(void *win) {
    int w = 0, h = 0;
    if (win) SDL_GetWindowSize((SDL_Window *)win, &w, &h);
    return w;
}

int32_t sdl2_window_height(void *win) {
    int w = 0, h = 0;
    if (win) SDL_GetWindowSize((SDL_Window *)win, &w, &h);
    return h;
}

int32_t sdl2_window_pixel_width(void *win) {
    int w = 0, h = 0;
    if (win) SDL_GetWindowSizeInPixels((SDL_Window *)win, &w, &h);
    return w;
}

int32_t sdl2_window_pixel_height(void *win) {
    int w = 0, h = 0;
    if (win) SDL_GetWindowSizeInPixels((SDL_Window *)win, &w, &h);
    return h;
}

void sdl2_set_window_position(void *win, int32_t x, int32_t y) {
    if (win) SDL_SetWindowPosition((SDL_Window *)win, x, y);
}

int32_t sdl2_window_x(void *win) {
    int x = 0, y = 0;
    if (win) SDL_GetWindowPosition((SDL_Window *)win, &x, &y);
    return x;
}

int32_t sdl2_window_y(void *win) {
    int x = 0, y = 0;
    if (win) SDL_GetWindowPosition((SDL_Window *)win, &x, &y);
    return y;
}

int32_t sdl2_window_id(void *win) {
    if (!win) return 0;
    return (int32_t)SDL_GetWindowID((SDL_Window *)win);
}

int32_t sdl2_window_flags(void *win) {
    if (!win) return 0;
    return (int32_t)SDL_GetWindowFlags((SDL_Window *)win);
}

void *sdl2_window_from_id(int32_t id) {
    return SDL_GetWindowFromID((Uint32)id);
}

void sdl2_show_window(void *win) { if (win) SDL_ShowWindow((SDL_Window *)win); }
void sdl2_hide_window(void *win) { if (win) SDL_HideWindow((SDL_Window *)win); }
void sdl2_maximize_window(void *win) { if (win) SDL_MaximizeWindow((SDL_Window *)win); }
void sdl2_minimize_window(void *win) { if (win) SDL_MinimizeWindow((SDL_Window *)win); }
void sdl2_restore_window(void *win) { if (win) SDL_RestoreWindow((SDL_Window *)win); }
void sdl2_raise_window(void *win) { if (win) SDL_RaiseWindow((SDL_Window *)win); }

void sdl2_set_window_bordered(void *win, bool on) {
    if (win) SDL_SetWindowBordered((SDL_Window *)win, on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_set_window_resizable(void *win, bool on) {
    if (win) SDL_SetWindowResizable((SDL_Window *)win, on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_set_window_on_top(void *win, bool on) {
    if (win) SDL_SetWindowAlwaysOnTop((SDL_Window *)win, on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_set_window_keyboard_grab(void *win, bool on) {
    if (win) SDL_SetWindowKeyboardGrab((SDL_Window *)win, on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_set_window_mouse_grab(void *win, bool on) {
    if (win) SDL_SetWindowMouseGrab((SDL_Window *)win, on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_set_window_mouse_rect(void *win, int32_t x, int32_t y,
                                int32_t w, int32_t h) {
    SDL_Rect rect;
    if (!win) return;
    if (w < 0 || h < 0) {
        SDL_SetWindowMouseRect((SDL_Window *)win, NULL);
        return;
    }
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_SetWindowMouseRect((SDL_Window *)win, &rect);
}

void sdl2_set_window_fullscreen(void *win, int32_t flags) {
    if (win) SDL_SetWindowFullscreen((SDL_Window *)win, (Uint32)flags);
}

void sdl2_set_window_minimum_size(void *win, int32_t w, int32_t h) {
    if (win) SDL_SetWindowMinimumSize((SDL_Window *)win, w, h);
}

void sdl2_set_window_maximum_size(void *win, int32_t w, int32_t h) {
    if (win) SDL_SetWindowMaximumSize((SDL_Window *)win, w, h);
}

void sdl2_set_window_opacity(void *win, double opacity) {
    if (!win) return;
    if (opacity < 0.0) opacity = 1.0;
    if (opacity > 1.0) opacity = 1.0;
    SDL_SetWindowOpacity((SDL_Window *)win, (float)opacity);
}

bool sdl2_window_has_focus(void *win) {
    if (!win) return false;
    return (SDL_GetWindowFlags((SDL_Window *)win) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool sdl2_window_has_mouse_focus(void *win) {
    if (!win) return false;
    return (SDL_GetWindowFlags((SDL_Window *)win) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

/* --------------------------- renderers ------------------------- */

void *sdl2_create_renderer(void *win, int32_t index, int32_t flags) {
    if (!win) return NULL;
    return SDL_CreateRenderer((SDL_Window *)win, (int)index, (Uint32)flags);
}

void sdl2_destroy_renderer(void *ren) {
    if (ren) SDL_DestroyRenderer((SDL_Renderer *)ren);
}

const char *sdl2_renderer_name(void *ren) {
    SDL_RendererInfo info;
    if (!ren || SDL_GetRendererInfo((SDL_Renderer *)ren, &info) != 0) return "";
    return info.name ? info.name : "";
}

int32_t sdl2_num_render_drivers(void) { return SDL_GetNumRenderDrivers(); }

const char *sdl2_render_driver_name(int32_t index) {
    SDL_RendererInfo info;
    if (SDL_GetRenderDriverInfo((int)index, &info) != 0) return "";
    return info.name ? info.name : "";
}

bool sdl2_render_target_supported(void *ren) {
    if (!ren) return false;
    return SDL_RenderTargetSupported((SDL_Renderer *)ren) == SDL_TRUE;
}

void sdl2_set_draw_color(void *ren_, int32_t r, int32_t g, int32_t b, int32_t a) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    if (!ren) return;
    SDL_SetRenderDrawColor(ren, byte_(r), byte_(g), byte_(b), byte_(a));
}

void sdl2_render_clear(void *ren) {
    if (ren) SDL_RenderClear((SDL_Renderer *)ren);
}

void sdl2_render_present(void *ren) {
    if (ren) SDL_RenderPresent((SDL_Renderer *)ren);
}

void sdl2_render_flush(void *ren) {
    if (ren) SDL_RenderFlush((SDL_Renderer *)ren);
}

void sdl2_set_render_vsync(void *ren, bool on) {
    if (ren) SDL_RenderSetVSync((SDL_Renderer *)ren, on ? 1 : 0);
}

void sdl2_set_viewport(void *ren_, int32_t x, int32_t y, int32_t w, int32_t h) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    SDL_Rect rect;
    if (!ren) return;
    if (w < 0 || h < 0) {
        SDL_RenderSetViewport(ren, NULL);
        return;
    }
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_RenderSetViewport(ren, &rect);
}

void sdl2_set_logical_size(void *ren, int32_t w, int32_t h) {
    if (ren) SDL_RenderSetLogicalSize((SDL_Renderer *)ren, (int)w, (int)h);
}

void sdl2_set_integer_scale(void *ren, bool on) {
    if (ren) SDL_RenderSetIntegerScale((SDL_Renderer *)ren, on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_set_render_scale(void *ren, double sx, double sy) {
    if (ren) SDL_RenderSetScale((SDL_Renderer *)ren, (float)sx, (float)sy);
}

int32_t sdl2_output_width(void *ren) {
    int w = 0, h = 0;
    if (ren) SDL_GetRendererOutputSize((SDL_Renderer *)ren, &w, &h);
    return w;
}

int32_t sdl2_output_height(void *ren) {
    int w = 0, h = 0;
    if (ren) SDL_GetRendererOutputSize((SDL_Renderer *)ren, &w, &h);
    return h;
}

void sdl2_set_render_target(void *ren, void *tex) {
    if (ren) SDL_SetRenderTarget((SDL_Renderer *)ren, (SDL_Texture *)tex);
}

void sdl2_set_blend_mode(void *ren, int32_t mode) {
    if (ren) SDL_SetRenderDrawBlendMode((SDL_Renderer *)ren, (SDL_BlendMode)mode);
}

/* SDL2 has no whole-target flip (that is SDL3's SDL_RenderFlip): flipping is
   per-copy, so it lives in sdl2_copy_ex's `flip` argument. */

/* --------------------------- geometry -------------------------- */

void sdl2_draw_point(void *ren, int32_t x, int32_t y) {
    if (ren) SDL_RenderDrawPoint((SDL_Renderer *)ren, (int)x, (int)y);
}

void sdl2_draw_line(void *ren_, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    if (!ren) return;
    SDL_RenderDrawLine(ren, (int)x0, (int)y0, (int)x1, (int)y1);
}

void sdl2_draw_rect(void *ren_, int32_t x, int32_t y, int32_t w, int32_t h) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    SDL_Rect rect;
    if (!ren) return;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_RenderDrawRect(ren, &rect);
}

void sdl2_fill_rect(void *ren_, int32_t x, int32_t y, int32_t w, int32_t h) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    SDL_Rect rect;
    if (!ren) return;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_RenderFillRect(ren, &rect);
}

/* SDL2 has no circle primitive: midpoint circle, outline only. */
void sdl2_draw_circle(void *ren_, int32_t cx, int32_t cy, int32_t radius) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    int x, y, err;
    if (!ren || radius < 0) return;
    x = radius; y = 0; err = 1 - radius;
    while (x >= y) {
        SDL_RenderDrawPoint(ren, cx + x, cy + y);
        SDL_RenderDrawPoint(ren, cx + y, cy + x);
        SDL_RenderDrawPoint(ren, cx - y, cy + x);
        SDL_RenderDrawPoint(ren, cx - x, cy + y);
        SDL_RenderDrawPoint(ren, cx - x, cy - y);
        SDL_RenderDrawPoint(ren, cx - y, cy - x);
        SDL_RenderDrawPoint(ren, cx + y, cy - x);
        SDL_RenderDrawPoint(ren, cx + x, cy - y);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* Same walk, but each octant pair becomes a scanline. */
void sdl2_fill_circle(void *ren_, int32_t cx, int32_t cy, int32_t radius) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    int x, y, err;
    if (!ren || radius < 0) return;
    x = radius; y = 0; err = 1 - radius;
    while (x >= y) {
        SDL_RenderDrawLine(ren, cx - x, cy + y, cx + x, cy + y);
        SDL_RenderDrawLine(ren, cx - x, cy - y, cx + x, cy - y);
        SDL_RenderDrawLine(ren, cx - y, cy + x, cx + y, cy + x);
        SDL_RenderDrawLine(ren, cx - y, cy - x, cx + y, cy - x);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* --------------------------- textures -------------------------- */

void *sdl2_create_texture(void *ren, int32_t access, int32_t w, int32_t h) {
    if (!ren) return NULL;
    return SDL_CreateTexture((SDL_Renderer *)ren, SDL_PIXELFORMAT_ARGB8888,
                             (int)access, (int)w, (int)h);
}

void *sdl2_solid_texture(void *ren_, int32_t w, int32_t h,
                         int32_t r, int32_t g, int32_t b, int32_t a) {
    SDL_Surface *surf;
    SDL_Texture *tex;
    Uint32 pixel;
    if (!ren_ || w <= 0 || h <= 0) return NULL;
    surf = SDL_CreateRGBSurfaceWithFormat(0, (int)w, (int)h, 32,
                                          SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return NULL;
    pixel = SDL_MapRGBA(surf->format, byte_(r), byte_(g), byte_(b), byte_(a));
    SDL_FillRect(surf, NULL, pixel);
    tex = SDL_CreateTextureFromSurface((SDL_Renderer *)ren_, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void *sdl2_load_bmp(void *ren_, const char *path) {
    SDL_Surface *surf;
    SDL_Texture *tex;
    if (!ren_ || !path) return NULL;
    surf = SDL_LoadBMP(path);
    if (!surf) return NULL;
    tex = SDL_CreateTextureFromSurface((SDL_Renderer *)ren_, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void sdl2_destroy_texture(void *tex) {
    if (tex) SDL_DestroyTexture((SDL_Texture *)tex);
}

int32_t sdl2_texture_width(void *tex) {
    int w = 0, h = 0;
    if (tex) SDL_QueryTexture((SDL_Texture *)tex, NULL, NULL, &w, &h);
    return w;
}

int32_t sdl2_texture_height(void *tex) {
    int w = 0, h = 0;
    if (tex) SDL_QueryTexture((SDL_Texture *)tex, NULL, NULL, &w, &h);
    return h;
}

int32_t sdl2_set_texture_color(void *tex, int32_t r, int32_t g, int32_t b) {
    if (!tex) return -1;
    return SDL_SetTextureColorMod((SDL_Texture *)tex, byte_(r), byte_(g), byte_(b));
}

int32_t sdl2_set_texture_alpha(void *tex, int32_t a) {
    if (!tex) return -1;
    return SDL_SetTextureAlphaMod((SDL_Texture *)tex, byte_(a));
}

int32_t sdl2_set_texture_blend_mode(void *tex, int32_t mode) {
    if (!tex) return -1;
    return SDL_SetTextureBlendMode((SDL_Texture *)tex, (SDL_BlendMode)mode);
}

/* Refill a texture from the CPU (streaming / target textures). Returns non-zero
   for textures SDL refuses to lock, e.g. most STATIC ones. */
int32_t sdl2_fill_texture(void *tex_, int32_t r, int32_t g, int32_t b, int32_t a) {
    SDL_Texture *tex = (SDL_Texture *)tex_;
    void *pixels = NULL;
    Uint32 value, *row;
    int pitch = 0, w = 0, h = 0, y, x;
    if (!tex) return -1;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) != 0) return -1;
    value = ((Uint32)byte_(a) << 24) | ((Uint32)byte_(r) << 16) |
            ((Uint32)byte_(g) << 8) | (Uint32)byte_(b);
    for (y = 0; y < h; y++) {
        row = (Uint32 *)((Uint8 *)pixels + (size_t)y * (size_t)pitch);
        for (x = 0; x < w; x++) row[x] = value;
    }
    SDL_UnlockTexture(tex);
    return 0;
}

void sdl2_copy(void *ren_, void *tex_, int32_t sx, int32_t sy,
               int32_t sw, int32_t sh, int32_t dx, int32_t dy,
               int32_t dw, int32_t dh) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    SDL_Texture *tex = (SDL_Texture *)tex_;
    SDL_Rect src, dst;
    const SDL_Rect *sp = NULL;
    if (!ren || !tex) return;
    if (sx >= 0) {
        src.x = sx; src.y = sy; src.w = sw; src.h = sh;
        sp = &src;
    }
    if (dw <= 0 || dh <= 0) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
        if (dw <= 0) dw = sp ? sp->w : tw;
        if (dh <= 0) dh = sp ? sp->h : th;
    }
    dst.x = dx; dst.y = dy; dst.w = dw; dst.h = dh;
    SDL_RenderCopy(ren, tex, sp, &dst);
}

void sdl2_copy_ex(void *ren_, void *tex_, int32_t sx, int32_t sy,
                  int32_t sw, int32_t sh, int32_t dx, int32_t dy,
                  int32_t dw, int32_t dh, double angle,
                  int32_t cx, int32_t cy, int32_t flip) {
    SDL_Renderer *ren = (SDL_Renderer *)ren_;
    SDL_Texture *tex = (SDL_Texture *)tex_;
    SDL_Rect src, dst;
    SDL_Point center;
    const SDL_Rect *sp = NULL;
    const SDL_Point *cp = NULL;
    if (!ren || !tex) return;
    if (sx >= 0) {
        src.x = sx; src.y = sy; src.w = sw; src.h = sh;
        sp = &src;
    }
    if (dw <= 0 || dh <= 0) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
        if (dw <= 0) dw = sp ? sp->w : tw;
        if (dh <= 0) dh = sp ? sp->h : th;
    }
    dst.x = dx; dst.y = dy; dst.w = dw; dst.h = dh;
    if (cx >= 0) {
        center.x = cx; center.y = cy;
        cp = &center;
    } else {
        center.x = dst.x + dst.w / 2;
        center.y = dst.y + dst.h / 2;
        cp = &center; /* rotate about the middle of the destination rect */
    }
    SDL_RenderCopyEx(ren, tex, sp, &dst, angle, cp, (SDL_RendererFlip)flip);
}

/* ---------------------------- events --------------------------- */

void *sdl2_event_new(void) { return calloc(1, sizeof(SDL_Event)); }

void sdl2_event_free(void *ev) { free(ev); }

void sdl2_pump_events(void) { SDL_PumpEvents(); }

int32_t sdl2_poll_event(void *ev) {
    if (!ev) return 0;
    return SDL_PollEvent((SDL_Event *)ev);
}

int32_t sdl2_wait_event(void *ev) {
    if (!ev) return 0;
    return SDL_WaitEvent((SDL_Event *)ev);
}

int32_t sdl2_wait_timeout(void *ev, int32_t ms) {
    if (!ev) return 0;
    return SDL_WaitEventTimeout((SDL_Event *)ev, (int)ms);
}

void sdl2_flush_events(void) { SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); }

int32_t sdl2_push_quit(void) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_QUIT;
    return SDL_PushEvent(&e);
}

int32_t sdl2_ev_type(void *ev) { return ev ? (int32_t)((SDL_Event *)ev)->type : 0; }

int32_t sdl2_ev_window_id(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->window.windowID : 0;
}

int32_t sdl2_ev_window_event(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->window.event : 0;
}

int32_t sdl2_ev_window_data1(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->window.data1 : 0;
}

int32_t sdl2_ev_window_data2(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->window.data2 : 0;
}

int32_t sdl2_ev_key_scancode(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->key.keysym.scancode : 0;
}

int32_t sdl2_ev_key_keycode(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->key.keysym.sym : 0;
}

bool sdl2_ev_key_down(void *ev) {
    return ev && ((SDL_Event *)ev)->key.state == SDL_PRESSED;
}

int32_t sdl2_ev_key_mod(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->key.keysym.mod : 0;
}

const char *sdl2_ev_text(void *ev) {
    static char buf[40];
    SDL_Event *e = (SDL_Event *)ev;
    buf[0] = '\0';
    if (!e) return buf;
    if (e->type == SDL_TEXTINPUT) {
        strncpy(buf, e->text.text, sizeof(buf) - 1);
    } else if (e->type == SDL_TEXTEDITING) {
        strncpy(buf, e->edit.text, sizeof(buf) - 1);
    } else if (e->type == SDL_DROPFILE || e->type == SDL_DROPTEXT) {
        strncpy(buf, e->drop.file ? e->drop.file : "", sizeof(buf) - 1);
        /* SDL owns the dropped filename and expects SDL_free; the Oak side has
           no way to call it, so the copy releases it right away. */
        SDL_free(e->drop.file);
        e->drop.file = NULL;
    }
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

int32_t sdl2_ev_mouse_button(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->button.button : 0;
}

bool sdl2_ev_mouse_down(void *ev) {
    return ev && ((SDL_Event *)ev)->button.state == SDL_PRESSED;
}

int32_t sdl2_ev_mouse_x(void *ev) { return ev ? (int32_t)((SDL_Event *)ev)->button.x : 0; }
int32_t sdl2_ev_mouse_y(void *ev) { return ev ? (int32_t)((SDL_Event *)ev)->button.y : 0; }

int32_t sdl2_ev_mouse_relx(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->motion.xrel : 0;
}

int32_t sdl2_ev_mouse_rely(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->motion.yrel : 0;
}

double sdl2_ev_wheel_x(void *ev) {
    return ev ? (double)((SDL_Event *)ev)->wheel.preciseX : 0.0;
}

double sdl2_ev_wheel_y(void *ev) {
    return ev ? (double)((SDL_Event *)ev)->wheel.preciseY : 0.0;
}

int32_t sdl2_ev_user_code(void *ev) {
    return ev ? (int32_t)((SDL_Event *)ev)->user.code : 0;
}

int32_t sdl2_ev_user_data1(void *ev) {
    /* SDL_UserEvent carries void * payloads; Oak sees the low 32 bits. */
    return ev ? (int32_t)(intptr_t)((SDL_Event *)ev)->user.data1 : 0;
}

/* --------------------------- keyboard -------------------------- */

/* Polled key state (SDL_GetKeyboardState), independent of the event queue. */
bool sdl2_key_down(int32_t scancode) {
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    if (!state || scancode < 0 || scancode >= SDL_NUM_SCANCODES) return false;
    return state[scancode] != 0;
}

/* SDL lays letters/digits/function keys out consecutively, so three tiny
   helpers cover the whole alphabet instead of 40 named constants. */
int32_t sdl2_scancode_letter(int32_t letter) {
    int32_t ch = letter;
    if (ch >= 'A' && ch <= 'Z') ch += ('a' - 'A');
    if (ch < 'a' || ch > 'z') return SDL_SCANCODE_UNKNOWN;
    return SDL_SCANCODE_A + (ch - 'a');
}

int32_t sdl2_scancode_digit(int32_t digit) {
    if (digit < 0 || digit > 9) return SDL_SCANCODE_UNKNOWN;
    if (digit == 0) return SDL_SCANCODE_0;
    return SDL_SCANCODE_1 + (digit - 1);
}

int32_t sdl2_scancode_key(int32_t index) {
    if (index < 1 || index > 12) return SDL_SCANCODE_UNKNOWN;
    return SDL_SCANCODE_F1 + (index - 1);
}

const char *sdl2_scancode_name(int32_t scancode) {
    return SDL_GetScancodeName((SDL_Scancode)scancode);
}

const char *sdl2_keycode_name(int32_t keycode) {
    return SDL_GetKeyName((SDL_Keycode)keycode);
}

int32_t sdl2_keycode_from_scancode(int32_t scancode) {
    return (int32_t)SDL_GetKeyFromScancode((SDL_Scancode)scancode);
}

int32_t sdl2_scancode_from_keycode(int32_t keycode) {
    return (int32_t)SDL_GetScancodeFromKey((SDL_Keycode)keycode);
}

bool sdl2_keyboard_focus(void) { return SDL_GetKeyboardFocus() != NULL; }

void sdl2_text_input(bool on) {
    if (on) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

/* ---------------------------- mouse ---------------------------- */

int32_t sdl2_mouse_x(void) {
    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    return x;
}

int32_t sdl2_mouse_y(void) {
    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    return y;
}

int32_t sdl2_mouse_global_x(void) {
    int x = 0, y = 0;
    SDL_GetGlobalMouseState(&x, &y);
    return x;
}

int32_t sdl2_mouse_global_y(void) {
    int x = 0, y = 0;
    SDL_GetGlobalMouseState(&x, &y);
    return y;
}

int32_t sdl2_mouse_buttons(void) { return (int32_t)SDL_GetMouseState(NULL, NULL); }

bool sdl2_mouse_button_down(int32_t button) {
    return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(button)) != 0;
}

void sdl2_show_cursor(bool show) {
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

bool sdl2_cursor_shown(void) { return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE; }

void sdl2_set_mouse_relative(bool on) {
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

void sdl2_warp_mouse(void *win, int32_t x, int32_t y) {
    SDL_WarpMouseInWindow((SDL_Window *)win, (int)x, (int)y);
}

/* ---------------------------- timers --------------------------- */

double sdl2_ticks(void) { return (double)SDL_GetTicks64(); }

void sdl2_delay(int32_t ms) {
    if (ms > 0) SDL_Delay((Uint32)ms);
}

double sdl2_perf_counter(void) { return (double)SDL_GetPerformanceCounter(); }

double sdl2_perf_freq(void) {
    Uint64 f = SDL_GetPerformanceFrequency();
    return f ? (double)f : 1.0;
}

double sdl2_time(void) {
    Uint64 f = SDL_GetPerformanceFrequency();
    if (!f) f = 1;
    return (double)SDL_GetPerformanceCounter() / (double)f;
}

/* -------------------------- constants -------------------------- */

#define OAK_SDL_CONST(name, macro) int32_t sdl2_##name(void) { return (int32_t)(macro); }
#define OAK_SDL_HINT(name, macro) const char *sdl2_##name(void) { return macro; }
#include "sdl2_consts.def"
#undef OAK_SDL_CONST
#undef OAK_SDL_HINT






