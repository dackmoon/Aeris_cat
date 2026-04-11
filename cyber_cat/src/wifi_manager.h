#pragma once
#include <WiFi.h>

struct WifiNetwork {
    const char *ssid;
    const char *pass;
};

// Multi-network init: scans and connects to the strongest known network.
// networks[] must stay valid for the lifetime of the program (use string literals).
void wifiInit(const WifiNetwork *networks, int count);

bool wifiConnected();
void wifiLoop();
