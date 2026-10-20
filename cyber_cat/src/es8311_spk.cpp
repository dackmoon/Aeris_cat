#include "es8311_spk.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <math.h>

// =============================================================
//  Hardware constants (V2 board)
// =============================================================
#define ES8311_ADDR   0x18
#define I2S_PORT      I2S_NUM_0
#define PA_CTRL_PIN   15

// =============================================================
//  I2C helper
// =============================================================
static bool wr(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission(true);
    if (err) {
        Serial.printf("[ES8311] I2C write reg=0x%02X val=0x%02X err=%d\n", reg, val, err);
        return false;
    }
    return true;
}

// =============================================================
//  Public: init
//
//  Register sequence for 16kHz, MCLK=4.096MHz (= SR×256),
//  I2S slave mode (ESP32-S3 provides MCLK/BCLK/LRCK).
//  Based on Espressif ESP-ADF es8311.c + Waveshare V2 reference.
// =============================================================
bool es8311Init(uint32_t sampleRate) {
    // Soft reset (3-step sequence from Espressif reference driver)
    if (!wr(0x00, 0x1F)) return false;
    delay(20);
    wr(0x00, 0x00);
    wr(0x00, 0x80);  // Power-on command — missing this caused no-init symptoms

    // Clock manager — MCLK from external pin, slave on BCLK/LRCK
    // 0x3F = enable ALL internal clock gates; bit7=0: MCLK pin; bit6=0: not inverted
    wr(0x01, 0x3F);

    // Clock dividers for MCLK=4096kHz → SR=16kHz
    // From Espressif coeff_div table: {4096000, 16000, pre_div=1, pre_multi=0,
    //   adc_div=1, dac_div=1, fs_mode=0, lrck_h=0x00, lrck_l=0xFF, bclk_div=4,
    //   adc_osr=0x10, dac_osr=0x10}
    wr(0x02, 0x00);  // (pre_div-1)<<5 | pre_multi<<3 = 0
    wr(0x03, 0x10);  // fs_mode<<6 | adc_osr = 0x10
    wr(0x04, 0x10);  // dac_osr = 0x10
    wr(0x05, 0x00);  // (adc_div-1)<<4 | (dac_div-1) = 0
    wr(0x06, 0x03);  // bclk_div - 1 = 3
    wr(0x07, 0x00);  // lrck_h = 0x00
    wr(0x08, 0xFF);  // lrck_l = 0xFF

    // Serial data port format: Slave mode, Standard I2S, 16-bit
    // REG09 = SDP In  (ADC path): 16-bit → (3<<2) = 0x0C
    // REG0A = SDP Out (DAC path): 16-bit → (3<<2) = 0x0C
    // (REG00 bit6=0 after reset = slave mode, no explicit write needed)
    wr(0x09, 0x0C);
    wr(0x0A, 0x0C);

    // System power-up sequence (from Espressif reference es8311_init)
    wr(0x0D, 0x01);  // Power up analog circuitry
    wr(0x0E, 0x02);  // Enable analog PGA + ADC modulator (was 0xA0 — WRONG)
    wr(0x12, 0x00);  // Power-up DAC
    wr(0x13, 0x10);  // Enable output to HP drive — CRITICAL, was 0x00 (silent!)

    // Analog MIC input (for recording path — keep enabled)
    wr(0x14, 0x1A);  // Enable analog MIC, max PGA gain

    // ADC equalizer bypass + DC offset cancel
    wr(0x1C, 0x6A);

    // Bypass DAC equalizer (was 0x48 — wrong fade-rate bits)
    wr(0x37, 0x08);

    // DAC digital volume: 50% default — call es8311SetVolume() to change
    wr(0x32, 0x7F);

    Serial.printf("[ES8311] init OK (SR=%u, MCLK=%u)\n",
                  (unsigned)sampleRate, (unsigned)(sampleRate * 256));
    return true;
}

// =============================================================
//  Public: play
//
//  Accepts 16kHz 16-bit mono or stereo WAV (44-byte header).
//  Mono samples are duplicated L→R for the stereo I2S bus.
//  Runs entirely inside the caller's task (blocking).
// =============================================================
// Frames queued in the I2S DMA ring (dma_buf_count × dma_buf_len in audio_input.cpp)
// play out after i2s_write() returns; the published level is delayed to match.
static const size_t DMA_QUEUED_FRAMES = 8 * 256;

static std::atomic<uint16_t> s_playLevel{0};
static std::atomic<bool>     s_silent{false};
static bool                  s_aborted = false;

uint16_t es8311GetPlayLevel()        { return s_playLevel.load(); }
void     es8311SetSilent(bool silent) { s_silent = silent; }
bool     es8311WasAborted()          { return s_aborted; }

