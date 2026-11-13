/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Tomorrow's Man (TIDE). Drives Charles Vestal's AMB-1 engine (app/dsp/amb.c). */
/* TIDE's desktop render harness: the engine alone (the same DSP and generation the FM-1 runs), driven
 * from a seed and a knob log, rendered faster than real time to a stereo WAV at the device's rate and a
 * per-second CSV of its internal state. No simulator, no clock: the same script and seed give the same
 * samples every time, here and on the device.
 *
 *   cc -O2 -ffp-contract=off -std=c99 -DEN_HOST -Iapp/dsp -Ibase/include -o render tools/render.c app/dsp/amb.c -lm
 *   render --script knobs.txt --seconds 14400 --wav out.wav --csv out.csv [--master 2048] [--bits 16|24]
 *          [--solo pad|bells|bass|pulse] [--events out.txt] [--env out.env] [--quiet]
 * --env writes the output's mean square (L and R averaged) every 100 ms, one value a line: the envelope at
 * 10 Hz for the report's modulation spectrum (the per-second CSV aliases the plate's 0.7 Hz ripple to 3.3
 * and 9.4 s; found 2026-11-09).
 *
 * The knob log: one command a line, an optional time first (seconds, m:ss, h:mm:ss, or 90s/5m/2h; a line
 * without a time keeps the last one), '#' comments.
 *   piece Garden | piece 2        the piece's recipe (all parameters, its scale) and fresh loops (en_regrow)
 *   seed 1107                     stir the generator (16 bits reach the engine; the engine starts from a constant)
 *   play | stop | next | regrow | panic | freeze on|off
 *   set Bright 70 | set echo_time 3   a parameter by name (case and spaces/underscores don't matter)
 *   mode Dorian | mode 1          the mode and its pool;  root D | root 2;  pool 0xAAD (bits: root + i semitones)
 *   master 2048                   the MASTER pot's position, 0..4096 (the simulator's default is 2048)
 *   plate_exc 0.5                 host only: the plate's modulation depth, 0..1 of AMB-1's (an A/B for the ear)
 *   plate_rate 0.7 | plate_wander host only: a steady modulation rate for the plate, in Hz (AMB-1's: 0.7), or back to the
 *                                 engine's own (Phase 5b: each half of the tank on a rate that wanders 0.25-1.3 Hz)
 *   plate_range 0.5 1.3           host only: the wandering plate's range of rates, Hz
 *   pad_turn 75 210 20 70         host only: the pad's turn (seconds with a voice up) and its recess (seconds unheard);
 *                                 "pad_turn 0": no turns, the pad as v1 had it (an A/B). Before "play"
 *   pad_range 48 77               host only: the pad's window, MIDI notes, before the register walker (an A/B).
 *                                 Before "play"
 * --solo mutes the other three layers (en_host_mute: their dry sound and their sends), so a solo render's output
 * is that layer alone with its own echo and reverb, and the four solo renders sum to the mix (up to the output's
 * soft clip). The governor would then hear a thinner garden than in the mix pass and the passes would drift apart
 * (found 2026-11-09: within a minute), so the mix pass writes its plate and echo meter energies (--meter-out) and a
 * solo pass replays them into the governor (--meter-in): same generation, same music, in every pass. The pulse
 * slot is TIDE's Ground layer ("pulse" is kept as its column name). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include "amb.h"

#define BLOCK 256u
#define SR 44100u

/* ------------------------------------------------------------ note events --- */
static uint32_t notes[4];                      /* pad, bells, bass, pulse: note-ons since the last row */
static FILE *events;
static uint64_t frame_now;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    int ch = (int)(st & 0x0Fu), k = -1;
    if ((st & 0xF0u) != 0x90u || d2 == 0)
        return;
    k = ch == 0 ? 0 : ch == 1 ? 1 : ch == 2 ? 2 : (ch == 3 || ch == 9) ? 3 : -1;   /* 3: Ground (TIDE); 9: AMB-1's pulse */
    if (k < 0)
        return;
    notes[k]++;
    if (events)
        fprintf(events, "%.3f %s %u %u\n", (double)frame_now / (double)SR,
                k == 0 ? "pad" : k == 1 ? "bell" : k == 2 ? "bass" : "ground", (unsigned)d1, (unsigned)d2);
}

