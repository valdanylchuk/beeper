/* bp_patterns.c -- demo loop sequence data for the beeper synth.
 *
 * The event-list loops the demo sequencer (in bp_synth.c) plays.
 * Tracks are score strings (grammar in bp_patterns.h) expanded into bp_ev arrays
 * by bp_patterns_init(). */

#include <stdlib.h>

#include "bp_patterns.h"

typedef struct {
    int8_t transpose;  /* added to every written pitch on expansion */
    const char *s;
} bp_score;

/* ---------------------------------------------------------------- scores */

static const bp_score aha_lead = { -24, /* 24 notes */
    "f#4!68:12 24 f#4 d4 b3 48 b3 e4 | "
    "e4 e4 24 g#4 g#4 a4 b4 | "
    "a4 a4 a4 e4 48 d4 f#4 | "
    "f#4 f#4 24 e4 e4 f#4 e4 "
};

static const bp_score aha_bass = { -12, /* 15 notes */
    "b1!67:12 24 b1 b2 120 e2 | "
    "48 e3 72 e2 24 e2 | "
    "72 a1 24 a1 a2 a1 96 d2 | "
    "48 d3 72 c#2 24 c#2 "
};

static const bp_score aha_drums = { 0, /* 64 notes */
    "( K!50:6 12 h!18 K!50 h!18 S!50 h!18 K!50 h!18 )*8 "
};

/* The +2st up-and-back bends on the long notes are written as split
 * legato segments; the lead preset's glide fuses them into slides. */
static const bp_score till_lead = { -36, /* 38 notes */
    "48 a#5!110:9 24 a#5:24 c6:27 28 a#5:24 44 c#6:14 | "
    "48 c#6!72:12 c#6!39 | "
    "168 f#5!110:8 24 g#5:27 27 a5:8 7 a#5:16 16 g#5:22 46 a#5:14 | "
    "48 c#6 c#6!66:12 c#6!36 | "
    "120 a#5!110:8 24 a#5:28 28 c6:24 24 a#5 44 c#6:12 | "
    "48 c#6:14 c#6:23 24 d#6:12 12 f6:19 18 d#6:16 f#5:46 | "
    "96 f#5:10 24 g#5:30 30 a#5:24 24 g#5:19 42 a#5:14 | "
    "48 c#6 c#6:26 26 d6:8 8 d#6:16 15 c#6:23 23 g#5!100:45 "
};

static const bp_score till_bass = { -12, /* 52 notes: bars 1-4 twice */
    "( a#2!90:11 24 a#2:12 36 a#2:10 12 a#1:22 48 a#2:18 36 a#2:11 12 a#1:20 | "
    "48 a#2:22 36 a#2:10 12 a#1:20 48 a#2:23 36 a#2:10 12 a#1:18 | "
    "48 d#2:22 36 d#2:10 12 d#1:22 48 d#2:20 36 d#2:10 12 d#1:22 | "
    "48 f#2:20 36 f#2:10 12 f#1:19 24 f#2:14 f#1:18 g#2:13 g#1:18 )*2 "
};

static const bp_score till_drums = { 0, /* 80 notes */
    "( K!90:6 24 h!40 K!90 +S!60 h!40 )*16 "
};

/* Octave scoops and the chromatic descents are split legato segments;
 * they only read as bends on a preset with glide. */
static const bp_score gangnam_lead = { -12, /* 38 notes: bars 1-2 twice */
    "( b3!127:18 36 b3:6 6 b4 30 b4:16 36 g4:12 12 a4 a#4 b4:16 24 b4:10"
    " 12 c#5:12 | "
    "b3:18 36 b3:6 6 b4 30 b4:16 24 b4:10 a#4 a4 f#4:12 12 d4 )*2 "
};

static const bp_score gangnam_bass = { -12, /* 34 notes: bars 1-2 twice */
    "( b1!95:36 36 b2:12 b2:24 g2:12 12 a2 a#2 b2:24 24 b2:10"
    " 12 c#3:12 | "
    "b1:36 36 b2:12 b2:24 24 b2:10 a#2 a2 f#2:12 12 d2 )*2 "
};

static const bp_score gangnam_drums = { 0, /* 32 notes */
    "( K!103:6 24 h!73 S!103 h!73 )*8 "
};

