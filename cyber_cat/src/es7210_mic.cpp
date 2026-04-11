#include "es7210_mic.h"
#include <Wire.h>
#include <Arduino.h>

// =============================================================
//  ES7210 I2C address (AD1=0, AD0=0 on Waveshare V2)
// =============================================================
#define ES7210_ADDR  0x40

// =============================================================
//  Register map (matching Waveshare/Espressif reference driver)
// =============================================================
#define REG00_RESET          0x00
#define REG02_MAINCLK        0x02
#define REG04_LRCK_DIVH      0x04
#define REG05_LRCK_DIVL      0x05
#define REG06_POWER_DOWN     0x06
#define REG07_OSR            0x07
#define REG09_TIME_CTRL0     0x09
#define REG0A_TIME_CTRL1     0x0A
#define REG11_SDP_IF1        0x11
#define REG12_SDP_IF2        0x12
#define REG1B_ADC1_VOL       0x1B
#define REG1C_ADC2_VOL       0x1C
#define REG1D_ADC3_VOL       0x1D
#define REG1E_ADC4_VOL       0x1E
#define REG20_ADC34_HPF2     0x20
#define REG21_ADC34_HPF1     0x21
#define REG22_ADC12_HPF2     0x22
#define REG23_ADC12_HPF1     0x23
#define REG40_ANALOG         0x40
#define REG41_MIC12_BIAS     0x41
#define REG42_MIC34_BIAS     0x42
#define REG43_MIC1_GAIN      0x43
#define REG44_MIC2_GAIN      0x44
#define REG45_MIC3_GAIN      0x45
#define REG46_MIC4_GAIN      0x46
#define REG47_MIC1_POWER     0x47
#define REG48_MIC2_POWER     0x48
#define REG49_MIC3_POWER     0x49
#define REG4A_MIC4_POWER     0x4A
#define REG4B_MIC12_POWER    0x4B
#define REG4C_MIC34_POWER    0x4C

// =============================================================
//  Clock coefficient table  (from Espressif/Waveshare reference)
// =============================================================
struct CoeffEntry {
    uint32_t mclk;
    uint32_t lrck;
    uint8_t  adc_div;
    uint8_t  dll;
    uint8_t  doubler;
    uint8_t  osr;
    uint8_t  lrck_h;
    uint8_t  lrck_l;
};

static const CoeffEntry kCoeffs[] = {
    {  4096000,   8000,  0x01, 0x01, 0x00, 0x20, 0x02, 0x00 },
    { 12288000,   8000,  0x03, 0x01, 0x00, 0x20, 0x06, 0x00 },
    {  4096000,  16000,  0x01, 0x01, 0x01, 0x20, 0x01, 0x00 },
    { 12288000,  16000,  0x03, 0x01, 0x01, 0x20, 0x03, 0x00 },
    { 16384000,  16000,  0x02, 0x01, 0x00, 0x20, 0x04, 0x00 },
    { 11289600,  22050,  0x01, 0x01, 0x00, 0x20, 0x02, 0x00 },
    { 11289600,  44100,  0x01, 0x01, 0x01, 0x20, 0x01, 0x00 },
    { 12288000,  48000,  0x01, 0x01, 0x01, 0x20, 0x01, 0x00 },
};

// =============================================================
//  I2C helpers
// =============================================================
static bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission(true);
    if (err) {
        Serial.printf("[ES7210] I2C write 0x%02X=0x%02X failed (err=%d)\n",
                      reg, val, err);
        return false;
    }
    return true;
}

static bool readReg(uint8_t reg, uint8_t *val) {
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true)) return false;
    Wire.requestFrom((uint8_t)ES7210_ADDR, (uint8_t)1);
    if (Wire.available()) { *val = Wire.read(); return true; }
    return false;
}

static const CoeffEntry *findCoeff(uint32_t mclk, uint32_t lrck) {
    for (size_t i = 0; i < sizeof(kCoeffs) / sizeof(kCoeffs[0]); i++) {
        if (kCoeffs[i].mclk == mclk && kCoeffs[i].lrck == lrck)
            return &kCoeffs[i];
    }
    return nullptr;
}

