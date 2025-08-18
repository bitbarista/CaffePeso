#ifndef BATTERYMONITOR_H
#define BATTERYMONITOR_H

#include <Arduino.h>
#include <Preferences.h>

class BatteryMonitor {
public:
    BatteryMonitor(uint8_t batteryPin);
    void begin();
    
    // Battery readings
    float getBatteryVoltage();
    int getBatteryPercentage();
    String getBatteryStatus();  // "Full", "Good", "Low", "Critical"
    
    // Battery state indicators
    bool isCharging();  // Future expansion for charge detection
    bool isLowBattery();
    bool isCriticalBattery();
    
    // Configuration and calibration
    void calibrateVoltage(float actualVoltage);  // For fine-tuning readings
    float getCalibrationOffset() const { return calibrationOffset; }
    
    // Update method for periodic readings
    void update();
    
    // OLED display helper - returns battery segments (0-3)
    int getBatterySegments();
    
private:
    uint8_t batteryPin;
    Preferences preferences;
    
    // Li-ion voltage thresholds (with protection circuit)
    static constexpr float BATTERY_FULL = 4.2f;      // 100%
    static constexpr float BATTERY_GOOD = 3.8f;      // ~75%
    static constexpr float BATTERY_NOMINAL = 3.7f;   // ~50%
    static constexpr float BATTERY_LOW = 3.4f;       // ~25%
    static constexpr float BATTERY_CRITICAL = 3.0f;  // ~5%
    static constexpr float BATTERY_EMPTY = 2.9f;     // 0% (protection kicks in)
    
    // Hardware configuration
    static constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;  // 100k + 100k resistors
    static constexpr float ADC_REFERENCE = 3.3f;          // ESP32-S3 with ADC_11db attenuation (0-3.3V)
    static constexpr int ADC_RESOLUTION = 4095;
    
    // Calibration and smoothing
    float calibrationOffset = 0.0f;  // Voltage adjustment for accuracy
    float lastVoltage = 0.0f;        // For smoothing readings
    unsigned long lastUpdate = 0;
    static constexpr unsigned long UPDATE_INTERVAL = 1000; // Update every 1 second
    
    // Internal methods
    float readRawVoltage();
    void loadCalibration();
    void saveCalibration();
};

#endif
