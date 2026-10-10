/* LXRXXL -- parameter names and value text, as the LXR shows them (tables from the LXR02Open menu). */
#include <stdio.h>
#include <string.h>
#include "lxr.h"
#include "menu.h"
#include "MenuText.h"
#include "CcNr2Text.h"

#include "lxr/menu_tables.inc"

/* the LXR-02's waveform list (firmware 1.70): PWM where the 0.37 firmware had its crash sample (Oscillator.h) */
static const char *const waveNames[] = {"Sin", "Tri", "Saw", "Rec", "Noi", "PWM"};
#define NUM_WAVES 6
/* audio outs: the LXR-02 adds the FX bus */
static const char *const outNames[] = {"St1", "St2", "L1", "R1", "L2", "R2", "FX"};
#define NUM_OUTS 7

const char *meta_textShort(uint8_t t) { return t < NUM_NAMES ? shortNames[valueNames[t].shortName] : ""; }
const char *meta_textLong(uint8_t t) { return t < NUM_NAMES ? longNames[valueNames[t].longName] : ""; }
const char *meta_textCat(uint8_t t) { return t < NUM_NAMES ? catNames[valueNames[t].category] : ""; }

const char *meta_targetLong(uint8_t idx)
{
    if (idx >= getNumModTargets()) return "";
    return longNames[valueNames[modTargets[idx].nameIdx].longName];
}

uint8_t meta_numModTargets(void) { return getNumModTargets(); }
uint16_t meta_modTargetParam(uint8_t idx) { return idx < getNumModTargets() ? modTargets[idx].param : PAR_NONE; }

void meta_modTargetRange(uint8_t voice0, uint8_t *start, uint8_t *end)
{
    if (voice0 > 5) voice0 = 5;
    *start = modTargetVoiceOffsets[voice0].start;
    *end = modTargetVoiceOffsets[voice0].end;
}

uint8_t meta_paramToModTarget(uint16_t par)
{
    uint8_t i, n = getNumModTargets();
    for (i = 1; i < n; i++)
        if (modTargets[i].param == par) return i;
    return 0;
}

/* menu_displayModTargetShort(): "off", or the target's short name, with its voice number in front when asked */
void meta_modTargetText(uint8_t idx, char *buf, int withVoice)
{
    if (idx >= getNumModTargets() || modTargets[idx].param == PAR_NONE) { strcpy(buf, "off"); return; }
    if (withVoice) {
        uint8_t v = 0, i;
        for (i = 0; i < 6; i++)
            if (idx >= modTargetVoiceOffsets[i].start && idx <= modTargetVoiceOffsets[i].end) v = (uint8_t)(i + 1);
        if (v) { sprintf(buf, "%d%s", v, shortNames[valueNames[modTargets[idx].nameIdx].shortName]); return; }
    }
    strcpy(buf, shortNames[valueNames[modTargets[idx].nameIdx].shortName]);
}

/* setNoteName(), menu.c */
void meta_noteName(uint8_t num, char *buf)
{
    uint8_t n = num % 12;
    buf[0] = (char)((n > 8 ? 60 : 67) + ((n + (n > 4)) / 2));
    buf[1] = ((n < 5 && (n & 1)) || (n > 5 && !(n & 1))) ? '#' : 0;
    if (buf[1]) { sprintf(buf + 2, "%d", num / 12); } else { sprintf(buf + 1, "%d", num / 12); }
}

static uint8_t menu_entries(uint8_t menuId)
{
    switch (menuId) {
    case MENU_TRANS: return (uint8_t)transientNames[0][0];
    case MENU_AUDIO_OUT: return NUM_OUTS;
    case MENU_FILTER: return (uint8_t)filterTypes[0][0];
    case MENU_SYNC_RATES: return (uint8_t)syncRateNames[0][0];
    case MENU_LFO_WAVES: return (uint8_t)lfoWaveNames[0][0];
    case MENU_RETRIGGER: return (uint8_t)retriggerNames[0][0];
    case MENU_SEQ_QUANT: return (uint8_t)quantisationNames[0][0];
    case MENU_MIDI: return (uint8_t)midiModes[0][0];
    case MENU_NEXT_PATTERN: return (uint8_t)nextPatternNames[0][0];
    case MENU_WAVEFORM: return NUM_WAVES;
    case MENU_ROLL_RATES: return (uint8_t)rollRateNames[0][0];
    case MENU_MIDI_ROUTING: return (uint8_t)midiRoutingNames[0][0];
    case MENU_MIDI_FILTERING: return (uint8_t)midiFilterNames[0][0];
    case MENU_PPQ: return (uint8_t)ppqNames[0][0];
    case MENU_EXT_SYNC: return (uint8_t)extSyncNames[0][0];
    default: return 1;
    }
}

