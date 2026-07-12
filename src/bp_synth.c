/* bp_synth.c - see bp_synth.h
 * Q32 phase-accumulator oscillators, integer Chamberlin SVF run
 * 2x per sample, linear AD envelope. Floats appear only in setup paths
 * (preset load, note pitch), never per sample.
 *
 * Two voices: voice 0 is the user's instrument (bp_params() edits it),
 * voice 1 is the fixed accompaniment. Each demo pattern has a lead track
 * and a back track; the preset's def_role says which one the user plays,
 * and the pattern supplies a default preset for the other track. */

#include "bp_synth.h"
#include "bp_patterns.h" /* pattern types + data, P_* preset indices, DR_* */

#include <string.h>

/* ---- Presets: the 10 MVP controls ----
 * Order must match the P_* indices in bp_synth.h. */
static const bp_preset k_presets[BP_NUM_PRESETS] = {
    /* name           wave           det  sub  cut  res  envA  atk  dec  sus glide drv cho shift pat          role gain */
    { "808 Sub Bass", BP_WAVE_SINE,    0,   0, 255,   0,    0,   2, 350,  0,   0,  51,  0, 12, PAT_HUMBLE,    BP_ROLE_LEAD, 480 },
    { "Funk Bass",    BP_WAVE_SQUARE,  0, 180,  60,  60,  140,   2, 180,  0,   0, 120,  0, 12, PAT_CHAMELEON, BP_ROLE_BACK, 240 },
    { "Acid 303",     BP_WAVE_SAW,     0,   0,  70, 200,  170,   2, 220,  0,  60, 150,  0, 12, PAT_BREATHE,   BP_ROLE_LEAD, 190 },
    { "Reese Bass",   BP_WAVE_SAW,   200,   0,  90,  40,    0,   5, 250,  1,   0,  60,  0, 12, PAT_GANGNAM,   BP_ROLE_BACK, 360 },
    { "80s Brass",    BP_WAVE_SAW,    60, 120, 120,  50,  120,  60, 300,  1,   0,   0,120, 12, PAT_DREAMS,    BP_ROLE_LEAD, 120 },
    { "G-Funk Lead",  BP_WAVE_SINE,    0,   0, 255,   0,    0,   1, 250,  1,  30,  25,  0, 24, PAT_TILL,      BP_ROLE_LEAD, 300 },
    { "Synthwave Ld", BP_WAVE_SAW,   100,  40, 200,  30,    0,  15, 400,  1,   0,   0,120, 24, PAT_AHA,       BP_ROLE_LEAD, 130 },
    { "Synthpop Plk", BP_WAVE_SQUARE, 30,   0, 170,  40,   70,   1, 260,  0,   0,   0,140, 24, PAT_SMALLTOWN, BP_ROLE_LEAD, 160 },
    /* Hoover Lead: Acid 303 with sustain + shorter release; held notes
     * ride the glide through legato splits. */
    { "Hoover Lead",  BP_WAVE_SAW,     0,   0,  70, 200,  170,   2, 100,  1,  60, 150,  0, 12, PAT_IMPALA,    BP_ROLE_LEAD, 120 },
    { "Dance Brass",  BP_WAVE_SAW,    76,   0,  79,   0,  200,   2, 220,  0,   0,  50,100, 12, PAT_LOVE,      BP_ROLE_LEAD, 160 },
    { "Picked Bass",  BP_WAVE_SQUARE,  0,  80,  70,  30,   60,   2, 140,  0,   0,   0,  0, 12, PAT_IFEEL,     BP_ROLE_BACK, 380 },
    /* Octave Bass: Picked Bass with sustain on. */
    { "Octave Bass",  BP_WAVE_SQUARE,  0,  80,  70,  30,   60,   2, 140,  1,   0,   0,  0, 12, PAT_ENJOY,     BP_ROLE_BACK, 300 },
};

/* ---- Synth state ---- */

