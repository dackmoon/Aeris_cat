#include "audio_input.h"
#include "es7210_mic.h"
#include "es8311_spk.h"
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <math.h>
#include <atomic>

// =============================================================
//  V2 board I2S pins — shared bus for mic (ES7210) and speaker (ES8311)
// =============================================================
#define I2S_MCLK_PIN   2
#define I2S_BCK_PIN   48
#define I2S_LRCK_PIN  38
#define MIC_SD_PIN    39
#define PA_CTRL_PIN   15

#define I2S_PORT      I2S_NUM_0

// =============================================================
//  Internal state
// =============================================================
#define CHUNK_FRAMES   160   // 10 ms at 16 kHz

static uint8_t  *_buf             = nullptr;
static size_t    _bytes           = 0;
static bool      _active          = false;
static bool      _initOk          = false;
static std::atomic<uint16_t> _level{0};

// VAD state
static uint32_t  _silenceStartMs  = 0;
static uint32_t  _speechStartMs   = 0;
static bool      _speechDetected  = false;
static uint32_t  _recordingStartMs = 0;

// Per-session diagnostics
static uint32_t  _lastRmsPrintMs  = 0;
static int       _diagCount       = 0;

// =============================================================
//  Helpers
// =============================================================
static int32_t rms(const int16_t *samples, size_t count) {
    if (count == 0) return 0;
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum += s * s;
    }
    return (int32_t)sqrtf((float)(sum / (int64_t)count));
}

// =============================================================
//  Public API
// =============================================================

void audioInputInit() {
    _buf = (uint8_t *)heap_caps_malloc(AUDIO_MAX_BYTES,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_buf) {
        Serial.println("[AudioIn] ERROR: PSRAM alloc failed");
        return;
    }

    // PA off during mic usage
    pinMode(PA_CTRL_PIN, OUTPUT);
    digitalWrite(PA_CTRL_PIN, LOW);

    // Step 1: Init both codecs via I2C BEFORE I2S clocks start.
    //         ES7210 must be configured first (Waveshare official sequence).
    //         ES8311 also needs I2C registers set before MCLK arrives.
    if (!es7210Init(AUDIO_SAMPLE_RATE)) {
        Serial.println("[AudioIn] ES7210 init failed — mic will not work");
    }
    if (!es8311Init(AUDIO_SAMPLE_RATE)) {
        Serial.println("[AudioIn] ES8311 init failed — speaker will not work");
    }

    // Step 2: Install legacy I2S driver (avoids GDMA/PSRAM issue)
    i2s_config_t cfg = {};
    cfg.mode            = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
    cfg.sample_rate     = AUDIO_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format  = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM;
    cfg.dma_buf_count   = 8;
    cfg.dma_buf_len     = 256;
    cfg.use_apll        = false;
    cfg.fixed_mclk      = AUDIO_SAMPLE_RATE * 256;  // 4.096 MHz

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[AudioIn] i2s_driver_install failed: %d\n", err);
        return;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_BCK_PIN;
    pins.ws_io_num    = I2S_LRCK_PIN;
    pins.data_out_num = 47;   // DOUT → ES8311 (needed for TTS playback)
    pins.data_in_num  = MIC_SD_PIN;
    pins.mck_io_num   = I2S_MCLK_PIN;

    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("[AudioIn] i2s_set_pin failed: %d\n", err);
        return;
    }

    _initOk = true;

    Serial.printf("[AudioIn] I2S legacy driver OK — SR=%d, MCLK=%d, pins: MCLK=%d BCK=%d LRCK=%d DIN=%d\n",
                  AUDIO_SAMPLE_RATE, AUDIO_SAMPLE_RATE * 256,
                  I2S_MCLK_PIN, I2S_BCK_PIN, I2S_LRCK_PIN, MIC_SD_PIN);

    delay(200);

    // Test read
    {
        int16_t testBuf[16];
        size_t  testRead = 0;
        i2s_read(I2S_PORT, testBuf, sizeof(testBuf), &testRead, pdMS_TO_TICKS(200));
        Serial.printf("[AudioIn] test read: %u bytes, raw[0..3]: %04X %04X %04X %04X\n",
                      (unsigned)testRead,
                      (unsigned)(uint16_t)testBuf[0], (unsigned)(uint16_t)testBuf[1],
                      (unsigned)(uint16_t)testBuf[2], (unsigned)(uint16_t)testBuf[3]);
    }

    Serial.printf("[AudioIn] init OK (V2/ES7210), buffer %u KB in PSRAM\n",
                  (unsigned)(AUDIO_MAX_BYTES / 1024));
}

