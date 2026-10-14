/* stemrec.h: records several signals of a plugin (its voices, say) as one WAV file each, in the background, while it
 * plays. Written for Percolator (2026-10-12) and meant to be reused by any engine with separate voices: nothing here
 * knows about drums.
 *
 * Version 2 (2026-10-14, for 6W6 / 8W8 / CW-78 / 9W9 and LXRXXL): up to SR_MAXCH ring channels in up to SR_MAXF files,
 * a file mono or stereo (sr_init2), the ring sized to the channel count (SR_RING_BYTES), and optionally no file for a
 * channel that stayed silent through the whole take (S->drop_silent). sr_init and everything else work as in version 1,
 * so Percolator can take this file unchanged.
 *
 * Threads. The audio thread never waits, locks, allocates or touches a file: it copies each block into a ring buffer
 * (sr_write) and posts START / STOP marks into an event queue (sr_start, sr_stop), all lock-free (one writer, one
 * reader, C11-style atomics). A worker thread of the recorder's own drains the ring every 20 ms while armed, opens a
 * take's files, writes them and closes them. Asleep (not armed, no take open) it waits on a condition variable and
 * costs nothing.
 *
 * Every file of a take starts on the same sample and has the same length, so the files line up when they're laid
 * side by side in a DAW. If the card stalls for longer than the ring holds (6-12 s), the frames that didn't fit are
 * written as silence in every file (a GAP mark at that position), so the timeline still holds; the take's take.txt
 * counts them.
 *
 * Files: <base>/<YYYY-MM-DD HH-MM-SS> <label>/<prefix> <name>.wav, 32-bit float WAV (mono or stereo) at SR_RATE (no
 * clipping, no dither: what the engine computed), plus take.txt (tempo, start position, length, what each file holds). */
#pragma once
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SR_MAXCH 32                /* ring channels (a stereo file takes two) */
#define SR_MAXF 24                 /* files */
#define SR_RATE 44100
#define SR_RING_BYTES (24u << 20)  /* the ring's size: 2^19 frames (11.9 s) up to 12 channels, 2^18 (5.9 s) up to 24 */
#define SR_RING_MIN (1u << 16)
#define SR_RING_MAX (1u << 19)
#define SR_NEV 64                  /* START / STOP / GAP marks in flight */
#define SR_CHUNK 2048              /* frames the worker writes at a time */

#define SR_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define SR_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

enum { SR_EV_START, SR_EV_STOP, SR_EV_GAP };
enum { SR_OK, SR_ERR_OPEN, SR_ERR_WRITE };   /* the last take's trouble: couldn't make its folder / files, a write failed */

typedef struct {
    double bpm, ppq;      /* at the take's first frame; bpm 0 / ppq < 0: unknown */
    int transport;        /* 1: started by the host's transport, 0: by the switch */
    char label[40];       /* in the folder name (a kit name) */
    char mode[40];        /* written to take.txt */
} sr_meta_t;

typedef struct {
    int type;
    uint32_t pos;         /* stream position (frames sr_write has taken) where it applies */
    uint32_t len;         /* GAP: frames of silence */
    sr_meta_t meta;       /* START */
} sr_ev_t;

typedef struct {
    /* set up by sr_init / sr_init2 */
    int nch, nf, ok;
    int drop_silent;      /* set before a take: a file whose channels stayed at 0 is removed when the take ends */
    char prefix[32], base[480];
    char f_name[SR_MAXF][24], f_info[SR_MAXF][112];
    int f_ch[SR_MAXF], f_off[SR_MAXF];   /* a file's channel count (1, 2) and its first ring channel */
    uint32_t ring_n;      /* frames in the ring (a power of two) */
    float *ring;          /* ring_n frames x nch, interleaved */
    sr_ev_t ev[SR_NEV];
    uint32_t w, r, ev_w, ev_r;   /* ring: frames written (audio) / read (worker); events posted / done */
    /* audio thread */
    int rec, stop_pending, gap_open;
    uint32_t gap_len;
    uint32_t take_frames; /* frames in the take so far (read by the screen) */
    /* worker */
    pthread_t th;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int quit, armed;
    FILE *f[SR_MAXF];
    int open;             /* a take's files are open */
    uint64_t frames, gaps;
    char dir[600];
    sr_meta_t meta;
    time_t t0;
    float scratch[SR_CHUNK * 2];
    float peak[SR_MAXF];
    /* status, for the screen */
    int err;              /* SR_OK, SR_ERR_OPEN, SR_ERR_WRITE (the last take's) */
    int saved;            /* the last take closed: its start time as hhmmss + 1 (0: none yet) */
    int kept;             /* the last take closed: files kept (drop_silent) */
} stemrec_t;

