#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_sleep.h>
#ifdef ESP_IDF_VERSION_MAJOR
    #include "esp_wifi.h"
    #include "esp_err.h"
#endif
#include "WebServer.h"
#include "Scale.h"
#include "WiFiManager.h"
#include "FlowRate.h"
#include "Calibration.h"
#include "BluetoothScale.h"
#include "TouchSensor.h"
#include "Display.h"
#include "PowerManager.h"
#include "BatteryMonitor.h"
#include "BoardConfig.h"
#include "Version.h"
#include "SmartSwitch.h"

// Board-specific pin configuration
uint8_t dataPin = HX711_DATA_PIN;     // HX711 Data pin
uint8_t clockPin = HX711_CLOCK_PIN;   // HX711 Clock pin  
uint8_t touchPin = TOUCH_TARE_PIN;    // Touch sensor for tare
uint8_t sleepTouchPin = TOUCH_SLEEP_PIN;  // Touch sensor for sleep functionality
uint8_t batteryPin = BATTERY_PIN;     // Battery voltage monitoring
uint8_t sdaPin = I2C_SDA_PIN;         // I2C Data pin for display
uint8_t sclPin = I2C_SCL_PIN;         // I2C Clock pin for display
float calibrationFactor = 4195.712891;
Scale scale(dataPin, clockPin, calibrationFactor);
FlowRate flowRate;
BluetoothScale bluetoothScale;
TouchSensor touchSensor(touchPin, &scale);
Display oledDisplay(sdaPin, sclPin, &scale, &flowRate);
PowerManager powerManager(sleepTouchPin, touchPin, &oledDisplay);
BatteryMonitor batteryMonitor(batteryPin);
SmartSwitch smartSwitch;

