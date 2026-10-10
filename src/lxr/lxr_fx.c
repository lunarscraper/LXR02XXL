/* LXRXXL -- the LXR-02 FX unit. See lxr_fx.h. Ranges follow the owner's manual where it gives them:
 * compressor ratio 1:1..8:1, attack 0.3..100 ms, release ("dec") 25..200 ms, threshold -96..0 dB;
 * delay time 1..20 ms (low range) or 20 ms..0.7 s (high range), a low-pass ("ton") in the feedback path. */
#include <math.h>
#include <string.h>
#include "lxr_fx.h"
#include "lxr_dsp.h"

#define SR 44100.f
enum { FX_OFF = 0, FX_DRIVE, FX_RING, FX_COMP, FX_DELAY };   /* the LXR-02 1.70 list: off drv rng cmp del */
static const float ratios[6] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f};   /* 1:1 2:1 3:1 4:1 6:1 8:1 */
enum { DRV_TUBE = 0, DRV_FOLD, DRV_CLIP };

static float expmap(uint8_t v, float lo, float hi) { return lo * powf(hi / lo, v / 127.f); }
static float onepole(float hz) { float c = 1.f - expf(-2.f * 3.14159265f * hz / SR); return c > 1.f ? 1.f : c; }
#define V(k) fx->v[(k) - EXT_FX_TYPE]

static void derive(LxrFx *fx)
{
    fx->dw = V(EXT_FX_DW) / 127.f;
    switch (V(EXT_FX_TYPE)) {
    case FX_DRIVE:
        fx->drive = 1.f + powf(V(EXT_FX_P1) / 127.f, 2.f) * 63.f;
        fx->toneCoef = onepole(expmap(V(EXT_FX_P2), 400.f, 20000.f));
        fx->colour = V(EXT_FX_P3) / 127.f;
        fx->vol = V(EXT_FX_P4) / 127.f * 1.5f;
        break;
    case FX_RING:
        fx->ringInc = expmap(V(EXT_FX_P1), 8.f, 8000.f) / SR;
        fx->ringWave = V(EXT_FX_RINGWAVE);
        break;
    case FX_COMP:
        fx->ratio = ratios[V(EXT_FX_RATIO) > 5 ? 5 : V(EXT_FX_RATIO)];
        fx->atkCoef = 1.f - expf(-1.f / (expmap(V(EXT_FX_P1), 0.3f, 100.f) * 0.001f * SR));
        fx->relCoef = 1.f - expf(-1.f / (expmap(V(EXT_FX_P2), 25.f, 200.f) * 0.001f * SR));
        fx->thrDb = -96.f + 96.f * V(EXT_FX_P3) / 127.f;
        fx->makeup = powf(10.f, (24.f * V(EXT_FX_P4) / 127.f) / 20.f);
        break;
    case FX_OFF:
        break;
    case FX_DELAY: default:
        fx->delaySamples = (V(EXT_FX_P2) >= 64 ? expmap(V(EXT_FX_P1), 20.f, 700.f) : expmap(V(EXT_FX_P1), 1.f, 20.f))
                           * 0.001f * SR;
        fx->toneCoef = onepole(expmap(V(EXT_FX_P3), 300.f, 20000.f));
        fx->fbk = V(EXT_FX_P4) / 127.f * 0.98f;
        break;
    }
}

void lxr_fx_init(LxrFx *fx)
{
    memset(fx, 0, sizeof *fx);
    V(EXT_FX_TYPE) = FX_DELAY;
    V(EXT_FX_DW) = 64;
    V(EXT_FX_P1) = 90; V(EXT_FX_P2) = 127; V(EXT_FX_P3) = 90; V(EXT_FX_P4) = 60;
    V(EXT_FX_RATIO) = 2;
    fx->ringRnd = 0x12345678u;
    derive(fx);
    fx->dTimeSmooth = fx->delaySamples;
}

void lxr_fx_reset(LxrFx *fx)
{
    memset(fx->delay, 0, sizeof fx->delay);
    memset(fx->delay2, 0, sizeof fx->delay2);
    fx->dlp2 = 0;
    memset(fx->clipFb, 0, sizeof fx->clipFb);
    fx->toneZ = fx->dcX = fx->dcY = fx->clipLp = fx->env = fx->dlp = 0;
}

void lxr_fx_set(LxrFx *fx, uint8_t ext, uint8_t value)
{
    if (ext < EXT_FX_TYPE || ext > EXT_FX_DELAYTYPE) return;
    if (ext == EXT_FX_TYPE && value > FX_DELAY) value = FX_DELAY;
    if (ext == EXT_FX_DRVTYPE && value > DRV_CLIP) value = DRV_CLIP;
    if (ext == EXT_FX_OUT && value > OUT_R2) value = OUT_R2;
    if (ext == EXT_FX_RINGWAVE && value > 4) value = 4;
    if (ext == EXT_FX_RATIO && value > 5) value = 5;
    if (ext == EXT_FX_DELAYTYPE && value > 1) value = 1;
    if (ext == EXT_FX_TYPE && value != V(EXT_FX_TYPE)) lxr_fx_reset(fx);
    fx->v[ext - EXT_FX_TYPE] = value;
    derive(fx);
}

