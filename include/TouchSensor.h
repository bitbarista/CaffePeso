#ifndef TOUCHSENSOR_H
#define TOUCHSENSOR_H

#include <Arduino.h>

class Scale; // Forward declaration
class Display; // Forward declaration
class FlowRate; // Forward declaration

class TouchSensor {
public:
    TouchSensor(uint8_t touchPin, Scale* scale);
    void begin();
    void update();
    void setTouchThreshold(uint16_t threshold);
    uint16_t getTouchValue();
    bool isTouched();
    void setDisplay(Display* display);
    void setFlowRate(FlowRate* flowRate);
    void setSleepPin(uint8_t pin) { sleepPin = pin; } // Suppress tare while sleep button is held

    // Returns true exactly once after a hold-tare fully completes (arm + tare
    // executed).  Clears itself on read — poll from main loop each iteration.
    bool wasHoldTareCompleted() {
        bool v = holdTareJustCompleted;
        holdTareJustCompleted = false;
        return v;
    }

private:
    uint8_t touchPin;
    uint8_t sleepPin = 255; // 255 = not set; checked in update() to suppress coupling
    Scale* scalePtr;
    Display* displayPtr;
    FlowRate* flowRatePtr;
    uint16_t touchThreshold;
    bool lastTouchState;
    unsigned long lastTouchTime;
    unsigned long touchStartTime;
    unsigned long debounceDelay;
    bool longPressDetected;

    // Delayed tare functionality for mounted touch sensors
    bool delayedTarePending;
    unsigned long delayedTareTime;
    static const unsigned long TARE_DELAY        = 1500; // delay before tap-tare executes
    static const unsigned long HOLD_TARE_MS      = 1500; // hold duration to show "Release!"
    static const unsigned long HOLD_FEEDBACK_MS  = 500;  // show "Taring..." at this point
    static const unsigned long HOLD_SETTLE_MS    = 600;  // settle time after release before weight capture

    // Hold-tare state
    bool  holdTarePending;            // set when hold+release detected; cleared in checkDelayedTare
    bool  holdFeedbackShown;          // true once the "Taring..." intermediate message has been shown
    bool  holdWaitingForRelease;      // true once hold threshold reached; waiting for button release
    bool  holdTareJustCompleted = false; // strobed true for one loop when hold-tare fully executes

    void handleTouch();
    void scheduleDelayedTare();
    void checkDelayedTare();
};

#endif
