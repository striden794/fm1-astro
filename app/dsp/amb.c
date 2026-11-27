/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Tomorrow's Man (TIDE: the conductor, the governor, the Ground layer) */
/* The instrument (amb.h). One compilation unit, -O2.
 *
 * TIDE, Phase 3: AMB-1's control layer (the arc, the pad's scheduled breath, the 1 s hand-offs, the chord
 * drift timer, the pulse) is replaced by the conductor in DESIGN.md: walkers on four timescales, the pad as
 * a layer that enters, breathes on summed random walks, dissolves and rests, an energy governor that
 * listens to the mix and each layer's share, global rate limits, and Ground, a rare low drone in the
 * pulse's place. The sounds (voices, bells, bass, echo, plate) are AMB-1's.
 * Phase 4: the timbre landscape and its cursor. Phase 5: the harmonic field (a scale on a root that moves over
 * hours, one pitch class at a time), the bells sparse and soft, the governor one-sided. Phase 5b: the bells in the
 * scale at every register and in passages that let up, the plate's modulation wandering (ARCHITECTURE.md 11-14).
 * Phase 5c: a pad voice's crest has a ceiling, its high notes are softer and rounder, no two voices sing one note;
 * the ebb, the whole garden sinking for a few minutes and coming back (ARCHITECTURE.md 15). */
#include "amb.h"
#include "fastmath.h"
#ifdef EN_HOST
#include <string.h>
#endif

#ifndef EN_PULSE
#define EN_PULSE 0                              /* AMB-1's Euclidean pulse: kept, not compiled (TESTING #9) */
#endif

#if defined EN_HOST || defined PLAT_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const EN_MODE_NAME[EN_NMODES] = {"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Aeolian",
                                             "Pent maj", "Pent min", "Whole tn", "Lyd dom"};
#define B(a, b, c, d, e, f, g) (1u << (a) | 1u << (b) | 1u << (c) | 1u << (d) | 1u << (e) | 1u << (f) | 1u << (g))
const uint16_t EN_MODE_POOL[EN_NMODES] = {
    B(0, 2, 4, 5, 7, 9, 11), B(0, 2, 3, 5, 7, 9, 10), B(0, 1, 3, 5, 7, 8, 10), B(0, 2, 4, 6, 7, 9, 11),
    B(0, 2, 4, 5, 7, 9, 10), B(0, 2, 3, 5, 7, 8, 10), B(0, 2, 4, 7, 9, 9, 9),  B(0, 3, 5, 7, 10, 10, 10),
    B(0, 2, 4, 6, 8, 10, 10), B(0, 2, 4, 6, 7, 9, 10)};   /* Whole tone, Lydian dominant: the space scales */
#undef B
const uint8_t EN_LOOP_LEN[EN_NLOOP] = {13, 17, 23, 29};
const uint8_t EN_DRUM_N[EN_NDRUM] = {16, 12, 7};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Pad", 0, 100, 60},      {"Bells", 0, 100, 65},    {"Bass", 0, 100, 45},     {"Pulse", 0, 100, 40},
    {"Density", 0, 100, 40},  {"Evolve", 0, 100, 35},   {"Drift", 0, 100, 25},    {"Hits", 0, 100, 35},
    {"Echo", 0, 100, 45},     {"Echo time", 0, 5, 3},   {"Reverb", 0, 100, 65},   {"Size", 0, 100, 80},
    {"Bright", 0, 100, 40},   {"Swell", 0, 100, 60},    {"Bell tone", 0, 100, 50}, {"Motion", 0, 100, 50},
    {"Tune", -50, 50, 0},     {"MIDI out", 0, 1, 1},    {"Tempo", 30, 120, 72},   {"Root", 0, 11, 2},
    {"Mode", 0, EN_NMODES - 1, 1}, {"Bell oct", -2, 2, 0}, {"Pad tone", 0, 5, 0},
    {"Bass oct", -1, 1, 0},   {"Glide", 0, 100, 20},    {"Fifth", 0, 100, 35},
    {"Swing", 0, 100, 0},     {"Hats", 0, 100, 50},
    {"Mood", -50, 50, 0},     {"Space", -50, 50, 0},
    {"Bell type", 0, 5, 0},   {"Bass type", 0, 5, 0},
};
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* the pieces: four recipes for the whole garden (AMB-1 has eight) (the FM-1's PRESETS, the web app's cards). Tune and
 * MIDI out are taken from here by neither: they belong to the player, not the piece.
 * TIDE reads them as: Pulse = the Ground layer's level; Drift = the weather's speed (0 holds, 50 = 1x);
 * Swell = how far the pad voices breathe; Hits, Swing, Hats: unused (the pulse is not played). Mood and Space
 * (Phase 6) are the player's offsets around a recipe and are 0 in every one (the initialisers end before them). */
/*                     Pad Bel Bas Pul  Den Evo Dri Hit  Ech ET Rev Siz  Bri Swe Ton Mot  Tun MI Tem Rt Md Oc  PT  BO Gli Fif  Swi Hat
 * (PT: the pad's colour, 0 Rhodes, 1 Voices, 2 Choir; BO: the bass's octave) */
static const struct {
    const char *name;
    int8_t v[P_NPARAMS];
} PIECES[EN_NPIECES] = {
    /* the four sci-fi pieces that replaced SAMSARA's own four (Samsara, Maranasati, Anatta, Nirvana), not yet heard: a first
     * guess from what the parameters mean. Cryosleep: A Aeolian, very slow, no Ground, a Glass pad, Crystal bells, a Void
     * bass, a huge reverb. Derelict: Eb Phrygian, a dark Hollow pad, Gong bells, a Reactor bass, a faint pulse of the ship's
     * own. Hyperlane: G Mixolydian, quick, an Aurora pad, Glass bells, a Pulse bass and the Ground driving. Terraform: C
     * major pentatonic, warm and growing, a Voices pad, Wood bells, a Triangle bass. */
    {"Cryosleep",    {65, 45, 35,  0, 20, 20, 15,  0, 55, 4, 85, 95, 30, 90, 70, 30, 0, 1, 44, 9, 5, 1, 4, 0, 40, 20, 0, 0, 0, 0, 5, 4}},
    {"Derelict",     {60, 50, 55, 15, 25, 30, 30, 20, 50, 4, 80, 90, 20, 80, 35, 45, 0, 1, 52, 3, 2, -1, 5, 0, 30, 45, 0, 30, 0, 0, 4, 5}},
    {"Hyperlane",    {55, 70, 50, 45, 60, 55, 45, 50, 55, 2, 60, 70, 65, 40, 70, 70, 0, 1, 96, 7, 4, 1, 3, 0, 15, 30, 0, 60, 0, 0, 1, 3}},
    {"Terraform",    {60, 60, 45, 30, 45, 40, 35, 35, 45, 3, 65, 75, 50, 70, 45, 55, 0, 1, 68, 0, 6, 0, 1, 0, 25, 40, 10, 50, 0, 0, 3, 1}},
    /* the space pieces (not yet heard: a first guess from what the parameters mean). Perihelion: D Lydian, Choir pad,
     * bright bells, a long plate. Aphelion: Bb whole tone, sparse and cold, a Rhodes pad, high bells. Horizon: F Aeolian,
     * Voices pad, a heavy Ground, dark and far. Pulsar: E Lydian dominant, quicker, more bells, Rhodes pad. */
    {"Perihelion",   {55, 65, 40, 40, 40, 45, 20, 0, 55, 4, 75, 92, 55, 70, 65, 70, 0, 1, 60, 2, 3, 0, 3, 0, 30, 30, 0, 50, 0, 0, 0, 0}},
    {"Aphelion",     {45, 55, 35, 45, 25, 50, 15, 0, 65, 5, 80, 95, 40, 80, 80, 60, 0, 1, 50, 10, 8, 0, 4, 0, 40, 20, 0, 50, 0, 0, 1, 0}},
    {"Horizon",      {50, 40, 50, 60, 25, 30, 10, 0, 70, 4, 85, 95, 25, 85, 40, 45, 0, 1, 56, 5, 5, 0, 5, 0, 45, 35, 0, 50, 0, 0, 0, 2}},
    {"Pulsar",       {40, 70, 45, 30, 55, 70, 30, 0, 50, 2, 65, 80, 65, 55, 70, 85, 0, 1, 84, 4, 9, 0, 0, 0, 20, 40, 0, 50, 0, 0, 3, 3}},
};

const char *en_piece_name(int i) { return PIECES[(unsigned)i < EN_NPIECES ? i : 0].name; }
void en_piece(int i, int16_t *par, uint16_t *pool)
{
    int p;
    i = (unsigned)i < EN_NPIECES ? i : 0;
    for (p = 0; p < P_NPARAMS; p++)
        par[p] = PIECES[i].v[p];
    *pool = EN_MODE_POOL[PIECES[i].v[P_MODE]];
}

static const char *const ETIME_NAME[6] = {"1/4.", "1/2", "1/2.", "1 bar", "2 bars", "4 bars"};
static const float ETIME_BEATS[6] = {1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f};

static char *itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

/* TIDE: Drift is the weather's speed: 0 holds everything, 50 is 1x, 25 half, 100 four times */
static float drift_speed(int v) { return v <= 0 ? 0.0f : fm_exp2f((float)(v - 50) * 0.04f); }

void en_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    const char *s = 0;
    if (i == P_ETIME)
        s = ETIME_NAME[(unsigned)v < 6u ? v : 0];
    else if (i == P_MIDI)
        s = ONOFF[v ? 1 : 0];
    else if (i == P_PADTONE)
    {
        static const char *const PT[6] = {"Rhodes", "Voices", "Choir", "Aurora", "Glass", "Hollow"};
        s = PT[(unsigned)v < 6u ? v : 0];
    } else if (i == P_BELLTYPE) {
        static const char *const BT[6] = {"Chime", "Glass", "Plink", "Wood", "Gong", "Crystal"};
        s = BT[(unsigned)v < 6u ? v : 0];
    } else if (i == P_BASSTYPE) {
        static const char *const BS[6] = {"Sub", "Triangle", "Drone", "Pulse", "Void", "Reactor"};
        s = BS[(unsigned)v < 6u ? v : 0];
    }
    else if (i == P_MODE)
        s = EN_MODE_NAME[(unsigned)v < EN_NMODES ? v : 0];
    else if (i == P_ROOT)
        s = EN_NOTE_NAME[(unsigned)v < 12u ? v : 0];
    else if (i == P_DRIFT && v <= 0)
        s = "Hold";
    if (s) {
        int k = 0;
        while (s[k] && k < 9) {
            b[k] = s[k];
            k++;
        }
        b[k] = 0;
        return;
    }
    if (i == P_DRIFT) {                          /* the speed, as "0.50x" */
        int h = (int)(drift_speed(v) * 100.0f + 0.5f);
        b = itoa_s(h / 100, b);
        *b++ = '.';
        *b++ = (char)('0' + h % 100 / 10);
        *b++ = (char)('0' + h % 10);
        *b++ = 'x';
        *b = 0;
        return;
    }
    if (i == P_SWING) {                          /* as a swing percentage: 50% (straight) .. 75% */
        b = itoa_s(50 + v / 4, b);
        *b++ = '%';
        *b = 0;
        return;
    }
    if ((i == P_TUNE || i == P_BOCT || i == P_BASSOCT || i == P_MOOD || i == P_SPACE) && v > 0)
        *b++ = '+';
    itoa_s(v, b);
}

/* ------------------------------------------------------- the pure parts --- */
uint32_t en_euclid(int k, int n, int rot)
{
    /* Bjorklund's algorithm (as Toussaint's "The Euclidean Algorithm Generates Traditional Musical
     * Rhythms"): k groups "x" and n - k groups ".", the remainders folded onto the heads until at most
     * one remains. Groups are bit strings, LSB first: (bits, length). */
    uint32_t gb[32], m = 0;
    uint8_t gl[32];
    int ng, heads, tails, i, pos = 0;
    if (n <= 0 || n > 32 || k <= 0)
        return 0;
    if (k > n)
        k = n;
    for (i = 0; i < n; i++) {
        gb[i] = i < k ? 1u : 0u;
        gl[i] = 1;
    }
    ng = n;
    heads = k;
    tails = n - k;
    while (tails > 1 && heads > 0) {
        int pairs = heads < tails ? heads : tails;
        for (i = 0; i < pairs; i++) {           /* a tail onto each head */
            gb[i] |= gb[ng - pairs + i] << gl[i];
            gl[i] = (uint8_t)(gl[i] + gl[ng - pairs + i]);
        }
        if (heads > tails) {                    /* the unpaired heads become the new tails */
            tails = heads - pairs;
            heads = pairs;
        } else {
            tails -= pairs;
            heads = pairs;
        }
        ng = heads + tails;
    }
    for (i = 0; i < ng; i++) {
        uint32_t j;
        for (j = 0; j < gl[i]; j++)
            if (gb[i] >> j & 1u)
                m |= 1u << (((pos + (int)j + rot) % n + n) % n);
        pos += gl[i];
    }
    return m;
}

int en_scale(uint32_t pool, int *steps)
{
    int i, m = 0;
    pool |= 1u;
    for (i = 0; i < 12; i++)
        if (pool >> i & 1u)
            steps[m++] = i;
    return m;
}

int en_chord_tones(uint32_t pool, int root, int deg, int *pcs)
{
    static const uint8_t OFF[7] = {0, 2, 4, 6, 1, 3, 5};   /* stacked thirds, then what's left */
    int st[12], m = en_scale(pool, st), want, n = 0, i, j;
    want = m >= 6 ? 4 : 3;
    deg = ((deg % m) + m) % m;
    for (i = 0; i < 7 && n < want; i++) {
        int pc = (root + st[(deg + OFF[i]) % m]) % 12, dup = 0;
        for (j = 0; j < n; j++)
            dup |= pcs[j] == pc;
        if (!dup)
            pcs[n++] = pc;
    }
    return n;
}

static int nearest(int from, int pc, int lo, int hi)    /* the note of class pc nearest from, in lo..hi */
{
    int n = from + ((pc - from) % 12 + 12) % 12;          /* at or above */
    if (n - from > 6)
        n -= 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}

void en_voice_lead(const int *from, const int *pcs, int nt, int *to, int lo, int hi)
{
    static const uint8_t PERM[24][4] = {
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};
    int t[4], p, v, best = 1 << 30;
    for (v = 0; v < 4; v++)
        t[v] = pcs[v < nt ? v : 0];                        /* three tones: the root doubled */
    for (p = 0; p < 24; p++) {
        int c = 0, n[4], a, b;
        for (v = 0; v < 4; v++) {
            n[v] = nearest(from[v], t[PERM[p][v]], lo, hi);
            c += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
        }
        for (a = 0; a < 4; a++)                            /* no two voices on one note */
            for (b = a + 1; b < 4; b++)
                if (n[a] == n[b])
                    c += 40;
        if (c < best) {
            best = c;
            for (v = 0; v < 4; v++)
                to[v] = n[v];
        }
    }
}

