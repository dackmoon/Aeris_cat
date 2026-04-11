#pragma once
#include <WiFi.h>

void wifiInit(const char *ssid, const char *pass);
bool wifiConnected();
void wifiLoop();