void audioInputStart() {
    if (!_buf || !_initOk) {
        Serial.println("[AudioIn] not initialised");
        return;
    }

    _bytes          = 0;
    _active         = false;
    _speechDetected = false;
    _speechStartMs  = 0;
    _silenceStartMs = millis();

    digitalWrite(PA_CTRL_PIN, LOW);

    // Flush stale DMA data
    i2s_zero_dma_buffer(I2S_PORT);
    {
        int16_t discard[128];
        size_t  discardRead = 0;
        for (int i = 0; i < 5; i++) {
            i2s_read(I2S_PORT, discard, sizeof(discard), &discardRead, pdMS_TO_TICKS(10));
        }
    }
    _active = true;
    _recordingStartMs = millis();
    _lastRmsPrintMs   = 0;
    _diagCount        = 0;

    Serial.println("[AudioIn] recording started");
}

void audioInputStop() {
    _level = 0;
    if (!_active) return;
    _active = false;
    Serial.printf("[AudioIn] stopped — %u bytes / %.1f s\n",
                  (unsigned)_bytes,
                  (float)_bytes / (AUDIO_SAMPLE_RATE * (AUDIO_BITS / 8)));
}

bool audioInputIsActive() { return _active; }

bool audioInputProcess() {
    if (!_active || !_buf) return false;

    // Read stereo 16-bit frames: [L0, R0, L1, R1, ...]
    int16_t raw16[CHUNK_FRAMES * 2];
    size_t  bytesRead = 0;
    i2s_read(I2S_PORT, raw16, sizeof(raw16), &bytesRead, pdMS_TO_TICKS(20));

    if (bytesRead > 0) {
        size_t totalSamples = bytesRead / sizeof(int16_t);
        size_t frames = totalSamples / 2;

        // Extract left channel (MIC1) for mono 16-bit
        int16_t mono[CHUNK_FRAMES];
        for (size_t i = 0; i < frames && i < CHUNK_FRAMES; i++) {
            mono[i] = raw16[i * 2];
        }
        size_t monoBytes = frames * sizeof(int16_t);

        if (_bytes + monoBytes > AUDIO_MAX_BYTES) {
            Serial.println("[AudioIn] buffer full, forcing stop");
            return true;
        }
        memcpy(_buf + _bytes, mono, monoBytes);
        _bytes += monoBytes;

        int32_t r = rms(mono, frames);
        _level = (uint16_t)(r > 0xFFFF ? 0xFFFF : r);

        // Hard time limit: force stop regardless of VAD state
        uint32_t now = millis();
        uint32_t recDur = now - _recordingStartMs;
        if (recDur >= (uint32_t)AUDIO_MAX_SECONDS * 1000) {
            Serial.printf("[VAD] max duration %ds reached, forcing stop\n", AUDIO_MAX_SECONDS);
            return true;
        }

        if (now - _lastRmsPrintMs >= 1000) {
            Serial.printf("[VAD] rms=%d L=%d R=%d (thr=%d) t=%.1fs\n",
                          r, (int)raw16[0], (int)raw16[1],
                          VAD_RMS_THRESHOLD, recDur / 1000.0f);
            if (_diagCount < 5) {
                Serial.printf("  raw[0..7]: %04X %04X %04X %04X %04X %04X %04X %04X\n",
                    (unsigned)(uint16_t)raw16[0], (unsigned)(uint16_t)raw16[1],
                    (unsigned)(uint16_t)raw16[2], (unsigned)(uint16_t)raw16[3],
                    (unsigned)(uint16_t)raw16[4], (unsigned)(uint16_t)raw16[5],
                    (unsigned)(uint16_t)raw16[6], (unsigned)(uint16_t)raw16[7]);
                _diagCount++;
            }
            _lastRmsPrintMs = now;
        }

        if (r >= VAD_RMS_THRESHOLD) {
            if (!_speechDetected) {
                _speechDetected = true;
                _speechStartMs  = millis();
                Serial.printf("[VAD] speech start (rms=%d)\n", r);
            }
            _silenceStartMs = millis();
        } else if (_speechDetected) {
            uint32_t silenceDur = millis() - _silenceStartMs;
            uint32_t speechDur  = millis() - _speechStartMs;
            if (silenceDur >= VAD_SILENCE_MS && speechDur >= VAD_MIN_SPEECH_MS) {
                Serial.printf("[VAD] silence timeout (speech=%.1fs)\n",
                              speechDur / 1000.0f);
                return true;
            }
        }
    }
    return false;
}

uint16_t       audioInputGetLevel()  { return _level.load(); }
const uint8_t* audioInputGetBuffer() { return _buf; }
size_t         audioInputGetBytes()   { return _bytes; }
void           audioInputClear()      { _bytes = 0; }