const char *en_chord_name(const int *pcs, int n, char *buf)
{
    uint32_t iv = 0;
    int i, k = 0, third, fifth;
    const char *q = "", *r = EN_NOTE_NAME[pcs[0] % 12];
    for (i = 1; i < n; i++)
        iv |= 1u << ((pcs[i] - pcs[0] + 12) % 12);
    third = iv >> 4 & 1u ? 4 : iv >> 3 & 1u ? 3 : 0;
    fifth = iv >> 7 & 1u ? 7 : iv >> 6 & 1u ? 6 : iv >> 8 & 1u ? 8 : 0;
    if (third == 4) {
        q = fifth == 8 ? "aug" : iv >> 11 & 1u ? "M7" : iv >> 10 & 1u ? "7" : iv >> 9 & 1u ? "6"
            : iv >> 2 & 1u ? "add9" : "";
    } else if (third == 3) {
        if (fifth == 6)
            q = iv >> 10 & 1u ? "m7b5" : iv >> 9 & 1u ? "dim7" : "dim";
        else
            q = iv >> 10 & 1u ? "m7" : iv >> 11 & 1u ? "mM7" : iv >> 9 & 1u ? "m6" : iv >> 2 & 1u ? "madd9" : "m";
    } else {
        q = iv >> 5 & 1u ? (iv >> 10 & 1u ? "7sus4" : "sus4") : iv >> 2 & 1u ? "sus2" : "5";
    }
    while (*r)
        buf[k++] = *r++;
    while (*q)
        buf[k++] = *q++;
    buf[k] = 0;
    return buf;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_POOL, C_PLAY, C_NEXT, C_FREEZE, C_SEED, C_PANIC, C_CLOCK, C_REGROW };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_pool(uint32_t pool) { post(C_POOL, 0, (int)(pool & 0xFFFu)); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_next(void) { post(C_NEXT, 0, 0); }
void en_freeze(int on) { post(C_FREEZE, on, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_regrow(void) { post(C_REGROW, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_frozen, en_ext;
volatile uint8_t en_chord[EN_NPAD], en_chord_root, en_chord_n;
volatile float en_drift, en_pad_level, en_bass_level, en_ground_level;
volatile uint8_t en_loop_pos[EN_NLOOP];
volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];
volatile float en_loop_glow[EN_NLOOP];
volatile uint8_t en_drum_pos[EN_NDRUM];
volatile uint32_t en_drum_pat[EN_NDRUM];
volatile float en_drum_glow[EN_NDRUM];
volatile uint32_t en_bars;
volatile float en_arc;
volatile uint8_t en_pulse_rest;
volatile uint8_t en_pad_note[EN_NPAD];
volatile float en_pad_amp[EN_NPAD];
volatile float en_cond[EN_NCOND];
const char *const EN_COND_NAME[EN_NCOND] = {
    "bright", "density", "register", "space", "chord", "pres_pad", "pres_bells", "pres_bass", "pres_ground",
    "s_speed", "s_density", "s_bright", "speed", "level20", "level60", "target", "trim",
    "share_pad", "share_bells", "share_bass", "share_ground", "gain_pad", "gain_bells", "gain_bass", "gain_ground",
    "pad_target", "pad_voices", "ground", "opening", "bells", "bass", "ff", "cursor_x", "cursor_y", "anchor",
    "s_mode", "s_root", "field_shift", "field_move", "bell_act", "alone", "field_pcs", "ebb", "pad_dip",
    "pad_expo", "pad_hold"};

static int16_t par[P_NPARAMS];
static uint32_t pool = 0xAADu;                  /* Ionian until told. TIDE Phase 5: the field's scale, relative to the field's root */
static uint32_t home_pool = 0xAADu;             /* the player's scale, relative to the player's Root: where the field starts and returns */
static int froot;                               /* the field's root (a pitch class); the player's Root is home */
volatile uint8_t en_field_root;
volatile uint16_t en_field_pool;
static int nsteps, steps[12];                   /* the scale */
static int deg;                                 /* the chord's scale degree */
static int chord_pcs[4], chord_nt;
static float tunefac = 1.0f;
static uint32_t rng = 0x9E3779B9u;

static uint32_t rnd(void)                       /* Marsaglia's xorshift (Xorshift RNGs, 2003): shifts 13, 17, 5 */
{
    uint32_t x = rng;
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    rng = x ^ (x << 5);
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static float gauss(void)                        /* about N(0, 1): four uniforms summed (Irwin-Hall), scaled */
{
    return (frand() + frand() + frand() + frand() - 2.0f) * 1.7320508f;
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        en_midi_out(st, d1, d2);
}

static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
/* The Choir (2026-11-14, after v1; Tomorrow's Man, TESTING #54: "a slight buzz that is reminiscent of Brian Eno's classic pieces,
 * such as I Dormienti"; #57: the pad in Samsara and Nirvana). A third colour of the pad beside the Rhodes and the
 * Voices: every voice reads a wavetable of three single cycles (choir.h: harmonics 1 to 32 of three sung vowels,
 * each harmonic a sine in phase, so a cross-fade never cancels one), the place between them moving on two slow rates
 * (periods 11-35 s) and a little with the crest. The cycles are built here at start, CHOIR_LEN samples each (read
 * with linear interpolation: 72 dB or more under the cycle). With it: the pad an octave down (pad_window: TESTING #52),
 * and a tape's wow of 1 cent (CHOIR_WOW of the life's 5: at 5 the held note walks across the plate's resonances and
 * the pad's level moves 1.3 dB/s against the swell limit's 1.0; at 1 cent 0.74; he hears no difference, TESTING #57).
 * By count the voice reads two cycles a sample where the FM voice reads the sine table three times. */
#include "choir.h"
#define CHOIR_LEN 1024
#define CHOIR_WOW 0.2f
static float choir_t[CHOIR_NCYC][CHOIR_LEN + 1];  /* in .bss, not the pool: the pool was 444 bytes short on the device */
static uint8_t choir_on;                        /* the pad's colour is the Choir (P_PADTONE 2) */
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

/* ------------------------------------------------- slow random motion --- */
/* AMB-1's tape wobbles (Motion): pitch, vowel, pan, the echo heads. Small, fast and zero-mean; kept. */
#define NMOD 9                                  /* 0..5 the pads', 6..8 the echo's */
#define NMOD_PAD 6
static struct {
    float v, tgt, rate;
    uint32_t left;                              /* samples until a new target */
} mod[NMOD];

static void mods_step(uint32_t n)
{
    int i;
    for (i = 0; i < NMOD; i++) {
        if (mod[i].left <= n) {
            /* TIDE: the pad's wobbles (pitch, vowel, pan) four times slower than AMB-1's 7..40 s: a held
             * note's pitch drifting across the plate's ripples was a swell by itself */
            float secs = i < NMOD_PAD ? 28.0f + 132.0f * frand() : 7.0f + 33.0f * frand();
            mod[i].tgt = frand() * 2.0f - 1.0f;
            mod[i].left = (uint32_t)(secs * EN_SR);
            mod[i].rate = (float)n / (secs * EN_SR * 0.5f);
        } else {
            mod[i].left -= n;
        }
        mod[i].v += (mod[i].tgt - mod[i].v) * fm_minf(mod[i].rate, 1.0f);
    }
}
static float motion;                            /* 0..1: Motion */

/* ------------------------------------------------------ TIDE: walkers --- */
/* Every evolving value is a walker (DESIGN section 2): an Ornstein-Uhlenbeck process that wanders with a
 * standard deviation sd around its center and is drawn back on a timescale tau (seconds at Drift 1x),
 * clamped to lo..hi. The conductor steps them every CTRL_N samples; speed multiplies time (0 holds). */
#define CTRL_N 2048u                            /* 46 ms: the conductor's step */
#define CTRL_DT ((float)CTRL_N / EN_SR)
typedef struct {
    float x, center, tau, sd, lo, hi;
} walker_t;

static void walker_set(walker_t *w, float center, float tau, float sd, float lo, float hi)
{
    w->center = center;
    w->tau = tau;
    w->sd = sd;
    w->lo = lo;
    w->hi = hi;
    w->x = fm_clampf(center + 0.5f * sd * gauss(), lo, hi);   /* start near its center; it strays from there */
}

static void walk(walker_t *w, float speed)
{
    float a;
    if (speed <= 0.0f)
        return;
    a = fm_minf(CTRL_DT * speed / w->tau, 1.0f);
    w->x += (w->center - w->x) * a + w->sd * fm_sqrtf(2.0f * a) * gauss();
    w->x = fm_clampf(w->x, w->lo, w->hi);
}

/* a rate-limited move in dB (the global rate limits, DESIGN section 6) */
static float slew(float x, float tgt, float up, float down, float dt)
{
    float d = tgt - x, m = (d > 0.0f ? up : down) * dt;
    if (d > m)
        d = m;
    else if (d < -m)
        d = -m;
    return x + d;
}

/* ---------------------------------------------------------------- pad --- */
/* Four voices, as the sung tape loops of Music for Airports' "2/1", but as a layer, not a bed: the
 * conductor asks for 0..3 of them. A voice enters at the swell limit on its note of the chord, breathes
 * on three summed walkers at unrelated rates (no two crests alike), stays a minute or four, then
 * dissolves more slowly than it came and rests. A chord change never bends a sounding note: a voice off
 * the new chord dissolves and comes back on a new one. The timbre: two saws a few cents apart through two
 * formant band-passes (Voices), or an FM Rhodes, as AMB-1. */
enum { PV_OFF, PV_ON, PV_OUT };
typedef struct {
    float ph[2], inc[2];
    float note, target;                         /* MIDI: the note sung, the next one's */
    float amp;                                  /* the amplitude, smoothed per sample */
    float lin;                                  /* the conductor's amplitude for it (0..1), per step */
    float ldb, base_db;                         /* its level in dB (slewed) and this stay's resting level */
    float rise, fall;                           /* dB/s: in and while singing, dissolving */
    float stay, rest;                           /* seconds left singing; seconds before it may enter again */
    float crest;                                /* 0..1: how far above its base it is (brightness follows) */
    float tilt, hk;                             /* Phase 5c: its note's height: dB off its level, and its overtones' share (pad_height) */
    float vowel, idx;                           /* the vowel's place (0..1) and the Rhodes index, rate-limited */
    walker_t lw[3], bw;                         /* level walkers; a brightness offset */
    float ic[4];                                /* the two filters' states */
    float c1[4], c2[4];                         /* their coefficients: a1, a2, a3, k */
    float vib, pan;
    float ratio;                                /* FM: the modulator's ratio to the carrier, the nearest anchor's as the note started */
    float hf;                                   /* a handoff's fade: 1 normal */
    uint8_t midi_note, hand;                    /* hand: 1 fading out of the old key, 2 into the new */
    uint8_t voices;                             /* this stay's colour: 0 Rhodes, 1 Voices (the nearest anchor's), 2 Choir */
    uint8_t land;                               /* the landscape the note was taken from (a change hands the voices over) */
    uint8_t st;                                 /* PV_* */
    float ph2, inc2;                            /* the life (below): the Rhodes's octave partial on its own phase */
    float ph3, inc3;                            /* the chorus's second oscillator */
    float lfo[4];                               /* and four slow phases: the wow's two, the colour's two */
} pad_t;
static pad_t pad[EN_NPAD];
/* The pad's life (2026-11-13, after v1; Tomorrow's Man, TESTING #52: an octave down "the tone is still 'alarming' when it
 * lingers", at 0:28:20 of that take; and of the other, "overwhelmed by the lingering 440Hz tone"). Each of his moments
 * (those two, #51's and #25's) is one voice alone and up, the loudest thing the ear has: the bass is 100 Hz and
 * under. And a voice at its crest does not move at all: one oscillator at a fixed pitch (the drift was taken out in
 * Phase 3: it walked a held note across the plate's resonances), its level cut flat by PAD_TOP, its colour slewed
 * over minutes. Four ways of giving a held voice a life of its own, each 0 (off, the engine as it is) to 1 and over,
 * set by the host for an A/B:
 * breath:  the ceiling cuts the voice's resting level, not its walkers' sum: it touches PAD_TOP at a crest and is
 *          under it otherwise, where it used to lie on it for a minute.
 * shimmer: the colour moves inside the note, as a wavetable's position would. Rhodes: the index opens to 1.8 times
 *          and closes again on two slow rates (periods 11-35 s, never the same twice running); the modulator and the
 *          octave partial a fraction of a hertz off the carrier's multiples, so the overtones turn against the
 *          fundamental. Voices: the vowel wanders a quarter of its range on the same rates.
 * wow:     the pitch on a tape's slow wow, +-5 cents, two rates a voice (0.3-0.9 Hz), no two voices alike.
 * vib:     a singer's vibrato, +-6 cents at 4.6-5.8 Hz, deeper as the voice crests.
 * chorus:  a second oscillator 1.5-3.5 cents off the first, at 0.6 of it: the tone beats against itself, the
 *          overtones faster than the fundamental. The one way of the five that costs the FM-1 time every sample. */
static float pl_breath, pl_shimmer, pl_wow, pl_vib, pl_chorus;
static const float LIFE_HZ[4][EN_NPAD] = {{0.31f, 0.43f, 0.37f, 0.47f}, {0.83f, 0.67f, 0.71f, 0.79f},
                                          {0.071f, 0.053f, 0.089f, 0.061f}, {0.037f, 0.029f, 0.043f, 0.033f}};
static const float LIFE_OFF[EN_NPAD] = {0.07f, -0.05f, 0.04f, -0.08f};   /* Hz: the octave partial off the octave; the modulator half of it
                                                                          * (at 2.5 times the index and the whole of it, a Glass voice, ratio 2,
                                                                          * cancelled its own fundamental every 12 s) */
#ifdef EN_HOST
void en_host_pad_life(float breath, float shimmer, float wow, float vib, float chorus)
{
    pl_breath = breath;
    pl_shimmer = shimmer;
    pl_wow = wow;
    pl_vib = vib;
    pl_chorus = chorus;
}
/* The pad's voice itself, for an A/B (2026-11-13; Tomorrow's Man, TESTING #53: with shimmer and wow the sine-like voice at 0:14
 * is well, the Glass voice at 0:28 "just as 'alarming' as before", 8 dB softer too). Host only, 0 by default:
 * 1: a Glass voice (the modulator an octave up: odd harmonics only, a square's hollow) plays as a Rhodes (ratio 1).
 * 2: every voice is a wavetable: two to four single cycles (band-limited and levelled by the host, WT_LEN samples
 *    each), the place between them moved by the shimmer's two slow rates and a little by the crest. */
#define WT_LEN 2048
#define WT_MAX 4
static int pv_mode, wt_n;
static float wt[WT_MAX][WT_LEN + 1];
void en_host_pad_voice(int mode) { pv_mode = mode; }
void en_host_pad_table(int k, const float *t)
{
    int j;
    if (k < 0 || k >= WT_MAX)
        return;
    for (j = 0; j < WT_LEN; j++)
        wt[k][j] = t[j];
    wt[k][WT_LEN] = t[0];
    if (k + 1 > wt_n)
        wt_n = k + 1;
}
#endif
static float pad_gate, pad_gain, pad_vol, bright, swell;
static float mode_tilt;                        /* Phase 6: how far Bright and Mood lean the mode walker, in mode steps */
static float mood_br;                          /* and where Mood puts the center of the weather's brightness (cd.br) */
static const float PAD_PAN[EN_NPAD] = {-0.55f, -0.18f, 0.18f, 0.55f};
static const float VOICE_K[EN_NPAD] = {1.0f, 1.13f, 0.89f, 1.21f};   /* each voice's walkers a little apart */
/* Phase 5c (Tomorrow's Man, TESTING #25: tones at ~530 and ~1060 Hz that "overwhelm when they become static", "almost a standing
 * wave" at 0:48:30, "the very specific tone that can sound like feedback" at 3:39). Each was one pad voice: C5 at
 * 0:48 with its octave louder than itself, C#5 at 3:39 sung by two voices at once, the voice 5-9 dB over its
 * resting level on three walkers that happened to crest together, the rest of the garden nearly out and the plate
 * ringing on the held note. Four numbers:
 * PAD_CREST: a voice's walkers lift it this far over its resting level and no further (the knee is at half of it;
 *   their sum reached +8.6 dB at 0:48). Downward they are as deep as before: quiet is the music.
 * PAD_TILT: dB a semitone a voice is softer above PAD_TILT_FROM (G4): about the ear's own curve between 400 Hz and
 *   1 kHz, which neither the governor's meter nor a loudness meter knows of when the rest is a bass at 73 Hz.
 * PAD_ROUND: above E4 a voice's overtones (the FM index, its bloom at a crest, the second harmonic) halve an octave,
 *   to this at the least; at a crest the Rhodes's octave was louder than its fundamental (C6, 1045 Hz, at 0:48:40).
 * And no unison: a voice does not enter on a note another is singing (pad_step). */
#define PAD_CREST 3.0f
#define PAD_TOP (-3.0f)                         /* and no voice sings over this, dB, whatever its resting level: a voice alone
                                                 * at 0 dB weighs 4-5 dB more than the whole garden is meant to (a take with
                                                 * PAD_CREST alone: one voice at +0.7 dB in a garden lifted 6 dB for its thinness,
                                                 * the 60 s loudness 4.4 dB over at 6:13); at -3 it is about the target by itself */
#define PAD_TILT 0.35f
#define PAD_TILT_FROM 67.0f
#define PAD_ROUND 0.35f
static void pad_height(pad_t *v)                /* a voice's note is set: what its height takes off it */
{
    v->tilt = -PAD_TILT * fm_maxf(v->note - PAD_TILT_FROM, 0.0f);
    v->hk = fm_clampf(fm_exp2f((64.0f - v->note) * (1.0f / 12.0f)), PAD_ROUND, 1.0f);
}
static const float VOWEL[4][2] = {{320.0f, 800.0f}, {480.0f, 900.0f}, {730.0f, 1090.0f}, {530.0f, 1840.0f}};

static void svf_coefs(float *c, float f, float k)
{
    float g = fm_tanf(FM_PI * fm_clampf(f, 40.0f, 16000.0f) * (1.0f / EN_SR));
    c[0] = 1.0f / (1.0f + g * (g + k));
    c[1] = g * c[0];
    c[2] = g * c[1];
    c[3] = k;
}

/* ------------------------------------------------ TIDE: the timbre landscape (DESIGN section 4) --- */
/* A landscape holds six anchor patches on a unit square, AMB-1's own colour at its center; the timbre
 * cursor wanders over it on two walkers (a season's region, the weather's drift about it) and never
 * parks. The continuous parameters are the inverse-distance blend of the anchors (each colour's own,
 * blended among its own anchors); the discrete ones (the colour, the FM ratio) are the nearest anchor's,
 * and a voice takes them as its note starts, never mid-note: the voices already sounding keep theirs, so
 * a change is heard as the voices cross, not as a jump. The brightness walkers (cd.br, each voice's bw)
 * still move the vowel and the index on top of the cursor. */
typedef struct {
    const char *name;
    uint8_t colour;                             /* 0 Rhodes (FM), 1 Voices (formants) */
    float ratio;                                /* FM: the modulator's frequency as a ratio of the carrier's */
    float x, y;                                 /* its place in the landscape */
    float idx, bloom, h2;                       /* FM: the index (1 = AMB-1's), the crest's bloom (1 = AMB-1's), the second harmonic */
    float vowel, q, saw;                        /* Voices: the vowel's offset, the formants' width (1 = AMB-1's), the saw's amount */
    float fc;                                   /* the pad bus's low-pass, octaves from the weather's */
} anchor_t;
#define NANCHOR 6
#define NLAND 5
static const anchor_t LAND[NLAND][NANCHOR] = {
    {   /* "Rhodes": AMB-1's Rhodes at the center, its neighbours around it */
        {"Rhodes", 0, 1.0f, 0.50f, 0.50f, 1.0f, 1.0f, 0.12f, 0.0f, 1.0f, 0.45f, 0.0f},
        {"Glass", 0, 2.0f, 0.85f, 0.70f, 0.8f, 1.4f, 0.05f, 0.0f, 1.0f, 0.45f, 0.3f},      /* the modulator an octave up: hollow, glassy */
        {"Reed", 0, 3.0f, 0.20f, 0.85f, 0.55f, 0.8f, 0.0f, 0.0f, 1.0f, 0.45f, -0.2f},      /* a twelfth: odd harmonics, a soft reed */
        {"Wurli", 0, 1.0f, 0.80f, 0.20f, 1.4f, 1.5f, 0.25f, 0.0f, 1.0f, 0.45f, 0.4f},      /* the same pair, driven harder: a bark */
        {"Oh", 1, 1.0f, 0.15f, 0.30f, 1.0f, 1.0f, 0.12f, -0.3f, 1.3f, 0.3f, -0.3f},        /* a dark, soft vowel */
        {"Ah", 1, 1.0f, 0.50f, 0.95f, 1.0f, 1.0f, 0.12f, 0.2f, 0.85f, 0.6f, 0.3f},         /* an open one */
    },
    {   /* "Voices": AMB-1's Voices at the center */
        {"Ah", 1, 1.0f, 0.50f, 0.50f, 1.0f, 1.0f, 0.12f, 0.0f, 1.0f, 0.45f, 0.0f},
        {"Oh", 1, 1.0f, 0.15f, 0.35f, 1.0f, 1.0f, 0.12f, -0.35f, 1.25f, 0.3f, -0.4f},
        {"Ee", 1, 1.0f, 0.85f, 0.65f, 1.0f, 1.0f, 0.12f, 0.45f, 0.7f, 0.6f, 0.3f},
        {"Hum", 1, 1.0f, 0.30f, 0.90f, 1.0f, 1.0f, 0.12f, -0.5f, 1.5f, 0.15f, -0.7f},      /* a closed mouth */
        {"Glass", 0, 2.0f, 0.80f, 0.15f, 0.7f, 1.2f, 0.05f, 0.0f, 1.0f, 0.45f, 0.2f},
        {"Rhodes", 0, 1.0f, 0.55f, 0.10f, 1.0f, 1.0f, 0.12f, 0.0f, 1.0f, 0.45f, 0.0f},
    },
    {   /* "Aurora": a wide, warm wash, the Voices' formant colour with the saw all the way up (the space pad) */
        {"Dawn", 1, 1.0f, 0.50f, 0.50f, 1.0f, 1.0f, 0.12f, 0.1f, 0.75f, 0.9f, 0.3f},
        {"Haze", 1, 1.0f, 0.15f, 0.30f, 1.0f, 1.0f, 0.12f, -0.2f, 1.0f, 0.7f, 0.0f},
        {"Ray", 1, 1.0f, 0.85f, 0.65f, 1.0f, 1.0f, 0.12f, 0.4f, 0.6f, 1.0f, 0.5f},
        {"Veil", 1, 1.0f, 0.30f, 0.90f, 1.0f, 1.0f, 0.12f, 0.0f, 0.9f, 0.8f, 0.2f},
        {"Bloom", 1, 1.0f, 0.80f, 0.15f, 1.0f, 1.0f, 0.12f, 0.3f, 0.7f, 0.95f, 0.4f},
        {"Dusk", 1, 1.0f, 0.55f, 0.10f, 1.0f, 1.0f, 0.12f, -0.4f, 1.2f, 0.6f, -0.2f},
    },
    {   /* "Glass": cold FM, the modulator a ratio of 2, 3 or 4 up: hollow, bell-like overtones */
        {"Ice", 0, 2.0f, 0.50f, 0.50f, 0.8f, 1.4f, 0.05f, 0.0f, 1.0f, 0.45f, 0.4f},
        {"Shard", 0, 3.0f, 0.85f, 0.70f, 0.6f, 1.2f, 0.03f, 0.0f, 1.0f, 0.45f, 0.6f},
        {"Frost", 0, 2.0f, 0.20f, 0.85f, 0.9f, 1.5f, 0.05f, 0.0f, 1.0f, 0.45f, 0.3f},
        {"Star", 0, 3.5f, 0.80f, 0.20f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.45f, 0.5f},
        {"Sleet", 0, 2.0f, 0.15f, 0.30f, 0.7f, 1.3f, 0.05f, 0.0f, 1.0f, 0.45f, 0.2f},
        {"Crystal", 0, 4.0f, 0.55f, 0.95f, 0.6f, 1.1f, 0.02f, 0.0f, 1.0f, 0.45f, 0.5f},
    },
    {   /* "Hollow": odd harmonics, a low index, darker and softer: a distant organ or a hollow synth */
        {"Hollow", 0, 3.0f, 0.50f, 0.50f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.45f, -0.1f},
        {"Pipe", 0, 3.0f, 0.85f, 0.70f, 0.4f, 0.8f, 0.0f, 0.0f, 1.0f, 0.45f, -0.2f},
        {"Flute", 0, 1.0f, 0.20f, 0.85f, 0.3f, 0.8f, 0.0f, 0.0f, 1.0f, 0.45f, -0.1f},
        {"Organ", 0, 2.0f, 0.80f, 0.20f, 0.35f, 0.9f, 0.02f, 0.0f, 1.0f, 0.45f, -0.3f},
        {"Drone", 0, 3.0f, 0.15f, 0.30f, 0.45f, 0.8f, 0.0f, 0.0f, 1.0f, 0.45f, -0.4f},
        {"Void", 0, 3.0f, 0.55f, 0.10f, 0.55f, 1.0f, 0.0f, 0.0f, 1.0f, 0.45f, -0.3f},
    },
};
static int pad_land_of(int v) { return v == 1 ? 1 : v >= 3 ? (v - 1 < NLAND ? v - 1 : NLAND - 1) : 0; }   /* the Colour's landscape (2 is the Choir) */
static int land_now;                            /* the landscape (Pad tone, set by the piece) */
static struct {
    walker_t sx, sy, wx, wy;                    /* the season's region and the weather's drift about it */
    float x, y;                                 /* where the cursor is (0..1) */
    int near;                                   /* the nearest anchor */
    float idx, bloom, h2, vowel, q, saw, fc;    /* the blend, rate-limited (brightness is rate-limited too, DESIGN section 6) */
} tcur;

/* the cursor's blend: inverse-distance weights over the anchors (at an anchor its own weight is ~250 to a
 * neighbour's ~6, so the anchor is heard nearly pure); init sets the values at once, else they slew */
static void cursor_blend(int init, float dt)
{
    const anchor_t *A = LAND[land_now];
    float wa = 0.0f, wf = 0.0f, wv = 0.0f, best = 1e9f, d2a[NANCHOR];
    float idx = 0.0f, bloom = 0.0f, h2 = 0.0f, vowel = 0.0f, q = 0.0f, saw = 0.0f, fc = 0.0f, want[7];
    float *const val[7] = {&tcur.idx, &tcur.bloom, &tcur.h2, &tcur.vowel, &tcur.q, &tcur.saw, &tcur.fc};
    int i, near = 0;
    for (i = 0; i < NANCHOR; i++) {
        float dx = tcur.x - A[i].x, dy = tcur.y - A[i].y, d2 = dx * dx + dy * dy, w = 1.0f / (d2 + 0.004f);
        d2a[i] = d2;
        if (d2 < best) {
            best = d2;
            near = i;
        }
        wa += w;
        fc += w * A[i].fc;
        if (A[i].colour) {
            wv += w;
            vowel += w * A[i].vowel;
            q += w * A[i].q;
            saw += w * A[i].saw;
        } else {
            wf += w;
            idx += w * A[i].idx;
            bloom += w * A[i].bloom;
            h2 += w * A[i].h2;
        }
    }
    if (init || d2a[near] < 0.7f * d2a[tcur.near])   /* a new nearest only when it is clearly nearer: no flicker on a boundary
                                                      * (178 changes in a fast hour without this, 2026-11-09) */
        tcur.near = near;
    want[0] = wf > 0.0f ? idx / wf : 1.0f;
    want[1] = wf > 0.0f ? bloom / wf : 1.0f;
    want[2] = wf > 0.0f ? h2 / wf : 0.12f;
    want[3] = wv > 0.0f ? vowel / wv : 0.0f;
    want[4] = wv > 0.0f ? q / wv : 1.0f;
    want[5] = wv > 0.0f ? saw / wv : 0.45f;
    want[6] = fc / wa;
    for (i = 0; i < 7; i++)
        *val[i] = init ? want[i] : slew(*val[i], want[i], 0.01f, 0.01f, dt);   /* a unit in 100 s; the cursor itself is slower */
}

const char *en_anchor_name(void) { return LAND[land_now][tcur.near].name; }
/* the screen's word: the Choir plays none of the Rhodes map's colours (only its brightness follows the map), so it is
 * named itself (Tomorrow's Man, TESTING #64 d, 2026-11-17: "let's make it accurate") */
const char *en_tone_word(void) { return choir_on ? "Choir" : en_anchor_name(); }

/* ------------------------------------------------------ TIDE: the conductor's state --- */
enum { L_PAD, L_BELLS, L_BASS, L_GROUND, NLAYERS };
static struct {
    walker_t s_speed, s_density, s_bright;      /* seasons (hours): how fast the weather moves, its density
                                                 * and brightness centers */
    walker_t bright, density, reg, space, chord;/* weather (minutes): the pad's tone, how many notes, the
                                                 * pad's register, the space, how long chords dwell */
    walker_t pres[NLAYERS];                     /* weather: each layer's presence */
    walker_t bact;                              /* weather: the bells' activity, log2 of their count (passages and lulls) */
    float alone;                                /* 0..1: how thin the garden under the bells is (their company, governor_step) */
    float speed;                              /* Drift: time's multiplier; 0 holds */
    float wspeed;                               /* the weather's: speed times the season's */
    float dens, br;                             /* the combined density and brightness deviations */
    float opening;                              /* seconds of a quicker start left (from silence) */
    /* the governor */
    float e_dry[NLAYERS], e_plate, e_echo;      /* energy since the last step (sums of squares, L and R) */
    uint32_t frames;
    float e20[NLAYERS], m3, m20, m60;           /* smoothed: each layer, the mix */
    float level20, level60, target;             /* dB */
    float trim, bias[NLAYERS], gdb[NLAYERS], g[NLAYERS];   /* the trim, each layer's bias, their slewed sum, linear */
    float ff, ff_e0[NLAYERS], ff_db0[NLAYERS];  /* the make-up for a resting layer: its energy (before its gain) and level as it left */
    float wshare[NLAYERS];                      /* each layer's share of the meter's (K-weighted) energy */
    float under, silent;                        /* seconds the mix has sat 6 dB under its target; seconds with no layer sounding */
    float share[NLAYERS], lead[NLAYERS];        /* each layer's share of the energy; seconds over the cap */
    uint8_t pad_thin;                           /* the pad has led too long: one voice fewer */
    float floor_hold;                           /* seconds the ensemble floor's voice is still asked for (a latch) */
    /* the ebb (Phase 5c) */
    float ebb, ebb20, ebb3;                     /* dB on every layer's gain (0: the tide is in); the same as the 20 s and 3 s meters hear it */
    float ebb_depth, ebb_hold;                  /* this ebb's bottom, dB under the target; real seconds left there */
    float ebb_t, ebb_next;                      /* Drift seconds since the last one; when the next is due */
    uint8_t ebbing;
    /* the pad */
    int pad_target;
    float since_entry, entry_gap;               /* seconds since a voice entered; the least before the next may */
    float pad_rest, pad_rest_max;               /* Drift seconds with no voice singing; the most such a rest lasts */
    float turn, turn_max;                       /* the pad's turn: real seconds a voice has been up; the most before a recess */
    float recess, away;                         /* real seconds of the recess left at its bottom; seconds with no voice heard */
    float dip, dip_depth;                       /* the recess: dB on the pad's gain (0: none); this one's bottom */
    uint8_t recessing;                          /* 0 a turn, 1 going down or at the bottom, 2 coming back */
    /* the pad's company (2026-11-14; the note above ear_w) */
    float e5;                                   /* the pad's energy on a 5 s meter (a voice rises 7 dB in the 20 s one's lag) */
    float expo;                                 /* the pad over its company as the ear weighs them, dB, as it sounds */
    float hold, hold5;                          /* dB on the pad's gain (0: none); the same as the 5 s meter hears it */
    float exposed, called;                      /* real seconds a voice has stood alone; seconds of the bells' quick return left */
    uint8_t hold_up;                            /* the hold is being given back this step */
    uint32_t acc;                             /* samples toward the next step */
} cd;
/* the pad's turn and recess (the note above pad_count) */
static float pad_turn_lo = 75.0f, pad_turn_span = 135.0f;      /* a turn: 75-210 s with a voice up */
static float pad_recess_lo = 20.0f, pad_recess_span = 50.0f;   /* a recess: 20-70 s at its bottom */
#define PAD_DIP_FALL 0.1f                       /* dB/s: the ebb's own pace down */
#define PAD_DIP_RISE 0.12f                      /* and back a little quicker (a voice's 0.4 and the gains' 0.16 beside it;
                                                 * at 0.15 one take read 1.52 dB in 2 s against the swell limit's 1.5) */
#define PAD_CARRY (150.0f + 150.0f * frand())   /* the pad alone while the bass rests: 2.5-5 minutes, then the bass returns */
#define PAD_TURN (pad_turn_lo + pad_turn_span * frand())
#define PAD_RECESS (pad_recess_lo + pad_recess_span * frand())
/* the pad's company (the note above ear_w) */
static float pc_line = 3.0f, pc_most = 14.0f, pc_call = 20.0f;   /* dB over its company a pad may stand; the most the hold
                                                 * takes (0: no hold); real seconds alone before the bells are called (0: never).
                                                 * The engine's since 2026-11-14 (Tomorrow's Man, TESTING #56: "the 'alarm' does not seem
                                                 * to be there any longer"; "I do think you have nailed it") */
#define PC_FALL 0.25f                           /* dB/s: the hold comes on a little slower than a voice rises (0.36) */
#define PC_RISE 0.1f                            /* and is given back at the recess's pace */
static float ground_db = -60.0f, ground_lin;   /* the Ground layer's level */
static uint8_t ground_on;
static float ground_gap;                        /* seconds before Ground may enter again; its stay */
static float ground_stay;
static float ground_due;                        /* weather seconds since it last left; when it is overdue (40-70 min)
                                                 * its presence is called up, so a rare layer is not an absent one */
static float ground_due_at;

/* a voice takes its note (an entry, or a hand-off's second half) */
static void pad_note_on(pad_t *v, int i)
{
    (void)i;
    if (en_playing && v->midi_note)
        midi(0x80, v->midi_note, 0);
    v->note = v->target;
    pad_height(v);
    {   /* the discrete set: the nearest anchor's colour and ratio, as the note starts (DESIGN section 4) */
        const anchor_t *a = &LAND[land_now][tcur.near];
        v->voices = choir_on ? 2 : a->colour;
        v->ratio = a->ratio;
#ifdef EN_HOST
        if (pv_mode == 1 && v->ratio == 2.0f)   /* an A/B: no Glass */
            v->ratio = 1.0f;
#endif
        v->land = (uint8_t)(choir_on ? NLAND : land_now);
        v->ph[1] = v->ph[0];                    /* the modulator in phase with the carrier */
    }
    v->midi_note = (uint8_t)v->target;
    if (en_playing)
        midi(0x90, v->midi_note, (uint32_t)(50.0f + 40.0f * fm_db2lin(v->base_db)));
}

static void pad_enter(pad_t *v, int i)          /* a voice enters: from the floor, at the swell limit */
{
    int k;
    /* its size: -6..-2 dB, moved a little by the governor's headroom (the target less the mix's level now): the trim
     * and the make-up answer the same deficit, so the voice's own share of it is small (Phase 3: 0.8 per dB) */
    v->base_db = fm_clampf(-6.0f + 4.0f * frand() + 0.4f * fm_clampf(cd.target + cd.ebb20 - cd.level20, -4.0f, 4.0f), -14.0f, 0.0f);
    v->stay = 60.0f + 240.0f * frand();
    v->rise = 0.36f;                            /* (0.4 until 2026-11-13: v1's take read 1.48 dB in 2 s against the limit's 1.5
                                                 * and two takes with the pad's turn 1.50; a tenth slower for the margin)
                                                 * with the governor's 0.16 dB/s this stays under the swell limit;
                                                 * the opening (play from silence) is quicker */
    {   /* from -45 dB after the gains: with the trim and the make-up at +15 dB in a thin stretch, a voice appearing at -45
         * before them was a step the ear and the swell check both caught (2026-11-09, at a pause in the 8 h render) */
        /* Phase 5b: from -52. In a take where the pad sat 5 dB lower than usual (its median -50 dB), a lone voice
         * appearing at -45 came up through the report's floor (30 dB under the pad's median) with the plate filling
         * behind it: 3 dB/s on the 3 s level, 57 such seconds in 8 h, all 25-30 dB under the pad's median and under
         * the bass. Seven dB lower it appears under the floor in any take; the cost is 17 s more of a rise nobody hears. */
        const float from = -52.0f - fm_maxf(cd.gdb[L_PAD], 0.0f);
        v->ldb = cd.opening > 0.0f ? -12.0f : v->st == PV_OUT ? fm_maxf(v->ldb, from) : from;
    }
    v->crest = 0.0f;
    v->vowel = fm_clampf(bright * 0.01f + 0.5f * cd.br + v->bw.x + tcur.vowel, 0.0f, 1.0f);   /* enters where the weather and the cursor are */
    v->hf = 1.0f;
    v->hand = 0;
    v->st = PV_ON;
    for (k = 0; k < 3; k++)                     /* a fresh breath: the walkers start near their centers */
        v->lw[k].x = fm_clampf(0.5f * v->lw[k].sd * gauss(), v->lw[k].lo, v->lw[k].hi);
    pad_note_on(v, i);
    v->idx = (0.04f + 0.1f * fm_clampf(bright * 0.01f + 0.6f * cd.br + v->bw.x, 0.0f, 1.0f)) * tcur.idx * v->hk;
    cd.since_entry = 0.0f;
    {   /* the next entrance no sooner than 6-46 s, mostly the short end (the square of a uniform draw: median 16 s).
         * Phase 4 and before: 10 s flat, and a third of all entrances came 9.5-10.5 s apart, a 10-15 s rhythm in the
         * pad; a uniform 8-25 s still left one hour of eight with a 7 dB peak at 14 s (after a chord change the
         * voices re-form one gap apart, so the gap's spread is the rhythm's) */
        const float r = frand();
        cd.entry_gap = 6.0f + 40.0f * r * r;
    }
    /* (Phase 3 knocked 3 dB off a raised trim here, "the governor hears it coming"; removed 2026-11-08: with the
     * gains slewed at 0.16 dB/s every entrance ducked the other layers 3 dB over 20 s, a sawtooth at the pace of the
     * entrances. The voice's size is already scaled by the headroom; the trim follows the meter.) */
}

/* a new key (piece, root, scale, colour) from the player: the voices still singing the old one cross
 * over in about a second to their notes of the new chord (AMB-1's hand-off; the engine's own changes
 * never do this: there a voice dissolves and comes back) */
#define HAND_STEP (32.0f / (1.0f * EN_SR))
static void pad_handoff(void)
{
    int i;
    for (i = 0; i < EN_NPAD; i++) {
        pad_t *v = &pad[i];
        if (v->st != PV_OFF && v->amp > 0.002f &&
            ((float)v->midi_note != v->target || v->land != (uint8_t)(choir_on ? NLAND : land_now)))
            v->hand = 1;                        /* singing: crosses over; dissolving: goes out with the old key */
        else if (v->st == PV_OFF && !v->hand)
            v->note = v->target;
    }
}

/* a chord change: a voice singing a note the new chord doesn't have dissolves (never bent); a voice on a
 * shared note holds it to the end of its stay */
static void pad_leave(void)
{
    int i, k;
    for (i = 0; i < EN_NPAD; i++) {
        pad_t *v = &pad[i];
        int pc = v->midi_note % 12, shared = 0;
        for (k = 0; k < chord_nt; k++)
            shared |= chord_pcs[k] == pc;
        if (v->st == PV_ON && !shared && !v->hand)
            v->st = PV_OUT;
        else if (v->st == PV_OFF)
            v->note = v->target;
    }
}

static void pad_coefs(pad_t *v, int i)          /* every 32 samples: a hand-off, pitch, the vowel */
{
    float f, vw, f1, f2, b;
    int a;
    if (v->hand == 1) {                         /* out of the old key */
        v->hf -= HAND_STEP;
        if (v->hf <= 0.0f) {
            if (v->st == PV_OUT) {              /* it was dissolving anyway: gone */
                v->st = PV_OFF;
                v->lin = 0.0f;
                v->ldb = -60.0f;
                v->rest = 20.0f;
                v->hf = 1.0f;
                v->hand = 0;
                if (v->midi_note) {
                    midi(0x80, v->midi_note, 0);
                    v->midi_note = 0;
                }
                return;
            }
            v->hf = 0.0f;
            v->hand = 2;
            pad_note_on(v, i);
        }
    } else if (v->hand == 2) {                  /* into the new */
        v->hf += HAND_STEP * 0.6f;
        if (v->hf >= 1.0f) {
            v->hf = 1.0f;
            v->hand = 0;
        }
    }
    v->vib += (4.6f + 0.4f * (float)i) * 32.0f / EN_SR;
    if (v->vib >= 1.0f)
        v->vib -= 1.0f;
    /* no pitch drift on the pad (AMB-1: ±4 cents a voice with Motion, "as tape"). The plate's resonances at
     * this decay are about 0.1 Hz wide, so even a slow drift walks a held note across them in seconds and
     * its reverb swells or sinks 6-9 dB on its own; and voices drifting apart beat on a chord's coincident
     * harmonics. The tape wow stays where it is heard: on the echo's repeats. */
    f = mtof(v->note);
    /* (the Choir: the place between its cycles moves on the shimmer's rates, and a little wow) */
    const float pl_sh = v->voices == 2 ? fm_maxf(pl_shimmer, 1.0f) : pl_shimmer;
    const float pl_ww = v->voices == 2 && pl_wow <= 0.0f ? CHOIR_WOW : pl_wow;
    if (pl_sh > 0.0f || pl_ww > 0.0f || pl_vib > 0.0f || pl_chorus > 0.0f) {   /* the pad's life (the note at pl_breath) */
        int k;
        float cents, col;
        for (k = 0; k < 4; k++) {
            v->lfo[k] += LIFE_HZ[k][i] * (32.0f / EN_SR);
            if (v->lfo[k] >= 1.0f)
                v->lfo[k] -= 1.0f;
        }
        cents = pl_ww * 5.0f * (0.6f * sine(v->lfo[0]) + 0.4f * sine(v->lfo[1]))
                + pl_vib * 6.0f * (0.3f + 0.7f * v->crest) * sine(v->vib);
        f *= 1.0f + cents * 0.00057779f;
        col = pl_sh * (0.6f * sine(v->lfo[2]) + 0.4f * sine(v->lfo[3]));
        v->inc[0] = f * 0.99856f * (1.0f / EN_SR);
        v->inc3 = v->inc[0] * (1.0f + (i & 1 ? -1.0f : 1.0f) * (2.5f + mod[(i + 1) % NMOD_PAD].v) * 0.00057779f);
        v->inc[1] = v->inc[0] * v->ratio + pl_sh * 0.5f * LIFE_OFF[i] * (1.0f + 0.5f * mod[i].v) * (1.0f / EN_SR);
        v->inc2 = 2.0f * v->inc[0] - pl_sh * 0.6f * LIFE_OFF[i] * (1.0f / EN_SR);
        b = fm_clampf(bright * 0.01f + 0.5f * cd.br + v->bw.x + 0.15f * v->crest + 0.1f * motion * mod[(i + 2) % NMOD_PAD].v
                      + tcur.vowel + 0.25f * col, 0.0f, 1.0f);
        v->vowel = slew(v->vowel, b, 0.05f, 0.05f, 32.0f / EN_SR);
        v->idx = slew(v->idx, ((0.04f + 0.1f * fm_clampf(bright * 0.01f + 0.6f * cd.br + v->bw.x, 0.0f, 1.0f)) * tcur.idx
                      * (1.0f + 0.4f * (pl_sh + col))
                      + 0.1f * v->crest * tcur.bloom) * v->hk, 0.05f, 0.05f, 32.0f / EN_SR);
    } else {
    v->inc[0] = f * 0.99856f * (1.0f / EN_SR);  /* -2.5 cents (AMB-1's pair; its second saw is not played) */
    v->inc[1] = v->inc[0] * v->ratio;           /* FM: the modulator, at the anchor's ratio (Phase 4) */
    v->inc2 = 2.0f * v->inc[0];
    v->inc3 = v->inc[0];
    /* the vowel: Bright, the weather's brightness, this voice's offset, its crest, and the tape wobble;
     * it moves at most a fiftieth of its range a second (the formants' gain would otherwise swell) */
    b = fm_clampf(bright * 0.01f + 0.5f * cd.br + v->bw.x + 0.15f * v->crest + 0.1f * motion * mod[(i + 2) % NMOD_PAD].v
                  + tcur.vowel, 0.0f, 1.0f);   /* + the cursor's vowel (Phase 4) */
    v->vowel = slew(v->vowel, b, 0.02f, 0.02f, 32.0f / EN_SR);
    v->idx = slew(v->idx, ((0.04f + 0.1f * fm_clampf(bright * 0.01f + 0.6f * cd.br + v->bw.x, 0.0f, 1.0f)) * tcur.idx
                  + 0.1f * v->crest * tcur.bloom) * v->hk, 0.01f, 0.01f, 32.0f / EN_SR);   /* x the cursor's index and bloom,
                                                 * x the note's height (Phase 5c: a high voice is rounder) */
    }
    vw = v->vowel * 2.999f;
    a = (int)vw;
    vw -= (float)a;
    f1 = VOWEL[a][0] + (VOWEL[a + 1][0] - VOWEL[a][0]) * vw;
    f2 = VOWEL[a][1] + (VOWEL[a + 1][1] - VOWEL[a][1]) * vw;
    svf_coefs(v->c1, f1 * (1.0f + 0.03f * (float)i), 0.45f * tcur.q);   /* the formants' width: the cursor's */
    svf_coefs(v->c2, f2 * (1.0f - 0.02f * (float)i), 0.3f * tcur.q);
}

static inline float blep_saw(float t, float dt)
{
    float y = 2.0f * t - 1.0f;
    if (t < dt) {
        float x = t / dt;
        y -= x + x - x * x - 1.0f;
    } else if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        y -= x * x + x + x + 1.0f;
    }
    return y;
}

/* ------------------------------------------------------------------ bass --- */
static struct {
    float ph, inc, note, target, amp;
    float ldb, lin;                             /* TIDE: its level in dB (slewed) and linear */
    walker_t lw[2];
    float rest_t, gap;                          /* weather seconds rested so far; how long this rest lasts */
    float stay_t, min_stay;                     /* weather seconds present so far; the least a stay lasts */
    uint8_t midi_note, rest;                    /* rest: sitting out (the conductor's presence) */
} bass;
static float bass_vol, glide_k = 1.0f / (0.25f * EN_SR);      /* glide_k: 1 / the glide's time in samples */

/* ----------------------------------------------------------------- bells --- */
#define NBELL 6
typedef struct {
    float cph, mph, cinc, minc, amp, adec, idx, idec, att, arate, gl, gr;
    int32_t wait;
    uint8_t on, note, loop;
} bell_t;
static bell_t bell[NBELL];
static float bell_vol, btone;
static int btype, bass_type;                    /* the bells' and the bass' sound (P_BELLTYPE, P_BASSTYPE) */
static float bass_ph2;                          /* the Void's second sine, a few cents off the first */
static float bass_lp;                           /* the soft saw's and pulse's low-pass */
static float bell_amt = 1.0f;                   /* the Bells knob's part of the count: (Bells / 65)^1.5, 1 at Garden's 65 */
#define BELL_ACT 0.9f                           /* the activity walker's center, log2 of the count against Phase 5's
                                                 * (a first take at 0.5: 12 strikes in three minutes at the median, Phase 5's
                                                 * 11; the middle of Phase 4's 42 and Phase 5's 11 is about 20) */
#define BELL_BRIDGE 0.5f                        /* log2 more bells in a garden that has thinned to nothing under them */
static struct {                                 /* TIDE: the bells as a layer (Tomorrow's Man, TESTING #11) */
    float ldb, lin;                             /* the layer's level in dB (slewed) and linear */
    walker_t lw[2];                             /* it breathes on two summed walkers */
    float rest_t, gap;                          /* weather seconds rested so far; how long this rest lasts */
    float stay_t, min_stay;                     /* weather seconds present so far; the least a stay lasts */
    float quiet;                                /* seconds since a bell rang: 20 s of none counts as out for the pad's floor */
    float busy, busy_max;                       /* seconds the activity has been up; the most a passage lasts */
    uint8_t rest;                               /* sitting out: no bell rings (its presence is low) */
} bells;
static int8_t loop_note[EN_NLOOP][EN_LOOP_MAX];
static uint8_t loop_vel[EN_NLOOP][EN_LOOP_MAX], loop_pos[EN_NLOOP];
static const uint8_t LOOP_BASE[EN_NLOOP] = {72, 67, 79, 62};     /* each loop's register */
static const float LOOP_PAN[EN_NLOOP] = {-0.6f, 0.35f, 0.7f, -0.25f};
static const float RATIO[5] = {1.0f, 2.0f, 3.0f, 3.5f, 4.77f};
#define LOOP_STEP 12u                           /* sixteenths a loop slot (TIDE; AMB-1: 4) */
static const uint8_t LOOP_OFS[EN_NLOOP] = {0, 3, 7, 10};   /* each loop's sixteenth of the slot */

/* ----------------------------------------------------------------- pulse --- */
/* AMB-1's pulse (a thump and two hats on Euclidean patterns). TIDE does not play it (Tomorrow's Man: no drum);
 * the code stays for reference under EN_PULSE. Its slot (level, taps, MIDI channel 9 -> 3) is Ground's. */
#if EN_PULSE
typedef struct {
    float ph, inc, inc_end, amp, adec, idx, x1, x2, att, mph[4], pend;
    int32_t wait;                               /* swing: samples until a hit that waits (pend: its velocity) */
    uint8_t on, note;
} drum_t;
static drum_t drum[EN_NDRUM];
static uint8_t drum_pos[EN_NDRUM];
static uint32_t drum_pat[EN_NDRUM];
static float swing, hat_f = 0.9f, hat_open = 0.42f;
static uint32_t noise = 22222u;
static const uint8_t DRUM_KMAX[EN_NDRUM] = {5, 7, 3};
static const uint8_t DRUM_GM[EN_NDRUM] = {36, 42, 46};          /* kick, closed hat, open hat */
/* the hats' metal: four square waves at the 808's inharmonic ratios (Hz / EN_SR) */
static const float HAT_INC[4] = {304.4f / EN_SR, 369.6f / EN_SR, 522.7f / EN_SR, 800.0f / EN_SR};
#endif
static float pulse_vol;                         /* Ground's level (the Pulse parameter) */
static float step_len, pulse_left;               /* a sixteenth, in samples; to the next MIDI clock pulse */

/* ------------------------------------------------------------------ space --- */
static float echo_send, rev_send, echo_len, echo_len_t, echo_fb = 0.55f;
static float rev_in_gain = 1.0f;                /* falls to 0 while frozen */

/* -------------------------------------------------------------- harmony --- */
static void scale_set(void)
{
    nsteps = en_scale(pool, steps);
}

static int reg_now;                             /* the register shift (the register walker, whole semitones) */
#define OFF_DOWN 32                             /* a loop offset of OFF_DOWN + o: the tone o, an octave down */
#define BELL_REG (-3)                           /* the bells' registers against AMB-1's, semitones (the root is found under it) */

static int bell_note(int l, int off)            /* a loop slot's note, from the chord's degree */
{
    int down = off >= OFF_DOWN, d, oct, s, base, root = froot, n;
    if (down)
        off -= OFF_DOWN;
    d = deg + off;
    oct = d / nsteps - down;
    s = steps[d % nsteps];
    /* the register walker moves the bells too: it moves each loop's register, and the root is found at or below
     * that, so every note stays a note of the field. (Phase 3b to 5 added reg_now to the note itself: a transposition
     * by plain semitones, and 38-46% of all bell strikes in those renders were outside the scale; found 2026-11-09
     * from Tomorrow's Man's "sharper bell tones" at 3:27 of the Phase 4 take, the bells a major third down. BELL_REG: the
     * bells sit a little lower than AMB-1's, where he heard a nylon guitar.) */
    base = LOOP_BASE[l] + BELL_REG + reg_now;
    base -= ((base - root) % 12 + 12) % 12;                         /* the root at or below the register */
    n = base + s + 12 * oct + 12 * par[P_BOCT];
    while (n > 103)
        n -= 12;
    while (n < 36)
        n += 12;
    return n;
}

static int pad_lo = 48, pad_hi = 77;            /* the pad's window, C3..F5, moved by the register walker */
static uint8_t pad_range_host;                  /* the harness set the window: the colour leaves it alone */
/* the colour's window: the Choir sings an octave down, C2..F4 (Tomorrow's Man, TESTING #52: "the superior render overall"). When
 * the window moves the voices are seated in it afresh (a tone over the floor, in fifths), as at start: led from where
 * they were they would stay in the top of the new one. They take the notes as they next enter or hand over. */
static void pad_window(void)
{
    const int lo = choir_on ? 36 : 48;
    int i;
    if (pad_range_host || lo == pad_lo)
        return;
    pad_lo = lo;
    pad_hi = lo + 29;
    for (i = 0; i < EN_NPAD; i++) {
        pad[i].target = (float)(lo + 2 + 7 * i);
        if (pad[i].st == PV_OFF)
            pad[i].note = pad[i].target;
    }
}
#ifdef EN_HOST
void en_host_pad_range(int lo, int hi)          /* an A/B (Tomorrow's Man, TESTING #51: the pad's C5, 523 Hz, reads as an alarm) */
{
    pad_range_host = 1;
    pad_lo = lo;
    pad_hi = hi < lo + 12 ? lo + 12 : hi;       /* an octave at the least: every pitch class has a place */
    {   /* the voices are seated in it as the engine seats them in its own (50 + 7 i: a tone over the floor, in fifths) */
        int i;
        for (i = 0; i < EN_NPAD; i++)
            pad[i].note = pad[i].target = (float)(lo + 2 + 7 * i);
    }
}
#endif

static void pad_retarget(void)                  /* the chord's tones to the four voices */
{
    int from[4], to[4], i;
    chord_nt = en_chord_tones(pool, froot, deg, chord_pcs);
    for (i = 0; i < EN_NPAD; i++)
        from[i] = (int)(pad[i].target + 0.5f);
    en_voice_lead(from, chord_pcs, chord_nt, to, pad_lo + reg_now, pad_hi + reg_now);
    for (i = 0; i < EN_NPAD; i++) {             /* each voice takes its note as it next enters */
        pad[i].target = (float)to[i];
        en_chord[i] = (uint8_t)to[i];
    }
    en_chord_root = (uint8_t)chord_pcs[0];
    en_chord_n = (uint8_t)chord_nt;
}

static void bass_to(int pc)
{
    int n = 36 + pc % 12 + 12 * par[P_BASSOCT];     /* C2..B2, an octave down or up */
    bass.target = (float)n;
    if (en_playing && bass.midi_note != n && !bass.rest) {
        midi(0x82, bass.midi_note, 0);
        midi(0x92, (uint32_t)n, 80);
    }
    bass.midi_note = (uint8_t)n;
}

static float ground_target;                     /* Ground's note (MIDI), C1..B1 */
static void ground_to(int pc) { ground_target = (float)(24 + pc % 12); }

static float drift_left, drift_total, evolve;   /* the chord's dwell, in weather seconds; evolve: Evolve, 0..1 */
static uint8_t change_pending, fifth_done;

/* ------------------------------------------- TIDE Phase 5: the harmonic field (DESIGN section 7) --- */
/* The field is a scale on a root. It starts at the player's (home) and moves over hours, one pitch class at a
 * time, on two season walkers:
 *   the set   the scale's notes move a fifth round the circle with the root held: one note of the seven (or
 *             five) goes a semitone up or down and the mode is one step brighter or darker (Dorian's sixth
 *             falls: Aeolian; its third rises: Mixolydian). The walker wanders about home, tilted by Bright.
 *   the root  home itself moves a fifth up or down inside the same set of notes: no note changes, only which
 *             of them is the center (D Dorian to A Aeolian). Rarer.
 * A step is done at a chord change and only if the root and its fifth stay in the scale (no Locrian). The note
 * that leaves is never bent: the step waits for a chord change at which no pad voice is singing it, the bells'
 * cells hold scale steps, not pitches, so their note on that step simply plays the new one the next time round,
 * and a ringing bell rings out. Drift at Hold holds the field. */
#define FLD_MODE_TAU (60.0f * 60.0f)
#define FLD_MODE_SD 1.2f
#define FLD_ROOT_TAU (100.0f * 60.0f)
#define FLD_ROOT_SD 0.8f
#define FLD_DWELL ((17.0f + 13.0f * frand()) * 60.0f)   /* season seconds a field lasts at the least: 34-60 min at Garden's half speed */
static struct {
    walker_t mode, root;                        /* the set's and the root's places, in fifths from home */
    int shift, move;                            /* where the field is now */
    float since, dwell;                         /* season seconds since it last changed; the least before the next step */
    int8_t pend;                                /* a step asked for: +1/-1 the set a fifth up/down, +2/-2 the root */
} fld;

static uint32_t rot12(uint32_t m, int k)        /* a set of pitch classes, k semitones up */
{
    k = (k % 12 + 12) % 12;
    return ((m << k) | (m >> (12 - k))) & 0xFFFu;
}

static void field_publish(void)
{
    en_field_root = (uint8_t)froot;
    en_field_pool = (uint16_t)pool;
}

static void field_home(void)                    /* the player's key (a piece, Root, the scale): the field is home again */
{
    froot = (par[P_ROOT] % 12 + 12) % 12;
    pool = home_pool;
    fld.shift = fld.move = 0;
    fld.pend = 0;
    fld.since = 0.0f;
    scale_set();
    field_publish();
}

const char *en_field_name(char *buf)            /* "D Dorian"; a scale with no name as "D 0x6AD" (16 bytes) */
{
    static const char HEX[] = "0123456789ABCDEF";
    const char *r = EN_NOTE_NAME[en_field_root % 12], *m = 0;
    const uint32_t pl = en_field_pool;
    int k = 0, i;
    for (i = 0; i < EN_NMODES; i++)
        if (EN_MODE_POOL[i] == pl)
            m = EN_MODE_NAME[i];
    while (*r)
        buf[k++] = *r++;
    buf[k++] = ' ';
    if (m) {
        while (*m && k < 15)
            buf[k++] = *m++;
    } else {
        buf[k++] = '0';
        buf[k++] = 'x';
        buf[k++] = HEX[pl >> 8 & 15u];
        buf[k++] = HEX[pl >> 4 & 15u];
        buf[k++] = HEX[pl & 15u];
    }
    buf[k] = 0;
    return buf;
}

/* a step, tried at a chord change. Returns 1 the set moved, 2 the root moved, 0 not done; *never: it cannot
 * be done from here at all (the root or its fifth would leave the scale), so the walker is turned back */
static int field_step(int d, int *never)
{
    const uint32_t abs0 = rot12(pool, froot);
    int i;
    *never = 0;
    if (d == 1 || d == -1) {
        const uint32_t abs1 = rot12(abs0, d > 0 ? 7 : 5), gone = abs0 & ~abs1, come = abs1 & ~abs0;
        int pc_gone = -1, n = 0;
        for (i = 0; i < 12; i++)
            if (gone >> i & 1u) {
                pc_gone = i;
                n++;
            }
        if (n != 1 || (come != rot12(gone, 1) && come != rot12(gone, 11)) || !(abs1 >> froot & 1u) ||
            !(abs1 >> ((froot + 7) % 12) & 1u)) {
            *never = 1;                         /* not one note moving a semitone, or the root or its fifth would go */
            return 0;
        }
        for (i = 0; i < EN_NPAD; i++)           /* a voice still sings the note that goes: later */
            if (pad[i].st != PV_OFF && pad[i].ldb > -40.0f && (int)pad[i].midi_note % 12 == pc_gone)
                return 0;
        pool = rot12(abs1, 12 - froot);
        fld.shift += d;
        scale_set();
        return 1;
    }
    {
        const int nr = (froot + (d > 0 ? 7 : 5)) % 12, old_pc = (froot + steps[deg]) % 12;
        if (!(abs0 >> nr & 1u) || !(abs0 >> ((nr + 7) % 12) & 1u)) {
            *never = 1;
            return 0;
        }
        froot = nr;
        pool = rot12(abs0, 12 - froot);
        fld.move += d > 0 ? 1 : -1;
        scale_set();
        for (i = 0; i < nsteps; i++)            /* the chord that is sounding keeps its notes: its degree is renumbered */
            if ((froot + steps[i]) % 12 == old_pc)
                deg = i;
        return 2;
    }
}

static void drift_reset(void)                   /* how long this chord dwells: the chord walker, minutes */
{
    drift_total = 90.0f * fm_exp2f(cd.chord.x) * (0.7f + 0.6f * frand());
    drift_left = drift_total;
    fifth_done = 0;
}

static void chord_change(void)                  /* to another degree: the near, strong moves first */
{
    static const float W7[7] = {4.0f, 2.0f, 1.0f, 3.0f, 3.0f, 3.0f, 0.4f};
    float w[12], sum = 0.0f, r;
    int i, stepped = 0;
    if (fld.pend && cd.speed > 0.0f) {          /* the field's step, if one is asked for (never at Hold) */
        int never;
        stepped = field_step(fld.pend, &never);
        if (stepped) {
            /* the walker is set on the new place: going back takes a full three quarters of a step again (it had only
             * half a step to fall, and a field flipped to and fro: one 8 h take saw three fields in six changes) */
            if (stepped == 1)
                fld.mode.x = (float)fld.shift - mode_tilt;
            else
                fld.root.x = (float)fld.move;
            fld.pend = 0;
            fld.since = 0.0f;
            fld.dwell = FLD_DWELL;
            field_publish();
        } else if (never) {
            if (fld.pend == 1 || fld.pend == -1)
                fld.mode.x = (float)fld.shift;
            else
                fld.root.x = (float)fld.move;
            fld.pend = 0;
        }
    }
    if (stepped == 2) {
        i = 0;                                  /* home has moved: to the new tonic (if the chord is it already, it stays) */
    } else {
        for (i = 0; i < nsteps; i++) {
            w[i] = i == deg ? 0.0f : nsteps == 7 ? W7[i] : i == 0 ? 3.0f : 1.5f;
            sum += w[i];
        }
        r = frand() * sum;
        for (i = 0; i < nsteps; i++) {
            r -= w[i];
            if (r <= 0.0f && w[i] > 0.0f)
                break;
        }
        if (i >= nsteps)
            i = deg == 0 ? (nsteps > 1 ? 1 : 0) : 0;
    }
    deg = i;
    pad_retarget();
    pad_leave();
    bass_to(chord_pcs[0]);
    ground_to(chord_pcs[0]);
    drift_reset();
}

/* ------------------------------------------------------------- the loops --- */
static int rand_offset(void)                    /* scale steps above the chord's degree */
{
    if (frand() < 0.72f) {                      /* a chord tone; sometimes an octave up, sometimes down (TIDE) */
        int o = 2 * irand(chord_nt);
        float r = frand();
        return r < 0.25f ? o + nsteps : r < 0.45f ? o + OFF_DOWN : o;
    }
    return 1 + 2 * irand(3);                    /* a passing note: the 2nd, 4th or 6th */
}

static int loop_count(int l)
{
    int i, c = 0;
    for (i = 0; i < EN_LOOP_LEN[l]; i++)
        c += loop_note[l][i] >= 0;
    return c;
}

/* 0..4 notes a loop: Density, moved by the weather's density, the bells' presence and the governor.
 * At Garden (Density 40) a fully present loop carries about 1.1 notes, a thinning one none: the bells'
 * presence (center 0.5) empties the loops below 0.25 and rests the layer below 0.3 (Tomorrow's Man, TESTING #11:
 * the bells must come and go like the pad does). AMB-1 carried 0..5 and never emptied. */
static int loop_target(void)
{
    float presf = fm_clampf((cd.pres[L_BELLS].x - 0.25f) * (1.0f / 0.35f), 0.0f, 1.0f);
    /* the weather's and the season's density, without the governor's part of cd.dens: a raised trim added a fifth to
     * the count exactly when the garden was thin (Tomorrow's Man, TESTING #20: 58 bells in three minutes at 3:27 of the Phase 4
     * take, the bass away, the pad at a voice, the trim on its rail) */
    float dens = fm_clampf(cd.density.x + cd.s_density.x, -1.0f, 1.0f);
    float d = (float)par[P_DENSITY] * 0.028f * (1.0f + 0.5f * dens) * presf * fm_db2lin(0.7f * fm_minf(cd.trim, 0.0f));
    /* (the trim only thins: a raised trim no longer adds notes, which made the bells busiest when the garden was
     * thinnest, 33-41 a minute in a 40 min check on 2026-11-08) */
    /* Phase 5b (Tomorrow's Man, TESTING #21: "I don't want them too frequent always or too sparse always"; a middle ground
     * between the Phase 4 take, 12.5 a minute and never letting up, and the Phase 5 take, 3.2): the count rides the
     * bells' activity, a weather walker of its own (BELL_ACT: its center, log2; 0.33x to 2.3x about it), so a passage
     * of bells comes, has the stage for some minutes and lets up; a thin garden draws them out a little (BELL_BRIDGE:
     * his "bridge" at 3:25 of the Phase 4 take, the pad gone and the bells alone, sparse and low), where they are
     * also softer and rounder (governor_step, bell_voice); and the Bells knob is their amount, count and level
     * together (his question: one knob for frequency and amplitude): the count goes as (Bells / 65)^1.5. */
    d *= fm_exp2f(BELL_ACT + cd.bact.x + BELL_BRIDGE * cd.alone) * bell_amt;
    return (int)(fm_clampf(d, 0.0f, 3.0f) + frand());
}
static uint8_t loop_rest[EN_NLOOP];             /* this pass: silent (Evolve) */

static void loop_add(int l)
{
    int len = EN_LOOP_LEN[l], i, s;
    for (i = 0; i < 8; i++) {
        s = irand(len);
        if (loop_note[l][s] < 0) {
            loop_note[l][s] = (int8_t)rand_offset();
            loop_vel[l][s] = (uint8_t)(10 + irand(54));           /* TIDE: 0.10..0.63 (Tomorrow's Man's numbers, TESTING #20; AMB-1 0.50..0.99,
                                                                   * Phase 3b 0.30..0.94, Phase 4 0.25..0.80) */
            return;
        }
    }
}

static int loop_pick(int l)                     /* a random sounding slot, -1 none */
{
    int len = EN_LOOP_LEN[l], i, s = irand(len);
    for (i = 0; i < len; i++, s = (s + 1) % len)
        if (loop_note[l][s] >= 0)
            return s;
    return -1;
}

static void loop_publish(int l)
{
    int i;
    for (i = 0; i < EN_LOOP_MAX; i++)
        en_loop_note[l][i] = i < EN_LOOP_LEN[l] ? loop_note[l][i] : -1;
}

static void loop_regrow(int l)
{
    int i, n = loop_target();
    for (i = 0; i < EN_LOOP_MAX; i++)
        loop_note[l][i] = -1;
    for (i = 0; i < n; i++)
        loop_add(l);
    loop_publish(l);
}

static void loop_pass(int l)                    /* the end of a pass: grow, prune, evolve */
{
    int c, t, s;
    const float ev = evolve;
    if (cd.speed <= 0.0f) {                     /* Drift at Hold: the cells keep their notes (and none sits out) */
        loop_rest[l] = 0;
        return;
    }
    c = loop_count(l);
    t = loop_target();
    for (; c < t; c++)                          /* Density: to its count by the end of a pass */
        loop_add(l);
    for (; c > t && (s = loop_pick(l)) >= 0; c--)
        loop_note[l][s] = -1;
    loop_rest[l] = frand() < 0.1f + ev * 0.25f;    /* TIDE: a loop sits a pass out now and then (AMB-1: ev * 0.15) */
    if (frand() < ev * 0.7f && (s = loop_pick(l)) >= 0) {
        if (frand() < 0.5f) {
            loop_note[l][s] = (int8_t)rand_offset();             /* another note */
        } else {
            int d = irand(EN_LOOP_LEN[l]);                       /* the same note, elsewhere */
            if (loop_note[l][d] < 0) {
                loop_note[l][d] = loop_note[l][s];
                loop_vel[l][d] = loop_vel[l][s];
                loop_note[l][s] = -1;
            }
        }
    }
    loop_publish(l);
}

/* each loop its own bell: index, decay and ratio a little apart, so the four can be told apart */
static const float LOOP_IDX[EN_NLOOP] = {1.0f, 0.55f, 1.5f, 0.8f}, LOOP_DEC[EN_NLOOP] = {1.0f, 1.35f, 0.75f, 1.6f};
static const int8_t LOOP_RAT[EN_NLOOP] = {0, 0, 1, -1};

/* the guard (amb.h): what is shed at each step */
volatile uint8_t en_shed;
static volatile uint8_t shed_hit;
static uint32_t shed_up_ms, shed_calm_ms;
static uint8_t shed_begun;
static const uint8_t SHED_NBELL[4] = {NBELL, 4, 3, 2}, SHED_NPAD[4] = {EN_NPAD, EN_NPAD, 2, 1};
static const float SHED_FALL[4] = {0.0f, 0.0f, 1.0f, 6.0f};   /* dB/s: a pad voice sent out by the guard leaves at the
                                                               * swell limit, and in seven seconds at the last step */
void en_overload(void) { shed_hit = 1; }
void en_load(uint32_t pct, uint32_t ms)
{
    if (!shed_begun) {
        shed_begun = 1;
        shed_up_ms = ms - 400u;
        shed_calm_ms = ms;
    }
    if (shed_hit) {
        shed_hit = 0;
        pct = 100u;
    }
    if (pct >= EN_SHED_UP) {
        shed_calm_ms = ms;
        if (en_shed < 3u && ms - shed_up_ms >= 400u) {
            en_shed++;
            shed_up_ms = ms;
        }
    } else if (pct > EN_SHED_DOWN) {
        shed_calm_ms = ms;                      /* between the two: it stays where it is */
    } else if (en_shed && ms - shed_calm_ms >= 20000u) {
        en_shed--;
        shed_calm_ms = ms;
    }
}

static void bell_voice(int l, int n, float vel, int32_t wait)
{
    int i, best = 0, ri;
    const int nb = SHED_NBELL[en_shed & 3u];
    float lo = 9.0f, t60, p, tone;
    bell_t *v;
    for (i = 0; i < nb; i++) {                  /* a free voice, or the quietest (the guard: of fewer) */
        float a = bell[i].on ? bell[i].amp : -1.0f;
        if (a < lo) {
            lo = a;
            best = i;
        }
    }
    v = &bell[best];
    if (v->on)
        midi(0x81, v->note, 0);
    v->on = 1;
    v->note = (uint8_t)n;
    v->loop = (uint8_t)l;
    v->cph = v->mph = 0.0f;
    v->cinc = mtof((float)n) * (1.0f / EN_SR);
    /* Phase 5b: the tone a strike takes is the Bell tone knob's, moved by the weather's brightness (the bells brighten
     * and darken with the garden, +-0.12 of the knob's range) and rounder when the garden under them is thin (to
     * -0.12: nearly alone they are the plucked, nylon sound Tomorrow's Man heard at 3:25 of the Phase 4 take, TESTING #21, not
     * the bright bell) */
    tone = fm_clampf(btone + 0.2f * cd.br - 0.12f * cd.alone, 0.0f, 1.0f);
    ri = (int)(tone * 100.0f) * 5 / 101 + LOOP_RAT[l];
    v->minc = v->cinc * RATIO[ri < 0 ? 0 : ri > 4 ? 4 : ri];
    v->amp = vel;
    t60 = 4.2f - (float)(n - 60) * 0.06f;       /* the low bells ring longer */
    t60 = fm_clampf(t60, 1.6f, 5.0f) * LOOP_DEC[l];
    v->adec = fm_expf(-6.9f / (t60 * EN_SR));
    /* Bell tone: 0 a round, nearly pure tone; 100 a long metallic clang (index in radians, as turns). Phase 5b: the
     * index falls with the note's height above G4, by half an octave of index an octave (a high note at the same
     * index is the "harsher bright bell" of TESTING #21; a string's high notes are rounder, not sharper) */
    v->idx = (0.15f + 6.5f * tone * tone) * LOOP_IDX[l] * (0.6f + 0.4f * vel) * (1.0f / FM_TWO_PI)
             * fm_exp2f(-0.5f * fm_clampf((float)(n - 67) * (1.0f / 12.0f), 0.0f, 3.0f));
    v->idec = fm_expf(-6.9f / ((0.35f + 2.6f * tone) * EN_SR));
    v->att = 0.0f;
    /* the strike: Bell tone's (~4 ms bright .. ~57 ms round, AMB-1's), and the softer the note the softer the mallet
     * (Tomorrow's Man, TESTING #20: a longer attack). Phase 5b, between the Phase 4 take's 4 ms for every note and Phase 5's
     * 20-45 ms: at Garden's tone 11 ms at the loudest velocity (0.63), 22 ms at the softest; never slower than 80 ms */
    v->arate = fm_maxf((0.0004f + 0.0056f * fm_minf(tone * 2.0f, 1.0f)) / (1.0f + 5.0f * fm_clampf(1.0f - vel, 0.0f, 1.0f)),
                       1.0f / (0.08f * EN_SR));
    if (btype == 1) {                           /* Glass: the metallic ratio, a long ring */
        v->minc = v->cinc * 3.5f;
        v->adec = fm_expf(-6.9f / (t60 * 1.5f * EN_SR));
    } else if (btype == 2) {                    /* Plink: short and dry, a blip */
        v->minc = v->cinc * 2.0f;
        v->adec = fm_expf(-6.9f / (t60 * 0.2f * EN_SR));
        v->idec = fm_expf(-6.9f / (0.15f * EN_SR));
        v->amp *= 2.6f;                         /* (a short bell carries less: up to the Chime's level) */
    } else if (btype == 3) {                    /* Wood: a marimba bar, a quick strike and decay */
        v->minc = v->cinc * 4.0f;
        v->idx *= 0.7f;
        v->adec = fm_expf(-6.9f / (t60 * 0.35f * EN_SR));
        v->idec = fm_expf(-6.9f / (0.25f * EN_SR));
        v->arate *= 3.0f;
        v->amp *= 1.8f;
    } else if (btype == 4) {                    /* Gong: an inharmonic ratio, deep and long, a slow bloom */
        v->minc = v->cinc * 1.41f;
        v->adec = fm_expf(-6.9f / (t60 * 2.2f * EN_SR));
        v->idx *= 1.2f;
    } else if (btype == 5) {                    /* Crystal: a high, pure ring, the modulator a ratio of 5.19, gently struck */
        v->minc = v->cinc * 5.19f;
        v->idx *= 0.5f;
        v->adec = fm_expf(-6.9f / (t60 * 1.8f * EN_SR));
        v->idec = fm_expf(-6.9f / (1.2f * EN_SR));
    }
    p = (LOOP_PAN[l] + 1.0f) * (FM_PI / 4.0f);
    v->gl = fm_cosf(p);
    v->gr = fm_sinf(p);
    v->wait = wait;
    en_loop_glow[l] = 1.0f;
    midi(0x91, (uint32_t)n, (uint32_t)(vel * 110.0f));
}

static void bell_ring(int l, int slot)
{
    int n = bell_note(l, loop_note[l][slot]);
    bells.quiet = 0.0f;
    float vel = (float)loop_vel[l][slot] * 0.01f;
    int32_t wait = irand(2600);                 /* a human hand: up to 60 ms late */
    bell_voice(l, n, vel, wait);
    if (n + 12 <= 103 && frand() < 0.08f + 0.24f * fm_maxf(cd.br, 0.0f))
        bell_voice(l, n + 12, vel * 0.4f, wait + 300 + irand(2000));   /* now and then, its octave */
}

/* ---------------------------------------------------------------- pulse --- */
#if EN_PULSE
static const uint8_t DRUM_ROT[EN_NDRUM] = {0, 2, 3};
static int drum_k[EN_NDRUM], drum_kb[EN_NDRUM], drum_rot[EN_NDRUM], pulse_rest;   /* rest: bars left */

static void drum_publish(int d)
{
    drum_pat[d] = en_euclid(drum_k[d], EN_DRUM_N[d], drum_rot[d]);
    en_drum_pat[d] = drum_pat[d];
}

static void drums_pattern(void)                 /* Hits: the patterns' home */
{
    int d;
    for (d = 0; d < EN_NDRUM; d++) {
        drum_kb[d] = drum_k[d] = (par[P_HITS] * DRUM_KMAX[d] + 50) / 100;
        drum_rot[d] = DRUM_ROT[d];
        drum_publish(d);
    }
}

static void drums_evolve(void)                  /* a bar: a pattern turns, or gains or loses a hit */
{
    int d = irand(EN_NDRUM), n = EN_DRUM_N[d];
    if (!drum_kb[d])
        return;
    if (frand() < 0.5f) {
        drum_rot[d] = (drum_rot[d] + (frand() < 0.5f ? 1 : n - 1)) % n;
    } else {
        int k = drum_k[d] + (frand() < 0.5f ? 1 : -1);
        if (k >= drum_kb[d] - 1 && k <= drum_kb[d] + 1 && k >= 1 && k <= n)
            drum_k[d] = k;
    }
    drum_publish(d);
}

static void drum_fire(int d, float vel)
{
    drum_t *v = &drum[d];
    v->on = 1;
    v->amp = vel;
    v->att = 0.0f;
    switch (d) {
    case 0: {                                   /* thump: the chord's root, low, falling into place */
        float f = mtof((float)(36 + chord_pcs[0]));
        v->note = DRUM_GM[0];
        v->inc = f * 2.2f * (1.0f / EN_SR);
        v->inc_end = f * (1.0f / EN_SR);
        v->adec = fm_expf(-6.9f / (0.45f * EN_SR));
        break;
    }
    case 1:                                     /* closed hat: short; it chokes the open one */
        v->note = DRUM_GM[1];
        v->adec = fm_expf(-6.9f / (0.06f * EN_SR));
        drum[2].adec = fm_expf(-6.9f / (0.02f * EN_SR));
        break;
    default:                                    /* open hat: a long, soft "tss" */
        v->note = DRUM_GM[2];
        v->adec = fm_expf(-6.9f / (hat_open * EN_SR));
        break;
    }
    en_drum_glow[d] = 1.0f;
    midi(0x99, v->note, (uint32_t)(vel * 100.0f));
    midi(0x89, v->note, 0);
}

static void drum_hit(int d, float vel, int off)  /* off: an off sixteenth, which Swing delays */
{
    int32_t w = off ? (int32_t)(swing * step_len) : 0;
    if (w > 0) {
        drum[d].wait = w;
        drum[d].pend = vel;
    } else {
        drum_fire(d, vel);
    }
}

static int32_t drums_wait(void)                 /* samples to the next hit that waits; 0: none */
{
    int32_t m = 0;
    int d;
    for (d = 0; d < EN_NDRUM; d++)
        if (drum[d].wait > 0 && (!m || drum[d].wait < m))
            m = drum[d].wait;
    return m;
}

static void drums_wait_step(int32_t k)          /* k samples went by: hits whose wait is over */
{
    int d;
    for (d = 0; d < EN_NDRUM; d++)
        if (drum[d].wait > 0 && (drum[d].wait -= k) <= 0) {
            drum[d].wait = 0;
            drum_fire(d, drum[d].pend);
        }
}
#endif

/* the grid: one call a sixteenth (MIDI clock: 6 ticks) */
static uint32_t sixteenth;
static void tick(void)
{
    int l;
#if EN_PULSE
    {
        int d;
        if ((sixteenth & 15u) == 0u) {              /* a bar: Evolve's rests and turns */
            if (pulse_rest > 0)
                pulse_rest--;
            else if (frand() < evolve * 0.015f)
                pulse_rest = 2 + irand(3);           /* a short breath: 2 to 4 bars */
            en_pulse_rest = (uint8_t)(pulse_rest > 0);
            if (frand() < evolve * 0.25f)
                drums_evolve();
        }
        for (d = 0; d < EN_NDRUM; d++) {
            if (drum_pat[d] >> drum_pos[d] & 1u && !pulse_rest)
                drum_hit(d, (drum_pos[d] == 0 ? 0.95f : 0.6f) + 0.25f * frand(), (int)(sixteenth & 1u));
            en_drum_pos[d] = drum_pos[d];
            if (++drum_pos[d] >= EN_DRUM_N[d])
                drum_pos[d] = 0;
        }
    }
#endif
    /* the loops step every three quarter notes, each on its own sixteenth of the twelve (AMB-1: every quarter, all
     * four together). At Garden's tempo a slot is 2.5 s and the cells are 33, 43, 58 and 73 s long (AMB-1: 11-24 s;
     * DESIGN section 3 asks for 20 s - 3 min), so a bell has room to ring out and its tail to fade before the next
     * (Tomorrow's Man, TESTING #20: 35 and more in three minutes, "essentially constant"), and no two loops strike on one
     * beat (the loud hits of TESTING #14 were two or three loops together) */
    for (l = 0; l < EN_NLOOP; l++) {
        if (sixteenth % LOOP_STEP == LOOP_OFS[l]) {
            int s = loop_pos[l];
            if (loop_note[l][s] >= 0 && !loop_rest[l] && !bells.rest)
                bell_ring(l, s);
            en_loop_pos[l] = (uint8_t)s;
            if (++loop_pos[l] >= EN_LOOP_LEN[l]) {
                loop_pos[l] = 0;
                loop_pass(l);
            }
        }
    }
    if ((sixteenth & 15u) == 0u && change_pending && !en_frozen) {   /* chords move on the bar */
        change_pending = 0;
        chord_change();
    }
    sixteenth++;
    en_bars = sixteenth;
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in FoMni): a predelay, four input diffusers,
 * then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an
 * allpass and a delay, feeding the other half. Stereo out from seven taps a side. The paper's lengths
 * are for 29761 Hz; DS() scales them to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 1323                                   /* 30 ms */
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] EN_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_decay_t = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;
static float pl_w = 0.0001f;                    /* the tank's modulation rate, radians a sample: 0.70 Hz (Dattorro's ~1 Hz at 29.8 kHz) */
/* Phase 5b: the plate wanders (Tomorrow's Man, TESTING #18: "an LFO that never lets it get metallic but lets it breathe and
 * morph"; of the four clips of TESTING #23 he chose F, "definitely"). Each of the two modulated allpasses has its own
 * oscillator whose rate slides between 0.25 and 1.3 Hz, a new target every 2-7 s, so the ripple a held note gets is
 * never periodic (AMB-1: one oscillator at 0.70 Hz for both, a steady ripple of 1.7 dB rms on a held pad note with a
 * sharp line at 0.7 Hz; wandering 2.1 dB and no line). Its random numbers are its own: the music's generator is not
 * touched. The host can still set a fixed rate for an A/B (en_host_plate_mod). */
static float pl_w2 = 0.0001f, lfo2_s, lfo2_c = 1.0f, pl_wt[2] = {0.0001f, 0.0001f};
static uint32_t pl_wleft[2], pl_rng = 0x1234567u;
static uint8_t pl_wander = 1;
static float pl_wlo = 0.25f, pl_wspan = 1.05f;   /* the wander's rates, Hz: from, and how far above */
static float pl_exc_k = 1.0f;                  /* the tank's modulation depth, 1 = Dattorro's 16 samples (host A/B, TESTING #18:
                                                * on a held pad note it is a 0.7 Hz level ripple of several dB; found 2026-11-09) */

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, mr, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.6f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + pl_w * lfo_c, c = lfo_c - pl_w * s;
        lfo_s = s;
        lfo_c = c;
        mr = c;                                 /* the right half: the same oscillator, a quarter turn on */
    }
    if (pl_wander) {                            /* the right half on its own oscillator */
        float s = lfo2_s + pl_w2 * lfo2_c, c = lfo2_c - pl_w2 * s;
        lfo2_s = s;
        lfo2_c = c;
        mr = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * pl_exc_k * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.45f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * pl_exc_k * mr, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.45f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------- echo: tape and Echoplex --- */
/* After Discreet Music's chain: an Echoplex (a short, smearing echo, ~160 ms, fed back) and a tape loop
 * (long: up to ~5.6 s, synced to the tempo), each repeat a little darker and thinner, with a little
 * wow. The tape runs at a quarter rate (11 kHz, 16-bit, mono): it loses its highs anyway. Two heads:
 * the first plays the loop's length, the second a part of it (3/4 at first). With Motion the echo
 * evolves: each head wanders across the field, the feedback leans now on one head, now on the other
 * (the repeats fall in changing patterns, left and right), and now and then the second head slides to
 * another part of the loop, as a head moved by hand (the repeats bend a little in pitch on the way).
 * The Echoplex has a second tap, for the right. */
#define TAPE_N 62000                             /* 5.6 s at 11.025 kHz */
#define TAPE_SR 11025.0f
static int16_t tape[TAPE_N] EN_POOL;
static int tape_w, tape_ph;
static float tape_acc, tape_lp, tape_hp, tape_p1, tape_p2, tape_c1, tape_c2;
static const float HEAD2[5] = {0.75f, 0.6667f, 0.625f, 0.5f, 0.375f};   /* the second head's parts */
static float head2 = 0.75f, head2_t = 0.75f, head2_v;   /* its place (of the loop), target, speed */
static float fb_lean;                            /* 0..1: how much of the feedback is the second head's */
static float hg[4] = {1.0f, 0.0f, 0.0f, 1.0f};   /* the heads' pan gains: 1 left, 1 right, 2 left, 2 right */
static uint32_t head_left;                       /* samples until the second head may move */
#define PLEX_N 7100                              /* 160 ms at 44.1 kHz */
#define PLEX_TAP 2700                            /* the right's tap: ~100 ms */
static int16_t plex[PLEX_N];
static int plex_w;
static float plex_lp, plex_lp2;
static float echo_fb_eff = 0.55f;                /* the feedback as played: Size, the space walker, the governor */

static inline float tape_read(float d)           /* d quarter-rate samples ago */
{
    int k = (int)d, i0, i1;
    float f = d - (float)k, a, c;
    i0 = tape_w - k;
    i1 = i0 - 1;
    if (i0 < 0)
        i0 += TAPE_N;
    if (i1 < 0)
        i1 += TAPE_N;
    a = (float)tape[i0];
    c = (float)tape[i1];
    return (a + (c - a) * f) * (1.0f / 12000.0f);
}
static inline int16_t s16(float x)
{
    x *= 12000.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}

static void echo_time(void)
{
    int i = par[P_ETIME];
    float beat, t;
    if (par[P_TEMPO] <= 0)
        return;                                  /* not yet (en_init applies in order) */
    beat = 60.0f / (float)par[P_TEMPO];
    t = ETIME_BEATS[i] * beat;
    while (t * TAPE_SR > (float)(TAPE_N - 8) && i > 0)
        t = ETIME_BEATS[--i] * beat;             /* too long for the tape: the next shorter */
    if (t * TAPE_SR > (float)(TAPE_N - 8))
        t = (float)(TAPE_N - 8) / TAPE_SR;
    echo_len_t = t * TAPE_SR;
    if (echo_len <= 0.0f)
        echo_len = echo_len_t;
}

/* once a chunk: where the heads sit in the field, how the feedback leans, the second head's slide */
static void heads_step(uint32_t n)
{
    float p, want, vmax;
    int i;
    for (i = 0; i < 2; i++) {
        p = fm_clampf((i ? 0.7f : -0.7f) + 0.9f * motion * mod[6 + i].v, -1.0f, 1.0f);
        p = (p + 1.0f) * (FM_PI / 4.0f);
        hg[2 * i] = fm_cosf(p);
        hg[2 * i + 1] = fm_sinf(p);
    }
    fb_lean = 0.25f * motion * (1.0f + mod[8].v);
    if (head_left <= n) {
        head_left = (uint32_t)((20.0f + 40.0f * frand()) * EN_SR);
        if (en_playing && !en_frozen && frand() < motion)
            head2_t = HEAD2[(int)(frand() * 4.999f)];
    } else {
        head_left -= n;
    }
    /* its speed (a part of the loop per tape sample): at most a bend of ~14 cents, easing in and out */
    vmax = 0.008f / fm_maxf(echo_len, 100.0f);
    want = fm_clampf((head2_t - head2) * (1.0f / (2.0f * TAPE_SR)), -vmax, vmax);
    head2_v += (want - head2_v) * fm_minf((float)n * (1.0f / EN_SR), 1.0f);
}

/* one sample of both: the input (a mono send) to the tape's heads (el, er) and the Echoplex (xl, xr) */
static inline void echoes(float in, float *el, float *er, float *xl, float *xr)
{
    float y = (float)plex[plex_w] * (1.0f / 12000.0f);         /* the Echoplex: 160 ms, fed back, darker */
    int t = plex_w - PLEX_TAP;
    plex_lp += 0.35f * (y - plex_lp);
    plex_lp2 += 0.35f * ((float)plex[t < 0 ? t + PLEX_N : t] * (1.0f / 12000.0f) - plex_lp2);
    plex[plex_w] = s16(in + 0.55f * plex_lp);
    if (++plex_w >= PLEX_N)
        plex_w = 0;
    *xl = plex_lp;
    *xr = plex_lp2;
    tape_acc += in + 0.3f * plex_lp;
    if (++tape_ph >= 4) {                        /* the tape, a quarter of the time */
        float d, h1, h2, fb;
        tape_ph = 0;
        echo_len += (echo_len_t - echo_len) * 0.001f;
        head2 += head2_v;
        d = echo_len * (1.0f + 0.003f * motion * mod[5].v);   /* wow */
        if (d > (float)(TAPE_N - 4))
            d = (float)(TAPE_N - 4);
        h1 = tape_read(d);
        h2 = tape_read(d * head2);
        tape_lp += 0.55f * (h1 + fb_lean * (h2 - h1) - tape_lp);   /* each time round: darker, thinner */
        tape_hp += 0.03f * (tape_lp - tape_hp);
        fb = tape_lp - tape_hp;
        if (++tape_w >= TAPE_N)
            tape_w = 0;
        tape[tape_w] = s16(tape_acc * 0.25f + echo_fb_eff * fb);
        tape_acc = 0.0f;
        tape_p1 = tape_c1;
        tape_p2 = tape_c2;
        tape_c1 = h1;
        tape_c2 = h2;
    }
    {   /* between the tape's samples: a line from the last to the next; then to the field */
        float f = (float)tape_ph * 0.25f;
        float h1 = tape_p1 + (tape_c1 - tape_p1) * f, h2 = tape_p2 + (tape_c2 - tape_p2) * f;
        *el = hg[0] * h1 + hg[2] * h2;
        *er = hg[1] * h1 + hg[3] * h2;
    }
}

/* ------------------------------------------------------------ parameters --- */
static int pulse;
static uint32_t ext_age;

static void tempo_set(void) { step_len = EN_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

static void swell_set(void)                     /* Swell: how far each voice's level walkers stray */
{
    int i, k;
    float sd = 0.7f + 0.023f * swell;           /* dB per walker: 0.7 .. 3.0 (three summed); Garden 2.08, AMB-1's 1.8 */
    for (i = 0; i < EN_NPAD; i++)
        for (k = 0; k < 3; k++)
            pad[i].lw[k].sd = sd;
    for (k = 0; k < 2; k++) {
        bass.lw[k].sd = 0.56f * sd;             /* two summed: the bass 1.2 dB each at Garden, the bells 1.4 */
        bells.lw[k].sd = 0.65f * sd;
    }
}

/* Phase 6: the main page's Mood and Space. Each is an offset (-50..+50) around what the piece's own knobs say, so at
 * 0 the piece is exactly its recipe (and a render is the same samples as before there were such knobs).
 * Mood, dark .. bright: the pad's Bright moves with it, 0.6 of a point a step (+-30 points: the filter about an octave
 * either way, the vowel and the FM index with it), and the mode walker leans 0.03 of a mode step a step (+-1.5 steps:
 * fully bright, a piece at home in Dorian spends its hours around Mixolydian and Ionian; fully dark, Aeolian and
 * Phrygian). And the center of the weather's brightness (cd.br: the pad's filter, vowel and FM index, the bells'
 * tone and how often one rings an octave up) moves with it, MOOD_BR_DOWN .. MOOD_BR_UP. The pad is close to a sine,
 * so its tone has only so far to go: on a held state (the same chord and voices; out/phase6's mood-sweep-held) the
 * pad alone reads 338 Hz (rms frequency) at Mood 0, 312 at -50 and 425 at +50; with Bright's offset alone and no
 * move of cd.br the bright side did not register. The walkers keep moving around the new center (DESIGN section 9).
 * Space, near .. far: toward near, Reverb and Echo fall 0.6 of a point a step and Size (the plate's decay, the echo's
 * feedback) 0.4; toward far, Echo rises 0.6 but Reverb only 0.3 and Size 0.2. The far side was first as long as the
 * near: at +50 Garden's Reverb stood at 95 and its Size at 100, and on a held state the output rose 8.2 dB (11 over
 * the target), with one note ringing in the plate within 3 dB of the whole target for 3.8 of 5 minutes: the standing
 * tone of TESTING #25, made by a knob (measured 2026-11-10, out/phase6's space-sweep-held). */
#define MOOD_BR_UP 0.50f                        /* cd.br's center at Mood +50 */
#define MOOD_BR_DOWN 0.40f                      /* and how far under at -50 */
static void mood_set(void)
{
    bright = fm_clampf((float)par[P_BRIGHT] + 0.6f * (float)par[P_MOOD], 0.0f, 100.0f);
    mode_tilt = ((float)par[P_BRIGHT] - 50.0f) * 0.02f + 0.03f * (float)par[P_MOOD];
    mood_br = (float)par[P_MOOD] * (par[P_MOOD] > 0 ? MOOD_BR_UP : MOOD_BR_DOWN) * 0.02f;
}
#define SPACE_FAR_REV 0.3f                      /* Reverb points a step of Space above 0 */
#define SPACE_FAR_SIZE 0.2f                     /* and Size points */
static void space_set(void)
{
    const float s = (float)par[P_SPACE];
    const float e = fm_clampf((float)par[P_ECHO] + 0.6f * s, 0.0f, 100.0f);
    const float r = fm_clampf((float)par[P_REVERB] + (s > 0.0f ? SPACE_FAR_REV : 0.6f) * s, 0.0f, 100.0f);
    const float z = fm_clampf((float)par[P_SIZE] + (s > 0.0f ? SPACE_FAR_SIZE : 0.4f) * s, 0.0f, 100.0f);
    echo_send = e * e * 0.0001f;
    rev_send = r * r * 0.0001f;
    pl_decay_t = 0.6f + 0.375f * z * 0.01f;            /* to 0.975: a long hall */
    echo_fb = 0.5f + 0.32f * z * 0.01f;
}

static void apply(int p, int v)
{
    const en_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_PAD: pad_vol = (float)(v * v) * 0.0001f; break;
    case P_BELLS:                                /* the bells' amount: their level, and (Phase 5b) their count with it */
        bell_vol = (float)(v * v) * 0.0001f;
        bell_amt = (float)v * (1.0f / 65.0f);
        bell_amt *= fm_sqrtf(bell_amt);
        break;
    case P_BASS: bass_vol = (float)(v * v) * 0.0001f; break;
    case P_PULSE: pulse_vol = (float)(v * v) * 0.0001f; break;
#if EN_PULSE
    case P_HITS: drums_pattern(); break;
    case P_SWING: swing = (float)v * 0.005f; break;      /* the off sixteenths up to half a step late */
    case P_HATS:
        hat_f = 0.55f + 0.0070f * (float)v;               /* the high-pass: ~4 kHz dark .. ~10 kHz bright */
        hat_open = 0.6f - 0.0035f * (float)v;             /* the open hat: long and soft .. short and crisp */
        break;
#endif
    case P_BASSOCT:
        if (nsteps)
            bass_to(bass.midi_note % 12);
        break;
    case P_GLIDE: glide_k = 1.0f / ((0.02f + 0.012f * (float)v) * EN_SR); break;   /* 20 ms .. 1.2 s */
    case P_DRIFT: cd.speed = drift_speed(v); break;
    case P_ETIME: echo_time(); break;
    case P_ECHO: case P_REVERB: case P_SIZE: case P_SPACE: space_set(); break;
    case P_BRIGHT: case P_MOOD: mood_set(); break;
    case P_SWELL:
        swell = (float)v;
        swell_set();
        break;
    case P_EVOLVE: evolve = (float)v * 0.01f; break;
    case P_BTONE: btone = (float)v * 0.01f; break;
    case P_BELLTYPE: btype = v; break;
    case P_BASSTYPE: bass_type = v; break;
    case P_PADTONE:                              /* the landscape: the voices cross over as they did for a new key */
        choir_on = v == 2;                       /* the Choir keeps the Rhodes landscape's cursor (the pad bus's filter) */
        land_now = choir_on ? 0 : pad_land_of(v);
        cursor_blend(1, 0.0f);
        {
            const int lo = pad_lo;
            pad_window();
            if (lo != pad_lo)
                pad_retarget();                  /* the chord's tones in the new window */
        }
        pad_handoff();
        break;
    case P_MOTION: motion = (float)v * 0.01f; break;
    case P_BOCT:                                 /* let it be heard now: a bell from the first loop */
        if (en_playing && nsteps) {
            int l, k;
            for (l = 0; l < EN_NLOOP; l++)
                for (k = 0; k < EN_LOOP_LEN[l]; k++)
                    if (loop_note[l][k] >= 0) {
                        bell_ring(l, k);
                        return;
                    }
        }
        break;
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    case P_TEMPO:
        tempo_set();
        echo_time();
        break;
    case P_ROOT:
        if (nsteps) {
            field_home();
            if (deg >= nsteps)
                deg = 0;
            pad_retarget();
            pad_handoff();
            bass_to(chord_pcs[0]);
            ground_to(chord_pcs[0]);
        }
        break;
    case P_MIDI:
        if (!v)
            en_midi_out(0xB0, 123, 0);
        break;
    default: break;
    }
}

static void pool_set(uint32_t p)
{
    home_pool = (p | 1u) & 0xFFFu;
    field_home();
    if (deg >= nsteps)
        deg = 0;
    pad_retarget();
    pad_handoff();
    bass_to(chord_pcs[0]);
    ground_to(chord_pcs[0]);
}

/* ------------------------------------------------- TIDE: the conductor --- */
/* Phase 5c: the ebb (Tomorrow's Man, TESTING #25: "the excerpt on the whole stays the same loudness"; Eno's 1/1 "sometimes
 * made you feel as if it was over before a swell or motif came back in. This never relents to that extent"). The
 * layers' rests are independent and the governor levels what is left, so the garden as a whole never went out. Now,
 * every so often, it does: every layer's gain sinks together at EBB_FALL, the bells go to a lull,
 * the garden stays at the bottom for EBB_HOLD, and comes back at EBB_RISE, two and a half times quicker than it went
 * (Tomorrow's Man, TESTING #15: back up more quickly than it fades down). Not silence (his "I always want something burbling
 * away", #17): the foundation's voice or the bass still sounds, far off, and the bottom is a place, EBB_DEPTH under
 * the Density target (the Definition of done's line is 20 dB on the 60 s loudness), not a drop. The governor's
 * target sinks with it, so nothing is lifted to fill it. This is DESIGN section 8's composed silence, at the pace
 * section 6's rate limits allow. Gaps in Drift's time (Hold stops the tide too), the bottom in real seconds. */
#define EBB_GAP ((5.0f + 11.0f * frand()) * 60.0f)    /* Drift seconds between ebbs: 10-32 minutes at Garden's half speed */
#define EBB_DEPTH (11.0f + 5.0f * frand())            /* dB under the Density target: where this ebb's bottom lies (3 s meter).
                                                       * (The one dip Tomorrow's Man saw in the 5b take's waveform, 1:40-1:55, was 4-7 dB
                                                       * under, and "this never relents": a take with bottoms at 9-14 read 9-15 dB
                                                       * under on the 60 s loudness, so the servo holds within a dB) */
#define EBB_MOST 17.0f                                /* dB: the most an ebb takes off the gains */
#define EBB_HOLD (45.0f + 120.0f * frand())           /* seconds at the bottom */
#define EBB_FALL 0.1f                                 /* dB/s: 1-2.5 minutes going out */
#define EBB_RISE 0.25f                                /* dB/s: 35-65 s coming back; with a voice's 0.4 and the gains' 0.16
                                                       * it sits under the swell limit's 1.0 (a first take at 0.3: the pad's
                                                       * p99.9 rate 0.93 dB/s, from 0.86) */

static void conductor_reset(void)               /* a new piece: the weather and seasons start afresh */
{
    int i, k;
    walker_set(&cd.s_speed, 0.0f, 2.3f * 3600.0f, 0.35f, -1.0f, 1.0f);       /* log2 of the weather's pace */
    walker_set(&cd.s_density, 0.0f, 3.1f * 3600.0f, 0.2f, -0.5f, 0.5f);
    walker_set(&cd.s_bright, 0.0f, 1.7f * 3600.0f, 0.12f, -0.3f, 0.3f);
    walker_set(&cd.bright, 0.0f, 11.0f * 60.0f, 0.18f, -0.5f, 0.5f);
    walker_set(&cd.density, 0.0f, 17.0f * 60.0f, 0.3f, -0.9f, 0.9f);
    walker_set(&cd.reg, 0.0f, 23.0f * 60.0f, 2.5f, -6.0f, 6.0f);
    walker_set(&cd.space, 0.0f, 13.0f * 60.0f, 0.4f, -1.0f, 1.0f);
    walker_set(&cd.chord, 0.0f, 8.0f * 60.0f, 0.5f, -1.2f, 1.2f);
    walker_set(&cd.pres[L_PAD], 0.5f, 9.0f * 60.0f, 0.3f, 0.0f, 1.0f);
    walker_set(&cd.pres[L_BELLS], 0.5f, 14.0f * 60.0f, 0.3f, 0.0f, 1.0f);   /* rests below 0.3: about a quarter of the time */
    walker_set(&cd.pres[L_BASS], 0.5f, 19.0f * 60.0f, 0.3f, 0.0f, 1.0f);
    walker_set(&cd.pres[L_GROUND], 0.3f, 29.0f * 60.0f, 0.3f, 0.0f, 1.0f);   /* enters above 0.65: about 12% of the time */
    walker_set(&cd.bact, 0.0f, 6.0f * 60.0f, 0.8f, -1.6f, 1.2f);            /* the bells' passages and lulls (loop_target) */
    cd.alone = 0.0f;
    for (k = 0; k < L_GROUND; k++)              /* a piece begins with its layers present: the rests come later */
        cd.pres[k].x = fm_clampf(cd.pres[k].center + 0.5f * fm_fabsf(cd.pres[k].x - cd.pres[k].center), 0.0f, 1.0f);
    cd.wspeed = cd.speed;
    cd.dens = cd.br = 0.0f;
    for (k = 0; k < NLAYERS; k++) {
        cd.e20[k] = 0.0f;
        cd.bias[k] = cd.gdb[k] = 0.0f;
        cd.g[k] = 1.0f;
        cd.share[k] = 0.0f;
        cd.lead[k] = 0.0f;
    }
    cd.m3 = cd.m20 = cd.m60 = 0.0f;
    cd.trim = cd.ff = 0.0f;
    cd.under = cd.silent = 0.0f;
    for (k = 0; k < NLAYERS; k++) {
        cd.ff_e0[k] = 0.0f;
        cd.ff_db0[k] = 0.0f;
    }
    cd.pad_thin = 0;
    cd.floor_hold = 0.0f;
    cd.ebb = cd.ebb20 = cd.ebb3 = 0.0f;
    cd.ebbing = 0;
    cd.ebb_t = 0.0f;
    cd.ebb_next = EBB_GAP;
    cd.ebb_depth = cd.ebb_hold = 0.0f;
    cd.pad_target = 0;
    cd.since_entry = 100.0f;
    cd.entry_gap = 10.0f;
    cd.pad_rest = 0.0f;
    cd.pad_rest_max = 240.0f + 360.0f * frand();
    cd.turn = cd.away = cd.recess = cd.dip = cd.dip_depth = 0.0f;
    cd.turn_max = PAD_TURN;
    cd.recessing = 0;
    cd.e5 = cd.expo = cd.hold = cd.hold5 = cd.exposed = cd.called = 0.0f;
    cd.hold_up = 0;
    for (i = 0; i < EN_NPAD; i++) {
        pad_t *v = &pad[i];
        /* deeper swings at the same speed (Tomorrow's Man, TESTING #11): sd x1.25, tau x1.56 keeps dB/s where it was */
        walker_set(&v->lw[0], 0.0f, 39.0f * VOICE_K[i], 2.2f, -8.0f, 8.0f);
        walker_set(&v->lw[1], 0.0f, 73.0f * VOICE_K[i], 2.2f, -8.0f, 8.0f);
        walker_set(&v->lw[2], 0.0f, 140.0f * VOICE_K[i], 2.2f, -8.0f, 8.0f);
        walker_set(&v->bw, 0.0f, 60.0f * VOICE_K[i], 0.08f, -0.2f, 0.2f);
        v->rest = 0.0f;
    }
    walker_set(&bass.lw[0], 0.0f, 80.0f, 1.2f, -8.0f, 8.0f);      /* the bass breathes deeper and slower (TESTING #11):
                                                                   * Phase 3 1.0 dB at 40/95 s; dB/s is kept where it was */
    walker_set(&bass.lw[1], 0.0f, 190.0f, 1.2f, -8.0f, 8.0f);
    bass.rest_t = bells.rest_t = 0.0f;
    bass.gap = bells.gap = 0.0f;
    bass.stay_t = bells.stay_t = 0.0f;
    bass.min_stay = 300.0f + 420.0f * frand();   /* the first stay: 5-12 weather minutes before a rest may begin */
    bells.min_stay = 240.0f + 360.0f * frand();
    bells.busy = 0.0f;
    bells.busy_max = 150.0f + 240.0f * frand();
    walker_set(&bells.lw[0], 0.0f, 55.0f, 1.5f, -8.0f, 8.0f);     /* the bells' level breathes too */
    walker_set(&bells.lw[1], 0.0f, 130.0f, 1.5f, -8.0f, 8.0f);
    bells.rest = 0;
    bells.ldb = -60.0f;
    bells.lin = 0.0f;
    swell_set();
    ground_gap = 120.0f;
    ground_stay = 0.0f;
    ground_due = 0.0f;
    ground_due_at = (40.0f + 30.0f * frand()) * 60.0f;
    reg_now = 0;
    /* the timbre cursor (DESIGN section 4): a season's region, the weather's drift about it; from the center */
    choir_on = par[P_PADTONE] == 2;
    land_now = choir_on ? 0 : pad_land_of(par[P_PADTONE]);
    pad_window();
    /* (the first 8 h render, 2026-11-09: a season of sd 0.22 within +-0.4 parked in a corner for hours and the weather
     * of sd 0.18 could not leave it, 3 anchors in 8 h; now the season shifts the region a little and the weather
     * does the visiting) */
    /* (Phase 5: the season smaller again, 0.11 within +-0.18 (was 0.15 within +-0.25), the weather wider, 0.27 within
     * +-0.5 (was 0.25 within +-0.45): at Garden's half speed a season lasts five hours, and one 8 h take sat in the
     * Oh corner with Glass and Ah out of the weather's reach, 4 anchors) */
    walker_set(&tcur.sx, 0.0f, 2.5f * 3600.0f, 0.11f, -0.18f, 0.18f);
    walker_set(&tcur.sy, 0.0f, 2.9f * 3600.0f, 0.11f, -0.18f, 0.18f);
    walker_set(&tcur.wx, 0.0f, 20.0f * 60.0f, 0.27f, -0.5f, 0.5f);
    walker_set(&tcur.wy, 0.0f, 25.0f * 60.0f, 0.27f, -0.5f, 0.5f);
    tcur.x = fm_clampf(0.5f + tcur.sx.x + tcur.wx.x, 0.0f, 1.0f);
    tcur.y = fm_clampf(0.5f + tcur.sy.x + tcur.wy.x, 0.0f, 1.0f);
    cursor_blend(1, 0.0f);
    /* the harmonic field (Phase 5): from home; the set's walker (a mode step or three either way) and the root's (a
     * fifth or two), seasons both; the first step no sooner than 17-30 season minutes in */
    walker_set(&fld.mode, 0.0f, FLD_MODE_TAU, FLD_MODE_SD, -3.0f, 3.0f);
    walker_set(&fld.root, 0.0f, FLD_ROOT_TAU, FLD_ROOT_SD, -2.0f, 2.0f);
    fld.dwell = FLD_DWELL;
    field_home();
}

/* The pad's turn (2026-11-13, after v1; Tomorrow's Man: "the way the pad can take over sometimes for a very long time is really
 * the only thing left that gets distracting", it should not drone on "for more than a minute at a time", and the
 * pieces should open up, with "more breathing room (i.e., lower amplitude/volume)"). Measured on the v1 take (Samsara,
 * seed 1107, 8 h): a voice is singing 90% of the time, in stretches of up to 27 minutes, its rests 23 s at the median,
 * the loudest voice within 20 dB of full for up to 23 minutes at a stretch; and the six longest stretches with the pad
 * over 70% of the energy (10-15 minutes each) are the bass's six longest rests, when the foundation rule keeps a voice
 * up and hands it on voice to voice. So, as the bells' passages and the whole garden's ebbs are, the pad's presence
 * is bounded by a timer: once a voice has been up (within 15 dB of full), or the pad has led from lower down, for
 * PAD_TURN real seconds, the pad takes a recess: the layer's own ebb. cd.dip (dB on the pad's gain, its own slews,
 * as cd.ebb is on every layer's) falls at PAD_DIP_FALL to PAD_DIP under where the pad was (less when the pad is the
 * one sustained layer: something always burbles), stays PAD_RECESS seconds, and returns at PAD_DIP_RISE; then a new
 * turn is drawn. At the bottom the voice nearest the end of its stay is let go, so what returns is not quite what
 * left. The voices live on underneath, so the foundation and the hand-over are as they were. The bass's rest is
 * bounded by it: when the bass leaves, the pad is given a fresh turn of PAD_CARRY to carry the garden alone; at
 * its end the bass is called back, and once it is up the pad takes its recess. The bass does not begin a rest
 * during a recess.
 * (A first build made the recess a rest: every voice out, the bass called back if it was resting. Two 8 h takes: the
 * bass never rested again (0.2-2% of the time; 15% in v1) and led for 44 and 78 minutes at a stretch, the pad's share
 * fell from 48% to 13-23%, and a pad that returns from nothing each time moved its level faster than the swell limit.
 * One takeover for another.) Real seconds: a turn is heard in real time. At Hold nothing of it moves. */
#ifdef EN_HOST
void en_host_pad_turn(float lo, float hi, float rlo, float rhi)   /* an A/B; lo <= 0: no turns (v1's pad) */
{
    pad_turn_lo = lo <= 0.0f ? 1e9f : lo;
    pad_turn_span = fm_maxf(hi - lo, 0.0f);
    pad_recess_lo = rlo;
    pad_recess_span = fm_maxf(rhi - rlo, 0.0f);
    cd.turn_max = PAD_TURN;
}
#endif

/* The pad's company (2026-11-14, after v1; Tomorrow's Man, TESTING #52 to #55: a pad tone that lingers "alarms", under twelve
 * voices and an octave apart; with only the pad's level moved (#55) "the best balance" was the pad 14 dB down, level
 * with the rest to the ear, and the same voice left as alone at another moment alarmed too). Measured at his moment
 * (0:28 of take D): one voice 16.5 dB over everything else, A-weighted, the bells in a rest of their own for seven
 * minutes; the governor saw nothing wrong, since by its meter (K-weighted) the bass held 30-45% of the energy. To the
 * ear a bass at 55-110 Hz is 18-28 dB less than its meter reading and a pad voice at 175-260 Hz 8-12 dB less: the
 * bass is hardly company. So the pad's level is weighed against its company as the ear weighs them (ear_w: the
 * A-curve at each layer's note), and
 *  - the pad's gain is held (cd.hold, its own slews, as the recess's cd.dip) so the pad stands at most pc_line dB over
 *    its company, by at most pc_most dB;
 *  - a voice that is up and would stand 3 dB or more over the line for pc_call seconds calls the bells: a rest of
 *    theirs ends, a lull becomes a passage, and they come back at 0.8 dB/s (struck notes: no swell). His word for the
 *    pad held down with nothing else there: "a static fade out". Then four minutes before the next call.
 * The hold follows the level the pad would have without it (the meter less the hold as that meter hears it), so it
 * does not chase itself. Real seconds. At Hold nothing of it moves. */
static float ear_w(float note)                  /* the A-curve at a MIDI note, as a weight on energy */
{
    static const float A[9] = {-55.8f, -38.7f, -25.6f, -15.6f, -8.3f, -3.0f, 0.1f, 1.2f, 0.9f};   /* dB at C0, C1 ... C8 */
    const float x = fm_clampf((note - 12.0f) * (1.0f / 12.0f), 0.0f, 7.999f);
    const int i = (int)x;
    const float g = fm_db2lin(A[i] + (A[i + 1] - A[i]) * (x - (float)i));
    return g * g;
}
#define EAR_BELLS 0.63f                         /* the bells, D4 to G5 and their partials: 2 dB off */
#ifdef EN_HOST
void en_host_pad_company(float line, float most, float call)   /* an A/B; most 0 and call 0: no rule (the pad of 2026-11-13) */
{
    pc_line = line;
    pc_most = fm_maxf(most, 0.0f);
    pc_call = fm_maxf(call, 0.0f);
}
#endif

static int pad_count(void)                      /* voices singing (not dissolving) */
{
    int i, c = 0;
    for (i = 0; i < EN_NPAD; i++)
        c += pad[i].st == PV_ON;
    return c;
}

/* the governor (DESIGN section 5): each layer's energy over the last step, the plate's and the echoes'
 * returns shared out in proportion to the sends, smoothed; the mix against the Density target; each
 * layer's share against the cap. It moves a trim (everything) and a bias per layer, slowly. */
static const float SEND_R[NLAYERS] = {1.08f, 0.98f, 0.0064f, 0.0225f};  /* reverb send energy per dry energy */
static const float SEND_E[NLAYERS] = {0.12f, 1.45f, 0.0f, 0.0f};        /* echo send energy per dry energy */
static const float K_W[NLAYERS] = {1.0f, 1.6f, 0.45f, 0.3f};            /* about a loudness meter's K-weighting at each
                                                                         * layer's register: bells +2 dB, bass -3.5 (its sub-octave
                                                                         * at 33 Hz is -10 dB to the meter), Ground -5. Phase 3 had
                                                                         * the bass at -1: when it led, the mix read 2-3 dB louder
                                                                         * here than to a LUFS meter (the 2026-11-08 Garden render) */
static void governor_step(float dt)
{
    float dry[NLAYERS], e[NLAYERS], plate, echo, mix, sa = 0.0f, se = 0.0f, tot = 0.0f, err, cap, company;
    int k;
    const float inv = cd.frames ? 1.0f / (2.0f * (float)cd.frames) : 0.0f;
    for (k = 0; k < NLAYERS; k++) {
        dry[k] = cd.e_dry[k] * inv;
        cd.e_dry[k] = 0.0f;
    }
#ifdef EN_HOST
    if (en_host_meter_dry)
        en_host_meter_dry(dry, NLAYERS);     /* a whole performance replayed under another pad voice (the pad's life):
                                              * the layers' own energies too, recorded or replaced */
#endif
    for (k = 0; k < NLAYERS; k++) {
        sa += SEND_R[k] * dry[k];
        se += SEND_E[k] * dry[k];
    }
    plate = cd.e_plate * inv;
    echo = cd.e_echo * inv;
#ifdef EN_HOST
    if (en_host_meter_hook)
        en_host_meter_hook(&plate, &echo);   /* the mix pass records what its plate and echoes returned; a solo pass
                                              * replays it, so the governor hears the same garden and the passes stay
                                              * in step (the dry energies are the same by construction) */
#endif
    cd.e_plate = cd.e_echo = 0.0f;
    cd.frames = 0;
    mix = plate + echo;
    mix = 0.0f;
    for (k = 0; k < NLAYERS; k++) {
        e[k] = dry[k] + (sa > 0.0f ? plate * SEND_R[k] * dry[k] / sa : 0.0f) + (se > 0.0f ? echo * SEND_E[k] * dry[k] / se : 0.0f);
        mix += K_W[k] * e[k];
        cd.e20[k] += (e[k] - cd.e20[k]) * fm_minf(dt * (1.0f / 20.0f), 1.0f);
        tot += cd.e20[k];
    }
    cd.m3 += (mix - cd.m3) * fm_minf(dt * (1.0f / 3.0f), 1.0f);
    cd.m20 += (mix - cd.m20) * fm_minf(dt * (1.0f / 20.0f), 1.0f);
    cd.m60 += (mix - cd.m60) * fm_minf(dt * (1.0f / 60.0f), 1.0f);
    cd.level20 = 3.0103f * fm_log2f(fm_maxf(cd.m20, 1e-9f));
    cd.level60 = 3.0103f * fm_log2f(fm_maxf(cd.m60, 1e-9f));
    /* -22 dB at Density 40 (Phase 3: -18, AMB-1's Garden sat at -16 with the pad at 78%). Lowered 4 dB on 2026-11-08: with
     * the bells at 0.5 and sparse and the layers resting, the trim sat at +7 (p50) and hit its +9 rail whenever the
     * garden thinned; now it centers near +3 with reach both ways. MASTER is the volume. */
    cd.target = -22.0f + 0.15f * (float)(par[P_DENSITY] - 40);
    if (!en_playing || cd.speed <= 0.0f)        /* resting, or Drift at Hold: the governor's hands hold too */
        return;
    /* loudness: the 20 s meter against the target; inside the band (±1.5 dB) nothing; outside, the trim moves, at most
     * 0.06 dB/s up and 0.1 down (2026-11-08: Phase 3 moved 0.1 dB/s on a ±1 dB band and chased the meter's bell
     * strikes, ±3 dB in minutes; the 60 s one-pole was tried and lags a layer's leaving by minutes) */
    /* Phase 5c: the ebb (the note above conductor_reset). Its own slews, outside the gains' 0.16 dB/s; the meter is
     * compared with the target less the ebb as a 20 s meter hears it, so the trim neither chases the ebb down nor
     * lifts into it */
    {
        float want = 0.0f;
        if (cd.ebbing) {
            {   /* the bottom is a place, not a drop: the garden's own level (the 3 s meter less the ebb as that meter
                 * hears it) is taken down to ebb_depth under the target. If the garden thins by itself meanwhile (a lone
                 * voice's walkers can take it 10 dB down in a minute) the ebb gives the difference back. Three takes with
                 * the ebb as a drop of 9-16 dB and a floor on the 20 s meter: 19.1, 19.3 and 20.9 dB under the target at
                 * the deepest (the Definition of done's line: 20) */
                const float nat = 3.0103f * fm_log2f(fm_maxf(cd.m3, 1e-9f)) - cd.ebb3;
                want = fm_clampf(cd.target - cd.ebb_depth - nat, -EBB_MOST, 0.0f);
            }
            if (cd.ebb <= want + 1.0f)
                cd.ebb_hold -= dt;              /* at the bottom */
            if (cd.ebb_hold <= 0.0f) {
                cd.ebbing = 0;
                cd.ebb_t = 0.0f;
                cd.ebb_next = EBB_GAP;
                want = 0.0f;
            }
        } else {
            cd.ebb_t += dt * cd.speed;
            if (cd.ebb_t > cd.ebb_next && cd.opening <= 0.0f && cd.ebb > -0.5f &&
                cd.level20 > cd.target - 5.0f) {   /* from a garden that is in: an ebb out of a thin stretch is not heard as one */
                cd.ebbing = 1;
                cd.ebb_depth = EBB_DEPTH;
                cd.ebb_hold = EBB_HOLD;
                cd.bact.x = fm_minf(cd.bact.x, -1.0f - 0.4f * frand());   /* the bells: to a lull, and their own way back */
                bells.busy = 0.0f;
                want = cd.ebb;
            }
        }
        cd.ebb = slew(cd.ebb, want, EBB_RISE, EBB_FALL, dt);
        cd.ebb20 += (cd.ebb - cd.ebb20) * fm_minf(dt * (1.0f / 20.0f), 1.0f);
        cd.ebb3 += (cd.ebb - cd.ebb3) * fm_minf(dt * (1.0f / 3.0f), 1.0f);
    }
    err = cd.level20 - (cd.target + cd.ebb20);
    /* Phase 5 (Tomorrow's Man, TESTING #17: the piece may grow quieter, and no rule should fill a quiet stretch): the band is
     * one-sided. Over the target by 1.5 dB the trim comes down (0.15 dB/s: too loud is the fault); it lifts only when
     * the mix is more than 6 dB under (Phase 4: 1.5), by at most 6 dB (Phase 4: 9), and a lift relaxes at 0.02 dB/s
     * once the mix is back inside (as a cut does), so it is lent, not kept. */
    if (err > 1.5f)
        cd.trim = fm_maxf(cd.trim - fm_minf(err * (1.0f / 3.0f), 1.0f) * 0.15f * dt, -12.0f);   /* its full rate from 3 dB over (was 6) */
    else if (err < -6.0f && cd.dip > -3.0f)     /* (not into the pad's recess: the quiet is meant) */
        cd.trim = fm_minf(cd.trim + fm_minf((-6.0f - err) * (1.0f / 6.0f) + 0.2f, 1.0f) * 0.06f * dt, 6.0f);
    else {                                      /* inside: back toward 0, either way */
        /* Phase 5c: a lift is handed back as the garden returns. Once the mix is within 3 dB of its target again (the
         * 20 s meter or the 3 s one) a raised trim comes down at the cut's 0.15 dB/s, not the relaxing 0.02: in a
         * take the trim sat at +6 and the make-up at +3 through a 14 dB lull, the bass and a pad voice came back
         * together under them, and the 60 s loudness went 5.8 dB over (4:47) before the cut began */
        const float err3 = 3.0103f * fm_log2f(fm_maxf(cd.m3, 1e-9f)) - (cd.target + cd.ebb3);
        const float back = err > -3.0f || err3 > -3.0f ? 0.15f : 0.02f;
        cd.trim = cd.trim > 0.0f ? fm_maxf(cd.trim - back * dt, 0.0f) : fm_minf(cd.trim + 0.02f * dt, 0.0f);
    }
    /* the make-up for a resting layer (the bells, the bass): the conductor knows what it is taking away, so it
     * gives the energy back as the layer fades (and takes it back as it returns) instead of waiting for the meter.
     * What is missing is the layer's energy as it left (before its gain, so today's gain applies), weighted as the
     * meter weighs it, less what of it still sounds; the make-up is the dB the meter would gain with it back,
     * against the mix as it is now (a first version used the layer's share as it left, which stayed at 6 dB when
     * another layer had meanwhile returned: a +13 dB minute in the 2026-11-08 Garden render). Capped at 6 dB. The
     * trim, this and the bias sum into each layer's gain, slewed at 0.16 dB/s: that slew is the rate budget's guard
     * (a voice's 0.4 + 0.16 + the plate's ripple sit under the swell limit). */
    {
        float miss = 0.0f, want;
        if (!bells.rest && bells.ldb >= cd.ff_db0[L_BELLS] - 3.0f)
            cd.ff_e0[L_BELLS] = 0.0f;           /* back where it was: nothing missing */
        if (!bass.rest && bass.ldb >= cd.ff_db0[L_BASS] - 3.0f)
            cd.ff_e0[L_BASS] = 0.0f;
        if (cd.ff_e0[L_BELLS] > 0.0f)
            miss += K_W[L_BELLS] * cd.ff_e0[L_BELLS] * cd.g[L_BELLS] * cd.g[L_BELLS]
                    * (1.0f - fm_db2lin(2.0f * fm_minf(bells.ldb - cd.ff_db0[L_BELLS], 0.0f)));
        if (cd.ff_e0[L_BASS] > 0.0f)
            miss += K_W[L_BASS] * cd.ff_e0[L_BASS] * cd.g[L_BASS] * cd.g[L_BASS]
                    * (1.0f - fm_db2lin(2.0f * fm_minf(bass.ldb - cd.ff_db0[L_BASS], 0.0f)));
        want = miss > 0.0f ? fm_minf(3.0103f * fm_log2f((cd.m20 + miss) / fm_maxf(cd.m20, 1e-9f)), 3.0f) : 0.0f;   /* Phase 5: at most 3 dB
                                                            * (was 6): half of what a resting layer took away is given back, the rest is the quiet */
        want = fm_maxf(want - fm_maxf(err, 0.0f), 0.0f);   /* less whatever the mix is already over its target: the trim had
                                                            * risen meanwhile too (Phase 4, 8 h render: trim +8 and make-up +5
                                                            * together as the bass returned, +7 dB over for minutes, the pad
                                                            * thinned to nothing and the bass alone for an hour) */
        cd.ff = slew(cd.ff, want, 0.15f, 0.15f, dt);   /* the gains' slew (0.16 dB/s) is the budget's guard */
    }
    /* shares: a layer over the cap (45% with three or more layers sounding; none with two) is drawn
     * down (at most 8 dB, 0.06 dB/s); 10 points under it its bias returns. The pad, when it has led for
     * two minutes, also loses a voice. */
    {
        int active = 0;
        for (k = 0; k < NLAYERS; k++)
            active += cd.e20[k] > 1e-5f;        /* sounding (above -50 dB): a resting layer is not counted */
        cap = active >= 3 ? 0.45f : 2.0f;       /* two layers: no bias (one of them must lead) */
    }
    {
        float wtot = 0.0f;
        for (k = 0; k < NLAYERS; k++)
            wtot += K_W[k] * cd.e20[k];
        for (k = 0; k < NLAYERS; k++)
            cd.wshare[k] = wtot > 0.0f ? K_W[k] * cd.e20[k] / wtot : 0.0f;
    }
    /* the bells' company (Tomorrow's Man, TESTING #20: nearly alone they need not be as loud as in a full garden; he measured
     * the same -28 dB peaks with only the low end under them as with everything): how far the other layers together
     * sit under the target. Within 4 dB of it nothing; below that the bells follow them down, by at most 4 dB
     * (Phase 5: 0.6 dB a dB to 9 dB. Phase 5b, Tomorrow's Man's TESTING #21: the stretch he calls gold, 3:25 of the Phase 4
     * take, is the bells nearly alone as the pad goes, and at Phase 5's -9 they would be 12 dB under where he heard
     * them; what jarred at 3:27 was high, bright, out of the scale and three times as many. So alone they are a
     * little softer, rounder (bell_voice) and a few more (loop_target): cd.alone, 0..1.) */
    {
        const float others = K_W[L_PAD] * cd.e20[L_PAD] + K_W[L_BASS] * cd.e20[L_BASS] + K_W[L_GROUND] * cd.e20[L_GROUND];
        company = fm_clampf(0.6f * (3.0103f * fm_log2f(fm_maxf(others, 1e-9f)) - (cd.target + cd.ebb20) + 4.0f), -9.0f, 0.0f);
        cd.alone += (company * (-1.0f / 9.0f) - cd.alone) * fm_minf(dt * (1.0f / 20.0f), 1.0f);
        company *= 4.0f / 9.0f;
    }
    /* the pad's company (the note above ear_w) */
    {
        float wp = 0.0f, top = -99.0f, rest, un;
        int i;
        for (i = 0; i < EN_NPAD; i++)
            if (pad[i].st != PV_OFF && pad[i].ldb > top) {
                top = pad[i].ldb;
                wp = ear_w(pad[i].note);        /* the loudest voice's note stands for the pad */
            }
        cd.e5 += (e[L_PAD] - cd.e5) * fm_minf(dt * (1.0f / 5.0f), 1.0f);
        cd.hold5 += (cd.hold - cd.hold5) * fm_minf(dt * (1.0f / 5.0f), 1.0f);
        rest = EAR_BELLS * cd.e20[L_BELLS] + ear_w((float)bass.midi_note) * cd.e20[L_BASS] + ear_w(ground_target) * cd.e20[L_GROUND];
        /* (+4 dB: against the pad's solo pass and the mix less it, A-weighted, the notes' weights alone read 3.5 dB low
         * at 0:28 of take D (13.0 for 16.5) and 4.8 low at 0:14 (2.1 for 6.9): the pad's overtones are heard better than
         * its note, and the bass's sub-octave not at all) */
        cd.expo = fm_clampf(4.0f + 3.0103f * (fm_log2f(fm_maxf(wp * cd.e5, 1e-12f)) - fm_log2f(fm_maxf(rest, 1e-12f))), -40.0f, 40.0f);
        cd.hold_up = 0;
        if (pc_most > 0.0f || pc_call > 0.0f) {
            const float was = cd.hold;
            un = cd.expo - cd.hold5;            /* where the pad would stand without the hold */
            cd.hold = slew(cd.hold, -fm_clampf(un - pc_line, 0.0f, pc_most), PC_RISE, PC_FALL, dt);
            cd.hold_up = cd.hold > was;
            if (cd.called > 0.0f)
                cd.called -= dt;
            if (cd.exposed < 0.0f)              /* the grace after a call */
                cd.exposed = fm_minf(cd.exposed + dt, 0.0f);
            else if (un > pc_line + 3.0f && top > -15.0f && cd.opening <= 0.0f)
                cd.exposed += dt;
            else
                cd.exposed = fm_maxf(cd.exposed - dt, 0.0f);
            if (pc_call > 0.0f && cd.exposed > pc_call && bell_vol > 0.0f) {
                if (bells.rest) {
                    bells.rest = 0;
                    bells.stay_t = 0.0f;
                    bells.min_stay = 240.0f + 360.0f * frand();
                }
                cd.pres[L_BELLS].x = fm_maxf(cd.pres[L_BELLS].x, 0.5f);
                cd.bact.x = fm_maxf(cd.bact.x, 0.2f + 0.4f * frand());   /* a passage; its own timer lets it up (bells_step) */
                bells.busy = 0.0f;
                cd.called = 90.0f;
                cd.exposed = -240.0f;
            }
        }
    }
    for (k = 0; k < NLAYERS; k++) {
        float want;
        cd.share[k] = tot > 0.0f ? cd.e20[k] / tot : 0.0f;
        cd.lead[k] = cd.share[k] > 0.5f ? cd.lead[k] + dt : fm_maxf(cd.lead[k] - dt, 0.0f);
        /* the bias: 0 at the cap less 5 points, -8 dB at the cap plus 25, in between in proportion */
        want = -8.0f * fm_clampf((cd.share[k] - (cap - 0.05f)) * (1.0f / 0.3f), 0.0f, 1.0f);
        if (err < -1.0f && want < cd.bias[k])
            want = cd.bias[k];                  /* the mix is under its target: no layer is drawn down further */
        cd.bias[k] = slew(cd.bias[k], want, 0.06f, 0.06f, dt);
        cd.gdb[k] = slew(cd.gdb[k], k == L_BELLS ? fm_minf(cd.trim, 0.0f) + cd.bias[k] + company : cd.trim + cd.ff + cd.bias[k],
                         0.16f, 0.16f, dt);    /* the bells are struck notes: the governor never lifts them (Phase 4: at most
                                                * 3 dB, and it sat there; before that +9 to +15 in a thin garden, Tomorrow's Man's loud
                                                * lone bells), it turns them down with the mix, and by their company */
        cd.g[k] = fm_db2lin(cd.gdb[k] + cd.ebb + (k == L_PAD ? cd.dip + cd.hold : 0.0f));   /* Phase 5c: and the ebb, on its own slews;
                                                 * 2026-11-13: and the pad's recess */
    }
    if (cd.share[L_PAD] > cap + 0.05f && cd.lead[L_PAD] > 120.0f)
        cd.pad_thin = 1;                        /* (never with fewer than three layers sounding: cap is 2) */
    else if (cd.share[L_PAD] < cap - 0.05f)
        cd.pad_thin = 0;
    /* the ensemble floor (2026-11-08): the layers' rests are independent, so now and then they coincide, and with the
     * trim's reach spent (+9 dB) the pad alone cannot reach the target: the first 4 h Garden render fell to -53 LUFS,
     * Voices to silence. Rule: when the mix has sat 6 dB or more under its target for a minute, or no layer has
     * sounded for ten seconds, one resting layer is called back (the bass first, then the bells), its presence set
     * just over its return threshold so it does not rest again at once; then three minutes' grace. The pad's own
     * floor is in pad_step (a voice at least while the bells and the bass both rest). A discrete call, so the
     * walkers' means are not biased (Phase 3's continuous calls were: see the note at the end of this function). */
    {
        int active = 0;
        const float eg = fm_db2lin(cd.ebb20);
        for (k = 0; k < L_GROUND; k++)
            active += cd.e20[k] > 1e-3f * eg * eg;   /* sounding above -30 dB (Phase 5c: less the ebb) */
        cd.silent = active ? 0.0f : cd.silent + dt;
        cd.under = err < -12.0f ? cd.under + dt : fm_maxf(cd.under - dt, -1000.0f);   /* Phase 5: 12 dB under (was 6): a quiet
                                                                                       * stretch is the music, a vanishing one is not */
        if ((cd.under > 60.0f || cd.silent > 10.0f) && cd.under > -1.0f) {
            if (bass.rest && bass_vol > 0.0f) {
                bass.rest = 0;
                cd.pres[L_BASS].x = fm_maxf(cd.pres[L_BASS].x, 0.45f);
                midi(0x92, bass.midi_note, 80);
            } else if (bells.rest && bell_vol > 0.0f) {
                bells.rest = 0;
                cd.pres[L_BELLS].x = fm_maxf(cd.pres[L_BELLS].x, 0.47f);
            } else {
                cd.pres[L_PAD].x = fm_maxf(cd.pres[L_PAD].x, 0.65f);   /* the pad: two voices' worth of presence */
            }
            cd.under = -180.0f;
            cd.silent = 0.0f;
        }
    }
    /* Phase 3 also called the resting layers' presence up when a layer had led for three minutes or the mix was
     * 2 dB under its target (0.02 and 0.03 per second). Removed 2026-11-08: a presence walker's own pull is about
     * 0.0004 per second, so any such call overrode the rests (the bells' presence averaged 0.65 around a center of
     * 0.5 and the bells rested twice in four hours). The rests are the design; the trim and the pad's headroom
     * term are the governor's hands when the garden thins. */
}

static void pad_step(float dt)
{
    int i, n_on, want_n, relay = 0;
    const unsigned shed = en_shed & 3u;
    float want, top = -99.0f;
    cd.since_entry += dt;
    /* how many voices: the pad's presence, the density, the Density knob, the governor */
    want = fm_clampf(cd.pres[L_PAD].x + 0.25f * cd.dens + 0.3f * (float)(par[P_DENSITY] - 40) * (1.0f / 60.0f)
                     + 0.05f * fm_clampf(cd.target + cd.ebb20 - cd.level20, -4.0f, 2.0f), 0.0f, 1.0f);   /* the governor: fewer voices when the
                                                 * mix is over its target, and only a little more of them when it is under (Phase 5,
                                                 * TESTING #17; +4 here kept a voice singing 95% of the time once the bells were sparse) */
    want_n = want < 0.3f ? 0 : want < 0.6f ? 1 : want < 0.85f ? 2 : 3;   /* about 25% / 38% / 25% / 12% */
    want_n -= cd.pad_thin;
    /* Phase 5: the foundation. One sustained layer always sounds (Tomorrow's Man, TESTING #17: "I always want something burbling
     * away"): while the bass is resting or far down, the pad sings a voice at least. The bells are an ornament now (a
     * strike every ten seconds or so) and no longer count as company; in a piece without a bass they still do, by
     * Phase 4's rule (resting, far down, or nothing rung for 20 s). A first Phase 5 render without this left a few
     * quiet bells alone for eight minutes at 0:59, the bass resting and the pad's presence low. */
    if (bass_vol > 0.0f ? (bass.rest || bass.ldb < -20.0f)
                        : (bells.rest || bells.ldb < -20.0f || bells.quiet > 20.0f || bell_vol <= 0.0f))
        cd.floor_hold = 60.0f;                  /* and the ask holds a minute: a single bell reset the 20 s and the floor's
                                                 * voice was sent out a second after it entered, in and out every 20 s */
    else if (cd.floor_hold > 0.0f)
        cd.floor_hold -= dt;
    /* Phase 5: the pad's rest is bounded, as the bells' and the bass's have been since Phase 3b. With no voice singing
     * for 4-10 minutes of Drift's time (8-20 min at Garden's half speed) its presence is lifted over the first voice's
     * threshold and a voice is asked for. A presence walker alone can sit low for an hour: in the first final 8 h take
     * the pad sang nothing through the whole eighth hour, the bass (present, so the foundation rule was content) and a
     * few bells alone. */
    if (en_playing && cd.speed > 0.0f) {
        if (pad_count() == 0) {
            cd.pad_rest += dt * cd.speed;
            if (cd.pad_rest > cd.pad_rest_max) {
                cd.pres[L_PAD].x = fm_maxf(cd.pres[L_PAD].x, 0.45f);
                cd.floor_hold = fm_maxf(cd.floor_hold, 60.0f);
            }
        } else if (cd.pad_rest > 0.0f) {
            cd.pad_rest = 0.0f;
            cd.pad_rest_max = 240.0f + 360.0f * frand();
        }
    }
    if (want_n < 1 && cd.floor_hold > 0.0f)
        want_n = 1;                             /* the ensemble floor: a voice at least while the others are out or
                                                 * still far away (a return from -60 dB at 0.4 dB/s is inaudible for a minute) */
    /* (a rule "the pad sings a voice while the bass has led five minutes" was tried on 2026-11-09 and removed the same
 * day: the bass's energy share is over half whenever the pad thins, so the pad never rested again (0% of 8 h, 2.8 voices:
 * a bed, Tomorrow's Man's TESTING #7). The bass's share is reported, not tuned: Tomorrow's Man calls it the foundation.) */
    if (want_n < 0)
        want_n = 0;
    /* the pad's turn and its recess (the note above pad_count) */
    if (en_playing && cd.speed > 0.0f && cd.opening <= 0.0f) {
        int up = 0, heard = 0;
        float want_dip = 0.0f;
        for (i = 0; i < EN_NPAD; i++) {
            up |= pad[i].st == PV_ON && pad[i].ldb > -15.0f;
            heard |= pad[i].st != PV_OFF && pad[i].ldb > -30.0f;
        }
        cd.away = heard ? 0.0f : cd.away + dt;
        if (!cd.recessing) {
            if (up || (heard && cd.share[L_PAD] > 0.5f))   /* up, or leading from lower down (the bass away, the voices
                                                 * passing the foundation hand to hand at -16 to -30 dB: 98% of the energy) */
                cd.turn += dt;
            else if (cd.away > 20.0f && cd.turn > 0.0f) {   /* it went by itself: the next turn is a new one */
                cd.turn = 0.0f;
                cd.turn_max = PAD_TURN;
            }
            if (cd.turn > cd.turn_max) {
                /* the pad alone, the bass resting: the bass is called back (bass_step returns it) and the recess waits
                 * until it is up, a minute and a half. With a shallow recess instead (6-8 dB, the bass left to finish
                 * its rest) two takes kept the pad over 70% of the energy for 15 minutes at a stretch, as v1 did */
                if (bass_vol > 0.0f && (bass.rest || bass.ldb < -20.0f)) {
                    if (bass.rest)
                        bass.rest_t = bass.gap;
                } else {
                    cd.recessing = 1;
                    cd.recess = PAD_RECESS;
                    cd.dip_depth = cd.floor_hold > 0.0f ? 6.0f + 2.0f * frand()   /* a piece without a bass, its bells away:
                                                                                   * the one layer sounding, not as far */
                                                        : 10.0f + 5.0f * frand();
                }
            }
        }
        if (cd.recessing == 1) {
            want_dip = -cd.dip_depth;
            if (cd.dip <= want_dip + 1.0f) {    /* at the bottom */
                if (cd.recess > 0.0f && cd.recess - dt <= 0.0f) {
                    int best = -1;              /* the voice nearest the end of its stay is let go */
                    for (i = 0; i < EN_NPAD; i++)
                        if (pad[i].st == PV_ON && (best < 0 || pad[i].stay < pad[best].stay))
                            best = i;
                    if (best >= 0 && pad_count() > 1)
                        pad[best].stay = 0.0f;
                }
                cd.recess -= dt;
            }
            if (cd.recess <= 0.0f)
                cd.recessing = 2;               /* on the way back */
        }
        if (cd.recessing == 2 && cd.dip > -0.5f) {
            cd.recessing = 0;
            cd.turn = 0.0f;
            cd.turn_max = PAD_TURN;
        }
        cd.dip = slew(cd.dip, want_dip, PAD_DIP_RISE, PAD_DIP_FALL, dt);
    }
    if (!en_playing || cd.speed <= 0.0f)
        want_n = pad_count();                   /* resting, or Hold: no entrances, no leaves */
    if (want_n > SHED_NPAD[shed])
        want_n = SHED_NPAD[shed];               /* the guard: fewer voices, Hold or not */
    cd.pad_target = want_n;
    for (i = 0; i < EN_NPAD; i++)
        if (pad[i].st != PV_OFF && pad[i].ldb > top)
            top = pad[i].ldb;
    for (i = 0; i < EN_NPAD; i++) {             /* each voice: its walkers, its stay, its level */
        pad_t *v = &pad[i];
        float tgt;
        walk(&v->lw[0], cd.speed);
        walk(&v->lw[1], cd.speed);
        walk(&v->lw[2], cd.speed);
        walk(&v->bw, cd.speed);
        if (v->rest > 0.0f)
            v->rest -= dt;
        if (v->st == PV_ON) {
            if (cd.speed > 0.0f)
                v->stay -= dt;
            if (v->stay <= 0.0f) {
                /* Phase 5b: the foundation's voice hands over. While the pad is the one sustained layer (the bass away)
                 * a voice at the end of its stay waits until another is up, and one is asked for: it used to leave
                 * at once, its successor rising from far below as it fell, and where they crossed the garden was 18 dB
                 * down for a minute (every quiet extreme of the Phase 5 and 5b takes: 16 to 22 dB under the target) */
                int k, other = 0;
                for (k = 0; k < EN_NPAD; k++)
                    other |= k != i && pad[k].st == PV_ON && pad[k].ldb > -15.0f;
                if (cd.floor_hold > 0.0f && !other && v->ldb > -30.0f)
                    relay = 1;
                else
                    v->st = PV_OUT;
            }
        }
        {   /* Phase 5c: the crest's ceiling (PAD_CREST) and the note's height (PAD_TILT) */
            float sw = v->lw[0].x + v->lw[1].x + v->lw[2].x;
            if (sw > 0.5f * PAD_CREST)
                sw = fm_minf(0.5f * PAD_CREST + 0.25f * (sw - 0.5f * PAD_CREST), PAD_CREST);
            tgt = v->st == PV_ON ? fm_minf((pl_breath > 0.0f ? fm_minf(v->base_db, PAD_TOP - PAD_CREST) : v->base_db) + sw, PAD_TOP)
                                   + v->tilt : -60.0f;   /* (breath: the note at pl_breath) */
        }
        /* (Phase 5b: a voice rising in another's shadow, 20 dB or more under the loudest, comes up at 2 dB/s: it is a
         * hundredth of the pad's energy there, so the pad's level does not move with it, and the minute and a half an
         * entrance from -52 dB took before it could be heard is a quarter of that) */
        /* (2026-11-13: while the pad's recess is coming back the voice's own rise is less by the recess's, so the two
         * together are the 0.4 dB/s the rate budget was written for; summed, a take read 1.58 dB in 2 s against 1.5) */
        v->ldb = slew(v->ldb, tgt, cd.opening > 0.0f || (v->st == PV_ON && v->ldb < top - 20.0f) ? 2.0f
                                   : cd.recessing == 2 ? v->rise - PAD_DIP_RISE
                                   : cd.hold_up ? v->rise - PC_RISE : v->rise,   /* (2026-11-14: the hold given back, the same) */
                      v->st == PV_ON ? fm_maxf(v->rise * 0.75f, v->fall) : fm_maxf(v->fall, SHED_FALL[shed]), dt);
        v->crest = fm_clampf((v->ldb - v->tilt - v->base_db + 4.0f) * (1.0f / 8.0f), 0.0f, 1.0f);
        if (v->st == PV_OUT && v->ldb <= -50.0f) {
            v->st = PV_OFF;
            v->rest = 20.0f + 40.0f * frand();
            if (v->midi_note) {
                midi(0x80, v->midi_note, 0);
                v->midi_note = 0;
            }
        }
        v->lin = v->st == PV_OFF ? 0.0f : fm_db2lin(v->ldb);
    }
    n_on = pad_count();
    if (relay && want_n < 2 && SHED_NPAD[shed] >= 2)
        want_n = 2;                             /* the hand-over: the successor is asked for, the old voice kept */
    if (n_on > want_n) {                        /* one too many: the one nearest the end of its stay leaves */
        int best = -1;
        for (i = 0; i < EN_NPAD; i++)
            if (pad[i].st == PV_ON && (best < 0 || pad[i].stay < pad[best].stay))
                best = i;
        if (best >= 0)
            pad[best].st = PV_OUT;
    } else if (n_on < want_n && cd.since_entry >= (cd.opening > 0.0f ? 3.0f : cd.entry_gap)) {
        /* one more: a rested voice, by preference one whose note no singing voice has; when every voice is
         * still dissolving (a chord change can send three out at once), the faintest one below -30 dB takes
         * its new note from where it is */
        int best = -1, k;
        for (i = 0; i < EN_NPAD; i++) {
            int dup = 0, same = 0;
            if (pad[i].st != PV_OFF || pad[i].rest > 0.0f)
                continue;
            for (k = 0; k < EN_NPAD; k++) {
                dup |= k != i && pad[k].st == PV_ON && (int)pad[k].note % 12 == (int)pad[i].target % 12;
                same |= k != i && pad[k].st != PV_OFF && pad[k].ldb > -30.0f && (int)pad[k].note == (int)pad[i].target;
            }
            if (same)
                continue;                       /* Phase 5c: never the very note another voice is singing or still
                                                 * dissolving on (two voices on C#5 at 3:39 of the 5b take: one frequency,
                                                 * twice, into the plate); it waits, or another voice goes */
            if (!dup) {
                best = i;
                break;
            }
            if (best < 0)
                best = i;
        }
        if (best < 0)
            for (i = 0; i < EN_NPAD; i++)
                if (pad[i].st == PV_OUT && pad[i].ldb < -30.0f && !pad[i].hand && (best < 0 || pad[i].ldb < pad[best].ldb)) {
                    int same = 0;
                    for (k = 0; k < EN_NPAD; k++)
                        same |= k != i && pad[k].st != PV_OFF && pad[k].ldb > -30.0f && (int)pad[k].note == (int)pad[i].target;
                    if (!same)
                        best = i;
                }
        if (best >= 0)
            pad_enter(&pad[best], best);
    }
}

static int pad_sings(void)                      /* a voice is up: singing, within 15 dB of full */
{
    int i;
    for (i = 0; i < EN_NPAD; i++)
        if (pad[i].st == PV_ON && pad[i].ldb > -15.0f)
            return 1;
    return pad_vol <= 0.0f;                     /* (a piece without a pad: nothing to wait for) */
}

static void bass_step(float dt)
{
    float tgt;
    walk(&bass.lw[0], cd.speed);
    walk(&bass.lw[1], cd.speed);
    /* the bass sits out when its presence is low, once it has stayed its least (5-12 weather minutes); a rest lasts
     * 2-5 weather minutes after the layer has gone (under -40 dB), then it returns whatever its presence says, the
     * presence lifted a little over the threshold so it does not leave again at once. Bounded on both sides
     * (2026-11-08): a presence walker alone can sit under 0.3 for an hour at Garden's half speed */
    if (en_playing && cd.speed > 0.0f) {
        if (!bass.rest) {
            bass.stay_t += dt * cd.wspeed;
            if (cd.pres[L_BASS].x < 0.3f && bass.stay_t >= bass.min_stay &&
                !(bell_vol > 0.0f && (bells.rest || bells.ldb < -20.0f)) &&   /* not while the bells are out: both away left
                                                                            * the pad alone for six minutes (Tomorrow's Man, TESTING #15) */
                !pad_sings()) {
                cd.floor_hold = fm_maxf(cd.floor_hold, 30.0f);   /* Phase 5, the foundation's hand-over: the bass waits for
                                                 * a pad voice to be up (within 15 dB of full) before it leaves, and asks for
                                                 * one; leaving first left a minute or two of bells alone, 17 dB under */
            } else if (cd.pres[L_BASS].x < 0.3f && bass.stay_t >= bass.min_stay && !cd.recessing &&
                       !(bell_vol > 0.0f && (bells.rest || bells.ldb < -20.0f))) {
                bass.rest = 1;
                bass.rest_t = 0.0f;
                cd.turn = 0.0f;                 /* 2026-11-13: the pad carries the garden for one turn (the note above pad_count) */
                cd.turn_max = PAD_CARRY;
                bass.gap = 120.0f + 180.0f * frand();
                cd.ff_e0[L_BASS] = cd.e20[L_BASS] / fm_maxf(cd.g[L_BASS] * cd.g[L_BASS], 1e-6f);
                cd.ff_db0[L_BASS] = bass.ldb;
                midi(0x82, bass.midi_note, 0);
            }
        } else {
            if (bass.ldb <= -40.0f)
                bass.rest_t += dt * cd.wspeed;
            if (bass.rest_t >= bass.gap) {
                bass.rest = 0;
                bass.stay_t = 0.0f;
                bass.min_stay = 300.0f + 420.0f * frand();
                cd.pres[L_BASS].x = fm_maxf(cd.pres[L_BASS].x, 0.4f);
                midi(0x92, bass.midi_note, 80);
            }
        }
    }
    /* its level: the two walkers, and 4 dB less as its presence falls from 0.6 toward the rest (a layer on
     * its way out is already quieter) */
    tgt = en_playing && !bass.rest ? bass.lw[0].x + bass.lw[1].x + 4.0f * fm_clampf((cd.pres[L_BASS].x - 0.6f) * (1.0f / 0.3f), -1.0f, 0.0f)
                                   : -60.0f;
    bass.ldb = slew(bass.ldb, tgt, cd.opening > 0.0f ? 2.0f : 0.4f, en_playing ? 0.18f : 6.0f, dt);   /* back quicker than it left
                                                                   * (Tomorrow's Man, TESTING #15); 0.4 is a pad voice's, inside the rate budget */
    bass.lin = bass.ldb <= -59.0f ? 0.0f : fm_db2lin(bass.ldb);
}

/* the bells as a layer (Tomorrow's Man, TESTING #11: they were constant, loud and busy): their presence empties the
 * loops as it falls (loop_target) and below 0.3 rests the layer (no bell rings; the ringing ones decay),
 * back above 0.42; the layer's level breathes on two summed walkers, is 6 dB less as its presence falls
 * from 0.6 toward the rest, leaves at 0.18 dB/s and comes back at 0.4 (Phase 4, Tomorrow's Man: up quicker than down) */
static void bells_step(float dt)
{
    float tgt;
    walk(&bells.lw[0], cd.speed);
    walk(&bells.lw[1], cd.speed);
    bells.quiet += dt;                          /* real seconds: a silence is heard in real time */
    /* Phase 5b: a passage lets up (Tomorrow's Man, TESTING #21, of the Phase 4 take: "the density plays beautifully, but it never
     * let up"). The activity walker alone can stay high for half an hour (a first 5b take: 10 strikes a minute and
     * more for 28 minutes at a stretch), so, like the rests, a passage is bounded by a timer: after 2.5-6.5 minutes
     * with the activity up it is set down to a lull and finds its own way back. Real minutes: a passage is heard in
     * real time. The loops take the new count as each ends its pass, so the let-up comes over a minute. */
    if (en_playing && cd.speed > 0.0f) {
        if (cd.bact.x > 0.2f) {
            bells.busy += dt;
            if (bells.busy > bells.busy_max) {
                cd.bact.x = -0.5f - 0.7f * frand();
                bells.busy = 0.0f;
                bells.busy_max = 150.0f + 240.0f * frand();
            }
        } else if (cd.bact.x < -0.1f) {
            bells.busy = 0.0f;
        }
    }
    /* the rest, as the bass's: after a stay of 4-10 weather minutes at the least, when the presence is under 0.3;
     * 2.5-6 weather minutes once the layer has gone; back whatever the presence says, lifted to 0.42 */
    if (en_playing && cd.speed > 0.0f) {
        if (!bells.rest) {
            bells.stay_t += dt * cd.wspeed;
            if (cd.pres[L_BELLS].x < 0.3f && bells.stay_t >= bells.min_stay &&
                !(bass_vol > 0.0f && (bass.rest || bass.ldb < -20.0f))) {     /* not while the bass is out (TESTING #15) */
                bells.rest = 1;
                bells.rest_t = 0.0f;
                bells.gap = 150.0f + 210.0f * frand();
                cd.ff_e0[L_BELLS] = cd.e20[L_BELLS] / fm_maxf(cd.g[L_BELLS] * cd.g[L_BELLS], 1e-6f);
                cd.ff_db0[L_BELLS] = bells.ldb;
            }
        } else {
            if (bells.ldb <= -40.0f)
                bells.rest_t += dt * cd.wspeed;
            if (bells.rest_t >= bells.gap) {
                bells.rest = 0;
                bells.stay_t = 0.0f;
                bells.min_stay = 240.0f + 360.0f * frand();
                cd.pres[L_BELLS].x = fm_maxf(cd.pres[L_BELLS].x, 0.42f);
            }
        }
    }
    tgt = en_playing && !bells.rest ? bells.lw[0].x + bells.lw[1].x + 6.0f * fm_clampf((cd.pres[L_BELLS].x - 0.6f) * (1.0f / 0.3f), -1.0f, 0.0f)
                                    : -60.0f;
    bells.ldb = slew(bells.ldb, tgt, cd.opening > 0.0f ? 2.0f : cd.called > 0.0f ? 0.8f : 0.4f, en_playing ? 0.18f : 6.0f, dt);   /* back quicker than it left (TESTING #15) */
    bells.lin = bells.ldb <= -59.0f ? 0.0f : fm_db2lin(bells.ldb);
}

static void ground_step(float dt)               /* Ground: rare, slow, low (TESTING #9) */
{
    float tgt;
    if (ground_gap > 0.0f)
        ground_gap -= dt;
    if (en_playing && cd.speed > 0.0f) {
        if (!ground_on) {
            ground_due += dt * cd.wspeed;
            if (ground_due > ground_due_at && cd.pres[L_GROUND].x < 0.7f)   /* overdue: called up */
                cd.pres[L_GROUND].x += 0.01f * dt * cd.speed;
        }
        if (!ground_on && ground_gap <= 0.0f && cd.pres[L_GROUND].x > 0.65f) {
            ground_on = 1;
            ground_stay = 120.0f + 180.0f * frand();
            midi(0x93, (uint32_t)ground_target, 70);
        } else if (ground_on) {
            ground_stay -= dt;
            if (cd.pres[L_GROUND].x < 0.5f || ground_stay <= 0.0f) {
                ground_on = 0;
                ground_gap = 180.0f + 240.0f * frand();
                ground_due = 0.0f;
                ground_due_at = (40.0f + 30.0f * frand()) * 60.0f;
                midi(0x83, (uint32_t)ground_target, 0);
            }
        }
    }
    tgt = en_playing && ground_on ? 0.0f : -60.0f;
    ground_db = slew(ground_db, tgt, 0.3f, en_playing ? 0.2f : 6.0f, dt);
    ground_lin = ground_db <= -59.0f ? 0.0f : fm_db2lin(ground_db);
    en_ground_level = ground_lin;
    en_drum_glow[0] = ground_lin;               /* the screen's fourth meter (Phase 6 gives it a name) */
}

static void conductor_step(void)                /* every CTRL_N samples */
{
    const float dt = CTRL_DT;
    const int hold = !en_playing || en_frozen;
    const float sp = hold ? 0.0f : cd.speed;
    int k;
    if (cd.opening > 0.0f)
        cd.opening -= dt;
    /* seasons, then the weather at the season's pace */
    walk(&cd.s_speed, sp);
    walk(&cd.s_density, sp);
    walk(&cd.s_bright, sp);
    cd.wspeed = sp * fm_exp2f(cd.s_speed.x);
    walk(&cd.bright, cd.wspeed);
    walk(&cd.density, cd.wspeed);
    walk(&cd.reg, cd.wspeed);
    walk(&cd.space, cd.wspeed);
    walk(&cd.chord, cd.wspeed);
    for (k = 0; k < NLAYERS; k++)
        walk(&cd.pres[k], cd.wspeed);
    walk(&cd.bact, cd.wspeed);
    cd.dens =fm_clampf(cd.density.x + cd.s_density.x + fm_clampf(cd.trim * 0.05f, -0.4f, 0.0f), -1.0f, 1.0f);   /* the governor
                                                 * thins a loud garden; it no longer thickens a quiet one (Phase 5, TESTING #17) */
    cd.br = fm_clampf(cd.bright.x + cd.s_bright.x, -0.6f, 0.6f) + mood_br;   /* (Phase 6: around Mood's center) */
    reg_now = (int)(cd.reg.x + (cd.reg.x >= 0.0f ? 0.5f : -0.5f));
    walk(&tcur.sx, sp);                         /* the timbre cursor: its region with the seasons, its drift with the weather */
    walk(&tcur.sy, sp);
    walk(&tcur.wx, cd.wspeed);
    walk(&tcur.wy, cd.wspeed);
    tcur.x = fm_clampf(0.5f + tcur.sx.x + tcur.wx.x, 0.0f, 1.0f);
    tcur.y = fm_clampf(0.5f + tcur.sy.x + tcur.wy.x, 0.0f, 1.0f);
    cursor_blend(0, dt);
    /* the harmonic field: its walkers with the seasons; a step is asked for when a walker has strayed three quarters of a step
     * from where the field is (Bright tilts the set's: a brighter piece leans to brighter modes), and done at a
     * chord change */
    walk(&fld.mode, sp);
    walk(&fld.root, sp);
    fld.since += dt * sp;
    if (!fld.pend && fld.since > fld.dwell) {
        const float dm = fld.mode.x + mode_tilt - (float)fld.shift, dr = fld.root.x - (float)fld.move;
        fld.pend = (int8_t)(dm > 0.75f ? 1 : dm < -0.75f ? -1 : dr > 0.75f ? 2 : dr < -0.75f ? -2 : 0);
    }
    echo_fb_eff = fm_clampf(echo_fb + 0.08f * cd.space.x - 0.05f * fm_clampf(-cd.trim * (1.0f / 6.0f), 0.0f, 1.0f), 0.3f, 0.9f);
    governor_step(dt);
    pad_step(dt);
    bells_step(dt);
    bass_step(dt);
    ground_step(dt);
    /* the chord dwells in weather seconds: Hold stops it, a fast season shortens it */
    if (en_playing && !en_frozen && !change_pending) {
        drift_left -= dt * cd.wspeed;
        if (!fifth_done && drift_left < 0.5f * drift_total) {
            fifth_done = 1;                      /* halfway: the bass may lean on the fifth */
            if (frand() < (float)par[P_FIFTH] * 0.01f && chord_nt >= 3)
                bass_to(chord_pcs[2]);
        }
        if (drift_left <= 0.0f)
            change_pending = 1;
    }
    en_drift = drift_total > 0.0f ? 1.0f - fm_clampf(drift_left / drift_total, 0.0f, 1.0f) : 0.0f;
    en_arc = cd.dens;
    /* for the screen and the harness */
    en_cond[CD_BRIGHT] = cd.br;
    en_cond[CD_DENSITY] = cd.dens;
    en_cond[CD_REGISTER] = cd.reg.x;
    en_cond[CD_SPACE] = cd.space.x;
    en_cond[CD_CHORD] = cd.chord.x;
    for (k = 0; k < NLAYERS; k++) {
        en_cond[CD_PRES_PAD + k] = cd.pres[k].x;
        en_cond[CD_SHARE_PAD + k] = cd.share[k];
        en_cond[CD_GAIN_PAD + k] = cd.gdb[k];
    }
    en_cond[CD_S_SPEED] = cd.s_speed.x;
    en_cond[CD_S_DENSITY] = cd.s_density.x;
    en_cond[CD_S_BRIGHT] = cd.s_bright.x;
    en_cond[CD_SPEED] = cd.wspeed;
    en_cond[CD_LEVEL20] = cd.level20;
    en_cond[CD_LEVEL60] = cd.level60;
    en_cond[CD_TARGET] = cd.target;
    en_cond[CD_TRIM] = cd.trim;
    en_cond[CD_PAD_TARGET] = (float)cd.pad_target;
    en_cond[CD_PAD_VOICES] = (float)pad_count();
    en_cond[CD_GROUND] = ground_db;
    en_cond[CD_OPENING] = cd.opening > 0.0f ? 1.0f : 0.0f;
    en_cond[CD_BELLS] = bells.ldb;
    en_cond[CD_BASS] = bass.ldb;
    en_cond[CD_FF] = cd.ff;
    en_cond[CD_CURSOR_X] = tcur.x;
    en_cond[CD_CURSOR_Y] = tcur.y;
    en_cond[CD_ANCHOR] = (float)tcur.near;
    en_cond[CD_S_MODE] = fld.mode.x;
    en_cond[CD_S_ROOT] = fld.root.x;
    en_cond[CD_FIELD_SHIFT] = (float)fld.shift;
    en_cond[CD_FIELD_MOVE] = (float)fld.move;
    en_cond[CD_BELL_ACT] = BELL_ACT + cd.bact.x + BELL_BRIDGE * cd.alone;
    en_cond[CD_ALONE] = cd.alone;
    en_cond[CD_FIELD_PCS] = (float)rot12(pool, froot);
    en_cond[CD_EBB] = cd.ebb;
    en_cond[CD_PAD_DIP] = cd.dip;       /* 2026-11-13: the pad's recess, dB */
    en_cond[CD_PAD_EXPO] = cd.expo;     /* 2026-11-14: the pad over its company to the ear, dB; the hold, dB */
    en_cond[CD_PAD_HOLD] = cd.hold;
}

static void regrow(void)                        /* a new piece: fresh loops, home chord, the weather afresh */
{
    int l;
    conductor_reset();
    for (l = 0; l < EN_NLOOP; l++) {
        loop_regrow(l);
        loop_rest[l] = 0;
    }
    deg = 0;
    pad_retarget();
    pad_handoff();
    bass_to(chord_pcs[0]);
    ground_to(chord_pcs[0]);
    bass.rest = 0;
    en_pulse_rest = 0;
#if EN_PULSE
    pulse_rest = 0;
    drums_pattern();
#endif
    drift_reset();
    change_pending = 0;
}

static void notes_off(void)
{
    int i;
    for (i = 0; i < 4; i++)
        en_midi_out(0xB0 | (uint32_t)i, 123, 0);
    en_midi_out(0xB9, 123, 0);
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NBELL; i++)
        bell[i].on = 0;
#if EN_PULSE
    for (i = 0; i < EN_NDRUM; i++)
        drum[i].on = 0, drum[i].wait = 0;
#endif
    for (i = 0; i < EN_NPAD; i++) {
        pad[i].amp = 0.0f;
        pad[i].lin = 0.0f;
        pad[i].ldb = -60.0f;
        pad[i].st = PV_OFF;
        pad[i].midi_note = 0;
    }
    bass.amp = 0.0f;
    bass.ldb = -60.0f;
    bass.lin = 0.0f;
    ground_db = -60.0f;
    ground_lin = 0.0f;
    ground_on = 0;
    notes_off();
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    {   /* the Choir's cycles from their harmonics (SN is CHOIR_LEN: harmonic h at sample i is the sine table at h i) */
        int k, h;
        for (k = 0; k < CHOIR_NCYC; k++)
            for (i = 0; i <= CHOIR_LEN; i++) {
                float x = 0.0f;
                for (h = 1; h <= CHOIR_NH; h++)
                    x += CHOIR_H[k][h - 1] * sint[(h * i) & (SN - 1)];
                choir_t[k][i] = x;
            }
    }
    plate_init();
    for (i = 0; i < TAPE_N; i++)
        tape[i] = 0;
    for (i = 0; i < PLEX_N; i++)
        plex[i] = 0;
    tape_w = plex_w = tape_ph = 0;
    head2 = head2_t = 0.75f;
    head2_v = 0.0f;
    echo_len = 0.0f;
    q_r = q_w = 0;
    for (i = 0; i < NMOD; i++) {
        mod[i].v = mod[i].tgt = 0.0f;
        mod[i].left = 0;
    }
    for (i = 0; i < EN_NPAD; i++) {
        pad[i].note = pad[i].target = (float)(50 + 7 * i);
        pad[i].pan = PAD_PAN[i];
        pad[i].amp = 0.0f;
        pad[i].lin = 0.0f;
        pad[i].ldb = -60.0f;
        pad[i].st = PV_OFF;
        pad[i].ph[0] = 0.0f;
        pad[i].ph[1] = 0.37f;
        pad[i].ph2 = pad[i].inc2 = 0.0f;
        pad[i].ph3 = 0.41f;
        pad[i].inc3 = 0.0f;
        pad[i].lfo[3] = 0.17f * (float)i;
        pad[i].lfo[0] = 0.21f * (float)i;
        pad[i].lfo[1] = 0.13f * (float)i;
        pad[i].lfo[2] = 0.29f * (float)i;
        pad[i].vib = 0.3f * (float)i;
        pad[i].midi_note = 0;
        pad[i].hf = 1.0f;
        pad[i].hand = 0;
        pad[i].rise = 0.4f;
        pad[i].fall = 0.25f;
    }
    bass.ldb = -60.0f;
    bass.lin = 0.0f;
    bass.amp = 0.0f;
    ground_db = -60.0f;
    ground_lin = 0.0f;
    ground_on = 0;
    pad_gain = 0.0f;
    en_playing = 0;                             /* a reset is a reset (Phase 5b: a host that calls en_init again, as the tests
                                                 * do for each scenario, was still "playing", so its Play did nothing and
                                                 * the opening never ran: two minutes of creeping in; the device calls
                                                 * this once, at boot) */
    en_frozen = 0;
    cd.speed = 1.0f;
    cd.opening = 0.0f;
    cd.acc = 0;
    cd.frames = 0;
    for (i = 0; i < NLAYERS; i++)
        cd.e_dry[i] = 0.0f;
    cd.e_plate = cd.e_echo = 0.0f;
    conductor_reset();
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    pool_set(EN_MODE_POOL[par[P_MODE]]);
    for (i = 0; i < EN_NPAD; i++)
        pad[i].note = pad[i].target;
    bass.note = bass.target;
    for (i = 0; i < EN_NLOOP; i++) {
        loop_pos[i] = 0;
        loop_regrow(i);
    }
#if EN_PULSE
    drums_pattern();
#endif
    drift_reset();
    pl_decay = pl_decay_t;
    for (i = 0; i < EN_NCOND; i++)
        en_cond[i] = 0.0f;
}

/* ------------------------------------------------------------ transport --- */
static void clock_pulse(void)
{
    if (pulse == 0)
        tick();
    if (!en_ext)
        midi(0xF8, 0, 0);
    if (++pulse >= 6)
        pulse = 0;
}

static void play_start(int from_top)
{
    int i, any = 0;
    if (from_top) {
        pulse = 0;
        sixteenth = 0;
#if EN_PULSE
        for (i = 0; i < EN_NDRUM; i++)
            drum_pos[i] = 0;
#endif
        for (i = 0; i < EN_NLOOP; i++)
            loop_pos[i] = 0;
    }
    pulse_left = 0.0f;
    en_playing = 1;
    pad_gate = 1.0f;
    for (i = 0; i < EN_NPAD; i++)
        any |= pad[i].amp >= 1e-4f;
    if (!any) {                                  /* from silence: a quicker start, one voice at once */
        cd.opening = 30.0f;
        cd.since_entry = 100.0f;
        for (i = 0; i < EN_NPAD; i++) {
            pad[i].st = PV_OFF;
            pad[i].rest = 0.0f;
            pad[i].midi_note = 0;
        }
        pad_enter(&pad[0], 0);
    }
    if (bass.amp < 1e-4f)
        bass.note = bass.target;
    if (!bass.rest)
        midi(0x92, bass.midi_note, 80);
    if (!en_ext)
        midi(from_top ? 0xFA : 0xFB, 0, 0);
}

static void play_stop(void)
{
#if EN_PULSE
    int d;
    for (d = 0; d < EN_NDRUM; d++)
        drum[d].wait = 0;                        /* a swung hit still waiting stays unplayed */
#endif
    en_playing = 0;
    pad_gate = 0.0f;
    if (!en_ext)
        midi(0xFC, 0, 0);
    notes_off();
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_POOL: pool_set((uint32_t)b & 0xFFFu); break;
        case C_PLAY:
            if (a && !en_playing)
                play_start(1);
            else if (!a && en_playing)
                play_stop();
            break;
        case C_NEXT:
            if (en_playing)
                change_pending = 1;
            else
                chord_change();
            break;
        case C_FREEZE: en_frozen = (uint8_t)(a != 0); break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            break;
        case C_CLOCK:
            en_ext = 1;
            ext_age = 0;
            if (a == EN_CLK_TICK) {
                if (en_playing)
                    clock_pulse();
            } else if (a == EN_CLK_START) {
                play_start(1);
            } else if (a == EN_CLK_CONTINUE) {
                if (!en_playing)
                    play_start(0);
            } else if (en_playing) {
                play_stop();
            }
            break;
        case C_PANIC: quiet(); break;
        case C_REGROW: regrow(); break;
        }
    }
}

