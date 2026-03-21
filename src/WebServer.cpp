#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include "WebServer.h"
#include "Scale.h"
#include "WiFiManager.h"
#include <Preferences.h>
#include "FlowRate.h"
#include "Calibration.h"
#include "BluetoothScale.h"
#include "Version.h"

Preferences preferences;

// Deferred WiFi disable — set from async callback, actioned in loop() via checkPendingWiFiDisable()
static volatile bool wifiDisablePending = false;
static unsigned long wifiDisableTime = 0;

// Deferred restart — set after OTA completes, actioned in loop()
static volatile bool restartPending = false;
static unsigned long restartTime = 0;

// Cache for display settings to avoid repeated slow EEPROM reads
static int cachedDecimals = -1; // -1 indicates not cached yet
static unsigned long lastDecimalCacheTime = 0;
const unsigned long DECIMAL_CACHE_TIMEOUT = 300000; // 5 minutes cache timeout

int getCachedDecimals() {
    // Fast path - return immediately if already cached and recent
    if (cachedDecimals != -1 && (millis() - lastDecimalCacheTime < DECIMAL_CACHE_TIMEOUT)) {
        return cachedDecimals;
    }
    
    unsigned long startTime = millis();
    
    if (preferences.begin("display", false)) {
        cachedDecimals = preferences.getInt("decimals", 1);
        preferences.end();
        lastDecimalCacheTime = millis();
        Serial.printf("Display: OK in %lums\n", millis() - startTime);
    } else {
        cachedDecimals = 1; // Use default
        lastDecimalCacheTime = millis();
        Serial.println("Display: FAIL");
    }
    
    return cachedDecimals;
}

void setCachedDecimals(int decimals) {
    Serial.println("Saving decimal setting...");
    unsigned long startTime = millis();
    
    if (preferences.begin("display", false)) {
        preferences.putInt("decimals", decimals);
        preferences.end();
        cachedDecimals = decimals; // Update cache
        lastDecimalCacheTime = millis();
        Serial.printf("Decimal setting saved in %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Failed to save decimal setting to EEPROM");
    }
}

// Cache for display mode (0=standard, 1=weight-focus)
static int cachedDisplayMode = -1;

int getCachedDisplayMode() {
    if (cachedDisplayMode != -1) return cachedDisplayMode;
    if (preferences.begin("display", false)) {
        cachedDisplayMode = preferences.getInt("disp_mode", 0);
        preferences.end();
    } else {
        cachedDisplayMode = 0;
    }
    return cachedDisplayMode;
}

void setCachedDisplayMode(int mode) {
    if (preferences.begin("display", false)) {
        preferences.putInt("disp_mode", mode);
        preferences.end();
        cachedDisplayMode = mode;
    } else {
        Serial.println("ERROR: Failed to save display mode to NVS");
    }
}

// Dose weight — manually set by user, persisted so it survives reboots
static float cachedDoseWeight = 0.0f;
static bool  doseWeightCached = false;

float getCachedDoseWeight() {
    if (doseWeightCached) return cachedDoseWeight;
    if (preferences.begin("display", false)) {
        cachedDoseWeight = preferences.getFloat("dose_w", 0.0f);
        preferences.end();
    }
    doseWeightCached = true;
    return cachedDoseWeight;
}

void setCachedDoseWeight(float grams, Display& display) {
    cachedDoseWeight = grams;
    doseWeightCached = true;
    if (preferences.begin("display", false)) {
        preferences.putFloat("dose_w", grams);
        preferences.end();
    }
    display.setDoseWeight(grams);
}

// Auto-tare settings cache
static bool  cachedAutoTareEnabled   = false;
static float cachedAutoTareThreshold = 20.0f;
static bool  autoTareCached          = false;

void loadAutoTareSettings(Display& display) {
    if (autoTareCached) return;
    if (preferences.begin("display", false)) {
        cachedAutoTareEnabled   = preferences.getBool("at_en", false);
        cachedAutoTareThreshold = preferences.getFloat("at_thresh", 20.0f);
        preferences.end();
    }
    autoTareCached = true;
    display.setAutoTareEnabled(cachedAutoTareEnabled);
    display.setAutoTareThreshold(cachedAutoTareThreshold);
}

void saveAutoTareSettings(bool enabled, float threshold, Display& display) {
    cachedAutoTareEnabled   = enabled;
    cachedAutoTareThreshold = threshold;
    if (preferences.begin("display", false)) {
        preferences.putBool("at_en", enabled);
        preferences.putFloat("at_thresh", threshold);
        preferences.end();
    }
    display.setAutoTareEnabled(enabled);
    display.setAutoTareThreshold(threshold);
}

// Idle reset settings cache
static bool          cachedIdleResetEnabled = false;
static unsigned long cachedIdleResetTimeout = 30000UL;
static bool          idleResetCached        = false;

void loadIdleResetSettings(Display& display) {
    if (idleResetCached) return;
    if (preferences.begin("display", false)) {
        cachedIdleResetEnabled = preferences.getBool("ir_en", false);
        cachedIdleResetTimeout = preferences.getULong("ir_timeout", 30000UL);
        preferences.end();
    }
    idleResetCached = true;
    display.setIdleResetEnabled(cachedIdleResetEnabled);
    display.setIdleResetTimeout(cachedIdleResetTimeout);
}

