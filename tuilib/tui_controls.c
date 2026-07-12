#include "tui_controls.h"

#include <stdio.h>
#include <string.h>

/* --- shared value logic --- */

int tui_value_step(int value, int min, int max, int dir, bool coarse) {
    int step = 1;
    if (coarse) {
        step = (max - min) / 10;
        if (step < 1)
            step = 1;
    }
    value += dir * step;
    if (value < min)
        value = min;
    if (value > max)
        value = max;
    return value;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* value -> position on a scale of `steps` (rounded); span-safe. */
static int scale(int value, int min, int max, int steps) {
    int span = max - min;
    if (span < 1)
        span = 1;
    return clampi((int)(((long)(value - min) * steps + span / 2) / span), 0,
                  steps);
}

static void fmt_value(tui_value_fmt_fn fmt, void *ctx, int value,
                      bool bipolar, char *buf, size_t cap) {
    if (fmt)
        fmt(ctx, value, buf, cap);
    else
        snprintf(buf, cap, bipolar ? "%+d" : "%d", value);
}

/* --- horizontal slider --- */

void tui_slider(tui_surface *s, tui_rect r, const tui_slider_opts *o,
                const tui_theme *th) {
    if (r.h < 1 || r.w < 1)
        return;
    uint8_t label_a = o->focused ? th->focus : th->normal;
    int v = clampi(o->value, o->min, o->max);

    int label_w = o->label_w;
    if (label_w <= 0)
        label_w = o->label ? (int)strlen(o->label) : 0;
    if (o->label)
        tui_put_str(s, r, r.x, r.y, o->label, label_a);

    int readout_w = o->readout_w > 0 ? o->readout_w : 4;
    int tx = r.x + label_w + 1; /* '[' column */
    int track_w = r.w - label_w - 1 - 2 - 1 - readout_w;

    if (track_w >= 1) {
        tui_put_char(s, r, tx, r.y, '[', th->normal);
        tui_put_char(s, r, tx + 1 + track_w, r.y, ']', th->normal);

        if (!o->bipolar) {
            int fill = scale(v, o->min, o->max, track_w);
            for (int i = 0; i < track_w; i++)
                tui_put_char(s, r, tx + 1 + i, r.y, i < fill ? '#' : '-',
                             i < fill ? th->slider_fill : th->slider_track);
        } else {
            int last = track_w - 1;
            int pos = scale(v, o->min, o->max, last);
            int center = scale(0, o->min, o->max, last);
            for (int i = 0; i < track_w; i++)
                tui_put_char(s, r, tx + 1 + i, r.y, '-', th->slider_track);
            /* fill from the center marker out to the value, arrow head at
             * the value end: [----<====|----] / [--------|==>--] */
            if (pos > center) {
                for (int i = center + 1; i < pos; i++)
                    tui_put_char(s, r, tx + 1 + i, r.y, '=',
                                 th->slider_fill);
                tui_put_char(s, r, tx + 1 + pos, r.y, '>', th->slider_fill);
            } else if (pos < center) {
                for (int i = pos + 1; i < center; i++)
                    tui_put_char(s, r, tx + 1 + i, r.y, '=',
                                 th->slider_fill);
                tui_put_char(s, r, tx + 1 + pos, r.y, '<', th->slider_fill);
            }
            tui_put_char(s, r, tx + 1 + center, r.y, '|', th->normal);
        }
    }

    char buf[16];
    fmt_value(o->fmt, o->fmt_ctx, v, o->bipolar, buf, sizeof(buf));
    int len = (int)strlen(buf);
    tui_put_str(s, r, r.x + r.w - len, r.y, buf, label_a);
}

/* --- vertical fader --- */

void tui_fader_draw(tui_surface *s, tui_rect r, int value, int min, int max,
                    bool focused, const tui_theme *th) {
    if (r.h < 1 || r.w < 1)
        return;
    int lvl = scale(clampi(value, min, max), min, max, r.h * 2);
    int bw = r.w > 2 ? 2 : r.w;
    uint8_t a = focused ? th->focus : th->slider_fill;

    for (int row = 0; row < r.h; row++) {
        int cell_lvl = lvl - row * 2; /* 2 levels per row, bottom-up */
        char ch;
        if (cell_lvl >= 2)
            ch = '#';
        else if (cell_lvl == 1)
            ch = ':';
        else
            continue;
        int y = r.y + r.h - 1 - row;
        for (int i = 0; i < bw; i++)
            tui_put_char(s, r, r.x + i, y, ch, a);
    }
}

void tui_fader_bank(tui_surface *s, tui_rect r, const tui_fader faders[],
                    int n, int focus, tui_fader_fmt_fn fmt, void *fmt_ctx,
                    const tui_theme *th) {
    if (r.h < 3 || r.w < 2)
        return;
    for (int i = 0; i < n; i++) {
        int x = r.x + i * 3;
        if (x + 1 >= r.x + r.w)
            break;
        const tui_fader *f = &faders[i];
        bool foc = i == focus;
        uint8_t a = foc ? th->focus : th->normal;

        if (f->label)
            tui_put_str(s, r, x, r.y, f->label, a);
        tui_fader_draw(s,
                       tui_rect_intersect(r, tui_rect_make(x, r.y + 1, 2,
                                                           r.h - 2)),
                       f->value, f->min, f->max, foc, th);
        char buf[8];
        if (fmt)
            fmt(fmt_ctx, i, f->value, buf, sizeof(buf));
        else
            snprintf(buf, sizeof(buf), "%d", f->value);
        tui_put_str(s, r, x, r.y + r.h - 1, buf, a);
    }
}

/* --- stepper --- */

void tui_stepper(tui_surface *s, tui_rect r, const char *label, int value,
                 tui_value_fmt_fn fmt, void *fmt_ctx, bool focused,
                 const tui_theme *th) {
    if (r.h < 1)
        return;
    uint8_t a = focused ? th->focus : th->normal;
    int x = r.x;
    if (label) {
        tui_put_str(s, r, x, r.y, label, a);
        x += (int)strlen(label) + 1;
    }
    char buf[16];
    fmt_value(fmt, fmt_ctx, value, false, buf, sizeof(buf));
    tui_put_char(s, r, x, r.y, '<', th->slider_track);
    tui_put_str(s, r, x + 1, r.y, buf, a);
    tui_put_char(s, r, x + 1 + (int)strlen(buf), r.y, '>', th->slider_track);
}

/* --- segmented selector --- */

void tui_select(tui_surface *s, tui_rect r, const char *label,
                const char *const options[], int n, int selected,
                bool focused, const tui_theme *th) {
    if (r.h < 1)
        return;
    int x = r.x;
    if (label) {
        tui_put_str(s, r, x, r.y, label,
                    focused ? th->focus : th->normal);
        x += (int)strlen(label) + 1;
    }
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(options[i]);
        if (i == selected) {
            tui_put_char(s, r, x, r.y, '[', th->selected);
            tui_put_str(s, r, x + 1, r.y, options[i], th->selected);
            tui_put_char(s, r, x + 1 + len, r.y, ']', th->selected);
        } else {
            tui_put_str(s, r, x + 1, r.y, options[i], th->normal);
        }
        x += len + 2;
    }
}

