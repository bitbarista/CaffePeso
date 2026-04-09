#pragma once
#include <Arduino.h>

// Download url over HTTPS and write to the Update flash partition.
// Uses esp_http_client (ESP-IDF native) to avoid Arduino SSL stack issues
// when BLE and WiFi are running concurrently.
// Updates *progress (0-100) as bytes arrive; sets *message on error.
bool otaStreamUpdate(const String& url, int updateType, int* progress, String* message);
