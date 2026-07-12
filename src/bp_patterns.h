/* bp_patterns.h -- demo loop sequences for the beeper synth.
 *
 * Each pattern is up to three tracks of timed note events at 48 ticks per beat
 * (fine enough for swing and light humanization). An event is
 * {delta, note, vel, gate}: delta ticks after the previous event, MIDI
 * note before the preset's note_shift, velocity 0..127, and gate ticks the
 * note is held. A gate that reaches the next event's onset plays legato
 * (with glide > 0 it slides, 303-style). vel 0 is a no-op spacer for gaps
 * longer than 255 ticks. Drum track notes are DR_KICK/DR_SNARE/DR_HAT.
 *
 * Tracks are written as score strings (bp_score) and expanded into bp_ev
 * arrays once, by bp_patterns_init(). Score grammar -- a stream of tokens
 * over three sticky registers, delta D, velocity V and gate G:
 *
 *   c4 f#2 ...   note (pitch as written in the song; the track's transpose
 *                is added on expansion), sounding D ticks after the
 *                previous event.  K / S / h are kick / snare / hat.
 *   24           bare number: set D for this and following notes
 *   +c4          this note sounds together with the previous one
 *                (delta 0); D is not changed
 *   c4!100:18    !v / :g suffixes set V / G from this note on; standalone
 *                !v / :g tokens take effect from the next note
 *   ( ... )*4    repeat a span; registers carry across iterations
 *   |            bar line, cosmetic only (ignored)
 */

#ifndef BP_PATTERNS_H
#define BP_PATTERNS_H

#include <stdint.h>

#include "bp_synth.h" /* BP_NUM_PATTERNS, BP_ROLE_*, P_* preset indices */

/* GM drum notes (K / S / h in scores). */
#define DR_KICK  36
#define DR_SNARE 38
#define DR_HAT   42

typedef struct { uint8_t delta, note, vel, gate; } bp_ev;
typedef struct { const bp_ev *ev; uint16_t n; } bp_track;

typedef struct {
    const char *name;
    uint16_t bpm;
    uint16_t ticks;            /* loop length: bars * 4 * 48 */
    uint8_t lead_preset;       /* defaults for whichever track the */
    uint8_t back_preset;       /* user's instrument is NOT playing */
    bp_track tr[2];            /* [BP_ROLE_LEAD], [BP_ROLE_BACK] */
    bp_track drums;            /* optional third track */
} pattern_t;

extern pattern_t k_patterns[BP_NUM_PATTERNS];

/* Expand the score strings into k_patterns. Called by bp_init();
 * safe to call more than once. */
void bp_patterns_init(void);

#endif /* BP_PATTERNS_H */
