/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Tomorrow's Man (TIDE) */
/* The instrument: a generative ambient garden. Four layers draw on one pool of notes (a scale:
 * the root plus up to eleven more pitch classes) and one chord that drifts slowly through it:
 *
 *   PAD     four breathing voices: each swells in, hangs and fades on its own cycle (unequal lengths,
 *           as the loops of Music for Airports), taking its note of the chord as it starts, so the
 *           chord forms and dissolves. Two colours, by piece: a filtered Rhodes (FM, blooming brighter
 *           as it swells) or Voices (saws through two formants, as "2/1"); then a soft low-pass and an
 *           ensemble chorus. The chord moves every DRIFT (20 s .. 5 min): voices on shared
 *           notes hold, the others breathe in anew.
 *   BELLS   soft two-operator FM bells played by four tape loops of unequal length (13, 17, 23, 29
 *           quarter notes), each holding a short phrase that shifts against the others, each its own
 *           shade of bell. EVOLVE changes a note now and then; DENSITY sets how many notes a loop holds.
 *   BASS    a sine and its sub-octave on the chord's root (now and then its fifth), gliding.
 *   GROUND  (TIDE, in the pulse's place) a rare low drone on the chord's root, an octave under the bass,
 *           in and out over a minute or two, a few minutes at a time.
 *
 * Over it all (TIDE), the CONDUCTOR: walkers on four timescales (notes, cells, weather of minutes,
 * seasons of hours) move the pad's voices, their breathing, the bells' count, the bass's and Ground's
 * presence, the brightness, the register and the space; a governor holds the mix near the DENSITY
 * target (from above only: the piece may grow quieter) and lets no layer lead for long; the harmonic field
 * (the scale and its root) moves over hours, a pitch class at a time; nothing moves faster than the swell limit. DRIFT is the
 * speed of all of it (fully down holds). AMB-1's arc and scheduled breath are gone; with EVOLVE a bell
 * loop still changes a note or sits out a pass.
 * Then, after Discreet Music's chain, an Echoplex (160 ms, fed back) and a tape loop (to ~5.6 s, two
 * heads, each repeat darker), and Dattorro's plate (from FoMni), fed generously: the space is most of
 * what is heard.
 * FREEZE holds the chord and lets the plate ring on forever.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state below marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NLOOP 4                     /* bell tape loops */
#define EN_LOOP_MAX 29
#define EN_NDRUM 3                     /* pulse: thump, closed hat, open hat */
#define EN_NPAD 4                      /* pad voices */

/* modes: pools relative to the root (bit i: root + i semitones) */
enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];
extern const uint8_t EN_LOOP_LEN[EN_NLOOP];
extern const uint8_t EN_DRUM_N[EN_NDRUM];

