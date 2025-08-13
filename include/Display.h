#ifndef DISPLAY_H
#define DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class Scale; // Forward declaration
class FlowRate; // Forward declaration
class BluetoothScale; // Forward declaration
class PowerManager; // Forward declaration

enum class ScaleMode {
    FLOW = 0,
    TIME = 1,
    AUTO = 2
};

class Display {
public:
    Display(uint8_t sdaPin, uint8_t sclPin, Scale* scale, FlowRate* flowRate);
    bool begin();
    bool isConnected() const { return displayConnected; } // Check if display is available
    void update();
    void showWeight(float weight);
    void showMessage(const String& message, int duration = 2000);
    void showSleepCountdown(int seconds); // Show sleep countdown in large format
    void showSleepMessage(); // Show initial sleep message with big/small text format
    void showGoingToSleepMessage(); // Show "Touch To / Wake Up" message like WeighMyBru Ready
    void showSleepCancelledMessage(); // Show "Sleep / Cancelled" message like WeighMyBru Ready
    void showTaringMessage(); // Show "Taring..." message like WeighMyBru Ready
    void showTaredMessage(); // Show "Tared!" message like WeighMyBru Ready
    void showAutoTaredMessage(); // Show "Auto Tared!" message like WeighMyBru Ready
    void showModeMessage(ScaleMode mode); // Show mode name like WeighMyBru Ready
    void showModeMessage(ScaleMode mode, int duration); // Show mode name with custom duration
    void clearMessageState(); // Clear message state to return to weight display
    void showIPAddresses(); // Show WiFi IP addresses
    void clear();
    void setBrightness(uint8_t brightness);
    
    // Bluetooth connection status
    void setBluetoothScale(BluetoothScale* bluetooth);
    
    // Power manager reference for timer state synchronization
    void setPowerManager(PowerManager* powerManager);
    
    // Mode management
    void setMode(ScaleMode mode);
    void setMode(ScaleMode mode, bool delayTare); // Overload with option to delay tare
    ScaleMode getMode() const;
    void nextMode();
    void nextMode(bool delayTare); // Overload for mode switching with delayed tare
    
    // Timer management for TIME mode
    void startTimer();
    void stopTimer();
    void resetTimer();
    bool isTimerRunning() const;
    float getTimerSeconds() const;
    unsigned long getElapsedTime() const; // Get current elapsed time in milliseconds
    
    // Auto mode timer management
    bool isAutoTimerActive() const; // Check if auto timer was started and is still active
    void stopAutoTimer(); // Stop auto timer specifically (for power button control)
    
    // Mode switching with delayed tare
    void completePendingModeTare(); // Complete mode tare after touch release
    
    // Auto mode functionality
    void checkAutoTare(float weight);
    void checkAutoTimer(float flowRate);
    void resetAutoSequence(); // Reset auto mode sequence when manually tared
    void completeTareOperation(); // Called after manual tare to set up proper timing
    
private:
    uint8_t sdaPin;
    uint8_t sclPin;
    Scale* scalePtr;
    FlowRate* flowRatePtr;
    BluetoothScale* bluetoothPtr;
    PowerManager* powerManagerPtr;
    Adafruit_SSD1306* display;
    bool displayConnected; // Track if display is actually connected
    
    static const uint8_t SCREEN_WIDTH = 128;
    static const uint8_t SCREEN_HEIGHT = 32;
    static const uint8_t OLED_RESET = -1; // Reset pin not used
    static const uint8_t SCREEN_ADDRESS = 0x3C; // Common I2C address for SSD1306
    
    unsigned long messageStartTime;
    int messageDuration; // Store the duration for each message
    bool showingMessage;
    String currentMessage;
    
    // Mode system
    ScaleMode currentMode;
    
    // Timer system for TIME mode
    unsigned long timerStartTime;
    unsigned long timerPausedTime;
    bool timerRunning;
    bool timerPaused;
    
    // Auto mode system
    float lastWeight;
    unsigned long lastWeightChangeTime;
    bool waitingForStabilization;
    float weightWhenChanged;
    unsigned long stabilizationStartTime;
    bool autoTareEnabled;
    float lastFlowRate;
    bool autoTimerStarted;
    unsigned long autoTareCompletedTime; // Track when auto-tare completed to prevent immediate timer start
    
    // Mode switching system
    bool pendingModeTare;
    unsigned long modeSwitchTime; // Track when mode was switched
    bool stabilizationMessageShown; // Track if we've shown the stabilization end message
    
    // Mode tare stabilization tracking
    bool waitingForModeTareStabilization;
    unsigned long modeTareStabilizationStart;
    float lastStableWeight;
    float fingerPressWeight; // Weight when finger was pressing (for release detection)
    bool fingerReleaseDetected; // Track if we've detected finger release
    
    void drawWeight(float weight);
    void showWeightWithTimer(float weight); // For TIME mode display
    void showWeightWithFlowAndTimer(float weight); // For AUTO mode display
    void setupDisplay();
    void drawBluetoothStatus(); // Draw Bluetooth connection status icon
};

#endif
