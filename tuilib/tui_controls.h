/* breezy_tui control & visualization widgets, driven by the
 * music-app family. Same conventions as tui_widgets.h:
 * stateless draw functions, app owns all state, colors come
 * from tui_theme, everything clipped to the given rect, no malloc, no
 * floats, ASCII only.
 *
 * Interaction convention (app-side, not enforced here): Up/Down or
 * Tab/Shift-Tab move focus (tui_focus_step), Left/Right adjust the focused
 * control (tui_value_step), Shift/PgUp/PgDn = coarse, Home/End = min/max.
 * Every widget takes a `focused` bool that switches its attrs. */
#ifndef TUI_CONTROLS_H
#define TUI_CONTROLS_H

#include <stddef.h>

#include "tui_core.h"
#include "tui_widgets.h"

/* --- shared value logic --- */

/* Step value by dir (+/-1); fine = 1, coarse = span/10 (at least 1).
 * Clamps to [min, max]. */
int tui_value_step(int value, int min, int max, int dir, bool coarse);

/* Readout formatter: write up to cap-1 chars (NUL-terminated) for `value`.
 * NULL formatter = print the raw value. This is how apps show Hz/ms/
 * semitones while the widgets stay unit-agnostic. */
typedef void (*tui_value_fmt_fn)(void *ctx, int value, char *buf,
                                 size_t cap);

/* --- horizontal slider (one row) ---
 *
 *   Cutoff   [########------] 1.2k        unipolar
 *   Env>Cut  [----<====|----]  -96        bipolar (center origin)
 */
typedef struct {
    const char *label;      /* NULL = no label */
    int value, min, max;
    bool bipolar;           /* center-origin fill for signed params */
    bool focused;
    tui_value_fmt_fn fmt;   /* NULL = raw value ("%+d" when bipolar) */
    void *fmt_ctx;
    int label_w;            /* label columns; 0 = strlen(label) */
    int readout_w;          /* readout columns (right-aligned); 0 = 4 */
} tui_slider_opts;

void tui_slider(tui_surface *s, tui_rect r, const tui_slider_opts *o,
                const tui_theme *th);

/* --- vertical fader ---
 * Bar only, bottom-up in r (up to 2 columns wide), ':' half-cell steps for
 * 2x vertical resolution. Labels/readouts are the caller's (or the bank's). */
void tui_fader_draw(tui_surface *s, tui_rect r, int value, int min, int max,
                    bool focused, const tui_theme *th);

/* Fader bank: N faders side by side in r (3 columns per fader: 2-wide bar
 * + 1 gap), label row on top, value row below, `focus` = focused index
 * (-1 = none). Same formatter idea as the slider, plus the fader index. */
typedef struct {
    const char *label;      /* short (fits 2 cols), shown above the bar */
    int value, min, max;
} tui_fader;

typedef void (*tui_fader_fmt_fn)(void *ctx, int index, int value, char *buf,
                                 size_t cap);

void tui_fader_bank(tui_surface *s, tui_rect r, const tui_fader faders[],
                    int n, int focus, tui_fader_fmt_fn fmt, void *fmt_ctx,
                    const tui_theme *th);

/* --- stepper / spinner (one row): `BPM <096>` --- */
void tui_stepper(tui_surface *s, tui_rect r, const char *label, int value,
                 tui_value_fmt_fn fmt, void *fmt_ctx, bool focused,
                 const tui_theme *th);

/* --- segmented selector (one row): `Wave  Saw [Sqr] Sin` ---
 * One-of-N inline radio for small N; the selected segment is bracketed and
 * drawn in th->selected. */
void tui_select(tui_surface *s, tui_rect r, const char *label,
                const char *const options[], int n, int selected,
                bool focused, const tui_theme *th);

/* --- toggle (one row): `[x] Sustain` --- */
void tui_toggle(tui_surface *s, tui_rect r, const char *label, bool on,
                bool focused, const tui_theme *th);

/* --- indicators (read-only) --- */

/* Level meter, 0..max, low/mid/high zone colors by position, optional
 * peak-hold marker (peak <= 0 = none). Horizontal when r.h == 1
 * (`[====|....]` style '='/'.', '|' peak), vertical otherwise (bottom-up
 * rows, up to 2 columns wide, '-' peak). */
void tui_meter(tui_surface *s, tui_rect r, int level, int peak, int max,
               const tui_theme *th);

/* Bar-graph pane (moddy's spectrum, generalized): nbins levels 0..max ->
 * 2-cell bars with 1-cell gaps, ':' half / '|' full cells, zone colors by
 * height, optional peak-hold dots (`peak` may be NULL). Energy simulation
 * (decay/attack dynamics) stays app-side; this only draws bins. */
void tui_bargraph(tui_surface *s, tui_rect r, const int level[],
                  const int peak[], int nbins, int max, const tui_theme *th);

/* How many bins fit a given width under the 3-columns-per-bar layout. */
int tui_bargraph_bins(int width);

/* Sparkline / mini-plot: n levels 0..max as 1-cell columns with ':'/half
 * '|'/full steps — envelope shape, LFO shape, any small "shape at a
 * glance". `marker` = column drawn in th->peak (e.g. current envelope
 * position), -1 = none. */
void tui_sparkline(tui_surface *s, tui_rect r, const int level[], int n,
                   int max, int marker, const tui_theme *th);

#endif /* TUI_CONTROLS_H */