static const bp_score dreams_lead = { -12, /* 32 notes */
    "( c2!96:16 24 c2 c3 c4 d#3 d#4 c3 c4 | "
    "g#2 g#2 g#3 c4 g2 g2 g3 c4 )*2 "
};

static const bp_score dreams_bass = { -12, /* 26 notes */
    "( c2!96:24 48 c3 24 c2 48 c3 24 c2 c3 | "
    "g#1 48 g#2 24 g#1 g1 g2 a#1 a#2 )*2 "
};

static const bp_score dreams_drums = { 0, /* 16 notes, kick every beat */
    "( K!127:6 48 K )*8 "
};

static const bp_score love_lead = { -12, /* 32 notes */
    "b4!127:8 24 a#4 b4 "
    "( g#4 b4 a#4 b4 )*2 "
    "f#4 b4 a#4 b4 f#4 | "
    "( a#4 g#4 a#4 f#4 )*4 "
};

static const bp_score love_bass = { -12, /* 20 notes */
    "g#1!97:24 48 g#1 36 g#1:16 g#1 48 f#1:12 | "
    "24 b1:24 48 b1 36 b1:16 c#2 48 d#2:12 | "
    "24 d#2:24 48 d#2 36 d#2:16 d#2 48 d#2:12 | "
    "24 f#2:24 48 f#2 36 f#2:16 d#2 48 f#2:12 "
};

static const bp_score love_drums = { 0, /* 92 notes */
    "( K!127:6 +h!57 12 h h h K!127 +S!113 +h!57 h h S!73 +h!57"
    " K!127 +h!57 h h h K!127 +S!113 +h!57 h h h )*4 "
};

/* Bars 3-4: the siren */
static const bp_score impala_lead = { -12, /* 28 notes */
    "d4!50:12 24 d4!36 d4 d4 e4!50 e4!36 e4 c4!50 | "
    "c4!36 c4 c4 c4 d4!50 d4!36 d4 d4 | "
    "f#4!50:56 56 a#4:8 8 d5 f#5:104 104 d5:8 8 a#4 f#4:56 | "
    "56 a#4:8 8 d5 f#5:80 80 c#5:8 8 g#4 e4:24 "
};

static const bp_score impala_bass = { -12, /* 25 notes */
    "d3!69:24 24 d3 d3 d3 c#3 c#3 c#3 c3 | "
    "c3 c3 c3 48 g2 24 g2 g2 a2 | "
    "f#2 72 f#2 24 f#2 72 f#2 | "
    "24 f#2 72 f#2 24 g#2 g#2 g#2 g#2 "
};

static const bp_score impala_drums = { 0, /* 62 notes */
    "( K!127:6 +h!64 24 h!100 K!90 +S!127 +h!64 K!127 +h!100 )*2 "
    "( K!127 +h!64 K!127 +h!100 K!90 +S!127 +h!64 h!100 )*2 "
    "( K!127 +h!64 h!100 K!90 +S!127 +h!64 K!127 +h!100 )*2 "
    "K!127 +h!64 h!100 K!90 +S!127 +h!64 h!100"
    " K!127 +h!64 S!127 +h!100 h!64 S!127 +h!100 "
};

/* Piano-style bass melody on the lead; the sub (bass track) plays it two
 * octaves down, root-only, locked to the kick. Meant for the 808 preset. */
static const bp_score humble_lead = { -12, /* 16 notes */
    "d#3!72:36 48 d#3:24 96 e3:36 | "
    "48 d#3 g#2:24 24 g#2 48 g#2 24 e3:36 | "
    "48 d#3 d#3:24 96 e3:36 | "
    "48 d#3 g#2:24 24 g#2 48 g#2 24 e3:36 "
};

static const bp_score humble_bass = { -12, /* 16 notes */
    "d#1!100:24 72 d#1 24 d#1 72 d#1 | "
    "24 d#1 72 g#0 24 g#0 36 e1 | "
    "60 d#1 72 d#1 24 d#1 72 d#1 | "
    "24 d#1 72 g#0 24 g#0 36 e1 "
};