void setup() {
  Serial.begin(115200);
  
  // 80 MHz is the lowest stable frequency for WiFi + BLE on the ESP32-S3.
  // Dropping below this causes radio instability; higher values increase power draw with no benefit here.
  setCpuFrequencyMhz(80);
  Serial.printf("CPU frequency set to: %dMHz for power optimization\n", getCpuFrequencyMhz());
  
  // Version and board identification
  Serial.println("=================================");
  Serial.printf("WeighMyBru² v%s\n", WEIGHMYBRU_VERSION_STRING);
  Serial.printf("Board: %s\n", WEIGHMYBRU_BOARD_NAME);
  Serial.printf("Build: %s %s\n", WEIGHMYBRU_BUILD_DATE, WEIGHMYBRU_BUILD_TIME);
  Serial.printf("Full Version: %s\n", WEIGHMYBRU_FULL_VERSION);
  Serial.printf("Flash Size: %dMB\n", FLASH_SIZE_MB);
  Serial.printf("CPU Frequency: %dMHz (Power Optimized)\n", getCpuFrequencyMhz());
  Serial.println("=================================");
  
  // Link scale and flow rate for tare operation coordination
  scale.setFlowRatePtr(&flowRate);
  
  // Check for factory reset request (hold touch pin during boot)
  pinMode(touchPin, INPUT_PULLDOWN);
  if (digitalRead(touchPin) == HIGH) {
    Serial.println("FACTORY RESET: Touch pin held during boot - clearing WiFi credentials");
    clearWiFiCredentials();
    delay(1000);
  }
  
  // CRITICAL: Initialize BLE FIRST before WiFi to prevent radio conflicts
  Serial.println("Initializing BLE FIRST for GaggiMate compatibility...");
  Serial.printf("Free heap before BLE init: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM before BLE init: %u bytes\n", ESP.getFreePsram());
  
  try {
    bluetoothScale.begin();  // Initialize BLE without scale reference
    Serial.println("BLE initialized successfully - GaggiMate should be able to connect");
    Serial.printf("Free heap after BLE init: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM after BLE init: %u bytes\n", ESP.getFreePsram());
  } catch (...) {
    Serial.println("BLE initialization failed - continuing without Bluetooth");
    Serial.printf("Free heap after BLE fail: %u bytes\n", ESP.getFreeHeap());
  }
  
  // Initialize display with error handling - don't block if display fails
  Serial.println("Initializing display...");
  bool displayAvailable = oledDisplay.begin();
  
  if (!displayAvailable) {
    Serial.println("WARNING: Display initialization failed!");
    Serial.println("System will continue in headless mode without display.");
    Serial.println("All functionality remains available via web interface.");
  } else {
    Serial.println("Display initialized - ready for visual feedback");
    // Set reduced brightness for power optimization
    oledDisplay.setBrightness(128);  // 50% brightness vs 255 max
    Serial.println("Display brightness set to 50% for power optimization");
  }
  
  // Log wake-up reason (no delay needed — real init below keeps "Starting" on screen)
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:    Serial.println("Wakeup: external signal (touch)"); break;
    case ESP_SLEEP_WAKEUP_EXT1:    Serial.println("Wakeup: external signal (RTC_CNTL)"); break;
    case ESP_SLEEP_WAKEUP_TIMER:   Serial.println("Wakeup: timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:Serial.println("Wakeup: touchpad"); break;
    default: Serial.println("Cold boot (wakeup cause: " + String(wakeup_reason) + ")"); break;
  }

  // Reset WiFi hardware to clean state then start
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50); // minimal hardware settle
  WiFi.setSleep(true);
  setupWiFiForced();
  Serial.printf("Version: %s\n", ESP.getSdkVersion());
  // Initialize scale with error handling - don't block web server if HX711 fails
  Serial.println("Initializing scale...");
  if (!scale.begin()) {
    Serial.println("WARNING: Scale (HX711) initialization failed!");
    Serial.println("Web server will continue to run, but scale readings will not be available.");
    Serial.println("Check HX711 wiring and connections.");
  } else {
    Serial.println("Scale initialized successfully");
    // Now that scale is ready, set the reference in BluetoothScale
    bluetoothScale.setScale(&scale);
  }
  
  // BLE was initialized earlier - no need to initialize again
  // bluetoothScale.begin(&scale);
  
  // Set bluetooth reference in display for status indicator (if display available)
  if (oledDisplay.isConnected()) {
    oledDisplay.setBluetoothScale(&bluetoothScale);
  }
  
  // Set display reference in bluetooth for timer control
  bluetoothScale.setDisplay(&oledDisplay);
  
  // Set power manager reference in display for timer state synchronization (if display available)
  if (oledDisplay.isConnected()) {
    oledDisplay.setPowerManager(&powerManager);
  }
  
  // Set battery monitor reference in display for battery status (if display available)
  if (oledDisplay.isConnected()) {
    oledDisplay.setBatteryMonitor(&batteryMonitor);
  }

  // Initialize touch sensor
  touchSensor.begin();

  // Initialize power manager
  powerManager.begin();

  // Initialize battery monitor
  batteryMonitor.begin();

  // Check for low battery - prevent boot if voltage too low
  float batteryVoltage = batteryMonitor.getBatteryVoltage();
  if (batteryVoltage < 3.2f && batteryVoltage > 0.1f) { // 3.2V = BATTERY_EMPTY; > 0.1V to ignore missing/disconnected battery
    Serial.printf("CRITICAL: Battery voltage too low (%.2fV) - entering sleep\n", batteryVoltage);
    
    // Show battery low message on display with large, centered formatting
    if (oledDisplay.isConnected()) {
      oledDisplay.showBatteryLowMessage(batteryVoltage, 3000);
    }
    
    delay(3000); // Show message for 3 seconds
    
    // Force clear any display state and sleep immediately
    if (oledDisplay.isConnected()) {
      oledDisplay.clear();
    }
    
    Serial.println("Forcing deep sleep now...");
    esp_deep_sleep_start();
  }
  
  Serial.printf("Battery voltage OK (%.2fV) - continuing boot\n", batteryVoltage);

  // Show ready message once all hardware is initialised
  if (oledDisplay.isConnected()) {
    oledDisplay.showIPAddresses();
  }

  // Link display to touch sensor for tare feedback (if display available)
  if (oledDisplay.isConnected()) {
    touchSensor.setDisplay(&oledDisplay);
  }
  
  // Link flow rate to touch sensor for averaging reset on tare
  touchSensor.setFlowRate(&flowRate);

  // Tell tare sensor which pin is the sleep button so it can suppress
  // false tares caused by capacitive coupling between the adjacent pads
  touchSensor.setSleepPin(sleepTouchPin);

  smartSwitch.begin();
  // Ensure Shelly relay is ON at boot/wake — clears any stale postTriggerRelayOff
  // state and sends a best-effort ON command so the relay is in a known state.
  smartSwitch.ensureRelayOn();
  setupWebServer(scale, flowRate, bluetoothScale, oledDisplay, batteryMonitor, powerManager, smartSwitch);
  
}