typedef struct {
    /* oscillators */
    uint32_t ph1, ph2, phs;    /* main, detuned twin, sub */
    uint32_t inc;              /* current (glide-slewed) phase increment */
    uint32_t inc_target;
    uint32_t glide_g;          /* Q16 slew coefficient per sample, 65536 = instant */
    uint32_t det_q16;          /* twin offset: inc2 = inc + inc*det>>16 */
    /* envelope, Q15 */
    int32_t  env;
    int32_t  atk_step, dec_step;
    uint8_t  env_stage;        /* 0 idle, 1 attack, 2 sustain/decay */
    uint8_t  gate;
    int32_t  vel;              /* note velocity 0..127, scales the amp */
    /* filter */
    int32_t  flp, fbp;         /* SVF state */
    int32_t  qcoef;            /* Q16 damping (1/Q) */
} synth_t;

typedef struct {
    bp_preset p;               /* params (voice 0's are edited by the UI) */
    synth_t s;
    int note;                  /* sounding MIDI note incl. shift */
} voice_t;

static voice_t g_v[2];          /* 0 = user, 1 = accompaniment */
static int g_role = BP_ROLE_LEAD; /* which track voice 0 plays */
static bool g_solo = false;     /* mute the accompaniment */
static int32_t g_ftab[256];     /* cutoff control -> SVF f coefficient, Q16 */

/* visualization taps (voice 0 unless noted) */
static uint32_t g_note_events;
static int g_peak;              /* of the mix */
#define SCOPE_LEN 512
static int16_t g_scope[SCOPE_LEN];
static int g_scope_w;

/* f = 2*sin(pi*fc/(2*RATE)), Q16, for the 2x-iterated SVF. Small angles
 * (max ~0.29 rad at 8 kHz), so sin x ~= x - x^3/6 is plenty. Table built
 * once with floats: fc = 40 * 2^(i/32), capped at 8 kHz. */
static void build_ftab(void)
{
    const float r = 1.0218971487f;   /* 2^(1/32) */
    float fc = 40.0f;
    for (int i = 0; i < 256; i++) {
        float f = fc > 8000.0f ? 8000.0f : fc;
        float a = 3.14159265f * f / (2.0f * BP_RATE);
        float s = a - a * a * a * (1.0f / 6.0f);
        g_ftab[i] = (int32_t)(2.0f * s * 65536.0f);
        fc *= r;
    }
}

static void derive(voice_t *v)
{
    synth_t *s = &v->s;
    const bp_preset *p = &v->p;
    s->det_q16 = (uint32_t)p->detune * 8;           /* max ~3.1% */
    s->atk_step = (int32_t)(32767.0f / (p->attack_ms * 44.1f) + 1.0f);
    s->dec_step = (int32_t)(32767.0f / (p->decay_ms * 44.1f) + 1.0f);
    s->glide_g = p->glide_ms == 0
                 ? 65536u
                 : (uint32_t)(65536.0f / (p->glide_ms * 44.1f) + 1.0f);
    /* damping 1/Q: ~1.9 (dead) down to ~0.1 (screaming) */
    s->qcoef = 124518 - (int32_t)p->res * 462;
}

void bp_params_changed(void) { derive(&g_v[0]); }

void bp_load_preset(int idx)
{
    g_v[0].p = k_presets[idx];
    g_role = k_presets[idx].def_role;
    bp_params_changed();
}

const bp_preset *bp_preset_ref(int idx) { return &k_presets[idx]; }
bp_preset *bp_params(void) { return &g_v[0].p; }

void bp_set_solo(bool on) { g_solo = on; }
bool bp_solo(void) { return g_solo; }
const char *bp_comp_name(void) { return g_v[1].p.name; }

/* Hz readout: fc = 40 * 2^(ctl/32), capped at 8 kHz. 32 Q16 fractional
 * steps of 2^(j/32) cover the mantissa; whole octaves shift. */
int bp_cutoff_hz(int ctl)
{
    static const uint32_t frac[32] = {
        65536, 66968, 68431, 69927, 71455, 73017, 74613, 76244,
        77910, 79613, 81353, 83131, 84948, 86805, 88702, 90641,
        92622, 94646, 96715, 98829, 100989, 103196, 105452, 107757,
        110113, 112520, 114979, 117492, 120060, 122685, 125367, 128107,
    };
    if (ctl < 0) ctl = 0;
    if (ctl > 255) ctl = 255;
    int hz = (int)(((uint64_t)(40u << (ctl / 32)) * frac[ctl % 32]) >> 16);
    return hz > 8000 ? 8000 : hz;
}