/* --- toggle --- */

void tui_toggle(tui_surface *s, tui_rect r, const char *label, bool on,
                bool focused, const tui_theme *th) {
    if (r.h < 1)
        return;
    uint8_t a = focused ? th->focus : th->normal;
    tui_put_str(s, r, r.x, r.y, on ? "[x]" : "[ ]", a);
    if (label)
        tui_put_str(s, r, r.x + 4, r.y, label, a);
}

/* --- meter --- */

/* Zone attr by position `i` out of `steps` (same thresholds as moddy's
 * spectrum: top quarter high, upper half mid). */
static uint8_t zone_attr(int i, int steps, const tui_theme *th) {
    if (i * 4 >= steps * 3)
        return th->meter_high;
    if (i * 2 >= steps)
        return th->meter_mid;
    return th->meter_low;
}

void tui_meter(tui_surface *s, tui_rect r, int level, int peak, int max,
               const tui_theme *th) {
    if (r.h < 1 || r.w < 1)
        return;
    if (max < 1)
        max = 1;

    if (r.h == 1) { /* horizontal */
        int cells = r.w;
        int fill = scale(clampi(level, 0, max), 0, max, cells);
        int pk = peak > 0 ? scale(clampi(peak, 0, max), 0, max, cells) - 1
                          : -1;
        for (int i = 0; i < cells; i++) {
            char ch;
            uint8_t a;
            if (i == pk && i >= fill) {
                ch = '|';
                a = th->peak;
            } else if (i < fill) {
                ch = '=';
                a = zone_attr(i, cells, th);
            } else {
                ch = '.';
                a = th->slider_track;
            }
            tui_put_char(s, r, r.x + i, r.y, ch, a);
        }
    } else { /* vertical, bottom-up */
        int rows = r.h;
        int fill = scale(clampi(level, 0, max), 0, max, rows);
        int pk = peak > 0 ? scale(clampi(peak, 0, max), 0, max, rows) - 1
                          : -1;
        int bw = r.w > 2 ? 2 : r.w;
        for (int row = 0; row < rows; row++) {
            char ch;
            uint8_t a;
            if (row == pk && row >= fill) {
                ch = '-';
                a = th->peak;
            } else if (row < fill) {
                ch = '=';
                a = zone_attr(row, rows, th);
            } else {
                continue;
            }
            int y = r.y + r.h - 1 - row;
            for (int i = 0; i < bw; i++)
                tui_put_char(s, r, r.x + i, y, ch, a);
        }
    }
}

