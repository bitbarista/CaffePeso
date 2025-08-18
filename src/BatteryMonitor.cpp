#include "BatteryMonitor.h"

BatteryMonitor::BatteryMonitor(uint8_t batteryPin) : batteryPin(batteryPin) {
    lastVoltage = 0.0f;
    lastUpdate = 0;
}

void BatteryMonitor::begin() {
    Serial.println("Initializing Battery Monitor...");
    
    // Configure ADC pin
    pinMode(batteryPin, INPUT);
    
    // Load calibration from preferences
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
    int totalReading = 0;
    const int samples = 10;
    
    for (int i = 0; i < samples; i++) {
        totalReading += analogRead(batteryPin);
        delayMicroseconds(100);  // Small delay between readings
    }
    
    int avgReading = totalReading / samples;
    
    // Convert ADC reading to voltage
    float voltage = ((float)avgReading / ADC_RESOLUTION) * ADC_REFERENCE * VOLTAGE_DIVIDER_RATIO;
    
    // Apply calibration offset
    voltage += calibrationOffset;
    
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
        percentage = 0;  // Below protection circuit threshold
    }
    
    return constrain(percentage, 0, 100);
}

String BatteryMonitor::getBatteryStatus() {
    float voltage = getBatteryVoltage();
    
    if (voltage >= BATTERY_FULL) {
        return "Full";
    } else if (voltage >= BATTERY_GOOD) {
        return "Good";
    } else if (voltage >= BATTERY_LOW) {
        return "Fair";
    } else if (voltage >= BATTERY_CRITICAL) {
        return "Low";
    } else {
        return "Critical";
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

void BatteryMonitor::calibrateVoltage(float actualVoltage) {
    float measuredVoltage = readRawVoltage() - calibrationOffset;  // Get uncalibrated reading
    calibrationOffset = actualVoltage - measuredVoltage;
    
    // Save calibration
    preferences.begin("battery", false);
    saveCalibration();
    preferences.end();
    
    Serial.printf("Battery calibrated: offset = %.3fV\n", calibrationOffset);
}

void BatteryMonitor::loadCalibration() {
    calibrationOffset = preferences.getFloat("cal_offset", 0.0f);
    Serial.printf("Battery calibration loaded: offset = %.3fV\n", calibrationOffset);
}

void BatteryMonitor::saveCalibration() {
    preferences.putFloat("cal_offset", calibrationOffset);
    Serial.println("Battery calibration saved");
}
