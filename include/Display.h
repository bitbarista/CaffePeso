#ifndef DISPLAY_H
#define DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class Scale; // Forward declaration
class FlowRate; // Forward declaration
class BluetoothScale; // Forward declaration
class PowerManager; // Forward declaration
class BatteryMonitor; // Forward declaration

class Display {
public:
    Display(uint8_t sdaPin, uint8_t sclPin, Scale* scale, FlowRate* flowRate);
    ~Display() { if (display) { delete display; display = nullptr; } }
    bool begin();
    bool isConnected() const { return displayConnected; } // Check if display is available
    void update();
    void showWeight(float weight);
    void showMessage(const String& message, int duration = 2000);
    void showBatteryLowMessage(float voltage, int duration = 3000);
    void showSleepCountdown(int seconds); // Show sleep countdown in large format
    void showSleepMessage(); // Show initial sleep message with big/small text format
    void showGoingToSleepMessage(); // Show "Touch To / Wake Up" message like WeighMyBru Ready
    void showSleepCancelledMessage(); // Show "Sleep / Cancelled" message like WeighMyBru Ready
    void showTaringMessage(); // Show "Taring..." message like WeighMyBru Ready
    void showTaredMessage(); // Show "Tared!" message like WeighMyBru Ready
    void showWiFiStatusMessage(bool isEnabled); // Show WiFi status message like WeighMyBru Ready
    void clearMessageState(); // Clear message state to return to weight display
    void showIPAddresses(); // Show startup ready message
    void showStatusPage(); // Show status page with battery, BLE, WiFi, and scale status
    void toggleStatusPage(); // Toggle between main display and status page
    void clear();
    void setBrightness(uint8_t brightness);
    void setWeightDecimals(int decimals) { weightDecimals = decimals; }
    void setDisplayMode(int mode) { displayMode = mode; }
    int  getDisplayMode() const   { return displayMode; }

    // Auto-tare on vessel placement
    void setAutoTareEnabled(bool en)       { autoTareEnabled = en; }
    void setAutoTareThreshold(float grams) { autoTareThreshold = grams; }
    bool getAutoTareEnabled() const        { return autoTareEnabled; }
    float getAutoTareThreshold() const     { return autoTareThreshold; }

    // Post-brew idle reset
    void setIdleResetEnabled(bool en)          { idleResetEnabled = en; }
    void setIdleResetTimeout(unsigned long ms) { idleResetTimeout = ms; }
    bool getIdleResetEnabled() const           { return idleResetEnabled; }
    unsigned long getIdleResetTimeout() const  { return idleResetTimeout; }

    // Brew ratio
    void  setDoseWeight(float grams) { doseWeight = grams; }
    float getDoseWeight() const      { return doseWeight; }

    // Target yield alert
    void  setTargetRatio(float ratio) { targetRatio = ratio; }
    float getTargetRatio() const      { return targetRatio; }

    // Armed auto-start (hold tare = tare + arm)
    void  arm(float cupWeightBeforeTare); // Set arm state and save cup weight to NVS
    void  disarm();
    bool  isArmed() const              { return armedAutoStart; }
    float getSavedTareWeight() const   { return savedTareWeight; }
    void  showArmedMessage();

    // Shot stats capture (for shot history)
    bool  hasPendingShot() const   { return pendingShot; }
    float getLastBrewYield() const { return lastBrewYield; }
    float getLastBrewTime()  const { return lastBrewTime; }
    void  clearPendingShot()       { pendingShot = false; }

    // Bluetooth connection status
    void setBluetoothScale(BluetoothScale* bluetooth);
    
    // Power manager reference for timer state synchronization
    void setPowerManager(PowerManager* powerManager);
    
    // Battery monitor reference for battery status display
    void setBatteryMonitor(BatteryMonitor* battery);
    
    // WiFi manager reference for network status display  
    void setWiFiManager(class WiFiManager* wifi);
    
