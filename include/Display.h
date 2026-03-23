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
    void showGoingToSleepMessage();   // "Touch To / Wake Up" — shown just before deep sleep
    void showSleepCancelledMessage(); // "Sleep / Cancelled" — shown when sleep is interrupted
    void showTaringMessage();         // "Taring..." — shown at 500ms hold as "keep holding" cue
    void showReleaseMessage();        // "Release!" — shown at 1500ms hold, prompts user to release
    void showTaredMessage();          // "Scale / Tared!" — shown after successful tare
    void clearMessageState(); // Clear message state to return to weight display
    void showIPAddresses(); // Show IP address on boot (replaces "Ready" screen)
    void clear();
    void setBrightness(uint8_t brightness);
    void setWeightDecimals(int decimals) { weightDecimals = decimals; }
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
    float getSavedTareWeight() const         { return savedTareWeight; }
    void  setSavedTareWeight(float w)        { savedTareWeight = w; } // restore from NVS without arming
    void  setAutoReArmEnabled(bool en)       { autoReArmEnabled = en; }
    bool  getAutoReArmEnabled() const        { return autoReArmEnabled; }
    void  resetNegativeFlag()  { scaleWentNegative = false; }
    void  setTapTaredEmpty()   { tapTaredEmpty = true; scaleWentNegative = false; }  // tap-tare: disable direct re-arm path
    void  showArmedMessage();

    // Pre-infusion timing mode (timer starts immediately on arm, not on first drip)
    void setPreInfusionMode(bool en) { preInfusionMode = en; }
    bool getPreInfusionMode() const  { return preInfusionMode; }

    // Auto-stop on flow cessation
    void setAutoStopEnabled(bool en) { autoStopEnabled = en; }
    bool getAutoStopEnabled() const  { return autoStopEnabled; }

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
    Adafruit_SSD1306* display;
    bool displayConnected; // Track if display is actually connected
    int weightDecimals = 1; // Configurable decimal places (0, 1, or 2)

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

    // Auto-zero drift correction
    unsigned long autoZeroStableSince = 0;
    static const unsigned long AUTO_ZERO_STABLE_MS = 10000;

    // Brew ratio
    float doseWeight = 0.0f;

    // Target yield alert
    float targetRatio = 0.0f;
    bool  alertFired       = false;
    bool  alertFlashActive = false;
    unsigned long alertFlashStart = 0;
    static const unsigned long ALERT_FLASH_MS = 1000;

    // Armed auto-start
    bool  armedAutoStart   = false;
    bool  autoReArmEnabled = true;
    float savedTareWeight  = 0.0f;
    unsigned long armStartedAt = 0;
    unsigned long armWeightAboveThresholdSince = 0;
    bool  scaleWentNegative = false;   // set when weight < -5g; reset by arm()
    bool  tapTaredEmpty     = false;   // set by tap-tare; blocks case-1 (≈0g) until cup removed again
    unsigned long reArmStableSince  = 0;
    static constexpr float ARM_TRIGGER_THRESHOLD   = 1.0f;
    static constexpr float REARM_STABLE_WINDOW     = 5.0f;  // g — weight must be within ±5g of savedTareWeight
    static constexpr float REARM_DIRECT_WINDOW     = 20.0f; // g — direct re-arm: weight within ±20g of 0 after cup removal
    static const unsigned long ARM_SUSTAIN_MS      = 500;
    static const unsigned long ARM_TIMEOUT_MS      = 120000; // 2 minutes
    static const unsigned long REARM_STABLE_MS     = 200;   // weight must match for 200ms to re-arm

    // Pre-infusion timing mode
    bool preInfusionMode = false;

    // Auto-stop on flow cessation
    bool  autoStopEnabled               = false;
    bool  autoStopFlowWasActive         = false;
    unsigned long autoStopBelowThresholdSince = 0;
    static constexpr float AUTO_STOP_ACTIVE_THRESHOLD  = 1.0f;  // g/s — flow must exceed this to be "active"
    static constexpr float AUTO_STOP_CEASE_THRESHOLD   = 0.5f;  // g/s — flow below this is considered stopped
    static const unsigned long AUTO_STOP_SUSTAIN_MS    = 2000;  // flow must be below threshold for 2s
    static const unsigned long AUTO_STOP_MIN_BREW_MS   = 8000;  // don't auto-stop within first 8s of brew

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
    
    void showWeightWithFlowAndTimer(float weight); // Two-row layout: weight top, timer+flow/ratio bottom
    void setupDisplay();
    void drawBluetoothStatus(); // Draw Bluetooth connection status icon
    void drawBatteryStatus(); // Draw battery status with 3-segment indicator
};

#endif