/* --------------------------------------------------------------- render --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], rsend[BLK], esend[BLK];
static float lyl[BLK], lyr[BLK];                /* the layer being rendered, before it joins the mix */
#define PL_W(hz) ((hz) * (FM_TWO_PI / EN_SR))
static void plate_wander_step(uint32_t n)       /* once a chunk: each oscillator's rate slides to its target (1 s) */
{
    int i;
    float *const w[2] = {&pl_w, &pl_w2}, r;
    if (!pl_wander)
        return;
    for (i = 0; i < 2; i++) {
        if (pl_wleft[i] <= n) {
            pl_rng = pl_rng * 1664525u + 1013904223u;
            pl_wt[i] = PL_W(pl_wlo + pl_wspan * (float)(pl_rng >> 8) * (1.0f / 16777216.0f));
            pl_rng = pl_rng * 1664525u + 1013904223u;
            pl_wleft[i] = (uint32_t)((2.0f + 5.0f * (float)(pl_rng >> 8) * (1.0f / 16777216.0f)) * EN_SR);
        } else {
            pl_wleft[i] -= n;
        }
        *w[i] += (pl_wt[i] - *w[i]) * fm_minf((float)n * (1.0f / EN_SR), 1.0f);
    }
    r = lfo_s * lfo_s + lfo_c * lfo_c;          /* the oscillators' size holds as their rate moves */
    if (r > 1.02f || r < 0.98f) {
        r = 1.0f / fm_sqrtf(r);
        lfo_s *= r;
        lfo_c *= r;
    }
    r = lfo2_s * lfo2_s + lfo2_c * lfo2_c;
    if (r > 1.02f || r < 0.98f) {
        r = 1.0f / fm_sqrtf(r);
        lfo2_s *= r;
        lfo2_c *= r;
    }
}
#ifdef EN_HOST
static uint8_t host_mute[NLAYERS];
void (*en_host_meter_hook)(float *plate, float *echo);   /* the governor's plate and echo energies, recorded or replayed */
void (*en_host_meter_dry)(float *dry, int n);            /* and each layer's own (see governor_step) */
void en_host_plate_exc(float k) { pl_exc_k = fm_clampf(k, 0.0f, 1.0f); }
void en_host_plate_mod(float rate_hz, int wander)
{
    if (rate_hz > 0.0f)
        pl_w = pl_w2 = PL_W(fm_clampf(rate_hz, 0.05f, 6.0f));
    pl_wander = (uint8_t)(wander != 0);
    pl_wt[0] = pl_wt[1] = pl_w;
    pl_wleft[0] = pl_wleft[1] = 0;
}
void en_host_plate_range(float lo_hz, float hi_hz)   /* the wander's range (an A/B) */
{
    pl_wlo = fm_clampf(lo_hz, 0.05f, 6.0f);
    pl_wspan = fm_clampf(hi_hz - pl_wlo, 0.0f, 6.0f);
}
void en_host_mute(int layer, int on)
{
    if (layer >= 0 && layer < NLAYERS)
        host_mute[layer] = (uint8_t)(on != 0);
}
#endif

