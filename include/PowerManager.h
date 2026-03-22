#ifndef POWERMANAGER_H
#define POWERMANAGER_H

#include <Arduino.h>
#include <esp_sleep.h>
#include <Preferences.h>

class Display; // Forward declaration

class PowerManager {
public:
    PowerManager(uint8_t sleepTouchPin, uint8_t tareTouchPin, Display* display = nullptr);
    void begin();
    void update();
    void enterDeepSleep();
    void setSleepTouchThreshold(uint16_t threshold);
    bool isSleepTouchPressed();
    void setDisplay(Display* display);
    
    // Timer control for TIME mode
    void handleTimerControl();   // Touch-driven cycling: STOPPED→RUNNING→PAUSED→STOPPED
    void startTimer();           // Explicit start/resume (web UI)
    void stopTimer();            // Explicit pause (web UI)
    void resetTimer();           // Explicit reset (web UI)
    void resetTimerState();      // Reset internal state enum without touching display
    void notifyActivity();  // Call on any user/weight activity to reset inactivity timer

    // Inactivity sleep settings
    void setInactivityEnabled(bool enabled);
    void setInactivityTimeout(unsigned long ms);
    bool getInactivityEnabled() const { return inactivityEnabled; }
    unsigned long getInactivityTimeout() const { return inactivityTimeout; }

private:
    uint8_t sleepTouchPin;
    uint8_t tareTouchPin;
    Display* displayPtr;
    uint16_t sleepTouchThreshold;
    bool lastSleepTouchState;
    unsigned long lastSleepTouchTime;
    unsigned long touchStartTime;
    unsigned long debounceDelay;
    unsigned long sleepCountdownStart;
    bool sleepCountdownActive;
    bool cancelledRecently;
    unsigned long cancelTime;
    unsigned long lastActivityTime;
    unsigned long inactivityTimeout; // ms
    bool inactivityEnabled;
    Preferences preferences;
    
    // Timer control state
    enum class TimerState {
        STOPPED = 0,
        RUNNING = 1,
        PAUSED = 2
    };
    TimerState timerState;
    unsigned long lastTimerControlTime;
    
    void handleSleepTouch();
    void showSleepCountdown(int seconds);

    static const unsigned long HOLD_SLEEP_MS = 3000; // hold for sleep
};

#endif