    // Timer management
    void startTimer();
    void stopTimer();
    void resetTimer();
    bool isTimerRunning() const;
    bool isTimerPaused() const;
    float getTimerSeconds() const;
    unsigned long getElapsedTime() const; // Get current elapsed time in milliseconds
    
private:
    uint8_t sdaPin;
    uint8_t sclPin;
    Scale* scalePtr;
    FlowRate* flowRatePtr;
    BluetoothScale* bluetoothPtr;
    PowerManager* powerManagerPtr;
    BatteryMonitor* batteryPtr;
    class WiFiManager* wifiManagerPtr;
    Adafruit_SSD1306* display;
    bool displayConnected; // Track if display is actually connected
    int weightDecimals = 1; // Configurable decimal places (0, 1, or 2)
    int displayMode = 0;    // 0=standard split, 1=weight-focus (big weight when idle, auto-switches on flow)
    static constexpr float BREW_FLOW_THRESHOLD = 0.5f;    // g/s to trigger brew display
    static const unsigned long BREW_SUSTAIN_MS   = 2000; // flow must sustain this long before timer starts
    unsigned long flowAboveThresholdSince = 0;            // millis() when flow first exceeded threshold

    // Cup removal auto-stop
    static constexpr float CUP_REMOVAL_THRESHOLD = 15.0f; // g drop per 100ms update = >150 g/s
    float prevWeightForRemoval = 0.0f;

    // Auto-tare on vessel placement
    bool  autoTareEnabled         = false;
    float autoTareThreshold       = 20.0f;
    bool  autoTareFired           = false;
    unsigned long autoTareStableSince = 0;
    static const unsigned long AUTO_TARE_STABLE_MS = 600;

    // Post-brew idle reset
    bool          idleResetEnabled          = false;
    unsigned long idleResetTimeout          = 30000UL;
    unsigned long idleResetWeightStableFrom = 0;
    float         idleResetLastWeight       = 0.0f;

    // Brew ratio
    float doseWeight = 0.0f;

    // Target yield alert
    float targetRatio = 0.0f;
    bool  alertFired       = false;
    bool  alertFlashActive = false;
    unsigned long alertFlashStart = 0;
    static const unsigned long ALERT_FLASH_MS = 1000;

    // Armed auto-start
    bool  armedAutoStart  = false;
    float savedTareWeight = 0.0f;
    unsigned long armStartedAt = 0;
    unsigned long armWeightAboveThresholdSince = 0;
    static constexpr float ARM_TRIGGER_THRESHOLD = 1.0f;
    static const unsigned long ARM_SUSTAIN_MS    = 500;
    static const unsigned long ARM_TIMEOUT_MS    = 120000; // 2 minutes

    // Shot stats capture
    float currentWeightForCapture = 0.0f;
    bool  pendingShot   = false;
    float lastBrewYield = 0.0f;
    float lastBrewTime  = 0.0f;
    
    static const uint8_t SCREEN_WIDTH = 128;
    static const uint8_t SCREEN_HEIGHT = 32;
    static const uint8_t OLED_RESET = -1; // Reset pin not used
    static const uint8_t SCREEN_ADDRESS = 0x3C; // Common I2C address for SSD1306
    
    unsigned long messageStartTime;
    int messageDuration; // Store the duration for each message
    bool showingMessage;
    String currentMessage;
    
    // Timer system
    unsigned long timerStartTime;
    unsigned long timerPausedTime;
    bool timerRunning;
    bool timerPaused;
    float lastFlowRate; // Store last flow rate for comparison
    
    // Status page system
    bool showingStatusPage;
    unsigned long statusPageStartTime;
    static const unsigned long STATUS_PAGE_TIMEOUT = 10000; // 10 seconds timeout
    
    void drawWeight(float weight);
    void showWeightFull(float weight);             // Mode 1: weight fills full screen (timer stopped)
    void showWeightWithFlowAndTimer(float weight); // Main display showing weight, flow rate, and timer
    void setupDisplay();
    void drawBluetoothStatus(); // Draw Bluetooth connection status icon
    void drawBatteryStatus(); // Draw battery status with 3-segment indicator
};

#endif
