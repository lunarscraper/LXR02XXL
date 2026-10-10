/* LXRXXL: the LXR's random numbers (third_party/lxr02open/Core/DSPAudio/random.c reads the STM32's hardware RNG
 * registers, which don't exist here). Same interface, a xorshift32 generator: white, full 16-bit range, as the
 * hardware's. Compiled as an engine unit, so each instance has its own state (lxr_state.h). */
#include <stdint.h>
#include "random.h"

static uint32_t rng = 0x9E3779B9u;

void initRng(void) { if (!rng) rng = 0x9E3779B9u; }

int16_t GetRngValue(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (int16_t)(rng >> 8);
}
