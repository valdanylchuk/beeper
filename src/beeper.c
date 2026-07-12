/*
 * beeper - subtractive synth app for BreezyBox
 * Runs on Mac (SDL2 audio via host_sdl.c),
 * as a BreezyBox ELF app on ESP32-S3/P4 (snd_* from the firmware),
 * and as wasm via emscripten and xterm.js.
 *
 *   Up/Down, Tab/Shift-Tab  move focus through the control list
 *   Left/Right              adjust (fine); PgUp/PgDn coarse; Home/End limits
 *   Space/Enter             toggle / cycle the focused choice control
 *   1-9 0 - =, , / .        select preset ('*' in the title = edited)
 *   s                       solo (mute the accompaniment voice)
 *   m                       mute all output (everything keeps running)
 *   q / Ctrl-C / F10        quit
 *
 * A built-in two-voice pattern loops forever (the Pattern control switches it);
 * the user's preset plays its lead or back track, a fixed second
 * instrument plays the other. Tweak while it plays. Single loop, audio first,
 * keep the PCM ring topped up, then poll keys and redraw at ~30 fps.
 */

#include <stdio.h>
#include <string.h>

#include "bp_synth.h"
#include "tui_controls.h"
#include "tui_core.h"
#include "tui_input.h"
#include "tui_layout.h"
#include "tui_term.h"
#include "tui_widgets.h"

/* ---- Firmware API (elf_loader symbols on device, host_sdl.c on Mac) ---- */

int  snd_init(void);
int  snd_stream_open(int rate, int channels);
int  snd_stream_space(void);
int  snd_stream_write(const int16_t *frames, int nframes);
void snd_stream_close(void);
void snd_set_volume(int pct);   /* 0..100 percent */

typedef uint32_t TickType_t;
void vTaskDelay(TickType_t ticks);

#define CHUNK 256

/* ---- dark theme (device palette is customizable; these indices are
 * chosen against a near-black background) ---- */

static const tui_theme k_dark = {
    .normal       = TUI_ATTR(TUI_WHITE, TUI_BLACK),
    .selected     = TUI_ATTR(TUI_BLACK, TUI_CYAN),
    .border       = TUI_ATTR(TUI_BLACK | TUI_BRIGHT, TUI_BLACK),
    .title        = TUI_ATTR(TUI_CYAN | TUI_BRIGHT, TUI_BLACK),
    .status       = TUI_ATTR(TUI_WHITE, TUI_BLACK | TUI_BRIGHT),
    .keybar_key   = TUI_ATTR(TUI_YELLOW | TUI_BRIGHT, TUI_BLACK),
    .shadow       = TUI_ATTR(TUI_BLACK | TUI_BRIGHT, TUI_BLACK),
    .disabled     = TUI_ATTR(TUI_BLACK | TUI_BRIGHT, TUI_BLACK),
    .menu_sel     = TUI_ATTR(TUI_BLACK, TUI_CYAN),
    .focus        = TUI_ATTR(TUI_BLACK, TUI_YELLOW),
    .slider_track = TUI_ATTR(TUI_BLACK | TUI_BRIGHT, TUI_BLACK),
    .slider_fill  = TUI_ATTR(TUI_CYAN, TUI_BLACK),
    .meter_low    = TUI_ATTR(TUI_GREEN, TUI_BLACK),
    .meter_mid    = TUI_ATTR(TUI_YELLOW, TUI_BLACK),
    .meter_high   = TUI_ATTR(TUI_RED | TUI_BRIGHT, TUI_BLACK),
    .peak         = TUI_ATTR(TUI_WHITE | TUI_BRIGHT, TUI_BLACK),
};

/* ---- control list (the app-side descriptor pattern from PRD_Widgets) ----
 * Values live in bp_params() / app state; descriptors only say how to show
 * and step them. Order = focus order = visual order. */

enum {
    C_WAVE, C_DETUNE, C_SUB, C_OCTAVE,
    C_CUTOFF, C_RESON, C_ENVCUT,
    C_ATTACK, C_DECAY, C_SUSTAIN,
    C_GLIDE, C_DRIVE, C_CHORUS, C_VOLUME, C_PATTERN, C_SOLO,
    NCTL
};