/* MIDI note -> phase increment. Setup-only floats, no libm: walk semitone
 * ratios from A4 = 440 Hz (MIDI 69). */
static uint32_t note_inc(int midi)
{
    float f = 440.0f;
    for (int n = midi; n < 69; n++) f *= 0.943874313f;   /* 2^(-1/12) */
    for (int n = midi; n > 69; n--) f *= 1.059463094f;
    return (uint32_t)(f * (4294967296.0f / BP_RATE));
}

static void note_on(voice_t *v, int midi, int vel)
{
    synth_t *s = &v->s;
    v->note = midi + v->p.note_shift;
    s->vel = vel;
    if (v == &g_v[0]) g_note_events++;
    s->inc_target = note_inc(v->note);
    if (s->env_stage == 0 || s->glide_g == 65536u) s->inc = s->inc_target;
    s->gate = 1;
    s->env_stage = 1;
    /* no phase reset: mono synths keep oscillators free-running */
}

static void note_off(voice_t *v)
{
    v->s.gate = 0;
}

/* Parabolic pseudo-sine on a Q15 phase position, ~+/-8192 out. */
static inline int32_t osc_sine(uint32_t phase)
{
    int32_t t = (int32_t)((phase >> 17) & 0x7fff);
    t = (t < 16384) ? t * 2 - 16384 : 49150 - t * 2;   /* triangle +/-16384 */
    int32_t a = t < 0 ? -t : t;
    return (t * (32768 - a)) >> 15;
}

static inline int32_t osc_wave(uint32_t phase, int wave)
{
    switch (wave) {
        case BP_WAVE_SAW:    return ((int32_t)((phase >> 17) & 0x7fff) - 16384) >> 1;
        case BP_WAVE_SQUARE: return (phase & 0x80000000u) ? -8192 : 8192;
        default:             return osc_sine(phase);
    }
}

/* Triangle LFO sample on a Q32 phase, +/-16384. */
static inline int32_t lfo_tri(uint32_t phase)
{
    int32_t t = (int32_t)((phase >> 17) & 0x7fff);
    return (t < 16384) ? t * 2 - 16384 : 49150 - t * 2;
}

/* Render one voice; add == false overwrites out, add == true sums into it
 * (with clamp), so the mix costs no extra buffer. Each sample's chorus
 * send (out * p->chorus) is summed into send[]. */
static void render(voice_t *v, int16_t *out, int32_t *send, int n, bool add)
{
    synth_t *s = &v->s;
    const bp_preset *p = &v->p;

    for (int i = 0; i < n; i++) {
        /* envelope */
        if (s->env_stage == 1) {
            s->env += s->atk_step;
            if (s->env >= 32767) { s->env = 32767; s->env_stage = 2; }
        } else if (s->env_stage == 2) {
            if (!(p->sustain && s->gate)) {
                s->env -= s->dec_step;
                if (s->env <= 0) { s->env = 0; s->env_stage = 0; }
            }
        }
        if (s->env_stage == 0) {
            if (!add) out[i] = 0;
            continue;
        }

        /* glide */
        int32_t d = (int32_t)(s->inc_target - s->inc);
        s->inc += (uint32_t)((int32_t)(((int64_t)d * s->glide_g) >> 16));

        uint32_t inc = s->inc;

        /* oscillators */
        int32_t mix = osc_wave(s->ph1, p->wave);
        if (p->detune) mix += osc_wave(s->ph2, p->wave);
        if (p->sub) {
            int32_t sq = (s->phs & 0x80000000u) ? -8192 : 8192;
            mix += (sq * p->sub) >> 8;
        }
        s->ph1 += inc;
        s->ph2 += inc + (uint32_t)(((uint64_t)inc * s->det_q16) >> 16);
        s->phs += inc >> 1;

        /* filter: cutoff control + env*amount, then 2x-iterated SVF */
        int32_t c = p->cutoff + ((s->env * p->env_amt) >> 15);
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        int32_t f = g_ftab[c];
        for (int k = 0; k < 2; k++) {
            int32_t hp = mix - s->flp - (int32_t)(((int64_t)s->fbp * s->qcoef) >> 16);
            s->fbp += (int32_t)(((int64_t)f * hp) >> 16);
            s->flp += (int32_t)(((int64_t)f * s->fbp) >> 16);
            if (s->fbp > 262144) s->fbp = 262144;          /* resonance clamp */
            else if (s->fbp < -262144) s->fbp = -262144;
        }

        /* amp: env and note velocity, then per-preset make-up gain */
        int32_t y = (s->flp * s->env) >> 15;
        y = (y * s->vel) >> 7;

        /* drive: input gain (1x..4x) into a cubic soft clipper around
         * +/-8192 (the nominal osc level); drive 0 bypasses exactly */
        if (p->drive) {
            int32_t x = (y * (256 + 3 * (int32_t)p->drive)) >> 8;
            if (x > 8192) x = 8192;
            else if (x < -8192) x = -8192;
            y = x - (((((x * x) >> 13) * x) / 3) >> 13);
        }

        y = (y * p->gain) >> 8;
        if (p->chorus) send[i] += (y * p->chorus) >> 8;
        if (add) y += out[i];
        if (y > 32767) y = 32767;
        else if (y < -32767) y = -32767;
        out[i] = (int16_t)y;
    }
}