/* ------------------------------------------------------------------- WAV --- */
/* RIFF with a 28-byte JUNK chunk after the header; past 4 GB the header is rewritten as RF64 with a
 * ds64 chunk in the JUNK's place (EBU 3306). libsndfile and most DAWs read both. */
static FILE *wav;
static int wav_bits = 16;
static uint64_t wav_data;                      /* data bytes written */
static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFFu); put16(f, v >> 16); }
static void put64(FILE *f, uint64_t v) { put32(f, (uint32_t)v); put32(f, (uint32_t)(v >> 32)); }
static void wav_header(FILE *f, uint64_t data)
{
    const int rf64 = data + 80 > 0xFFFFFFF0ull, bps = wav_bits / 8;
    uint64_t riff = 4 + 8 + 28 + 8 + 16 + 8 + data;
    fseek(f, 0, SEEK_SET);
    fputs(rf64 ? "RF64" : "RIFF", f);
    put32(f, rf64 ? 0xFFFFFFFFu : (uint32_t)riff);
    fputs("WAVE", f);
    if (rf64) {
        fputs("ds64", f);
        put32(f, 28);
        put64(f, riff);
        put64(f, data);
        put64(f, data / (uint64_t)(2 * bps));
        put32(f, 0);
    } else {
        int i;
        fputs("JUNK", f);
        put32(f, 28);
        for (i = 0; i < 28; i++)
            fputc(0, f);
    }
    fputs("fmt ", f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, SR);
    put32(f, SR * 2u * (uint32_t)bps);
    put16(f, (unsigned)(2 * bps));
    put16(f, (unsigned)wav_bits);
    fputs("data", f);
    put32(f, rf64 ? 0xFFFFFFFFu : (uint32_t)data);
}
static unsigned char wbuf[BLOCK * 2 * 3];
static void wav_write(const int32_t *lr, uint32_t n)
{
    uint32_t j;
    unsigned char *p = wbuf;
    if (!wav)
        return;
    for (j = 0; j < 2 * n; j++) {
        int32_t v = lr[j];                     /* 24-bit in int32 */
        if (wav_bits == 16) {
            int32_t s = (v + 128) >> 8;        /* rounded */
            if (s > 32767)
                s = 32767;
            if (s < -32768)
                s = -32768;
            *p++ = (unsigned char)(s & 255);
            *p++ = (unsigned char)(s >> 8 & 255);
        } else {
            if (v > 8388607)
                v = 8388607;
            if (v < -8388608)
                v = -8388608;
            *p++ = (unsigned char)(v & 255);
            *p++ = (unsigned char)(v >> 8 & 255);
            *p++ = (unsigned char)(v >> 16 & 255);
        }
    }
    fwrite(wbuf, 1, (size_t)(p - wbuf), wav);
    wav_data += (uint64_t)(p - wbuf);
}

/* ------------------------------------------------------------ the script --- */
typedef struct {
    uint64_t at;                               /* frame */
    char text[96];
} cmd_t;
static cmd_t *cmds;
static int ncmds, cmds_cap;

static int parse_time(const char *s, double *out)   /* seconds; m:ss; h:mm:ss; 90s 5m 2h 1.5h */
{
    char *end;
    double parts[3];
    int n = 0;
    const char *p = s;
    if (*p == '@')
        p++;
    if (!isdigit((unsigned char)*p) && *p != '.')
        return 0;
    for (;;) {
        parts[n++] = strtod(p, &end);
        if (end == p)
            return 0;
        p = end;
        if (*p == ':' && n < 3) {
            p++;
            continue;
        }
        break;
    }
    if (n == 1) {
        double m = 1.0;
        if (*p == 's')
            p++;
        else if (*p == 'm')
            m = 60.0, p++;
        else if (*p == 'h')
            m = 3600.0, p++;
        *out = parts[0] * m;
    } else if (n == 2) {
        *out = parts[0] * 60.0 + parts[1];
    } else {
        *out = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    }
    return *p == 0;
}

