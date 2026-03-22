#include "TouchSensor.h"
#include "Scale.h"
#include "Display.h"
#include "FlowRate.h"
#include "WiFiManager.h"

TouchSensor::TouchSensor(uint8_t touchPin, Scale* scale)
    : touchPin(touchPin), scalePtr(scale), displayPtr(nullptr), flowRatePtr(nullptr), touchThreshold(30000),
      lastTouchState(false), lastTouchTime(0), touchStartTime(0), debounceDelay(200),
      longPressDetected(false), delayedTarePending(false), delayedTareTime(0),
      holdTarePending(false), preTareWeightCapture(0.0f) {
}

void TouchSensor::begin() {
    // Set up the pin as digital input with pull-down resistor for the touch sensor module
    // This prevents false triggers when no touch sensor is connected
    pinMode(touchPin, INPUT_PULLDOWN);
    Serial.println("Digital touch sensor initialized on pin " + String(touchPin) + " with pull-down resistor");
}

void TouchSensor::update() {
    // Suppress tare input while the sleep button is held AND for 500ms after it is
    // released. The two adjacent touch sensor pads couple capacitively: the active-HIGH
    // output on the sleep pad induces a brief spike on the tare pad at the moment of
    // release (HIGH→LOW transition), which is when the coupling peaks.
    if (sleepPin != 255) {
        static unsigned long sleepPinLastHighTime = 0;
        if (digitalRead(sleepPin) == HIGH) {
            sleepPinLastHighTime = millis();
            return;
        }
        if (millis() - sleepPinLastHighTime < 500) return;
    }

    bool currentTouchState = isTouched();
    unsigned long currentTime = millis();
    
    // Check for touch state change with debouncing
    if (currentTouchState != lastTouchState) {
        if (currentTime - lastTouchTime > debounceDelay) {
            if (currentTouchState) {
                // Touch started - record start time
                touchStartTime = currentTime;
                longPressDetected = false;
                Serial.println("Touch started");
            } else {
                // Touch ended
                unsigned long pressDuration = currentTime - touchStartTime;
                
                if (!longPressDetected) {
                    // Tap: always tare (resets timer too if brew is finished)
                    scheduleDelayedTare();
                    Serial.println("Tare button: tare");
                }
                longPressDetected = false;
                Serial.println("Touch ended");
            }
            lastTouchState = currentTouchState;
            lastTouchTime = currentTime;
        }
    }
    
    
    // During-hold: check for hold-tare (arm + save cup weight)
    if (currentTouchState && !longPressDetected) {
        unsigned long held = currentTime - touchStartTime;
        if (held >= HOLD_TARE_MS) {
            longPressDetected = true;
            Serial.println("Hold tare: arm + save cup weight");
            preTareWeightCapture = (scalePtr != nullptr) ? scalePtr->getCurrentWeight() : 0.0f;
            holdTarePending   = true;
            delayedTarePending = true;
            delayedTareTime    = currentTime + TARE_DELAY;
            if (displayPtr != nullptr) displayPtr->showArmedMessage();
        }
    }

    // Check for pending delayed tare
    checkDelayedTare();
}

void TouchSensor::setTouchThreshold(uint16_t threshold) {
    touchThreshold = threshold;
    Serial.println("Touch threshold set to: " + String(touchThreshold));
}

uint16_t TouchSensor::getTouchValue() {
    // For digital touch sensor modules, return the digital state as 0 or 1
    return digitalRead(touchPin) ? 1 : 0;
}

bool TouchSensor::isTouched() {
    // For digital touch sensor modules, check if the pin is HIGH
    // Most touch sensor modules output HIGH when touched
    bool touched = digitalRead(touchPin) == HIGH;
    
    // Debug: log unexpected HIGH readings when no sensor should be connected
    static unsigned long lastDebugTime = 0;
    if (touched && millis() - lastDebugTime > 5000) { // Log every 5 seconds max
        Serial.println("DEBUG: Touch pin GPIO" + String(touchPin) + " reading HIGH - check for floating pin or connected sensor");
        lastDebugTime = millis();
    }
    
    return touched;
}

void TouchSensor::setDisplay(Display* display) {
    displayPtr = display;
}

void TouchSensor::setFlowRate(FlowRate* flowRate) {
    flowRatePtr = flowRate;
}

void TouchSensor::handleTouch() {
    if (scalePtr != nullptr) {
        Serial.println("Touch detected! Taring scale...");
        
        // Show tare message on display if available
        if (displayPtr != nullptr) {
            displayPtr->showTaringMessage();
        }
        
        scalePtr->tare();
        Serial.println("Scale tared successfully");
        
        // Reset timer when manual tare is pressed
        if (displayPtr != nullptr) {
            displayPtr->resetTimer();
            Serial.println("Timer reset with manual tare");
        }
        
        // Reset flow rate averaging for fresh brew
        if (flowRatePtr != nullptr) {
            flowRatePtr->resetTimerAveraging();
            Serial.println("Flow rate averaging reset for fresh brew");
        }
        
        // Show completion message on display if available
        if (displayPtr != nullptr) {
            displayPtr->showTaredMessage();
        }
    } else {
        Serial.println("Error: Scale pointer is null");
    }
}

void TouchSensor::scheduleDelayedTare() {
    Serial.println("Touch detected - showing taring message immediately");

    // Capture weight at tap time for auto-re-arm comparison
    preTareWeightCapture = (scalePtr != nullptr) ? scalePtr->getCurrentWeight() : 0.0f;
    holdTarePending = false; // tap, not hold

    if (displayPtr != nullptr) {
        displayPtr->showTaringMessage();
        Serial.println("Taring message displayed");
    }

    Serial.println("Scheduling delayed tare in 1.5 seconds...");
    delayedTarePending = true;
    delayedTareTime = millis() + TARE_DELAY;
}

void TouchSensor::checkDelayedTare() {
    if (delayedTarePending && millis() >= delayedTareTime) {
        Serial.println("Executing delayed tare operation");
        delayedTarePending = false;
        bool wasHoldTare   = holdTarePending;
        holdTarePending    = false;

        if (scalePtr != nullptr) {
            scalePtr->tare();
            Serial.println("Scale tared successfully");

            if (displayPtr != nullptr) {
                if (wasHoldTare) {
                    // Hold-tare: arm auto-start and save cup weight to NVS
                    displayPtr->arm(preTareWeightCapture);
                    Serial.printf("Cup weight saved: %.1fg, armed for auto-start\n", preTareWeightCapture);
                } else {
                    // Tap-tare: reset timer if not mid-brew (covers both stopped and paused states)
                    if (!displayPtr->isTimerRunning()) {
                        displayPtr->resetTimer();
                        if (flowRatePtr != nullptr) flowRatePtr->resetTimerAveraging();
                        Serial.println("Timer reset on tare");
                    }
                    // Auto-re-arm: if pre-tare weight matches saved cup weight ±5g
                    float saved = displayPtr->getSavedTareWeight();
                    if (saved > 5.0f && fabs(preTareWeightCapture - saved) <= 5.0f) {
                        displayPtr->arm(saved); // re-arm with existing saved weight
                        Serial.printf("Auto-re-armed: %.1fg matches saved %.1fg\n",
                                      preTareWeightCapture, saved);
                    } else {
                        displayPtr->showTaredMessage();
                    }
                }
            }
        } else {
            Serial.println("Error: Scale pointer is null");
        }
    }
}

