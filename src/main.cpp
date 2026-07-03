#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <Preferences.h>
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
#include "Buzzer.h"

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
Buzzer buzzer(BUZZER_PIN, BUZZER_PIN_INV, BUZZER_LEDC_CHANNEL, BUZZER_RESONANT_HZ, BUZZER_DIFFERENTIAL);

// --- Crash diagnostics ------------------------------------------------------
// Captured at boot and persisted to NVS so the reason for an unexpected reset
// (e.g. a brownout while the buzzer plays) survives the *next* reset — important
// on the ESP32-S3 because opening the USB serial port itself triggers a reset,
// which would otherwise overwrite the live esp_reset_reason(). Surfaced over
// WiFi via /api/device/info so no USB connection is needed to read it.
esp_reset_reason_t g_lastReset = ESP_RST_UNKNOWN;
uint32_t           g_bootCount = 0;
String             g_resetHistory; // most-recent-first CSV of reset-reason codes

// HX711 stall self-heal logging — persisted to NVS so recoveries can be reviewed
// after the fact (e.g. after a shot session) without watching a live sweep.
uint32_t           g_hxRecovTotal = 0;  // lifetime recovery count across all boots
String             g_hxRecovLog;        // most-recent-first CSV of "boot#@<uptime>s" markers

const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External pin / USB-serial reset";
    case ESP_RST_SW:        return "Software reset";
    case ESP_RST_PANIC:     return "PANIC / exception (firmware bug)";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT - power sag (electrical)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "Unknown";
  }
}

static void captureResetReason() {
  g_lastReset = esp_reset_reason();
  Preferences diag;
  diag.begin("diag", false);
  g_bootCount = diag.getUInt("boots", 0) + 1;
  diag.putUInt("boots", g_bootCount);

  // Prepend this boot's reason to the history, keep at most 12 entries.
  String hist = diag.getString("rr", "");
  hist = String((int)g_lastReset) + (hist.length() ? "," + hist : "");
  int commas = 0;
  for (int i = 0; i < (int)hist.length(); ++i) {
    if (hist[i] == ',' && ++commas == 12) { hist = hist.substring(0, i); break; }
  }
  diag.putString("rr", hist);

  // Restore the persisted HX711 recovery log/total (same namespace).
  g_hxRecovTotal = diag.getUInt("hx_tot", 0);
  g_hxRecovLog   = diag.getString("hx_log", "");

  diag.end();
  g_resetHistory = hist;

  Serial.printf("[BOOT] #%u reset reason: %s (%d)\n",
                g_bootCount, resetReasonStr(g_lastReset), (int)g_lastReset);
}

// Detect HX711 self-heal recoveries (counted in Scale) and persist them to NVS so
// they survive reboots and can be reviewed later. Debounced to one NVS write per
// 5s so a burst of glitches can't wear flash or stall the loop.
static void logHx711Recoveries() {
  static uint32_t      lastSeen  = 0;
  static unsigned long lastFlush = 0;
  static bool          dirty     = false;

  uint32_t now = scale.getRecoveryCount();
  if (now > lastSeen) {
    g_hxRecovTotal += (now - lastSeen);
    lastSeen = now;
    // Prepend a marker: which boot + seconds into that boot the recovery happened.
    // (No RTC, so this is uptime-relative; boot# groups events per power cycle.)
    // TODO(future): once the scale has real time (NTP-over-WiFi, or a DS3231 RTC),
    // log a wall-clock timestamp here instead of millis()/1000 uptime.

    String ev = String(g_bootCount) + "@" + String(millis() / 1000) + "s";
    g_hxRecovLog = ev + (g_hxRecovLog.length() ? "," + g_hxRecovLog : "");
    int commas = 0;                          // keep the ring at 20 entries
    for (int i = 0; i < (int)g_hxRecovLog.length(); ++i) {
      if (g_hxRecovLog[i] == ',' && ++commas == 20) { g_hxRecovLog = g_hxRecovLog.substring(0, i); break; }
    }
    dirty = true;
  }

  if (dirty && millis() - lastFlush >= 5000) {
    Preferences diag;
    diag.begin("diag", false);
    diag.putUInt("hx_tot", g_hxRecovTotal);
    diag.putString("hx_log", g_hxRecovLog);
    diag.end();
    dirty = false;
    lastFlush = millis();
  }
}

