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

    // Post-trigger relay safety
    // True when the relay was turned off by a trigger and has not yet been
    // re-enabled by a deliberate hold-tare from the user.
    bool isPostTriggerRelayOff() const { return postTriggerRelayOff; }

    // Called from main.cpp when a hold-tare completes and postTriggerRelayOff
    // is set.  Sends the ON command to the Shelly and clears the flag on
    // success.  Returns false (and leaves the flag set) if the HTTP call fails.
    bool reEnableRelay();

    // Best-effort ON command — used on boot/wake and when the feature is
    // disabled.  Always clears postTriggerRelayOff regardless of HTTP result.
    void ensureRelayOn();

    // Returns the learned after-stop time for the given dose/ratio (or DEFAULT_AST if unknown)
    float getCurrentAST(float dose, float ratio) const;

    // Clears all learned after-stop-time data
    void resetLearning();

    void saveSettings();
    void loadSettings();

private:
    bool   enabled  = false;
    String shellyIP = "";

    // Safety: relay was turned off by a trigger; stays set until a deliberate
    // hold-tare re-enables it via reEnableRelay().  NOT cleared by
    // resetForNewBrew() — it must survive the brew-idle reset cycle.
    bool  postTriggerRelayOff  = false;

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
    bool  httpCall(bool on);   // Returns true on HTTP 2xx, false on any failure
    void  saveASTData();
    void  loadASTData();
};
