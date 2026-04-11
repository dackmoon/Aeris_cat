// Set to 1 to dump TTS WAV to serial before playback.
// Capture the serial log, then run:
//   python3 -c "
//   import re, binascii, sys
//   d = open('serial.log').read()
//   m = re.search(r'WAV_DUMP_BEGIN \d+ bytes\n([0-9A-F\n]+)WAV_DUMP_END', d)
//   open('tts.wav','wb').write(binascii.unhexlify(m.group(1).replace('\n','')))
//   "
#define TTS_DUMP_SERIAL 0

#include "cloud_client.h"
#include "es8311_spk.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// =============================================================
//  Constants
// =============================================================
static const char *SYSTEM_PROMPT =
    "你是一只可爱的电子猫咪。用简短的一两句话回复，可以加上喵~等拟声词。";

static const int  HTTP_TIMEOUT_MS  = 15000;
static const int  WORKER_STACK     = 16384;   // chat task
static const int  STT_WORKER_STACK = 32768;   // STT needs extra room for TLS + 245KB body

static const char *STT_MODEL     = "FunAudioLLM/SenseVoiceSmall";
static const char *BOUNDARY       = "CyberCatBoundary";

// =============================================================
//  WAV header builder (44 bytes, little-endian PCM)
// =============================================================
static void buildWavHeader(uint8_t *hdr, uint32_t pcmBytes,
                            uint32_t sampleRate = 16000,
                            uint16_t channels   = 1,
                            uint16_t bitsPerSample = 16) {
    uint32_t dataSize   = pcmBytes;
    uint32_t byteRate   = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;
    uint32_t chunkSize  = 36 + dataSize;

    // RIFF chunk
    memcpy(hdr,      "RIFF", 4);
    memcpy(hdr + 4,  &chunkSize,    4);
    memcpy(hdr + 8,  "WAVE", 4);
    // fmt sub-chunk
    memcpy(hdr + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(hdr + 16, &fmtSize,      4);
    uint16_t audioFmt = 1;   // PCM
    memcpy(hdr + 20, &audioFmt,     2);
    memcpy(hdr + 22, &channels,     2);
    memcpy(hdr + 24, &sampleRate,   4);
    memcpy(hdr + 28, &byteRate,     4);
    memcpy(hdr + 32, &blockAlign,   2);
    memcpy(hdr + 34, &bitsPerSample,2);
    // data sub-chunk
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &dataSize,     4);
}

// =============================================================
//  Public API
// =============================================================
void CloudClient::begin(const char *apiKey,    const char *apiBase,    const char *model,
                         const char *sttApiKey, const char *sttApiBase,
                         const char *ttsApiKey, const char *ttsApiBase,
                         const char *ttsModel,  const char *ttsVoice) {
    _apiKey     = apiKey;
    _apiBase    = apiBase;
    _model      = model;
    _sttApiKey  = sttApiKey;
    _sttApiBase = sttApiBase;
    _ttsApiKey  = ttsApiKey;
    _ttsApiBase = ttsApiBase;
    _ttsModel   = ttsModel;
    _ttsVoice   = ttsVoice;

    _response   = (char *)ps_malloc(RESP_BUF_SIZE);
    _errMsg     = (char *)ps_malloc(ERR_BUF_SIZE);
    _transcript = (char *)ps_malloc(RESP_BUF_SIZE);
    if (_response)   _response[0]   = '\0';
    if (_errMsg)     _errMsg[0]     = '\0';
    if (_transcript) _transcript[0] = '\0';
}

