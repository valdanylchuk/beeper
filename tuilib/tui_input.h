/* breezy_tui input: escape-sequence key decoder, blocking key reader, and a
 * tiny optional app loop. The decoder itself (tui_key_feed) is a pure
 * feed-bytes state machine with no tty access, so tests can drive it with
 * byte tables; tui_read_key wraps it over tui_read_byte(). */
#ifndef TUI_INPUT_H
#define TUI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "tui_core.h"

typedef enum {
    TUI_KEY_NONE = 0, /* timeout, nothing decoded */
    TUI_KEY_CHAR,     /* printable or ctrl-letter; see .ch and .ctrl */
    TUI_KEY_ENTER,
    TUI_KEY_ESC,
    TUI_KEY_TAB,
    TUI_KEY_BACKSPACE,
    TUI_KEY_UP,
    TUI_KEY_DOWN,
    TUI_KEY_LEFT,
    TUI_KEY_RIGHT,
    TUI_KEY_HOME,
    TUI_KEY_END,
    TUI_KEY_PGUP,
    TUI_KEY_PGDN,
    TUI_KEY_INSERT,
    TUI_KEY_DELETE,
    TUI_KEY_F1, /* F1..F12 are contiguous: TUI_KEY_F1 + n */
    TUI_KEY_F2,
    TUI_KEY_F3,
    TUI_KEY_F4,
    TUI_KEY_F5,
    TUI_KEY_F6,
    TUI_KEY_F7,
    TUI_KEY_F8,
    TUI_KEY_F9,
    TUI_KEY_F10,
    TUI_KEY_F11,
    TUI_KEY_F12
} tui_key_kind;

typedef struct {
    uint8_t kind; /* tui_key_kind */
    char ch;      /* the character for TUI_KEY_CHAR (lowercase letter when
                   * ctrl is set: Ctrl-A -> ch='a', ctrl=true), else 0 */
    bool ctrl, alt, shift; /* decoded where the terminal reports them */
} tui_key;

/* --- pure decoder --- */

/* Decoder state; zero-initialize (or tui_key_decoder d = {0};). */
typedef struct {
    uint8_t state;    /* internal */
    int param[2];     /* CSI numeric params being accumulated */
    int nparam;
    bool param_seen;  /* digits seen for param[nparam] */
} tui_key_decoder;

/* Feed one input byte. Returns true and fills *out when a key completes;
 * returns false while a multi-byte sequence is still pending. */
bool tui_key_feed(tui_key_decoder *d, unsigned char byte, tui_key *out);

/* Resolve a pending partial sequence on timeout: a lone ESC (or ESC that
 * turned out not to start a sequence) becomes TUI_KEY_ESC. Returns true and
 * fills *out if there was something pending, false otherwise. Resets the
 * decoder either way. */
bool tui_key_flush(tui_key_decoder *d, tui_key *out);

/* --- terminal reader (wraps tui_read_byte) --- */

/* Read and decode one key, waiting up to timeout_ms for the first byte
 * (0 = poll, negative = block). Follow-up bytes of an escape sequence use a
 * short internal timeout, so a lone ESC key is returned promptly. Returns a
 * key with kind == TUI_KEY_NONE on timeout. */
tui_key tui_read_key(int timeout_ms);

/* --- optional app loop --- */

/* Return false to leave the loop. */
typedef bool (*tui_handle_fn)(tui_key key, void *state);
typedef void (*tui_draw_fn)(tui_surface *s, void *state);

/* Minimal fixed loop for simple apps: draw (frame_begin + draw + present),
 * block for a key, dispatch. vi-style apps that own their loop skip this. */
void tui_run(void *state, tui_handle_fn handle, tui_draw_fn draw);

/* --- focus convention ---
 * Apps keep `int focus` themselves; Tab cycles it and each widget gets a
 * `focused` bool that picks its theme attrs. This helper is the whole
 * "framework": step +1 on Tab, -1 on Shift-Tab. */
static inline int tui_focus_step(int focus, int count, int dir) {
    return count > 0 ? (focus + dir + count) % count : 0;
}

#endif /* TUI_INPUT_H */