/* a layer is done: its energy for the governor (and the harness), then into the mix. The sends went to
 * rsend/esend directly; on the host a muted layer's sends are taken back by the taps below. */
static void layer_done(int k, uint32_t n)
{
    uint32_t j;
    float e = 0.0f;
    for (j = 0; j < n; j++)
        e += lyl[j] * lyl[j] + lyr[j] * lyr[j];
    cd.e_dry[k] += e;
#ifdef EN_HOST
    en_tap_dry[k] += (double)e;
    if (host_mute[k])
        return;                             /* a solo render: a muted layer's dry sound stays out of the mix */
#endif
    for (j = 0; j < n; j++) {
        mixl[j] += lyl[j];
        mixr[j] += lyr[j];
    }
}
static void layer_begin(uint32_t n)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        lyl[j] = lyr[j] = 0.0f;
}

/* the pad's bus: a 12 dB low-pass, darker than it sounds on paper (Brian Eno darkened everything: the
 * Synthi's filter below half, then tape). No chorus: a modulated delay against pure tones beats (on
 * the FM-1's speaker above all), and that read as pulsing. The width comes from the voices' places,
 * the echo's two heads and the plate. */
static float padl[BLK], padr[BLK], padm[BLK];   /* left, right, and the voices' unpanned sum for the sends */
static float plp[6];

