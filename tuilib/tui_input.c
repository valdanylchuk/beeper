#include "tui_input.h"

#include <string.h>

#include "tui_term.h"

/* Sequences covered (mined from breezy_term's vterm output and verified on
 * macOS Terminal):
 *   CSI A/B/C/D           arrows           ESC [ A
 *   CSI H / F             home / end       ESC [ H
 *   CSI <n> ~             ESC [ 3 ~  del; 1/7 home, 4/8 end, 2 ins,
 *                         5/6 pgup/pgdn, 11-15 F1-F5, 17-21 F6-F10,
 *                         23/24 F11/F12
 *   SS3 P/Q/R/S           F1-F4            ESC O P
 *   SS3 A-D, H, F         arrows/home/end in application cursor mode
 *   CSI 1;<m> X / <n>;<m> ~  modifier param m-1 = shift|alt<<1|ctrl<<2
 *   CSI Z                 Shift-Tab
 *   ESC <byte>            Alt + key
 *   lone ESC              resolved by timeout via tui_key_flush()
 */

enum { ST_GROUND, ST_ESC, ST_CSI, ST_SS3 };

static tui_key key_of(uint8_t kind) {
    tui_key k = {0};
    k.kind = kind;
    return k;
}

static void reset(tui_key_decoder *d) {
    memset(d, 0, sizeof(*d));
}

/* Decode a byte with no escape prefix pending. */
static tui_key ground_key(unsigned char b) {
    tui_key k = {0};
    switch (b) {
    case '\r':
    case '\n':
        k.kind = TUI_KEY_ENTER;
        return k;
    case '\t':
        k.kind = TUI_KEY_TAB;
        return k;
    case 0x7f:
    case 0x08:
        k.kind = TUI_KEY_BACKSPACE;
        return k;
    }
    if (b < 0x20) { /* remaining C0 controls: Ctrl-letter etc. */
        k.kind = TUI_KEY_CHAR;
        k.ctrl = true;
        k.ch = (char)(b + 'a' - 1);
        return k;
    }
    k.kind = TUI_KEY_CHAR;
    k.ch = (char)b;
    return k;
}

static void apply_modifier(tui_key *k, int m) {
    if (m < 2)
        return;
    m -= 1;
    k->shift = (m & 1) != 0;
    k->alt = (m & 2) != 0;
    k->ctrl = (m & 4) != 0;
}

/* Arrow/home/end finals shared by CSI and SS3. Returns TUI_KEY_NONE if the
 * final byte is not one of them. */
static uint8_t cursor_final(unsigned char b) {
    switch (b) {
    case 'A': return TUI_KEY_UP;
    case 'B': return TUI_KEY_DOWN;
    case 'C': return TUI_KEY_RIGHT;
    case 'D': return TUI_KEY_LEFT;
    case 'H': return TUI_KEY_HOME;
    case 'F': return TUI_KEY_END;
    }
    return TUI_KEY_NONE;
}

static uint8_t tilde_key(int n) {
    switch (n) {
    case 1: case 7: return TUI_KEY_HOME;
    case 2:         return TUI_KEY_INSERT;
    case 3:         return TUI_KEY_DELETE;
    case 4: case 8: return TUI_KEY_END;
    case 5:         return TUI_KEY_PGUP;
    case 6:         return TUI_KEY_PGDN;
    }
    if (n >= 11 && n <= 15) /* F1-F5 */
        return (uint8_t)(TUI_KEY_F1 + n - 11);
    if (n >= 17 && n <= 21) /* F6-F10 */
        return (uint8_t)(TUI_KEY_F6 + n - 17);
    if (n == 23 || n == 24) /* F11, F12 */
        return (uint8_t)(TUI_KEY_F11 + n - 23);
    return TUI_KEY_NONE;
}

