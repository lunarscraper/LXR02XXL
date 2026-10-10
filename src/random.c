/* LXRXXL mod: the two randomizers of the modded panel.
 *
 * VOICE RANDOM (the green key, its knob 0-100 % in tens): every sound parameter of the six voices moves by a random
 * amount of up to that share of its range, up or down, each one on its own draw. A parameter never lands where it
 * already is, nor back where it was before the last press, so a row of presses keeps going somewhere new. Whatever
 * would silence a voice stays where it can sound: the voice volumes, the amp envelope decays, the oscillator pitch and
 * a low-pass/band filter's cutoff keep at least 30 % of their range, a high-pass cutoff at most 70 %, an attack at most
 * 70 %; the outputs, the voices' modulation routings and their amp-envelope switch are left alone.
 *
 * STEP RANDOM (the yellow key, its knob in tens): a new beat on the working pattern, a euclidean rhythm per voice. The
 * knob sets the density: at n % each voice gets about n % of its track's steps as hits, the share drawn anew per voice
 * (half to all of it), spread evenly over the track (Bresenham), turned by a random rotation, with random accents;
 * a press never repeats the beat before it. */
#include <string.h>
#include <time.h>
#include "lxr.h"
#include "ParameterArray.h"
#include "menu.h"

/* xorshift64*, seeded per instance from the clock and the instance's address */
static uint64_t next(Lxr *x)
{
    uint64_t s = x->rnd.state;
    if (!s) s = ((uint64_t)time(NULL) << 32) ^ (uint64_t)(uintptr_t)x ^ 0x9E3779B97F4A7C15ull ^ lxr_clock(x);
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    x->rnd.state = s;
    return s * 0x2545F4914F6CDD1Dull;
}
static uint32_t below(Lxr *x, uint32_t n) { return n ? (uint32_t)((next(x) >> 33) % n) : 0; }
static float unit(Lxr *x) { return (float)(next(x) >> 40) / (float)(1u << 24); }   /* 0..1 */

static int in(uint16_t p, uint16_t first, int n) { return p >= first && p < first + n; }

/* the parameters the voice randomizer leaves alone */
static int skip(uint16_t p)
{
    return p == PAR_NONE || p == PAR_NRPN_DATA_ENTRY_COARSE || p == PAR_NRPN_FINE || p == PAR_NRPN_COARSE ||
           p == PAR_RESERVED4 || p == PAR_VOICE_DECIMATION_ALL ||
           in(p, PAR_VEL_DEST_1, 6) || in(p, PAR_VOICE_LFO1, 6) || in(p, PAR_TARGET_LFO1, 6) ||
           in(p, PAR_AUDIO_OUT1, 6) || in(p, PAR_VOLUME_MOD_ON_OFF1, 6) || p >= END_OF_SOUND_PARAMETERS;
}

static const uint16_t filterFreq[6] = {PAR_FILTER_FREQ_1, PAR_FILTER_FREQ_2, PAR_FILTER_FREQ_3, PAR_FILTER_FREQ_4,
                                       PAR_FILTER_FREQ_5, PAR_FILTER_FREQ_6};
static const uint16_t filterType[6] = {PAR_FILTER_TYPE_1, PAR_FILTER_TYPE_2, PAR_FILTER_TYPE_3, PAR_FILTER_TYPE_4,
                                       PAR_FILTER_TYPE_5, PAR_FILTER_TYPE_6};

/* the range a parameter may land in, so the voice keeps sounding */
static void limits(Lxr *x, uint16_t p, int mx, int *lo, int *hi)
{
    const int floor30 = (mx * 3 + 9) / 10, ceil70 = (mx * 7) / 10;
    int v;
    *lo = (parameter_dtypes[p] & 0x0F) == DTYPE_1B16 ? 1 : 0;
    *hi = mx;
    if (in(p, PAR_VOL1, 6) ||
        p == PAR_VELOD1 || p == PAR_VELOD2 || p == PAR_VELOD3 || p == PAR_VELOD4 || p == PAR_VELOD5 ||
        p == PAR_VELOD6_CLOSED || p == PAR_VELOD6_OPEN ||
        p == PAR_COARSE1 || p == PAR_COARSE2 || p == PAR_COARSE3 || p == PAR_COARSE4 || p == PAR_COARSE5 ||
        p == PAR_COARSE6)
        *lo = floor30;
    if (p == PAR_VELOA1 || p == PAR_VELOA2 || p == PAR_VELOA3 || p == PAR_VELOA4 || p == PAR_VELOA5 || p == PAR_VELOA6)
        *hi = ceil70;
    for (v = 0; v < 6; v++)
        if (p == filterFreq[v]) {
            if (x->kit.b[filterType[v]] == 1) *hi = ceil70;   /* HP: a cutoff at the top cuts everything */
            else *lo = floor30;                              /* LP, BP...: a cutoff at the bottom does */
        }
    if (*lo > *hi) *lo = *hi;
}