static void pad_bus(uint32_t n)
{
    const float fc = fm_clampf(700.0f * fm_exp2f(bright * 0.03f + 1.6f * cd.br + tcur.fc), 300.0f, 9000.0f);   /* + the cursor's octaves */
    const float c = 1.0f - fm_expf(-FM_TWO_PI * fc * (1.0f / EN_SR));
    uint32_t j;
    for (j = 0; j < n; j++) {
        float m;
        plp[0] += c * (padl[j] - plp[0]);
        plp[1] += c * (plp[0] - plp[1]);
        plp[2] += c * (padr[j] - plp[2]);
        plp[3] += c * (plp[2] - plp[3]);
        plp[4] += c * (padm[j] - plp[4]);
        plp[5] += c * (plp[4] - plp[5]);
        lyl[j] += plp[1] * 0.8f;
        lyr[j] += plp[3] * 0.8f;
        /* TIDE: the sends from the unpanned sum (AMB-1 sent l + r, which the pan wobble swelled by up to
         * 3 dB); 1.35 is the mean of cos + sin over the voices' places, so the space is as full as before */
        m = plp[5] * 1.35f;
        rsend[j] += m * 0.6f;
        esend[j] += m * 0.1f;                   /* AMB-1: 0.2; the tape's comb swells a held note as it drifts */
    }
    for (j = 0; j < 6; j++)
        plp[j] = fm_flush(plp[j]);
}