/* ---- the worker ------------------------------------------------------------------------------------------------ */

static void sr_put16(unsigned char *p, unsigned v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
static void sr_put32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 255; }

/* 32-bit float WAV: RIFF, fmt (18 bytes, IEEE float), fact (sample frames), data. Sizes patched at the end. */
#define SR_HDR 58
static void sr_header(unsigned char *h, uint32_t frames, int ch) {
    uint32_t data = frames * 4u * (uint32_t)ch;
    memcpy(h, "RIFF", 4); sr_put32(h + 4, SR_HDR - 8 + data); memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); sr_put32(h + 16, 18);
    sr_put16(h + 20, 3); sr_put16(h + 22, (unsigned)ch); sr_put32(h + 24, SR_RATE); sr_put32(h + 28, SR_RATE * 4u * (uint32_t)ch);
    sr_put16(h + 32, 4u * (unsigned)ch); sr_put16(h + 34, 32); sr_put16(h + 36, 0);
    memcpy(h + 38, "fact", 4); sr_put32(h + 42, 4); sr_put32(h + 46, frames);
    memcpy(h + 50, "data", 4); sr_put32(h + 54, data);
}

static int sr_mkdirs(char *path) {   /* mkdir -p */
    for (char *p = path + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            int e = mkdir(path, 0777) && errno != EEXIST;
            *p = '/';
            if (e) return -1;
        }
    return mkdir(path, 0777) && errno != EEXIST ? -1 : 0;
}

static void sr_clean(char *s) {   /* a name fit for a folder on any card (exFAT too) */
    for (; *s; s++)
        if (strchr("/\\:*?\"<>|", *s) || (unsigned char)*s < 32) *s = '-';
}

static void sr_path(const stemrec_t *S, int k, char *p, int len) {
    snprintf(p, len, "%s/%s %s.wav", S->dir, S->prefix, S->f_name[k]);
}

static void sr_open(stemrec_t *S, const sr_meta_t *m) {
    S->meta = *m;
    S->frames = S->gaps = 0;
    memset(S->peak, 0, sizeof S->peak);
    S->t0 = time(NULL);
    struct tm tm;
    localtime_r(&S->t0, &tm);
    char label[40];
    snprintf(label, sizeof label, "%s", m->label);
    sr_clean(label);
    int n = snprintf(S->dir, sizeof S->dir, "%s/%04d-%02d-%02d %02d-%02d-%02d%s%s", S->base, tm.tm_year + 1900,
                     tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, label[0] ? " " : "", label);
    struct stat st;
    for (int k = 2; k < 100 && n > 0 && n < (int)sizeof S->dir - 4 && stat(S->dir, &st) == 0; k++)   /* taken: " 2" ... */
        snprintf(S->dir + n, sizeof S->dir - n, " %d", k);
    SR_STORE(S->err, SR_OK);
    if (sr_mkdirs(S->dir)) { SR_STORE(S->err, SR_ERR_OPEN); return; }
    unsigned char h[SR_HDR];
    for (int c = 0; c < S->nf; c++) {
        char p[700];
        sr_path(S, c, p, sizeof p);
        sr_header(h, 0, S->f_ch[c]);
        S->f[c] = fopen(p, "wb");
        if (!S->f[c] || fwrite(h, 1, SR_HDR, S->f[c]) != SR_HDR) {
            SR_STORE(S->err, SR_ERR_OPEN);
            for (int k = 0; k <= c; k++) if (S->f[k]) { fclose(S->f[k]); S->f[k] = NULL; }
            return;
        }
        setvbuf(S->f[c], NULL, _IOFBF, 1 << 16);
    }
    S->open = 1;
}

static void sr_fail(stemrec_t *S) {   /* a write failed (card full, card pulled): close what there is */
    SR_STORE(S->err, SR_ERR_WRITE);
    for (int c = 0; c < S->nf; c++) if (S->f[c]) { fclose(S->f[c]); S->f[c] = NULL; }
    S->open = 0;
}

