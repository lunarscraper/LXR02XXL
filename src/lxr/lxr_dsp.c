/* LXRXXL -- the LXR-02 sound engine glue: parameters, triggers, the mixer with the FX bus.
 * Compiled with -include lxr_state.h like the vendored engine: its globals belong to the per-instance state. */
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "lxr_dsp.h"
#include "lxr_fx.h"

#include "config.h"
#include "globals.h"
#include "MidiMessages.h"
#include "MidiParser.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "mixer.h"
#include "modulationNode.h"
#include "automationNode.h"
#include "ParameterArray.h"
#include "valueShaper.h"
#include "BufferTools.h"
#include "squareRootLut.h"
#include "random.h"
#include "menu.h"
#include "CcNr2Text.h"
#include "sequencer.h"

/* ---- globals the firmware's other modules define (menu.c, MidiParser.c, mixer.c, adcPots.c, timebase.c) ---- */
uint8_t parameter_values[NUM_PARAMS];
uint8_t parameters2[END_OF_SOUND_PARAMETERS];
uint8_t midi_MidiChannels[8];
uint8_t midi_NoteOverride[7];
static uint16_t midiParser_activeNrpnNumber = 0;
uint8_t midiParser_originalCcValues[0xff];
uint8_t midiParser_selectedLfoVoice[6];
float slider_vol[6];
uint8_t mixer_audioRouting[6];
float mixer_decimation_rate[7];
static float mixer_decimation_cnt[6];
static int16_t mixer_voice_samples[6];
static float mixer_slider_last_gain[6];
volatile uint32_t systick_ticks;
static float g_bpm = 120.f;
static uint8_t g_mutes;   /* the firmware's mute CCs (CC2_MUTE_n) go here; the sequencer keeps its own */

/* 2^(1/12): used by the handler's detune (MidiParser.c) */
#define SEMITONE_UP 1.0594630943592952645618252949463f
#define SEMITONE_DOWN 0.94387431268169349664191315666753f
#define MIDI_DEFAULT_VOICE_NOTE_BASE 36u

/* what the handler calls that lives outside the DSP on the machine */
static void preset_morph(uint8_t morph) { (void)morph; }      /* CC1 -> morph is handled by the plugin's MIDI input */
void menu_notifyExternalParamChanged(uint16_t p) { (void)p; }
static void seq_setMute(uint8_t track, uint8_t muted) { if (muted) g_mutes |= 1u << track; else g_mutes &= ~(1u << track); }
void midiParser_checkMtc(void) {}
float lxr_dsp_getBpm(void) { return g_bpm; }

/* user samples (LXR02Open's flash sample memory): none in the plugin */
#include "SampleMemory.h"
uint8_t sampleMemory_getNumSamples(void) { return 0; }
uint32_t sampleMemory_getGeneration(void) { return 1; }
SampleInfo sampleMemory_getSampleInfo(uint8_t index) { SampleInfo s; (void)index; memset(&s, 0, sizeof s); return s; }
uint8_t sampleMemory_isLooped(uint8_t index) { (void)index; return 0; }

/* the firmware's parameter handler, verbatim */
#include "cc_handler.inc"

/* ---- automation nodes: two lanes per track, as sequencer.c keeps them ---- */
static AutomationNode lxr_autoNodes[7][2];

void lxr_dsp_init(void)
{
    uint8_t i;
    initRng();
    initDrumVoice();
    Snare_init();
    Cymbal_init();
    HiHat_init();
    for (i = 0; i < 6; i++) {
        mixer_decimation_rate[i] = 1;
        mixer_decimation_cnt[i] = 0;
        mixer_voice_samples[i] = 0;
        mixer_audioRouting[i] = 0;
        slider_vol[i] = 1.f;
        mixer_slider_last_gain[i] = 1.f;
    }
    mixer_decimation_rate[6] = 1;
    parameterArray_init();
    for (i = 0; i < 6; i++)
        modNode_init(&velocityModulators[i]);
    paramToModTargetInit();
    for (i = 0; i < 7; i++) {
        autoNode_init(&lxr_autoNodes[i][0]);
        autoNode_init(&lxr_autoNodes[i][1]);
        midi_MidiChannels[i] = 0;
        midi_NoteOverride[i] = 0;
    }
    midi_MidiChannels[7] = 0;
}

void lxr_dsp_setTempo(float bpm)
{
    if (bpm < 1.f) bpm = 1.f;
    if (bpm != g_bpm) {
        g_bpm = bpm;
        lfo_recalcSync();
    }
}