static const char *menu_name(uint8_t menuId, uint8_t v)
{
    switch (menuId) {
    case MENU_FILTER: return filterTypes[v + 1];
    case MENU_WAVEFORM: return waveNames[v < NUM_WAVES ? v : 0];
    case MENU_AUDIO_OUT: return outNames[v < NUM_OUTS ? v : 0];
    case MENU_TRANS: return transientNames[v + 1];
    case MENU_SYNC_RATES: return syncRateNames[v + 1];
    case MENU_LFO_WAVES: return lfoWaveNames[v + 1];
    case MENU_RETRIGGER: return retriggerNames[v + 1];
    case MENU_SEQ_QUANT: return quantisationNames[v + 1];
    case MENU_MIDI: return midiModes[v + 1];
    case MENU_NEXT_PATTERN: return nextPatternNames[v + 1];
    case MENU_ROLL_RATES: return rollRateNames[v + 1];
    case MENU_MIDI_ROUTING: return midiRoutingNames[v + 1];
    case MENU_MIDI_FILTERING: return midiFilterNames[v + 1];
    case MENU_PPQ: return ppqNames[v + 1];
    case MENU_EXT_SYNC: return extSyncNames[v + 1];
    default: return "";
    }
}

/* the largest value of a sound parameter: menu_encoderChangeParameter()'s clamps. The mod target selections are
 * ranges of the target list; their slots (ui.c) map them separately. */
uint8_t meta_soundMax(uint16_t par)
{
    if (par >= END_OF_SOUND_PARAMETERS) return 127;
    switch (parameter_dtypes[par] & 0x0F) {
    case DTYPE_0B255: return 255;
    case DTYPE_1B16: return 16;
    case DTYPE_0B15: return 15;
    case DTYPE_MIX_FM: case DTYPE_ON_OFF: case DTYPE_0b1: return 1;
    case DTYPE_VOICE_LFO: return 6;
    case DTYPE_MENU: return (uint8_t)(menu_entries((uint8_t)(parameter_dtypes[par] >> 4)) - 1);
    case DTYPE_TARGET_SELECTION_LFO: case DTYPE_TARGET_SELECTION_VELO: case DTYPE_AUTOM_TARGET:
        return (uint8_t)(getNumModTargets() - 1);
    default: return 127;
    }
}

/* the value as the LCD shows it (menu_repaintGeneric) */
void meta_soundText(uint16_t par, uint8_t v, char *buf)
{
    if (par >= END_OF_SOUND_PARAMETERS) { sprintf(buf, "%d", v); return; }
    switch (parameter_dtypes[par] & 0x0F) {
    case DTYPE_TARGET_SELECTION_VELO:
    case DTYPE_TARGET_SELECTION_LFO:
        meta_modTargetText(v, buf, 0);
        break;
    case DTYPE_MIX_FM: strcpy(buf, v == 1 ? "mix" : "fm"); break;
    case DTYPE_ON_OFF: strcpy(buf, v == 1 ? "on" : "off"); break;
    case DTYPE_MENU: {
        const char *s = menu_name((uint8_t)(parameter_dtypes[par] >> 4), v);
        snprintf(buf, 8, "%s", s);
        while (*buf && buf[strlen(buf) - 1] == ' ') buf[strlen(buf) - 1] = 0;
        break;
    }
    case DTYPE_PM63: sprintf(buf, "%d", (int)v - 63); break;
    case DTYPE_NOTE_NAME:
        if (par >= PAR_MIDI_NOTE1 && par <= PAR_MIDI_NOTE7 && v == 0) strcpy(buf, "any");
        else meta_noteName(v, buf);
        break;
    case DTYPE_0b1: sprintf(buf, "%d", v + 1); break;
    case DTYPE_VOICE_LFO: sprintf(buf, "v%d", v); break;
    default: sprintf(buf, "%d", v); break;
    }
}