enum { K_SLIDER, K_BIPOLAR, K_STEPPER, K_SELECT, K_TOGGLE };

typedef struct {
    uint8_t kind;
    const char *label;
    int min, max;
    tui_value_fmt_fn fmt;
    const char *const *options; /* K_SELECT */
    int noptions;
} ctl_desc;

static void fmt_hz(void *c, int v, char *b, size_t n) {
    (void)c;
    int hz = bp_cutoff_hz(v);
    if (hz >= 1000) snprintf(b, n, "%d.%dk", hz / 1000, hz % 1000 / 100);
    else snprintf(b, n, "%dHz", hz);
}
static void fmt_pct255(void *c, int v, char *b, size_t n) {
    (void)c; snprintf(b, n, "%d%%", v * 100 / 255);
}
static void fmt_pct(void *c, int v, char *b, size_t n) {
    (void)c; snprintf(b, n, "%d%%", v);
}
static void fmt_ms(void *c, int v, char *b, size_t n) {
    (void)c;
    if (v >= 1000) snprintf(b, n, "%d.%ds", v / 1000, v % 1000 / 100);
    else snprintf(b, n, "%dms", v);
}
static void fmt_oct(void *c, int v, char *b, size_t n) {
    (void)c; snprintf(b, n, "%+d", v);
}
static void fmt_pat(void *c, int v, char *b, size_t n) {
    /* short: the stepper readout caps at 15 chars */
    static const char *short_names[BP_NUM_PATTERNS] = {"Aha", "9PM", "Psy", "Dreams", "Love", "Impala", "Humble", "Smalltown", "Enjoy", "Breathe", "IFeelLove", "Chameleon"};
    (void)c;
    snprintf(b, n, "%s %dbpm", short_names[v], bp_pattern_bpm(v));
}
static const char *k_waves[] = {"Saw", "Sqr", "Sin"};

static const ctl_desc k_ctl[NCTL] = {
    [C_WAVE]    = {K_SELECT,  "Wave",    0, 2,    NULL,       k_waves, 3},
    [C_DETUNE]  = {K_SLIDER,  "Detune",  0, 255,  fmt_pct255, NULL, 0},
    [C_SUB]     = {K_SLIDER,  "Sub",     0, 255,  fmt_pct255, NULL, 0},
    [C_OCTAVE]  = {K_STEPPER, "Octave",  0, 3,    fmt_oct,    NULL, 0},
    [C_CUTOFF]  = {K_SLIDER,  "Cutoff",  0, 255,  fmt_hz,     NULL, 0},
    [C_RESON]   = {K_SLIDER,  "Reson",   0, 255,  fmt_pct255, NULL, 0},
    [C_ENVCUT]  = {K_SLIDER,  "Env>Cut", 0, 255,  NULL,       NULL, 0},
    [C_ATTACK]  = {K_SLIDER,  "Attack",  1, 2000, fmt_ms,     NULL, 0},
    [C_DECAY]   = {K_SLIDER,  "Decay",   40, 3000, fmt_ms,    NULL, 0},
    [C_SUSTAIN] = {K_TOGGLE,  "Sustain", 0, 1,    NULL,       NULL, 0},
    [C_GLIDE]   = {K_SLIDER,  "Glide",   0, 300,  fmt_ms,     NULL, 0},
    [C_DRIVE]   = {K_SLIDER,  "Drive",   0, 255,  fmt_pct255, NULL, 0},
    [C_CHORUS]  = {K_SLIDER,  "Chorus",  0, 255,  fmt_pct255, NULL, 0},
    [C_VOLUME]  = {K_SLIDER,  "Volume",  0, 100,  fmt_pct,    NULL, 0},
    [C_PATTERN] = {K_STEPPER, "Pattern", 0, BP_NUM_PATTERNS - 1,
                   fmt_pat, NULL, 0},
    [C_SOLO]    = {K_TOGGLE,  "Solo",    0, 1,    NULL,       NULL, 0},
};