bool CloudClient::sendChat(const char *message) {
    if (_state == CLOUD_BUSY) return false;

    size_t len  = strlen(message);
    _pendingMsg = (char *)ps_malloc(len + 1);
    if (!_pendingMsg) {
        Serial.println("[Cloud] ps_malloc failed for message");
        return false;
    }
    memcpy(_pendingMsg, message, len + 1);

    _state          = CLOUD_BUSY;
    _response[0]    = '\0';
    _errMsg[0]      = '\0';
    if (_transcript) _transcript[0] = '\0';

    BaseType_t ok = xTaskCreatePinnedToCore(
        _chatWorkerTask, "cloud_chat", WORKER_STACK, this, 1, nullptr, 1);  // Core 1 — Core 0 is LVGL
    if (ok != pdPASS) {
        _state = CLOUD_ERROR;
        strlcpy(_errMsg, "Failed to create chat task", ERR_BUF_SIZE);
        free(_pendingMsg);
        _pendingMsg = nullptr;
        return false;
    }
    return true;
}

bool CloudClient::sendAudio(const uint8_t *pcm, size_t bytes) {
    if (_state == CLOUD_BUSY) return false;
    if (!pcm || bytes == 0) {
        strlcpy(_errMsg, "sendAudio: empty buffer", ERR_BUF_SIZE);
        _state = CLOUD_ERROR;
        return false;
    }

    // Copy PCM into PSRAM-backed buffer (caller's buffer may be overwritten)
    _pendingPcm = (uint8_t *)ps_malloc(bytes);
    if (!_pendingPcm) {
        strlcpy(_errMsg, "sendAudio: ps_malloc failed", ERR_BUF_SIZE);
        _state = CLOUD_ERROR;
        return false;
    }
    memcpy(_pendingPcm, pcm, bytes);
    _pendingBytes = bytes;

    _state          = CLOUD_BUSY;
    _response[0]    = '\0';
    _errMsg[0]      = '\0';
    if (_transcript) _transcript[0] = '\0';

    BaseType_t ok = xTaskCreatePinnedToCore(
        _sttWorkerTask, "cloud_stt", STT_WORKER_STACK, this, 1, nullptr, 1);  // Core 1
    if (ok != pdPASS) {
        _state = CLOUD_ERROR;
        strlcpy(_errMsg, "Failed to create STT task", ERR_BUF_SIZE);
        free(_pendingPcm);
        _pendingPcm   = nullptr;
        _pendingBytes = 0;
        return false;
    }
    return true;
}

CloudState  CloudClient::poll()          const { return _state; }
const char *CloudClient::getResponse()   const { return _response; }
const char *CloudClient::getError()      const { return _errMsg; }
const char *CloudClient::getTranscript() const { return _transcript ? _transcript : ""; }
const char *CloudClient::getEmotion()    const { return _emotion; }

void CloudClient::consume() { _state = CLOUD_IDLE; }

// =============================================================
//  Internal: LLM chat (POST /chat/completions)
// =============================================================
bool CloudClient::_doChat(const char *text) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    char url[256];
    snprintf(url, sizeof(url), "%s/chat/completions", _apiBase);

    if (!http.begin(client, url)) {
        strlcpy(_errMsg, "Chat: HTTP begin failed", ERR_BUF_SIZE);
        return false;
    }

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", _apiKey);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", authHeader);

    JsonDocument doc;
    doc["model"] = _model;
    JsonArray messages = doc["messages"].to<JsonArray>();

    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"]    = "system";
    sysMsg["content"] = SYSTEM_PROMPT;

    JsonObject userMsg = messages.add<JsonObject>();
    userMsg["role"]    = "user";
    userMsg["content"] = text;

    doc["max_tokens"]  = 150;
    doc["temperature"] = 0.8;

    String body;
    serializeJson(doc, body);

    Serial.printf("[Cloud] Chat POST %d bytes\n", body.length());
    int code = http.POST(body);

    bool ok = false;
    if (code <= 0) {
        snprintf(_errMsg, ERR_BUF_SIZE, "Chat HTTP error: %s",
                 http.errorToString(code).c_str());
        Serial.printf("[Cloud] Chat failed: %s\n", _errMsg);
    } else if (code != 200) {
        String resp = http.getString();
        snprintf(_errMsg, ERR_BUF_SIZE, "Chat HTTP %d: %.180s", code, resp.c_str());
        Serial.printf("[Cloud] Chat failed: %s\n", _errMsg);
    } else {
        JsonDocument resp;
        DeserializationError err = deserializeJson(resp, http.getStream());
        if (err) {
            snprintf(_errMsg, ERR_BUF_SIZE, "Chat JSON: %s", err.c_str());
        } else {
            const char *content = resp["choices"][0]["message"]["content"];
            if (content) {
                strlcpy(_response, content, RESP_BUF_SIZE);
                Serial.printf("[Cloud] Reply: %s\n", _response);
                ok = true;
            } else {
                strlcpy(_errMsg, "Chat: no content in response", ERR_BUF_SIZE);
            }
        }
    }

    http.end();
    return ok;
}