/* ---- Chorus: one shared modulated delay line ----
 *
 * Both voices sum their sends into it; the wet tap is added back onto the
 * mix. Base delay ~25 ms, a slow triangle sweeps the read tap +/-8 ms with
 * linear interpolation between samples. Fixed rate/depth -- the preset's
 * chorus value (the send amount) is the only control. */

#define CHO_RUN   128                     /* send-buffer granularity */
#define CHO_LEN   2048                    /* ~46 ms at 44.1 kHz, power of 2 */
#define CHO_BASE  1102                    /* 25 ms */
#define CHO_SWEEP 353                     /* +/-8 ms */
#define CHO_INC   48696u                  /* 0.5 Hz as a Q32 phase step */

static int16_t g_cho_buf[CHO_LEN];
static int g_cho_w;
static uint32_t g_cho_ph;

static void chorus(int16_t *out, const int32_t *send, int n)
{
    for (int i = 0; i < n; i++) {
        int32_t x = send[i];
        if (x > 32767) x = 32767;
        else if (x < -32767) x = -32767;
        g_cho_buf[g_cho_w] = (int16_t)x;

        /* read tap: base + triangle sweep, Q8 fractional for interp */
        int32_t off = CHO_BASE * 256 + lfo_tri(g_cho_ph) * CHO_SWEEP / 64;
        g_cho_ph += CHO_INC;
        int idx = g_cho_w - (off >> 8);
        int frac = off & 255;
        int32_t a = g_cho_buf[(idx - 0) & (CHO_LEN - 1)];
        int32_t b = g_cho_buf[(idx - 1) & (CHO_LEN - 1)];
        int32_t wet = a + (((b - a) * frac) >> 8);

        g_cho_w = (g_cho_w + 1) & (CHO_LEN - 1);
        int32_t y = out[i] + wet;
        if (y > 32767) y = 32767;
        else if (y < -32767) y = -32767;
        out[i] = (int16_t)y;
    }
}

/* ---- Drum kit: three one-shot generators (kick, snare, hat) ----
 *
 * A third fixed "instrument" beside the two synth voices: sine thump with
 * a pitch drop (kick), noise burst + tone body (snare), differentiated
 * (high-passed) noise tick (hat). Each is a level with an exponential
 * shift-decay; all integer, no filter, mixed straight into the output. */

/* DR_KICK/DR_SNARE/DR_HAT (GM drum notes) are defined in bp_patterns.h. */

#define KICK_INC0   15582648u     /* 160 Hz as a Q32 phase step */
#define KICK_INCMIN  4382620u     /* 45 Hz pitch-drop floor */
#define SNARE_INC   17530479u     /* 180 Hz body tone */

static struct {
    int32_t  k_amp; uint32_t k_ph, k_inc;
    int32_t  s_amp; uint32_t s_ph;
    int32_t  h_amp; int32_t h_prev;
    uint32_t rng;
} g_drum = { .rng = 0x12345678u };