void saveIdleResetSettings(bool enabled, unsigned long ms, Display& display) {
    cachedIdleResetEnabled = enabled;
    cachedIdleResetTimeout = ms;
    if (preferences.begin("display", false)) {
        preferences.putBool("ir_en", enabled);
        preferences.putULong("ir_timeout", ms);
        preferences.end();
    }
    display.setIdleResetEnabled(enabled);
    display.setIdleResetTimeout(ms);
}

// Target ratio — for target yield alert (0 = disabled)
static float cachedTargetRatio = 0.0f;
static bool  targetRatioCached = false;

float getCachedTargetRatio() {
    if (targetRatioCached) return cachedTargetRatio;
    if (preferences.begin("display", false)) {
        cachedTargetRatio = preferences.getFloat("target_r", 0.0f);
        preferences.end();
    }
    targetRatioCached = true;
    return cachedTargetRatio;
}

void setCachedTargetRatio(float ratio, Display& display) {
    cachedTargetRatio  = ratio;
    targetRatioCached  = true;
    if (preferences.begin("display", false)) {
        preferences.putFloat("target_r", ratio);
        preferences.end();
    }
    display.setTargetRatio(ratio);
}

// Saved tare weight — cup weight persisted for auto-re-arm
void loadSavedTareWeight(Display& display) {
    if (preferences.begin("display", false)) {
        float saved = preferences.getFloat("saved_tare", 0.0f);
        preferences.end();
        // Inject into Display so arm() comparisons work after reboot
        // (arm state itself is NOT persisted — user must hold-tare again after boot)
        if (saved > 5.0f) {
            // Directly set via a temporary arm call then disarm so savedTareWeight is populated
            display.arm(saved);
            display.disarm();
        }
    }
}

// Shot history — rolling buffer of last 10 shots in "shots" NVS namespace
static uint8_t  shotHead    = 0;   // Next write slot (0-9)
static uint8_t  shotCount   = 0;   // Shots stored so far (0-10)
static uint32_t shotCounter = 0;   // Ever-incrementing shot number
static bool     shotHistoryLoaded = false;

void loadShotHistory() {
    if (shotHistoryLoaded) return;
    Preferences p;
    if (p.begin("shots", false)) {
        shotHead    = p.getUChar("head",    0);
        shotCount   = p.getUChar("count",   0);
        shotCounter = p.getULong("counter", 0);
        p.end();
    }
    shotHistoryLoaded = true;
}

void saveShotToHistory(float dose, float yield, float timeSec, float ratio) {
    loadShotHistory();
    shotCounter++;
    char key[4];
    Preferences p;
    if (!p.begin("shots", false)) return;
    snprintf(key, sizeof(key), "d%d", shotHead); p.putFloat(key, dose);
    snprintf(key, sizeof(key), "y%d", shotHead); p.putFloat(key, yield);
    snprintf(key, sizeof(key), "t%d", shotHead); p.putFloat(key, timeSec);
    snprintf(key, sizeof(key), "r%d", shotHead); p.putFloat(key, ratio);
    snprintf(key, sizeof(key), "n%d", shotHead); p.putULong(key, shotCounter);
    shotHead = (shotHead + 1) % 10;
    if (shotCount < 10) shotCount++;
    p.putUChar("head",    shotHead);
    p.putUChar("count",   shotCount);
    p.putULong("counter", shotCounter);
    p.end();
    Serial.printf("Shot #%lu saved: dose=%.1f yield=%.1f time=%.1fs ratio=%.2f\n",
                  (unsigned long)shotCounter, dose, yield, timeSec, ratio);
}

String getShotHistoryJSON() {
    loadShotHistory();
    Preferences p;
    if (!p.begin("shots", true)) return "[]";
    String json = "[";
    char key[4];
    for (int i = 0; i < shotCount; i++) {
        uint8_t idx = (shotHead - 1 - i + 10) % 10;
        snprintf(key, sizeof(key), "d%d", idx); float dose    = p.getFloat(key, 0.0f);
        snprintf(key, sizeof(key), "y%d", idx); float yield   = p.getFloat(key, 0.0f);
        snprintf(key, sizeof(key), "t%d", idx); float timeSec = p.getFloat(key, 0.0f);
        snprintf(key, sizeof(key), "r%d", idx); float ratio   = p.getFloat(key, 0.0f);
        snprintf(key, sizeof(key), "n%d", idx); unsigned long num = p.getULong(key, 0);
        if (i > 0) json += ",";
        json += "{\"shot\":" + String(num) +
                ",\"dose\":"  + String(dose,    1) +
                ",\"yield\":" + String(yield,   1) +
                ",\"time\":"  + String(timeSec, 1) +
                ",\"ratio\":" + String(ratio,   2) + "}";
    }
    p.end();
    json += "]";
    return json;
}

void clearShotHistory() {
    shotHead = 0; shotCount = 0;
    shotHistoryLoaded = true;
    Preferences p;
    if (p.begin("shots", false)) { p.clear(); p.end(); }
    Serial.println("Shot history cleared");
}

// Called from loop() — detect timer stop and save shot
static bool prevTimerRunningForShot = false;