static void render_pad(uint32_t n)
{
    int i;
    float lvl = 0.0f;
    uint32_t j0;
    for (j0 = 0; j0 < n; j0++)
        padl[j0] = padr[j0] = padm[j0] = 0.0f;
    const float g = pad_vol * 0.40f * cd.g[L_PAD];   /* AMB-1: 0.5; TIDE sits the pad lower among the layers (0.36 until
                                                      * Phase 5c: the crest's ceiling and the height's tilt took 3.5 dB off the
                                                      * pad's median level in a first take, its share 45% to 33%; 0.9 dB back) */
    pad_gain += (pad_gate - pad_gain) * fm_minf((float)n / ((pad_gate > 0.0f ? 2.0f : 5.0f) * EN_SR), 1.0f);
    for (i = 0; i < EN_NPAD; i++) {
        pad_t *v = &pad[i];
        float p0 = v->ph[0], p1 = v->ph[1], amp = v->amp, s0 = v->ic[0], s1 = v->ic[1], s2 = v->ic[2], s3 = v->ic[3],
              gl, gr, pn, tgt = 0.0f, idx = 0.0f, h2 = 0.12f, saw = 0.45f, p2 = v->ph2, p3 = v->ph3;
        const int shim = pl_shimmer > 0.0f;     /* the pad's life: the octave partial on its own phase */
        const float ch = 0.6f * pl_chorus, chn = 1.0f / fm_sqrtf(1.0f + ch * ch);   /* and the second oscillator */
        uint32_t j;
        int wa = 0;                             /* the Choir: the pair of cycles, and the place between */
        float wf = 0.0f;
        if (amp < 1e-5f && (pad_gain < 1e-4f || v->lin <= 0.0f)) {
            v->amp = 0.0f;
            en_pad_amp[i] = 0.0f;
            continue;
        }
        pn = fm_clampf(v->pan + 0.35f * motion * mod[(i + 3) % NMOD_PAD].v, -1.0f, 1.0f);
        gl = fm_cosf((pn + 1.0f) * (FM_PI / 4.0f)) * g;
        gr = fm_sinf((pn + 1.0f) * (FM_PI / 4.0f)) * g;
        for (j = 0; j < n; j++) {
            float x, v1, v2, v3, b1, l1, b2;
            if (!(j & 31u)) {
                pad_coefs(v, i);
                tgt = v->lin * pad_gain * v->hf;
                idx = v->idx;                    /* the Rhodes blooms: more overtones as the voice crests (in turns) */
                h2 = tcur.h2 * v->hk;            /* the cursor's second harmonic and saw (Phase 4); less of it higher up */
                saw = tcur.saw;
                if (v->voices == 2) {
                    const float pos = fm_clampf(0.4f + 0.45f * (0.6f * sine(v->lfo[2]) + 0.4f * sine(v->lfo[3]))
                                                + 0.15f * v->crest, 0.0f, 0.9999f) * (float)(CHOIR_NCYC - 1);
                    wa = (int)pos;
                    wf = pos - (float)wa;
                }
#ifdef EN_HOST
                if (pv_mode == 2 && wt_n > 1) {
                    const float pos = fm_clampf(0.4f + 0.45f * (0.6f * sine(v->lfo[2]) + 0.4f * sine(v->lfo[3]))
                                                + 0.15f * v->crest, 0.0f, 0.9999f) * (float)(wt_n - 1);
                    wa = (int)pos;
                    wf = pos - (float)wa;
                }
#endif
            }
#ifdef EN_HOST
            if (pv_mode == 2 && wt_n > 1) {
                const float fp = p0 * (float)WT_LEN;
                const int k = (int)fp;
                const float fr = fp - (float)k;
                const float xa = wt[wa][k] + (wt[wa][k + 1] - wt[wa][k]) * fr;
                const float xb = wt[wa + 1][k] + (wt[wa + 1][k + 1] - wt[wa + 1][k]) * fr;
                x = (xa + (xb - xa) * wf) * 1.2f;
                p0 += v->inc[0];
                if (p0 >= 1.0f)
                    p0 -= 1.0f;
                amp += (tgt - amp) * 0.01f;
                x *= amp;
                padl[j] += x * gl;
                padr[j] += x * gr;
                padm[j] += x * g;
                continue;
            }
#endif
            if (v->voices == 2) {               /* the Choir: two cycles, cross-faded */
                const float fp = p0 * (float)CHOIR_LEN;
                const int k = (int)fp;
                const float fr = fp - (float)k;
                const float xa = choir_t[wa][k] + (choir_t[wa][k + 1] - choir_t[wa][k]) * fr;
                const float xb = choir_t[wa + 1][k] + (choir_t[wa + 1][k + 1] - choir_t[wa + 1][k]) * fr;
                x = (xa + (xb - xa) * wf) * 1.2f;
                p0 += v->inc[0];
                if (p0 >= 1.0f)
                    p0 -= 1.0f;
                amp += (tgt - amp) * 0.01f;
                x *= amp;
                padl[j] += x * gl;
                padr[j] += x * gr;
                padm[j] += x * g;
                continue;
            }
            if (!v->voices) {                   /* FM, the modulator at the anchor's ratio to the carrier (1: a Rhodes) */
                float m = sine(p1) * idx;
                x = (sine(p0 + m + 1.0f) + h2 * sine(shim ? p2 : 2.0f * p0)) * 1.2f;   /* one carrier: no beating */
                if (ch > 0.0f) {
                    x = ((sine(p0 + m + 1.0f) + ch * sine(p3 + m + 1.0f)) * chn + h2 * sine(shim ? p2 : 2.0f * p0)) * 1.2f;
                    p3 += v->inc3;
                    if (p3 >= 1.0f)
                        p3 -= 1.0f;
                }
                p0 += v->inc[0];
                if (p0 >= 1.0f)
                    p0 -= 1.0f;
                p2 += v->inc2;
                if (p2 >= 1.0f)
                    p2 -= 1.0f;
                p1 += v->inc[1];
                if (p1 >= 1.0f)
                    p1 -= 1.0f;
                amp += (tgt - amp) * 0.01f;
                x *= amp;
                padl[j] += x * gl;
                padr[j] += x * gr;
                padm[j] += x * g;
                continue;
            }
            /* as Discreet Music's Synthi: a mellow triangle (a sine's neighbour), a little saw in it */
            x = (p0 < 0.5f ? 4.0f * p0 - 1.0f : 3.0f - 4.0f * p0) * 1.4f + saw * blep_saw(p0, v->inc[0]) + 0.6f * sine(p0);
            if (ch > 0.0f) {
                x = (x + ch * ((p3 < 0.5f ? 4.0f * p3 - 1.0f : 3.0f - 4.0f * p3) * 1.4f + saw * blep_saw(p3, v->inc3)
                               + 0.6f * sine(p3))) * chn;
                p3 += v->inc3;
                if (p3 >= 1.0f)
                    p3 -= 1.0f;
            }
            p0 += v->inc[0];
            if (p0 >= 1.0f)
                p0 -= 1.0f;
            v3 = x - s1;                         /* the first formant: TPT SVF (Zavalishin), bp and lp */
            v1 = v->c1[0] * s0 + v->c1[1] * v3;
            v2 = s1 + v->c1[1] * s0 + v->c1[2] * v3;
            s0 = 2.0f * v1 - s0;
            s1 = 2.0f * v2 - s1;
            b1 = v1 * v->c1[3];
            l1 = v2;
            v3 = x - s3;                         /* the second */
            v1 = v->c2[0] * s2 + v->c2[1] * v3;
            v2 = s3 + v->c2[1] * s2 + v->c2[2] * v3;
            s2 = 2.0f * v1 - s2;
            s3 = 2.0f * v2 - s3;
            b2 = v1 * v->c2[3];
            amp += (tgt - amp) * 0.01f;
            x = (b1 + 0.5f * b2 + 0.45f * l1) * amp;
            padl[j] += x * gl;                   /* to the pad's bus: smoothed below */
            padr[j] += x * gr;
            padm[j] += x * g;
        }
        v->ic[0] = fm_flush(s0);
        v->ic[1] = fm_flush(s1);
        v->ic[2] = fm_flush(s2);
        v->ic[3] = fm_flush(s3);
        v->amp = amp;
        v->ph[0] = p0;
        v->ph[1] = p1;
        v->ph2 = p2;
        v->ph3 = p3;
        en_pad_note[i] = (uint8_t)v->note;
        en_pad_amp[i] = amp;
        lvl = fm_maxf(lvl, amp);
    }
    en_pad_level = lvl;
    pad_bus(n);
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    const float g = bass_vol * 0.6f * cd.g[L_BASS], tgt = bass.lin;   /* AMB-1: 0.42 */
    float ph = bass.ph, ph2 = bass_ph2, amp = bass.amp, inc;
    if (amp < 1e-5f && tgt <= 0.0f) {
        bass.amp = 0.0f;
        en_bass_level = 0.0f;
        return;
    }
    bass.note += (bass.target - bass.note) * fm_minf((float)n * glide_k, 1.0f);   /* Glide */
    inc = mtof(bass.note) * (1.0f / EN_SR);
    const float lpa = fm_minf(25.0f * inc, 0.9f);
    for (j = 0; j < n; j++) {
        float x, t = ph - (float)(int)ph;
        if (bass_type == 1)                      /* Triangle: round and hollow, with the sub-octave */
            x = (t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t) * 0.9f + sine(ph * 0.5f + 0.25f) * 0.3f;
        else if (bass_type == 2) {               /* Drone: a saw, softened to a few times its pitch, over the sub */
            bass_lp += ((2.0f * t - 1.0f) - bass_lp) * lpa;
            x = bass_lp * 0.9f + sine(ph * 0.5f + 0.25f) * 0.45f;
        } else if (bass_type == 3) {             /* Pulse: a soft square, over the sub */
            bass_lp += ((t < 0.5f ? 1.0f : -1.0f) - bass_lp) * lpa;
            x = bass_lp * 0.55f + sine(ph * 0.5f + 0.25f) * 0.45f;
        } else if (bass_type == 4) {             /* Void: two sines a few cents apart, beating slowly, over the sub */
            x = (sine(ph) + sine(ph2)) * 0.45f + sine(ph * 0.5f + 0.25f) * 0.4f;
        } else if (bass_type == 5) {             /* Reactor: a saw and a square blended, softened, with the octave over it */
            bass_lp += ((0.6f * (2.0f * t - 1.0f) + 0.4f * (t < 0.5f ? 1.0f : -1.0f)) - bass_lp) * lpa;
            x = bass_lp * 0.6f + sine(ph * 0.5f + 0.25f) * 0.4f + sine(ph * 2.0f) * 0.1f;
        } else                                   /* Sub: AMB-1's own */
            x = sine(ph) * 0.75f + sine(ph * 0.5f + 0.25f) * 0.45f + sine(ph * 2.0f) * 0.12f;
        ph += inc;
        if (ph >= 2.0f)                          /* two turns: the sub-octave's period */
            ph -= 2.0f;
        ph2 += inc * 1.0035f;
        if (ph2 >= 2.0f)
            ph2 -= 2.0f;
        amp += (tgt - amp) * 0.002f;             /* the level is already rate-limited: this only smooths */
        x *= amp * g;
        lyl[j] += x;
        lyr[j] += x;
        rsend[j] += x * 0.08f;
    }
    bass.ph = ph;
    bass_ph2 = ph2;
    bass.amp = amp;
    en_bass_level = amp;
}

