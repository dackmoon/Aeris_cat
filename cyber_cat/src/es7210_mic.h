#pragma once
#include <stdint.h>
#include <stdbool.h>

// ES7210 4-channel audio ADC — minimal driver for Waveshare V2 board.
// Only MIC1+MIC2 are populated; we use standard I2S (not TDM).
// Communicates over Wire (I2C) which must already be initialised.

bool es7210Init(uint32_t sampleRate);