static float fold(float x)
{
    /* triangle wavefolder: reflects everything past +-1 back into range */
    x = (x + 1.f) * 0.25f;
    x -= floorf(x);
    return 4.f * fabsf(x - 0.5f) - 1.f;
}

static float ring_osc(LxrFx *fx)
{
    float p = fx->ringPhase, o;
    switch (fx->ringWave) {
    case 0: o = sinf(2.f * 3.14159265f * p); break;
    case 1: o = 1.f - 4.f * fabsf(p - 0.5f); break;
    case 2: o = 2.f * p - 1.f; break;
    case 3: o = p < 0.5f ? 1.f : -1.f; break;
    default: o = fx->ringNoise; break;
    }
    p += fx->ringInc;
    if (p >= 1.f) {
        p -= 1.f;
        fx->ringRnd = fx->ringRnd * 1664525u + 1013904223u;   /* noise is sample-and-hold at the frequency */
        fx->ringNoise = (int32_t)fx->ringRnd * (1.f / 2147483648.f);
    }
    fx->ringPhase = p;
    return o;
}

uint8_t lxr_fx_process(LxrFx *fx, const float *in, float *outL, float *outR, int n)
{
    int i;
    const uint8_t type = V(EXT_FX_TYPE);
    for (i = 0; i < n; i++) {
        const float x = in[i];
        float y, y2 = 0;
        int stereo = 0;
        switch (type) {
        case FX_DRIVE: {
            const uint8_t dt = V(EXT_FX_DRVTYPE);
            float g = x * fx->drive;
            if (dt == DRV_TUBE) {
                const float bias = (fx->colour - 0.5f) * 1.2f;   /* asymmetry: even harmonics */
                y = tanhf(g + bias) - tanhf(bias);
            } else if (dt == DRV_FOLD) {
                y = fold(g + (fx->colour - 0.5f) * 2.f);
            } else {
                /* hard clip with a feedback loop through a short delay: with full feedback it rings on its own, like
                 * a microphone in front of a speaker (the manual's warning) */
                const float fb = fx->clipFb[fx->clipPos];
                g += fx->colour * 1.6f * fb;
                y = g > 1.f ? 1.f : g < -1.f ? -1.f : g;
                fx->clipLp += 0.35f * (y - fx->clipLp);
                fx->clipFb[fx->clipPos] = -fx->clipLp;
                fx->clipPos = (fx->clipPos + 1) & 63;
            }
            fx->toneZ += fx->toneCoef * (y - fx->toneZ);
            y = fx->toneZ;
            /* DC blocker (the bias and fold offset) */
            {
                const float d = y - fx->dcX + 0.995f * fx->dcY;
                fx->dcX = y; fx->dcY = d; y = d;
            }
            y *= fx->vol;
            break;
        }
        case FX_OFF:
            y = x;
            break;
        case FX_RING:
            y = x * ring_osc(fx);
            break;
        case FX_COMP: {
            const float a = fabsf(x);
            fx->env += (a > fx->env ? fx->atkCoef : fx->relCoef) * (a - fx->env);
            {
                const float lvl = 20.f * log10f(fx->env + 1e-9f);
                float gdb = 0.f;
                if (lvl > fx->thrDb) gdb = (fx->thrDb + (lvl - fx->thrDb) / fx->ratio) - lvl;
                y = x * powf(10.f, gdb / 20.f) * fx->makeup;
            }
            break;
        }
        case FX_DELAY: default: {
            float rp, frac, d, d2;
            int i0, i1;
            fx->dTimeSmooth += 0.0005f * (fx->delaySamples - fx->dTimeSmooth);   /* time changes glide, no clicks */
            rp = (float)fx->dpos - fx->dTimeSmooth;
            while (rp < 0) rp += LXR_FX_DELAY_LEN;
            i0 = (int)rp; frac = rp - i0; i1 = (i0 + 1) & (LXR_FX_DELAY_LEN - 1);
            d = fx->delay[i0] + frac * (fx->delay[i1] - fx->delay[i0]);
            fx->dlp += fx->toneCoef * (d - fx->dlp);
            if (V(EXT_FX_DELAYTYPE) == 1) {
                /* ping-pong: the input starts on the left line, each repeat crosses to the other side */
                d2 = fx->delay2[i0] + frac * (fx->delay2[i1] - fx->delay2[i0]);
                fx->dlp2 += fx->toneCoef * (d2 - fx->dlp2);
                fx->delay[fx->dpos] = x + fx->dlp2 * fx->fbk;
                fx->delay2[fx->dpos] = fx->dlp * fx->fbk;
                y = d; y2 = d2;
            } else {
                fx->delay[fx->dpos] = x + fx->dlp * fx->fbk;
                y = d; y2 = d;
            }
            fx->dpos = (fx->dpos + 1) & (LXR_FX_DELAY_LEN - 1);
            break;
        }
        }
        stereo = type == FX_DELAY;
        if (!stereo) y2 = y;
        if (!(y == y) || y > 8.f || y < -8.f) y = 0;   /* never let a NaN or a runaway reach the mix */
        if (!(y2 == y2) || y2 > 8.f || y2 < -8.f) y2 = 0;
        outL[i] = x * (1.f - fx->dw) + y * fx->dw;
        outR[i] = x * (1.f - fx->dw) + y2 * fx->dw;
    }
    return V(EXT_FX_OUT);
}