/* Ground: the chord's root an octave under the bass (C1..B1), two sines a few cents apart (a slow
 * beat), its octave and a little of its twelfth so small speakers hear it; it enters and leaves over a
 * minute or two and sits for a few, rarely */
static struct {
    float ph, ph2, note, amp;
} ground;
static void render_ground(uint32_t n)
{
    uint32_t j;
    const float g = pulse_vol * 0.5f * cd.g[L_GROUND], tgt = ground_lin;
    float ph = ground.ph, ph2 = ground.ph2, amp = ground.amp, inc;
    if (amp < 1e-5f && tgt <= 0.0f) {
        ground.amp = 0.0f;
        return;
    }
    if (ground.note <= 0.0f)
        ground.note = ground_target;
    ground.note += (ground_target - ground.note) * fm_minf((float)n * (1.0f / (2.0f * EN_SR)), 1.0f);
    inc = mtof(ground.note) * (1.0f / EN_SR);
    for (j = 0; j < n; j++) {
        float x = (sine(ph) + sine(ph2)) * 0.5f + 0.6f * sine(2.0f * ph) + 0.1f * sine(3.0f * ph2);
        ph += inc;
        ph2 += inc * 1.0023f;                    /* 4 cents: a beat every few seconds */
        if (ph >= 1.0f)
            ph -= 1.0f;
        if (ph2 >= 1.0f)
            ph2 -= 1.0f;
        amp += (tgt - amp) * 0.002f;
        x *= amp * g;
        lyl[j] += x;
        lyr[j] += x;
        rsend[j] += x * 0.15f;
    }
    ground.ph = ph;
    ground.ph2 = ph2;
    ground.amp = amp;
}