static const bp_score humble_drums = { 0, /* 42 notes */
    "( K!110:6 24 h!68 48 K!110 +h!68 24 K!110 +S!100 h!68 h"
    " 12 h K!110 +h!68 | "
    "24 K!110 h!68 48 K!110 +h!68 24 K!110 +S!100 h!68 12 K!110"
    " 36 h!68 12 h )*2 "
};

/* Lead answers land syncopated on the and-of-4 of each bar. */
static const bp_score smalltown_lead = { -24, /* 20 notes */
    "24 c5!84:24 d#5!112:36 48 c5!84:24 24 d#5!112:24 48 d#5 | "
    "c5!84 24 d5!112:36 48 c5!84:24 24 d5!112:24 48 d5 | "
    "c5!84 24 f5!112:36 48 c5!84:24 24 f5!112:24 48 f5 | "
    "c5!84 24 d#5!112:36 48 c5!84:24 24 d#5!112:24 48 d#5 "
};

static const bp_score smalltown_bass = { -12, /* 32 notes */
    "( c2!92:23 24 c3 )*4 "
    "( a#1 a#2 )*4 "
    "( f2 f3 )*4 "
    "( d#2 d#3 )*4 "
};

static const bp_score smalltown_drums = { 0, /* 40 notes */
    "( K!108:6 24 h!68 K!108 +S!98 h!68 )*8 "
};

static const bp_score enjoy_lead = { -36, /* 16 notes */
    "48 d#5!96:24 24 c6!106:48 48 d#5!96:24 24 d6!106:96 | "
    "96 d#5!96:24 24 d#6!106:48 48 d#5!96:24 24 a#5!106:96 | "
    "96 d#5!96:24 24 a#5!106:48 48 d#5!96:24 24 g5!106:96 | "
    "96 d#5!96:24 24 a#5!106:48 48 d#5!96:24 24 g5!106:48 "
};

static const bp_score enjoy_bass = { -12, /* 4 notes, one per bar */
    "f3!103:192 | "
    "192 g#3 | "
    "c4 | "
    "a#3 "
};

static const bp_score enjoy_drums = { 0, /* 49 notes */
    "( K!108:6 24 h!68 K!108 +S!98 h!68 12 h K!108 h!68 h"
    " 24 K!108 +S!98 h!68 )*4 "
    "12 S!98 "
};

static const bp_score breathe_lead = { -12, /* 68 notes */
    "( d#3!114:24 24 d#3 d#3 d#3 f#3 d#3 f#3 f3 | "
    "d#3 f3 d3 d3 d3 d3 d3 d3:12 12 d3 )*4 "
};

static const bp_score breathe_bass = { -12, /* 108 notes */
    "( d2!55:8 24 d#2 d#2 d#2 )*3 "
    "d2 d#2 d2 d#2 | "
    "( d2 12 d#2 d#2 d#2 d#2 d#2 d#2 d#2 d2 d#2 d#2 d#2 d#2 d#2 d#2 d#2 | "
    "d2 d#2 d#2 d#2 d#2 d#2 d#2 d#2 d2 d#2 d#2 d#2 d2 d#2 d#2 d#2 )*2 "
    "( d2 d#2 d#2 d#2 d#2 d#2 d#2 d#2 )*3 "
    "d2 d#2 d#2 d#2 "
};

static const bp_score breathe_drums = { 0, /* 145 notes */
    "( K!100:6 +h 24 K +h S +h h 12 K +S h h K +h 24 S +h h 12 K )*3 "
    "K +h 24 K +h S +h h 12 K +S h h K +h"
    " 24 S!70 +h!100 12 S!82 S!94 +h!100 K +S!106 | "
    "( K!100 +h 24 K +h S +h h 12 K +S h h K +h 24 S +h h 12 K )*3 "
    "K +h 24 K +h S +h h K +S +h 12 K +S +h h S K +S +h K +S h K +S "
};

static const bp_score ifeel_lead = { -24, /* 10 notes */
    "12 a5!105:177 179 g5:127 | "
    "131 g5!85:31 36 g5:28 | "
    "30 f#5!105:186 | "
    "188 g#5!100:44 44 g#5:33 35 b5:56 75 g#5!85 19 a5!90:19 "
};

