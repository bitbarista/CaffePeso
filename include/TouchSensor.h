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
    static const unsigned long TARE_DELAY    = 1500; // delay before tare executes
    static const unsigned long HOLD_TARE_MS  = 500;  // hold duration to trigger arm+save

    // Hold-tare state
    bool  holdTarePending;       // set when hold detected; cleared in checkDelayedTare
    float preTareWeightCapture;  // weight captured at button event time, before tare

    void handleTouch();
    void scheduleDelayedTare();
    void checkDelayedTare();
};

#endif
