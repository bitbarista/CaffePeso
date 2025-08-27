#include "TouchSensor.h"
#include "Scale.h"
#include "Display.h"
#include "FlowRate.h"

TouchSensor::TouchSensor(uint8_t touchPin, Scale* scale) 
    : touchPin(touchPin), scalePtr(scale), displayPtr(nullptr), flowRatePtr(nullptr), touchThreshold(30000), 
      lastTouchState(false), lastTouchTime(0), touchStartTime(0), debounceDelay(200),
      longPressDetected(false), delayedTarePending(false), delayedTareTime(0) {
}

void TouchSensor::begin() {
    // Set up the pin as digital input with pull-down resistor for the touch sensor module
    // This prevents false triggers when no touch sensor is connected
    pinMode(touchPin, INPUT_PULLDOWN);
    Serial.println("Digital touch sensor initialized on pin " + String(touchPin) + " with pull-down resistor");
}

void TouchSensor::update() {
    bool currentTouchState = isTouched();
    unsigned long currentTime = millis();
    
    // Check for touch state change with debouncing
    if (currentTouchState != lastTouchState) {
        if (currentTime - lastTouchTime > debounceDelay) {
            if (currentTouchState) {
                // Touch started - record start time for long press detection
                touchStartTime = currentTime;
                longPressDetected = false;
                Serial.println("Touch started");
            } else {
                // Touch ended
                if (!longPressDetected) {
                    // Check if it was a medium press for status page (500ms)
                    if (currentTime - touchStartTime >= 500) {
                        handleStatusPageToggle();
                    } else {
                        // Short press - schedule delayed tare to allow scale to stabilize
                        scheduleDelayedTare();
                    }
                }
                longPressDetected = false;
                Serial.println("Touch ended");
            }
            lastTouchState = currentTouchState;
            lastTouchTime = currentTime;
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
    
    // Show taring message immediately for better user feedback
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
        
        // Perform the actual tare operation without showing message again
        if (scalePtr != nullptr) {
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
}

void TouchSensor::handleStatusPageToggle() {
    Serial.println("Medium press detected - toggling status page");
    
    if (displayPtr != nullptr) {
        displayPtr->toggleStatusPage();
    } else {
        Serial.println("Error: Display pointer is null");
    }
}