void checkPendingShotSave(Display& display, Scale& scale) {
    if (display.hasPendingShot()) {
        float dose    = display.getDoseWeight();
        float yield   = display.getLastBrewYield();
        float timeSec = display.getLastBrewTime();
        if (dose > 0.5f && yield > 5.0f && timeSec > 3.0f) {
            saveShotToHistory(dose, yield, timeSec, yield / dose);
        }
        display.clearPendingShot();
    }
}

void diagnoseEEPROMPerformance() {
    Serial.println("=== EEPROM Performance Diagnostics ===");
    
    // Test WiFi preferences
    unsigned long startTime = millis();
    Preferences testPrefs;
    if (testPrefs.begin("test", false)) {
        testPrefs.putInt("testkey", 42);
        int val = testPrefs.getInt("testkey", 0);
        testPrefs.end();
        Serial.printf("EEPROM test write/read took: %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Cannot open test preferences namespace");
    }
    
    // Test existing namespaces
    startTime = millis();
    if (testPrefs.begin("wifi", true)) {
        String ssid = testPrefs.getString("ssid", "");
        testPrefs.end();
        Serial.printf("WiFi namespace read took: %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Cannot open wifi preferences namespace");
    }
    
    startTime = millis();
    if (testPrefs.begin("display", true)) {
        int decimals = testPrefs.getInt("decimals", 1);
        testPrefs.end();
        Serial.printf("Display namespace read took: %lu ms\n", millis() - startTime);
    } else {
        Serial.println("ERROR: Cannot open display preferences namespace");
    }
    
    Serial.println("=== End Diagnostics ===");
}

AsyncWebServer server(80);

/*
 * API Endpoints for External Brewing Systems (e.g., GaggiMate):
 * 
 * Ultra-fast weight reading (minimal latency):
 * GET /api/brew/weight
 * Response: "45.2" (weight in grams, 1 decimal)
 * 
 * Fast brewing status:
 * GET /api/brew/status  
 * Response: {"w":45.2,"f":2.1} (weight and flowrate)
 * 
 * Standard dashboard:
 * GET /api/dashboard
 * Response: {"weight":45.23,"flowrate":2.15}
 */

