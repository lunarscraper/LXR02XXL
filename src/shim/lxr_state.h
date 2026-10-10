/* LXRXXL: force-included before every LXR DSP source and lxr_dsp.c (build flag -include).
 *
 * The LXR firmware keeps its whole sound engine in global variables (one drum machine per chip). MPC loads every
 * instance of a plugin into one process with one copy of those globals (mpc-vst-plugins docs/NOTES.md), so two LXRXXL
 * tracks would share one engine. This puts all of that state (initialised and zeroed data) into two named sections;
 * lxr_state.c gives every instance its own copy and swaps it in, under a lock, before the engine runs. */
#if defined(__clang__)
#pragma clang section bss="lxrstate_bss" data="lxrstate_data"
#else
#error "LXRXXL builds the LXR engine with clang (zig cc): the per-instance state relies on #pragma clang section"
#endif
