#pragma once
#include <stdint.h>
#include <stddef.h>

// =============================================================
//  Audio input — I2S microphone recording with simple VAD
//
//  Usage:
//    1. audioInputInit()   — once at startup
//    2. audioInputStart()  — begin recording session
//    3. loop: audioInputProcess() until it returns true (VAD stop)
//       or call audioInputStop() explicitly (e.g. on tap)
//    4. audioInputGetBuffer() / audioInputGetBytes() — raw PCM
//    5. audioInputClear() before next session
// =============================================================

// Recording parameters
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_BITS          16          // bits per sample
#define AUDIO_MAX_SECONDS   30
#define AUDIO_MAX_BYTES     ((size_t)AUDIO_SAMPLE_RATE * (AUDIO_BITS / 8) * AUDIO_MAX_SECONDS)

// VAD tuning — adjust after first hardware test
#define VAD_RMS_THRESHOLD   400         // RMS < this = silence
#define VAD_SILENCE_MS      2000        // ms of continuous silence → auto-stop
#define VAD_MIN_SPEECH_MS   300         // ignore silence until at least this much speech

void           audioInputInit();
void           audioInputStart();
void           audioInputStop();
bool           audioInputIsActive();

// Call repeatedly in the main loop while LISTENING (outside LVGL lock).
// Returns true when VAD has detected silence long enough to auto-stop.
bool           audioInputProcess();

const uint8_t* audioInputGetBuffer();
size_t         audioInputGetBytes();
void           audioInputClear();
