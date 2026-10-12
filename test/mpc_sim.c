/* LXRXXL: the plugin driven the way MPC drives it, through the VST2 wrapper only (no engine internals): MPC's transport
 * and song position, pads as notes, screen taps as parameter sets. 0.1.0 passed the engine tests and made no sound on a
 * Force (MPC's song position never reached the sequencer, the pads were taken as panel keys on assumed notes), so every
 * path to sound is checked here the way a user meets it. Exit 1 on failure. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "params.h"
#include "param_ids.h"

typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
 void *p; void (*setP)(AEffect *, int32_t, float); float (*getP)(AEffect *, int32_t);
 int32_t np, npar, ni, no, flags; intptr_t r1, r2; int32_t a, b, c; float io; void *obj, *user; int32_t uid, ver;
 void (*pr)(AEffect *, float **, float **, int32_t); void *pdr; char f[56]; };
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void *ev[2]; } EV;
extern AEffect *VSTPluginMain(cb);

static double ti[16];           /* VstTimeInfo: [3] ppqPos, [4] tempo, flags = ((int32_t *)&ti[8])[5] */
static int playing;
static double tempo = 120.0, ppq;
static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o)
{
    if (op == 7) {   /* audioMasterGetTime: as MPC answers it */
        memset(ti, 0, sizeof ti);
        ti[1] = 44100.0; ti[3] = ppq; ti[4] = tempo;
        ((int32_t *)&ti[8])[5] = (1 << 10) | (1 << 9) | (playing ? 2 : 0);
        return (intptr_t)ti;
    }
    return 0;
}

/* render `sec` seconds in MPC's 256-frame blocks, the song position moving while playing; rms of the output */
static double run(AEffect *a, double sec)
{
    float L[256], R[256], *o[2] = {L, R};
    double e = 0;
    int k, i, blocks = (int)(sec * 44100 / 256);
    for (k = 0; k < blocks; k++) {
        a->pr(a, 0, o, 256);
        for (i = 0; i < 256; i++) e += L[i] * L[i] + R[i] * R[i];
        if (playing) ppq += 256.0 * tempo / 60.0 / 44100.0;
    }
    return sqrt(e / (blocks * 512.0));
}

static void note(AEffect *a, int status, int n, int v)
{
    ME m = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)status, (unsigned char)n, (unsigned char)v, 0}};
    EV ev = {1, 0, {&m, 0}};
    a->d(a, 25, 0, 0, &ev, 0);
}
static void pad(AEffect *a, int n) { note(a, 0x90, n, 100); run(a, 0.02); note(a, 0x80, n, 0); run(a, 0.01); }
static void tap(AEffect *a, int p) { a->setP(a, p, a->getP(a, p) > 0.5f ? 0.f : 1.f); run(a, 0.02); }

