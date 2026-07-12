/* breezy_tui core: rects, cells, surface, drawing primitives, diff renderer.
 * Terminal-free: everything here renders into in-memory cell grids and can
 * run under tests with no tty. See tui_term.h for the terminal backend. */
#ifndef TUI_CORE_H
#define TUI_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- colors / attributes (indices match VTERM_* in breezy_term) --- */
enum {
    TUI_BLACK = 0,
    TUI_RED,
    TUI_GREEN,
    TUI_YELLOW,
    TUI_BLUE,
    TUI_MAGENTA,
    TUI_CYAN,
    TUI_WHITE,
    TUI_BRIGHT = 8 /* OR into a color: TUI_WHITE | TUI_BRIGHT */
};

#define TUI_ATTR(fg, bg)  ((uint8_t)((((bg) & 0x0F) << 4) | ((fg) & 0x0F)))
#define TUI_ATTR_FG(a)    ((a) & 0x0F)
#define TUI_ATTR_BG(a)    (((a) >> 4) & 0x0F)
#define TUI_DEFAULT_ATTR  TUI_ATTR(TUI_WHITE, TUI_BLACK)

/* --- geometry --- */
typedef struct {
    int x, y, w, h;
} tui_rect;

tui_rect tui_rect_make(int x, int y, int w, int h);
tui_rect tui_rect_intersect(tui_rect a, tui_rect b);
bool tui_rect_contains(tui_rect r, int x, int y);
bool tui_rect_empty(tui_rect r);

/* --- cells and surface --- */
typedef struct {
    char ch;
    uint8_t attr; /* (bg << 4) | fg, matches vterm_cell_t */
} tui_cell;

typedef struct {
    int w, h;
    tui_cell *back;  /* widgets draw here */
    tui_cell *front; /* what the terminal currently shows */
    int cursor_x, cursor_y;
    bool cursor_visible;
    int shown_cursor_visible; /* -1 until first present */
    int shown_attr;           /* SGR the terminal is left in; -1 = unknown */
} tui_surface;

/* Attach caller-provided storage (each grid w*h cells). No allocation here;
 * tui_init() in tui_term allocates once and calls this. Front buffer is
 * invalidated so the first present redraws everything. */
void tui_surface_setup(tui_surface *s, tui_cell *back, tui_cell *front,
                       int w, int h);

/* Clear the back buffer to spaces with TUI_DEFAULT_ATTR. */
void tui_frame_begin(tui_surface *s);

/* Whole surface as a rect (clip argument for full-screen drawing). */
tui_rect tui_surface_rect(const tui_surface *s);

/* --- drawing primitives, all clipped to `clip` ∩ surface --- */
void tui_put_char(tui_surface *s, tui_rect clip, int x, int y, char ch,
                  uint8_t attr);
void tui_put_str(tui_surface *s, tui_rect clip, int x, int y, const char *str,
                 uint8_t attr);
void tui_fill(tui_surface *s, tui_rect r, char ch, uint8_t attr);
void tui_hline(tui_surface *s, tui_rect clip, int x, int y, int len, char ch,
               uint8_t attr);
void tui_vline(tui_surface *s, tui_rect clip, int x, int y, int len, char ch,
               uint8_t attr);
/* Single-line ASCII border (+-|) on the edge of r, optional title on the top
 * edge (NULL for none). Interior is not touched. */
void tui_box(tui_surface *s, tui_rect r, const char *title, uint8_t attr);

/* --- cursor --- */
void tui_cursor_set(tui_surface *s, int x, int y, bool visible);

/* --- diff renderer --- */
/* Byte sink; the terminal backend points this at a buffered stdout writer,
 * tests point it at a byte counter / capture buffer. */
typedef void (*tui_out_fn)(void *ctx, const char *bytes, size_t n);

/* Diff back vs front, emit minimal ANSI (cursor moves + SGR only on attr
 * change) into `out`, copy back to front. Returns bytes emitted. */
size_t tui_surface_present(tui_surface *s, tui_out_fn out, void *ctx);

#endif /* TUI_CORE_H */
