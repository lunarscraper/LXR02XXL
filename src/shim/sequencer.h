/* LXRXXL shim: the only thing the LXR DSP (lfo.c) takes from the sequencer is the tempo, for synced LFOs.
 * In the plugin it is the LXR's own tempo, or MPC's when the LXR follows MPC (lxr_dsp.c). A float, so a fractional
 * MPC tempo keeps synced LFOs in step. */
#pragma once
#include <stdint.h>
float lxr_dsp_getBpm(void);
#define seq_getBpm() lxr_dsp_getBpm()