/* 16th-note ostinato with the delay pump written in as note pairs. */
static const bp_score ifeel_bass = { -12, /* 64 notes */
    "( a2!127:12 12 a2 a2 a2 e2 e2 g2 g2 )*2 "
    "( c3 c3 c3 c3 g2 g2 a2 a2 )*2 "
    "( d3 d3 d3 d3 a2 a2 c3 c3 )*2 "
    "( e3 e3 e3 e3 b2 b2 d3 d3 )*2 "
};

static const bp_score ifeel_drums = { 0, /* 88 notes */
    "( K!110:6 +h!100 12 h h h K!110 +S!100 +h h h h )*8 "
};

static const bp_score chameleon_lead = { -12, /* 15 notes */
    "a#4!112:9 12 a#4!92 36 g#4!112 12 g#4!92 36 a#4!112 12 a#4!92"
    " 36 c#5!108:48 | "
    "48 a#4:12 144 a#4 12 g#4 f4!50 g#4!108:36 | "
    "36 a#4:9 144 c#5:48 | "
    "48 d#5:9 "
};

static const bp_score chameleon_bass = { -12, /* 24 notes: bars 1-2 twice */
    "( a#1!100:9 36 g#2 a#2 48 c2:24 24 c#2 d2 | "
    "d#2:9 36 a#2 c#3 48 g1:24 24 g#1 a1 )*2 "
};

static const bp_score chameleon_drums = { 0, /* 53 notes */
    "K!104:6 +h!100 24 h!56 12 S!108 h!100 24 h!56 h!100 12 K 12 K +h!56"
    " 24 S!108 +h!100 h!56 | "
    "K!104 +h!100 h!56 12 S!108 h!100 24 h!56 h!100 12 K 12 K +h!56"
    " 24 S!108 +h!100 K!104 +h!56 | "
    "K!104 +h!100 h!56 12 S!108 h!100 24 h!56 h!100 K!104 +h!56"
    " S!108 +h!100 h!56 | "
    "K!104 +h!100 h!56 12 S!108 h!100 24 h!56 h!100 12 K 12 K +h!56"
    " 24 S!108 +h!100 K!104 +h!56 "
};

/* ------------------------------------------------------------- patterns */

typedef struct {
    const char *name;
    uint16_t bpm, ticks;
    uint8_t lead_preset, back_preset;
    const bp_score *sc[3];     /* lead, back, drums */
} score_pattern;

static const score_pattern k_score_patterns[BP_NUM_PATTERNS] = {
    { "Take On Me",    169, 768, P_SWLEAD, P_FUNK,
      { &aha_lead, &aha_bass, &aha_drums } },
    { "Till I Come",   142, 1536, P_GFUNK, P_PICKBASS,
      { &till_lead, &till_bass, &till_drums } },
    { "Gangnam Style", 132, 768, P_HOOVER, P_REESE,
      { &gangnam_lead, &gangnam_bass, &gangnam_drums } },
    { "Sweet Dreams",  126, 768, P_BRASS, P_FUNK,
      { &dreams_lead, &dreams_bass, &dreams_drums } },
    { "What Is Love",  126, 768, P_DBRASS, P_PICKBASS,
      { &love_lead, &love_bass, &love_drums } },
    { "It Might Be Time", 116, 768, P_HOOVER, P_REESE,
      { &impala_lead, &impala_bass, &impala_drums } },
    { "HUMBLE.", 150, 768, P_808, P_808,
      { &humble_lead, &humble_bass, &humble_drums } },
    { "Smalltown Boy", 136, 768, P_PLUCK, P_OCTBASS,
      { &smalltown_lead, &smalltown_bass, &smalltown_drums } },
    { "Enjoy the Silence", 113, 768, P_SWLEAD, P_OCTBASS,
      { &enjoy_lead, &enjoy_bass, &enjoy_drums } },
    { "Breathe", 130, 1536, P_ACID, P_OCTBASS,
      { &breathe_lead, &breathe_bass, &breathe_drums } },
    { "I Feel Love", 130, 768, P_SWLEAD, P_PICKBASS,
      { &ifeel_lead, &ifeel_bass, &ifeel_drums } },
    { "Chameleon", 103, 768, P_BRASS, P_FUNK,
      { &chameleon_lead, &chameleon_bass, &chameleon_drums } },
};

