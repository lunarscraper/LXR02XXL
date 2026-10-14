/* LXRXXL STEMS, offline (x86; test/run_tests.sh runs it under valgrind): the PERF page's "Stems" slot switches it, the
 * sound is the same bit for bit with STEMS on or off, PLAY ... STOP makes one take of stereo WAV files (a file per voice
 * that played, FX if it did, MIX, take.txt), every file the same length, the voices and FX add up to MIX, STOP keeps the
 * tail, and STEMS is kept in the MPC project. */
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"

static const mpc_engine_t *E;
static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

static double ppq;
/* n blocks of 128 with MPC playing (or not), its position moving at 120 BPM; the output kept in o (may be NULL) */
static void run(void *I, int blocks, int playing, int16_t *o)
{
    int16_t b[256];
    char t[96];
    for (int k = 0; k < blocks; k++) {
        snprintf(t, sizeof t, "%d %.9f 120.0", playing, ppq);
        E->set_param(I, "host_time", t);
        E->render(I, b, 128);
        if (o) memcpy(o + 256 * k, b, sizeof b);
        if (playing) ppq += 128.0 / 44100.0 * 2.0;
    }
}

static void status(void *I, char *b) { b[0] = 0; E->get_param(I, "stems_status", b, 32); }

typedef struct { char name[300]; float *x; long n; int ch; } wav_t;

static int read_wav(const char *path, wav_t *w)
{
    FILE *f = fopen(path, "rb");
    unsigned char h[58];
    if (!f) return -1;
    if (fread(h, 1, 58, f) != 58 || memcmp(h, "RIFF", 4) || memcmp(h + 50, "data", 4) || h[20] != 3) { fclose(f); return -1; }
    uint32_t bytes = h[54] | h[55] << 8 | h[56] << 16 | (uint32_t)h[57] << 24;
    w->ch = h[22];
    w->n = bytes / 4 / w->ch;
    w->x = malloc(bytes + 8);
    long got = (long)fread(w->x, 4 * w->ch, w->n, f);
    fclose(f);
    return got == w->n ? 0 : -1;
}

static int take_dirs(const char *dir, char out[][600], int max)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    while (d && (e = readdir(d)) && n < max) if (e->d_name[0] != '.') snprintf(out[n++], 600, "%s/%s", dir, e->d_name);
    if (d) closedir(d);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (strcmp(out[i], out[j]) > 0) { char t[600]; strcpy(t, out[i]); strcpy(out[i], out[j]); strcpy(out[j], t); }
    return n;
}

