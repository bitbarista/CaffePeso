#include "WiFiManager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>

Preferences wifiPrefs;

// Station credentials
char stored_ssid[33] = {0};
char stored_password[65] = {0};

// Cache for WiFi credentials to avoid repeated slow EEPROM reads
static String cachedSSID = "";
static String cachedPassword = "";
static bool credentialsCached = false;
static unsigned long lastCacheTime = 0;
const unsigned long CACHE_TIMEOUT = 300000; // 5 minutes cache timeout

// AP credentials
const char* ap_ssid = "WeighMyBru-AP";
const char* ap_password = "";

unsigned long startAttemptTime = 0;
const unsigned long timeout = 10000; // 10 seconds

void saveWiFiCredentials(const char* ssid, const char* password) {
    Serial.println("Saving WiFi credentials...");
    unsigned long startTime = millis();
    
    if (wifiPrefs.begin("wifi", false)) {
        wifiPrefs.putString("ssid", ssid);
        wifiPrefs.putString("password", password);
        wifiPrefs.end();
        
        // Update cache immediately
        cachedSSID = String(ssid);
        cachedPassword = String(password);
        credentialsCached = true;
        lastCacheTime = millis();
        
        Serial.printf("WiFi credentials saved in %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Failed to open WiFi preferences for writing");
    }
}

void clearWiFiCredentials() {
    Serial.println("Clearing WiFi credentials...");
    if (wifiPrefs.begin("wifi", false)) {
        wifiPrefs.clear();
        wifiPrefs.end();
        
        // Clear cache
        cachedSSID = "";
        cachedPassword = "";
        credentialsCached = true;
        lastCacheTime = millis();
        
        Serial.println("WiFi credentials cleared");
    } else {
        Serial.println("ERROR: Failed to open WiFi preferences for clearing");
    }
}

bool loadWiFiCredentialsFromEEPROM() {
    // Check if cache is still valid first (no serial output for speed)
    if (credentialsCached && (millis() - lastCacheTime < CACHE_TIMEOUT)) {
        return true;
    }
    
    unsigned long startTime = millis();
    
    // Add timeout protection
    const unsigned long EEPROM_TIMEOUT = 5000; // 5 second timeout
    bool success = false;
    
    unsigned long attemptStart = millis();
    if (wifiPrefs.begin("wifi", true)) {
        // Check if operation is taking too long
        if (millis() - attemptStart > EEPROM_TIMEOUT) {
            wifiPrefs.end();
            cachedSSID = "";
            cachedPassword = "";
        } else {
            cachedSSID = wifiPrefs.getString("ssid", "");
            cachedPassword = wifiPrefs.getString("password", "");
            success = true;
        }
        wifiPrefs.end();
        
        credentialsCached = true;
        lastCacheTime = millis();
        
        // Minimal serial output to reduce blocking
        Serial.printf("WiFi: %s in %lums\n", success ? "OK" : "TIMEOUT", millis() - startTime);
        return success;
    } else {
        // Use empty defaults if EEPROM fails
        cachedSSID = "";
        cachedPassword = "";
        credentialsCached = true;
        lastCacheTime = millis();
        Serial.println("WiFi: EEPROM FAIL");
        return false;
    }
}

void loadWiFiCredentials(char* ssid, char* password, size_t maxLen) {
    loadWiFiCredentialsFromEEPROM();
    strncpy(ssid, cachedSSID.c_str(), maxLen - 1);
    strncpy(password, cachedPassword.c_str(), maxLen - 1);
    ssid[maxLen - 1] = '\0';
    password[maxLen - 1] = '\0';
}

String getStoredSSID() {
    // Fast path - return immediately if already cached and recent
    if (credentialsCached && (millis() - lastCacheTime < CACHE_TIMEOUT)) {
        return cachedSSID;
    }
    
    // Only do EEPROM read if cache is invalid
    loadWiFiCredentialsFromEEPROM();
    return cachedSSID;
}

String getStoredPassword() {
    // Fast path - return immediately if already cached and recent
    if (credentialsCached && (millis() - lastCacheTime < CACHE_TIMEOUT)) {
        return cachedPassword;
    }
    
    // Only do EEPROM read if cache is invalid
    loadWiFiCredentialsFromEEPROM();
    return cachedPassword;
}

