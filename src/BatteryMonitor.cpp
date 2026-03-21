#include "BatteryMonitor.h"

BatteryMonitor::BatteryMonitor(uint8_t batteryPin) : batteryPin(batteryPin) {
    lastVoltage = 0.0f;
    lastUpdate = 0;
}

void BatteryMonitor::begin() {
    Serial.println("Initializing Battery Monitor...");
    
    // Configure ADC pin and settings
    pinMode(batteryPin, INPUT);
    analogReadResolution(12);  // Use 12-bit resolution (0-4095)
    analogSetAttenuation(ADC_11db);  // 0-3.3V range for better accuracy
    
    // Load calibration scale from preferences
    preferences.begin("battery", false);
    loadCalibration();
    preferences.end();

    // Take initial reading
    update();
    
    Serial.printf("Battery Monitor initialized on GPIO%d\n", batteryPin);
    Serial.printf("Initial voltage: %.2fV (%d%%)\n", getBatteryVoltage(), getBatteryPercentage());
}

void BatteryMonitor::update() {
    unsigned long currentTime = millis();
    
    // Limit update frequency to reduce noise
    if (currentTime - lastUpdate < UPDATE_INTERVAL) {
        return;
    }
    
    float newVoltage = readRawVoltage();
    
    // Simple smoothing filter (exponential moving average)
    if (lastVoltage == 0.0f) {
        lastVoltage = newVoltage;  // First reading
    } else {
        lastVoltage = (lastVoltage * 0.8f) + (newVoltage * 0.2f);  // 80/20 smoothing
    }
    
    lastUpdate = currentTime;
}

float BatteryMonitor::readRawVoltage() {
    // Take multiple readings for accuracy
    uint32_t totalMillivolts = 0;
    const int samples = 10;

    for (int i = 0; i < samples; i++) {
        totalMillivolts += analogReadMilliVolts(batteryPin);
        delayMicroseconds(100);  // Small delay between readings
    }

    // Convert factory-calibrated millivolts to battery voltage via divider ratio and scale correction
    float voltage = (totalMillivolts / (float)samples / 1000.0f) * VOLTAGE_DIVIDER_RATIO * calibrationScale;

    return voltage;
}

float BatteryMonitor::getBatteryVoltage() {
    return lastVoltage;
}

int BatteryMonitor::getBatteryPercentage() {
    float voltage = getBatteryVoltage();
    
    // Convert voltage to percentage using Li-ion discharge curve
    int percentage;
    
    if (voltage >= BATTERY_FULL) {
        percentage = 100;
    } else if (voltage >= BATTERY_GOOD) {
        // 100% to 75% range
        percentage = 75 + (int)((voltage - BATTERY_GOOD) / (BATTERY_FULL - BATTERY_GOOD) * 25);
    } else if (voltage >= BATTERY_NOMINAL) {
        // 75% to 50% range  
        percentage = 50 + (int)((voltage - BATTERY_NOMINAL) / (BATTERY_GOOD - BATTERY_NOMINAL) * 25);
    } else if (voltage >= BATTERY_LOW) {
        // 50% to 25% range
        percentage = 25 + (int)((voltage - BATTERY_LOW) / (BATTERY_NOMINAL - BATTERY_LOW) * 25);
    } else if (voltage >= BATTERY_CRITICAL) {
        // 25% to 5% range
        percentage = 5 + (int)((voltage - BATTERY_CRITICAL) / (BATTERY_LOW - BATTERY_CRITICAL) * 20);
    } else if (voltage >= BATTERY_EMPTY) {
        // 5% to 0% range
        percentage = (int)((voltage - BATTERY_EMPTY) / (BATTERY_CRITICAL - BATTERY_EMPTY) * 5);
    } else {
        percentage = 0;  // Below 3.2V threshold
    }
    
    return constrain(percentage, 0, 100);
}

String BatteryMonitor::getBatteryStatus() {
    float voltage = getBatteryVoltage();
    
    if (voltage >= BATTERY_FULL) {
        return "Full";
    } else if (voltage >= BATTERY_GOOD) {
        return "Good";        // 4.0V+ - Reliable ESP32 operation
    } else if (voltage >= BATTERY_NOMINAL) {
        return "Fair";        // 3.8V+ - Normal operation
    } else if (voltage >= BATTERY_LOW) {
        return "Low";         // 3.6V+ - Consider charging
    } else if (voltage >= BATTERY_CRITICAL) {
        return "Critical";    // 3.4V+ - Charge immediately
    } else {
        return "Empty";       // <3.4V - Below critical threshold
    }
}

bool BatteryMonitor::isCharging() {
    // Future implementation: detect if voltage is increasing over time
    // For now, return false (would need additional circuitry to detect charging)
    return false;
}

bool BatteryMonitor::isLowBattery() {
    return getBatteryVoltage() < BATTERY_LOW;
}

void BatteryMonitor::calibrateVoltage(float actualVoltage) {
    // Compute scale factor from unscaled reading so we don't compound corrections
    float unscaled = readRawVoltage() / calibrationScale;
    if (unscaled > 0.1f) {
        calibrationScale = actualVoltage / unscaled;
        preferences.begin("battery", false);
        saveCalibration();
        preferences.end();
        Serial.printf("Battery calibrated: scale = %.4f (measured %.3fV, actual %.3fV)\n",
                      calibrationScale, unscaled, actualVoltage);
    }
}

void BatteryMonitor::loadCalibration() {
    calibrationScale = preferences.getFloat("cal_scale", 1.0f);
    Serial.printf("Battery calibration loaded: scale = %.4f\n", calibrationScale);
}

void BatteryMonitor::saveCalibration() {
    preferences.putFloat("cal_scale", calibrationScale);
    Serial.println("Battery calibration saved");
}

bool BatteryMonitor::isCriticalBattery() {
    return getBatteryVoltage() < BATTERY_CRITICAL;
}

int BatteryMonitor::getBatterySegments() {
    int percentage = getBatteryPercentage();
    
    // Convert percentage to 3-segment display
    if (percentage >= 75) {
        return 3;  // Full battery - 3 segments
    } else if (percentage >= 50) {
        return 2;  // Good battery - 2 segments  
    } else if (percentage >= 25) {
        return 1;  // Low battery - 1 segment
    } else {
        return 0;  // Critical battery - empty/flashing
    }
}