/* --------------------------------------------------------------- parser */

/* All tracks expand into one static arena (1577 events as of writing). */
#define BP_EV_ARENA 1664

static bp_ev s_arena[BP_EV_ARENA];
pattern_t k_patterns[BP_NUM_PATTERNS];

/* a b c d e f g -> pitch class */
static const int8_t k_pc[7] = { 9, 11, 0, 2, 4, 5, 7 };

/* Expand one score into out[]; returns the event count, or -1 on a
 * malformed score / overflow (leaving the track empty). */
static int score_parse(const bp_score *sc, bp_ev *out, int max)
{
    const char *s = sc->s;
    int d = 0, v = 0, g = 0, n = 0, plus = 0, sp = 0;
    struct { const char *body; int left; } stk[4];

    while (*s) {
        char c = *s;
        if (c == ' ' || c == '\t' || c == '\n' || c == '|') { s++; continue; }
        if (c == '+') { plus = 1; s++; continue; }
        if (c == '(') {
            if (sp == 4) return -1;
            stk[sp].body = ++s;
            stk[sp].left = -1;
            sp++;
            continue;
        }
        if (c == ')') {
            if (sp == 0 || s[1] != '*') return -1;
            int cnt = (int)strtol(s + 2, (char **)&s, 10);
            if (stk[sp - 1].left < 0) stk[sp - 1].left = cnt - 1;
            if (stk[sp - 1].left > 0) {
                stk[sp - 1].left--;
                s = stk[sp - 1].body;
            } else {
                sp--;
            }
            continue;
        }
        if (c >= '0' && c <= '9') { d = (int)strtol(s, (char **)&s, 10); continue; }
        if (c == '!') { v = (int)strtol(s + 1, (char **)&s, 10); continue; }
        if (c == ':') { g = (int)strtol(s + 1, (char **)&s, 10); continue; }

        /* note name: K/S/h drums, or [a-g][#]?<octave> + transpose */
        int note;
        if (c == 'K') { note = DR_KICK; s++; }
        else if (c == 'S') { note = DR_SNARE; s++; }
        else if (c == 'h') { note = DR_HAT; s++; }
        else if (c >= 'a' && c <= 'g') {
            note = k_pc[c - 'a'];
            s++;
            if (*s == '#') { note++; s++; }
            if (*s < '0' || *s > '9') return -1;
            note += (*s++ - '0' + 1) * 12 + sc->transpose;
        } else {
            return -1;
        }
        while (*s == '!' || *s == ':') {  /* attached !vel / :gate */
            if (*s == '!') v = (int)strtol(s + 1, (char **)&s, 10);
            else           g = (int)strtol(s + 1, (char **)&s, 10);
        }
        int dd = plus ? 0 : d;
        plus = 0;
        while (dd > 255) {  /* vel-0 spacers for long gaps */
            if (n == max) return -1;
            out[n].delta = 255; out[n].note = 0;
            out[n].vel = 0;     out[n].gate = 0;
            n++;
            dd -= 255;
        }
        if (n == max || note < 0 || note > 127) return -1;
        out[n].delta = (uint8_t)dd;
        out[n].note = (uint8_t)note;
        out[n].vel = (uint8_t)v;
        out[n].gate = (uint8_t)g;
        n++;
    }
    return sp == 0 ? n : -1;
}

void bp_patterns_init(void)
{
    int used = 0;
    for (int p = 0; p < BP_NUM_PATTERNS; p++) {
        const score_pattern *sc = &k_score_patterns[p];
        pattern_t *pt = &k_patterns[p];
        pt->name = sc->name;
        pt->bpm = sc->bpm;
        pt->ticks = sc->ticks;
        pt->lead_preset = sc->lead_preset;
        pt->back_preset = sc->back_preset;
        bp_track *tr[3] = { &pt->tr[0], &pt->tr[1], &pt->drums };
        for (int t = 0; t < 3; t++) {
            int n = score_parse(sc->sc[t], s_arena + used, BP_EV_ARENA - used);
            if (n < 0) n = 0;  /* malformed score: leave the track empty */
            tr[t]->ev = s_arena + used;
            tr[t]->n = (uint16_t)n;
            used += n;
        }
    }
}