static void render_bells(uint32_t n)
{
    int i;
    /* the layer's gain: 0.45 with no lift from the governor, which is where Phase 4's 0.32 sat with its +3 dB lift held
     * nearly all the time (AMB-1 0.62, Phase 3 0.8, Phase 3b 0.5); the governor's part (it only turns them down: the
     * mix too loud, their share, and the garden's company, governor_step), and the layer's breathing level */
    const float g = bell_vol * 0.45f * cd.g[L_BELLS] * bells.lin;
    for (i = 0; i < NBELL; i++) {
        bell_t *v = &bell[i];
        float cph = v->cph, mph = v->mph, amp = v->amp, idx = v->idx, att = v->att;
        const float arate = v->arate;
        uint32_t j = 0;
        if (!v->on)
            continue;
        if (i >= SHED_NBELL[en_shed & 3u])      /* the guard: a bell over the count dies away in a quarter second */
            v->adec = fm_minf(v->adec, 0.999374f);
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j = (uint32_t)v->wait;
            v->wait = 0;
        }
        for (; j < n; j++) {
            float m = sine(mph) * idx, x;
            float p = cph + m + 4.0f;            /* + 4 turns: positive for the table */
            x = sine(p) * amp * att * g;
            lyl[j] += x * v->gl;
            lyr[j] += x * v->gr;
            rsend[j] += x * 0.7f;
            esend[j] += x * 0.85f;
            cph += v->cinc;
            mph += v->minc;
            if (cph >= 1.0f)
                cph -= 1.0f;
            if (mph >= 1.0f)
                mph -= 1.0f;
            amp *= v->adec;
            idx *= v->idec;
            att += (1.0f - att) * arate;         /* the strike (bell_voice) */
        }
        v->cph = cph;
        v->mph = mph;
        v->amp = amp;
        v->idx = idx;
        v->att = att;
        if (amp < 0.0008f) {
            v->on = 0;
            midi(0x81, v->note, 0);
        }
    }
}

#if EN_PULSE
/* a hat: the metal and noise, high-passed (Chamberlin SVF, ~4 to 10 kHz by Hats); no pitch, little echo */
static void render_hat(drum_t *v, uint32_t n, float g, float pl, float pr)
{
    uint32_t j;
    int k;
    float x1, x2, amp, att;
    if (!v->on)
        return;
    x1 = v->x1, x2 = v->x2, amp = v->amp, att = v->att;
    for (j = 0; j < n; j++) {
        float in = 0.0f, hp, x;
        for (k = 0; k < 4; k++) {
            v->mph[k] += HAT_INC[k];
            if (v->mph[k] >= 1.0f)
                v->mph[k] -= 1.0f;
            in += v->mph[k] < 0.5f ? 0.25f : -0.25f;
        }
        noise = noise * 1664525u + 1013904223u;
        in = 0.5f * in + 0.6f * (float)(int32_t)noise * (1.0f / 2147483648.0f);
        hp = in - x2 - 0.8f * x1;
        x1 += hat_f * hp;
        x2 += hat_f * x1;
        x = hp * amp * att * g;
        lyl[j] += x * pl;
        lyr[j] += x * pr;
        rsend[j] += x * 0.2f;
        esend[j] += x * 0.08f;
        amp *= v->adec;
        att += (1.0f - att) * 0.3f;
    }
    v->x1 = fm_flush(x1);
    v->x2 = fm_flush(x2);
    v->amp = amp;
    v->att = att;
    v->on = amp > 0.0005f;
}

static void render_drums(uint32_t n)
{
    uint32_t j;
    const float g = pulse_vol * 1.1f;
    drum_t *v;
    v = &drum[0];                                /* thump: a sine falling to its note */
    if (v->on) {
        float ph = v->ph, inc = v->inc, amp = v->amp, att = v->att;
        for (j = 0; j < n; j++) {
            float x = sine(ph) * amp * att * g;
            lyl[j] += x;
            lyr[j] += x;
            rsend[j] += x * 0.1f;
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            inc += (v->inc_end - inc) * 0.0012f;
            amp *= v->adec;
            att += (1.0f - att) * 0.02f;
        }
        v->ph = ph;
        v->inc = inc;
        v->amp = amp;
        v->att = att;
        v->on = amp > 0.0005f;
    }
    render_hat(&drum[1], n, g * 0.2f, 0.6f, 1.0f);    /* closed: a little right */
    render_hat(&drum[2], n, g * 0.14f, 1.0f, 0.7f);   /* open: a little left */
}
#endif

#ifdef EN_HOST
/* TIDE: the harness's taps (amb.h). Each layer's dry energy comes from layer_done(); its sends are the
 * difference it makes to the send buses, which a muted layer gives back (so a solo render is one layer
 * with its own echo and reverb, and generation, the governor included, is unchanged). */
double en_tap_dry[EN_TAP_NLAYERS], en_tap_rsend[EN_TAP_NLAYERS], en_tap_esend[EN_TAP_NLAYERS];
double en_tap_echo, en_tap_plate;
uint32_t en_tap_frames;
static float taps[BLK], tape_[BLK];
static void tap_begin(uint32_t n)
{
    memcpy(taps, rsend, n * sizeof(float));
    memcpy(tape_, esend, n * sizeof(float));
}
static void tap_end(int k, uint32_t n)
{
    uint32_t j;
    double s = 0.0, e = 0.0;
    for (j = 0; j < n; j++) {
        double c = (double)(rsend[j] - taps[j]), f = (double)(esend[j] - tape_[j]);
        s += c * c;
        e += f * f;
    }
    en_tap_rsend[k] += s;
    en_tap_esend[k] += e;
    if (host_mute[k]) {                     /* a muted layer's sends go too: the solo is the layer with its own echo and
                                             * reverb. The governor would then hear a thinner garden than in the mix pass
                                             * and the passes would make different music within a minute (found
                                             * 2026-11-09: the trim 2-4 dB apart, the pad's voice count differing in two
                                             * thirds of all seconds): so a solo pass replays the mix pass's plate and
                                             * echo meter energies into the governor (en_host_meter_hook, render.c) */
        memcpy(rsend, taps, n * sizeof(float));
        memcpy(esend, tape_, n * sizeof(float));
    }
}
void en_tap_reset(void)
{
    int k;
    for (k = 0; k < EN_TAP_NLAYERS; k++)
        en_tap_dry[k] = en_tap_rsend[k] = en_tap_esend[k] = 0.0;
    en_tap_echo = en_tap_plate = 0.0;
    en_tap_frames = 0;
}
int en_tap_param(int p) { return (p >= 0 && p < P_NPARAMS) ? par[p] : 0; }
int en_tap_bells_on(void)
{
    int i, c = 0;
    for (i = 0; i < NBELL; i++)
        c += bell[i].on != 0;
    return c;
}
int en_tap_drums_on(void) { return ground.amp > 1e-4f; }
int en_tap_deg(void) { return deg; }
int en_tap_loop_notes(void)
{
    int l, c = 0;
    for (l = 0; l < EN_NLOOP; l++)
        c += loop_count(l);
    return c;
}
int en_tap_pad_state(int i) { return (i >= 0 && i < EN_NPAD) ? pad[i].st : 0; }
#define TAP_BEGIN(n) tap_begin(n)
#define TAP_END(k, n) tap_end(k, n)
#else
#define TAP_BEGIN(n) ((void)0)
#define TAP_END(k, n) ((void)0)
#endif

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    const float rg = rev_send * rev_in_gain * (1.0f + 0.3f * cd.space.x), eg = echo_send;
    float ep = 0.0f, ee = 0.0f;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = esend[j] = 0.0f;
    layer_begin(n);
    TAP_BEGIN(n);
    render_pad(n);
    layer_done(L_PAD, n);
    TAP_END(L_PAD, n);
    layer_begin(n);
    TAP_BEGIN(n);
    render_bass(n);
    layer_done(L_BASS, n);
    TAP_END(L_BASS, n);
    layer_begin(n);
    TAP_BEGIN(n);
    render_bells(n);
    layer_done(L_BELLS, n);
    TAP_END(L_BELLS, n);
    layer_begin(n);
    TAP_BEGIN(n);
#if EN_PULSE
    render_drums(n);
#else
    render_ground(n);
#endif
    layer_done(L_GROUND, n);
    TAP_END(L_GROUND, n);
    for (j = 0; j < n; j++) {
        float wl, wr, l, r, el, er, xl, xr, al, ar, pl, pr;
        echoes(esend[j] * eg, &el, &er, &xl, &xr);
        reverb((rsend[j] * rg + (el + er + xl + xr) * 0.35f * rev_send) * 0.8f, &wl, &wr);
        al = 0.75f * el + 0.3f * xl;
        ar = 0.75f * er + 0.3f * xr;
        pl = 1.3f * wl;
        pr = 1.3f * wr;
        ee += al * al + ar * ar;
        ep += pl * pl + pr * pr;
        l = (mixl[j] + al + pl) * 0.36f * gain;
        r = (mixr[j] + ar + pr) * 0.36f * gain;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
    cd.e_echo += ee;
    cd.e_plate += ep;
    cd.frames += n;
#ifdef EN_HOST
    en_tap_echo += (double)ee;
    en_tap_plate += (double)ep;
    en_tap_frames += n;
#endif
}

static void control(uint32_t n)                 /* once a chunk: the fast things; the conductor on its step */
{
    int i;
    mods_step(n);
    heads_step(n);
    plate_wander_step(n);
    pl_decay += ((en_frozen ? 0.9995f : pl_decay_t) - pl_decay) * 0.002f;
    pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
    rev_in_gain += ((en_frozen ? 0.0f : 1.0f) - rev_in_gain) * 0.01f;
    cd.acc += n;
    while (cd.acc >= CTRL_N) {
        cd.acc -= CTRL_N;
        conductor_step();
    }
    for (i = 0; i < EN_NLOOP; i++)
        en_loop_glow[i] *= 0.985f;
    for (i = 1; i < EN_NDRUM; i++)
        en_drum_glow[i] *= 0.95f;
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    if (en_ext && ++ext_age > 100u)
        en_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        int timed = en_playing && !en_ext;
        if (timed) {
            if (pulse_left <= 0.0f) {
                clock_pulse();
                pulse_left += step_len * (1.0f / 6.0f);
            }
            if ((float)k > pulse_left)
                k = (uint32_t)pulse_left + 1u;
        }
        if (k > n)
            k = n;
#if EN_PULSE
        {
            int32_t w = drums_wait();            /* a swung hit lands on its sample */
            if (w > 0 && (uint32_t)w < k)
                k = (uint32_t)w;
        }
#endif
        if (timed)
            pulse_left -= (float)k;
        control(k);
        render_chunk(out, k, gain);
#if EN_PULSE
        drums_wait_step((int32_t)k);
#endif
        out += 2 * k;
        n -= k;
    }
}