// =============================================================
//  Public API — follows Waveshare/Espressif init sequence exactly
// =============================================================
bool es7210Init(uint32_t sampleRate) {
    // I2C bus scan for diagnostics
    Serial.println("[ES7210] I2C bus scan:");
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission(true) == 0) {
            Serial.printf("  Found device at 0x%02X\n", addr);
        }
    }

    const uint32_t mclk = sampleRate * 256;
    const CoeffEntry *c = findCoeff(mclk, sampleRate);
    if (!c) {
        Serial.printf("[ES7210] No coeff for SR=%u MCLK=%u\n",
                      (unsigned)sampleRate, (unsigned)mclk);
        return false;
    }

    // Software reset
    if (!writeReg(REG00_RESET, 0xFF)) return false;
    delay(10);
    if (!writeReg(REG00_RESET, 0x32)) return false;
    delay(10);

    // Power-up timing
    writeReg(REG09_TIME_CTRL0, 0x30);
    writeReg(REG0A_TIME_CTRL1, 0x30);

    // High-pass filter for ADC 1-4
    writeReg(REG23_ADC12_HPF1, 0x2A);
    writeReg(REG22_ADC12_HPF2, 0x0A);
    writeReg(REG21_ADC34_HPF1, 0x2A);
    writeReg(REG20_ADC34_HPF2, 0x0A);

    // I2S format: standard I2S, 16-bit, non-TDM
    writeReg(REG11_SDP_IF1, 0x60);   // 0x60 = 16-bit | I2S format
    writeReg(REG12_SDP_IF2, 0x00);   // non-TDM

    // Analog power
    writeReg(REG40_ANALOG, 0xC3);

    // MIC bias: 2.87V
    writeReg(REG41_MIC12_BIAS, 0x70);
    writeReg(REG42_MIC34_BIAS, 0x70);

    // MIC gain: 24dB (value 8 | PGA enable 0x10 = 0x18)
    // 36dB causes saturation (0x7FFF clipping) at close range
    writeReg(REG43_MIC1_GAIN, 0x18);
    writeReg(REG44_MIC2_GAIN, 0x18);
    writeReg(REG45_MIC3_GAIN, 0x18);
    writeReg(REG46_MIC4_GAIN, 0x18);

    // Power on MIC 1-4
    writeReg(REG47_MIC1_POWER, 0x08);
    writeReg(REG48_MIC2_POWER, 0x08);
    writeReg(REG49_MIC3_POWER, 0x08);
    writeReg(REG4A_MIC4_POWER, 0x08);

    // Clock coefficients
    uint8_t mainclk = c->adc_div | (c->doubler << 6) | (c->dll << 7);
    writeReg(REG02_MAINCLK, mainclk);
    writeReg(REG07_OSR, c->osr);
    writeReg(REG04_LRCK_DIVH, c->lrck_h);
    writeReg(REG05_LRCK_DIVL, c->lrck_l);

    // Power down DLL
    writeReg(REG06_POWER_DOWN, 0x04);

    // Power on MIC1-4 bias & ADC & PGA
    writeReg(REG4B_MIC12_POWER, 0x0F);
    writeReg(REG4C_MIC34_POWER, 0x0F);

    // Enable device
    writeReg(REG00_RESET, 0x71);
    writeReg(REG00_RESET, 0x41);

    // Set ADC digital volume: 0xBF=0dB(default), 0xFF=+32dB max
    // Using 0dB — +32dB causes clipping at close range
    uint8_t volReg = 0xBF;
    writeReg(REG1B_ADC1_VOL, volReg);
    writeReg(REG1C_ADC2_VOL, volReg);
    writeReg(REG1D_ADC3_VOL, volReg);
    writeReg(REG1E_ADC4_VOL, volReg);

    // Verify
    uint8_t readback = 0;
    if (readReg(REG40_ANALOG, &readback)) {
        Serial.printf("[ES7210] reg 0x40 readback: 0x%02X (expect 0xC3)\n", readback);
    }

    Serial.printf("[ES7210] init OK — SR=%uHz, MCLK=%uHz\n",
                  (unsigned)sampleRate, (unsigned)mclk);
    return true;
}