void loop() {
  static unsigned long lastWeightUpdate = 0;
  static unsigned long lastWiFiCheck = 0;
  static unsigned long lastDisplayUpdate = 0;
  
  // Update weight at reduced frequency for power optimization
  if (millis() - lastWeightUpdate >= 50) { // Reduced from 20ms to 50ms (20Hz from 50Hz)
    float weight = scale.getWeight();
    flowRate.update(weight);
    lastWeightUpdate = millis();

    // Reset inactivity timer on significant weight change (>0.5g)
    static float lastActivityWeight = 0.0f;
    if (fabs(weight - lastActivityWeight) > 0.5f) {
      powerManager.notifyActivity();
      lastActivityWeight = weight;
    }

    // Smart switch: reset brew state when timer returns to idle
    static bool prevBrewIdle = true;
    bool brewIdle = !oledDisplay.isTimerRunning() && !oledDisplay.isTimerPaused();
    if (brewIdle && !prevBrewIdle) {
      smartSwitch.resetForNewBrew();
    }
    prevBrewIdle = brewIdle;

    // Smart switch: check trigger every weight cycle.
    // Snapshot post-trigger state before update so we can detect the moment
    // the relay turns off and show a one-shot OLED prompt.
    bool wasPostTrigger = smartSwitch.isPostTriggerRelayOff();
    smartSwitch.update(
      weight,
      flowRate.getFlowRate(),
      oledDisplay.isTimerRunning() && !oledDisplay.isTimerPaused(),
      oledDisplay.isArmed(),
      oledDisplay.getDoseWeight(),
      oledDisplay.getTargetRatio()
    );
    if (!wasPostTrigger && smartSwitch.isPostTriggerRelayOff()) {
      // Relay just turned off — tell the user what to do next
      oledDisplay.showMessage("Relay off-Hold tare", 3000);
    }
  }
  
  static unsigned long lastBLEUpdate = 0;
  
  // Check WiFi status every 30 seconds for debugging
  if (millis() - lastWiFiCheck >= 30000) {
    printWiFiStatus();
    lastWiFiCheck = millis();
  }
  
  // Maintain WiFi AP stability
  maintainWiFi();
  checkPendingWiFiDisable();
  checkPendingShotSave(oledDisplay, scale);
  
  // Update Bluetooth less frequently to reduce BLE interference and power usage
  if (millis() - lastBLEUpdate >= 100) { // Reduced from 50ms to 100ms (10Hz from 20Hz)
    bluetoothScale.update();
    lastBLEUpdate = millis();
  }
  
  // Update touch sensor
  touchSensor.update();

  // Smart switch safety: re-enable relay only via deliberate hold-tare.
  // wasHoldTareCompleted() strobes true for exactly one loop iteration after
  // a hold-tare fully executes, giving us a clean one-shot check here.
  if (touchSensor.wasHoldTareCompleted() && smartSwitch.isPostTriggerRelayOff()) {
    if (!oledDisplay.isTimerRunning()) {
      if (!smartSwitch.reEnableRelay()) {
        oledDisplay.showMessage("Relay err-check WiFi", 3000);
      }
      // On success the normal inverted "Armed" screen was already shown by
      // the hold-tare itself — no extra message needed.
    } else {
      // Timer is still running (e.g. auto-stop hasn't fired yet after trigger)
      oledDisplay.showMessage("Wait-timer running", 2000);
    }
  }

  // Update power manager
  powerManager.update();
  
  // Update battery monitor
  batteryMonitor.update();
  
  // Update display less frequently for power saving
  if (millis() - lastDisplayUpdate >= 100) { // Reduced display refresh rate to 10Hz
    oledDisplay.update();
    lastDisplayUpdate = millis();
  }
  
  // Increased delay for better power efficiency while maintaining responsiveness
  delay(10); // Optimized delay: 10ms for good responsiveness with power savings
}
