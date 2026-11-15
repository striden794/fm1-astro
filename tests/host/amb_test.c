/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Tomorrow's Man (TIDE: the conductor's rules) */
/* The instrument on the host: the pure parts (Euclid, scales, chords, voice leading, names) and the
 * render (sound, no NaN, bounded, the loops evolve, the chord moves, freeze holds). TIDE adds its rules:
 * at most three pad voices sing, a voice enters only on a note of the chord, Drift at Hold holds, the
 * governor keeps the mix near its target, and nothing sustained moves faster than the swell limit. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...)                                                                                              \
    do {                                                                                                           \
        if (!(c)) {                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
            fails++;                                                                                               \
        }                                                                                                          \
    } while (0)

static uint32_t midi_count, bell_count, bell_off;
static int bell_lo = 127, bell_hi;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_count++;
    if (st == 0x91u && d2 > 0u) {               /* a bell struck (channel 2) */
        const uint32_t r = en_field_root, a = ((uint32_t)en_field_pool << r | (uint32_t)en_field_pool >> (12 - r)) & 0xFFFu;
        bell_count++;
        bell_off += !(a >> (d1 % 12u) & 1u);    /* outside the harmonic field as it is at the strike */
        if ((int)d1 < bell_lo)
            bell_lo = (int)d1;
        if ((int)d1 > bell_hi)
            bell_hi = (int)d1;
    }
}

static uint32_t pat(const char *s)
{
    uint32_t m = 0;
    int i;
    for (i = 0; s[i]; i++)
        if (s[i] == 'x')
            m |= 1u << i;
    return m;
}

static int popcount(uint32_t m)
{
    int c = 0;
    while (m) {
        c += (int)(m & 1u);
        m >>= 1;
    }
    return c;
}

static int32_t buf[2 * 256];
static float peak, sum2;
static int bad;
static void run(float secs)
{
    int blocks = (int)(secs * 44100.0f / 256.0f), b, j;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, 2048);
        for (j = 0; j < 512; j++) {
            float x = (float)buf[j] / 8388608.0f;
            if (x != x)
                bad++;
            if (fabsf(x) > peak)
                peak = fabsf(x);
            sum2 += x * x;
        }
    }
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