/* ---- app state ---- */

typedef struct {
    int focus;
    int preset;
    int volume;
    /* spectrum energy simulation (app-side) */
    int bins[12], binpk[12], pkage[12];
    uint32_t seen_events;
    /* output meter */
    int out_level, out_peak, out_pkage;
    bool muted;    /* zero the output, keep everything running */
} app;

static int ctl_get(const app *a, int id)
{
    const bp_preset *p = bp_params();
    switch (id) {
    case C_WAVE:    return p->wave;
    case C_DETUNE:  return p->detune;
    case C_SUB:     return p->sub;
    case C_OCTAVE:  return p->note_shift / 12;
    case C_CUTOFF:  return p->cutoff;
    case C_RESON:   return p->res;
    case C_ENVCUT:  return p->env_amt;
    case C_ATTACK:  return p->attack_ms;
    case C_DECAY:   return p->decay_ms;
    case C_SUSTAIN: return p->sustain;
    case C_CHORUS:  return p->chorus;
    case C_GLIDE:   return p->glide_ms;
    case C_DRIVE:   return p->drive;
    case C_VOLUME:  return a->volume;
    case C_PATTERN: return bp_pattern();
    case C_SOLO:    return bp_solo();
    }
    return 0;
}

static void ctl_set(app *a, int id, int v)
{
    bp_preset *p = bp_params();
    const ctl_desc *d = &k_ctl[id];
    if (v < d->min) v = d->min;
    if (v > d->max) v = d->max;
    switch (id) {
    case C_WAVE:    p->wave = (uint8_t)v; break;
    case C_DETUNE:  p->detune = (uint8_t)v; break;
    case C_SUB:     p->sub = (uint8_t)v; break;
    case C_OCTAVE:  p->note_shift = (uint8_t)(v * 12); break;
    case C_CUTOFF:  p->cutoff = (uint8_t)v; break;
    case C_RESON:   p->res = (uint8_t)v; break;
    case C_ENVCUT:  p->env_amt = (uint8_t)v; break;
    case C_ATTACK:  p->attack_ms = (uint16_t)v; break;
    case C_DECAY:   p->decay_ms = (uint16_t)v; break;
    case C_SUSTAIN: p->sustain = (uint8_t)v; break;
    case C_CHORUS:  p->chorus = (uint8_t)v; break;
    case C_GLIDE:   p->glide_ms = (uint16_t)v; break;
    case C_DRIVE:   p->drive = (uint8_t)v; break;
    case C_VOLUME:
        a->volume = v;
        snd_set_volume(v);
        return;
    case C_PATTERN:
        if (v != bp_pattern()) bp_select_pattern(v);
        return;
    case C_SOLO:
        bp_set_solo(v != 0);
        return;
    }
    bp_params_changed();
}

static void load_preset(app *a, int idx)
{
    a->preset = idx;
    bp_load_preset(idx);
    bp_select_pattern(bp_preset_ref(idx)->def_pat);
}

static bool preset_edited(const app *a)
{
    return memcmp(bp_params(), bp_preset_ref(a->preset),
                  sizeof(bp_preset)) != 0;
}

/* ---- input ---- */

// Wide ms ranges (attack/decay) step proportionally (~6% fine, ~25% coarse)
// so the useful low end isn't a hundred taps from the top.
static int step_value(int id, int v, const ctl_desc *d, int dir, bool coarse)
{
    if (id == C_ATTACK || id == C_DECAY) {
        int step = coarse ? v / 4 : v / 16;
        if (step < (coarse ? 10 : 1)) step = coarse ? 10 : 1;
        v += dir * step;
        return v < d->min ? d->min : v > d->max ? d->max : v;
    }
    return tui_value_step(v, d->min, d->max, dir, coarse);
}