static void add_cmd(uint64_t at, const char *text)
{
    if (ncmds == cmds_cap) {
        cmds_cap = cmds_cap ? cmds_cap * 2 : 64;
        cmds = (cmd_t *)realloc(cmds, (size_t)cmds_cap * sizeof(cmd_t));
    }
    cmds[ncmds].at = at;
    strncpy(cmds[ncmds].text, text, sizeof cmds[ncmds].text - 1);
    cmds[ncmds].text[sizeof cmds[ncmds].text - 1] = 0;
    ncmds++;
}

static int read_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    double t = 0.0;
    if (!f) {
        fprintf(stderr, "render: cannot open %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = line, *e, tok[64];
        int k = 0;
        if ((e = strchr(s, '#')))
            *e = 0;
        while (isspace((unsigned char)*s))
            s++;
        e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1]))
            *--e = 0;
        if (!*s)
            continue;
        while (s[k] && !isspace((unsigned char)s[k]) && k < 63)
            tok[k] = s[k], k++;
        tok[k] = 0;
        {
            double tt;
            if (parse_time(tok, &tt)) {
                t = tt;
                s += k;
                while (isspace((unsigned char)*s))
                    s++;
                if (!*s)
                    continue;
            }
        }
        add_cmd((uint64_t)(t * (double)SR + 0.5), s);
    }
    fclose(f);
    {   /* in time order; lines at the same time keep the file's order */
        int i, j;
        for (i = 1; i < ncmds; i++) {
            cmd_t c = cmds[i];
            for (j = i; j > 0 && cmds[j - 1].at > c.at; j--)
                cmds[j] = cmds[j - 1];
            cmds[j] = c;
        }
    }
    return 1;
}

/* ---------------------------------------------------------- the engine's names --- */
static void fold(const char *s, char *out)        /* lower case, no spaces or underscores */
{
    int k = 0;
    for (; *s && k < 31; s++)
        if (*s != ' ' && *s != '_' && *s != '-')
            out[k++] = (char)tolower((unsigned char)*s);
    out[k] = 0;
}
static int find_param(const char *name)
{
    char a[32], b[32];
    int p;
    fold(name, a);
    for (p = 0; p < P_NPARAMS; p++) {
        fold(en_param_info(p)->name, b);
        if (!strcmp(a, b))
            return p;
    }
    return -1;
}
static int find_name(const char *name, const char *const *list, int n)
{
    char a[32], b[32];
    int i;
    fold(name, a);
    for (i = 0; i < n; i++) {
        fold(list[i], b);
        if (!strcmp(a, b))
            return i;
    }
    if (isdigit((unsigned char)name[0]) || name[0] == '-') {
        i = atoi(name);
        return i >= 0 && i < n ? i : -1;
    }
    return -1;
}
static int find_piece(const char *name)
{
    const char *names[EN_NPIECES];
    int i;
    for (i = 0; i < EN_NPIECES; i++)
        names[i] = en_piece_name(i);
    return find_name(name, names, EN_NPIECES);
}

/* ------------------------------------------------------------------ state --- */
static int solo = -1;                           /* -1 all; else EN_TAP_* kept, the others muted */
static int piece_now = -1, root_now = 2, mode_now = 1;
static uint32_t gain_q12 = 1024;                /* APP_GAIN(2048): the simulator's default MASTER */
static int32_t blk[2 * BLOCK];
static int quiet;

static void flush(void) { en_render(blk, 0, gain_q12); }   /* drain the command queue (64 deep) */

static void apply_solo(void)
{
    int k;
    if (solo < 0)
        return;
    for (k = 0; k < 4; k++)
        en_host_mute(k, k != solo);
}

static void load_piece(int i)
{
    int16_t v[P_NPARAMS];
    uint16_t pool;
    int p;
    en_piece(i, v, &pool);
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, v[p]);
    en_pool(pool);
    flush();
    en_regrow();
    piece_now = i;
    root_now = v[P_ROOT];
    mode_now = v[P_MODE];
}

