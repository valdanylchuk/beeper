/* breezy_tui basic widgets: stateless draw functions over tui_surface.
 * Convention: every widget takes a `const tui_theme *` for its colors —
 * no loose attr parameters. All drawing is clipped to the given rect. */
#ifndef TUI_WIDGETS_H
#define TUI_WIDGETS_H

#include <stddef.h>

#include "tui_core.h"

/* --- theme --- */
typedef struct {
    uint8_t normal;      /* body text */
    uint8_t selected;    /* list selection bar */
    uint8_t border;      /* boxes / scrollbar track */
    uint8_t title;       /* box titles, statusbar */
    uint8_t status;      /* statusbar / keybar labels */
    uint8_t keybar_key;  /* keybar F-key numbers */
    uint8_t shadow;      /* dialog/menu drop shadow (chars keep, attr set) */
    uint8_t disabled;    /* greyed-out menu items */
    uint8_t menu_sel;    /* menu selection bar (must contrast th->status,
                          * which is the menu surface color) */
    /* controls (tui_controls.h) */
    uint8_t focus;        /* focused control label/readout */
    uint8_t slider_track; /* unfilled slider/meter track */
    uint8_t slider_fill;  /* slider fill, fader bars, sparkline */
    uint8_t meter_low;    /* meter/bargraph zones by level */
    uint8_t meter_mid;
    uint8_t meter_high;
    uint8_t peak;         /* peak-hold dots / sparkline marker */
} tui_theme;

/* MC-like blue default (white on blue, cyan selection bar). */
const tui_theme *tui_theme_default(void);

/* Library-level current theme: modal helpers (tui_msgbox, tui_prompt,
 * tui_menu_run) draw with tui_theme_get(). Starts as tui_theme_default();
 * set NULL to reset to the default. The pointed-to theme must outlive use. */
void tui_theme_set(const tui_theme *th);
const tui_theme *tui_theme_get(void);

/* Classic MC drop shadow: re-attr (chars kept) two columns right of r and
 * one row beneath it, offset one cell down/right. */
void tui_shadow(tui_surface *s, tui_rect r, const tui_theme *th);

/* --- paragraph: n lines drawn from lines[scroll_offset], truncated (no
 * wrap in v0), clipped to rect. --- */
void tui_paragraph(tui_surface *s, tui_rect r, const char *const lines[],
                   int n, const tui_theme *th, int scroll_offset);

/* --- list --- */
typedef struct {
    int selected; /* index of the highlighted row */
    int offset;   /* first visible row; adjusted to keep selection visible */
} tui_list_state;

/* Custom row text: write up to `cap`-1 chars (NUL-terminated) for row
 * `index` into `buf`. When set, `items` may be NULL. */
typedef void (*tui_list_render_fn)(void *ctx, int index, char *buf,
                                   size_t cap);

/* Scrollable selection list. Clamps state->selected to [0, count-1] and
 * scrolls state->offset so the selection stays visible. The selected row
 * is drawn full-width in th->selected. */
void tui_list(tui_surface *s, tui_rect r, const char *const items[],
              int count, tui_list_state *state, const tui_theme *th,
              tui_list_render_fn render_row, void *render_ctx);

/* Vertical scrollbar in column r.x (uses r.h rows). No-op when everything
 * fits (total <= viewport). */
void tui_scrollbar(tui_surface *s, tui_rect r, int total, int offset,
                   int viewport, const tui_theme *th);

/* One-row bar: left_text left-aligned, right_text right-aligned, filled
 * with th->status. Either text may be NULL. */
void tui_statusbar(tui_surface *s, tui_rect r, const char *left_text,
                   const char *right_text, const tui_theme *th);

/* MC-style F-key bar: 10 equal slots, "1Help 2Menu ...". NULL labels
 * render as empty slots. */
void tui_keybar(tui_surface *s, tui_rect r, const char *const labels[10],
                const tui_theme *th);

#endif /* TUI_WIDGETS_H */
