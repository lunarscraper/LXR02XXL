/* LXRXXL -- the LXR-02 sound engine (Sonic Potions / LXR02Open DSP) driven as a plugin.
 *
 * The voices, filters, envelopes, LFOs, modulation, transient generator, decimator and distortion are the firmware's own
 * code (third_party/lxr02open), parameters go in through the firmware's own handler (cc_handler.inc) as on the machine,
 * and the mixer below is the firmware's mixer with the LXR-02's FX bus added. Everything here works on the engine's
 * global state, so it must only run with this instance's state swapped in (lxr_state.c). */
#pragma once
#include <stdint.h>

#define LXR_NUM_SOUND_PARAMS 228   /* END_OF_SOUND_PARAMETERS in the firmware: the bytes of a kit */

/* The LXR-02 parameters the 0.37 firmware has no slot for (its manual's NRPN 72-77 and 106-117): pulse width per voice
 * and the FX unit. Saved with the kit after the firmware's bytes. */
enum {
    EXT_PWM1 = 0, EXT_PWM2, EXT_PWM3, EXT_PWM4, EXT_PWM5, EXT_PWM6,
    EXT_FX_TYPE,      /* 0 off, 1 drv, 2 rng, 3 cmp, 4 del */
    EXT_FX_OUT,       /* 0 St1 .. 5 R2 (the output the FX signal is mixed to) */
    EXT_FX_DW,        /* dry/wet */
    EXT_FX_DRVTYPE,   /* drive: 0 Tub, 1 Fld, 2 Clp */
    EXT_FX_P1, EXT_FX_P2, EXT_FX_P3, EXT_FX_P4,
    EXT_FX_RATIO,     /* compressor ratio: 1:1 2:1 3:1 4:1 6:1 8:1 */
    EXT_FX_RINGWAVE,  /* ring modulator waveform */
    EXT_FX_DELAYTYPE, /* 0 Del, 1 Pp (ping-pong) */
    LXR_NUM_EXT
};
#define LXR_KIT_BYTES (LXR_NUM_SOUND_PARAMS + LXR_NUM_EXT)

/* the audio output values of a voice's "out" (and the FX's) */
enum { OUT_ST1 = 0, OUT_ST2, OUT_L1, OUT_R1, OUT_L2, OUT_R2, OUT_FX, OUT_NUM };

struct LxrFx;

void lxr_dsp_init(void);                                   /* power-up: every voice, the parameter table */
void lxr_dsp_setTempo(float bpm);                          /* for synced LFOs */
/* one sound parameter (0..227) as the menu sends it: mod targets, the LFO voice/target pair, morph-free */
void lxr_dsp_setParam(uint16_t par, uint8_t value);
void lxr_dsp_setExt(uint8_t ext, uint8_t value);           /* pulse width; FX values go to the FX unit by the caller */
/* the whole kit (sound bytes + ext), blended with the morph kit by `morph` (0..255) as the firmware's morph does */
void lxr_dsp_applyKit(const uint8_t *kit, const uint8_t *morphKit, uint8_t morph);
uint8_t lxr_dsp_morphValue(const uint8_t *kit, const uint8_t *morphKit, uint16_t par, uint8_t morph);
/* trigger track 0..6 (6 = open hi-hat) with velocity 0..127 and note (63 = no transpose) */
void lxr_dsp_trigger(uint8_t track, uint8_t vel, uint8_t note);
/* step automation (parameter locks), per track: two lanes, the firmware's automation nodes */
void lxr_dsp_automation(uint8_t track, uint8_t lane, uint8_t param1, uint8_t value);   /* param1 = parameter + 1, 0 = none */
void lxr_dsp_automationReset(void);
void lxr_dsp_globalDecimation(uint8_t value);              /* performance page "sr" */
/* 32 frames (the firmware's block) of the main stereo out, float -1..1; faders 0..1 per voice (6) */
void lxr_dsp_render32(float *outL, float *outR, const float fader[6], struct LxrFx *fx);
uint8_t lxr_dsp_voiceOfTrack(uint8_t track);
