#include "PowerManager.h"
#include "Display.h"

PowerManager::PowerManager(uint8_t sleepTouchPin, uint8_t tareTouchPin, Display* display)
    : sleepTouchPin(sleepTouchPin), tareTouchPin(tareTouchPin), displayPtr(display), sleepTouchThreshold(0),
      lastSleepTouchState(false), lastSleepTouchTime(0), touchStartTime(0),
      debounceDelay(50), sleepCountdownStart(0), sleepCountdownActive(false),
      cancelledRecently(false), cancelTime(0),
      lastActivityTime(0), inactivityTimeout(10UL * 60UL * 1000UL), inactivityEnabled(true),
      timerState(TimerState::STOPPED), lastTimerControlTime(0) {
}

void PowerManager::begin() {
    // Set up the pin as digital input with pull-down resistor for the digital touch sensor module
    // This prevents false triggers when no touch sensor is connected
    pinMode(sleepTouchPin, INPUT_PULLDOWN);
    
    // Wake from deep sleep on either touch button — EXT1 supports multiple pins.
    // Any pin going HIGH (touch sensor output) will trigger the wake.
    uint64_t wakePinMask = (1ULL << sleepTouchPin) | (1ULL << tareTouchPin);
    esp_sleep_enable_ext1_wakeup(wakePinMask, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    // Load persisted inactivity settings (false = read-write so namespace is created on first boot)
    preferences.begin("power", false);
    inactivityEnabled = preferences.getBool("sleep_en", true);
    inactivityTimeout = preferences.getULong("sleep_ms", 10UL * 60UL * 1000UL);
    preferences.end();
    Serial.printf("Sleep timeout: %s, %lums\n", inactivityEnabled ? "enabled" : "disabled", inactivityTimeout);

    lastActivityTime = millis();
    Serial.printf("Power Manager initialized. Wake pins: GPIO%d (sleep) + GPIO%d (tare)\n", sleepTouchPin, tareTouchPin);
    Serial.println("Using EXT1 wake-up — either touch button will wake the device");
}

void PowerManager::update() {
    bool currentSleepTouchState = isSleepTouchPressed();
    unsigned long currentTime = millis();
    
    // Clear recent cancellation flag after 1 second
    if (cancelledRecently && (currentTime - cancelTime) > 1000) {
        cancelledRecently = false;
    }
    
    // Handle sleep countdown
    if (sleepCountdownActive) {
        unsigned long elapsed = currentTime - sleepCountdownStart;
        
        if (elapsed < 4000) { // Total 4 seconds: 1 sec message + 3 sec countdown
            // Show countdown every second, but only after the initial message has been shown
            if (elapsed > 1500) { // Start countdown after 1.5 seconds
                int countdownElapsed = (elapsed - 1500) / 1000; // Countdown time since 1.5s mark
                int remainingSeconds = 3 - countdownElapsed;
                
                if (remainingSeconds > 0 && (elapsed - 1500) % 1000 < 100) {
                    showSleepCountdown(remainingSeconds);
                }
            }
        } else {
            // Countdown finished, go to sleep
            enterDeepSleep();
        }
    }
    
    // Handle touch state changes.
    // Asymmetric debounce: 50ms on press (responsive), 200ms on release (filters contact bounce).
    if (currentSleepTouchState != lastSleepTouchState) {
        unsigned long required = currentSleepTouchState ? 50UL : 200UL;
        if (currentTime - lastSleepTouchTime > required) {
            if (currentSleepTouchState) {
                // Touch started
                if (sleepCountdownActive) {
                    // Touch during countdown - cancel sleep
                    sleepCountdownActive = false;
                    cancelledRecently = true;
                    cancelTime = currentTime;
                    lastActivityTime = currentTime; // Reset inactivity timer on cancel
                    Serial.println("Sleep cancelled - touch pressed during countdown");
                    if (displayPtr != nullptr) {
                        displayPtr->showSleepCancelledMessage();
                    }
                } else if (!cancelledRecently) {
                    // Handle timer control
                    touchStartTime = currentTime;
                    lastActivityTime = currentTime; // Reset inactivity timer on touch
                    Serial.println("Timer control touch started");
                }
            } else {
                // Touch ended — fire action based on hold duration
                if (!sleepCountdownActive && !cancelledRecently) {
                    unsigned long held = currentTime - touchStartTime;
                    if (held >= HOLD_SLEEP_MS) {
                        Serial.println("Hold 3s release: sleep");
                        handleSleepTouch();
                    } else {
                        Serial.println("Tap: timer control");
                        handleTimerControl();
                    }
                }
            }
            lastSleepTouchState = currentSleepTouchState;
            lastSleepTouchTime = currentTime;
        }
    }
    
    // No during-hold actions — all levels fire on release

    // Inactivity timeout: sleep after no activity for inactivityTimeout ms
    if (inactivityEnabled && inactivityTimeout > 0 && !sleepCountdownActive && !currentSleepTouchState) {
        if (currentTime - lastActivityTime >= inactivityTimeout) {
            Serial.println("Inactivity timeout reached - starting sleep countdown");
            sleepCountdownActive = true;
            sleepCountdownStart = currentTime;
            lastActivityTime = currentTime; // Reset so a cancel gives a full timeout
            if (displayPtr != nullptr) {
                displayPtr->showSleepMessage();
            }
        }
    }
}

void PowerManager::enterDeepSleep() {
    Serial.println("Entering deep sleep mode...");
    
    if (displayPtr != nullptr) {
        displayPtr->clearMessageState(); // Clear countdown message state
        displayPtr->showGoingToSleepMessage();
        delay(2000);
        displayPtr->clear();
    }
    
    // Print wake-up configuration for debugging
    Serial.println("Wake-up configured for EXT0 on GPIO" + String(sleepTouchPin));
    Serial.println("Will wake when pin goes HIGH");
    
    // Flush serial output
    Serial.flush();
    
    // Enter deep sleep - will wake up on external signal
    esp_deep_sleep_start();
}

void PowerManager::setSleepTouchThreshold(uint16_t threshold) {
    sleepTouchThreshold = threshold;
    Serial.println("Sleep touch threshold set to: " + String(sleepTouchThreshold));
}

bool PowerManager::isSleepTouchPressed() {
    // For digital touch sensor modules, check if the pin is HIGH
    bool pressed = digitalRead(sleepTouchPin) == HIGH;
    
    // Debug: log unexpected HIGH readings when no sensor should be connected
    static unsigned long lastDebugTime = 0;
    if (pressed && millis() - lastDebugTime > 5000) { // Log every 5 seconds max
        Serial.println("DEBUG: Sleep touch pin GPIO" + String(sleepTouchPin) + " reading HIGH - check for floating pin or connected sensor");
        lastDebugTime = millis();
    }
    
    return pressed;
}

void PowerManager::setDisplay(Display* display) {
    displayPtr = display;
}

void PowerManager::handleSleepTouch() {
    sleepCountdownActive = true;
    sleepCountdownStart = millis();
    Serial.println("Hold 5s: starting sleep countdown");
    if (displayPtr != nullptr) {
        displayPtr->showSleepMessage();
    }
}

void PowerManager::showSleepCountdown(int seconds) {
    if (displayPtr != nullptr) {
        displayPtr->showSleepCountdown(seconds);
    }
}

void PowerManager::handleTimerControl() {
    if (displayPtr == nullptr) return;
    
    // Prevent rapid successive timer control actions (minimum 300ms between actions)
    unsigned long currentTime = millis();
    if (currentTime - lastTimerControlTime < 300) {
        Serial.println("Timer control ignored - too soon after last action");
        return;
    }
    
    lastTimerControlTime = currentTime;
    Serial.println("Timer control triggered");
    
    // Unified mode timer control
    switch (timerState) {
        case TimerState::STOPPED:
            // First tap - start timer
            displayPtr->startTimer();
            timerState = TimerState::RUNNING;
            Serial.println("Timer started");
            break;
            
        case TimerState::RUNNING:
            // Second tap - stop/pause timer
            displayPtr->stopTimer();
            timerState = TimerState::PAUSED;
            Serial.println("Timer stopped/paused");
            break;
            
        case TimerState::PAUSED:
            // Third tap - reset timer
            displayPtr->resetTimer();
            timerState = TimerState::STOPPED;
            Serial.println("Timer reset");
            break;
    }
}

void PowerManager::resetTimerState() {
    timerState = TimerState::STOPPED;
    Serial.println("PowerManager timer state reset");
}

void PowerManager::startTimer() {
    if (displayPtr == nullptr) return;
    displayPtr->startTimer();
    timerState = TimerState::RUNNING;
    Serial.println("Timer started (web)");
}

void PowerManager::stopTimer() {
    if (displayPtr == nullptr) return;
    displayPtr->stopTimer();
    timerState = TimerState::PAUSED;
    Serial.println("Timer stopped (web)");
}

void PowerManager::resetTimer() {
    if (displayPtr == nullptr) return;
    displayPtr->resetTimer();
    timerState = TimerState::STOPPED;
    Serial.println("Timer reset (web)");
}

void PowerManager::notifyActivity() {
    lastActivityTime = millis();
}

void PowerManager::setInactivityEnabled(bool enabled) {
    inactivityEnabled = enabled;
    preferences.begin("power", false);
    preferences.putBool("sleep_en", enabled);
    preferences.end();
    Serial.printf("Sleep timeout %s\n", enabled ? "enabled" : "disabled");
}

void PowerManager::setInactivityTimeout(unsigned long ms) {
    inactivityTimeout = ms;
    preferences.begin("power", false);
    preferences.putULong("sleep_ms", ms);
    preferences.end();
    Serial.printf("Sleep timeout set to %lums\n", ms);
}
