#include "wifi_manager.h"
#include <Arduino.h>

static const WifiNetwork *_networks    = nullptr;
static int                _count       = 0;
static int                _activeIdx   = -1;   // index of currently connected network
static bool               _connected   = false;
static uint32_t           _lastRetryMs = 0;
static const uint32_t     RETRY_INTERVAL_MS = 10000;

static void _onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        _connected = true;
        Serial.printf("[WiFi] Connected — SSID: %s  IP: %s\n",
                      WiFi.SSID().c_str(),
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

// Scan visible networks and return the index in _networks[] with the highest RSSI.
// Returns -1 if no known network is found.
static int _selectBestNetwork() {
    Serial.println("[WiFi] Scanning...");
    int found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
    if (found <= 0) {
        Serial.println("[WiFi] Scan: no networks found");
        WiFi.scanDelete();
        return -1;
    }

    int bestIdx  = -1;
    int bestRssi = -1000;

    for (int s = 0; s < found; s++) {
        String scannedSsid = WiFi.SSID(s);
        int    rssi        = WiFi.RSSI(s);
        for (int k = 0; k < _count; k++) {
            if (scannedSsid == _networks[k].ssid) {
                Serial.printf("[WiFi] Found %s  RSSI=%d\n",
                              _networks[k].ssid, rssi);
                if (rssi > bestRssi) {
                    bestRssi = rssi;
                    bestIdx  = k;
                }
                break;
            }
        }
    }

    WiFi.scanDelete();

    if (bestIdx < 0) {
        Serial.println("[WiFi] None of the configured networks visible");
    } else {
        Serial.printf("[WiFi] Best network: %s (RSSI=%d)\n",
                      _networks[bestIdx].ssid, bestRssi);
    }
    return bestIdx;
}

void wifiInit(const WifiNetwork *networks, int count) {
    _networks = networks;
    _count    = count;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(_onWiFiEvent);

    _activeIdx = _selectBestNetwork();
    if (_activeIdx < 0) {
        // Fall back to first configured network
        _activeIdx = 0;
        Serial.printf("[WiFi] Fallback — connecting to %s\n", _networks[0].ssid);
    }
    WiFi.begin(_networks[_activeIdx].ssid, _networks[_activeIdx].pass);
}

bool wifiConnected() {
    return _connected;
}

void wifiLoop() {
    if (_connected) return;

    uint32_t now = millis();
    if (now - _lastRetryMs < RETRY_INTERVAL_MS) return;
    _lastRetryMs = now;

    // Re-scan to pick the best available network on each retry cycle.
    int best = _selectBestNetwork();
    if (best < 0) best = _activeIdx;  // keep last known if nothing visible

    if (best != _activeIdx) {
        Serial.printf("[WiFi] Switching to better network: %s\n",
                      _networks[best].ssid);
        _activeIdx = best;
    } else {
        Serial.printf("[WiFi] Backstop reconnect to %s\n",
                      _networks[_activeIdx].ssid);
    }
    WiFi.begin(_networks[_activeIdx].ssid, _networks[_activeIdx].pass);
}
