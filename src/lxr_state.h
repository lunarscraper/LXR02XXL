/* LXRXXL -- per-instance copies of the LXR engine's global state (see src/shim/lxr_state.h). */
#pragma once
typedef struct LxrState LxrState;
LxrState *lxr_state_create(void);
void lxr_state_destroy(LxrState *s);
void lxr_state_enter(LxrState *s);   /* lock, and make this instance's engine state the live one */
void lxr_state_leave(void);