bool es8311Play(const uint8_t *wavData, size_t wavBytes, const std::atomic<bool> *abort) {
    s_aborted = false;
    if (!wavData || wavBytes < 44) {
        Serial.println("[ES8311] play: invalid WAV");
        return false;
    }

    // Parse WAV header
    uint16_t channels    = *(const uint16_t *)(wavData + 22);
    uint32_t sampleRate  = *(const uint32_t *)(wavData + 24);
    uint16_t bitsPerSamp = *(const uint16_t *)(wavData + 34);
    size_t   pcmBytes    = wavBytes - 44;

    Serial.printf("[ES8311] play: %uHz %uch %ubit, %u bytes PCM\n",
                  (unsigned)sampleRate, (unsigned)channels,
                  (unsigned)bitsPerSamp, (unsigned)pcmBytes);

    // PCM content diagnostics: RMS and peak of first 1 second
    {
        const int16_t *pcm = (const int16_t *)(wavData + 44);
        size_t nSamp = min(pcmBytes / 2, (size_t)sampleRate);  // first 1s
        int64_t sum = 0;
        int16_t peak = 0;
        for (size_t i = 0; i < nSamp; i++) {
            sum += (int64_t)pcm[i] * pcm[i];
            int16_t a = pcm[i] < 0 ? -pcm[i] : pcm[i];
            if (a > peak) peak = a;
        }
        int32_t rmsV = (int32_t)sqrtf((float)(sum / (int64_t)nSamp));
        Serial.printf("[ES8311] PCM stats (1st 1s): rms=%d peak=%d\n", rmsV, (int)peak);
    }

    if (bitsPerSamp != 16) {
        Serial.println("[ES8311] play: only 16-bit PCM supported");
        return false;
    }

    // Stereo conversion buffer in PSRAM (512 stereo frames × 4 bytes = 2KB)
    static const size_t FRAMES = 512;
    int16_t *stereoBuf = (int16_t *)heap_caps_malloc(FRAMES * 4,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!stereoBuf) {
        Serial.println("[ES8311] play: alloc failed");
        return false;
    }

    const int16_t *src = (const int16_t *)(wavData + 44);
    size_t numSrcSamples = pcmBytes / 2;   // total 16-bit samples in file
    size_t srcIdx = 0;

    // Reset TX DMA state before playback (recording may have left it in an odd state)
    i2s_zero_dma_buffer(I2S_PORT);

    // Enable PA
    if (!s_silent) digitalWrite(PA_CTRL_PIN, HIGH);
    delay(10);  // allow NS4150B to ramp up

    static const size_t LEVEL_DELAY = DMA_QUEUED_FRAMES / FRAMES;
    uint16_t levelRing[LEVEL_DELAY + 1] = {};
    size_t   levelIdx = 0;

    uint32_t t0 = millis();
    bool ok = true;
    while (srcIdx < numSrcSamples) {
        if (abort && abort->load()) {
            s_aborted = true;
            Serial.println("[ES8311] play aborted");
            break;
        }
        // How many source frames can we grab this chunk?
        size_t srcFrames;
        if (channels == 1) {
            srcFrames = numSrcSamples - srcIdx;
            if (srcFrames > FRAMES) srcFrames = FRAMES;
            for (size_t i = 0; i < srcFrames; i++) {
                stereoBuf[i * 2]     = src[srcIdx + i];   // L
                stereoBuf[i * 2 + 1] = src[srcIdx + i];   // R (duplicate)
            }
        } else {
            // stereo: each frame = 2 samples
            srcFrames = (numSrcSamples - srcIdx) / 2;
            if (srcFrames > FRAMES) srcFrames = FRAMES;
            memcpy(stereoBuf, &src[srcIdx], srcFrames * 4);
        }
        if (srcFrames == 0) break;

        {
            int64_t sum = 0;
            for (size_t i = 0; i < srcFrames; i++) {
                int32_t s = stereoBuf[i * 2];
                sum += s * s;
            }
            levelRing[levelIdx % (LEVEL_DELAY + 1)] = (uint16_t)sqrtf((float)(sum / (int64_t)srcFrames));
            s_playLevel = levelRing[(levelIdx + 1) % (LEVEL_DELAY + 1)];
            levelIdx++;
        }

        // Write with full retry loop — partial writes must not silently drop data
        size_t toWrite = srcFrames * 4;
        size_t totalWritten = 0;
        const uint8_t *wptr = (const uint8_t *)stereoBuf;
        while (totalWritten < toWrite) {
            size_t bytesWritten = 0;
            esp_err_t err = i2s_write(I2S_PORT, wptr + totalWritten,
                                       toWrite - totalWritten,
                                       &bytesWritten, pdMS_TO_TICKS(500));
            if (err != ESP_OK) {
                Serial.printf("[ES8311] i2s_write err=%d\n", err);
                ok = false;
                break;
            }
            if (bytesWritten == 0) {
                Serial.println("[ES8311] i2s_write: DMA stalled (0 bytes), aborting");
                ok = false;
                break;
            }
            totalWritten += bytesWritten;
        }
        if (!ok) break;

        // Advance source index only after successful write
        if (channels == 1) srcIdx += srcFrames;
        else               srcIdx += srcFrames * 2;
    }

    uint32_t elapsed = millis() - t0;
    Serial.printf("[ES8311] play loop done in %ums (expected ~%ums)\n",
                  elapsed, (unsigned)(pcmBytes * 1000UL / (sampleRate * 2)));

    // Let I2S DMA drain before muting PA (avoids pop). After an abort the queued
    // tail is cleared instead of being played out.
    if (s_aborted) i2s_zero_dma_buffer(I2S_PORT);
    vTaskDelay(pdMS_TO_TICKS(80));
    digitalWrite(PA_CTRL_PIN, LOW);
    s_playLevel = 0;

    heap_caps_free(stereoBuf);
    return ok || s_aborted;
}