static int run_cmd(const char *text, double t)
{
    char c[32], a[64], b[64];
    int n = sscanf(text, "%31s %63s %63s", c, a, b);
    if (n < 1)
        return 1;
    if (!strcmp(c, "piece")) {
        int i = n >= 2 ? find_piece(a) : -1;
        if (i < 0) {
            fprintf(stderr, "render: unknown piece '%s'\n", n >= 2 ? a : "");
            return 0;
        }
        load_piece(i);
    } else if (!strcmp(c, "seed")) {
        en_seed((uint32_t)strtoul(n >= 2 ? a : "0", 0, 0));
    } else if (!strcmp(c, "play")) {
        en_play(1);
    } else if (!strcmp(c, "stop")) {
        en_play(0);
    } else if (!strcmp(c, "next")) {
        en_next();
    } else if (!strcmp(c, "regrow")) {
        en_regrow();
    } else if (!strcmp(c, "panic")) {
        en_panic();
    } else if (!strcmp(c, "freeze")) {
        en_freeze(n < 2 || !strcmp(a, "on") || !strcmp(a, "1"));
    } else if (!strcmp(c, "set")) {
        int p = n >= 3 ? find_param(a) : -1;
        if (p < 0) {
            fprintf(stderr, "render: bad set '%s'\n", text);
            return 0;
        }
        en_set(p, atoi(b));
        if (p == P_ROOT)
            root_now = atoi(b);
        if (p == P_MODE) {
            mode_now = atoi(b);
            en_pool(EN_MODE_POOL[(unsigned)mode_now < EN_NMODES ? mode_now : 0]);
        }
    } else if (!strcmp(c, "plate_exc")) {       /* host only: the plate's modulation depth, 0..1 of AMB-1's (TESTING #18) */
        en_host_plate_exc((float)atof(n >= 2 ? a : "1"));
    } else if (!strcmp(c, "plate_rate")) {      /* host only: the plate's modulation rate in Hz (0.7 is AMB-1's) */
        en_host_plate_mod((float)atof(n >= 2 ? a : "0.7"), 0);
    } else if (!strcmp(c, "plate_range")) {     /* host only: the wandering plate's rates, Hz (the engine's: 0.25 1.3) */
        if (n >= 3)
            en_host_plate_range((float)atof(a), (float)atof(b));
    } else if (!strcmp(c, "pad_turn")) {        /* host only: the pad's turn, and its recess, seconds; "pad_turn 0": none */
        float v[4] = {75.0f, 210.0f, 20.0f, 70.0f};
        sscanf(text, "%*s %f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
        en_host_pad_turn(v[0], v[1], v[2], v[3]);
    } else if (!strcmp(c, "pad_range")) {       /* host only: the pad's window, MIDI notes (the engine's: 48 77) */
        if (n >= 3)
            en_host_pad_range(atoi(a), atoi(b));
    } else if (!strcmp(c, "plate_wander")) {    /* host only: each half's rate wanders 0.25-1.3 Hz, never periodic */
        en_host_plate_mod(0.0f, 1);
    } else if (!strcmp(c, "mode")) {
        int m = n >= 2 ? find_name(a, EN_MODE_NAME, EN_NMODES) : -1;
        if (m < 0) {
            fprintf(stderr, "render: unknown mode '%s'\n", n >= 2 ? a : "");
            return 0;
        }
        en_set(P_MODE, m);
        en_pool(EN_MODE_POOL[m]);
        mode_now = m;
    } else if (!strcmp(c, "root")) {
        int r = n >= 2 ? find_name(a, EN_NOTE_NAME, 12) : -1;
        if (r < 0) {
            fprintf(stderr, "render: unknown root '%s'\n", n >= 2 ? a : "");
            return 0;
        }
        en_set(P_ROOT, r);
        root_now = r;
    } else if (!strcmp(c, "pool")) {
        en_pool((uint32_t)strtoul(n >= 2 ? a : "1", 0, 0));
    } else if (!strcmp(c, "master")) {
        uint32_t m = (uint32_t)atoi(n >= 2 ? a : "2048");
        if (m > 4096u)
            m = 4096u;
        gain_q12 = m * m >> 12;
    } else {
        fprintf(stderr, "render: unknown command '%s'\n", text);
        return 0;
    }
    apply_solo();
    flush();
    if (!quiet)
        fprintf(stderr, "  %9.1f s  %s\n", t, text);
    return 1;
}

/* -------------------------------------------------------------------- CSV --- */
static FILE *csv;
static double e_l, e_r, pk;                     /* the output this second */
static double d_acc, last_l, last_r;            /* the output's first difference: its rms frequency (brightness) */
static FILE *envf;                              /* --env: the 100 ms envelope */
static FILE *meter_out, *meter_in;              /* --meter-out / --meter-in: the governor's plate and echo energies */
static void meter_hook(float *plate, float *echo)
{
    float v[2];
    if (meter_out) {
        v[0] = *plate;
        v[1] = *echo;
        fwrite(v, sizeof v, 1, meter_out);
    }
    if (meter_in && fread(v, sizeof v, 1, meter_in) == 1) {
        *plate = v[0];
        *echo = v[1];
    }
}
static double env_e;
static uint32_t env_frames;
#define ENV_N 4410u
static uint64_t frames_row;

static void csv_header(void)
{
    int p;
    fputs("t,playing,frozen,piece,root,mode,field,anchor,chord,chord_root,deg,chord_n,drift,arc,"
          "pad_level,bass_level,pad_amp0,pad_amp1,pad_amp2,pad_amp3,pad_on,bells_on,bass_on,pulse_on,pulse_rest,"
          "layers_on,rms_pad,rms_bells,rms_bass,rms_pulse,rms_echo,rms_plate,rms_out_l,rms_out_r,peak_out,bright_hz,"
          "rsend_pad,rsend_bells,rsend_bass,rsend_pulse,esend_pad,esend_bells,esend_bass,esend_pulse,"
          "notes_pad,notes_bells,notes_bass,notes_pulse,loop_notes,pad_st0,pad_st1,pad_st2,pad_st3",
          csv);
    for (p = 0; p < EN_NCOND; p++)
        fprintf(csv, ",c_%s", EN_COND_NAME[p]);
    for (p = 0; p < P_NPARAMS; p++) {
        char nm[32];
        fold(en_param_info(p)->name, nm);
        fprintf(csv, ",p_%s", nm);
    }
    fputc('\n', csv);
}

static double rms(double e, uint64_t frames) { return frames ? sqrt(e / (2.0 * (double)frames)) : 0.0; }

static void csv_row(double t)
{
    int pcs[5], i, pad_on = 0, bells_on = en_tap_bells_on(), bass_on = en_bass_level > 0.01f,
                pulse_on = en_tap_drums_on(), p;
    char chord[16], field[16];
    uint64_t fr = en_tap_frames;
    pcs[0] = en_chord_root % 12;                /* the name is read from the root (as the web app does) */
    for (i = 0; i < EN_NPAD; i++) {
        pcs[1 + i] = en_chord[i] % 12;
        pad_on += en_pad_amp[i] > 0.01f;
    }
    en_chord_name(pcs, 5, chord);
    /* root and mode: the player's (home); field: the engine's harmonic field as it is now (Phase 5) */
    fprintf(csv, "%.0f,%d,%d,%s,%s,%s,%s,%s,%s,%d,%d,%d,%.4f,%.4f,", t, en_playing, en_frozen,
            piece_now >= 0 ? en_piece_name(piece_now) : "-", EN_NOTE_NAME[(root_now % 12 + 12) % 12],
            EN_MODE_NAME[(unsigned)mode_now < EN_NMODES ? mode_now : 0], en_field_name(field), en_anchor_name(), chord,
            en_chord_root, en_tap_deg(), en_chord_n, (double)en_drift, (double)en_arc);
    fprintf(csv, "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,", (double)en_pad_level, (double)en_bass_level,
            (double)en_pad_amp[0], (double)en_pad_amp[1], (double)en_pad_amp[2], (double)en_pad_amp[3], pad_on,
            bells_on, bass_on, pulse_on, en_pulse_rest, (pad_on > 0) + (bells_on > 0) + bass_on + (pulse_on > 0));
    fprintf(csv, "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.1f,", rms(en_tap_dry[EN_TAP_PAD], fr),
            rms(en_tap_dry[EN_TAP_BELLS], fr), rms(en_tap_dry[EN_TAP_BASS], fr), rms(en_tap_dry[EN_TAP_PULSE], fr),
            rms(en_tap_echo, fr), rms(en_tap_plate, fr), frames_row ? sqrt(e_l / (double)frames_row) : 0.0,
            frames_row ? sqrt(e_r / (double)frames_row) : 0.0, pk,
            e_l + e_r > 0.0 ? (double)SR / (2.0 * 3.14159265358979) * sqrt(d_acc / (e_l + e_r)) : 0.0);
    fprintf(csv, "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,", rms(2.0 * en_tap_rsend[EN_TAP_PAD], fr),
            rms(2.0 * en_tap_rsend[EN_TAP_BELLS], fr), rms(2.0 * en_tap_rsend[EN_TAP_BASS], fr),
            rms(2.0 * en_tap_rsend[EN_TAP_PULSE], fr), rms(2.0 * en_tap_esend[EN_TAP_PAD], fr),
            rms(2.0 * en_tap_esend[EN_TAP_BELLS], fr), rms(2.0 * en_tap_esend[EN_TAP_BASS], fr),
            rms(2.0 * en_tap_esend[EN_TAP_PULSE], fr));
    fprintf(csv, "%u,%u,%u,%u,%d,%d,%d,%d,%d", notes[0], notes[1], notes[2], notes[3], en_tap_loop_notes(),
            en_tap_pad_state(0), en_tap_pad_state(1), en_tap_pad_state(2), en_tap_pad_state(3));
    for (p = 0; p < EN_NCOND; p++)
        fprintf(csv, ",%.4f", (double)en_cond[p]);
    for (p = 0; p < P_NPARAMS; p++)
        fprintf(csv, ",%d", en_tap_param(p));
    fputc('\n', csv);
    notes[0] = notes[1] = notes[2] = notes[3] = 0;
    en_tap_reset();
    e_l = e_r = pk = d_acc = 0.0;
    frames_row = 0;
}

/* ------------------------------------------------------------------- main --- */
static void usage(void)
{
    fputs("render --script FILE --seconds N | --hours H [--wav OUT] [--csv OUT] [--events OUT]\n"
          "       [--master 0..4096] [--bits 16|24] [--solo pad|bells|bass|pulse] [--env OUT]\n"
          "       [--meter-out FILE | --meter-in FILE] [--quiet]\n",
          stderr);
}

int main(int argc, char **argv)
{
    const char *script = 0, *wavp = 0, *csvp = 0, *evp = 0, *envp = 0, *mop = 0, *mip = 0;
    double seconds = 0.0;
    uint64_t total, next_row;
    int i, ci = 0;
    static const char *const LAYER[4] = {"pad", "bells", "bass", "pulse"};
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--script"))
            script = v, i++;
        else if (!strcmp(a, "--seconds"))
            seconds = atof(v), i++;
        else if (!strcmp(a, "--hours"))
            seconds = atof(v) * 3600.0, i++;
        else if (!strcmp(a, "--wav"))
            wavp = v, i++;
        else if (!strcmp(a, "--csv"))
            csvp = v, i++;
        else if (!strcmp(a, "--events"))
            evp = v, i++;
        else if (!strcmp(a, "--env"))
            envp = v, i++;
        else if (!strcmp(a, "--meter-out"))
            mop = v, i++;
        else if (!strcmp(a, "--meter-in"))
            mip = v, i++;
        else if (!strcmp(a, "--master")) {
            uint32_t m = (uint32_t)atoi(v);
            if (m > 4096u)
                m = 4096u;
            gain_q12 = m * m >> 12;
            i++;
        } else if (!strcmp(a, "--bits"))
            wav_bits = atoi(v) == 24 ? 24 : 16, i++;
        else if (!strcmp(a, "--solo")) {
            solo = find_name(v, LAYER, 4);
            if (solo < 0) {
                fprintf(stderr, "render: --solo pad|bells|bass|pulse\n");
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--quiet"))
            quiet = 1;
        else {
            usage();
            return 2;
        }
    }
    if (!script || seconds <= 0.0) {
        usage();
        return 2;
    }
    if (!read_script(script))
        return 1;
    if (wavp && !(wav = fopen(wavp, "wb"))) {
        fprintf(stderr, "render: cannot write %s\n", wavp);
        return 1;
    }
    if (csvp && !(csv = fopen(csvp, "w"))) {
        fprintf(stderr, "render: cannot write %s\n", csvp);
        return 1;
    }
    if (evp && !(events = fopen(evp, "w"))) {
        fprintf(stderr, "render: cannot write %s\n", evp);
        return 1;
    }
    if (envp && !(envf = fopen(envp, "w"))) {
        fprintf(stderr, "render: cannot write %s\n", envp);
        return 1;
    }
    if (mop && !(meter_out = fopen(mop, "wb"))) {
        fprintf(stderr, "render: cannot write %s\n", mop);
        return 1;
    }
    if (mip && !(meter_in = fopen(mip, "rb"))) {
        fprintf(stderr, "render: cannot read %s\n", mip);
        return 1;
    }
    if (meter_out || meter_in)
        en_host_meter_hook = meter_hook;
    if (wav)
        wav_header(wav, 0);
    if (csv)
        csv_header();
    en_init();
    en_tap_reset();
    apply_solo();
    total = (uint64_t)(seconds * (double)SR + 0.5);
    next_row = SR;
    frame_now = 0;
    while (frame_now < total) {
        uint32_t n = BLOCK, j;
        if (total - frame_now < n)
            n = (uint32_t)(total - frame_now);
        if (next_row - frame_now < n)
            n = (uint32_t)(next_row - frame_now);
        if (envf && ENV_N - env_frames < n)
            n = ENV_N - env_frames;
        while (ci < ncmds && cmds[ci].at <= frame_now) {
            if (!run_cmd(cmds[ci].text, (double)frame_now / (double)SR))
                return 1;
            ci++;
        }
        en_render(blk, n, gain_q12);
        for (j = 0; j < n; j++) {
            double l = (double)blk[2 * j] * (1.0 / 8388608.0), r = (double)blk[2 * j + 1] * (1.0 / 8388608.0);
            e_l += l * l;
            e_r += r * r;
            env_e += l * l + r * r;
            d_acc += (l - last_l) * (l - last_l) + (r - last_r) * (r - last_r);
            last_l = l;
            last_r = r;
            if (fabs(l) > pk)
                pk = fabs(l);
            if (fabs(r) > pk)
                pk = fabs(r);
        }
        frames_row += n;
        if (envf && (env_frames += n) >= ENV_N) {
            fprintf(envf, "%.6e\n", env_e / (2.0 * (double)env_frames));
            env_e = 0.0;
            env_frames = 0;
        }
        wav_write(blk, n);
        frame_now += n;
        if (frame_now == next_row) {
            if (csv)
                csv_row((double)frame_now / (double)SR);
            next_row += SR;
            if (!quiet && (frame_now / SR) % 600u == 0u)
                fprintf(stderr, "  %6.0f s rendered\r", (double)frame_now / (double)SR);
        }
    }
    if (csv && frames_row)
        csv_row((double)frame_now / (double)SR);
    if (wav) {
        wav_header(wav, wav_data);
        fclose(wav);
    }
    if (csv)
        fclose(csv);
    if (events)
        fclose(events);
    if (envf)
        fclose(envf);
    if (meter_out)
        fclose(meter_out);
    if (meter_in)
        fclose(meter_in);
    if (!quiet)
        fprintf(stderr, "\nrender: %.0f s of audio%s%s\n", seconds, wavp ? " -> " : "", wavp ? wavp : "");
    return 0;
}
