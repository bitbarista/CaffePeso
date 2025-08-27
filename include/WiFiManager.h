#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>

void setupWiFi();
void saveWiFiCredentials(const char* ssid, const char* password);
void clearWiFiCredentials(); // Clear stored WiFi credentials
void loadWiFiCredentials(char* ssid, char* password, size_t maxLen);
bool loadWiFiCredentialsFromEEPROM(); // Load and cache WiFi credentials from EEPROM
String getStoredSSID();
String getStoredPassword();
void setupmDNS(); // Setup mDNS for weighmybru.local hostname
void printWiFiStatus(); // Print detailed WiFi status for debugging
void maintainWiFi(); // Periodic WiFi maintenance to ensure AP stability
bool attemptSTAConnection(const char* ssid, const char* password); // Attempt STA connection and switch from AP mode
void switchToAPMode(); // Switch back to AP mode if STA connection fails

#endif