// =============================================================
//  Public: setVolume / getVolume  (0–100%)
//
//  s_userVol is the persisted user preference.
//  Voice-command volume control: call es8311SetVolume() with the
//  new level — that's the only hook needed for Phase 5.
// =============================================================
static uint8_t s_userVol = 20;  // persists across TTS calls

static void applyVolReg(uint8_t vol_pct) {
    uint8_t reg32 = (vol_pct == 0) ? 0 : (uint8_t)((vol_pct * 256 / 100) - 1);
    wr(0x32, reg32);
}

void es8311SetVolume(uint8_t vol_pct) {
    if (vol_pct > 100) vol_pct = 100;
    s_userVol = vol_pct;
    applyVolReg(vol_pct);
    Serial.printf("[ES8311] volume %u%% (user)\n", vol_pct);
}

uint8_t es8311GetVolume() {
    return s_userVol;
}

void es8311BoostForTTS(uint8_t floor_pct) {
    uint8_t vol = (s_userVol > floor_pct) ? s_userVol : floor_pct;
    applyVolReg(vol);
    Serial.printf("[ES8311] TTS boost %u%% (user=%u%%, floor=%u%%)\n",
                  vol, s_userVol, floor_pct);
}

void es8311RestoreVolume() {
    applyVolReg(s_userVol);
    Serial.printf("[ES8311] restored %u%%\n", s_userVol);
}

// =============================================================
//  Public: beep  (hardware self-test — generates sine wave)
// =============================================================
bool es8311Beep(uint32_t freq_hz, uint32_t duration_ms) {
    const uint32_t sr      = 16000;
    const uint32_t nSamp   = sr * duration_ms / 1000;
    const size_t   pcmBytes = nSamp * 2;
    const size_t   total    = 44 + pcmBytes;

    uint8_t *wav = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav) {
        Serial.println("[ES8311] beep: alloc failed");
        return false;
    }

    // Build minimal WAV header (inline, no external helper needed)
    uint32_t byteRate   = sr * 2;
    uint32_t chunkSize  = 36 + pcmBytes;
    uint32_t dataSize   = pcmBytes;
    uint16_t ch = 1, bits = 16, fmt = 1, align = 2;
    memcpy(wav,      "RIFF", 4); memcpy(wav+4,  &chunkSize, 4);
    memcpy(wav+8,   "WAVE", 4); memcpy(wav+12, "fmt ", 4);
    uint32_t fmtSz = 16;
    memcpy(wav+16, &fmtSz, 4);  memcpy(wav+20, &fmt,    2);
    memcpy(wav+22, &ch,    2);  memcpy(wav+24, &sr,      4);
    memcpy(wav+28, &byteRate,4);memcpy(wav+32, &align,   2);
    memcpy(wav+34, &bits,  2);  memcpy(wav+36, "data", 4);
    memcpy(wav+40, &dataSize,4);

    // Generate sine wave (amplitude ~75% of full scale)
    int16_t *pcm = (int16_t *)(wav + 44);
    for (uint32_t i = 0; i < nSamp; i++) {
        pcm[i] = (int16_t)(24000.0f * sinf(2.0f * (float)M_PI * freq_hz * i / sr));
    }

    Serial.printf("[ES8311] beep %uHz %ums\n", (unsigned)freq_hz, (unsigned)duration_ms);
    bool ok = es8311Play(wav, total);
    heap_caps_free(wav);
    return ok;
}
