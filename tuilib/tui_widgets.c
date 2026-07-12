#include "tui_widgets.h"

#include <stdio.h>
#include <string.h>

const tui_theme *tui_theme_default(void) {
    static const tui_theme th = {
        .normal = TUI_ATTR(TUI_WHITE, TUI_BLUE),
        .selected = TUI_ATTR(TUI_BLACK, TUI_CYAN),
        .border = TUI_ATTR(TUI_WHITE | TUI_BRIGHT, TUI_BLUE),
        .title = TUI_ATTR(TUI_YELLOW | TUI_BRIGHT, TUI_BLUE),
        .status = TUI_ATTR(TUI_BLACK, TUI_CYAN),
        .keybar_key = TUI_ATTR(TUI_WHITE, TUI_BLACK),
        .shadow = TUI_ATTR(TUI_WHITE, TUI_BLACK),
        .disabled = TUI_ATTR(TUI_WHITE, TUI_CYAN),
        .menu_sel = TUI_ATTR(TUI_WHITE | TUI_BRIGHT, TUI_BLACK),
        .focus = TUI_ATTR(TUI_BLACK, TUI_CYAN),
        .slider_track = TUI_ATTR(TUI_CYAN, TUI_BLUE),
        .slider_fill = TUI_ATTR(TUI_CYAN | TUI_BRIGHT, TUI_BLUE),
        .meter_low = TUI_ATTR(TUI_GREEN | TUI_BRIGHT, TUI_BLUE),
        .meter_mid = TUI_ATTR(TUI_YELLOW | TUI_BRIGHT, TUI_BLUE),
        .meter_high = TUI_ATTR(TUI_RED | TUI_BRIGHT, TUI_BLUE),
        .peak = TUI_ATTR(TUI_WHITE | TUI_BRIGHT, TUI_BLUE),
    };
    return &th;
}

static const tui_theme *g_theme = NULL;

void tui_theme_set(const tui_theme *th) { g_theme = th; }

const tui_theme *tui_theme_get(void) {
    return g_theme ? g_theme : tui_theme_default();
}

void tui_shadow(tui_surface *s, tui_rect r, const tui_theme *th) {
    for (int y = r.y + 1; y < r.y + r.h + 1; y++)
        for (int x = r.x + r.w; x < r.x + r.w + 2; x++)
            if (x >= 0 && x < s->w && y >= 0 && y < s->h)
                s->back[y * s->w + x].attr = th->shadow;
    for (int x = r.x + 2; x < r.x + r.w + 2; x++)
        if (x >= 0 && x < s->w && r.y + r.h >= 0 && r.y + r.h < s->h)
            s->back[(r.y + r.h) * s->w + x].attr = th->shadow;
}

void tui_paragraph(tui_surface *s, tui_rect r, const char *const lines[],
                   int n, const tui_theme *th, int scroll_offset) {
    if (scroll_offset < 0)
        scroll_offset = 0;
    for (int row = 0; row < r.h; row++) {
        int i = scroll_offset + row;
        if (i >= n)
            break;
        tui_put_str(s, r, r.x, r.y + row, lines[i], th->normal);
    }
}

void tui_list(tui_surface *s, tui_rect r, const char *const items[],
              int count, tui_list_state *state, const tui_theme *th,
              tui_list_render_fn render_row, void *render_ctx) {
    if (count <= 0 || r.h <= 0)
        return;
    if (state->selected < 0)
        state->selected = 0;
    if (state->selected >= count)
        state->selected = count - 1;

    /* keep the selection inside the viewport */
    if (state->offset > state->selected)
        state->offset = state->selected;
    if (state->offset < state->selected - r.h + 1)
        state->offset = state->selected - r.h + 1;
    if (state->offset > count - r.h)
        state->offset = count - r.h;
    if (state->offset < 0)
        state->offset = 0;

    char buf[256];
    for (int row = 0; row < r.h; row++) {
        int i = state->offset + row;
        if (i >= count)
            break;
        const char *text;
        if (render_row) {
            render_row(render_ctx, i, buf, sizeof(buf));
            text = buf;
        } else {
            text = items[i];
        }
        uint8_t attr = (i == state->selected) ? th->selected : th->normal;
        /* full-width row so the selection bar spans the rect */
        tui_hline(s, r, r.x, r.y + row, r.w, ' ', attr);
        tui_put_str(s, r, r.x, r.y + row, text, attr);
    }
}

void tui_scrollbar(tui_surface *s, tui_rect r, int total, int offset,
                   int viewport, const tui_theme *th) {
    if (r.h <= 0 || total <= viewport || total <= 0)
        return;
    if (offset < 0)
        offset = 0;
    if (offset > total - viewport)
        offset = total - viewport;

    int thumb_h = (int)((long)viewport * r.h / total);
    if (thumb_h < 1)
        thumb_h = 1;
    if (thumb_h > r.h)
        thumb_h = r.h;
    int span = r.h - thumb_h;
    int thumb_y = (total - viewport) > 0
                      ? (int)((long)offset * span / (total - viewport))
                      : 0;

    tui_vline(s, r, r.x, r.y, r.h, '|', th->border);
    tui_vline(s, r, r.x, r.y + thumb_y, thumb_h, '#', th->border);
}

void tui_statusbar(tui_surface *s, tui_rect r, const char *left_text,
                   const char *right_text, const tui_theme *th) {
    if (r.h <= 0)
        return;
    tui_hline(s, r, r.x, r.y, r.w, ' ', th->status);
    if (left_text)
        tui_put_str(s, r, r.x + 1, r.y, left_text, th->status);
    if (right_text) {
        int len = (int)strlen(right_text);
        tui_put_str(s, r, r.x + r.w - len - 1, r.y, right_text, th->status);
    }
}

void tui_keybar(tui_surface *s, tui_rect r, const char *const labels[10],
                const tui_theme *th) {
    if (r.h <= 0 || r.w <= 0)
        return;
    tui_hline(s, r, r.x, r.y, r.w, ' ', th->keybar_key);
    for (int i = 0; i < 10; i++) {
        /* slot boundaries computed from the full width so the bar always
         * spans the rect exactly (no accumulated rounding gap) */
        int x0 = r.x + r.w * i / 10;
        int x1 = r.x + r.w * (i + 1) / 10;
        char num[3];
        snprintf(num, sizeof(num), "%d", i + 1);
        int num_len = (int)strlen(num);
        tui_put_str(s, r, x0, r.y, num, th->keybar_key);
        int lx = x0 + num_len;
        tui_hline(s, r, lx, r.y, x1 - lx, ' ', th->status);
        if (labels[i])
            tui_put_str(s, tui_rect_intersect(r, tui_rect_make(lx, r.y,
                                                               x1 - lx, 1)),
                        lx, r.y, labels[i], th->status);
    }
}
