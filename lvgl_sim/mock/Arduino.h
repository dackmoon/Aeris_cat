#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "lvgl.h"

// millis() — map to LVGL tick (same unit: ms since boot)
static inline uint32_t millis() { return lv_tick_get(); }
static inline uint32_t micros() { return lv_tick_get() * 1000u; }

// random(lo, hi) — returns value in [lo, hi)
static inline int32_t random(int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo);
}

// Serial stub — writes to stdout
struct _SerialStub {
    void printf(const char *fmt, ...) const {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
    void println(const char *s) const { ::printf("%s\n", s); }
    void println()              const { ::printf("\n"); }
};
extern _SerialStub Serial;
