#include "Scale.h"
#include "WebServer.h"
#include "Calibration.h"
#include "FlowRate.h"

Scale::Scale(uint8_t dataPin, uint8_t clockPin, float calibrationFactor)
    : dataPin(dataPin), clockPin(clockPin), calibrationFactor(calibrationFactor), currentWeight(0.0f),
      readingIndex(0), samplesInitialized(false), previousFilteredWeight(0), medianSamples(3), averageSamples(2) {
    // Initialize readings array
    for (int i = 0; i < MAX_SAMPLES; i++) {
        readings[i] = 0.0f;
    }
}

bool Scale::begin() {
    Serial.println("Starting scale initialization...");
    
    preferences.begin("scale", false);
    calibrationFactor = preferences.getFloat("calib", calibrationFactor);
    
    // Load filtering parameters with load cell-specific defaults
    loadFilterSettings();
    
    // Auto-adjust brewing threshold based on calibration factor and load cell characteristics
    // Only if not previously saved by user (check if key exists)
    if (!preferences.isKey("brew_thresh")) {
        // For 3kg load cells (1mV/V): calibration factors typically 400-800
        // For 500g load cells (2mV/V): calibration factors typically 2000-5000+
        if (calibrationFactor < 1000) {
            brewingThreshold = 0.25f;  // 3kg load cell with 1mV/V - needs higher threshold due to lower sensitivity
            Serial.println("Auto-detected 3kg load cell (low calibration factor)");
        } else if (calibrationFactor < 2500) {
            brewingThreshold = 0.15f; // Medium sensitivity load cell
            Serial.println("Auto-detected medium sensitivity load cell");
        } else {
            brewingThreshold = 0.1f;  // 500g load cell with 2mV/V - more sensitive, can use lower threshold
            Serial.println("Auto-detected high sensitivity load cell (500g/2mV/V type)");
        }
        saveFilterSettings(); // Save auto-detected values
    }
    
    preferences.end();
    
    // Initialize HX711 with error handling
    Serial.println("Initializing HX711...");
    hx711.begin(dataPin, clockPin);
    hx711.set_scale(calibrationFactor);
    
    // Test if HX711 is responding with a timeout
    Serial.println("Testing HX711 connection...");
    unsigned long startTime = millis();
    bool testPassed = false;
    
    // Try to get a reading with 3 second timeout
    while (millis() - startTime < 3000) {
        if (hx711.is_ready()) {
            long testReading = hx711.read();
            if (testReading != 0) {  // HX711 returns 0 when not connected
                testPassed = true;
                Serial.println("HX711 test reading: " + String(testReading));
                break;
            }
        }
        delay(100);  // Small delay between attempts
    }
    
    if (testPassed) {
        Serial.println("HX711 connected successfully");
        isConnected = true;
        
        // Only tare if connection is confirmed
        Serial.println("Performing initial tare...");
        hx711.tare();
        
        Serial.println("Scale filtering configured:");
        Serial.println("Brewing threshold: " + String(brewingThreshold) + "g");
        Serial.println("Stability timeout: " + String(stabilityTimeout) + "ms");
        Serial.println("Median samples: " + String(medianSamples));
        Serial.println("Average samples: " + String(averageSamples));
        
        return true;
    } else {
        Serial.println("ERROR: HX711 not responding!");
        Serial.println("Check connections:");
        Serial.println("- VCC to 3.3V or 5V");
        Serial.println("- GND to GND");
        Serial.println("- DT to GPIO " + String(dataPin));
        Serial.println("- SCK to GPIO " + String(clockPin));
        Serial.println("- Load cell connections");
        
        isConnected = false;
        return false;
    }
}

void Scale::tare(uint8_t times) {
    if (!isConnected) {
        Serial.println("Cannot tare: HX711 not connected");
        return;
    }
    
    // Pause flow rate calculation to prevent tare operation from affecting flow rate
    if (flowRatePtr != nullptr) {
        flowRatePtr->pauseCalculation();
    }
    
    Serial.println("Taring scale...");
    hx711.tare(times);
    Serial.println("Tare complete");
    
    // Resume flow rate calculation after a short delay to ensure stable readings
    if (flowRatePtr != nullptr) {
        delay(100); // Short delay to let scale stabilize
        flowRatePtr->resumeCalculation();
    }
}

void Scale::set_scale(float factor) {
    // Only save if the calibration factor actually changed
    if (calibrationFactor != factor) {
        calibrationFactor = factor;
        hx711.set_scale(calibrationFactor);
        saveCalibration();
    }
}

void Scale::saveCalibration() {
    preferences.begin("scale", false);
    preferences.putFloat("calib", calibrationFactor);
    preferences.end();
}

void Scale::loadCalibration() {
    preferences.begin("scale", true);
    calibrationFactor = preferences.getFloat("calib", calibrationFactor);
    preferences.end();
}

