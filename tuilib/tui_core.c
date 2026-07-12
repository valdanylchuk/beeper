#include "tui_core.h"

#include <stdio.h>
#include <string.h>

/* --- geometry --- */

tui_rect tui_rect_make(int x, int y, int w, int h) {
    tui_rect r = {x, y, w, h};
    return r;
}

bool tui_rect_empty(tui_rect r) {
    return r.w <= 0 || r.h <= 0;
}

tui_rect tui_rect_intersect(tui_rect a, tui_rect b) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int ax2 = a.x + a.w, bx2 = b.x + b.w;
    int ay2 = a.y + a.h, by2 = b.y + b.h;
    int x2 = ax2 < bx2 ? ax2 : bx2;
    int y2 = ay2 < by2 ? ay2 : by2;
    tui_rect r = {x1, y1, x2 - x1, y2 - y1};
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

bool tui_rect_contains(tui_rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* --- surface --- */

void tui_surface_setup(tui_surface *s, tui_cell *back, tui_cell *front,
                       int w, int h) {
    s->w = w;
    s->h = h;
    s->back = back;
    s->front = front;
    s->cursor_x = 0;
    s->cursor_y = 0;
    s->cursor_visible = false;
    s->shown_cursor_visible = -1;
    s->shown_attr = -1;
    /* ch=0 never appears in a drawn frame, so the first present diffs 100% */
    memset(front, 0, (size_t)w * (size_t)h * sizeof(tui_cell));
    memset(back, 0, (size_t)w * (size_t)h * sizeof(tui_cell));
}

tui_rect tui_surface_rect(const tui_surface *s) {
    return tui_rect_make(0, 0, s->w, s->h);
}

void tui_frame_begin(tui_surface *s) {
    tui_cell blank = {' ', TUI_DEFAULT_ATTR};
    int n = s->w * s->h;
    for (int i = 0; i < n; i++)
        s->back[i] = blank;
}

/* --- primitives --- */

static tui_rect clip_to_surface(const tui_surface *s, tui_rect clip) {
    return tui_rect_intersect(clip, tui_rect_make(0, 0, s->w, s->h));
}

void tui_put_char(tui_surface *s, tui_rect clip, int x, int y, char ch,
                  uint8_t attr) {
    tui_rect c = clip_to_surface(s, clip);
    if (!tui_rect_contains(c, x, y))
        return;
    tui_cell *cell = &s->back[y * s->w + x];
    cell->ch = ch;
    cell->attr = attr;
}

void tui_put_str(tui_surface *s, tui_rect clip, int x, int y, const char *str,
                 uint8_t attr) {
    tui_rect c = clip_to_surface(s, clip);
    if (tui_rect_empty(c) || y < c.y || y >= c.y + c.h)
        return;
    for (int i = 0; str[i]; i++) {
        int cx = x + i;
        if (cx >= c.x + c.w)
            break;
        if (cx < c.x)
            continue;
        tui_cell *cell = &s->back[y * s->w + cx];
        cell->ch = str[i];
        cell->attr = attr;
    }
}

void tui_fill(tui_surface *s, tui_rect r, char ch, uint8_t attr) {
    tui_rect c = clip_to_surface(s, r);
    for (int y = c.y; y < c.y + c.h; y++) {
        tui_cell *row = &s->back[y * s->w];
        for (int x = c.x; x < c.x + c.w; x++) {
            row[x].ch = ch;
            row[x].attr = attr;
        }
    }
}

void tui_hline(tui_surface *s, tui_rect clip, int x, int y, int len, char ch,
               uint8_t attr) {
    for (int i = 0; i < len; i++)
        tui_put_char(s, clip, x + i, y, ch, attr);
}

void tui_vline(tui_surface *s, tui_rect clip, int x, int y, int len, char ch,
               uint8_t attr) {
    for (int i = 0; i < len; i++)
        tui_put_char(s, clip, x, y + i, ch, attr);
}

void tui_box(tui_surface *s, tui_rect r, const char *title, uint8_t attr) {
    if (r.w < 2 || r.h < 2)
        return;
    tui_rect clip = tui_surface_rect(s);
    int x2 = r.x + r.w - 1, y2 = r.y + r.h - 1;
    tui_hline(s, clip, r.x + 1, r.y, r.w - 2, '-', attr);
    tui_hline(s, clip, r.x + 1, y2, r.w - 2, '-', attr);
    tui_vline(s, clip, r.x, r.y + 1, r.h - 2, '|', attr);
    tui_vline(s, clip, x2, r.y + 1, r.h - 2, '|', attr);
    tui_put_char(s, clip, r.x, r.y, '+', attr);
    tui_put_char(s, clip, x2, r.y, '+', attr);
    tui_put_char(s, clip, r.x, y2, '+', attr);
    tui_put_char(s, clip, x2, y2, '+', attr);
    if (title && title[0] && r.w >= 4) {
        /* " title " centered-ish on the top edge, clipped to the edge */
        tui_rect edge = tui_rect_make(r.x + 1, r.y, r.w - 2, 1);
        edge = tui_rect_intersect(edge, clip);
        int len = (int)strlen(title);
        tui_put_str(s, edge, r.x + 2, r.y, " ", attr);
        tui_put_str(s, edge, r.x + 3, r.y, title, attr);
        tui_put_str(s, edge, r.x + 3 + len, r.y, " ", attr);
    }
}

void tui_cursor_set(tui_surface *s, int x, int y, bool visible) {
    s->cursor_x = x;
    s->cursor_y = y;
    s->cursor_visible = visible;
}

/* --- diff renderer --- */

typedef struct {
    tui_out_fn out;
    void *ctx;
    size_t total;
} emit_state;

static void emit(emit_state *e, const char *bytes, size_t n) {
    e->out(e->ctx, bytes, n);
    e->total += n;
}

static void emit_str(emit_state *e, const char *s) {
    emit(e, s, strlen(s));
}

static void emit_move(emit_state *e, int x, int y) {
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
    emit(e, buf, (size_t)n);
}

static void emit_sgr(emit_state *e, uint8_t attr) {
    int fg = TUI_ATTR_FG(attr), bg = TUI_ATTR_BG(attr);
    int fgc = (fg & TUI_BRIGHT) ? 90 + (fg & 7) : 30 + fg;
    int bgc = (bg & TUI_BRIGHT) ? 100 + (bg & 7) : 40 + bg;
    /* Emit fg and bg as two single-parameter SGR sequences rather than one
     * combined "ESC[fg;bgm". The ESP32 breezy_term vterm firmware only honors
     * the first parameter of a multi-param SGR, so a combined code drops the
     * background (wrong colors). Every reference app that works on-device
     * (file-mgr, plasma, vi) emits single-parameter codes. */
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "\x1b[%dm\x1b[%dm", fgc, bgc);
    emit(e, buf, (size_t)n);
}