void setup() {
  Serial.begin(115200);
  
  // 80 MHz is the lowest stable frequency for WiFi + BLE on the ESP32-S3.
  // Dropping below this causes radio instability; higher values increase power draw with no benefit here.
  setCpuFrequencyMhz(80);
  Serial.printf("CPU frequency set to: %dMHz for power optimization\n", getCpuFrequencyMhz());

  // Record why we (re)started before anything else can mask it.
  captureResetReason();

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

  // Initialize piezo sounder (LEDC tone). Harmless if no disc is connected.
  buzzer.begin();

  smartSwitch.begin();
  // Ensure Shelly relay is ON at boot/wake — clears any stale postTriggerRelayOff
  // state and sends a best-effort ON command so the relay is in a known state.
  smartSwitch.ensureRelayOn();
  setupWebServer(scale, flowRate, bluetoothScale, oledDisplay, batteryMonitor, powerManager, smartSwitch);

  // First-run guidance: only when no cup weight has ever been saved (genuine
  // first boot). setupWebServer() has now restored savedTareWeight from NVS, so
  // this check is reliable. Returning users skip straight to the live screen.
  if (oledDisplay.isConnected() && oledDisplay.getSavedTareWeight() < 5.0f) {
    oledDisplay.showFirstRunHint();
  }

  // Startup chime once everything is up.
  buzzer.trigger(BuzzerEvent::BootReady);
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

    // Reset inactivity timer on rapid weight change only (cup placement or active brew).
    // Thermal drift of the HX711 can exceed 0.5g/hour — checking flow rate (g/s)
    // distinguishes real events (>0.3 g/s) from slow drift (~0.0001 g/s).
    static float lastActivityWeight = 0.0f;
    if (fabs(weight - lastActivityWeight) > 0.5f) {
      if (fabsf(flowRate.getFlowRate()) > 0.3f) {
        powerManager.notifyActivity();
      }
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
      buzzer.trigger(BuzzerEvent::SmartSwitchFired);
    }

    // Target-yield beep — mirrors the OLED flash (Display.cpp uses the same
    // dose x ratio - 2g threshold). Fire once per brew; rearm when idle.
    static bool tyBeeped = false;
    if (brewIdle) {
      tyBeeped = false;
    } else if (!tyBeeped && oledDisplay.isTimerRunning() && !oledDisplay.isTimerPaused()) {
      float dose  = oledDisplay.getDoseWeight();
      float ratio = oledDisplay.getTargetRatio();
      if (dose > 0.5f && ratio > 0.0f) {
        float threshold = dose * ratio - 2.0f;
        if (threshold > 0.0f && weight >= threshold) {
          buzzer.trigger(BuzzerEvent::TargetYield);
          tyBeeped = true;
        }
      }
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
  checkPendingBleDeinit(bluetoothScale);
  
  // Update Bluetooth less frequently to reduce BLE interference and power usage
  if (millis() - lastBLEUpdate >= 100) { // Reduced from 50ms to 100ms (10Hz from 20Hz)
    bluetoothScale.update();
    lastBLEUpdate = millis();
  }
  
  // Update touch sensor
  touchSensor.update();

  // Drive the piezo pattern player (non-blocking).
  buzzer.update();

  // Persist any HX711 self-heal recoveries to NVS (debounced) for later review.
  logHx711Recoveries();

  // Tare beep — fires on every touch tare (tap or hold); strobe self-clears.
  if (touchSensor.wasTareCompleted()) buzzer.trigger(BuzzerEvent::Tare);

  // --- Audible event detection ---
  // Armed / auto-re-armed: poll the strobe set inside Display::arm(), not an
  // isArmed() edge — a re-arm while already armed (true->true) has no edge, and
  // an arm that completes within one loop iteration would be missed.
  if (oledDisplay.wasArmCompleted()) buzzer.trigger(BuzzerEvent::Armed);

  // BLE client connect / disconnect.
  static bool prevBle = false;
  bool bleNow = bluetoothScale.isConnected();
  if (bleNow && !prevBle)      buzzer.trigger(BuzzerEvent::BleConnected);
  else if (!bleNow && prevBle) buzzer.trigger(BuzzerEvent::BleDisconnected);
  prevBle = bleNow;

  // Battery entering low/critical (rising edge only, so it beeps once).
  static bool prevLow = false;
  bool lowNow = batteryMonitor.isLowBattery() || batteryMonitor.isCriticalBattery();
  if (lowNow && !prevLow) buzzer.trigger(BuzzerEvent::BatteryLow);
  prevLow = lowNow;

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