int main(void)
{
    AEffect *a = VSTPluginMain(host);
    double r;
    a->d(a, 12, 0, 1, 0, 0);   /* effMainsChanged on */
    /* silence before anything */
    r = run(a, 0.5);
    CHECK(r < 1e-5, "silent while MPC is stopped and nothing is played (%.5f)", r);
    /* a new LXRXXL is on the factory project: MPC's play plays its pattern 1 with its kit */
    playing = 1; ppq = 0;
    r = run(a, 2.0);
    CHECK(r > 0.003, "MPC play: the factory pattern 1 plays (rms %.4f)", r);
    playing = 0;
    run(a, 3.0);
    /* clear the pattern (SHIFT-less COPY/CLR menu is the machine's way; here: RESET then clear by hand is not needed,
     * the next checks only add steps) */
    /* steps 1 and 5 on DRUM1 from the screen's step keys, then MPC's play plays them */
    tap(a, P_S1); tap(a, P_S5);
    playing = 1; ppq = 0;
    r = run(a, 2.0);
    CHECK(r > 0.003, "MPC play: the LXR's steps sound (rms %.4f)", r);
    playing = 0;
    run(a, 2.0);
    r = run(a, 1.0);
    CHECK(r < 0.002, "MPC stop: it stops, only the FX tail left (%.5f)", r);
    /* the plugin's own PLAY, MPC stopped, tempo still "MPC" */
    tap(a, P_PLAY);
    r = run(a, 2.0);
    CHECK(r > 0.003, "the LXR's PLAY with MPC stopped: it plays at MPC's tempo (rms %.4f)", r);
    tap(a, P_PLAY);
    run(a, 2.0);
    r = run(a, 1.0);
    CHECK(r < 0.002, "PLAY again stops it (%.5f)", r);
    /* pads before any pad learn: they play the voices */
    note(a, 0x90, 36, 110);
    r = run(a, 0.3);
    note(a, 0x80, 36, 0);
    CHECK(r > 0.002, "a pad (note 36) plays DRUM1 before any pad learn (rms %.4f)", r);
    run(a, 2.0);
    note(a, 0x90, 72, 110);
    r = run(a, 0.3);
    note(a, 0x80, 72, 0);
    CHECK(r > 0.002, "any other pad (note 72) plays the selected voice (rms %.4f)", r);
    run(a, 2.0);
    /* MPC's step sequencer on the 16 drum pads, in pad order: pad n = voice n, from bank A (36..) or the drum-pad patch (0..) */
    {
        int t, ok36 = 1, ok0 = 1;
        for (t = 0; t < 7; t++) {
            static const int padNote[7] = {36, 38, 40, 41, 43, 45, 47};   /* the Force's pads 1-7: C major from C1 */
            note(a, 0x90, padNote[t], 110); r = run(a, 0.2); note(a, 0x80, padNote[t], 0); run(a, 1.5);
            if (!(r > 0.0005)) ok36 = 0, printf("     note %d silent\n", padNote[t]);
            note(a, 0x90, t, 110); r = run(a, 0.2); note(a, 0x80, t, 0); run(a, 1.5);
            if (!(r > 0.0005)) ok0 = 0, printf("     note %d silent\n", t);
        }
        CHECK(ok36, "the Force's pads 1-7 (C major from 36: 36 38 40 41 43 45 47) play DRUM1 .. OP HH");
        CHECK(ok0, "pads 1-7 of the drum-pad layout (notes 0-6) play DRUM1 .. OP HH");
        note(a, 0x90, 50, 110); r = run(a, 0.2); note(a, 0x80, 50, 0);
        CHECK(r < 1e-5, "pad 9 (note 50, no voice) stays silent (%.5f)", r);
        run(a, 1.0);
    }
    /* the pots stand on their value and turn it end to end; the DATA knob on the selection */
    {
        float p0, p1, d0, d1;
        tap(a, P_OSC);
        run(a, 0.05);
        a->setP(a, P_POT2, 0.f); run(a, 0.02);
        p0 = a->getP(a, P_POT2);
        a->setP(a, P_POT2, 1.f); run(a, 0.02);
        p1 = a->getP(a, P_POT2);
        CHECK(p0 < 0.01f && p1 > 0.99f, "pot 2 turns its value from the start to the end (%.2f -> %.2f)", p0, p1);
        a->setP(a, P_POT2, p1 - 1.f / 128); run(a, 0.02);
        CHECK(a->getP(a, P_POT2) < p1, "a Q-Link event back from the end steps back (%.3f)", a->getP(a, P_POT2));
        d0 = a->getP(a, P_DATA);
        a->setP(a, P_POT3, 0.7f); run(a, 0.02);
        a->setP(a, P_POT4, 0.3f); run(a, 0.02);
        CHECK(fabsf(a->getP(a, P_DATA) - d0) < 1e-4f, "pots 3 and 4 leave the DATA knob where it is (%.2f -> %.2f)", d0, a->getP(a, P_DATA));
        a->setP(a, P_DATA, d0 + 1.f / 128); run(a, 0.02);
        d1 = a->getP(a, P_DATA);
        CHECK(d1 > d0, "the DATA knob turns one place with the selection (%.2f -> %.2f)", d0, d1);
        a->setP(a, P_DATA, 1.f); run(a, 0.02);
        CHECK(a->getP(a, P_DATA) > 0.99f, "and reaches the end of the page (%.2f)", a->getP(a, P_DATA));
    }
    /* LOAD > KIT: the DATA legend (the encoder's push) blinks while a kit is picked; a tap on it, or the KIT step key
     * again, loads the kit shown */
    {
        char l1[64], l2[64], want[64];
        int i, on = 0, off = 0;
        tap(a, P_LOAD); tap(a, P_S8);
        a->d(a, 7, P_LCD1, 0, l1, 0);
        CHECK(strstr(l1, "Load: Kit") != NULL, "LOAD then the KIT key: '%s'", l1);
        for (i = 0; i < 40; i++) { run(a, 0.02); if (a->getP(a, P_ENC) > 0.5f) on++; else off++; }
        CHECK(on > 3 && off > 3, "the DATA push blinks while it waits (%d lit, %d unlit)", on, off);
        a->setP(a, P_DATA, a->getP(a, P_DATA) + 3.f / 128); run(a, 0.05);
        a->d(a, 7, P_LCD2, 0, want, 0);
        tap(a, P_ENC); run(a, 0.05);
        a->d(a, 7, P_LCD2, 0, l2, 0);
        CHECK(strstr(l2, "loaded") != NULL, "a tap on the DATA legend loads '%s' ('%s')", want, l2);
        run(a, 1.0);
        a->d(a, 7, P_LCD1, 0, l1, 0);
        CHECK(strstr(l1, "Load") == NULL, "and LOAD closes ('%s')", l1);
        for (i = 0, on = 0; i < 20; i++) { run(a, 0.02); if (a->getP(a, P_ENC) > 0.5f) on++; }
        CHECK(on == 0, "the DATA push is dark again (%d)", on);
        tap(a, P_LOAD); tap(a, P_S8);
        a->setP(a, P_DATA, a->getP(a, P_DATA) - 3.f / 128); run(a, 0.05);   /* back to the kit it had */
        tap(a, P_S8); run(a, 0.05);
        a->d(a, 7, P_LCD2, 0, l2, 0);
        CHECK(strstr(l2, "loaded") != NULL, "the KIT key pressed again loads it too ('%s')", l2);
        run(a, 1.0);
    }
    /* pad learn on a layout no one assumed: rows of 8 from note 48, 1 apart */
    tap(a, P_SHIFT);
    tap(a, P_S12);   /* CONFIG */
    a->setP(a, P_SLOT8, 1.0f);   /* lrn */
    run(a, 0.05);
    pad(a, 48); pad(a, 49); pad(a, 56);
    run(a, 0.1);
    /* now the pads are the panel: row 3 pad 2 = PLAY */
    pad(a, 48 + 16 + 1);
    r = run(a, 2.0);
    CHECK(r > 0.003, "after learning (48 / 49 / 56) the PLAY pad starts the LXR (rms %.4f)", r);
    pad(a, 48 + 16 + 1);
    run(a, 2.0);
    r = run(a, 1.0);
    CHECK(r < 0.002, "and stops it (%.5f)", r);
    a->d(a, 1, 0, 0, 0, 0);   /* effClose */
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails ? 1 : 0;
}
