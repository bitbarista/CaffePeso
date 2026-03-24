#pragma once
#include <Arduino.h>

class SmartSwitch {
public:
    SmartSwitch();

    void begin();   // Load settings and learned data from NVS

    // Called every weight-update cycle from main loop
    void update(float weight, float flowRate, bool brewActive, bool armed,
                float doseWeight, float targetRatio);

    // Call when timer is reset to idle — clears brew-level trigger state
    void resetForNewBrew();

    // Settings accessors
    void          setEnabled(bool en)           { enabled = en; }
    void          setShellyIP(const String& ip) { shellyIP = ip; }
    bool          getEnabled()    const          { return enabled; }
    const String& getShellyIP()   const          { return shellyIP; }

    // Returns the learned after-stop time for the given dose/ratio (or DEFAULT_AST if unknown)
    float getCurrentAST(float dose, float ratio) const;

    // Clears all learned after-stop-time data
    void resetLearning();

    void saveSettings();
    void loadSettings();

private:
    bool   enabled  = false;
    String shellyIP = "";

    // Per-brew trigger state
    bool  triggered            = false;
    bool  learningPending      = false;
    float triggerWeight        = 0.0f;
    float flowRateAtTrigger    = 0.0f;
    float lastDoseForLearning  = 0.0f;
    float lastRatioForLearning = 0.0f;
    bool  prevArmed            = false;

    // Post-trigger settle detection (for learning)
    float         settleLastWeight  = 0.0f;
    unsigned long settleStableSince = 0;
    static const  unsigned long SETTLE_STABLE_MS = 3000; // 3s stable = settled
    static constexpr float SETTLE_DEADBAND = 0.3f;       // g

    static constexpr float DEFAULT_AST = 0.5f;  // Conservative first-shot default (seconds)

    // Per dose/ratio learned after-stop times — up to 8 entries
    static const int MAX_AST_ENTRIES = 8;
    float astDose[MAX_AST_ENTRIES]  = {};
    float astRatio[MAX_AST_ENTRIES] = {};
    float astValue[MAX_AST_ENTRIES] = {};
    int   astCount = 0;

    float getAST(float dose, float ratio) const;
    void  updateAST(float dose, float ratio, float newValue);
    void  turnOn();
    void  turnOff();
    void  httpCall(bool on);
    void  saveASTData();
    void  loadASTData();
};
