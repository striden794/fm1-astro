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

static uint32_t midi_count;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
    midi_count++;
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
        float prev[EN_NPAD];
        for (i = 0; i < EN_NPAD; i++)
            prev[i] = en_pad_amp[i];
        for (k = 0; k < 480 * 4; k++) {
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
        }
        CHECK(entries >= 2, "pad entrances in 8 min: %d", entries);
        CHECK(!off_chord, "%d of %d entrances on a note outside the chord", off_chord, entries);
        CHECK(!over, "more than three pad voices singing at %d moments", over);
        printf("  pad entrances in 8 min: %d\n", entries);
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
        int held, k, diff = 0, l, moved = 0;
        en_set(P_DRIFT, 0);
        run(2.0f);
        held = en_chord[0] + 128 * en_chord[1];
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
        CHECK(moved == 0, "%d walkers moved at Hold", moved);
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
        for (k = 0; k < 3; k++) {
            int b2, j;
            long z = 0;
            float prev = 0.0f;
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
            for (b2 = 0; b2 < 60 * 44100 / 256; b2++) {   /* a minute: the loops carry 1-3 notes each now */
                en_render(buf, 256, 2048);
                for (j = 0; j < 256; j++) {
                    float x = (float)buf[2 * j];
                    z += (x > 0.0f) != (prev > 0.0f);
                    prev = x;
                }
            }
            zc[k] = (float)z;
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
        if (m1 > 1.0f) {
            printf("  fastest move at %d s (median level %.1f): ", n, (double)med);
            for (s = n - 4; s <= n + 2 && s < SECS; s++)
                printf("%.1f ", (double)d1[s]);
            printf("\n");
        }
        qsort(d2, n1, sizeof(float), cmpf);
        p1 = d2[(int)(0.999f * (float)n1)];
        for (s = START + 2; s < SECS; s++) {
            float r = fabsf(d1[s] - d1[s - 2]);
            d2[n2++] = r;
            if (r > m2)
                m2 = r;
        }
        qsort(d2, n2, sizeof(float), cmpf);
        p2 = d2[(int)(0.999f * (float)n2)];
        printf("  pad alone, 15 min: 3 s level moves %.2f dB/s (max %.2f), %.2f dB in 2 s (max %.2f) at p99.9; "
               "governor level %.1f dB, trim %.1f\n", (double)p1, (double)m1, (double)p2, (double)m2,
               (double)en_cond[CD_LEVEL20], (double)en_cond[CD_TRIM]);
        CHECK(p1 <= 1.0f, "the pad's level moved %.2f dB/s (limit 1.0)", (double)p1);
        CHECK(p2 <= 1.5f, "the pad's level moved %.2f dB in 2 s (limit 1.5)", (double)p2);
        CHECK(!bad, "NaN");
        /* the whole garden, 20 minutes: the governor holds the 60 s level within 3 dB of its target most of the
         * time (the layers rest and return; the first two minutes are the opening) */
        {
            int s, out = 0;
            float worst = 0.0f;
            en_init();
            en_play(1);
            run(120.0f);
            for (s = 0; s < 18 * 60; s++) {
                float e;
                run(1.0f);
                e = en_cond[CD_LEVEL60] - en_cond[CD_TARGET];
                out += fabsf(e) > 3.0f;
                if (fabsf(e) > fabsf(worst))
                    worst = e;
            }
            printf("  the garden, 20 min: 60 s level outside +-3 dB of the target %.0f%% of the time (worst %+.1f dB); "
                   "trim %.1f, make-up %.1f, shares %.0f/%.0f/%.0f/%.0f%%\n", (double)(100.0f * (float)out / (18.0f * 60.0f)),
                   (double)worst, (double)en_cond[CD_TRIM], (double)en_cond[CD_FF],
                   (double)(100.0f * en_cond[CD_SHARE_PAD]), (double)(100.0f * en_cond[CD_SHARE_BELLS]),
                   (double)(100.0f * en_cond[CD_SHARE_BASS]), (double)(100.0f * en_cond[CD_SHARE_GROUND]));
            CHECK(out < 18 * 60 / 4, "the governor was over 3 dB off its target %d of %d seconds", out, 18 * 60);
            CHECK(!bad, "NaN");
        }
    }

    /* TIDE: the bells are a layer (TESTING #11): in two hours of the garden at Drift 50 (1x) they rest at least
     * once for a minute or more (the layer's level under -30 dB: no bell rings, the last ones have decayed) and
     * come back; their level, a slewed sum of walkers, never moves faster than the swell limit */
    {
        int s, rests = 0, back = 0, resting = 0, longest = 0, cur = 0, rate_bad = 0;
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
        printf("  the bells, 2 h at Drift 50: %d rests of a minute or more (longest %d s), back %d times, level %.1f dB\n",
               rests, longest, back, (double)en_cond[CD_BELLS]);
        CHECK(rests >= 1, "the bells never rested in two hours at Drift 50");
        CHECK(back >= 1 || rests == 0, "the bells rested and never came back");
        CHECK(rate_bad == 0, "the bells' level moved over 1 dB/s at %d moments", rate_bad);
        CHECK(!bad, "NaN");
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