static bool handle(app *a, tui_key k)
{
    int id = a->focus;
    const ctl_desc *d = &k_ctl[id];
    bool choice = d->kind == K_SELECT || d->kind == K_TOGGLE;
    int v = ctl_get(a, id);

    switch (k.kind) {
    case TUI_KEY_CHAR:
        if (k.ch == 'q' || (k.ctrl && k.ch == 'c')) return false;
        if (k.ch >= '1' && k.ch <= '9' && k.ch - '1' < BP_NUM_PRESETS)
            load_preset(a, k.ch - '1');
        else if (k.ch == '0' && BP_NUM_PRESETS >= 10)
            load_preset(a, 9);
        else if (k.ch == '-' && BP_NUM_PRESETS >= 11)
            load_preset(a, 10);
        else if (k.ch == '=' && BP_NUM_PRESETS >= 12)
            load_preset(a, 11);
        else if (k.ch == 's')
            bp_set_solo(!bp_solo());
        else if (k.ch == 'm')
            a->muted = !a->muted;
        else if (k.ch == ',')
            load_preset(a, (a->preset + BP_NUM_PRESETS - 1) % BP_NUM_PRESETS);
        else if (k.ch == '.')
            load_preset(a, (a->preset + 1) % BP_NUM_PRESETS);
        else if (k.ch == ' ' && choice)
            ctl_set(a, id, d->kind == K_TOGGLE ? !v : (v + 1) % d->noptions);
        break;
    case TUI_KEY_ENTER:
        if (choice)
            ctl_set(a, id, d->kind == K_TOGGLE ? !v : (v + 1) % d->noptions);
        break;
    case TUI_KEY_F10:
        return false;
    case TUI_KEY_F5:
        load_preset(a, (a->preset + BP_NUM_PRESETS - 1) % BP_NUM_PRESETS);
        break;
    case TUI_KEY_F6:
        load_preset(a, (a->preset + 1) % BP_NUM_PRESETS);
        break;
    case TUI_KEY_UP:
        a->focus = tui_focus_step(a->focus, NCTL, -1);
        break;
    case TUI_KEY_DOWN:
        a->focus = tui_focus_step(a->focus, NCTL, 1);
        break;
    case TUI_KEY_TAB:
        a->focus = tui_focus_step(a->focus, NCTL, k.shift ? -1 : 1);
        break;
    case TUI_KEY_LEFT:
        ctl_set(a, id, step_value(id, v, d, -1, k.shift));
        break;
    case TUI_KEY_RIGHT:
        ctl_set(a, id, step_value(id, v, d, 1, k.shift));
        break;
    case TUI_KEY_PGUP:
        ctl_set(a, id, step_value(id, v, d, 1, true));
        break;
    case TUI_KEY_PGDN:
        ctl_set(a, id, step_value(id, v, d, -1, true));
        break;
    case TUI_KEY_HOME:
        ctl_set(a, id, d->min);
        break;
    case TUI_KEY_END:
        ctl_set(a, id, d->max);
        break;
    default:
        break;
    }
    return true;
}

/* ---- spectrum / meter simulation (per displayed frame, ~30 fps) ---- */

#define NBINS 12
#define EMAX  1000

static int note_bin(int midi)
{
    int b = (midi - 24) / 5;   /* ~29..84 -> 1..12 */
    return b < 0 ? 0 : b >= NBINS ? NBINS - 1 : b;
}

