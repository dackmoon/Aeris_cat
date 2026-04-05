#include "cloud_client.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char *SYSTEM_PROMPT =
    "你是一只可爱的电子猫咪。用简短的一两句话回复，可以加上喵~等拟声词。";

static const int HTTP_TIMEOUT_MS = 15000;
static const int WORKER_STACK    = 16384;

void CloudClient::begin(const char *apiKey, const char *apiBase,
                         const char *model) {
    _apiKey  = apiKey;
    _apiBase = apiBase;
    _model   = model;
    _response = (char *)ps_malloc(RESP_BUF_SIZE);
    _errMsg   = (char *)ps_malloc(256);
    if (_response) _response[0] = '\0';
    if (_errMsg)   _errMsg[0]   = '\0';
}

bool CloudClient::sendChat(const char *message) {
    if (_state == CLOUD_BUSY) return false;
    if (!_response || !_errMsg) {
        Serial.println("[Cloud] Buffers not allocated — call begin() first");
        return false;
    }

    size_t len = strlen(message);
    _pendingMsg = (char *)ps_malloc(len + 1);
    if (!_pendingMsg) {
        Serial.println("[Cloud] ps_malloc failed for message");
        return false;
    }
    memcpy(_pendingMsg, message, len + 1);

    _state = CLOUD_BUSY;
    _response[0] = '\0';
    _errMsg[0]   = '\0';

    BaseType_t ok = xTaskCreatePinnedToCore(
        _workerTask, "cloud", WORKER_STACK, this, 1, nullptr, 0);
    if (ok != pdPASS) {
        _state = CLOUD_ERROR;
        strlcpy(_errMsg, "Failed to create worker task", 256);
        free(_pendingMsg);
        _pendingMsg = nullptr;
        return false;
    }
    return true;
}

CloudState CloudClient::poll() {
    return _state;
}

const char *CloudClient::getResponse() const {
    return _response;
}

const char *CloudClient::getError() const {
    return _errMsg;
}

void CloudClient::consume() {
    _state = CLOUD_IDLE;
}

void CloudClient::_workerTask(void *param) {
    CloudClient *self = (CloudClient *)param;

    Serial.println("[Cloud] Worker started");

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    char url[256];
    snprintf(url, sizeof(url), "%s/chat/completions", self->_apiBase);

    if (!http.begin(client, url)) {
        strlcpy(self->_errMsg, "HTTP begin failed", 256);
        self->_state = CLOUD_ERROR;
        goto cleanup;
    }

    {
        http.addHeader("Content-Type", "application/json");

        char authHeader[128];
        snprintf(authHeader, sizeof(authHeader), "Bearer %s", self->_apiKey);
        http.addHeader("Authorization", authHeader);

        JsonDocument doc;
        doc["model"] = self->_model;
        JsonArray messages = doc["messages"].to<JsonArray>();

        JsonObject sysMsg = messages.add<JsonObject>();
        sysMsg["role"]    = "system";
        sysMsg["content"] = SYSTEM_PROMPT;

        JsonObject userMsg = messages.add<JsonObject>();
        userMsg["role"]    = "user";
        userMsg["content"] = self->_pendingMsg;

        doc["max_tokens"] = 150;
        doc["temperature"] = 0.8;

        String body;
        serializeJson(doc, body);

        Serial.printf("[Cloud] POST %d bytes\n", body.length());

        int httpCode = http.POST(body);

        if (httpCode <= 0) {
            snprintf(self->_errMsg, 256, "HTTP error: %s",
                     http.errorToString(httpCode).c_str());
            self->_state = CLOUD_ERROR;
        } else if (httpCode != 200) {
            int sz = http.getSize();
            if (sz < 0 || sz > 512) sz = 512;
            String resp = http.getString();
            resp.remove(sz);   // truncate to guard against oversized error bodies
            snprintf(self->_errMsg, 256, "HTTP %d: %.200s",
                     httpCode, resp.c_str());
            self->_state = CLOUD_ERROR;
        } else {
            int sz = http.getSize();
            Serial.printf("[Cloud] Response %d bytes\n", sz);
            if (sz > 0 && sz > (int)(RESP_BUF_SIZE * 8)) {
                strlcpy(self->_errMsg, "Response too large", 256);
                self->_state = CLOUD_ERROR;
            } else {
                JsonDocument respDoc;
                DeserializationError err = deserializeJson(respDoc, http.getStream());
                if (err) {
                    snprintf(self->_errMsg, 256, "JSON parse: %s", err.c_str());
                    self->_state = CLOUD_ERROR;
                } else {
                    const char *content =
                        respDoc["choices"][0]["message"]["content"];
                    if (content) {
                        strlcpy(self->_response, content, RESP_BUF_SIZE);
                        self->_state = CLOUD_DONE;
                        Serial.printf("[Cloud] Reply: %s\n", self->_response);
                    } else {
                        strlcpy(self->_errMsg, "No content in response", 256);
                        self->_state = CLOUD_ERROR;
                    }
                }
            }
        }

        http.end();
    }

cleanup:
    if (self->_pendingMsg) {
        free(self->_pendingMsg);
        self->_pendingMsg = nullptr;
    }
    Serial.println("[Cloud] Worker done");
    vTaskDelete(nullptr);
}