static void send_cc(uint16_t par, uint8_t value)
{
    MidiMsg msg;
    if (par < 128u) {
        msg.status = MIDI_CC;
        msg.data1 = (uint8_t)((par + 1u) & 0x7fu);   /* frontPanel_sendData(MIDI_CC) adds one */
    } else {
        msg.status = MIDI_CC2;
        msg.data1 = (uint8_t)(par - 128u);
    }
    msg.data2 = value;
    midiParser_ccHandler(msg, 1);
}

static void set_lfo_target(uint8_t lfo, uint8_t modTargetIdx)
{
    const uint16_t target = modTargets[modTargetIdx].param;
    switch (lfo) {
    case 0: case 1: case 2: modNode_setDestination(&voiceArray[lfo].lfo.modTarget, target); break;
    case 3: modNode_setDestination(&snareVoice.lfo.modTarget, target); break;
    case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, target); break;
    case 5: modNode_setDestination(&hatVoice.lfo.modTarget, target); break;
    default: break;
    }
}

/* menu_sendSoundParameter(), menu.c: mod targets go to the modulation nodes, the rest through the handler */
void lxr_dsp_setParam(uint16_t par, uint8_t value)
{
    if (par >= END_OF_SOUND_PARAMETERS) return;
    if (par == 127u) return;   /* PAR_RESERVED4: CC 128 does not exist (presetManager.c skips it too) */
    if (par >= PAR_VEL_DEST_1 && par <= PAR_VEL_DEST_6) {
        if (value >= getNumModTargets()) value = 0;
        modNode_setDestination(&velocityModulators[par - PAR_VEL_DEST_1], modTargets[value].param);
        return;
    }
    if (par >= PAR_TARGET_LFO1 && par <= PAR_TARGET_LFO6) {
        if (value >= getNumModTargets()) value = 0;
        set_lfo_target((uint8_t)(par - PAR_TARGET_LFO1), value);
        return;
    }
    if (par >= PAR_VOICE_LFO1 && par <= PAR_VOICE_LFO6)
        return;   /* only selects which voice's list the target comes from (ui.c) */
    send_cc(par, value);
}

void lxr_dsp_setExt(uint8_t ext, uint8_t value)
{
    if (ext <= EXT_PWM6) {
        /* the duty cycle: 1%..99% over 0..127, 50% at 64 */
        const float duty = 0.01f + 0.98f * (value / 127.f);
        const uint32_t off = (uint32_t)(duty * 4294967295.0);
        switch (ext) {
        case EXT_PWM1: case EXT_PWM2: case EXT_PWM3:
            voiceArray[ext].osc.pwmOffset = off;
            voiceArray[ext].modOsc.pwmOffset = off;
            break;
        case EXT_PWM4: snareVoice.osc.pwmOffset = off; break;
        case EXT_PWM5:
            cymbalVoice.osc.pwmOffset = off; cymbalVoice.modOsc.pwmOffset = off; cymbalVoice.modOsc2.pwmOffset = off;
            break;
        case EXT_PWM6:
            hatVoice.osc.pwmOffset = off; hatVoice.modOsc.pwmOffset = off; hatVoice.modOsc2.pwmOffset = off;
            break;
        }
    }
}

/* presetManager.c: preset_interpolate / preset_morphShouldSkip / preset_getMorphValue */
static uint8_t interpolate(uint8_t a, uint8_t b, uint8_t x)
{
    uint16_t fixedPointValue = (uint16_t)(((a * 256) + (b - a) * x));
    uint8_t result = (uint8_t)(fixedPointValue / 256);
    return (uint8_t)((fixedPointValue & 0xff) < 0x7f ? result : result + 1);
}

static uint8_t morph_skips(uint16_t index)
{
    if (index == 127u) return 1;
    if (index >= PAR_VEL_DEST_1 && index <= PAR_VEL_DEST_6) return 1;
    if (index >= PAR_VOICE_LFO1 && index <= PAR_VOICE_LFO6) return 1;
    if (index >= PAR_TARGET_LFO1 && index <= PAR_TARGET_LFO6) return 1;
    return 0;
}

uint8_t lxr_dsp_morphValue(const uint8_t *kit, const uint8_t *morphKit, uint16_t par, uint8_t morph)
{
    if (!morphKit || morph == 0 || morph_skips(par)) return kit[par];
    /* option values (waveforms, filter types, routing...) switch halfway instead of passing through other options */
    {
        const uint8_t dt = (uint8_t)(parameter_dtypes[par] & 0x0F);
        if (dt == DTYPE_MENU || dt == DTYPE_ON_OFF || dt == DTYPE_MIX_FM || dt == DTYPE_NOTE_NAME)
            return morph < 128 ? kit[par] : morphKit[par];
    }
    return interpolate(kit[par], morphKit[par], morph);
}