static bool csi_final(tui_key_decoder *d, unsigned char b, tui_key *out) {
    int mod = d->nparam >= 1 ? d->param[1] : 0;
    uint8_t kind;

    if (b == '~') {
        kind = tilde_key(d->param_seen || d->nparam ? d->param[0] : 0);
        if (kind == TUI_KEY_NONE)
            return false; /* unknown sequence: swallow */
        *out = key_of(kind);
        apply_modifier(out, mod);
        return true;
    }
    if (b == 'Z') { /* Shift-Tab */
        *out = key_of(TUI_KEY_TAB);
        out->shift = true;
        return true;
    }
    kind = cursor_final(b);
    if (kind == TUI_KEY_NONE)
        return false; /* unknown final: swallow */
    *out = key_of(kind);
    apply_modifier(out, mod);
    return true;
}

bool tui_key_feed(tui_key_decoder *d, unsigned char byte, tui_key *out) {
    switch (d->state) {
    case ST_GROUND:
        if (byte == 0x1b) {
            d->state = ST_ESC;
            return false;
        }
        *out = ground_key(byte);
        return true;

    case ST_ESC:
        if (byte == '[') {
            d->state = ST_CSI;
            return false;
        }
        if (byte == 'O') {
            d->state = ST_SS3;
            return false;
        }
        if (byte == 0x1b) { /* ESC ESC: report one, keep waiting */
            *out = key_of(TUI_KEY_ESC);
            return true;
        }
        /* Alt as ESC prefix */
        reset(d);
        *out = ground_key(byte);
        out->alt = true;
        return true;

    case ST_CSI:
        if (byte == 0x1b) { /* truncated sequence (dropped bytes): restart */
            reset(d);
            d->state = ST_ESC;
            return false;
        }
        if (byte >= '0' && byte <= '9') {
            int i = d->nparam;
            d->param[i] = d->param[i] * 10 + (byte - '0');
            d->param_seen = true;
            return false;
        }
        if (byte == ';') {
            if (d->nparam < 1)
                d->nparam++;
            d->param_seen = false;
            return false;
        }
        if (byte >= 0x40 && byte <= 0x7e) { /* final byte */
            bool got = csi_final(d, byte, out);
            reset(d);
            return got;
        }
        return false; /* other intermediates: keep collecting */

    case ST_SS3: {
        reset(d);
        if (byte == 0x1b) { /* truncated sequence (dropped bytes): restart */
            d->state = ST_ESC;
            return false;
        }
        if (byte >= 'P' && byte <= 'S') { /* F1-F4 */
            *out = key_of((uint8_t)(TUI_KEY_F1 + byte - 'P'));
            return true;
        }
        uint8_t kind = cursor_final(byte);
        if (kind == TUI_KEY_NONE)
            return false;
        *out = key_of(kind);
        return true;
    }
    }
    reset(d);
    return false;
}

bool tui_key_flush(tui_key_decoder *d, tui_key *out) {
    bool pending = d->state != ST_GROUND;
    bool was_esc = d->state == ST_ESC;
    reset(d);
    if (was_esc) {
        *out = key_of(TUI_KEY_ESC);
        return true;
    }
    (void)pending; /* mid-CSI/SS3 timeout: drop the partial sequence */
    return false;
}

/* Gap after ESC before we call it a lone ESC key. Escape-sequence bytes
 * arrive back-to-back even over ssh; a human never types this fast. */
#define ESC_TIMEOUT_MS 50

tui_key tui_read_key(int timeout_ms) {
    static tui_key_decoder d; /* survives split-across-reads sequences */
    tui_key k;

    int b = tui_read_byte(d.state == ST_GROUND ? timeout_ms : ESC_TIMEOUT_MS);
    for (;;) {
        if (b < 0) {
            if (tui_key_flush(&d, &k))
                return k;
            return key_of(TUI_KEY_NONE);
        }
        if (tui_key_feed(&d, (unsigned char)b, &k))
            return k;
        b = tui_read_byte(d.state == ST_GROUND ? 0 : ESC_TIMEOUT_MS);
    }
}

void tui_run(void *state, tui_handle_fn handle, tui_draw_fn draw) {
    tui_surface *s = tui_screen();
    for (;;) {
        tui_frame_begin(s);
        draw(s, state);
        tui_present();
        tui_key k = tui_read_key(-1);
        if (k.kind == TUI_KEY_NONE)
            continue;
        if (!handle(k, state))
            return;
    }
}
