// On-device integration test — no microphone or speaker involved.
// Feeds a pre-recorded speech sample through the same backend /chat pipeline
// the firmware uses, then downloads (but does not play) the TTS reply.
//
// Run: pio test -e esp32s3

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <unity.h>
#include <esp_heap_caps.h>

#include "secrets.h"
#include "speech_sample.h"

static const int I2C_SDA = 11;
static const int I2C_SCL = 10;
static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t HTTP_TIMEOUT_MS = 30000;
static const size_t   TTS_MAX_BYTES   = 500 * 1024;

static String g_audioUrl;

static void putLE16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void putLE32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }
static uint32_t getLE32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void buildWavHeader(uint8_t *h, uint32_t pcmBytes) {
    memcpy(h, "RIFF", 4);      putLE32(h + 4, 36 + pcmBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    putLE32(h + 16, 16);       putLE16(h + 20, 1);  putLE16(h + 22, 1);
    putLE32(h + 24, 16000);    putLE32(h + 28, 16000 * 2);
    putLE16(h + 32, 2);        putLE16(h + 34, 16);
    memcpy(h + 36, "data", 4); putLE32(h + 40, pcmBytes);
}

static bool i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------------------

void test_psram_available() {
    size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[test] PSRAM total=%u free=%u\n", (unsigned)psram,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    TEST_ASSERT_GREATER_THAN_MESSAGE(4 * 1024 * 1024, psram, "PSRAM missing or < 4MB");
}

void test_i2c_devices_respond() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.print("[test] I2C scan:");
    for (uint8_t a = 1; a < 127; a++) {
        if (i2cPresent(a)) Serial.printf(" 0x%02X", a);
    }
    Serial.println();

    TEST_ASSERT_TRUE_MESSAGE(i2cPresent(0x20), "TCA9554 GPIO expander (0x20) not found");
    TEST_ASSERT_TRUE_MESSAGE(i2cPresent(0x15), "CST816 touch (0x15) not found");
    TEST_ASSERT_TRUE_MESSAGE(i2cPresent(0x18), "ES8311 speaker DAC (0x18) not found");
    TEST_ASSERT_TRUE_MESSAGE(i2cPresent(0x40), "ES7210 mic ADC (0x40) not found");
    TEST_ASSERT_TRUE_MESSAGE(i2cPresent(0x51), "PCF85063 RTC (0x51) not found");
    // QMI8658 IMU is not used by the firmware yet and does not ACK at 0x6A/0x6B on this board.
}

void test_wifi_connects() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID_1, WIFI_PASS_1);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) delay(200);
    TEST_ASSERT_EQUAL_MESSAGE(WL_CONNECTED, WiFi.status(), "WiFi connect timeout (2.4GHz? password?)");
    Serial.printf("[test] WiFi ok: ip=%s rssi=%d dBm (%lums)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), millis() - start);
}

void test_backend_health() {
#ifndef BACKEND_URL
    TEST_IGNORE_MESSAGE("BACKEND_URL not defined (direct mode)");
#else
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    TEST_ASSERT_TRUE(http.begin(client, String(BACKEND_URL) + "/health"));
    int code = http.GET();
    String body = http.getString();
    http.end();
    Serial.printf("[test] /health -> %d %s\n", code, body.c_str());
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_EQUAL(-1, body.indexOf("ok"));
#endif
}

void test_backend_chat_pipeline() {
#ifndef BACKEND_URL
    TEST_IGNORE_MESSAGE("BACKEND_URL not defined (direct mode)");
#else
    size_t wavSize = 44 + SPEECH_PCM_LEN;
    uint8_t *wav = (uint8_t *)heap_caps_malloc(wavSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(wav);
    buildWavHeader(wav, SPEECH_PCM_LEN);
    memcpy(wav + 44, SPEECH_PCM, SPEECH_PCM_LEN);

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    TEST_ASSERT_TRUE(http.begin(client, String(BACKEND_URL) + "/chat"));
    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("X-Session-ID", "test-" + WiFi.macAddress());

    uint32_t t0 = millis();
    int code = http.POST(wav, wavSize);
    String body = http.getString();
    http.end();
    heap_caps_free(wav);
    Serial.printf("[test] /chat -> %d in %lums: %s\n", code, millis() - t0, body.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(200, code, body.c_str());

    JsonDocument doc;
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, body), "response is not JSON");
    const char *text    = doc["text_reply"] | "";
    const char *emotion = doc["emotion"]    | "";
    const char *audio   = doc["audio_url"]  | "";
    TEST_ASSERT_TRUE_MESSAGE(strlen(text) > 0, "empty text_reply");
    TEST_ASSERT_TRUE_MESSAGE(!strcmp(emotion, "happy") || !strcmp(emotion, "thinking") ||
                             !strcmp(emotion, "neutral") || !strcmp(emotion, "talking"),
                             "unknown emotion value");
    TEST_ASSERT_TRUE_MESSAGE(strncmp(audio, "http", 4) == 0, "audio_url missing");
    g_audioUrl = audio;
#endif
}

void test_tts_audio_download() {
    if (g_audioUrl.isEmpty()) TEST_IGNORE_MESSAGE("no audio_url from previous test");

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    TEST_ASSERT_TRUE(http.begin(client, g_audioUrl));
    int code = http.GET();
    TEST_ASSERT_EQUAL(200, code);

    uint8_t *buf = (uint8_t *)heap_caps_malloc(TTS_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(buf);
    WiFiClient *stream = http.getStreamPtr();
    size_t total = 0;
    uint32_t deadline = millis() + HTTP_TIMEOUT_MS;
    while (total < TTS_MAX_BYTES && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            total += stream->readBytes(buf + total, min((size_t)avail, TTS_MAX_BYTES - total));
        } else if (!http.connected()) {
            break;
        } else {
            delay(5);
        }
    }
    http.end();

    bool riff = total >= 44 && !memcmp(buf, "RIFF", 4) && !memcmp(buf + 8, "WAVE", 4);
    uint32_t rate = total >= 44 ? getLE32(buf + 24) : 0;
    uint16_t ch   = total >= 44 ? (buf[22] | buf[23] << 8) : 0;
    heap_caps_free(buf);

    Serial.printf("[test] TTS wav %u bytes, %u Hz, %u ch (~%.1fs, not played)\n",
                  (unsigned)total, (unsigned)rate, ch, total / (float)(rate * ch * 2 + 1));
    TEST_ASSERT_TRUE_MESSAGE(riff, "TTS download is not a WAV file");
    TEST_ASSERT_GREATER_THAN(1000, total);
    TEST_ASSERT_EQUAL_MESSAGE(16000, rate, "firmware expects 16kHz TTS");
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_psram_available);
    RUN_TEST(test_i2c_devices_respond);
    RUN_TEST(test_wifi_connects);
    RUN_TEST(test_backend_health);
    RUN_TEST(test_backend_chat_pipeline);
    RUN_TEST(test_tts_audio_download);
    UNITY_END();
}

void loop() {}