static void drum_trigger(int note, int vel)
{
    int32_t amp = vel << 8;                    /* 0..32512 */
    switch (note) {
    case DR_KICK:
        g_drum.k_amp = amp;
        g_drum.k_ph = 0;
        g_drum.k_inc = KICK_INC0;
        break;
    case DR_SNARE:
        g_drum.s_amp = amp;
        break;
    case DR_HAT:
        g_drum.h_amp = amp;
        break;
    }
}

/* Sum the sounding drums into buf (post-chorus would be fine too, but the
 * kit is dry so it joins the mix before the clamp). */
static void drums_render(int16_t *buf, int n)
{
    if (!(g_drum.k_amp | g_drum.s_amp | g_drum.h_amp)) return;
    for (int i = 0; i < n; i++) {
        int32_t y = 0;
        if (g_drum.k_amp) {
            y += (osc_sine(g_drum.k_ph) * g_drum.k_amp) >> 14;
            g_drum.k_ph += g_drum.k_inc;
            g_drum.k_inc -= g_drum.k_inc >> 11;         /* pitch drop */
            if (g_drum.k_inc < KICK_INCMIN) g_drum.k_inc = KICK_INCMIN;
            g_drum.k_amp -= (g_drum.k_amp >> 11) + 1;   /* ~46 ms decay */
        }
        if (g_drum.s_amp | g_drum.h_amp) {
            g_drum.rng = g_drum.rng * 1664525u + 1013904223u;
            int32_t nz = (int32_t)(g_drum.rng >> 16) - 32768;
            if (g_drum.s_amp) {
                y += ((nz >> 2) * g_drum.s_amp) >> 15;      /* rattle */
                y += (osc_sine(g_drum.s_ph) * g_drum.s_amp) >> 16;
                g_drum.s_ph += SNARE_INC;
                g_drum.s_amp -= (g_drum.s_amp >> 10) + 1;   /* ~23 ms */
            }
            if (g_drum.h_amp) {
                int32_t hp = (nz - g_drum.h_prev) >> 1;     /* one-zero HP */
                g_drum.h_prev = nz;
                y += ((hp >> 2) * g_drum.h_amp) >> 15;
                g_drum.h_amp -= (g_drum.h_amp >> 8) + 1;    /* ~6 ms tick */
            }
        }
        y += buf[i];
        if (y > 32767) y = 32767;
        else if (y < -32767) y = -32767;
        buf[i] = (int16_t)y;
    }
}

/* ---- Demo sequencer: plays the event-list loops from bp_patterns.c ---- */

static int g_pat = 0;          /* active pattern */
static int g_tick = 0;         /* 0..ticks-1 */
static int g_tick_smp = 0;     /* samples into the current tick */

/* Per-track playback cursor. next/off are absolute loop ticks; off < 0
 * means no gate-off pending. [0]/[1] follow pattern tracks, [2] drums. */
typedef struct { uint16_t idx; int next, off; } tstate_t;
static tstate_t g_tst[3];

/* Track index a voice plays: voice 0 follows g_role. */
static inline int voice_track(int v) { return v == 0 ? g_role : 1 - g_role; }

static const bp_track *track_ref(const pattern_t *p, int t)
{
    return t < 2 ? &p->tr[t] : &p->drums;
}

/* Rewind all cursors to tick 0. Melodic gates that extend exactly to the
 * loop end carry over so a final legato note hands off to the first. */
static void seq_rewind(bool keep_gates)
{
    const pattern_t *p = &k_patterns[g_pat];
    g_tick = 0;
    for (int t = 0; t < 3; t++) {
        const bp_track *tr = track_ref(p, t);
        g_tst[t].idx = 0;
        g_tst[t].next = tr->n ? tr->ev[0].delta : -1;
        g_tst[t].off = (keep_gates && g_tst[t].off >= p->ticks)
                       ? g_tst[t].off - p->ticks : -1;
    }
}

void bp_select_pattern(int idx)
{
    const pattern_t *p = &k_patterns[idx];
    g_pat = idx;
    g_tick_smp = 0;
    /* the accompaniment plays whichever track the user doesn't */
    g_v[1].p = k_presets[g_role == BP_ROLE_LEAD ? p->back_preset
                                                : p->lead_preset];
    derive(&g_v[1]);
    for (int v = 0; v < 2; v++) note_off(&g_v[v]);
    seq_rewind(false);
}