int main(void)
{
    int pcs[4], n, i;
    char name[16];

    /* Euclid: the classic patterns */
    CHECK(en_euclid(3, 8, 0) == pat("x..x..x."), "E(3,8) %x", en_euclid(3, 8, 0));
    CHECK(en_euclid(5, 8, 0) == pat("x.xx.xx."), "E(5,8) %x", en_euclid(5, 8, 0));
    CHECK(en_euclid(4, 16, 0) == pat("x...x...x...x..."), "E(4,16)");
    CHECK(en_euclid(0, 16, 0) == 0, "E(0,16)");
    CHECK(en_euclid(7, 7, 0) == 0x7Fu, "E(7,7)");
    for (i = 0; i <= 12; i++)
        CHECK(popcount(en_euclid(i, 12, 5)) == i, "E(%d,12) has %d hits", i, popcount(en_euclid(i, 12, 5)));
    CHECK(en_euclid(1, 7, 3) == 1u << 3, "rotation");

    /* scales and chords */
    {
        int st[12];
        CHECK(en_scale(EN_MODE_POOL[MODE_DORIAN], st) == 7 && st[2] == 3 && st[6] == 10, "Dorian steps");
        CHECK(en_scale(EN_MODE_POOL[MODE_PENT_MIN], st) == 5, "pentatonic minor has 5");
        CHECK(en_scale(0, st) == 1, "the root is always in");
    }
    n = en_chord_tones(EN_MODE_POOL[MODE_IONIAN], 0, 0, pcs);
    CHECK(n == 4 && pcs[0] == 0 && pcs[1] == 4 && pcs[2] == 7 && pcs[3] == 11, "C Ionian I = Cmaj7");
    CHECK(!strcmp(en_chord_name(pcs, n, name), "CM7"), "name %s", name);
    n = en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 0, pcs);
    CHECK(!strcmp(en_chord_name(pcs, n, name), "Dm7"), "D Dorian i: %s", name);
    n = en_chord_tones(EN_MODE_POOL[MODE_DORIAN], 2, 3, pcs);
    CHECK(!strcmp(en_chord_name(pcs, n, name), "G7"), "D Dorian IV: %s", name);
    n = en_chord_tones(EN_MODE_POOL[MODE_PENT_MAJ], 0, 0, pcs);
    CHECK(n == 3, "pentatonic chords have three tones (%d)", n);
    n = en_chord_tones(1u | 1u << 4 | 1u << 7 | 1u << 9, 0, 0, pcs);   /* four notes: no doubled tones */
    CHECK(n == 3 && pcs[0] != pcs[1] && pcs[1] != pcs[2] && pcs[0] != pcs[2], "4-note pool: distinct");
    n = en_chord_tones(1u | 1u << 4 | 1u << 7, 0, 1, pcs);
    CHECK(n == 3, "3-note pool: all three (%d)", n);

    /* voice leading: small moves, in range, no unisons */
    {
        int from[4] = {50, 57, 62, 66}, to[4], cm7[4] = {0, 4, 7, 11}, f7[4] = {5, 9, 0, 4}, tot = 0;
        en_voice_lead(from, cm7, 4, to, 48, 77);
        for (i = 0; i < 4; i++) {
            CHECK(to[i] >= 48 && to[i] <= 77, "range %d", to[i]);
            tot += abs(to[i] - from[i]);
        }
        CHECK(tot <= 8, "Cmaj7 from D-ish: moved %d", tot);
        memcpy(from, to, sizeof from);
        en_voice_lead(from, f7, 4, to, 48, 77);
        tot = 0;
        for (i = 0; i < 4; i++)
            tot += abs(to[i] - from[i]);
        CHECK(tot <= 6, "CM7 -> FM7 moved %d", tot);
        CHECK(to[0] != to[1] && to[1] != to[2] && to[2] != to[3] && to[0] != to[3], "no unisons");
    }

    /* texts */
    {
        char t[12];
        en_param_text(P_DRIFT, 0, t);
        CHECK(!strcmp(t, "Hold"), "drift 0: %s", t);
        en_param_text(P_DRIFT, 50, t);
        CHECK(!strcmp(t, "1.00x"), "drift 50: %s", t);
        en_param_text(P_DRIFT, 100, t);
        CHECK(!strcmp(t, "4.00x"), "drift 100: %s", t);
        en_param_text(P_ETIME, 3, t);
        CHECK(!strcmp(t, "1 bar"), "etime: %s", t);
        en_param_text(P_BOCT, 1, t);
        CHECK(!strcmp(t, "+1"), "boct: %s", t);
    }

    /* the render: silent at rest, sound when growing, never NaN, bounded */
    en_init();
    run(1.0f);
    CHECK(peak == 0.0f, "silent before play (%f)", (double)peak);
    en_play(1);
    peak = sum2 = 0.0f;
    run(20.0f);
    CHECK(!bad, "NaN in the output");
    CHECK(peak > 0.05f && peak < 0.99f, "peak %f", (double)peak);
    printf("  20 s playing: peak %.3f, rms %.4f, bars %u\n", (double)peak,
           (double)sqrtf(sum2 / (20.0f * 44100.0f * 2.0f)), en_bars / 16u);
    CHECK(en_bars / 16u >= 5u, "the grid ran (%u sixteenths)", en_bars);
    CHECK(en_pad_level > 0.25f, "the pad came in (%f)", (double)en_pad_level);

    /* the chord moves: with Drift at its fastest (4x), a few changes in five minutes */
    {
        int changes = 0, last = en_chord_root * 16 + en_chord[0] % 16, k;
        en_set(P_DRIFT, 100);
        for (k = 0; k < 300; k++) {
            int now;
            run(1.0f);
            now = en_chord_root * 16 + en_chord[0] % 16;
            changes += now != last;
            last = now;
        }
        CHECK(changes >= 2, "chord changes in 5 min at Drift 100: %d", changes);
        printf("  chord changes in 5 min at Drift 100: %d\n", changes);
    }

    /* TIDE: a voice enters only on a note of the chord (a voice already singing is never bent: it
     * dissolves); never more than three sing at once */
    {
        int k, i, off_chord = 0, entries = 0, over = 0;
        float prev[EN_NPAD], vsum[16] = {0};         /* the voices singing, per 30 s: printed when entrances are few */
        en_set(P_DRIFT, 50);                     /* 1x: at 4x a chord moves every 20-45 s and a voice entering at 0.4 dB/s is
                                                  * sent out again before it reaches the -20 dB this test counts (2026-11-09) */
        for (i = 0; i < EN_NPAD; i++)
            prev[i] = en_pad_amp[i];
        for (k = 0; k < 1200 * 4; k++) {        /* 20 min: the pad rests for up to ten minutes by design */
            uint32_t pcs = 0;
            run(0.25f);
            for (i = 0; i < EN_NPAD; i++)
                pcs |= 1u << (en_chord[i] % 12);
            for (i = 0; i < EN_NPAD; i++) {
                if (prev[i] < 0.1f && en_pad_amp[i] >= 0.1f) {   /* rising through 0.1: an entrance */
                    entries++;
                    if (!(pcs >> (en_pad_note[i] % 12) & 1u))
                        off_chord++;
                }
                prev[i] = en_pad_amp[i];
            }
            over += en_cond[CD_PAD_VOICES] > 3.0f;
            vsum[k / 300] += en_cond[CD_PAD_VOICES];
        }
        if (entries < 2) {
            printf("  pad voices singing per 75 s:");
            for (k = 0; k < 16; k++)
                printf(" %.1f", (double)(vsum[k] / 300.0f));
            printf("  (presence %.2f, target %.0f, bells %.0f dB, bass %.0f dB, trim %.1f)\n", (double)en_cond[CD_PRES_PAD],
                   (double)en_cond[CD_PAD_TARGET], (double)en_cond[CD_BELLS], (double)en_cond[CD_BASS], (double)en_cond[CD_TRIM]);
        }
        CHECK(entries >= 2, "pad entrances in 20 min: %d", entries);
        CHECK(!off_chord, "%d of %d entrances on a note outside the chord", off_chord, entries);
        CHECK(!over, "more than three pad voices singing at %d moments", over);
        printf("  pad entrances in 20 min: %d\n", entries);
    }

    /* the loops evolve: with Evolve at 100, the phrases change */
    {
        int8_t before[EN_NLOOP][EN_LOOP_MAX];
        int diff = 0, l;
        en_set(P_DRIFT, 50);
        en_set(P_EVOLVE, 100);
        en_set(P_DENSITY, 100);                  /* TIDE: the loops carry fewer notes and may be empty at a low presence */
        memcpy(before, (const void *)en_loop_note, sizeof before);
        run(180.0f);
        {
            int notes = 0;
            for (l = 0; l < EN_NLOOP; l++)
                for (i = 0; i < EN_LOOP_MAX; i++) {
                    diff += before[l][i] != en_loop_note[l][i];
                    notes += en_loop_note[l][i] >= 0;
                }
            CHECK(diff > 0 || notes == 0, "the loops evolved (%d notes)", notes);   /* TIDE: empty loops (a low presence) cannot */
        }
        for (l = 0; l < EN_NLOOP; l++)
            for (i = EN_LOOP_LEN[l]; i < EN_LOOP_MAX; i++)
                CHECK(en_loop_note[l][i] == -1, "loop %d slot %d past its end", l, i);
    }

    /* freeze: the chord holds */
    {
        int held = en_chord[0] + 128 * en_chord[1], k;
        en_set(P_DRIFT, 100);
        en_freeze(1);
        for (k = 0; k < 60; k++) {
            run(1.0f);
            CHECK(en_chord[0] + 128 * en_chord[1] == held, "the chord moved while frozen");
        }
        en_freeze(0);
    }

    /* TIDE: Drift at Hold holds the weather, the chord and the cells; the music goes on */
    {
        int8_t before[EN_NLOOP][EN_LOOP_MAX];
        float w0[EN_NCOND];
        int held, k, diff = 0, l, moved = 0, fr0, fp0;
        en_set(P_DRIFT, 0);
        run(2.0f);
        held = en_chord[0] + 128 * en_chord[1];
        fr0 = en_field_root;
        fp0 = en_field_pool;
        memcpy(before, (const void *)en_loop_note, sizeof before);
        for (k = 0; k < EN_NCOND; k++)
            w0[k] = en_cond[k];
        peak = 0.0f;
        run(180.0f);
        CHECK(en_chord[0] + 128 * en_chord[1] == held, "the chord moved at Hold");
        for (l = 0; l < EN_NLOOP; l++)
            for (i = 0; i < EN_LOOP_MAX; i++)
                diff += before[l][i] != en_loop_note[l][i];
        CHECK(diff == 0, "the cells changed %d notes at Hold", diff);
        for (k = CD_BRIGHT; k <= CD_S_BRIGHT; k++)
            moved += fabsf(en_cond[k] - w0[k]) > 1e-6f;
        moved += fabsf(en_cond[CD_CURSOR_X] - w0[CD_CURSOR_X]) > 1e-6f;   /* Phase 4: the timbre cursor too */
        moved += fabsf(en_cond[CD_CURSOR_Y] - w0[CD_CURSOR_Y]) > 1e-6f;
        moved += fabsf(en_cond[CD_S_MODE] - w0[CD_S_MODE]) > 1e-6f;       /* Phase 5: the harmonic field's walkers */
        moved += fabsf(en_cond[CD_S_ROOT] - w0[CD_S_ROOT]) > 1e-6f;
        CHECK(moved == 0, "%d walkers moved at Hold", moved);
        en_next();                               /* a chord asked for by hand at Hold: the field still holds */
        run(8.0f);
        CHECK(en_field_root == fr0 && en_field_pool == fp0, "the harmonic field moved at Hold");
        CHECK(peak > 0.02f, "silent at Hold (%f)", (double)peak);
        en_set(P_DRIFT, 25);
    }

    /* a new key while playing (a piece, the root, the scale): within 3 s no voice still sings the old
     * chord (a voice otherwise only takes a new note as it enters) */
    {
        int k, stale = 0;
        en_freeze(0);
        en_set(P_ROOT, 6);                       /* a tritone away: nothing in common to hide behind */
        en_pool(EN_MODE_POOL[MODE_LYDIAN]);
        en_regrow();
        run(3.0f);
        for (k = 0; k < EN_NPAD; k++)
            if (en_pad_amp[k] > 0.02f && en_pad_note[k] != en_chord[k]) {
                printf("  voice %d sings %d, the chord has %d (amp %.2f)\n", k, en_pad_note[k], en_chord[k],
                       (double)en_pad_amp[k]);
                stale++;
            }
        CHECK(!stale, "%d voices still in the old key after 3 s", stale);
        en_set(P_ROOT, 2);
    }

    /* a pool change: the chord's tones are in it */
    {
        uint32_t pent = EN_MODE_POOL[MODE_PENT_MIN];
        en_pool(pent);
        run(0.1f);
        for (i = 0; i < 4; i++)
            CHECK(pent >> ((en_chord[i] - 2 + 120) % 12) & 1u, "chord note %d not in the pool", en_chord[i]);
    }

    /* every setting at its extremes: still bounded */
    {
        int p;
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, en_param_info(p)->hi);
        en_set(P_MIDI, 1);
        peak = 0.0f;
        run(30.0f);
        CHECK(!bad && peak < 1.0f, "all at max: peak %f", (double)peak);
        for (p = 0; p < P_NPARAMS; p++)
            en_set(p, en_param_info(p)->lo);
        en_set(P_TEMPO, 30);
        run(10.0f);
        CHECK(!bad, "all at min: NaN");
    }

    /* Bell tone and Bell oct are heard: brighter (zero crossings) and higher with them, bells alone */
    {
        float zc[3];
        int k;
        static const int TONE[3] = {0, 100, 0}, OCT[3] = {0, 0, 2};
        /* zero crossings per bell struck (the loops' random fill differed 12 to 2 notes between runs on 2026-11-09, and a
         * minute of two notes is not brighter than a minute of twelve), over six minutes. Phase 5: per bell struck in
         * fact (it had been per MIDI message, nearly all of them clock), and six minutes for two, the loops being
         * three times as long */
        for (k = 0; k < 3; k++) {
            int b2, j;
            long z = 0, loud = 0;
            float prev = 0.0f;
            uint32_t m0;
            en_init();
            en_set(P_PAD, 0);
            en_set(P_BASS, 0);
            en_set(P_PULSE, 0);
            en_set(P_ECHO, 0);
            en_set(P_REVERB, 0);
            en_set(P_DENSITY, 100);
            en_set(P_BTONE, TONE[k]);
            en_set(P_BOCT, OCT[k]);
            en_play(1);
            run(40.0f);                          /* TIDE: the bells fade in over the opening's 30 s */
            m0 = bell_count;
            for (b2 = 0; b2 < 360 * 44100 / 256; b2++) {   /* six minutes: the loops carry 0-3 notes each now */
                long zb = 0;
                float pk = 0.0f;
                en_render(buf, 256, 2048);
                for (j = 0; j < 256; j++) {
                    float x = (float)buf[2 * j];
                    zb += (x > 0.0f) != (prev > 0.0f);
                    if (fabsf(x) > pk)
                        pk = fabsf(x);
                    prev = x;
                }
                if (pk > 8388608.0f * 0.0005f) {  /* a block in which a bell is ringing (over -66 dBFS) */
                    z += zb;
                    loud += 256;
                }
            }
            /* Phase 5b: crossings per second of ringing bell. (It was crossings per bell struck over the whole six
             * minutes: with the bells' count riding its own walker one setting had 13 bells and another 77, and the
             * silence between 13 bells counted for them.) */
            zc[k] = (float)z * 44100.0f / (float)(loud > 0 ? loud : 1);
            printf("  bells alone, tone %d oct %d: %ld zero crossings, %u bells (%.0f a second of ringing); bells at %.1f dB, loops hold %d notes\n",
                   TONE[k], OCT[k], z, bell_count - m0, (double)zc[k], (double)en_cond[CD_BELLS], en_tap_loop_notes());
        }
        CHECK(zc[1] > 1.5f * zc[0], "Bell tone 100 not brighter: %.0f vs %.0f", (double)zc[1], (double)zc[0]);
        CHECK(zc[2] > 1.4f * zc[0], "Bell oct +2 not higher: %.0f vs %.0f", (double)zc[2], (double)zc[0]);
    }

    /* TIDE: the governor and the swell limit. The pad alone (the other layers' levels at zero), the
     * default piece, 15 minutes: after the opening, the pad's 3 s level (its reverb included, floored
     * 30 dB under its median) never moves faster than 1 dB/s nor 1.5 dB in 2 s; and the governor's
     * 20 s level sits within 4 dB of its target by the end. */
    {
        enum { SECS = 900, START = 120 };
        static float lv[SECS];
        static float d1[SECS], d2[SECS];
        float e, med, floor_db, m1 = 0.0f, m2 = 0.0f, p1, p2;
        int s, n1 = 0, n2 = 0;
        FILE *dump = fopen("app/build/tests/padalone.txt", "w");   /* per second: the level and the voices */
        en_init();
        /* Phase 5b: through a steady plate. The engine's plate wanders now (Tomorrow's Man's choice, TESTING #23) and its
         * breathing alone moves the pad's 3 s level about 2 dB/s at its fastest; the limit is about the conductor's
         * moves (evolve_report judges the same way, on its steady-plate pass, and reports the level as heard) */
        en_host_plate_mod(0.7f, 0);
        en_set(P_BELLS, 0);
        en_set(P_BASS, 0);
        en_set(P_PULSE, 0);
        en_play(1);
        for (s = 0; s < SECS; s++) {
            double fr;
            sum2 = 0.0f;
            en_tap_reset();
            run(1.0f);
            fr = 2.0 * (double)en_tap_frames + 1e-9;
            e = sum2 / (44100.0f * 2.0f);
            lv[s] = 10.0f * log10f(e + 1e-12f);
            if (dump)
                fprintf(dump, "%d %.1f [dry %.1f rsend %.1f plate %.1f echo %.1f] %.3f/%d %.3f/%d %.3f/%d %.3f/%d  voices %.0f trim %.1f gain %.1f\n",
                        s, (double)lv[s], 10.0 * log10(en_tap_dry[0] / fr + 1e-12), 10.0 * log10(2.0 * en_tap_rsend[0] / fr + 1e-12),
                        10.0 * log10(en_tap_plate / fr + 1e-12), 10.0 * log10(en_tap_echo / fr + 1e-12),
                        (double)en_pad_amp[0], en_pad_note[0], (double)en_pad_amp[1], en_pad_note[1],
                        (double)en_pad_amp[2], en_pad_note[2], (double)en_pad_amp[3], en_pad_note[3],
                        (double)en_cond[CD_PAD_VOICES], (double)en_cond[CD_TRIM], (double)en_cond[CD_GAIN_PAD]);
        }
        if (dump)
            fclose(dump);
        for (s = 2; s < SECS; s++) {              /* the 3 s level */
            float a = powf(10.0f, lv[s] * 0.1f), b = powf(10.0f, lv[s - 1] * 0.1f), c = powf(10.0f, lv[s - 2] * 0.1f);
            d1[s] = 10.0f * log10f((a + b + c) * (1.0f / 3.0f) + 1e-12f);
        }
        memcpy(d2, d1 + START, (SECS - START) * sizeof(float));
        qsort(d2, SECS - START, sizeof(float), cmpf);
        med = d2[(SECS - START) / 2];
        floor_db = med - 30.0f;
        for (s = START; s < SECS; s++)
            if (d1[s] < floor_db)
                d1[s] = floor_db;
        for (s = START + 1; s < SECS; s++) {
            float r = fabsf(d1[s] - d1[s - 1]);
            d2[n1++] = r;
            if (r > m1) {
                m1 = r;
                n = s;
            }
        }
        if (m1 > 2.0f) {
            printf("  fastest move at %d s (median level %.1f): ", n, (double)med);
            for (s = n - 4; s <= n + 2 && s < SECS; s++)
                printf("%.1f ", (double)d1[s]);
            printf("\n");
        }
        qsort(d2, n1, sizeof(float), cmpf);
        p1 = d2[(int)(0.99f * (float)n1)];
        for (s = START + 2; s < SECS; s++) {
            float r = fabsf(d1[s] - d1[s - 2]);
            d2[n2++] = r;
            if (r > m2)
                m2 = r;
        }
        qsort(d2, n2, sizeof(float), cmpf);
        p2 = d2[(int)(0.99f * (float)n2)];
        /* Phase 5: the 99th percentile against the limits and the single fastest second against twice them. (It was
         * "p99.9", which over 780 seconds is the fastest second itself: 0.60 to 1.07 dB/s from one performance to the
         * next. The Definition of done's p99.9 is taken over hours, by evolve_report. A real fault here, a step at an
         * entrance or a pumped gain, is 4-9 dB/s and is caught by the cap.) */
        printf("  pad alone, 15 min: 3 s level moves %.2f dB/s (max %.2f), %.2f dB in 2 s (max %.2f) at p99; "
               "governor level %.1f dB, trim %.1f\n", (double)p1, (double)m1, (double)p2, (double)m2,
               (double)en_cond[CD_LEVEL20], (double)en_cond[CD_TRIM]);
        CHECK(p1 <= 1.0f, "the pad's level moved %.2f dB/s (limit 1.0)", (double)p1);
        CHECK(p2 <= 1.5f, "the pad's level moved %.2f dB in 2 s (limit 1.5)", (double)p2);
        CHECK(m1 <= 2.0f, "the pad's level moved %.2f dB in one second", (double)m1);
        CHECK(m2 <= 3.0f, "the pad's level moved %.2f dB in two seconds", (double)m2);
        CHECK(!bad, "NaN");
        en_host_plate_mod(0.0f, 1);              /* the engine's plate again */
        /* the whole garden, 20 minutes: the governor's band is one-sided (Phase 5; Tomorrow's Man, TESTING #17: the piece may
         * grow quieter, it may not grow loud, and something always sounds): the 60 s level is never more than 3 dB
         * over its target and never more than 15 dB under it (the first two minutes are the opening). Phase 3-4
         * asked for within 3 dB three quarters of the time. */
        {
            int s, out = 0, over = 0;
            float worst = 0.0f, lowest = 0.0f;
            en_init();
            en_play(1);
            run(120.0f);
            for (s = 0; s < 18 * 60; s++) {
                float e;
                run(1.0f);
                e = en_cond[CD_LEVEL60] - en_cond[CD_TARGET];
                out += fabsf(e) > 3.0f;
                over += e > 3.0f;
                if (e < lowest)
                    lowest = e;
                if (fabsf(e) > fabsf(worst))
                    worst = e;
            }
            printf("  the garden, 20 min: 60 s level outside +-3 dB of the target %.0f%% of the time (worst %+.1f dB); "
                   "trim %.1f, make-up %.1f, shares %.0f/%.0f/%.0f/%.0f%%\n", (double)(100.0f * (float)out / (18.0f * 60.0f)),
                   (double)worst, (double)en_cond[CD_TRIM], (double)en_cond[CD_FF],
                   (double)(100.0f * en_cond[CD_SHARE_PAD]), (double)(100.0f * en_cond[CD_SHARE_BELLS]),
                   (double)(100.0f * en_cond[CD_SHARE_BASS]), (double)(100.0f * en_cond[CD_SHARE_GROUND]));
            CHECK(over == 0, "the mix was more than 3 dB over its target %d of %d seconds", over, 18 * 60);
            CHECK(lowest > -15.0f, "the mix fell %.1f dB under its target", (double)lowest);
            CHECK(!bad, "NaN");
        }
    }

    /* TIDE: the bells are a layer (TESTING #11): in two hours of the garden at Drift 50 (1x) they rest at least
     * once for a minute or more (the layer's level under -30 dB: no bell rings, the last ones have decayed) and
     * come back; their level, a slewed sum of walkers, never moves faster than the swell limit */
    {
        int s, rests = 0, back = 0, resting = 0, longest = 0, cur = 0, rate_bad = 0, pad_none = 0, pad_longest = 0;
        int pad_up = 0, pad_up_longest = 0, pad_up_total = 0, dips = 0, dips_back = 0, in_dip = 0;
        float dip_low = 0.0f;
        float prev = -60.0f;
        en_init();
        en_set(P_DRIFT, 50);
        en_play(1);
        run(60.0f);
        prev = en_cond[CD_BELLS];
        for (s = 0; s < 7200; s++) {
            float lv;
            run(1.0f);
            lv = en_cond[CD_BELLS];
            pad_none = en_cond[CD_PAD_VOICES] < 0.5f ? pad_none + 1 : 0;   /* Phase 5: the pad's rest is bounded too */
            if (pad_none > pad_longest)
                pad_longest = pad_none;
            {   /* 2026-11-13: and so is its turn. A voice up (within 15 dB of full) with the pad's recess not at work
                 * (the dip above -3 dB) for longer than a turn's 210 s and the dip's own way down and back is the pad
                 * taking over; and every recess comes back */
                int k, up = 0;
                const float dip = en_cond[CD_PAD_DIP];
                for (k = 0; k < EN_NPAD; k++)
                    up |= en_pad_amp[k] > 0.178f;
                pad_up = up && dip > -3.0f ? pad_up + 1 : 0;
                if (pad_up > pad_up_longest)
                    pad_up_longest = pad_up;
                pad_up_total += up && dip > -3.0f;
                if (dip < dip_low)
                    dip_low = dip;
                if (!in_dip && dip < -5.0f) {
                    in_dip = 1;
                    dips++;
                } else if (in_dip && dip > -0.5f) {
                    in_dip = 0;
                    dips_back++;
                }
            }
            if (fabsf(lv - prev) > 1.0f)
                rate_bad++;
            prev = lv;
            if (lv < -30.0f) {
                cur++;
                if (cur > longest)
                    longest = cur;
                if (cur == 60)
                    rests++;
                resting = 1;
            } else {
                cur = 0;
                if (resting && lv > -6.0f) {
                    back++;
                    resting = 0;
                }
            }
        }
        {   /* 2026-11-14: the pad's company calls the bells to a voice left alone, which ends a rest of theirs: they rest
             * less often (5-9% of the time under -30 dB in two takes; a quarter by the presence walker alone), so two
             * more hours are given for the first one */
            int more = 0;
            for (s = 0; s < 7200 && rests == 0; s++, more++) {
                float lv;
                run(1.0f);
                lv = en_cond[CD_BELLS];
                if (fabsf(lv - prev) > 1.0f)
                    rate_bad++;
                prev = lv;
                if (lv < -30.0f) {
                    cur++;
                    if (cur > longest)
                        longest = cur;
                    if (cur == 60)
                        rests++;
                } else
                    cur = 0;
            }
            if (more)
                printf("  the bells: no rest in the two hours; the first after %d s more\n", more);
        }
        printf("  the bells, 2 h at Drift 50: %d rests of a minute or more (longest %d s), back %d times, level %.1f dB\n",
               rests, longest, back, (double)en_cond[CD_BELLS]);
        printf("  the pad, the same 2 h: the longest stretch with no voice singing %d s\n", pad_longest);
        CHECK(pad_longest <= 720, "the pad sang nothing for %d s at Drift 50 (its rest ends after 4-10 min, then the entrance's own wait)", pad_longest);
        printf("  the pad's turn, the same 2 h: up and undipped %d%% of the time, %d s at a stretch at the most; %d recesses, %d back, the deepest %.1f dB\n",
               pad_up_total * 100 / 7200, pad_up_longest, dips, dips_back, (double)dip_low);
        CHECK(pad_up_longest <= 480, "the pad was up for %d s at a stretch (a turn is 75-210 s, 150-300 while the bass rests, then the bass's return and the recess)", pad_up_longest);
        CHECK(pad_up_total >= 7200 / 10, "the pad was up only %d s of two hours", pad_up_total);
        CHECK(dips >= 3, "only %d recesses of the pad in two hours", dips);   /* (5 to 12 in four builds of 2026-11-13: a turn
                                                 * counts only while a voice is up, and the pad rests by itself too) */
        CHECK(dips_back >= dips - 1, "%d recesses, %d back", dips, dips_back);
        CHECK(dip_low >= -15.5f, "a recess went to %.1f dB (15 at the most)", (double)dip_low);
        CHECK(rests >= 1, "the bells never rested in four hours at Drift 50");
        CHECK(back >= 1 || rests <= 1, "the bells rested and never came back");
        CHECK(rate_bad == 0, "the bells' level moved over 1 dB/s at %d moments", rate_bad);
        CHECK(!bad, "NaN");
    }

    /* TIDE Phase 4: the timbre cursor. In an hour of the garden at Drift 100 (4x) the nearest anchor changes at
     * least twice and three or more anchors are visited; the cursor stays in the landscape; a voice's colour and
     * ratio are taken as it enters (the render stays bounded and finite through every anchor) */
    {
        int s, changes = 0, seen = 0, k, outside = 0;
        const char *names[16], *last;
        en_init();
        en_set(P_DRIFT, 100);
        en_play(1);
        run(1.0f);
        last = en_anchor_name();
        names[seen++] = last;
        peak = 0.0f;
        for (s = 0; s < 3600; s++) {
            const char *a;
            run(1.0f);
            a = en_anchor_name();
            outside += en_cond[CD_CURSOR_X] < 0.0f || en_cond[CD_CURSOR_X] > 1.0f || en_cond[CD_CURSOR_Y] < 0.0f || en_cond[CD_CURSOR_Y] > 1.0f;
            if (a != last) {
                changes++;
                last = a;
                for (k = 0; k < seen; k++)
                    if (names[k] == a)
                        break;
                if (k == seen && seen < 16)
                    names[seen++] = a;
            }
        }
        printf("  the cursor, 1 h at Drift 100: %d anchor changes, %d anchors visited (now %s); peak %.3f\n", changes, seen, last,
               (double)peak);
        CHECK(changes >= 2, "the nearest anchor changed %d times in an hour at Drift 100", changes);
        CHECK(seen >= 3, "only %d anchors visited in an hour at Drift 100", seen);
        CHECK(outside == 0, "the cursor left the landscape at %d moments", outside);
        CHECK(!bad && peak < 0.99f, "NaN or clipping through the anchors (peak %f)", (double)peak);
        /* the Voices landscape too */
        en_set(P_PADTONE, 1);
        peak = 0.0f;
        run(600.0f);
        CHECK(!bad && peak > 0.02f && peak < 0.99f, "the Voices landscape: peak %f", (double)peak);
        /* and the Choir (2026-11-14): it sounds, and its voices sing an octave down (the window C2..F4, the register
         * walker 6 semitones either way) */
        en_set(P_PADTONE, 2);
        peak = 0.0f;
        run(600.0f);
        CHECK(!bad && peak > 0.02f && peak < 0.99f, "the Choir: peak %f", (double)peak);
        {
            int k, hi = 0;
            for (k = 0; k < EN_NPAD; k++)
                if (en_pad_amp[k] > 0.01f && en_pad_note[k] > hi)
                    hi = en_pad_note[k];
            printf("  the Choir, 10 min: peak %.3f, the highest voice sounding %d\n", (double)peak, hi);
            CHECK(hi <= 71, "a Choir voice sings %d (the window ends at 65, the walker adds 6 at the most)", hi);
        }
        en_set(P_PADTONE, 0);
    }

    /* TIDE Phase 5: the harmonic field (DESIGN section 7). In three hours of the garden at Drift 100 (4x: twelve
     * season hours) the field changes at least three times and visits three or more fields; every change is one
     * step: either the root holds and one pitch class moves a semitone, or the notes hold and the root moves a
     * fifth; the root and its fifth are always in the scale; the chord's tones are always in the field; and a pad
     * voice that is singing is never left on a note outside the field it entered in for long (it dissolves: under
     * -20 dB within four minutes) */
    {
        int s, changes = 0, seen = 0, k, bad_step = 0, no_fifth = 0, off_field = 0, home_r, home_p;
        float reg_lo = 0.0f, reg_hi = 0.0f;
        uint32_t b0, o0;
        uint32_t fields[64], last;
        char nm[16];
        en_init();
        en_set(P_DRIFT, 100);
        en_play(1);
        run(1.0f);
        home_r = en_field_root;
        home_p = en_field_pool;
        last = (uint32_t)en_field_root << 16 | en_field_pool;
        fields[seen++] = last;
        CHECK(home_r == 2 && home_p == EN_MODE_POOL[MODE_DORIAN], "the field starts at home (%s)", en_field_name(nm));
        printf("  the field, 3 h at Drift 100: %s", en_field_name(nm));
        b0 = bell_count;
        o0 = bell_off;
        bell_lo = 127;
        bell_hi = 0;
        for (s = 0; s < 3 * 3600; s++) {
            uint32_t f, a0, a1;
            int r0, r1;
            run(1.0f);
            if (en_cond[CD_REGISTER] < reg_lo)
                reg_lo = en_cond[CD_REGISTER];
            if (en_cond[CD_REGISTER] > reg_hi)
                reg_hi = en_cond[CD_REGISTER];
            f = (uint32_t)en_field_root << 16 | en_field_pool;
            r1 = en_field_root;
            a1 = ((uint32_t)en_field_pool << r1 | (uint32_t)en_field_pool >> (12 - r1)) & 0xFFFu;
            no_fifth += !(a1 >> r1 & 1u) || !(a1 >> ((r1 + 7) % 12) & 1u);
            for (k = 0; k < en_chord_n && k < EN_NPAD; k++)
                off_field += !(a1 >> (en_chord[k] % 12) & 1u);
            if (f == last)
                continue;
            r0 = (int)(last >> 16);
            a0 = ((last & 0xFFFu) << r0 | (last & 0xFFFu) >> (12 - r0)) & 0xFFFu;
            if (r0 == r1) {                      /* the set moved: one note gone, one come, a semitone apart */
                uint32_t gone = a0 & ~a1, come = a1 & ~a0;
                bad_step += popcount(gone) != 1 || popcount(come) != 1 ||
                            (come != ((gone << 1 | gone >> 11) & 0xFFFu) && come != ((gone >> 1 | gone << 11) & 0xFFFu));
            } else {                             /* the root moved: a fifth, the notes the same */
                bad_step += a0 != a1 || ((r1 - r0 + 12) % 12 != 7 && (r1 - r0 + 12) % 12 != 5);
            }
            changes++;
            last = f;
            printf(" > %s", en_field_name(nm));
            for (k = 0; k < seen; k++)
                if (fields[k] == f)
                    break;
            if (k == seen && seen < 64)
                fields[seen++] = f;
        }
        printf("\n  %d changes, %d fields\n", changes, seen);
        CHECK(changes >= 3, "the field changed %d times in 3 h at Drift 100", changes);
        CHECK(seen >= 3, "only %d fields in 3 h at Drift 100", seen);
        CHECK(bad_step == 0, "%d field changes were not one step", bad_step);
        CHECK(no_fifth == 0, "the root or its fifth left the scale at %d moments", no_fifth);
        CHECK(off_field == 0, "a chord tone outside the field at %d moments", off_field);
        /* Phase 5b: every bell is a note of the field, wherever the register walker is (Phases 3b to 5 added the
         * register to the note in plain semitones and 4 strikes in 10 were outside the scale; no test listened) */
        printf("  %u bells (notes %d..%d), the register walker %.1f..%.1f st: %u outside the field\n", bell_count - b0, bell_lo,
               bell_hi, (double)reg_lo, (double)reg_hi, bell_off - o0);
        CHECK(bell_count - b0 > 200u, "only %u bells in 3 h at Drift 100", bell_count - b0);
        CHECK(reg_hi - reg_lo > 4.0f, "the register walker moved only %.1f st", (double)(reg_hi - reg_lo));
        CHECK(bell_off == o0, "%u bells outside the field", bell_off - o0);
        CHECK(!bad, "NaN");
        /* the player's key brings the field home */
        en_set(P_ROOT, 9);
        run(0.1f);
        CHECK(en_field_root == 9 && en_field_pool == home_p, "a new Root: the field is %s", en_field_name(nm));
        en_pool(EN_MODE_POOL[MODE_PENT_MAJ]);
        run(0.1f);
        CHECK(en_field_root == 9 && en_field_pool == EN_MODE_POOL[MODE_PENT_MAJ], "a new scale: the field is %s", en_field_name(nm));
    }

    /* TIDE Phase 5c (Tomorrow's Man, TESTING #25: standing tones; no breathers). In an hour of the garden at Drift 100: no two
     * pad voices sing the same note at once (both above -30 dB); no voice rises past its ceiling (-3 dB, less 0.35 dB
     * a semitone above G4); the tide goes out at least three times, 5 dB or more, never more than 17, and comes back
     * each time; and at Hold an ebb stays where it is */
    {
        int s, k, j, unison = 0, ebbs = 0, out = 0, back = 0;
        float amp_max = 0.0f, over = 0.0f, deepest = 0.0f, held, e0;
        en_init();
        en_set(P_DRIFT, 100);
        en_play(1);
        for (s = 0; s < 3600; s++) {
            run(1.0f);
            for (k = 0; k < EN_NPAD; k++) {
                const float cap = powf(10.0f, (-3.0f - 0.35f * fmaxf((float)en_pad_note[k] - 67.0f, 0.0f)) / 20.0f);
                if (en_pad_amp[k] > amp_max)
                    amp_max = en_pad_amp[k];
                if (en_pad_amp[k] / cap > over)
                    over = en_pad_amp[k] / cap;
                for (j = 0; j < k; j++)
                    unison += en_pad_amp[k] > 0.0316f && en_pad_amp[j] > 0.0316f && en_pad_note[k] == en_pad_note[j];
            }
            if (en_cond[CD_EBB] < deepest)
                deepest = en_cond[CD_EBB];
            if (!out && en_cond[CD_EBB] < -5.0f) {
                out = 1;
                ebbs++;
            } else if (out && en_cond[CD_EBB] > -0.5f) {
                out = 0;
                back++;
            }
        }
        printf("  the pad, 1 h at Drift 100: loudest voice %.2f (%.2f of its ceiling), %d s of unison; %d ebbs, %d back, the deepest %.1f dB\n",
               (double)amp_max, (double)over, unison, ebbs, back, (double)deepest);
        CHECK(unison == 0, "two voices on one note for %d s", unison);
        CHECK(over <= 1.02f, "a voice at %.2f of its ceiling", (double)over);
        CHECK(ebbs >= 3 && back >= ebbs - 1, "%d ebbs, %d back", ebbs, back);
        CHECK(deepest >= -17.01f, "an ebb of %.1f dB", (double)deepest);
        for (s = 0; s < 1200 && en_cond[CD_EBB] > -4.0f; s++)   /* into the next ebb, then Hold */
            run(1.0f);
        en_set(P_DRIFT, 0);
        run(2.0f);
        e0 = en_cond[CD_EBB];
        run(60.0f);
        held = en_cond[CD_EBB];
        CHECK(e0 < -3.0f && fabsf(held - e0) < 0.01f, "Hold: the ebb moved from %.2f to %.2f dB", (double)e0, (double)held);
        CHECK(!bad, "NaN");
    }

    /* TIDE Phase 6: the main page's Mood and Space (offsets around the piece's own knobs; at 0 a render is the same
     * samples as before they existed: out/phase6's Garden take against Phase 5c's). The same garden (the generator
     * starts from a constant) at Drift 100, 90 minutes each, Mood fully dark and fully bright: the bright one's
     * weather is brighter (the conductor's brightness, which the pad's filter and index and the bells' tone follow)
     * and its harmonic field leans to brighter modes. (The tone itself is measured on a held state by the render
     * harness: two performances differ in their notes more than in their tone.) Then ten minutes with Space fully
     * near and fully far: the far one's plate returns more. And both show their sign on the screen */
    {
        int m, s;
        double br[2], shift[2], plate[2];
        char t[10];
        en_param_text(P_MOOD, 20, t);
        CHECK(!strcmp(t, "+20"), "Mood text %s", t);
        en_param_text(P_SPACE, -5, t);
        CHECK(!strcmp(t, "-5"), "Space text %s", t);
        CHECK(en_param_info(P_MOOD)->def == 0 && en_param_info(P_SPACE)->def == 0, "Mood and Space rest at 0");
        for (m = 0; m < 2; m++) {
            double b = 0.0, sh = 0.0;
            en_init();
            en_set(P_DRIFT, 100);
            en_set(P_MOOD, m ? 50 : -50);
            en_play(1);
            for (s = 0; s < 5400; s++) {
                run(1.0f);
                b += (double)en_cond[CD_BRIGHT];
                sh += (double)en_cond[CD_FIELD_SHIFT];
            }
            br[m] = b / 5400.0;
            shift[m] = sh / 5400.0;
        }
        printf("  Mood, 90 min at Drift 100: dark, the weather's brightness %.2f on average, the field %.2f mode steps from home; "
               "bright %.2f, %.2f\n", br[0], shift[0], br[1], shift[1]);
        CHECK(br[1] > br[0] + 0.6, "Mood bright is no brighter: %.2f against %.2f", br[1], br[0]);
        CHECK(shift[1] > shift[0] + 0.5, "Mood does not lean the field: %.2f against %.2f", shift[1], shift[0]);
        for (m = 0; m < 2; m++) {
            en_init();
            en_set(P_SPACE, m ? 50 : -50);
            en_play(1);
            run(120.0f);
            en_tap_reset();
            run(480.0f);
            /* against the dry layers: the two runs are two performances (the generator is not rewound) */
            plate[m] = en_tap_plate / (en_tap_dry[EN_TAP_PAD] + en_tap_dry[EN_TAP_BELLS] + en_tap_dry[EN_TAP_BASS] +
                                       en_tap_dry[EN_TAP_PULSE] + 1e-12);
        }
        printf("  Space, 8 min: the plate's return against the dry layers (energy): near %.3g, far %.3g\n", plate[0], plate[1]);
        CHECK(plate[1] > 4.0 * plate[0], "Space far is no wetter: %.3g against %.3g", plate[1], plate[0]);
        CHECK(!bad, "NaN");
    }

    /* SAMSARA Phase 7: the guard (amb.h en_load). A load between the two lines moves nothing; at or over the upper one
     * it sheds a step every 0.4 s, to three; the pad then sings one voice at most within half a minute (a full garden
     * before it: Density 100); an overrun's flag is a step; 20 s of calm give a step back, a minute all three */
    {
        uint32_t ms = 1000u;
        int k, before;
        en_init();
        en_set(P_DENSITY, 100);
        en_set(P_DRIFT, 100);
        en_play(1);
        run(120.0f);
        for (k = 0; k < 120 && (int)en_cond[CD_PAD_VOICES] < 2; k++)   /* until two voices or more sing (2 h at most) */
            run(60.0f);
        before = (int)en_cond[CD_PAD_VOICES];
        CHECK(before >= 2, "no full pad to shed from (%d voices)", before);
        en_load(EN_SHED_UP - 1u, ms);
        CHECK(en_shed == 0, "the guard sheds under its line (%d)", en_shed);
        en_overload();
        en_load(10u, ms += 16u);
        CHECK(en_shed == 1, "an overrun is not a step (%d)", en_shed);
        en_load(EN_SHED_UP, ms += 16u);
        CHECK(en_shed == 1, "two steps within 0.4 s (%d)", en_shed);
        for (k = 0; k < 4; k++)
            en_load(EN_SHED_UP, ms += 400u);
        CHECK(en_shed == 3, "the guard's last step is 3 (%d)", en_shed);
        peak = 0.0f;
        run(30.0f);
        printf("  the guard: %d pad voices before, %d half a minute after its last step, peak %.2f\n", before,
               (int)en_cond[CD_PAD_VOICES], (double)peak);
        CHECK((int)en_cond[CD_PAD_VOICES] <= 1, "the guard left %d pad voices", (int)en_cond[CD_PAD_VOICES]);
        CHECK(!bad && peak < 1.0f, "the guard's shedding clips or NaN (%f)", (double)peak);
        en_load(EN_SHED_DOWN + 1u, ms += 30000u);
        CHECK(en_shed == 3, "the guard lets go between its lines (%d)", en_shed);
        en_load(EN_SHED_DOWN, ms += 19000u);
        CHECK(en_shed == 3, "the guard lets go before 20 s of calm (%d)", en_shed);
        for (k = 0; k < 3; k++)
            en_load(EN_SHED_DOWN, ms += 20000u);
        CHECK(en_shed == 0, "the guard does not let go (%d)", en_shed);
    }

    /* stop: the tails die away */
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, en_param_info(i)->def);
    run(30.0f);
    en_play(0);
    run(40.0f);
    peak = 0.0f;
    run(1.0f);
    CHECK(peak < 0.01f, "quiet after stopping (%f)", (double)peak);
    CHECK(midi_count > 50u, "MIDI out (%u)", midi_count);

    printf(fails ? "amb: %d FAILED\n" : "amb: ok\n", fails);
    return fails != 0;
}