void setupWebServer(Scale &scale, FlowRate &flowRate, BluetoothScale &bluetoothScale, Display &display, BatteryMonitor &battery, PowerManager &powerManager) {
  if (!LittleFS.begin()) {
    Serial.println();
    Serial.println("=====================================");
    Serial.println("FILESYSTEM NOT FOUND!");
    Serial.println("=====================================");
    Serial.println("The LittleFS filesystem failed to mount.");
    Serial.println("This means the web interface files are missing.");
    Serial.println();
    Serial.println("To fix this, please run:");
    Serial.println("  pio run -t uploadfs");
    Serial.println();
    Serial.println("Or in PlatformIO IDE:");
    Serial.println("  Project Tasks → Platform → Upload Filesystem Image");
    Serial.println();
    Serial.println("The scale will continue to work, but the web interface will be unavailable.");
    Serial.println("=====================================");
    Serial.println();
    return;
  }

  // Run EEPROM diagnostics
  diagnoseEEPROMPerformance();

  // Pre-cache settings to avoid delays on first page load
  Serial.println("Pre-caching settings for faster page loads...");
  getCachedDecimals();        // This will cache the decimal setting
  display.setWeightDecimals(getCachedDecimals()); // Push cached value to display
  getCachedDisplayMode();     // Cache display mode
  display.setDisplayMode(getCachedDisplayMode()); // Push cached value to display
  display.setDoseWeight(getCachedDoseWeight()); // Restore persisted dose weight
  loadAutoTareSettings(display);   // Cache and apply auto-tare settings
  loadIdleResetSettings(display);  // Cache and apply idle-reset settings
  getCachedTargetRatio();          // Cache target ratio
  display.setTargetRatio(getCachedTargetRatio()); // Push to display
  loadSavedTareWeight(display);    // Restore saved cup weight for auto-re-arm
  loadShotHistory();               // Pre-load shot history index
  getStoredSSID();            // This will cache WiFi credentials

  // Register API route first
  server.on("/api/dashboard", HTTP_GET, [&scale, &flowRate, &display, &battery, &bluetoothScale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"weight\":" + String(scale.getCurrentWeight(), 2) + ",";
    json += "\"flowrate\":" + String(flowRate.getFlowRate(), 1) + ",";
    json += "\"scale_connected\":" + String(scale.isHX711Connected() ? "true" : "false") + ",";
    json += "\"filter_state\":\"" + scale.getFilterState() + "\",";
    
    // Always show unified mode
    json += "\"mode\":\"UNIFIED\",";
    
    // Add timer information
    unsigned long elapsedTime = display.getElapsedTime();
    if (elapsedTime > 0 || display.isTimerRunning()) {
      unsigned long minutes = elapsedTime / 60000;
      unsigned long seconds = (elapsedTime % 60000) / 1000;
      unsigned long milliseconds = elapsedTime % 1000;
      json += "\"timer_running\":" + String(display.isTimerRunning() ? "true" : "false") + ",";
      json += "\"timer_elapsed\":" + String(elapsedTime) + ",";
      json += "\"timer_display\":\"" + String(minutes) + ":" + 
              (seconds < 10 ? "0" : "") + String(seconds) + "." + 
              (milliseconds < 100 ? (milliseconds < 10 ? "00" : "0") : "") + String(milliseconds) + "\",";
      
      // Add timer average flow rate
      if (flowRate.hasTimerAverage()) {
        json += "\"timer_avg_flowrate\":" + String(flowRate.getTimerAverageFlowRate(), 2);
      } else {
        json += "\"timer_avg_flowrate\":null";
      }
    } else {
      json += "\"timer_running\":false,";
      json += "\"timer_elapsed\":0,";
      json += "\"timer_display\":\"0:00.000\",";
      json += "\"timer_avg_flowrate\":null";
    }
    
    // Add battery information
    json += ",\"battery_voltage\":" + String(battery.getBatteryVoltage(), 2);
    json += ",\"battery_percentage\":" + String(battery.getBatteryPercentage());
    json += ",\"battery_status\":\"" + battery.getBatteryStatus() + "\"";
    json += ",\"battery_segments\":" + String(battery.getBatterySegments());
    json += ",\"battery_low\":" + String(battery.isLowBattery() ? "true" : "false");
    json += ",\"battery_critical\":" + String(battery.isCriticalBattery() ? "true" : "false");
    
    // Add signal strength information
    json += ",\"wifi_signal_strength\":" + String(getWiFiSignalStrength());
    json += ",\"wifi_signal_quality\":\"" + getWiFiSignalQuality() + "\"";
    json += ",\"bluetooth_connected\":" + String(bluetoothScale.isConnected() ? "true" : "false");
    json += ",\"bluetooth_signal_strength\":" + String(bluetoothScale.getBluetoothSignalStrength());
    
    // Add device version information
    json += ",\"device_version\":\"" + String(WEIGHMYBRU_VERSION_STRING) + "\"";
    json += ",\"device_board\":\"" + String(WEIGHMYBRU_BOARD_NAME) + "\"";
    json += ",\"device_build_date\":\"" + String(WEIGHMYBRU_BUILD_DATE) + "\"";
    json += ",\"device_full_version\":\"" + String(WEIGHMYBRU_FULL_VERSION) + "\"";
    json += ",\"dose_weight\":" + String(display.getDoseWeight(), 1);
    json += ",\"target_ratio\":" + String(display.getTargetRatio(), 2);
    json += ",\"armed\":" + String(display.isArmed() ? "true" : "false");

    json += "}";
    request->send(200, "application/json", json);
  });

  // Timer control endpoints — route through PowerManager to keep touch state in sync
  server.on("/api/timer/start", HTTP_POST, [&powerManager](AsyncWebServerRequest *request) {
    powerManager.startTimer();
    request->send(200, "text/plain", "Timer started");
  });

  server.on("/api/timer/stop", HTTP_POST, [&powerManager](AsyncWebServerRequest *request) {
    powerManager.stopTimer();
    request->send(200, "text/plain", "Timer stopped");
  });

  server.on("/api/timer/reset", HTTP_POST, [&powerManager](AsyncWebServerRequest *request) {
    powerManager.resetTimer();
    request->send(200, "text/plain", "Timer reset");
  });

  server.on("/api/weight", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(scale.getCurrentWeight()));
  });

  // Lightweight weight-only endpoint for brewing applications
  server.on("/api/weight-fast", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    // Minimal processing for fastest response
    request->send(200, "text/plain", String(scale.getCurrentWeight(), 2));
  });

  // Brewing mode endpoints for external devices like GaggiMate
  server.on("/api/brew/weight", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    // Ultra-fast response for brewing systems
    float weight = scale.getCurrentWeight();
    request->send(200, "text/plain", String(weight, 1)); // 1 decimal for speed
  });
  
  server.on("/api/brew/status", HTTP_GET, [&scale, &flowRate](AsyncWebServerRequest *request) {
    // Minimal JSON for brewing systems
    String json = "{\"w\":" + String(scale.getCurrentWeight(), 1) + 
                  ",\"f\":" + String(flowRate.getFlowRate(), 1) + "}";
    request->send(200, "application/json", json);
  });

  // Battery calibration endpoint
  server.on("/api/battery/calibrate", HTTP_GET, [&battery](AsyncWebServerRequest *request) {
    if (!request->hasParam("voltage")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing 'voltage' parameter. Use ?voltage=4.10\"}");
      return;
    }
    float actualVoltage = request->getParam("voltage")->value().toFloat();
    if (actualVoltage < 2.5f || actualVoltage > 5.0f) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Voltage must be between 2.5V and 5.0V\"}");
      return;
    }
    float beforeVoltage = battery.getBatteryVoltage();
    battery.calibrateVoltage(actualVoltage);
    float afterVoltage = battery.getBatteryVoltage();
    String json = "{";
    json += "\"status\":\"success\",";
    json += "\"before_voltage\":" + String(beforeVoltage, 3) + ",";
    json += "\"after_voltage\":" + String(afterVoltage, 3) + ",";
    json += "\"target_voltage\":" + String(actualVoltage, 3) + ",";
    json += "\"calibration_scale\":" + String(battery.getCalibrationScale(), 4);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Battery monitoring endpoint (general status)
  server.on("/api/battery", HTTP_GET, [&battery](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"voltage\":" + String(battery.getBatteryVoltage(), 3);
    json += ",\"percentage\":" + String(battery.getBatteryPercentage());
    json += ",\"status\":\"" + battery.getBatteryStatus() + "\"";
    json += ",\"segments\":" + String(battery.getBatterySegments());
    json += ",\"low_battery\":" + String(battery.isLowBattery() ? "true" : "false");
    json += ",\"critical_battery\":" + String(battery.isCriticalBattery() ? "true" : "false");
    json += ",\"charging\":" + String(battery.isCharging() ? "true" : "false");
    json += ",\"calibration_scale\":" + String(battery.getCalibrationScale(), 4);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Battery debug endpoint for troubleshooting
  server.on("/api/battery/debug", HTTP_GET, [&battery](AsyncWebServerRequest *request) {
    float rawMv = analogReadMilliVolts(battery.getBatteryPin()); // Use configured battery pin
    float dividedVoltage = (rawMv / 1000.0f) * 2.0f; // Apply voltage divider ratio

    String json = "{";
    json += "\"raw_mv\":" + String(rawMv, 1) + ",";
    json += "\"divided_voltage\":" + String(dividedVoltage, 3) + ",";
    json += "\"voltage\":" + String(battery.getBatteryVoltage(), 3) + ",";
    json += "\"percentage\":" + String(battery.getBatteryPercentage());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/tare", HTTP_POST, [&scale, &powerManager, &flowRate](AsyncWebServerRequest *request){
    scale.tare(20);

    // Reset timer through PowerManager to keep touch state in sync
    powerManager.resetTimer();

    // Reset flow rate averaging for fresh brew measurement
    flowRate.resetTimerAveraging();

    request->send(200, "text/plain", "Scale tared! Timer and flow rate reset for fresh brew.");
  });

  server.on("/api/set-calibrationfactor", HTTP_POST, [&scale](AsyncWebServerRequest *request){
  if (request->hasParam("calibrationfactor", true)) {
    String value = request->getParam("calibrationfactor", true)->value();
    float calibrationFactor = value.toFloat();
    Serial.printf("Updated calibration factor weight: %.2f\n", calibrationFactor);
    request->send(200, "text/plain", "Calibration factor updated to " + value);
    scale.set_scale(calibrationFactor); // Assuming you have a method to set the calibration factor in Scale class
  } else {
    request->send(400, "text/plain", "Missing 'calibrationfactor' parameter");
  }
});

  server.on("/api/calibrate", HTTP_POST, [&scale](AsyncWebServerRequest *request){
    if (request->hasParam("knownWeight", true)) {
      String value = request->getParam("knownWeight", true)->value();
      float knownWeight = value.toFloat();
      // Read raw value from the scale (uncalibrated)
      long raw = scale.getRawValue();
      if (knownWeight > 0 && raw != 0) {
        float newCalibrationFactor = (float)raw / knownWeight;
        scale.set_scale(newCalibrationFactor);
        Serial.printf("Calibration complete. New factor: %.6f\n", newCalibrationFactor);
        request->send(200, "text/plain", "Scale calibrated! New factor: " + String(newCalibrationFactor, 6));
      } else {
        request->send(400, "text/plain", "Invalid known weight or scale reading");
      }
    } else {
      request->send(400, "text/plain", "Missing 'knownWeight' parameter");
    }
  });

  server.on("/api/calibrationfactor", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(scale.getCalibrationFactor(), 6));
  });

  // Scale connection status endpoint
  server.on("/api/scale/status", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"connected\":" + String(scale.isHX711Connected() ? "true" : "false") + ",";
    json += "\"weight\":" + String(scale.getCurrentWeight(), 2) + ",";
    json += "\"raw_value\":" + String(scale.getRawValue()) + ",";
    json += "\"calibration_factor\":" + String(scale.getCalibrationFactor(), 6);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/wifi-creds", HTTP_GET, [](AsyncWebServerRequest *request) {
    String ssid = getStoredSSID();
    String password = getStoredPassword();
    String json = "{\"ssid\":\"" + ssid + "\",\"password\":\"" + password + "\"}";
    request->send(200, "application/json", json);
  });

  server.on("/api/wifi-creds", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();
      
      Serial.println("New WiFi credentials received via web interface");
      
      // Save credentials first
      saveWiFiCredentials(ssid.c_str(), password.c_str());
      
      // Attempt immediate STA connection to avoid needing a reboot
      bool connected = attemptSTAConnection(ssid.c_str(), password.c_str());
      
      if (connected) {
        request->send(200, "application/json", 
          "{\"status\":\"success\",\"message\":\"Connected successfully! AP mode disabled for power savings.\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
      } else {
        // Connection failed - switch back to AP mode
        switchToAPMode();
        request->send(200, "application/json", 
          "{\"status\":\"failed\",\"message\":\"Connection failed. Check credentials and try again. AP mode restored.\"}");
      }
    } else {
      request->send(400, "text/plain", "Missing SSID or password");
    }
  });

  server.on("/api/wifi-creds", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    clearWiFiCredentials();
    request->send(200, "text/plain", "WiFi credentials cleared. Reboot to apply changes.");
  });

  // WiFi Power Management endpoints
  server.on("/api/wifi-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"enabled\":" + String(isWiFiEnabled() ? "true" : "false") + ",";
    json += "\"connected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false");
    if (WiFi.status() == WL_CONNECTED) {
      json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    }
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/wifi-toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool currentlyEnabled = isWiFiEnabled() && WiFi.getMode() != WIFI_OFF;

    if (currentlyEnabled) {
      request->send(200, "text/plain", "WiFi disabled for battery saving. Device will be inaccessible until WiFi is re-enabled.");
      // Defer actual disable to loop() so the response can be fully transmitted first
      wifiDisablePending = true;
      wifiDisableTime = millis();
    } else {
      enableWiFi();
      request->send(200, "text/plain", "WiFi enabled");
    }
  });

  server.on("/api/wifi-enable", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("enabled", true)) {
      bool enabled = request->getParam("enabled", true)->value() == "true";
      if (enabled) {
        enableWiFi();
        request->send(200, "text/plain", "WiFi enabled");
      } else {
        request->send(200, "text/plain", "WiFi disabled for battery saving. Device will be inaccessible until WiFi is re-enabled.");
        // Defer actual disable to loop() so the response can be fully transmitted first
        wifiDisablePending = true;
        wifiDisableTime = millis();
      }
    } else {
      request->send(400, "text/plain", "Missing enabled parameter");
    }
  });

  // WiFi Network Scanning
  server.on("/api/wifi-scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    String scanResult = scanWiFiNetworks();
    request->send(200, "application/json", scanResult);
  });

  // Device information endpoint
  server.on("/api/device/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"version\":\"" + String(WEIGHMYBRU_VERSION_STRING) + "\",";
    json += "\"full_version\":\"" + String(WEIGHMYBRU_FULL_VERSION) + "\",";
    json += "\"board\":\"" + String(WEIGHMYBRU_BOARD_NAME) + "\",";
    json += "\"build_date\":\"" + String(WEIGHMYBRU_BUILD_DATE) + "\",";
    json += "\"build_time\":\"" + String(WEIGHMYBRU_BUILD_TIME) + "\",";
    json += "\"firmware_size\":" + String(ESP.getSketchSize()) + ",";
    json += "\"free_space\":" + String(ESP.getFreeSketchSpace()) + ",";
    json += "\"chip_model\":\"" + String(ESP.getChipModel()) + "\",";
    json += "\"chip_revision\":" + String(ESP.getChipRevision()) + ",";
    json += "\"cpu_frequency\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"flash_size\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"sdk_version\":\"" + String(ESP.getSdkVersion()) + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Signal strength endpoint for WiFi and Bluetooth monitoring
  server.on("/api/signal-strength", HTTP_GET, [&bluetoothScale](AsyncWebServerRequest *request) {
    String json = "{";
    
    // WiFi signal strength
    json += "\"wifi\":" + getWiFiConnectionInfo() + ",";
    
    // Bluetooth signal strength
    json += "\"bluetooth\":" + bluetoothScale.getBluetoothConnectionInfo();
    
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/decimal-setting", HTTP_GET, [](AsyncWebServerRequest *request) {
    int decimals = getCachedDecimals();
    String json = "{\"decimals\":" + String(decimals) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/decimal-setting", HTTP_POST, [&display](AsyncWebServerRequest *request) {
    if (request->hasParam("decimals", true)) {
      int decimals = request->getParam("decimals", true)->value().toInt();
      if (decimals < 0) decimals = 0;
      if (decimals > 2) decimals = 2;
      setCachedDecimals(decimals);
      display.setWeightDecimals(decimals); // Update display immediately
      request->send(200, "text/plain", "Decimal setting saved.");
    } else {
      request->send(400, "text/plain", "Missing decimals parameter");
    }
  });

  server.on("/api/display-mode", HTTP_GET, [](AsyncWebServerRequest *request) {
    int mode = getCachedDisplayMode();
    request->send(200, "application/json", "{\"mode\":" + String(mode) + "}");
  });

  server.on("/api/display-mode", HTTP_POST, [&display](AsyncWebServerRequest *request) {
    if (request->hasParam("mode", true)) {
      int mode = request->getParam("mode", true)->value().toInt();
      if (mode < 0) mode = 0;
      if (mode > 1) mode = 1;
      setCachedDisplayMode(mode);
      display.setDisplayMode(mode);
      request->send(200, "text/plain", "Display mode saved.");
    } else {
      request->send(400, "text/plain", "Missing mode parameter");
    }
  });

  server.on("/api/dose", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  "{\"dose_weight\":" + String(cachedDoseWeight, 1) + "}");
  });

  server.on("/api/dose", HTTP_POST, [&display](AsyncWebServerRequest *request) {
    if (request->hasParam("dose_weight", true)) {
      float grams = request->getParam("dose_weight", true)->value().toFloat();
      if (grams < 0.0f) grams = 0.0f;
      if (grams > 1000.0f) grams = 1000.0f;
      setCachedDoseWeight(grams, display);
      request->send(200, "text/plain", "Dose saved.");
    } else {
      request->send(400, "text/plain", "Missing dose_weight parameter");
    }
  });

  server.on("/api/auto-tare-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"enabled\":" + String(cachedAutoTareEnabled ? "true" : "false") +
                  ",\"threshold\":" + String(cachedAutoTareThreshold, 1) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/auto-tare-settings", HTTP_POST, [&display](AsyncWebServerRequest *request) {
    bool en = request->hasParam("enabled", true) &&
              request->getParam("enabled", true)->value() == "true";
    float thr = request->hasParam("threshold", true) ?
                request->getParam("threshold", true)->value().toFloat() : 20.0f;
    if (thr < 5.0f) thr = 5.0f;
    if (thr > 500.0f) thr = 500.0f;
    saveAutoTareSettings(en, thr, display);
    request->send(200, "text/plain", "Auto-tare settings saved.");
  });

  server.on("/api/idle-reset-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"enabled\":" + String(cachedIdleResetEnabled ? "true" : "false") +
                  ",\"timeoutSeconds\":" + String(cachedIdleResetTimeout / 1000UL) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/idle-reset-settings", HTTP_POST, [&display](AsyncWebServerRequest *request) {
    bool en = request->hasParam("enabled", true) &&
              request->getParam("enabled", true)->value() == "true";
    unsigned long secs = request->hasParam("timeoutSeconds", true) ?
                         (unsigned long)request->getParam("timeoutSeconds", true)->value().toInt() : 30UL;
    if (secs < 5) secs = 5;
    if (secs > 300) secs = 300;
    saveIdleResetSettings(en, secs * 1000UL, display);
    request->send(200, "text/plain", "Idle reset settings saved.");
  });

  server.on("/api/flowrate", HTTP_GET, [&flowRate](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(flowRate.getFlowRate(), 1));
  });

  // Bluetooth status API
  server.on("/api/bluetooth/status", HTTP_GET, [&bluetoothScale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"connected\":" + String(bluetoothScale.isConnected() ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Filter settings API endpoints
  server.on("/api/filter-settings", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"brewingThreshold\":" + String(scale.getBrewingThreshold(), 2) + ",";
    json += "\"stabilityTimeout\":" + String(scale.getStabilityTimeout()) + ",";
    json += "\"medianSamples\":" + String(scale.getMedianSamples()) + ",";
    json += "\"averageSamples\":" + String(scale.getAverageSamples());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/filter-settings", HTTP_POST, [&scale](AsyncWebServerRequest *request) {
    String response = "{\"status\":\"success\",\"message\":\"";
    bool updated = false;
    
    if (request->hasParam("brewingThreshold", true)) {
      float threshold = request->getParam("brewingThreshold", true)->value().toFloat();
      scale.setBrewingThreshold(threshold);
      response += "Brewing threshold updated. ";
      updated = true;
    }
    if (request->hasParam("stabilityTimeout", true)) {
      unsigned long timeout = request->getParam("stabilityTimeout", true)->value().toInt();
      scale.setStabilityTimeout(timeout);
      response += "Stability timeout updated. ";
      updated = true;
    }
    if (request->hasParam("medianSamples", true)) {
      int samples = request->getParam("medianSamples", true)->value().toInt();
      scale.setMedianSamples(samples);
      response += "Median samples updated. ";
      updated = true;
    }
    if (request->hasParam("averageSamples", true)) {
      int samples = request->getParam("averageSamples", true)->value().toInt();
      scale.setAverageSamples(samples);
      response += "Average samples updated. ";
      updated = true;
    }
    
    if (updated) {
      response += "\"}";
      request->send(200, "application/json", response);
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No valid parameters provided\"}");
    }
  });

  // Sleep timeout settings
  server.on("/api/sleep-settings", HTTP_GET, [&powerManager](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"enabled\":" + String(powerManager.getInactivityEnabled() ? "true" : "false") + ",";
    json += "\"timeoutMinutes\":" + String(powerManager.getInactivityTimeout() / 60000UL);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/sleep-settings", HTTP_POST, [&powerManager](AsyncWebServerRequest *request) {
    if (request->hasParam("enabled", true)) {
      bool enabled = request->getParam("enabled", true)->value() == "true";
      powerManager.setInactivityEnabled(enabled);
    }
    if (request->hasParam("timeoutMinutes", true)) {
      unsigned long minutes = request->getParam("timeoutMinutes", true)->value().toInt();
      minutes = constrain(minutes, 1, 60);
      powerManager.setInactivityTimeout(minutes * 60000UL);
    }
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });

  // Filter debug endpoint - shows current filter state
  server.on("/api/filter-debug", HTTP_GET, [&scale](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"filterState\":\"" + scale.getFilterState() + "\",";
    json += "\"brewingThreshold\":" + String(scale.getBrewingThreshold(), 2) + ",";
    json += "\"stabilityTimeout\":" + String(scale.getStabilityTimeout()) + ",";
    json += "\"medianSamples\":" + String(scale.getMedianSamples()) + ",";
    json += "\"averageSamples\":" + String(scale.getAverageSamples()) + ",";
    json += "\"currentWeight\":" + String(scale.getCurrentWeight(), 1);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Combined settings endpoint for faster loading
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Get WiFi credentials (from cache)
    String ssid = getStoredSSID();
    String password = getStoredPassword();
    
    // Get decimal setting (from cache)
    int decimals = getCachedDecimals();
    
    // Combine into single JSON response
    String json = "{";
    json += "\"ssid\":\"" + ssid + "\",";
    json += "\"password\":\"" + password + "\",";
    json += "\"decimals\":" + String(decimals);
    json += "}";
    
    request->send(200, "application/json", json);
  });

  // Emergency NVS reset endpoint (use with caution)
  server.on("/api/reset-nvs", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("confirm", true) && request->getParam("confirm", true)->value() == "yes") {
      Serial.println("Resetting NVS storage...");
      
      // Clear all preferences
      Preferences clearPrefs;
      clearPrefs.begin("wifi", false);
      clearPrefs.clear();
      clearPrefs.end();
      
      clearPrefs.begin("display", false);
      clearPrefs.clear();
      clearPrefs.end();
      
      clearPrefs.begin("scale", false);
      clearPrefs.clear();
      clearPrefs.end();
      
      request->send(200, "text/plain", "NVS storage reset. Device will restart in 3 seconds.");
      
      // Restart the ESP32 after a short delay
      delay(3000);
      ESP.restart();
    } else {
      request->send(400, "text/plain", "Missing confirmation parameter. Use 'confirm=yes' to reset NVS.");
    }
  });

  // Target ratio for yield alert
  server.on("/api/target-ratio", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  "{\"target_ratio\":" + String(cachedTargetRatio, 2) + "}");
  });

  server.on("/api/target-ratio", HTTP_POST, [&display](AsyncWebServerRequest *request) {
    if (request->hasParam("target_ratio", true)) {
      float ratio = request->getParam("target_ratio", true)->value().toFloat();
      if (ratio < 0.0f) ratio = 0.0f;
      if (ratio > 20.0f) ratio = 20.0f;
      setCachedTargetRatio(ratio, display);
      request->send(200, "text/plain", "Target ratio saved.");
    } else {
      request->send(400, "text/plain", "Missing target_ratio parameter");
    }
  });

  // Shot history
  server.on("/api/shot-history", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getShotHistoryJSON());
  });

  server.on("/api/shot-history", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    clearShotHistory();
    request->send(200, "text/plain", "Shot history cleared.");
  });

  // Serve static files for non-API paths
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // 404 Not Found handler for unmatched routes
  server.onNotFound([](AsyncWebServerRequest *request) {
    String path = request->url();
    // If the request is for an API endpoint that doesn't exist, return 404
    if (path.startsWith("/api/")) {
      request->send(404, "text/plain", "API endpoint not found");
      return;
    }
    // For all other unmatched paths, serve index.html (SPA fallback)
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Add explicit MIME type handlers for font files
  server.on("/css/all.min.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/css/all.min.css", "text/css");
  });
  
  server.on("/js/alpine.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/js/alpine.min.js", "application/javascript");
  });
  
  server.on("/webfonts/fa-solid-900.woff2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/webfonts/fa-solid-900.woff2", "font/woff2");
  });
  
  server.on("/webfonts/fa-regular-400.woff2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/webfonts/fa-regular-400.woff2", "font/woff2");
  });

  // OTA firmware upload endpoint
  server.on("/api/ota/firmware", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool success = !Update.hasError();
      request->send(200, "application/json",
        String("{\"status\":\"") + (success ? "success" : "error") +
        "\",\"message\":\"" + (success ? "Update complete, rebooting..." : "Update failed") + "\"}");
      if (success) {
        restartPending = true;
        restartTime = millis();
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        Serial.printf("OTA firmware start: %s (%u bytes)\n", filename.c_str(), request->contentLength());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Update.printError(Serial);
        }
      }
      if (Update.isRunning()) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }
      }
      if (final) {
        if (Update.end(true)) {
          Serial.printf("OTA firmware complete: %u bytes\n", index + len);
        } else {
          Update.printError(Serial);
        }
      }
    });

  // Calibration verification endpoint (checks accuracy with a second known weight)
  server.on("/api/calibrate/verify", HTTP_POST, [&scale](AsyncWebServerRequest *request) {
    if (!request->hasParam("knownWeight", true)) {
      request->send(400, "text/plain", "Missing knownWeight");
      return;
    }
    float knownWeight = request->getParam("knownWeight", true)->value().toFloat();
    if (knownWeight <= 0) {
      request->send(400, "text/plain", "Known weight must be positive");
      return;
    }
    float measured = scale.getCurrentWeight();
    float error = measured - knownWeight;
    float errorPct = (error / knownWeight) * 100.0f;
    String json = "{";
    json += "\"known\":" + String(knownWeight, 2) + ",";
    json += "\"measured\":" + String(measured, 2) + ",";
    json += "\"error\":" + String(error, 2) + ",";
    json += "\"error_pct\":" + String(errorPct, 2);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Only start the web server if WiFi is enabled
  if (isWiFiEnabled()) {
    server.begin();
    Serial.println("Web server started - accessible via WiFi");
  } else {
    Serial.println("Web server NOT started - WiFi is disabled for battery saving");
  }
}

void startWebServer() {
  if (isWiFiEnabled()) {
    server.begin();
    Serial.println("Web server started");
  }
}

void stopWebServer() {
  server.end();
  Serial.println("Web server stopped");
}

void checkPendingWiFiDisable() {
  // Disable WiFi ~200ms after the response was queued, giving TCP time to flush
  if (wifiDisablePending && (millis() - wifiDisableTime >= 200)) {
    wifiDisablePending = false;
    disableWiFi();
  }
  // Restart after OTA ~500ms after response was queued
  if (restartPending && (millis() - restartTime >= 500)) {
    restartPending = false;
    Serial.println("OTA complete — restarting...");
    ESP.restart();
  }
}