int main(void)
{
    const char *out = "build/host/stems";
    char cmd[256], st[40], t[4][600];
    static char buf[1 << 19];
    int16_t *a = malloc(sizeof(int16_t) * 256 * 1500), *b = malloc(sizeof(int16_t) * 256 * 1500);
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", out, out);
    if (system(cmd)) return 2;
    setenv("LXRXXL_STEMS_DIR", out, 1);
    E = mpc_engine();

    /* the PERF page's slot 6 is Stems */
    void *I = E->create(NULL);
    E->set_param(I, "perf", "1");
    E->get_param(I, "slot6_name", st, sizeof st);
    CHECK(!strcasecmp(st, "stm"), "PERF page, slot 6: '%s'", st);
    E->get_param(I, "slot6_display", st, sizeof st);
    CHECK(!strcmp(st, "off"), "Stems off: '%s'", st);
    E->set_param(I, "slot6", "1");
    E->get_param(I, "stems", st, sizeof st);
    CHECK(!strcmp(st, "1"), "the slot switches STEMS on (%s)", st);

    /* bit for bit: the factory pattern played by MPC's transport, STEMS on (I) and off (J) */
    void *J = E->create(NULL);
    ppq = 0;
    run(I, 1500, 1, a);
    ppq = 0;
    run(J, 1500, 1, b);
    CHECK(!memcmp(a, b, sizeof(int16_t) * 256 * 1500), "the sound is the same with STEMS on and off");
    double rms = 0;
    for (int i = 0; i < 256 * 1500; i++) rms += (double)a[i] * a[i];
    CHECK(rms > 0, "the pattern plays");
    status(I, st);
    CHECK(!strncmp(st, "REC 0:04", 8), "status while recording: '%s'", st);
    E->get_param(I, "slot6_display", buf, 64);
    CHECK(!strcmp(buf, st), "the slot shows it: '%s'", buf);
    run(I, 10, 0, NULL);
    status(I, st);
    CHECK(!strncmp(st, "end", 3), "STOP: the tail: '%s'", st);
    run(I, 5300, 0, NULL);
    for (int k = 0; k < 100; k++) { status(I, st); if (!strncmp(st, "sv", 2)) break; usleep(20000); run(I, 1, 0, NULL); }
    CHECK(!strncmp(st, "sv", 2), "saved: '%s'", st);

    int nt = take_dirs(out, t, 4);
    CHECK(nt == 1, "one take (%d)", nt);
    if (nt == 1) {
        wav_t w[8];
        int nw = 0, mix = -1;
        DIR *d = opendir(t[0]);
        struct dirent *e;
        while (d && (e = readdir(d)) && nw < 8) {
            size_t l = strlen(e->d_name);
            if (l < 5 || strcmp(e->d_name + l - 4, ".wav")) continue;
            char p[1000];
            snprintf(p, sizeof p, "%s/%s", t[0], e->d_name);
            snprintf(w[nw].name, sizeof w[nw].name, "%s", e->d_name);
            if (read_wav(p, &w[nw])) { CHECK(0, "%s unreadable", e->d_name); continue; }
            if (strstr(e->d_name, " MIX.wav")) mix = nw;
            nw++;
        }
        if (d) closedir(d);
        printf("     take: %d files, %.2f s\n", nw, nw ? w[0].n / 44100.0 : 0);
        CHECK(mix >= 0 && nw >= 3, "MIX and voices (%d files)", nw);
        for (int i = 0; i < nw; i++) CHECK(w[i].ch == 2 && w[i].n == w[0].n, "%s: stereo, %ld frames", w[i].name, w[i].n);
        CHECK(nw && w[0].n >= 1500L * 128, "the take is the song and its tail (%ld frames)", nw ? w[0].n : 0);
        if (mix >= 0) {
            double err = 0, peak = 0;
            for (long k = 0; k < w[mix].n; k++)
                for (int c = 0; c < 2; c++) {
                    double s = 0;
                    for (int i = 0; i < nw; i++) if (i != mix) s += w[i].x[2 * k + c];
                    if (fabs(w[mix].x[2 * k + c]) > 0.99) continue;
                    if (fabs(s - w[mix].x[2 * k + c]) > err) err = fabs(s - w[mix].x[2 * k + c]);
                    if (fabs(s) > peak) peak = fabs(s);
                }
            CHECK(peak > 0.01 && err < 1e-4, "the voices and FX add up to MIX (max error %.6f, peak %.3f)", err, peak);
            /* MIX is the first 1500 blocks as played */
            long same = 0;
            for (long k = 0; k < 1500L * 128; k++) same += (int16_t)lrintf(w[mix].x[2 * k] * 32768.f) == a[2 * k];
            CHECK(same == 1500L * 128, "MIX is the output, from the first block after PLAY (%ld of %ld)", same, 1500L * 128);
        }
        for (int i = 0; i < nw; i++) free(w[i].x);
        char p[700];
        snprintf(p, sizeof p, "%s/take.txt", t[0]);
        FILE *f = fopen(p, "r");
        size_t r = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
        buf[r] = 0;
        if (f) fclose(f);
        CHECK(strstr(buf, "Tempo:      120.00 BPM") && strstr(buf, "quarter note 0.000"), "take.txt: tempo and start");
    }

    /* kept in the MPC project; a project from before leaves it */
    int n = E->get_param(I, "state", buf, sizeof buf);
    CHECK(n > 0 && n < (int)sizeof buf, "state (%d bytes)", n);
    void *K = E->create(NULL);
    E->set_param(K, "state", buf);
    E->get_param(K, "stems", st, sizeof st);
    CHECK(!strcmp(st, "1"), "STEMS restored with the project (%s)", st);
    E->get_param(J, "state", buf, sizeof buf);   /* J: STEMS off */
    E->set_param(K, "state", buf);
    E->get_param(K, "stems", st, sizeof st);
    CHECK(!strcmp(st, "0"), "and switched off by a project saved with it off (%s)", st);

    /* STEMS off during a take ends it at once */
    E->set_param(I, "stems", "1");
    ppq = 0;
    run(I, 100, 1, NULL);
    E->set_param(I, "stems", "0");
    run(I, 2, 1, NULL);
    usleep(300000);
    nt = take_dirs(out, t, 4);
    CHECK(nt == 2, "STEMS off: the second take is closed (%d takes)", nt);

    E->destroy(K);
    E->destroy(J);
    E->destroy(I);
    free(a);
    free(b);
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
