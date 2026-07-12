/* bp_synth: beeper's synth engine -- subtractive mono voice, presets, and
 * the demo step sequencer. Pure DSP + state: no audio output, no tty, no
 * floats in the render path (setup paths only). The TUI (beeper.c) edits
 * bp_params() live and reads the taps below for visualization.
 *
 * Signal path:
 *
 *   [osc + detuned twin] + [sub square -1 oct] -> resonant LPF -> amp -> drive
 *          ^ glide slew           ^ cutoff += env * amount        ^ env
 *
 * Both voices feed a shared chorus (one modulated delay line);
 * each preset's chorus value is its send amount into it, so a lush pad and
 * a dry sub bass coexist.
 */
#ifndef BP_SYNTH_H
#define BP_SYNTH_H

#include <stdbool.h>
#include <stdint.h>

#define BP_RATE         44100
#define BP_NUM_PRESETS  12
#define BP_NUM_PATTERNS 12

/* Named preset indices, so pattern default-preset references (bp_patterns.c)
 * survive reordering. Must match the order of k_presets in bp_synth.c. */
enum {
    P_808, P_FUNK, P_ACID, P_REESE, P_BRASS, P_GFUNK,
    P_SWLEAD, P_PLUCK, P_HOOVER, P_DBRASS, P_PICKBASS, P_OCTBASS,
};

/* Named pattern indices for preset def_pat references (and anywhere else a
 * pattern is picked by number). Must match the order of k_patterns in
 * bp_patterns.c. */
enum {
    PAT_AHA, PAT_TILL, PAT_GANGNAM, PAT_DREAMS, PAT_LOVE, PAT_IMPALA,
    PAT_HUMBLE, PAT_SMALLTOWN, PAT_ENJOY, PAT_BREATHE, PAT_IFEEL, PAT_CHAMELEON,
};

enum { BP_WAVE_SAW = 0, BP_WAVE_SQUARE, BP_WAVE_SINE };

/* Each demo pattern has two melodic tracks (a preset plays one of them)
 * plus an optional built-in drum track. */
enum { BP_ROLE_LEAD = 0, BP_ROLE_BACK };

/* The tweakable controls (+ demo framing). The UI edits these in place and
 * calls bp_params_changed() afterwards to refresh derived coefficients
 * (detune/attack/decay/glide/res); the other fields are read per
 * sample and need no refresh. */
typedef struct {
    const char *name;
    uint8_t wave;
    uint8_t detune;     /* 0..255 -> 0..~3% twin osc offset */
    uint8_t sub;        /* 0..255 sub square level */
    uint8_t cutoff;     /* 0..255 exponential, ~40 Hz..~8 kHz */
    uint8_t res;        /* 0..255 */
    uint8_t env_amt;    /* 0..255, env -> cutoff */
    uint16_t attack_ms;
    uint16_t decay_ms;  /* doubles as release */
    uint8_t sustain;    /* 1 = hold at peak while gate on */
    uint16_t glide_ms;
    uint8_t drive;      /* 0..255 soft-clip input gain, 0 = clean bypass */
    uint8_t chorus;     /* 0..255 send into the shared chorus, 0 = dry */
    uint8_t note_shift; /* demo: semitones added to the loop */
    uint8_t def_pat;    /* demo: pattern that suits this preset */
    uint8_t def_role;   /* demo: track this preset plays in def_pat */
    uint16_t gain;      /* output make-up gain, Q8 */
} bp_preset;

/* --- setup / presets --- */
void bp_init(void);            /* build tables + load preset 0; call once */
void bp_load_preset(int idx);  /* copy preset idx into the live params */
const bp_preset *bp_preset_ref(int idx); /* factory values (for '*' dirty) */

/* Live (tweakable) parameter block. */
bp_preset *bp_params(void);
void bp_params_changed(void);

/* Cutoff control value (0..255) -> filter frequency in Hz (40..8000),
 * for readouts. Integer math. */
int bp_cutoff_hz(int ctl);

/* --- sequencer --- */
void bp_select_pattern(int idx);   /* also (re)loads the accompaniment */
int bp_pattern(void);
int bp_role(void);                 /* track the user's voice plays */
const char *bp_pattern_name(int idx);
int bp_pattern_bpm(int idx);

/* Accompaniment voice: fixed preset on the pattern's other track. */
void bp_set_solo(bool on);         /* true = mute the accompaniment */
bool bp_solo(void);
const char *bp_comp_name(void);    /* accompaniment preset name */

/* Render nframes of the looping demo beat into out (mono int16). */
void bp_run(int16_t *out, int nframes);

/* --- visualization taps (valid after each bp_run) --- */
int bp_note(void);        /* sounding MIDI note incl. note_shift */
bool bp_gate(void);
int bp_env(void);         /* envelope level, 0..32767 */
int bp_env_stage(void);   /* 0 idle, 1 attack, 2 sustain/decay */
uint32_t bp_note_events(void); /* increments on every note_on */
int bp_peak(void);        /* max |sample| of the last bp_run, 0..32767 */

/* Most recent rendered samples, oldest first: fills out[0..n-1]. */
void bp_scope(int16_t *out, int n);

#endif /* BP_SYNTH_H */
