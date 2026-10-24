#pragma once

#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t duration_ms;
} scenario_t;

// Slow squeeze, fingers to thumb: open 0.5 s, squeeze 2 s, hold 1 s, release 2 s.
extern const scenario_t SCENARIO_SLOW_SQUEEZE;

// Closure (0 = open, 1 = closed) at t_ms since scenario start.
float scenario_slow_squeeze_closure(uint32_t t_ms);