void lxr_dsp_applyKit(const uint8_t *kit, const uint8_t *morphKit, uint8_t morph)
{
    uint16_t i;
    uint8_t v;
    for (i = 0; i < END_OF_SOUND_PARAMETERS; i++) {
        v = lxr_dsp_morphValue(kit, morphKit, i, morph);
        parameter_values[i] = v;
        if (i >= PAR_VEL_DEST_1 && i <= PAR_VEL_DEST_6) continue;
        if (i >= PAR_TARGET_LFO1 && i <= PAR_TARGET_LFO6) continue;
        lxr_dsp_setParam(i, v);
    }
    /* preset_sendDrumsetParameters(): mod targets last, once the values they modulate are in place */
    for (i = 0; i < 6; i++) {
        lxr_dsp_setParam((uint16_t)(PAR_VEL_DEST_1 + i), kit[PAR_VEL_DEST_1 + i]);
        lxr_dsp_setParam((uint16_t)(PAR_TARGET_LFO1 + i), kit[PAR_TARGET_LFO1 + i]);
    }
    for (i = 0; i <= EXT_PWM6; i++)
        lxr_dsp_setExt((uint8_t)i, kit[LXR_NUM_SOUND_PARAMS + i]);
}

uint8_t lxr_dsp_voiceOfTrack(uint8_t track) { return track > 5 ? 5 : track; }

/* voiceControl_triggerNow(), MidiVoiceControl.c */
void lxr_dsp_trigger(uint8_t track, uint8_t vel, uint8_t note)
{
    if (track < 3) Drum_trigger(track, vel, note);
    else if (track < 4) Snare_trigger(vel, note);
    else if (track < 5) Cymbal_trigger(vel, note);
    else if (track < 7) HiHat_trigger(vel, (uint8_t)(track - 5), note);
}

void lxr_dsp_automation(uint8_t track, uint8_t lane, uint8_t param1, uint8_t value)
{
    uint16_t dest = NO_AUTOMATION;
    if (track > 6 || lane > 1) return;
    if (param1) {
        const uint16_t par = (uint16_t)(param1 - 1u);
        /* the automation node's destination is the firmware's step encoding: CC number (param + 1) below 128,
         * the parameter number from 128 (frontParser_applyStepParamsToMenu) */
        dest = par < 128u ? (uint16_t)(par + 1u) : par;
    }
    autoNode_setDestination(&lxr_autoNodes[track][lane], dest);
    autoNode_updateValue(&lxr_autoNodes[track][lane], value);
}

void lxr_dsp_automationReset(void)
{
    uint8_t i;
    for (i = 0; i < 7; i++) {
        autoNode_setDestination(&lxr_autoNodes[i][0], NO_AUTOMATION);
        autoNode_setDestination(&lxr_autoNodes[i][1], NO_AUTOMATION);
    }
}

void lxr_dsp_globalDecimation(uint8_t value) { send_cc(PAR_VOICE_DECIMATION_ALL, value); }

/* ---- the mixer (mixer.c), with the LXR-02's FX bus ---- */
static void decimate(const uint8_t voiceNr, int16_t *buffer)
{
    uint8_t i;
    for (i = 0; i < OUTPUT_DMA_SIZE; i++) {
        mixer_decimation_cnt[voiceNr] += mixer_decimation_rate[voiceNr] * mixer_decimation_rate[6];
        if (mixer_decimation_cnt[voiceNr] >= 1.f) {
            mixer_decimation_cnt[voiceNr] -= 1.f;
            mixer_voice_samples[voiceNr] = buffer[i];
        }
        buffer[i] = mixer_voice_samples[voiceNr];
    }
}

/* The plugin has one stereo output, MPC's track out: it is the LXR-02 with only MAIN OUT connected. The firmware then
 * sends what is routed to the unconnected OUT 2 to the main out (mixer_checkOutJackAvailable(): St2 -> St1, L2 -> L1,
 * R2 -> R1), and a mono routing stays on its side. */
static void route_add(uint8_t dest, float panL, float panR, const float *data, float *L, float *R)
{
    uint8_t i;
    switch (dest) {
    case OUT_ST1: case OUT_ST2:
        for (i = 0; i < OUTPUT_DMA_SIZE; i++) { L[i] += data[i] * panL; R[i] += data[i] * panR; }
        break;
    case OUT_L1: case OUT_L2:
        for (i = 0; i < OUTPUT_DMA_SIZE; i++) L[i] += data[i];
        break;
    case OUT_R1: case OUT_R2:
        for (i = 0; i < OUTPUT_DMA_SIZE; i++) R[i] += data[i];
        break;
    default: break;
    }
}

