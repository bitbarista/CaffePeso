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
    
    // Set WiFi power to maximum for better AP visibility
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // Maximum power
    Serial.println("WiFi power set to maximum for better AP visibility");
    
    // Enable WiFi sleep mode for BLE coexistence (required when both WiFi and BLE are active)
    WiFi.setSleep(false); // Disable sleep for better AP reliability
    Serial.println("WiFi sleep disabled for better AP performance");
    
    // Always start with AP mode first for stable operation
    Serial.println("Starting AP mode...");
    WiFi.mode(WIFI_AP);
    delay(1000); // Ensure mode switch is stable
    
    // Configure AP with optimized settings for maximum visibility
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    
    // Force AP broadcast with maximum power and visibility settings
    bool apStarted = false;
    Serial.println("Starting AP with maximum visibility settings...");
    
    // Try channel 6 first (most common and widely supported)
    apStarted = WiFi.softAP(ap_ssid, ap_password, 6, false, 4); // Channel 6, broadcast SSID, max 4 clients
    
    if (apStarted) {
        Serial.println("AP started successfully on channel 6 with broadcast enabled");
    } else {
        Serial.println("Channel 6 failed, trying channel 1...");
        apStarted = WiFi.softAP(ap_ssid, ap_password, 1, false, 4); // Channel 1, broadcast SSID
        
        if (apStarted) {
            Serial.println("AP started successfully on channel 1 with broadcast enabled");
        } else {
            Serial.println("Channel 1 failed, trying default settings...");
            apStarted = WiFi.softAP(ap_ssid); // Simplest possible configuration
            if (apStarted) {
                Serial.println("AP started with default settings");
            }
        }
    }
    
    Serial.println("AP Started: " + String(apStarted ? "YES" : "NO"));
    Serial.println("AP IP: " + WiFi.softAPIP().toString());
    Serial.println("AP SSID: " + String(ap_ssid));
    Serial.println("AP MAC: " + WiFi.softAPmacAddress());
    
    // Enhanced AP diagnostics
    if (apStarted) {
        Serial.println("=== AP DIAGNOSTICS ===");
        Serial.printf("AP Channel: %d\n", WiFi.channel());
        Serial.printf("AP Gateway: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("AP Subnet: %s\n", WiFi.softAPSubnetMask().toString().c_str());
        Serial.printf("WiFi TX Power: %d dBm\n", WiFi.getTxPower());
        Serial.println("AP should now be visible as 'WeighMyBru-AP'");
        Serial.println("If still not visible, try:");
        Serial.println("1. Restart your phone/computer WiFi");
        Serial.println("2. Manually add network 'WeighMyBru-AP' (no password)");
        Serial.println("3. Check for 2.4GHz band support on your device");
        Serial.println("======================");
    } else {
        Serial.println("ERROR: AP failed to start - hardware or RF issue suspected");
    }
    
    // Start mDNS immediately for AP mode access
    if (MDNS.begin("weighmybru")) {
        Serial.println("mDNS responder started in AP mode");
        Serial.println("Access the scale at: http://weighmybru.local or http://192.168.4.1");
        MDNS.addService("http", "tcp", 80);
    }
    
    // Give AP more time to fully stabilize
    delay(2000);
    
    // Only try STA connection if credentials exist
    if (strlen(ssid) > 0) {
        Serial.println("Attempting STA connection to: " + String(ssid));
        Serial.println("NOTE: STA connection may cause temporary AP instability");
        
        // Switch to dual mode only after AP is fully stable
        WiFi.mode(WIFI_AP_STA);
        delay(1000); // Longer delay for stable mode switch
        
        // Restart AP after mode switch to ensure it stays active
        WiFi.softAP(ap_ssid, ap_password, 6, 0, 8);
        delay(500);
        
        startAttemptTime = millis();
        WiFi.begin(ssid, password);
        
        // Wait for connection with reduced timeout to minimize AP disruption
        int connectionAttempts = 0;
        const int maxAttempts = 10; // Reduced to 5 seconds max to minimize AP disruption
        
        while (WiFi.status() != WL_CONNECTED && connectionAttempts < maxAttempts) {
            delay(500);
            Serial.print(".");
            connectionAttempts++;
            
            // Check if connection is failing badly
            if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) {
                Serial.println("\nSTA connection failed - bad credentials detected");
                Serial.println("Clearing stored credentials and keeping AP-only mode");
                
                // Clear bad credentials
                clearWiFiCredentials();
                
                // Immediately return to AP-only mode for stability
                WiFi.disconnect(true);
                delay(500);
                WiFi.mode(WIFI_AP);
                delay(1000);
                
                // Restart AP to ensure it's stable
                WiFi.softAP(ap_ssid, ap_password, 6, 0, 8);
                Serial.println("AP mode restored after credential failure");
                break;
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConnected to STA with IP: " + WiFi.localIP().toString());
            Serial.println("Running in dual AP+STA mode - AP should remain accessible");
        } else {
            Serial.println("\nSTA connection timeout. Returning to AP-only mode for maximum stability.");
            // Return to AP-only mode for stability
            WiFi.disconnect(true);
            delay(500);
            WiFi.mode(WIFI_AP);
            delay(1000);
            WiFi.softAP(ap_ssid, ap_password, 6, 0, 8);
            Serial.println("AP-only mode restored for stability");
        }
    } else {
        Serial.println("No stored WiFi credentials. AP mode only.");
    }
    
    // Setup mDNS after WiFi is stable
    setupmDNS();
    
    Serial.println("WiFi setup complete.");
    Serial.println("=== CONNECTION INFO ===");
    Serial.println("AP SSID: " + String(ap_ssid));
    Serial.println("AP IP: 192.168.4.1 (always available)");
    Serial.println("Hostname: weighmybru.local");
    Serial.println("Max clients: 8");
    Serial.println("WiFi sleep: disabled");
    Serial.println("=======================");
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
        
        // Check if AP is still running
        if (WiFi.getMode() == WIFI_OFF || WiFi.getMode() == WIFI_STA) {
            Serial.println("WARNING: AP mode lost! Restarting AP...");
            WiFi.mode(WIFI_AP);
            delay(500);
            WiFi.softAP(ap_ssid, ap_password, 6, 0, 8);
            Serial.println("AP mode restored");
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