// =============================================================
//  Internal: STT (POST /audio/transcriptions, multipart/form-data)
//
//  Body layout:
//    --CyberCatBoundary\r\n
//    Content-Disposition: form-data; name="model"\r\n\r\n
//    FunAudioLLM/SenseVoiceSmall\r\n
//    --CyberCatBoundary\r\n
//    Content-Disposition: form-data; name="file"; filename="audio.wav"\r\n
//    Content-Type: audio/wav\r\n\r\n
//    <44-byte WAV header + raw PCM>
//    \r\n
//    --CyberCatBoundary--\r\n
// =============================================================
bool CloudClient::_doStt() {
    // ---- Build multipart body in PSRAM ----
    const uint8_t *pcm      = _pendingPcm;
    const size_t   pcmBytes = _pendingBytes;

    static const char *PART_MODEL =
        "--" "CyberCatBoundary" "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n"
        "\r\n";
    static const char *PART_MODEL_END = "\r\n";

    static const char *PART_FILE =
        "--" "CyberCatBoundary" "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";

    static const char *FINAL_BOUNDARY = "\r\n--" "CyberCatBoundary" "--\r\n";

    const size_t modelNameLen   = strlen(STT_MODEL);
    const size_t wavHeaderBytes = 44;
    const size_t totalSize =
        strlen(PART_MODEL) + modelNameLen + strlen(PART_MODEL_END) +
        strlen(PART_FILE)  + wavHeaderBytes + pcmBytes +
        strlen(FINAL_BOUNDARY);

    uint8_t *body = (uint8_t *)ps_malloc(totalSize);
    if (!body) {
        strlcpy(_errMsg, "STT: ps_malloc failed for body", ERR_BUF_SIZE);
        return false;
    }

    // Assemble body
    size_t pos = 0;
    auto append = [&](const void *src, size_t n) {
        memcpy(body + pos, src, n);
        pos += n;
    };

    append(PART_MODEL,      strlen(PART_MODEL));
    append(STT_MODEL,       modelNameLen);
    append(PART_MODEL_END,  strlen(PART_MODEL_END));
    append(PART_FILE,       strlen(PART_FILE));

    uint8_t wavHdr[44];
    buildWavHeader(wavHdr, (uint32_t)pcmBytes);
    append(wavHdr, 44);
    append(pcm,    pcmBytes);

    append(FINAL_BOUNDARY, strlen(FINAL_BOUNDARY));

    Serial.printf("[Cloud] STT body: %u bytes (WAV %u + PCM %u)\n",
                  (unsigned)totalSize, (unsigned)wavHeaderBytes, (unsigned)pcmBytes);

    // ---- HTTP POST ----
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    char url[256];
    snprintf(url, sizeof(url), "%s/audio/transcriptions", _sttApiBase);

    bool ok = false;
    int  code = 0;

    // client must outlive http for the entire request incl. getString()
    WiFiClientSecure client;
    client.setInsecure();

    // Retry once on socket-level failure (transient WiFi / lwIP resource issue)
    for (int attempt = 1; attempt <= 2; attempt++) {
        if (!http.begin(client, url)) {
            strlcpy(_errMsg, "STT: HTTP begin failed", ERR_BUF_SIZE);
            if (attempt < 2) { http.end(); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            free(body);
            return false;
        }

        char authHeader[128];
        snprintf(authHeader, sizeof(authHeader), "Bearer %s", _sttApiKey);
        http.addHeader("Authorization", authHeader);
        http.addHeader("Content-Type",
                       "multipart/form-data; boundary=" "CyberCatBoundary");

        code = http.POST(body, totalSize);

        if (code > 0) break;  // got an HTTP response — stop retrying

        snprintf(_errMsg, ERR_BUF_SIZE, "STT HTTP error (attempt %d): %s",
                 attempt, http.errorToString(code).c_str());
        Serial.printf("[Cloud] %s\n", _errMsg);
        http.end();
        if (attempt < 2) {
            Serial.println("[Cloud] Retrying STT in 1s...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (code <= 0) {
    } else if (code != 200) {
        String resp = http.getString();
        snprintf(_errMsg, ERR_BUF_SIZE, "STT HTTP %d: %.180s", code, resp.c_str());
    } else {
        // Read raw body first so we can log it on failure
        String rawBody = http.getString();
        Serial.printf("[Cloud] STT raw response: %.300s\n", rawBody.c_str());

        JsonDocument resp;
        DeserializationError err = deserializeJson(resp, rawBody);
        if (err) {
            snprintf(_errMsg, ERR_BUF_SIZE, "STT JSON: %s", err.c_str());
        } else {
            const char *text = resp["text"];
            if (text && strlen(text) > 0) {
                Serial.printf("[Cloud] STT transcript: %s\n", text);
                ok = true;
                // Save transcript for caller (ticker display)
                if (_transcript) strlcpy(_transcript, text, RESP_BUF_SIZE);
                // Copy transcript into _pendingMsg for _doChat
                size_t tlen = strlen(text);
                _pendingMsg = (char *)ps_malloc(tlen + 1);
                if (_pendingMsg) {
                    memcpy(_pendingMsg, text, tlen + 1);
                } else {
                    strlcpy(_errMsg, "STT: ps_malloc failed for transcript", ERR_BUF_SIZE);
                    ok = false;
                }
            } else {
                // Empty transcript (silence / unrecognised audio)
                Serial.println("[Cloud] STT: empty transcript — treating as silence");
                strlcpy(_errMsg, "STT: empty transcript", ERR_BUF_SIZE);
            }
        }
    }

    http.end();
    free(body);
    return ok;
}

// =============================================================
//  Worker tasks
// =============================================================
void CloudClient::_chatWorkerTask(void *param) {
    CloudClient *self = (CloudClient *)param;
    Serial.println("[Cloud] Chat worker started");

    bool ok = self->_doChat(self->_pendingMsg);
    self->_state = ok ? CLOUD_DONE : CLOUD_ERROR;

    if (self->_pendingMsg) {
        free(self->_pendingMsg);
        self->_pendingMsg = nullptr;
    }
    Serial.println("[Cloud] Chat worker done");
    vTaskDelete(nullptr);
}

void CloudClient::_sttWorkerTask(void *param) {
    CloudClient *self = (CloudClient *)param;
    Serial.println("[Cloud] STT worker started");

    // Clear Phase 5 fields at the start of every request
    self->_emotion[0]  = '\0';
    self->_audioUrl[0] = '\0';

#ifdef BACKEND_URL
    // ---- Phase 5: unified backend ----
    bool ok = self->_doBackendChat();

    if (self->_pendingPcm) {
        free(self->_pendingPcm);
        self->_pendingPcm   = nullptr;
        self->_pendingBytes = 0;
    }
    self->_state = ok ? CLOUD_DONE : CLOUD_ERROR;
    Serial.printf("[Cloud] Backend chat %s\n", ok ? "done" : "failed");

#else
    // ---- Phase 4: direct STT → LLM ----
    bool sttOk = self->_doStt();

    if (self->_pendingPcm) {
        free(self->_pendingPcm);
        self->_pendingPcm   = nullptr;
        self->_pendingBytes = 0;
    }

    if (!sttOk) {
        self->_state = CLOUD_ERROR;
        Serial.println("[Cloud] STT failed");
        vTaskDelete(nullptr);
        return;
    }

    self->_state = CLOUD_STT_DONE;
    Serial.println("[Cloud] STT done, sending to LLM...");
    bool chatOk = self->_doChat(self->_pendingMsg);
    self->_state = chatOk ? CLOUD_DONE : CLOUD_ERROR;

    if (self->_pendingMsg) {
        free(self->_pendingMsg);
        self->_pendingMsg = nullptr;
    }
    Serial.println("[Cloud] STT worker done");
#endif

    vTaskDelete(nullptr);
}

// =============================================================
//  Public: sendTTS
// =============================================================
bool CloudClient::sendTTS(const char *text) {
    if (_state == CLOUD_BUSY) return false;
    if (!text || text[0] == '\0') {
        strlcpy(_errMsg, "sendTTS: empty text", ERR_BUF_SIZE);
        _state = CLOUD_ERROR;
        return false;
    }

    size_t len  = strlen(text);
    _pendingMsg = (char *)ps_malloc(len + 1);
    if (!_pendingMsg) {
        strlcpy(_errMsg, "sendTTS: ps_malloc failed", ERR_BUF_SIZE);
        _state = CLOUD_ERROR;
        return false;
    }
    memcpy(_pendingMsg, text, len + 1);

    _state    = CLOUD_BUSY;
    _errMsg[0] = '\0';

    BaseType_t ok = xTaskCreatePinnedToCore(
        _ttsWorkerTask, "cloud_tts", TTS_WORKER_STACK, this, 1, nullptr, 1);
    if (ok != pdPASS) {
        _state = CLOUD_ERROR;
        strlcpy(_errMsg, "Failed to create TTS task", ERR_BUF_SIZE);
        free(_pendingMsg);
        _pendingMsg = nullptr;
        return false;
    }
    return true;
}

// =============================================================
//  Internal: TTS  (POST /audio/speech → WAV → es8311Play)
//  Phase 5: if _audioUrl is set, skip SiliconFlow and use backend URL directly.
// =============================================================
bool CloudClient::_doTts(const char *text) {
#ifdef BACKEND_URL
    if (_audioUrl[0] != '\0') {
        return _downloadAndPlayWav(_audioUrl);
    }
#endif

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    char url[256];
    snprintf(url, sizeof(url), "%s/audio/speech", _ttsApiBase);

    if (!http.begin(client, url)) {
        strlcpy(_errMsg, "TTS: HTTP begin failed", ERR_BUF_SIZE);
        return false;
    }

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", _ttsApiKey);
    http.addHeader("Authorization", authHeader);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["model"]           = _ttsModel;
    doc["input"]           = text;
    doc["voice"]           = _ttsVoice;
    doc["response_format"] = "wav";
    doc["sample_rate"]     = 16000;
    doc["stream"]          = false;
    doc["speed"]           = 1.0f;
    doc["gain"]            = 0.0f;

    String body;
    serializeJson(doc, body);

    Serial.printf("[Cloud] TTS POST %d bytes\n", body.length());
    int code = http.POST(body);

    bool ok = false;
    if (code <= 0) {
        snprintf(_errMsg, ERR_BUF_SIZE, "TTS HTTP error: %s",
                 http.errorToString(code).c_str());
        Serial.printf("[Cloud] TTS failed: %s\n", _errMsg);
    } else if (code != 200) {
        String resp = http.getString();
        snprintf(_errMsg, ERR_BUF_SIZE, "TTS HTTP %d: %.180s", code, resp.c_str());
        Serial.printf("[Cloud] TTS failed: %s\n", _errMsg);
    } else {
        int contentLen = http.getSize();   // -1 if chunked
        size_t allocLen = (contentLen > 0 && contentLen <= (int)TTS_MAX_BYTES)
                          ? (size_t)contentLen : TTS_MAX_BYTES;

        uint8_t *buf = (uint8_t *)heap_caps_malloc(allocLen,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            strlcpy(_errMsg, "TTS: ps_malloc failed for audio buf", ERR_BUF_SIZE);
        } else {
            // Stream response into buffer
            WiFiClient *stream = http.getStreamPtr();
            size_t totalRead = 0;
            uint32_t deadline = millis() + HTTP_TIMEOUT_MS;
            while (totalRead < allocLen && millis() < deadline) {
                int avail = stream->available();
                if (avail > 0) {
                    size_t toRead = min((size_t)avail, allocLen - totalRead);
                    size_t n = stream->readBytes(buf + totalRead, toRead);
                    totalRead += n;
                    if (!http.connected() && stream->available() == 0) break;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            }

            Serial.printf("[Cloud] TTS downloaded %u bytes (Content-Length=%d)\n",
                          (unsigned)totalRead, contentLen);

            if (totalRead < 44) {
                snprintf(_errMsg, ERR_BUF_SIZE, "TTS: response too small (%u bytes)",
                         (unsigned)totalRead);
                heap_caps_free(buf);
            } else {
#if TTS_DUMP_SERIAL
                // Hex dump to serial — capture log then run the Python snippet above
                // Dumps up to first 64KB (≈4s audio) to keep transmission time under 12s
                size_t dumpLen = totalRead < 65536 ? totalRead : 65536;
                Serial.printf("WAV_DUMP_BEGIN %u bytes\n", (unsigned)dumpLen);
                for (size_t i = 0; i < dumpLen; i++) {
                    Serial.printf("%02X", buf[i]);
                    if ((i & 31) == 31) Serial.println();
                }
                Serial.println("\nWAV_DUMP_END");
#endif
                es8311BoostForTTS(70);
                ok = es8311Play(buf, totalRead);
                es8311RestoreVolume();
                heap_caps_free(buf);
                if (!ok) strlcpy(_errMsg, "TTS: playback failed", ERR_BUF_SIZE);
            }
        }
    }

    http.end();
    return ok;
}

// =============================================================
//  TTS worker task
// =============================================================
void CloudClient::_ttsWorkerTask(void *param) {
    CloudClient *self = (CloudClient *)param;
    Serial.println("[Cloud] TTS worker started");

    bool ok = self->_doTts(self->_pendingMsg);

    if (self->_pendingMsg) {
        free(self->_pendingMsg);
        self->_pendingMsg = nullptr;
    }

    self->_state = ok ? CLOUD_TTS_DONE : CLOUD_ERROR;
    Serial.printf("[Cloud] TTS worker done (%s)\n", ok ? "OK" : "FAIL");
    vTaskDelete(nullptr);
}

#ifdef BACKEND_URL
// =============================================================
//  Phase 5: POST WAV → unified backend → parse text+emotion+audio_url
// =============================================================
bool CloudClient::_doBackendChat() {
    // Build WAV in PSRAM: 44-byte header + raw PCM
    size_t wavSize = 44 + _pendingBytes;
    uint8_t *wav = (uint8_t *)heap_caps_malloc(wavSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav) {
        strlcpy(_errMsg, "Backend: alloc WAV failed", ERR_BUF_SIZE);
        return false;
    }
    buildWavHeader(wav, (uint32_t)_pendingBytes);
    memcpy(wav + 44, _pendingPcm, _pendingBytes);

    WiFiClient client;   // plain HTTP
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    char url[128];
    snprintf(url, sizeof(url), "%s/chat", BACKEND_URL);

    Serial.printf("[Cloud] Backend POST %s (%u bytes WAV)\n", url, (unsigned)wavSize);

    if (!http.begin(client, url)) {
        strlcpy(_errMsg, "Backend: HTTP begin failed", ERR_BUF_SIZE);
        heap_caps_free(wav);
        return false;
    }

    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("X-Session-ID", WiFi.macAddress());

    int code = http.POST(wav, wavSize);
    heap_caps_free(wav);

    bool ok = false;
    if (code <= 0) {
        snprintf(_errMsg, ERR_BUF_SIZE, "Backend HTTP error: %s",
                 http.errorToString(code).c_str());
        Serial.printf("[Cloud] %s\n", _errMsg);
    } else if (code != 200) {
        String resp = http.getString();
        snprintf(_errMsg, ERR_BUF_SIZE, "Backend HTTP %d: %.180s", code, resp.c_str());
        Serial.printf("[Cloud] %s\n", _errMsg);
    } else {
        String rawBody = http.getString();
        Serial.printf("[Cloud] Backend response: %.200s\n", rawBody.c_str());

        JsonDocument resp;
        DeserializationError err = deserializeJson(resp, rawBody);
        if (err) {
            snprintf(_errMsg, ERR_BUF_SIZE, "Backend JSON: %s", err.c_str());
        } else {
            const char *textReply = resp["text_reply"];
            const char *emotion   = resp["emotion"];
            const char *audioUrl  = resp["audio_url"];

            if (textReply && textReply[0] != '\0') {
                strlcpy(_response,   textReply, RESP_BUF_SIZE);
                strlcpy(_transcript, textReply, RESP_BUF_SIZE);  // show in ticker
                ok = true;
            } else {
                strlcpy(_errMsg, "Backend: empty text_reply", ERR_BUF_SIZE);
            }
            if (emotion)  strlcpy(_emotion,  emotion,  sizeof(_emotion));
            if (audioUrl) strlcpy(_audioUrl, audioUrl, sizeof(_audioUrl));

            Serial.printf("[Cloud] Backend: text=%s emotion=%s\n",
                          _response, _emotion);
        }
    }

    http.end();
    return ok;
}

// =============================================================
//  Phase 5: GET audio_url → download WAV → es8311Play
// =============================================================
bool CloudClient::_downloadAndPlayWav(const char *url) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    Serial.printf("[Cloud] Downloading TTS WAV: %s\n", url);

    if (!http.begin(client, url)) {
        strlcpy(_errMsg, "TTS download: HTTP begin failed", ERR_BUF_SIZE);
        return false;
    }

    int code = http.GET();
    bool ok = false;

    if (code != 200) {
        snprintf(_errMsg, ERR_BUF_SIZE, "TTS download HTTP %d", code);
        Serial.printf("[Cloud] %s\n", _errMsg);
    } else {
        int contentLen = http.getSize();
        size_t allocLen = (contentLen > 0 && contentLen <= (int)TTS_MAX_BYTES)
                          ? (size_t)contentLen : TTS_MAX_BYTES;

        uint8_t *buf = (uint8_t *)heap_caps_malloc(allocLen,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            strlcpy(_errMsg, "TTS download: alloc failed", ERR_BUF_SIZE);
        } else {
            WiFiClient *stream = http.getStreamPtr();
            size_t totalRead = 0;
            uint32_t deadline = millis() + HTTP_TIMEOUT_MS;
            while (totalRead < allocLen && millis() < deadline) {
                int avail = stream->available();
                if (avail > 0) {
                    size_t n = stream->readBytes(buf + totalRead,
                                                  min((size_t)avail, allocLen - totalRead));
                    totalRead += n;
                    if (!http.connected() && stream->available() == 0) break;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            }
            Serial.printf("[Cloud] TTS WAV downloaded %u bytes\n", (unsigned)totalRead);

            if (totalRead >= 44) {
                es8311BoostForTTS(70);
                ok = es8311Play(buf, totalRead);
                es8311RestoreVolume();
                if (!ok) strlcpy(_errMsg, "TTS download: playback failed", ERR_BUF_SIZE);
            } else {
                snprintf(_errMsg, ERR_BUF_SIZE, "TTS download: too small (%u bytes)",
                         (unsigned)totalRead);
            }
            heap_caps_free(buf);
        }
    }

    http.end();
    return ok;
}
#endif  // BACKEND_URL