void setupWiFi() {
    char ssid[33] = {0};
    char password[65] = {0};
    loadWiFiCredentials(ssid, password, sizeof(ssid));
    
    // Ensure WiFi is completely reset first
    Serial.println("Resetting WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500); // Longer delay for complete reset
    
    // Set WiFi power to maximum for better connectivity
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // Maximum power
    Serial.println("WiFi power set to maximum");
    
    // CRITICAL: Enable WiFi sleep mode for BLE coexistence (required when both WiFi and BLE are active)
    WiFi.setSleep(true); // MUST be true when BLE is active
    Serial.println("WiFi sleep enabled for BLE coexistence (required)");
    
    // Check if we have stored credentials - prioritize STA connection
    if (strlen(ssid) > 0) {
        Serial.println("Found stored credentials. Attempting STA connection first...");
        Serial.println("Connecting to: " + String(ssid));
        
        // Try STA mode first for lower power consumption
        WiFi.mode(WIFI_STA);
        delay(1000); // Ensure mode switch is stable
        
        startAttemptTime = millis();
        WiFi.begin(ssid, password);
        
        // Wait for connection with reasonable timeout
        int connectionAttempts = 0;
        const int maxAttempts = 20; // 10 seconds total
        
        while (WiFi.status() != WL_CONNECTED && connectionAttempts < maxAttempts) {
            delay(500);
            Serial.print(".");
            connectionAttempts++;
            
            // Check for immediate connection failures
            if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) {
                Serial.println("\nSTA connection failed - bad credentials or network unavailable");
                break;
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ STA connection successful!");
            Serial.println("Connected to: " + String(ssid));
            Serial.println("IP Address: " + WiFi.localIP().toString());
            Serial.println("Gateway: " + WiFi.gatewayIP().toString());
            Serial.println("DNS: " + WiFi.dnsIP().toString());
            Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
            
            // Setup mDNS for STA mode
            setupmDNS();
            
            Serial.println("⚡ Power optimized: AP mode disabled for lower consumption");
            return; // Exit early - we're connected via STA, no need for AP
        } else {
            Serial.println("\n❌ STA connection failed or timed out");
            Serial.println("Falling back to AP mode for configuration access");
        }
    } else {
        Serial.println("No stored WiFi credentials found. Starting AP mode for initial setup.");
    }
    
    // Fallback to AP mode if STA failed or no credentials exist
    Serial.println("Starting AP mode...");
    WiFi.mode(WIFI_AP);
    delay(1000); // Ensure mode switch is stable
    
    // Configure AP with optimized settings for maximum visibility
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    
    // Start AP with maximum power and visibility settings
    bool apStarted = false;
    Serial.println("Starting AP for credential configuration...");
    
    // Try channel 6 first (most common and widely supported)
    apStarted = WiFi.softAP(ap_ssid, ap_password, 6, false, 4); // Channel 6, broadcast SSID, max 4 clients
    
    if (apStarted) {
        Serial.println("✅ AP started successfully on channel 6");
    } else {
        Serial.println("Channel 6 failed, trying channel 1...");
        apStarted = WiFi.softAP(ap_ssid, ap_password, 1, false, 4); // Channel 1, broadcast SSID
        
        if (apStarted) {
            Serial.println("✅ AP started successfully on channel 1");
        } else {
            Serial.println("Channel 1 failed, trying default settings...");
            apStarted = WiFi.softAP(ap_ssid); // Simplest possible configuration
            if (apStarted) {
                Serial.println("✅ AP started with default settings");
            }
        }
    }
    
    if (apStarted) {
        Serial.println("=== AP MODE ACTIVE ===");
        Serial.println("AP SSID: " + String(ap_ssid));
        Serial.println("AP IP: " + WiFi.softAPIP().toString());
        Serial.println("AP MAC: " + WiFi.softAPmacAddress());
        Serial.printf("AP Channel: %d\n", WiFi.channel());
        Serial.printf("WiFi TX Power: %d dBm\n", WiFi.getTxPower());
        Serial.println("📱 Connect to 'WeighMyBru-AP' to configure WiFi");
        Serial.println("🌐 Access: http://192.168.4.1 or http://weighmybru.local");
        Serial.println("=====================");
        
        // Setup mDNS for AP mode
        setupmDNS();
    } else {
        Serial.println("❌ ERROR: AP failed to start - hardware or RF issue suspected");
    }
}

void setupmDNS() {
    // Start mDNS service with hostname "weighmybru"
    if (MDNS.begin("weighmybru")) {
        Serial.println("mDNS responder started/updated");
        Serial.println("Access the scale at: http://weighmybru.local");
        
        // Add service to MDNS-SD
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("websocket", "tcp", 81);
        
        // Add some useful service properties
        MDNS.addServiceTxt("http", "tcp", "device", "WeighMyBru Coffee Scale");
        MDNS.addServiceTxt("http", "tcp", "version", "2.0");
        
    } else {
        Serial.println("Error starting mDNS responder");
    }
}

void printWiFiStatus() {
    Serial.println("=== WiFi Status ===");
    Serial.println("WiFi Mode: " + String(WiFi.getMode()));
    Serial.println("AP Status: " + String(WiFi.softAPgetStationNum()) + " clients connected");
    Serial.println("AP IP: " + WiFi.softAPIP().toString());
    Serial.println("AP SSID: " + String(ap_ssid));
    Serial.println("STA Status: " + String(WiFi.status()));
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("STA IP: " + WiFi.localIP().toString());
        Serial.println("STA RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    Serial.println("WiFi Sleep: " + String(WiFi.getSleep() ? "ON" : "OFF"));
    Serial.println("==================");
}

void maintainWiFi() {
    static unsigned long lastMaintenance = 0;
    const unsigned long maintenanceInterval = 60000; // Every 60 seconds
    
    if (millis() - lastMaintenance >= maintenanceInterval) {
        lastMaintenance = millis();
        
        // Check current WiFi mode and connection health
        wifi_mode_t currentMode = WiFi.getMode();
        
        if (currentMode == WIFI_STA) {
            // We're in STA mode - check if connection is still healthy
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WARNING: STA connection lost! Checking reconnection...");
                
                // Try to reconnect to saved credentials
                char ssid[33] = {0};
                char password[65] = {0};
                loadWiFiCredentials(ssid, password, sizeof(ssid));
                
                if (strlen(ssid) > 0) {
                    Serial.println("Attempting to reconnect to: " + String(ssid));
                    WiFi.begin(ssid, password);
                    
                    // Wait briefly for reconnection
                    int attempts = 0;
                    while (WiFi.status() != WL_CONNECTED && attempts < 10) { // 5 second timeout
                        delay(500);
                        attempts++;
                    }
                    
                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.println("STA reconnection successful");
                    } else {
                        Serial.println("STA reconnection failed - switching to AP mode for user access");
                        switchToAPMode();
                    }
                } else {
                    Serial.println("No stored credentials - switching to AP mode");
                    switchToAPMode();
                }
            } else {
                Serial.println("STA mode healthy - connection maintained");
            }
        } else if (currentMode == WIFI_AP) {
            // We're in AP mode - just ensure it's still running properly
            if (WiFi.softAPgetStationNum() == 0) {
                Serial.println("AP mode active, no clients connected");
            } else {
                Serial.println("AP mode active, " + String(WiFi.softAPgetStationNum()) + " clients connected");
            }
        } else if (currentMode == WIFI_OFF) {
            Serial.println("WARNING: WiFi is OFF! This should not happen - restarting AP mode");
            switchToAPMode();
        }
        
        // Ensure WiFi sleep stays enabled for BLE coexistence
        if (!WiFi.getSleep()) {
            Serial.println("WARNING: WiFi sleep was disabled! Re-enabling for BLE coexistence...");
            WiFi.setSleep(true);
        }
        
        // Print status for debugging
        Serial.println("WiFi maintenance check completed");
    }
}