static void sr_close(stemrec_t *S) {
    if (!S->open) return;
    unsigned char h[SR_HDR];
    int gone[SR_MAXF] = {0}, kept = 0;
    for (int c = 0; c < S->nf; c++) {
        FILE *f = S->f[c];
        sr_header(h, (uint32_t)S->frames, S->f_ch[c]);
        int bad = fflush(f) || fseek(f, 0, SEEK_SET) || fwrite(h, 1, SR_HDR, f) != SR_HDR || fflush(f);
        fsync(fileno(f));
        if (fclose(f) || bad) SR_STORE(S->err, SR_ERR_WRITE);
        S->f[c] = NULL;
        if (S->drop_silent && S->peak[c] <= 0.0f) {   /* nothing but zeros: no file */
            char p[700];
            sr_path(S, c, p, sizeof p);
            gone[c] = remove(p) == 0;
        }
        kept += !gone[c];
    }
    S->open = 0;
    char p[700];
    snprintf(p, sizeof p, "%s/take.txt", S->dir);
    FILE *t = fopen(p, "w");
    if (t) {
        struct tm tm;
        localtime_r(&S->t0, &tm);
        fprintf(t, "%s stems, recorded %04d-%02d-%02d %02d:%02d:%02d\n\n", S->prefix, tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        if (S->meta.label[0]) fprintf(t, "Kit:        %s\n", S->meta.label);
        if (S->meta.mode[0]) fprintf(t, "Stems:      %s\n", S->meta.mode);
        fprintf(t, "Format:     WAV, 32-bit float, %d Hz, every file the same length from the same sample\n", SR_RATE);
        fprintf(t, "Length:     %.3f s (%llu frames)\n", (double)S->frames / SR_RATE, (unsigned long long)S->frames);
        fprintf(t, "Started by: %s\n", S->meta.transport ? "PLAY (the take ended at STOP, with the sound's tail)"
                                                        : "the STEMS switch (no transport from the host)");
        if (S->meta.bpm > 0) fprintf(t, "Tempo:      %.2f BPM\n", S->meta.bpm);
        if (S->meta.ppq >= 0) {
            double q = S->meta.ppq;
            fprintf(t, "Start:      quarter note %.3f from the song's start (bar %d, beat %.2f in 4/4)\n", q,
                    (int)(q / 4.0) + 1, q - 4.0 * (int)(q / 4.0) + 1.0);
            if (S->meta.bpm > 0 && q > 0.001)
                fprintf(t, "            = %.3f s after it: to line these up with a song exported from the start, "
                           "place them there\n", q * 60.0 / S->meta.bpm);
        }
        if (S->gaps)
            fprintf(t, "Gaps:       %llu frames (%.2f s) the card couldn't take in time, written as silence so the "
                       "timeline holds\n", (unsigned long long)S->gaps, (double)S->gaps / SR_RATE);
        fprintf(t, "\nFiles:\n");
        for (int c = 0; c < S->nf; c++) {
            float pk = S->peak[c];
            const char *ch = S->f_ch[c] == 2 ? "stereo" : "mono";
            if (gone[c]) fprintf(t, "  %s %s.wav  silent all take, no file  %s\n", S->prefix, S->f_name[c], S->f_info[c]);
            else if (pk > 0) fprintf(t, "  %s %s.wav  %s, peak %+.1f dBFS%s  %s\n", S->prefix, S->f_name[c], ch, 20.0 * log10((double)pk),
                                     pk > 1.0f ? " (over 0 dBFS: fine in float, turn it down before converting)" : "", S->f_info[c]);
            else fprintf(t, "  %s %s.wav  %s, silent  %s\n", S->prefix, S->f_name[c], ch, S->f_info[c]);
        }
        fclose(t);
    }
    struct tm tm;
    localtime_r(&S->t0, &tm);
    SR_STORE(S->kept, kept);
    SR_STORE(S->saved, tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec + 1);
}

/* write frames [r, to) of the ring into the open take (dropped if none is open) */
static void sr_drain(stemrec_t *S, uint32_t to) {
    uint32_t r = S->r;
    while (r != to) {
        uint32_t i = r & (S->ring_n - 1), n = to - r;
        if (n > S->ring_n - i) n = S->ring_n - i;
        if (n > SR_CHUNK) n = SR_CHUNK;
        if (S->open) {
            const float *src = S->ring + (size_t)i * S->nch;
            for (int c = 0; c < S->nf && S->open; c++) {
                const int nc = S->f_ch[c], o = S->f_off[c];
                float pk = S->peak[c];
                for (uint32_t k = 0; k < n; k++)
                    for (int j = 0; j < nc; j++) {
                        float x = src[k * S->nch + o + j];
                        S->scratch[k * nc + j] = x;
                        x = x < 0 ? -x : x;
                        if (x > pk) pk = x;
                    }
                S->peak[c] = pk;
                if (fwrite(S->scratch, sizeof(float) * nc, n, S->f[c]) != n) sr_fail(S);
            }
            if (S->open) S->frames += n;
        }
        r += n;
        SR_STORE(S->r, r);   /* the audio thread may refill it */
    }
}

static void sr_silence(stemrec_t *S, uint32_t len) {
    memset(S->scratch, 0, sizeof S->scratch);
    while (len && S->open) {
        uint32_t n = len < SR_CHUNK ? len : SR_CHUNK;
        for (int c = 0; c < S->nf && S->open; c++)
            if (fwrite(S->scratch, sizeof(float) * S->f_ch[c], n, S->f[c]) != n) sr_fail(S);
        if (S->open) { S->frames += n; S->gaps += n; }
        len -= n;
    }
}

static void sr_process(stemrec_t *S) {
    for (;;) {
        uint32_t W = SR_LOAD(S->w);   /* first the frames, then the marks: a mark at or before W is already posted */
        int did = 0;
        while (S->ev_r != SR_LOAD(S->ev_w)) {
            sr_ev_t *e = &S->ev[S->ev_r % SR_NEV];
            if ((int32_t)(e->pos - W) > 0) break;
            sr_drain(S, e->pos);
            if (e->type == SR_EV_START) {
                sr_close(S);
                sr_open(S, &e->meta);
            } else if (e->type == SR_EV_STOP) {
                sr_close(S);
            } else sr_silence(S, e->len);
            SR_STORE(S->ev_r, S->ev_r + 1);
            did = 1;
        }
        if (S->r != W) { sr_drain(S, W); did = 1; }
        if (!did) return;
    }
}

static void *sr_main(void *arg) {
    stemrec_t *S = arg;
    pthread_mutex_lock(&S->mu);
    while (!S->quit) {
        pthread_mutex_unlock(&S->mu);
        sr_process(S);
        pthread_mutex_lock(&S->mu);
        if (S->quit) break;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long wait_ns = S->armed || S->open || S->ev_r != SR_LOAD(S->ev_w) ? 20000000L : 1000000000L;
        ts.tv_nsec += wait_ns;
        while (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&S->cv, &S->mu, &ts);
    }
    pthread_mutex_unlock(&S->mu);
    sr_process(S);   /* what's left, then close the take */
    sr_close(S);
    return NULL;
}

/* ---- setup (not the audio thread) ------------------------------------------------------------------------------- */

/* nf files, file k with chans[k] channels (1 or 2; chans NULL: all mono). names: one per file, used in the file names;
 * info (may be NULL): a line per file for take.txt. sr_write takes frames of all the files' channels, in file order. */
static inline int sr_init2(stemrec_t *S, const char *prefix, int nf, const char *const *names, const char *const *info,
                           const int *chans, const char *base) {
    if (S->ok) return 0;
    if (nf < 1 || nf > SR_MAXF) return -1;
    int nch = 0;
    for (int c = 0; c < nf; c++) {
        int k = chans ? chans[c] : 1;
        if (k < 1 || k > 2) return -1;
        S->f_ch[c] = k;
        S->f_off[c] = nch;
        nch += k;
    }
    if (nch > SR_MAXCH) return -1;
    S->nf = nf;
    S->nch = nch;
    snprintf(S->prefix, sizeof S->prefix, "%s", prefix);
    snprintf(S->base, sizeof S->base, "%s", base);
    for (int c = 0; c < nf; c++) {
        snprintf(S->f_name[c], sizeof S->f_name[c], "%s", names[c]);
        snprintf(S->f_info[c], sizeof S->f_info[c], "%s", info ? info[c] : "");
    }
    uint32_t n = SR_RING_MAX;
    while (n > SR_RING_MIN && (size_t)n * nch * sizeof(float) > SR_RING_BYTES) n >>= 1;
    S->ring_n = n;
    S->ring = calloc((size_t)n * nch, sizeof(float));   /* untouched pages cost nothing until the first take */
    if (!S->ring) return -1;
    pthread_mutex_init(&S->mu, NULL);
    pthread_cond_init(&S->cv, NULL);
    if (pthread_create(&S->th, NULL, sr_main, S)) {
        free(S->ring);
        S->ring = NULL;
        return -1;
    }
    SR_STORE(S->ok, 1);
    return 0;
}

/* version 1: nch mono files */
static inline int sr_init(stemrec_t *S, const char *prefix, int nch, const char *const *names, const char *const *info,
                          const char *base) {
    return sr_init2(S, prefix, nch, names, info, NULL, base);
}

/* armed: the worker looks every 20 ms (a take may start any moment); not armed it sleeps */
static inline void sr_arm(stemrec_t *S, int on) {
    if (!S->ok) return;
    pthread_mutex_lock(&S->mu);
    S->armed = on;
    pthread_cond_signal(&S->cv);
    pthread_mutex_unlock(&S->mu);
}

static inline void sr_free(stemrec_t *S) {
    if (!S->ok) return;
    pthread_mutex_lock(&S->mu);
    S->quit = 1;
    pthread_cond_signal(&S->cv);
    pthread_mutex_unlock(&S->mu);
    pthread_join(S->th, NULL);
    pthread_cond_destroy(&S->cv);
    pthread_mutex_destroy(&S->mu);
    free(S->ring);
    S->ring = NULL;
    S->ok = 0;
}

/* ---- the audio thread ------------------------------------------------------------------------------------------- */

static inline int sr_post(stemrec_t *S, int type, uint32_t len, const sr_meta_t *m) {
    uint32_t ew = S->ev_w;
    if (ew - SR_LOAD(S->ev_r) >= SR_NEV) return -1;   /* full: the caller retries */
    sr_ev_t *e = &S->ev[ew % SR_NEV];
    e->type = type;
    e->pos = S->w;
    e->len = len;
    if (m) e->meta = *m;
    SR_STORE(S->ev_w, ew + 1);
    return 0;
}

static inline int sr_close_gap(stemrec_t *S) {
    if (!S->gap_open) return 0;
    if (sr_post(S, SR_EV_GAP, S->gap_len, NULL)) return -1;
    S->gap_open = 0;
    return 0;
}

/* each block, before sr_write: retries a STOP that found the queue full */
static inline void sr_poll(stemrec_t *S) {
    if (S->stop_pending && !sr_close_gap(S) && !sr_post(S, SR_EV_STOP, 0, NULL)) S->stop_pending = 0;
}

static inline int sr_recording(const stemrec_t *S) { return S->rec; }

static inline void sr_stop(stemrec_t *S) {
    if (!S->ok || !S->rec) return;
    S->rec = 0;
    S->stop_pending = 1;
    sr_poll(S);
}

static inline int sr_start(stemrec_t *S, const sr_meta_t *m) {
    if (!S->ok) return -1;
    sr_stop(S);
    if (S->stop_pending || sr_post(S, SR_EV_START, 0, m)) return -1;
    S->rec = 1;
    __atomic_store_n(&S->take_frames, 0, __ATOMIC_RELAXED);
    return 0;
}

/* frames of nch channels, interleaved */
static inline void sr_write(stemrec_t *S, const float *x, int frames) {
    if (!S->rec || frames <= 0) return;
    uint32_t used = S->w - SR_LOAD(S->r), room = S->ring_n - used;
    __atomic_store_n(&S->take_frames, S->take_frames + (uint32_t)frames, __ATOMIC_RELAXED);
    if (S->gap_open) {   /* the card fell behind: carry on only with a quarter of the ring free again */
        if (room < S->ring_n / 4 || sr_close_gap(S)) { S->gap_len += (uint32_t)frames; return; }
    }
    if (room < (uint32_t)frames) {
        S->gap_open = 1;
        S->gap_len = (uint32_t)frames;
        return;
    }
    uint32_t w = S->w;
    for (int k = 0; k < frames; k++, w++)
        memcpy(S->ring + (size_t)(w & (S->ring_n - 1)) * S->nch, x + (size_t)k * S->nch, sizeof(float) * S->nch);
    SR_STORE(S->w, w);
}
