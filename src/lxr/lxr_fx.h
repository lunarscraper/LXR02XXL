/* LXRXXL -- the LXR-02 FX unit (owner's manual 7.9): one effect at a time on the FX bus, saved with the kit.
 * off, Drive (tube / wavefolder / rodent-style hard clip with feedback), ring modulator, compressor, delay (plain or
 * ping-pong). The 0.37
 * firmware the engine comes from has no FX unit, so this one is built from the manual's description and ranges. */
#pragma once
#include <stdint.h>

#define LXR_FX_DELAY_LEN 32768   /* 0.74 s at 44.1 kHz: the high range goes to 0.7 s */

typedef struct LxrFx {
    uint8_t v[17];               /* the kit's FX bytes, indexed by EXT_FX_TYPE.. (lxr_dsp.h) minus EXT_FX_TYPE */
    /* derived */
    float dw, drive, toneCoef, colour, vol;
    float ringInc; uint8_t ringWave;
    float ratio, atkCoef, relCoef, thrDb, makeup;
    float delaySamples, fbk;
    /* state */
    float toneZ, dcX, dcY, clipFb[64]; int clipPos; float clipLp;
    float ringPhase; uint32_t ringRnd; float ringNoise;
    float env;
    float delay[LXR_FX_DELAY_LEN], delay2[LXR_FX_DELAY_LEN]; int dpos; float dlp, dlp2, dTimeSmooth;
} LxrFx;

void lxr_fx_init(LxrFx *fx);
void lxr_fx_set(LxrFx *fx, uint8_t ext, uint8_t value);       /* ext = EXT_FX_TYPE..EXT_FX_DELAYTYPE */
/* in: the FX bus (mono); outL/outR: what goes to the FX output, dry/wet mixed (the ping-pong delay is stereo, the
 * rest the same on both). Returns that output (OUT_ST1..). */
uint8_t lxr_fx_process(LxrFx *fx, const float *in, float *outL, float *outR, int n);
void lxr_fx_reset(LxrFx *fx);                                 /* silence the tails (a delay on Panic) */
