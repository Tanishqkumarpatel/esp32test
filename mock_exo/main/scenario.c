#include "scenario.h"

// Phase durations: from the mock exo plan (open 0.5 s, squeeze 2 s, hold 1 s, release 2 s).
// TBD: replace with real motion traces from the exo team (open question).
#define OPEN_MS     500u
#define SQUEEZE_MS  2000u
#define HOLD_MS     1000u
#define RELEASE_MS  2000u

// TBD placeholders: closure at rest and at full squeeze. Not from any doc.
// Ask the exo team what closure a real slow squeeze reaches.
#define CLOSURE_OPEN     0.0f
#define CLOSURE_SQUEEZED 0.8f

const scenario_t SCENARIO_SLOW_SQUEEZE = {
    .name = "slow_squeeze",
    .duration_ms = OPEN_MS + SQUEEZE_MS + HOLD_MS + RELEASE_MS,
};

// Linear ramps. TBD: a real hand likely follows a smoother profile.
float scenario_slow_squeeze_closure(uint32_t t_ms)
{
    if (t_ms < OPEN_MS) {
        return CLOSURE_OPEN;
    }
    t_ms -= OPEN_MS;

    if (t_ms < SQUEEZE_MS) {
        return CLOSURE_OPEN + (CLOSURE_SQUEEZED - CLOSURE_OPEN) * ((float)t_ms / SQUEEZE_MS);
    }
    t_ms -= SQUEEZE_MS;

    if (t_ms < HOLD_MS) {
        return CLOSURE_SQUEEZED;
    }
    t_ms -= HOLD_MS;

    if (t_ms < RELEASE_MS) {
        return CLOSURE_SQUEEZED - (CLOSURE_SQUEEZED - CLOSURE_OPEN) * ((float)t_ms / RELEASE_MS);
    }
    return CLOSURE_OPEN;
}