static void animate(app *a)
{
    /* decay + peak aging */
    for (int b = 0; b < NBINS; b++) {
        a->bins[b] -= (a->bins[b] >> 3) + 2;
        if (a->bins[b] < 0) a->bins[b] = 0;
        if (a->binpk[b] > 0 && ++a->pkage[b] > 12) a->binpk[b] -= 40;
        if (a->binpk[b] < a->bins[b]) {
            a->binpk[b] = a->bins[b];
            a->pkage[b] = 0;
        }
    }

    const bp_preset *p = bp_params();
    int env = bp_env();                       /* 0..32767 */

    /* attack injection on every note event since the last frame */
    uint32_t ev = bp_note_events();
    if (ev != a->seen_events) {
        a->seen_events = ev;
        int b = note_bin(bp_note());
        a->bins[b] += 700;
        if (p->sub && b > 0) a->bins[b - 1] += (700 * p->sub) >> 9;
        /* harmonics: saw > square > sine, gated by how open the filter is */
        int harm = p->wave == BP_WAVE_SAW ? 400
                 : p->wave == BP_WAVE_SINE ? 60 : 300;
        harm = harm * (p->cutoff + 40) / 295;
        for (int h = 1; h <= 3 && b + h < NBINS; h++)
            a->bins[b + h] += harm >> (h - 1);
    }

    /* sustain feed from the live envelope */
    if (env > 0) {
        int b = note_bin(bp_note());
        int feed = env * 600 / 32767;
        if (a->bins[b] < feed) a->bins[b] = feed;
    }

    for (int b = 0; b < NBINS; b++)
        if (a->bins[b] > EMAX) a->bins[b] = EMAX;

    /* output meter: fast attack, slow decay, aging peak-hold */
    int lvl = bp_peak() * EMAX / 32767;
    if (lvl > a->out_level) a->out_level = lvl;
    else a->out_level -= (a->out_level - lvl) >> 2;
    if (a->out_peak > 0 && ++a->out_pkage > 20) a->out_peak -= 25;
    if (a->out_peak < a->out_level) {
        a->out_peak = a->out_level;
        a->out_pkage = 0;
    }
}

/* ---- drawing ---- */

static void draw_ctl(tui_surface *s, tui_rect row, const app *a, int id)
{
    const tui_theme *th = &k_dark;
    const ctl_desc *d = &k_ctl[id];
    bool foc = a->focus == id;
    int v = ctl_get(a, id);

    switch (d->kind) {
    case K_SLIDER:
    case K_BIPOLAR: {
        tui_slider_opts o = {
            .label = d->label, .value = v, .min = d->min, .max = d->max,
            .bipolar = d->kind == K_BIPOLAR, .focused = foc, .fmt = d->fmt,
            .label_w = 8, .readout_w = 5,
        };
        tui_slider(s, row, &o, th);
        break;
    }
    case K_STEPPER: {
        char lbl[10];
        snprintf(lbl, sizeof lbl, "%-8s", d->label);
        tui_stepper(s, row, lbl, v, d->fmt, NULL, foc, th);
        break;
    }
    case K_SELECT: {
        char lbl[10];
        snprintf(lbl, sizeof lbl, "%-8s", d->label);
        tui_select(s, row, lbl, d->options, d->noptions, v, foc, th);
        break;
    }
    case K_TOGGLE:
        tui_toggle(s, row, d->label, v != 0, foc, th);
        break;
    }
}

/* Selected waveshape as 3-row glyph art */
static void wave_glyph(tui_surface *s, tui_rect clip, int x, int y, int wave)
{
    static const char *art[3][3] = {
        {"  /| /| /|", " / |/ |/ |", "/  |  |  |"},
        {" _   _   _", "| |_| |_| |", "         "},
        {" __     __", "/  \\   /  ", "    \\_/   "},
    };
    for (int row = 0; row < 3; row++)
        tui_put_str(s, clip, x, y + row, art[wave][row], k_dark.title);
}

/* Filter response sketch: flat passband, resonance bump at the knee,
 * falloff after. Levels 0..100 across `n` columns. */
static void filter_curve(int *out, int n)
{
    const bp_preset *p = bp_params();
    int knee = p->cutoff * (n - 1) / 255;
    for (int i = 0; i < n; i++) {
        if (i < knee) out[i] = 70;
        else out[i] = 70 - (i - knee) * 240 / n;
        if (i == knee || (i == knee - 1 && knee > 0))
            out[i] = 70 + (int)p->res * 30 / 255;
        if (out[i] < 4) out[i] = 4;
    }
}