size_t tui_surface_present(tui_surface *s, tui_out_fn out, void *ctx) {
    emit_state e = {out, ctx, 0};
    int cur_attr = s->shown_attr; /* SGR persists across frames */
    int cx = -2, cy = -2; /* terminal cursor position; -2 = unknown */

    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            /* Never write the bottom-right cell: on some terminals (e.g. the
             * ESP32 vterm in breezybox) filling the last column of the last
             * row triggers an auto-scroll. Leaving it untouched costs one
             * corner cell and avoids the scroll. */
            if (x == s->w - 1 && y == s->h - 1)
                continue;
            int i = y * s->w + x;
            tui_cell b = s->back[i];
            if (b.ch == s->front[i].ch && b.attr == s->front[i].attr)
                continue;
            if (x != cx || y != cy)
                emit_move(&e, x, y);
            if (b.attr != cur_attr) {
                emit_sgr(&e, b.attr);
                cur_attr = b.attr;
            }
            emit(&e, &b.ch, 1);
            cx = x + 1;
            cy = y;
            s->front[i] = b;
        }
    }

    if (s->cursor_visible) {
        emit_move(&e, s->cursor_x, s->cursor_y);
        if (s->shown_cursor_visible != 1)
            emit_str(&e, "\x1b[?25h");
    } else if (s->shown_cursor_visible != 0) {
        emit_str(&e, "\x1b[?25l");
    }
    s->shown_cursor_visible = s->cursor_visible ? 1 : 0;
    s->shown_attr = cur_attr;

    return e.total;
}
