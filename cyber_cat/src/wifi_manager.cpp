#include "wifi_manager.h"
#include <Arduino.h>

static const char *_ssid = nullptr;
static const char *_pass = nullptr;
static bool        _connected = false;
static uint32_t    _lastRetryMs = 0;
static const uint32_t RETRY_INTERVAL_MS = 10000;

static void _onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        _connected = true;
        Serial.printf("[WiFi] Connected — IP: %s\n",
                      WiFi.localIP().toString().c_str());
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        _connected = false;
        Serial.println("[WiFi] Disconnected");
        break;
    default:
        break;
    }
}

void wifiInit(const char *ssid, const char *pass) {
    _ssid = ssid;
    _pass = pass;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(_onWiFiEvent);

    // Non-blocking: fire and forget — _onWiFiEvent updates _connected when ready
    Serial.printf("[WiFi] Connecting to %s (background) ...\n", ssid);
    WiFi.begin(ssid, pass);
}

bool wifiConnected() {
    return _connected;
}

void wifiLoop() {
    if (_connected) return;

    uint32_t now = millis();
    if (now - _lastRetryMs < RETRY_INTERVAL_MS) return;
    _lastRetryMs = now;

    // setAutoReconnect(true) handles normal reconnection automatically.
    // Only call begin() here as a backstop when WiFi status has stalled
    // (e.g. after a long outage). Do NOT call disconnect() first — that
    // would cancel the auto-reconnect that's already in progress.
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[WiFi] Backstop reconnect to %s\n", _ssid);
        WiFi.begin(_ssid, _pass);
    }
}