/* --- bar-graph pane --- */

int tui_bargraph_bins(int width) {
    int n = (width + 1) / 3;
    return n < 1 ? 1 : n;
}

void tui_bargraph(tui_surface *s, tui_rect r, const int level[],
                  const int peak[], int nbins, int max,
                  const tui_theme *th) {
    if (r.h < 1 || r.w < 1)
        return;
    if (max < 1)
        max = 1;
    int levels = r.h * 2; /* ':' half vs '|' full per row */

    for (int b = 0; b < nbins; b++) {
        int x0 = r.x + b * 3;
        if (x0 + 1 >= r.x + r.w)
            break;
        int lvl = scale(clampi(level[b], 0, max), 0, max, levels);

        for (int row = 0; row < r.h; row++) {
            int cell_lvl = lvl - row * 2;
            char ch;
            if (cell_lvl >= 2)
                ch = '|';
            else if (cell_lvl == 1)
                ch = ':';
            else
                continue;
            int y = r.y + r.h - 1 - row;
            uint8_t a = zone_attr(row, r.h, th);
            tui_put_char(s, r, x0, y, ch, a);
            tui_put_char(s, r, x0 + 1, y, ch, a);
        }

        if (peak) { /* peak-hold dot above the bar */
            int plvl = scale(clampi(peak[b], 0, max), 0, max, levels);
            int prow = (plvl - 1) / 2;
            if (plvl > lvl && prow >= 0 && prow < r.h) {
                int y = r.y + r.h - 1 - prow;
                tui_put_char(s, r, x0, y, '.', th->peak);
                tui_put_char(s, r, x0 + 1, y, '.', th->peak);
            }
        }
    }
}

/* --- sparkline --- */

void tui_sparkline(tui_surface *s, tui_rect r, const int level[], int n,
                   int max, int marker, const tui_theme *th) {
    if (r.h < 1 || r.w < 1)
        return;
    if (max < 1)
        max = 1;
    int levels = r.h * 2;

    for (int i = 0; i < n && i < r.w; i++) {
        int lvl = scale(clampi(level[i], 0, max), 0, max, levels);
        uint8_t a = i == marker ? th->peak : th->slider_fill;
        for (int row = 0; row < r.h; row++) {
            int cell_lvl = lvl - row * 2;
            char ch;
            if (cell_lvl >= 2)
                ch = '|';
            else if (cell_lvl == 1)
                ch = ':';
            else
                continue;
            tui_put_char(s, r, r.x + i, r.y + r.h - 1 - row, ch, a);
        }
        /* keep the marker visible even at level 0 */
        if (i == marker && lvl == 0)
            tui_put_char(s, r, r.x + i, r.y + r.h - 1, '.', a);
    }
}
