#pragma once
#include <stdint.h>
#include <stddef.h>
#include <atomic>

// Initialize ES8311 DAC via I2C.
// Must be called after Wire.begin() and es7210Init(), before I2S starts.
// sampleRate must match the I2S driver (default 16000).
bool es8311Init(uint32_t sampleRate = 16000);

// Play WAV data (44-byte header + 16-bit PCM, mono or stereo).
// Blocking — runs inside a FreeRTOS task, not the main loop.
// Enables PA (GPIO15) before playback, mutes after.
// Returns true on success, including when stopped early via `abort`.
// If `abort` becomes true during playback, output stops within one chunk (~32ms)
// and es8311WasAborted() returns true until the next play.
bool es8311Play(const uint8_t *wavData, size_t wavBytes,
                const std::atomic<bool> *abort = nullptr);
bool es8311WasAborted();

// RMS of the samples currently leaving the speaker (DMA latency compensated);
// 0 when idle. Safe to read from any core.
uint16_t es8311GetPlayLevel();

// Keep the power amplifier off during playback (timing and level still behave
// as normal). Used for silent on-device testing.
void es8311SetSilent(bool silent);

// Set DAC output volume. vol_pct: 0 (mute) – 100 (max).
// Persists as the user base volume — this is the hook for voice-command volume control.
void es8311SetVolume(uint8_t vol_pct);

// Get the current user base volume (0–100).
uint8_t es8311GetVolume();

// Temporarily override DAC volume for TTS playback without changing the user
// base volume. Applies max(userVol, floor_pct) so user-set high volumes are
// never lowered. Call es8311RestoreVolume() after playback to revert.
void es8311BoostForTTS(uint8_t floor_pct = 70);
void es8311RestoreVolume();

// Play a pure sine-wave tone. Useful for hardware self-test at boot.
// freq_hz: e.g. 1000 for 1kHz; duration_ms: length in ms.
bool es8311Beep(uint32_t freq_hz, uint32_t duration_ms);