/* parameters: value ranges in en_param_info() */
enum {
    P_PAD, P_BELLS, P_BASS, P_PULSE,                  /* layer levels */
    P_DENSITY, P_EVOLVE, P_DRIFT, P_HITS,             /* motion */
    P_ECHO, P_ETIME, P_REVERB, P_SIZE,                /* space */
    P_BRIGHT, P_SWELL, P_BTONE, P_MOTION,             /* tone */
    P_TUNE, P_MIDI, P_TEMPO, P_ROOT, P_MODE, P_BOCT,  /* setup, panel encoders, bell octave */
    P_PADTONE,                                        /* the pad's colour, set by the piece: Rhodes, Voices */
    P_BASSOCT, P_GLIDE, P_FIFTH,                      /* bass: its octave, its glide, how often it leans on the fifth */
    P_SWING, P_HATS,                                  /* pulse: swing, the hats' colour */
    P_MOOD, P_SPACE,                                  /* TIDE Phase 6, the main page's two new knobs, each an offset around
                                                       * the piece's own values (0: the piece as its recipe has it). Mood:
                                                       * dark .. bright, the pad's tone and the lean of the harmonic field.
                                                       * Space: dry and near .. wet and far, the reverb, the echo, the decay */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (10 bytes) */

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_pool(uint32_t pool);                   /* the scale, relative to the root (bit 0 always set) */
void en_play(int on);                          /* the garden grows / rests (the tails ring on) */
void en_next(void);                            /* the next chord: on the next bar (now while resting) */
void en_freeze(int on);
void en_seed(uint32_t s);                      /* stir some entropy into the generators */
void en_panic(void);                           /* everything quiet, now */
void en_regrow(void);                          /* a new piece: fresh loops, the home chord */
/* MIDI clock in (as FoMni): ticks drive the 1/16 grid (6 a step) while they keep coming */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* SAMSARA Phase 7: the guard (the kit's rule 6: thin voices before the audio glitches). The main loop tells the
 * engine the audio load (per cent of a block's time, smoothed; the time in ms), an overrun flags itself from the
 * render, and the engine sheds in three steps, each through an envelope, so the guard itself is never a glitch:
 * 1 the bells ring four at a time at most (the others die away in a quarter second), 2 the pad sings two voices at
 * most and three bells, 3 one voice and two bells. It lets go a step every 20 s of calm. A desktop render and the
 * simulator never call it, so it changes no sample there. */
extern volatile uint8_t en_shed;               /* 0 (nothing shed) .. 3 */
void en_load(uint32_t pct, uint32_t ms);       /* the main loop, every frame */
void en_overload(void);                        /* the render took over 90 % of its time (any context) */
#define EN_SHED_UP 62u                         /* per cent: at or over it, a step more (0.4 s apart) */
#define EN_SHED_DOWN 50u                       /* at or under it for 20 s, a step less */

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_frozen, en_ext;
extern volatile uint8_t en_chord[EN_NPAD];     /* the chord's notes (MIDI), pad voices' targets */
extern volatile uint8_t en_chord_root;         /* the chord's root, MIDI pitch class 0..11 */
extern volatile uint8_t en_chord_n;            /* its distinct tones: 3 or 4 */
extern volatile float en_drift;                /* 0..1: how far to the next chord */
extern volatile float en_pad_level, en_bass_level, en_ground_level;
extern volatile uint8_t en_loop_pos[EN_NLOOP];
extern volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];   /* -1 rest, else the slot's offset */
extern volatile float en_loop_glow[EN_NLOOP];  /* 0..1: a bell just rang from this loop */
extern volatile uint8_t en_drum_pos[EN_NDRUM];
extern volatile uint32_t en_drum_pat[EN_NDRUM];/* bit i: a hit on step i */
extern volatile float en_drum_glow[EN_NDRUM];
extern volatile uint32_t en_bars;              /* sixteenths played since start (for tests) */
extern volatile float en_arc;                  /* TIDE: the weather's density, -1 sparse .. +1 full */
extern volatile uint8_t en_pulse_rest;         /* the pulse is sitting a few bars out */
extern volatile uint8_t en_pad_note[EN_NPAD];  /* the note each voice sings now (MIDI) */
extern volatile float en_pad_amp[EN_NPAD];     /* and how loud */
/* TIDE: the conductor's state (walkers, the governor), for the screen and the harness */
enum { CD_BRIGHT, CD_DENSITY, CD_REGISTER, CD_SPACE, CD_CHORD, CD_PRES_PAD, CD_PRES_BELLS, CD_PRES_BASS,
       CD_PRES_GROUND, CD_S_SPEED, CD_S_DENSITY, CD_S_BRIGHT, CD_SPEED, CD_LEVEL20, CD_LEVEL60, CD_TARGET, CD_TRIM,
       CD_SHARE_PAD, CD_SHARE_BELLS, CD_SHARE_BASS, CD_SHARE_GROUND, CD_GAIN_PAD, CD_GAIN_BELLS, CD_GAIN_BASS,
       CD_GAIN_GROUND, CD_PAD_TARGET, CD_PAD_VOICES, CD_GROUND, CD_OPENING, CD_BELLS, CD_BASS, CD_FF,
       CD_CURSOR_X, CD_CURSOR_Y, CD_ANCHOR, CD_S_MODE, CD_S_ROOT, CD_FIELD_SHIFT, CD_FIELD_MOVE,
       CD_BELL_ACT, CD_ALONE, CD_FIELD_PCS, CD_EBB, CD_PAD_DIP, CD_PAD_EXPO, CD_PAD_HOLD, EN_NCOND };   /* Phase 5b: the bells' activity (log2), how thin the garden
                                                           * under them is (0..1), the field's pitch classes (a 12-bit set, C = bit 0);
                                                           * Phase 5c: the ebb, dB (0: the tide is in);
                                                           * 2026-11-14: the pad over its company as the ear weighs them, dB, and
                                                           * the hold on the pad's gain that answers it, dB (0: none) */
extern const char *const EN_COND_NAME[EN_NCOND];
extern volatile float en_cond[EN_NCOND];
const char *en_anchor_name(void);              /* TIDE: the timbre cursor's nearest anchor (Phase 4) */
/* TIDE Phase 5: the harmonic field as it is now (the player's Root and scale are home; the field moves from there
 * over hours, a pitch class at a time): its root (a pitch class), its scale relative to that root, its name */
extern volatile uint8_t en_field_root;
extern volatile uint16_t en_field_pool;
const char *en_field_name(char *buf16);        /* "D Dorian", "A Aeolian"; a scale with no name as "D 0x6AD" */

/* the pure parts, for the tests and the screen */
uint32_t en_euclid(int k, int n, int rot);     /* Bjorklund's pattern, as a bitmask (bit 0 = step 0) */
int en_scale(uint32_t pool, int *steps);       /* the pool's pitch classes (relative), ascending; count */
/* the chord on scale degree deg: its tones (MIDI pitch classes, absolute), 3 or 4 of them; count */
int en_chord_tones(uint32_t pool, int root, int deg, int *pcs);
/* voice leading: new pad notes for the tones, each voice moving as little as possible, all in lo..hi */
void en_voice_lead(const int *from, const int *pcs, int ntones, int *to, int lo, int hi);
const char *en_chord_name(const int *pcs, int n, char *buf);   /* "Em7", "Dsus4", "C" ... */

/* the pieces: recipes for every parameter (Tune and MIDI out included, but they are the player's) */
#define EN_NPIECES 4
const char *en_piece_name(int i);
void en_piece(int i, int16_t *par, uint16_t *pool);


#ifdef EN_HOST
/* TIDE: taps for the desktop render harness (tools/render.c), host builds only. Energy (sums of squares,
 * L plus R, in the mix's float scale before the output gain) added since en_tap_reset(): each layer's dry
 * part, its reverb send and its echo send, and the echoes' and the plate's returns. Nothing here is
 * compiled for the device. */
enum { EN_TAP_PAD, EN_TAP_BELLS, EN_TAP_BASS, EN_TAP_PULSE, EN_TAP_NLAYERS };   /* EN_TAP_PULSE: Ground */
extern double en_tap_dry[EN_TAP_NLAYERS], en_tap_rsend[EN_TAP_NLAYERS], en_tap_esend[EN_TAP_NLAYERS];
extern double en_tap_echo, en_tap_plate;
extern uint32_t en_tap_frames;
void en_tap_reset(void);
int en_tap_param(int p);                       /* a parameter as the engine has applied it */
int en_tap_bells_on(void);                     /* bell voices sounding */
int en_tap_drums_on(void);                     /* Ground sounding (the pulse's slot) */
int en_tap_pad_state(int i);                   /* a pad voice: 0 off, 1 singing, 2 dissolving */
void en_host_mute(int layer, int on);          /* mute a layer's output after the governor has heard it (solo renders) */
void en_host_plate_exc(float k);               /* the plate's modulation depth, 0..1 of AMB-1's (an A/B, TESTING #18) */
void en_host_plate_range(float lo_hz, float hi_hz);   /* the wandering plate's range of rates, Hz (Phase 5b: 0.25..1.3) */
void en_host_plate_mod(float rate_hz, int wander);   /* its rate (0: as it is, 0.70 Hz), and the wandering mode (an A/B) */
void en_host_pad_turn(float lo, float hi, float rlo, float rhi);   /* the pad's turn and recess, real seconds (the engine's:
                                                * 75..210 and 20..70); lo <= 0: no turns, v1's pad (an A/B) */
/* the governor's plate and echo energies each conductor step, to record (the mix pass) or replace (a solo pass):
 * with the muted layers' sends gone the effects return less, and the governor would steer differently */
void en_host_pad_range(int lo, int hi);        /* the pad's window, MIDI notes before the register walker (the engine's: 48 77) */
extern void (*en_host_meter_hook)(float *plate, float *echo);
/* each layer's own energy as well: with both replayed, a pass plays the recorded performance note for note whatever
 * its pad voice sounds like (the pad's life, an A/B of one moment under several voices) */
extern void (*en_host_meter_dry)(float *dry, int n);
void en_host_pad_life(float breath, float shimmer, float wow, float vib, float chorus);   /* the pad's life, each 0 (the engine's) to 1 */
void en_host_pad_voice(int mode);              /* the pad's voice (an A/B): 0 the engine's, 1 Glass as a Rhodes, 2 a wavetable */
void en_host_pad_table(int k, const float *t); /* its cycle k of up to 4: 2048 samples, band-limited and levelled by the host */
void en_host_pad_company(float line, float most, float call);   /* the pad's company (an A/B): the pad is held to `line` dB over
                                                * the rest as the ear weighs them, by at most `most` dB (0: no hold, the
                                                * engine's); the bells are called after `call` seconds of a voice left alone (0: never) */
int en_tap_deg(void);                         /* the chord's scale degree */
int en_tap_loop_notes(void);                   /* notes held by the four loops together */
#endif
