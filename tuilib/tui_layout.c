#include "tui_layout.h"

void tui_layout_split(tui_rect parent, tui_dir dir,
                      const tui_constraint *cons, int n, tui_rect *out) {
    /* Sizes live on the stack; callers split into a handful of regions.
     * Guard against silly n so the VLA-free fixed buffer is safe. */
    enum { MAX_SPLIT = 32 };
    int size[MAX_SPLIT];
    if (n <= 0)
        return;
    if (n > MAX_SPLIT)
        n = MAX_SPLIT;

    int extent = (dir == TUI_DIR_HORIZ) ? parent.w : parent.h;
    if (extent < 0)
        extent = 0;

    /* pass 1: base sizes */
    long sum = 0;
    for (int i = 0; i < n; i++) {
        int v = 0;
        switch (cons[i].kind) {
        case TUI_CON_LEN:
        case TUI_CON_MIN:
            v = cons[i].value;
            break;
        case TUI_CON_PCT:
            v = (int)((long)cons[i].value * extent / 100);
            break;
        case TUI_CON_FILL:
            v = 0;
            break;
        }
        size[i] = v;
        sum += v;
    }

    if (sum > extent) {
        /* over-constrained: allocate left-to-right until space runs out */
        int remaining = extent;
        for (int i = 0; i < n; i++) {
            if (size[i] > remaining)
                size[i] = remaining;
            remaining -= size[i];
        }
    } else {
        /* grow FILLs (or MINs when there are no FILLs) with the leftover */
        int leftover = extent - (int)sum;
        int grow_kind = TUI_CON_FILL;
        int k = 0;
        for (int i = 0; i < n; i++)
            if (cons[i].kind == TUI_CON_FILL)
                k++;
        if (k == 0) {
            grow_kind = TUI_CON_MIN;
            for (int i = 0; i < n; i++)
                if (cons[i].kind == TUI_CON_MIN)
                    k++;
        }
        if (k > 0 && leftover > 0) {
            int share = leftover / k, extra = leftover % k;
            for (int i = 0; i < n; i++) {
                if (cons[i].kind != grow_kind)
                    continue;
                size[i] += share;
                if (extra > 0) {
                    size[i]++;
                    extra--;
                }
            }
        }
    }

    /* lay out consecutively along the split axis */
    int pos = (dir == TUI_DIR_HORIZ) ? parent.x : parent.y;
    for (int i = 0; i < n; i++) {
        if (dir == TUI_DIR_HORIZ)
            out[i] = tui_rect_make(pos, parent.y, size[i], parent.h);
        else
            out[i] = tui_rect_make(parent.x, pos, parent.w, size[i]);
        pos += size[i];
    }
}

tui_rect tui_rect_margin(tui_rect r, int n) {
    tui_rect m = tui_rect_make(r.x + n, r.y + n, r.w - 2 * n, r.h - 2 * n);
    if (m.w < 0)
        m.w = 0;
    if (m.h < 0)
        m.h = 0;
    return m;
}

tui_rect tui_rect_center(tui_rect parent, int w, int h) {
    if (w > parent.w)
        w = parent.w;
    if (h > parent.h)
        h = parent.h;
    return tui_rect_make(parent.x + (parent.w - w) / 2,
                         parent.y + (parent.h - h) / 2, w, h);
}