/* AD(S) envelope shape + live position marker. */
static void env_shape(int *out, int n, int *marker)
{
    const bp_preset *p = bp_params();
    int atk = p->attack_ms, dec = p->decay_ms;
    int total = atk + dec + (p->sustain ? (atk + dec) / 2 : 0);
    if (total < 1) total = 1;
    int akn = atk * (n - 1) / total;           /* attack knee column */
    int skn = p->sustain ? akn + (n - 1 - akn) / 3 : n - 1;
    for (int i = 0; i < n; i++) {
        if (i <= akn) out[i] = akn ? i * 100 / akn : 100;
        else if (p->sustain && i <= skn) out[i] = 100;
        else out[i] = 100 - (i - (p->sustain ? skn : akn)) * 100 /
                            (n - (p->sustain ? skn : akn));
        if (out[i] < 0) out[i] = 0;
        if (out[i] > 100) out[i] = 100;
    }
    int env = bp_env() * 100 / 32767, stage = bp_env_stage();
    *marker = -1;
    if (stage == 1) {
        *marker = akn ? env * akn / 100 : 0;
    } else if (stage == 2) {
        if (p->sustain && bp_gate()) *marker = skn;
        else
            for (int i = n - 1; i > akn; i--)
                if (out[i] >= env) { *marker = i; break; }
    }
}

static const char *k_note_names[12] = {"C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B"};

/* Bottom row: plain key hints */
static void draw_hints(tui_surface *s, tui_rect r, const tui_theme *th)
{
    static const struct { const char *key, *desc; } k_hints[] = {
        {"Up/Dn", "select"}, {"Lt/Rt", "adjust"}, {"Spc", "toggle"},
        {"1-9,0,-,=", "preset"}, {",/.", "prev/next"}, {"s", "solo"},
        {"m", "mute"}, {"q", "quit"},
    };
    tui_fill(s, r, ' ', th->normal);
    int x = r.x + 1;
    for (size_t i = 0; i < sizeof k_hints / sizeof *k_hints; i++) {
        tui_put_str(s, r, x, r.y, k_hints[i].key, th->keybar_key);
        x += (int)strlen(k_hints[i].key) + 1;
        tui_put_str(s, r, x, r.y, k_hints[i].desc, th->normal);
        x += (int)strlen(k_hints[i].desc) + 2;
    }
}