/* one landing: up or down from where it is by up to pct % of the range, never where it is or where it just was */
static int land(Lxr *x, int cur, int prev, int lo, int hi, int mx, int pct)
{
    const int span = (mx * pct + 50) / 100 > 0 ? (mx * pct + 50) / 100 : 1;
    int tries, v = cur;
    if (hi <= lo) return lo;
    for (tries = 0; tries < 12; tries++) {
        int d = (int)below(x, (uint32_t)span) + 1;   /* 1..span */
        if (below(x, 2)) d = -d;
        v = cur + d;
        while (v < lo || v > hi) v = v < lo ? 2 * lo - v : 2 * hi - v;   /* reflected at the ends: it turns back */
        if (v != cur && v != prev) return v;
    }
    /* a narrow range: the nearest place that is new */
    for (v = lo; v <= hi; v++) if (v != cur && v != prev) return v;
    return cur;
}

/* the parameters of one voice (0..5): its entries in the modulation target list, which lists every sound parameter
 * of each voice (Cc2Text.c) */
static void voice_params(int v, uint8_t *mine)
{
    uint8_t a, b, i;
    meta_modTargetRange((uint8_t)v, &a, &b);
    for (i = a; i <= b; i++) {
        const uint16_t p = meta_modTargetParam(i);
        if (p < LXR_NUM_SOUND_PARAMS) mine[p] = 1;
    }
}

void lxr_randomizeVoices(Lxr *x, int pct, int voice)
{
    uint16_t p;
    int e;
    uint8_t mine[LXR_NUM_SOUND_PARAMS];
    if (pct <= 0) return;
    if (pct > 100) pct = 100;
    memset(mine, voice < 0, sizeof mine);
    if (voice >= 0) voice_params(voice > 5 ? 5 : voice, mine);
    /* every parameter on its own draw (the filter types come after the cutoffs: those are checked again below) */
    for (p = 1; p < LXR_NUM_SOUND_PARAMS; p++) {
        int mx, lo, hi, v;
        if (skip(p) || !mine[p]) continue;
        mx = meta_soundMax(p);
        limits(x, p, mx, &lo, &hi);
        v = land(x, x->kit.b[p], x->rnd.prev[p], lo, hi, mx, pct);
        x->rnd.prev[p] = x->kit.b[p];
        lxr_setKitParam(x, p, (uint8_t)v, 0);
    }
    for (p = 0; p < 6; p++) {   /* the cutoffs once more, now that every filter type is settled */
        int lo, hi;
        const uint16_t f = filterFreq[p];
        if (!mine[f]) continue;
        limits(x, f, meta_soundMax(f), &lo, &hi);
        if (x->kit.b[f] < lo || x->kit.b[f] > hi) {   /* its filter type changed: a new place on the safe side */
            const int edge = x->kit.b[f] < lo ? lo : hi;
            lxr_setKitParam(x, f, (uint8_t)land(x, edge, x->rnd.prev[f], lo, hi, meta_soundMax(f), pct), 0);
        }
    }
    for (e = EXT_PWM1; e <= EXT_PWM6; e++) {   /* the LXR-02's pulse widths */
        const int i = LXR_NUM_SOUND_PARAMS + e;
        if (voice >= 0 && e != (voice > 5 ? 5 : voice)) continue;
        const int v = land(x, x->kit.b[i], x->rnd.prev[i], 1, 126, 127, pct);
        x->rnd.prev[i] = x->kit.b[i];
        lxr_setExt(x, (uint8_t)e, (uint8_t)v);
    }
    x->displayRev++;
}

/* ---------------------------------------------------------------- the beat */
void seq_randomize(Lxr *x, int pct, int track)
{
    uint8_t t;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    for (t = 0; t < NUM_TRACKS; t++) {
        const uint8_t len = x->pat.len[t] ? x->pat.len[t] : 16;
        if (track >= 0 && t != track) continue;
        uint64_t old = 0, now = 0;
        int tries, i;
        for (i = 0; i < len; i++) if (x->pat.s[t][i].on) old ^= (uint64_t)1 << i;
        for (tries = 0; tries < 8; tries++) {
            /* this voice's share of the knob's density: half to all of it */
            const float share = pct / 100.f * (0.5f + 0.5f * unit(x));
            int k = (int)(share * len + 0.5f);
            const int rot = (int)below(x, len);
            if (pct > 0 && k == 0) k = 1;
            if (k > len) k = len;
            now = 0;
            for (i = 0; i < len; i++) {
                const int src = (i + len - rot) % len;
                if (k && (src * k) % len < k) now |= (uint64_t)1 << i;
            }
            if (now != old || k == 0 || k == len) break;   /* a new beat every press */
        }
        for (i = 0; i < NUM_STEPS; i++) {
            Step *st = &x->pat.s[t][i];
            if (i < len && (now >> i & 1)) {
                st->on = 1;
                st->vel = (uint8_t)(80 + below(x, 48));   /* accents */
                st->note = 63;
                st->prob = 100;
            } else st->on = 0;
        }
    }
    x->displayRev++;
}