float Scale::getWeight() {
    // Return 0 if HX711 is not connected
    if (!isConnected) {
        return 0.0f;
    }
    
    static unsigned long lastReadTime = 0;
    unsigned long currentTime = millis();
    
    // Read at 50Hz (every 20ms) for good responsiveness
    if (currentTime - lastReadTime < 20) {
        return currentWeight;
    }
    lastReadTime = currentTime;

    // Check if HX711 is ready before attempting to read
    if (!hx711.is_ready()) {
        return currentWeight;  // Return last known value if not ready
    }
    
    float rawReading = hx711.get_units(1);
    
    // Handle NaN or invalid readings
    if (isnan(rawReading)) {
        return currentWeight;
    }
    
    // Initialize sample buffer on first valid reading
    if (!samplesInitialized) {
        initializeSamples(rawReading);
        currentWeight = rawReading;
        return currentWeight;
    }
    
    // Store reading in circular buffer
    readings[readingIndex] = rawReading;
    readingIndex = (readingIndex + 1) % MAX_SAMPLES;
    
    // Detect rapid weight changes for faster response
    float weightChange = abs(rawReading - currentWeight);
    
    if (weightChange > 5.0f) {  // Rapid change detected (>5g difference)
        // Clear buffer and reinitialize with new reading for immediate response
        initializeSamples(rawReading);
        currentWeight = rawReading;
    } else {
        // Normal filtering for stable readings
        currentWeight = averageFilter(averageSamples);
    }
    
    return currentWeight;
}

float Scale::getCurrentWeight() {
    return currentWeight;
}

long Scale::getRawValue() {
    if (!isConnected) {
        return 0;  // Return 0 if HX711 not connected
    }
    return hx711.get_value(1); // Get raw value from HX711
}

void Scale::initializeSamples(float initialValue) {
    for (int i = 0; i < MAX_SAMPLES; i++) {
        readings[i] = initialValue;
    }
    samplesInitialized = true;
}

float Scale::medianFilter(int samples) {
    if (samples > MAX_SAMPLES) samples = MAX_SAMPLES;
    
    // Copy recent readings
    float temp[samples];
    for (int i = 0; i < samples; i++) {
        int idx = (readingIndex - 1 - i + MAX_SAMPLES) % MAX_SAMPLES;
        temp[i] = readings[idx];
    }
    
    // Simple bubble sort for median (efficient for small arrays)
    for (int i = 0; i < samples - 1; i++) {
        for (int j = 0; j < samples - i - 1; j++) {
            if (temp[j] > temp[j + 1]) {
                float swap = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = swap;
            }
        }
    }
    return temp[samples / 2]; // Return median
}

float Scale::averageFilter(int samples) {
    if (samples > MAX_SAMPLES) samples = MAX_SAMPLES;
    
    float sum = 0;
    int validSamples = 0;
    
    // Calculate average of recent samples
    for (int i = 0; i < samples; i++) {
        int idx = (readingIndex - 1 - i + MAX_SAMPLES) % MAX_SAMPLES;
        sum += readings[idx];
        validSamples++;
    }
    
    return sum / validSamples; // Return simple average without additional smoothing
}

// Filter parameter setters with validation
void Scale::setBrewingThreshold(float threshold) {
    if (threshold >= 0.05f && threshold <= 1.0f) { // Reasonable bounds
        brewingThreshold = threshold;
        saveFilterSettings();
    }
}

void Scale::setStabilityTimeout(unsigned long timeout) {
    if (timeout >= 500 && timeout <= 10000) { // 0.5-10 seconds
        stabilityTimeout = timeout;
        saveFilterSettings();
    }
}

void Scale::setMedianSamples(int samples) {
    if (samples >= 1 && samples <= MAX_SAMPLES) {
        medianSamples = samples;
        saveFilterSettings();
    }
}

void Scale::setAverageSamples(int samples) {
    if (samples >= 1 && samples <= MAX_SAMPLES) {
        averageSamples = samples;
        saveFilterSettings();
    }
}

void Scale::saveFilterSettings() {
    preferences.begin("scale", false);
    preferences.putFloat("brew_thresh", brewingThreshold);
    preferences.putULong("stab_timeout", stabilityTimeout);
    preferences.putInt("median_samples", medianSamples);
    preferences.putInt("avg_samples", averageSamples);
    preferences.end();
    Serial.println("Filter settings saved to EEPROM");
}

void Scale::loadFilterSettings() {
    // Load with sensible defaults
    brewingThreshold = preferences.getFloat("brew_thresh", 0.15f);
    stabilityTimeout = preferences.getULong("stab_timeout", 2000);
    medianSamples = preferences.getInt("median_samples", 3);
    averageSamples = preferences.getInt("avg_samples", 2); // Reduced for faster response
}

void Scale::setFlowRatePtr(FlowRate* flowRatePtr) {
    this->flowRatePtr = flowRatePtr;
}
