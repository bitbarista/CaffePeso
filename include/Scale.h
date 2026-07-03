#ifndef SCALE_H
#define SCALE_H

#include <HX711.h>
#include <Preferences.h>

class Scale {
public:
    Scale(uint8_t dataPin, uint8_t clockPin, float calibrationFactor);
    bool begin();  // Returns true if successful, false if HX711 fails
    void tare(uint8_t times = 20);
    void set_scale(float factor);
    float getWeight();
    float getCurrentWeight();
    long getRawValue();
    void saveCalibration(); // Save calibration factor to NVS
    void loadCalibration(); // Load calibration factor from NVS
    float getCalibrationFactor() const { return calibrationFactor; } // Getter for API
    bool isHX711Connected() const { return isConnected; } // Check if HX711 is responding

    // --- Diagnostic / robustness helpers ---
    bool isReady();              // HX711 has a fresh sample ready (non-blocking)
    bool probeRaw(float& out);   // non-blocking: if ready, read raw (offset-removed) value; false if not ready
    void recover();              // power-cycle the HX711 to clear a lockup / power-down
    uint32_t getRecoveryCount() const { return hx711Recoveries; } // times getWeight() auto-recovered a stalled HX711
    
    // Filtering configuration - adjustable for different load cells
    void setBrewingThreshold(float threshold);
    void setStabilityTimeout(unsigned long timeout);
    void setMedianSamples(int samples);
    void setAverageSamples(int samples);
    
    float getBrewingThreshold() const { return brewingThreshold; }
    unsigned long getStabilityTimeout() const { return stabilityTimeout; }
    int getMedianSamples() const { return medianSamples; }
    int getAverageSamples() const { return averageSamples; }
    String getFilterState() const; // Get current filter state as string for debugging
    
    void saveFilterSettings();
    void loadFilterSettings();
    
    // FlowRate integration for tare operations
    void setFlowRatePtr(class FlowRate* flowRatePtr);
    
private:
    HX711 hx711;
    Preferences preferences;
    uint8_t dataPin;
    uint8_t clockPin;
    float calibrationFactor = 0.0f;
    float currentWeight;
    bool isConnected = false;  // Track HX711 connection status
    class FlowRate* flowRatePtr = nullptr; // For pausing flow rate during tare

    // HX711 stall self-heal: if the chip is observed not-ready continuously for
    // this long (normal cadence is ~100ms @10Hz, so 3 missed conversions = clearly
    // stuck, not jitter), an electrical glitch has likely latched it up / into
    // power-down — power-cycle it instead of returning a stale reading forever
    // (which presents as a frozen scale until power-off). Kept short so a glitch
    // mid-shot loses <~0.3s of data before recovery (plus the HX711's own ~400ms
    // post-power-up settle, which is inherent at 10Hz).
    // Detection measures *continuously observed* not-ready time (notReadySince is
    // set on the first not-ready poll and cleared the instant a sample arrives),
    // NOT time since the last read — so a loop that simply didn't call getWeight()
    // for a while (BLE / shot-save) can't false-trigger a recovery.
    static const unsigned long HX711_STALL_TIMEOUT_MS = 300;
    // After a power-cycle the HX711 needs ~400ms (@10Hz) to produce settled data
    // and re-assert ready. Don't recover again until this cooldown has elapsed,
    // otherwise we power-cycle faster than it can come back and storm-loop forever
    // (one stall -> endless ~3/s recoveries, scale stuck). Must be > the settle time.
    static const unsigned long HX711_RECOVERY_COOLDOWN_MS = 800;
    unsigned long notReadySince = 0;  // millis() of first consecutive not-ready poll (0 = ready)
    unsigned long lastRecoverMs = 0;  // millis() of last recover() — gates the cooldown
    uint32_t      hx711Recoveries = 0; // count of auto-recoveries (surfaced over API)
    
    // Smart filtering variables - reduced buffer for faster response
    static const int MAX_SAMPLES = 10;  // Reduced from 50 to 10 for faster response
    float readings[MAX_SAMPLES];
    int readingIndex = 0;
    bool samplesInitialized = false;
    float previousFilteredWeight = 0;
    
    // Brewing state tracking for smart filtering
    enum FilterState {
        STABLE,     // Using average filter - stable weight
        BREWING,    // Using median filter - active brewing
        TRANSITIONING // Waiting for stability after brewing activity
    };
    FilterState currentFilterState = STABLE;
    unsigned long lastBrewingActivity = 0;  // Track when brewing was last detected
    float lastStableWeight = 0.0f;          // Last weight when in stable state
    
    // Configurable filtering parameters
    float brewingThreshold = 0.15f;  // Keep for API compatibility
    unsigned long stabilityTimeout = 2000;  // Keep for API compatibility
    int medianSamples = 3;  // Keep for API compatibility
    int averageSamples = 2;  // Samples for average filter - reduced for faster response
    
    // Filter methods
    float medianFilter(int samples);
    float averageFilter(int samples);
    void initializeSamples(float initialValue);
};

#endif