static void draw(tui_surface *s, const app *a)
{
    const tui_theme *th = &k_dark;
    tui_frame_begin(s);
    tui_fill(s, tui_surface_rect(s), ' ', th->normal);

    int w = s->w < 100 ? s->w : 100;
    int lw = w * 52 / 100;             /* left column width */
    char buf[64];

    /* status bar: app title left; preset readout right, with the name
     * bracketed and highlighted (with dirty '*') */
    tui_rect bar = tui_rect_make(0, 0, s->w, 1);
    tui_statusbar(s, bar, " beeper: toy subtractive synth - try all the controls!", NULL, th);
    char name[40];
    snprintf(buf, sizeof buf, "PRESET %d/%d ", a->preset + 1, BP_NUM_PRESETS);
    snprintf(name, sizeof name, "[ %s ]%s ", bp_preset_ref(a->preset)->name,
             preset_edited(a) ? "*" : "");
    int rx = s->w - (int)(strlen(buf) + strlen(name)) - 1;
    tui_put_str(s, bar, rx, 0, buf, th->status);
    tui_put_str(s, bar, rx + (int)strlen(buf), 0, name,
                TUI_ATTR(TUI_YELLOW | TUI_BRIGHT, TUI_ATTR_BG(th->status)));

    /* --- left column: the controls, in signal-path order --- */

    tui_rect osc = tui_rect_make(0, 1, lw, 7);
    tui_box(s, osc, "OSCILLATOR", th->border);
    tui_rect oi = tui_rect_margin(osc, 1);
    draw_ctl(s, tui_rect_make(oi.x, oi.y, oi.w, 1), a, C_WAVE);
    wave_glyph(s, oi, oi.x + oi.w - 12, oi.y + 1, bp_params()->wave);
    draw_ctl(s, tui_rect_make(oi.x, oi.y + 2, 30, 1), a, C_DETUNE);
    draw_ctl(s, tui_rect_make(oi.x, oi.y + 3, 30, 1), a, C_SUB);
    draw_ctl(s, tui_rect_make(oi.x, oi.y + 4, 20, 1), a, C_OCTAVE);

    tui_rect fl = tui_rect_make(0, 8, lw, 6);
    tui_box(s, fl, "FILTER", th->border);
    tui_rect fi = tui_rect_margin(fl, 1);
    draw_ctl(s, tui_rect_make(fi.x, fi.y, fi.w, 1), a, C_CUTOFF);
    draw_ctl(s, tui_rect_make(fi.x, fi.y + 1, fi.w, 1), a, C_RESON);
    draw_ctl(s, tui_rect_make(fi.x, fi.y + 2, fi.w, 1), a, C_ENVCUT);
    tui_put_str(s, fi, fi.x, fi.y + 3, "curve", th->disabled);
    int curve[24];
    int ncv = fi.w - 8 < 24 ? fi.w - 8 : 24;
    if (ncv > 0) {
        filter_curve(curve, ncv);
        tui_sparkline(s, tui_rect_make(fi.x + 7, fi.y + 3, ncv, 1), curve,
                      ncv, 100, -1, th);
    }

    tui_rect en = tui_rect_make(0, 14, lw, 6);
    tui_box(s, en, "ENVELOPE", th->border);
    tui_rect ei = tui_rect_margin(en, 1);
    draw_ctl(s, tui_rect_make(ei.x, ei.y, 34, 1), a, C_ATTACK);
    draw_ctl(s, tui_rect_make(ei.x, ei.y + 1, 34, 1), a, C_DECAY);
    draw_ctl(s, tui_rect_make(ei.x, ei.y + 2, 20, 1), a, C_SUSTAIN);
    int env[13], marker;
    env_shape(env, 13, &marker);
    tui_sparkline(s, tui_rect_make(ei.x + 36, ei.y, 13, 4), env, 13, 100,
                  marker, th);

    tui_rect pl = tui_rect_make(0, 20, lw, 8);
    tui_box(s, pl, "PLAY", th->border);
    tui_rect pi = tui_rect_margin(pl, 1);
    draw_ctl(s, tui_rect_make(pi.x, pi.y, 34, 1), a, C_GLIDE);
    draw_ctl(s, tui_rect_make(pi.x, pi.y + 1, 34, 1), a, C_DRIVE);
    draw_ctl(s, tui_rect_make(pi.x, pi.y + 2, 34, 1), a, C_CHORUS);
    draw_ctl(s, tui_rect_make(pi.x, pi.y + 3, 34, 1), a, C_VOLUME);
    int note = bp_note();
    snprintf(buf, sizeof buf, "%s%d", k_note_names[note % 12], note / 12 - 1);
    tui_put_str(s, pi, pi.x + 36, pi.y + 3, buf, th->normal);
    tui_put_str(s, pi, pi.x + 40, pi.y + 3, bp_gate() ? "[#]" : "[ ]",
                bp_gate() ? th->meter_low : th->disabled);
    draw_ctl(s, tui_rect_make(pi.x, pi.y + 4, pi.w, 1), a, C_PATTERN);
    draw_ctl(s, tui_rect_make(pi.x, pi.y + 5, 16, 1), a, C_SOLO);
    snprintf(buf, sizeof buf, "with %s (%s)", bp_comp_name(),
             bp_role() == BP_ROLE_LEAD ? "back" : "lead");
    tui_put_str(s, pi, pi.x + 18, pi.y + 5, buf,
                bp_solo() ? th->disabled : th->normal);

    /* --- right column: live visualization --- */

    tui_rect rt = tui_rect_make(lw, 1, w - lw, 0);

    tui_rect sp = tui_rect_make(rt.x, 1, rt.w, 15);
    tui_box(s, sp, "SPECTRUM", th->border);
    tui_bargraph(s, tui_rect_margin(sp, 1), a->bins, a->binpk, NBINS, EMAX, th);

    tui_rect om = tui_rect_make(rt.x, 16, rt.w, 3);
    tui_box(s, om, "OUTPUT", th->border);
    if (a->muted)
        tui_put_str(s, om, om.x + 9, om.y, " [MUTED] ",
                    TUI_ATTR(TUI_BLACK, TUI_RED | TUI_BRIGHT));
    tui_rect omi = tui_rect_margin(om, 1);
    tui_put_str(s, omi, omi.x, omi.y, "Out", th->normal);
    tui_meter(s, tui_rect_make(omi.x + 4, omi.y, omi.w - 4, 1),
              a->out_level, a->out_peak, EMAX, th);

    tui_rect sc = tui_rect_make(rt.x, 19, rt.w, 12);
    tui_box(s, sc, "SCOPE", th->border);
    tui_rect sci = tui_rect_margin(sc, 1);
    if (sci.w > 0) {
        int n = sci.w < 96 ? sci.w : 96;
        int16_t raw[512];
        int levels[96];
        int span = n * 4 < 512 ? n * 4 : 512; /* ~9 ms window */
        bp_scope(raw, span);
        int wmax = 0;
        for (int i = 0; i < span; i++) {
            int v = raw[i] < 0 ? -raw[i] : raw[i];
            if (v > wmax) wmax = v;
        }
        for (int i = 0; i < n; i++) {
            /* rectified and normalized to the window peak, so quiet
             * (heavily filtered) signals still show their shape; a real
             * noise floor won't trigger it (silence gate at ~-30 dB) */
            int v = raw[i * span / n];
            if (v < 0) v = -v;
            levels[i] = wmax > 1000 ? v * 100 / wmax : 0;
        }
        tui_sparkline(s, sci, levels, n, 100, -1, th);
    }

    draw_hints(s, tui_rect_make(0, s->h - 1, s->w, 1), th);
}