/* Fire the current tick's events for one melodic voice. A gate-off that
 * lands on a new note's onset is skipped: the note plays legato (and with
 * glide, slides). */
static void seq_tick_voice(int v)
{
    const pattern_t *p = &k_patterns[g_pat];
    const bp_track *tr = track_ref(p, voice_track(v));
    tstate_t *ts = &g_tst[voice_track(v)];
    bool starts = ts->idx < tr->n && ts->next == g_tick &&
                  tr->ev[ts->idx].vel != 0;
    if (ts->off == g_tick && !starts) {
        note_off(&g_v[v]);
        ts->off = -1;
    }
    while (ts->idx < tr->n && ts->next == g_tick) {
        const bp_ev *e = &tr->ev[ts->idx];
        if (e->vel) {
            note_on(&g_v[v], e->note, e->vel);
            ts->off = g_tick + e->gate;
        }
        if (++ts->idx < tr->n) ts->next += tr->ev[ts->idx].delta;
    }
}

static void seq_tick_drums(void)
{
    const bp_track *tr = &k_patterns[g_pat].drums;
    tstate_t *ts = &g_tst[2];
    while (ts->idx < tr->n && ts->next == g_tick) {
        const bp_ev *e = &tr->ev[ts->idx];
        if (e->vel) drum_trigger(e->note, e->vel);
        if (++ts->idx < tr->n) ts->next += tr->ev[ts->idx].delta;
    }
}

int bp_pattern(void) { return g_pat; }
int bp_role(void) { return g_role; }
const char *bp_pattern_name(int idx) { return k_patterns[idx].name; }
int bp_pattern_bpm(int idx) { return k_patterns[idx].bpm; }

/* Render one chunk, running the sequencer tick-accurately against the
 * render clock. Every event (note on/off, drum hit) lands on a tick
 * boundary, so the loop just renders up to the next boundary. */
void bp_run(int16_t *buf, int nframes)
{
    const pattern_t *p = &k_patterns[g_pat];
    const int tick_len = BP_RATE * 60 / (p->bpm * 48);
    int filled = 0;

    while (filled < nframes) {
        if (g_tick_smp == 0) {
            for (int v = 0; v < 2; v++) seq_tick_voice(v);
            seq_tick_drums();
        }

        int run = tick_len - g_tick_smp;
        if (run > nframes - filled) run = nframes - filled;
        if (run > CHO_RUN) run = CHO_RUN;   /* chorus send buffer bound */
        static int32_t send[CHO_RUN];
        memset(send, 0, (size_t)run * sizeof send[0]);
        render(&g_v[0], buf + filled, send, run, false);
        if (!g_solo) {
            render(&g_v[1], buf + filled, send, run, true);
            drums_render(buf + filled, run);
        }
        chorus(buf + filled, send, run);
        filled += run;
        g_tick_smp += run;

        if (g_tick_smp >= tick_len) {
            g_tick_smp = 0;
            if (++g_tick >= p->ticks) seq_rewind(true);
        }
    }

    /* visualization taps */
    int peak = 0;
    for (int i = 0; i < nframes; i++) {
        int a = buf[i] < 0 ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    g_peak = peak;
    for (int i = 0; i < nframes; i++) {
        g_scope[g_scope_w] = buf[i];
        g_scope_w = (g_scope_w + 1) % SCOPE_LEN;
    }
}

int bp_note(void) { return g_v[0].note; }
bool bp_gate(void) { return g_v[0].s.gate != 0; }
int bp_env(void) { return g_v[0].s.env; }
int bp_env_stage(void) { return g_v[0].s.env_stage; }
uint32_t bp_note_events(void) { return g_note_events; }
int bp_peak(void) { return g_peak; }

void bp_scope(int16_t *out, int n)
{
    if (n > SCOPE_LEN) n = SCOPE_LEN;
    int start = (g_scope_w - n + SCOPE_LEN) % SCOPE_LEN;
    for (int i = 0; i < n; i++)
        out[i] = g_scope[(start + i) % SCOPE_LEN];
}

void bp_init(void)
{
    bp_patterns_init();
    build_ftab();
    bp_load_preset(0);
    bp_select_pattern(k_presets[0].def_pat);
}
