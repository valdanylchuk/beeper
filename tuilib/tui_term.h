/* breezy_tui terminal backend: raw mode, size detection, buffered output,
 * byte-level input with timeout. POSIX termios only; keep out of tui_core so
 * the renderer stays testable without a tty. */
#ifndef TUI_TERM_H
#define TUI_TERM_H

#include "tui_core.h"

/* Enter raw mode, detect screen size, allocate the double buffer (the only
 * allocation in the library), hide cursor, clear screen. Returns 0 on
 * success, -1 on error. Terminal state is restored on tui_shutdown(), at
 * exit, and on SIGINT/SIGTERM. */
int tui_init(void);
void tui_shutdown(void);

/* The screen surface set up by tui_init(). */
tui_surface *tui_screen(void);

/* Diff the screen surface and flush the minimal ANSI to stdout (one write). */
void tui_present(void);

/* Read one byte from stdin, waiting up to timeout_ms (0 = poll,
 * negative = block). Returns the byte, or -1 on timeout/error. */
int tui_read_byte(int timeout_ms);

/* Temporarily hand the terminal back (shelling out to another program):
 * tui_suspend() restores cooked mode, leaves the alt screen and shows the
 * cursor; tui_resume() re-enters raw mode and invalidates the front buffer
 * so the next tui_present() repaints everything. */
void tui_suspend(void);
int tui_resume(void);

#endif /* TUI_TERM_H */