static void voice_out(uint8_t v, uint8_t pan, int16_t *s16, const float fader[6], float *L, float *R, float *fxBus)
{
    /* mixer_addVoiceInt16ToOutput(): the fader gain ramps over the block, the int16 product is truncated, then panned */
    float data[OUTPUT_DMA_SIZE];
    const float gain = fader[v], last = mixer_slider_last_gain[v];
    const float inv = 1.f / (OUTPUT_DMA_SIZE - 1.f), dg = gain - last;
    uint8_t i;
    decimate(v, s16);
    for (i = 0; i < OUTPUT_DMA_SIZE; i++)
        data[i] = (float)(int16_t)((float)s16[i] * (last + (float)i * inv * dg)) * (1.f / 32768.f);
    mixer_slider_last_gain[v] = gain;
    if (mixer_audioRouting[v] == OUT_FX) {
        for (i = 0; i < OUTPUT_DMA_SIZE; i++) fxBus[i] += data[i];
        return;
    }
    route_add(mixer_audioRouting[v], squareRootLut[127 - pan], squareRootLut[pan], data, L, R);
}

void lxr_dsp_render32(float *outL, float *outR, const float fader[6], struct LxrFx *fx)
{
    int16_t s16[OUTPUT_DMA_SIZE];
    float fxBus[OUTPUT_DMA_SIZE];
    uint8_t i;

    modNode_resetTargets();
    modNode_reassignVeloMod();
    lfo_dispatchNextValue(&voiceArray[0].lfo);
    lfo_dispatchNextValue(&voiceArray[1].lfo);
    lfo_dispatchNextValue(&voiceArray[2].lfo);
    lfo_dispatchNextValue(&snareVoice.lfo);
    lfo_dispatchNextValue(&cymbalVoice.lfo);
    lfo_dispatchNextValue(&hatVoice.lfo);
    SVF_recalcFreq(&voiceArray[0].filter);
    SVF_recalcFreq(&voiceArray[1].filter);
    SVF_recalcFreq(&voiceArray[2].filter);
    SVF_recalcFreq(&snareVoice.filter);
    SVF_recalcFreq(&cymbalVoice.filter);
    SVF_recalcFreq(&hatVoice.filter);
    calcDrumVoiceAsync(0);
    calcDrumVoiceAsync(1);
    calcDrumVoiceAsync(2);
    Snare_calcAsync();
    Cymbal_calcAsync();
    HiHat_calcAsync();

    for (i = 0; i < OUTPUT_DMA_SIZE; i++) { outL[i] = 0; outR[i] = 0; fxBus[i] = 0; }
    for (i = 0; i < 3; i++) {
        calcDrumVoiceSyncBlock(i, s16, OUTPUT_DMA_SIZE);
        voice_out(i, voiceArray[i].pan, s16, fader, outL, outR, fxBus);
    }
    Snare_calcSyncBlock(s16, OUTPUT_DMA_SIZE);
    voice_out(3, snareVoice.pan, s16, fader, outL, outR, fxBus);
    Cymbal_calcSyncBlock(s16, OUTPUT_DMA_SIZE);
    voice_out(4, cymbalVoice.pan, s16, fader, outL, outR, fxBus);
    HiHat_calcSyncBlock(s16, OUTPUT_DMA_SIZE);
    voice_out(5, hatVoice.pan, s16, fader, outL, outR, fxBus);

    if (fx) {
        float wl[OUTPUT_DMA_SIZE], wr[OUTPUT_DMA_SIZE];
        uint8_t dest = lxr_fx_process(fx, fxBus, wl, wr, OUTPUT_DMA_SIZE);
        if (dest == OUT_ST1 || dest == OUT_ST2 || dest >= OUT_FX) {
            /* a stereo out: centre pan (the pan law's -3 dB), the ping-pong's two sides kept apart */
            const float c = squareRootLut[64];
            for (i = 0; i < OUTPUT_DMA_SIZE; i++) { outL[i] += wl[i] * c; outR[i] += wr[i] * c; }
        } else {
            for (i = 0; i < OUTPUT_DMA_SIZE; i++) wl[i] = 0.5f * (wl[i] + wr[i]);
            route_add(dest, 1.f, 1.f, wl, outL, outR);
        }
    }
}