// Function to attempt STA connection with new credentials and switch from AP mode
bool attemptSTAConnection(const char* ssid, const char* password) {
    Serial.println("=== ATTEMPTING STA CONNECTION ===");
    Serial.println("SSID: " + String(ssid));
    Serial.println("Switching from AP mode to STA mode...");
    
    // Disconnect from AP mode but keep WiFi on
    WiFi.mode(WIFI_STA);
    delay(1000); // Allow mode switch to stabilize
    
    // Attempt connection with new credentials
    startAttemptTime = millis();
    WiFi.begin(ssid, password);
    
    // Wait for connection with reasonable timeout
    int connectionAttempts = 0;
    const int maxAttempts = 30; // 15 seconds total - more generous for initial connection
    
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED && connectionAttempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        connectionAttempts++;
        
        // Check for immediate connection failures
        if (WiFi.status() == WL_NO_SSID_AVAIL) {
            Serial.println("\n❌ SSID not found");
            return false;
        }
        if (WiFi.status() == WL_CONNECT_FAILED) {
            Serial.println("\n❌ Connection failed - likely wrong password");
            return false;
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n🎉 STA CONNECTION SUCCESSFUL!");
        Serial.println("✅ Connected to: " + String(ssid));
        Serial.println("📍 IP Address: " + WiFi.localIP().toString());
        Serial.println("🔗 Gateway: " + WiFi.gatewayIP().toString());
        Serial.println("📡 RSSI: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("⚡ AP mode disabled - power consumption optimized");
        
        // Setup mDNS for the new STA connection
        setupmDNS();
        
        return true;
    } else {
        Serial.println("\n❌ STA connection failed or timed out");
        Serial.println("Status code: " + String(WiFi.status()));
        return false;
    }
}

// Function to switch back to AP mode if STA connection fails
void switchToAPMode() {
    Serial.println("=== SWITCHING TO AP MODE ===");
    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_AP);
    delay(1000);
    
    // Restart AP with same settings as setupWiFi()
    bool apStarted = WiFi.softAP(ap_ssid, ap_password, 6, false, 4);
    
    if (apStarted) {
        Serial.println("✅ AP mode restored");
        Serial.println("📱 Connect to 'WeighMyBru-AP' to retry configuration");
        Serial.println("🌐 Access: http://192.168.4.1 or http://weighmybru.local");
        
        // Setup mDNS for AP mode
        setupmDNS();
    } else {
        Serial.println("❌ Failed to restart AP mode");
    }
}