/* One iteration of the app loop: top up audio, drain input, redraw every
 * `redraw_every`-th call. Returns false when the user quits. */
static bool tick(app *a, uint32_t *tick_no, int redraw_every)
{
    static int16_t chunk[CHUNK];

    /* audio first: top up the PCM ring */
    int space = snd_stream_space();
    while (space >= CHUNK) {
        bp_run(chunk, CHUNK);
        if (a->muted) memset(chunk, 0, sizeof chunk);
        snd_stream_write(chunk, CHUNK);
        space -= CHUNK;
    }

    /* input: drain everything pending */
    tui_key k;
    while ((k = tui_read_key(0)).kind != TUI_KEY_NONE)
        if (!handle(a, k)) return false;

    if (*tick_no % (uint32_t)redraw_every == 0) {
        animate(a);
        draw(tui_screen(), a);
        tui_present();
    }
    (*tick_no)++;
    return true;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* Browser build: no blocking loop allowed; the rAF callback (~60 fps) sets
 * the pace, so redraw every 2nd call for the same ~30 fps.
 * Quitting: 'q' just stops the loop and blanks the screen. */
static app g_app;
static uint32_t g_tick;

static void em_tick(void)
{
    if (!tick(&g_app, &g_tick, 2)) {
        emscripten_cancel_main_loop();
        tui_shutdown();
        snd_stream_close();
    }
}
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (snd_init() != 0) {
        printf("beeper: sound init failed\n");
        return 1;
    }
    if (snd_stream_open(BP_RATE, 1) != 0) {
        printf("beeper: stream busy or unsupported\n");
        return 1;
    }
    bp_init();
    if (tui_init() != 0) {
        snd_stream_close();
        printf("beeper: tui init failed (not a tty?)\n");
        return 1;
    }

#ifdef __EMSCRIPTEN__
    g_app.focus = C_CUTOFF;
    g_app.volume = 80;
    snd_set_volume(g_app.volume);
    emscripten_set_main_loop(em_tick, 0, 1);
#else
    app a = {0};
    a.focus = C_CUTOFF;    /* good first knob to try */
    a.volume = 80;
    snd_set_volume(a.volume);

    uint32_t tick_no = 0;

    /* ~30 fps: 10 ms device tick, redraw every 3rd */
    while (tick(&a, &tick_no, 3))
        vTaskDelay(1);

    tui_shutdown();
    snd_stream_close();
    printf("beeper: bye\n");
#endif
    return 0;
}
