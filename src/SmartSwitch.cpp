#include "SmartSwitch.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

SmartSwitch::SmartSwitch() {}

void SmartSwitch::begin() {
    loadSettings();
    loadASTData();
}

void SmartSwitch::update(float weight, float flowRate, bool brewActive, bool armed,
                         float doseWeight, float targetRatio) {
    // When newly armed: turn Shelly on and clear any previous brew state
    if (armed && !prevArmed) {
        turnOn();
        resetForNewBrew();
    }
    prevArmed = armed;

    if (!enabled || shellyIP.isEmpty()) return;

    // Post-trigger settle detection — runs after brew ends until weight stabilises
    if (learningPending && !brewActive) {
        if (settleStableSince == 0) {
            settleStableSince = millis();
            settleLastWeight  = weight;
        } else if (fabsf(weight - settleLastWeight) > SETTLE_DEADBAND) {
            settleStableSince = millis();
            settleLastWeight  = weight;
        } else if (millis() - settleStableSince >= SETTLE_STABLE_MS) {
            float weightGain = weight - triggerWeight;
            if (weightGain > 0.0f && weightGain <= 5.0f && flowRateAtTrigger > 0.3f) {
                float measured = weightGain / flowRateAtTrigger;
                measured = constrain(measured, 0.2f, 2.0f);
                updateAST(lastDoseForLearning, lastRatioForLearning, measured);
                saveASTData();
                Serial.printf("[SmartSwitch] Learned AST=%.2fs (gain=%.2fg @ %.2fg/s)\n",
                              measured, weightGain, flowRateAtTrigger);
            }
            learningPending   = false;
            settleStableSince = 0;
        }
        return;
    }

    if (!brewActive || triggered) return;

    // Need both dose and ratio configured to compute a target
    if (doseWeight <= 0.5f || targetRatio <= 0.0f) return;

    float targetWeight = doseWeight * targetRatio;
    float ast = getAST(doseWeight, targetRatio);
    float triggerAt;

    if (flowRate > 0.1f) {
        // Predictive: trigger when projected final weight will hit target
        triggerAt = targetWeight - (flowRate * ast);
    } else {
        // No flow data yet — use conservative weight-based fallback
        triggerAt = targetWeight - (ast * 1.5f);
    }

    if (weight >= triggerAt) {
        triggerWeight        = weight;
        flowRateAtTrigger    = flowRate;
        lastDoseForLearning  = doseWeight;
        lastRatioForLearning = targetRatio;
        triggered            = true;
        learningPending      = true;
        settleStableSince    = 0;
        turnOff();
        Serial.printf("[SmartSwitch] Triggered at %.1fg (target=%.1fg, ast=%.2fs, flow=%.2fg/s)\n",
                      weight, targetWeight, ast, flowRate);
    }
}

void SmartSwitch::resetForNewBrew() {
    triggered         = false;
    learningPending   = false;
    settleStableSince = 0;
    settleLastWeight  = 0.0f;
}

float SmartSwitch::getCurrentAST(float dose, float ratio) const {
    return getAST(dose, ratio);
}

float SmartSwitch::getAST(float dose, float ratio) const {
    for (int i = 0; i < astCount; i++) {
        if (fabsf(astDose[i] - dose) < 0.05f && fabsf(astRatio[i] - ratio) < 0.01f) {
            return astValue[i];
        }
    }
    return DEFAULT_AST;
}

void SmartSwitch::updateAST(float dose, float ratio, float newValue) {
    for (int i = 0; i < astCount; i++) {
        if (fabsf(astDose[i] - dose) < 0.05f && fabsf(astRatio[i] - ratio) < 0.01f) {
            // Exponential weighted moving average: 75% old, 25% new — gradual adaptation
            astValue[i] = 0.75f * astValue[i] + 0.25f * newValue;
            Serial.printf("[SmartSwitch] Updated AST[%d]=%.2fs for %.1fg/%.2f\n",
                          i, astValue[i], dose, ratio);
            return;
        }
    }
    // New entry — evict slot 0 (LRU approximation) if table is full
    int idx = (astCount < MAX_AST_ENTRIES) ? astCount++ : 0;
    astDose[idx]  = dose;
    astRatio[idx] = ratio;
    astValue[idx] = newValue;
    Serial.printf("[SmartSwitch] New AST[%d]=%.2fs for %.1fg/%.2f\n",
                  idx, newValue, dose, ratio);
}

void SmartSwitch::resetLearning() {
    astCount = 0;
    saveASTData();
    Serial.println("[SmartSwitch] Learning data reset");
}

void SmartSwitch::turnOn() {
    if (!enabled || shellyIP.isEmpty()) return;
    httpCall(true);
}

void SmartSwitch::turnOff() {
    httpCall(false);
}

void SmartSwitch::httpCall(bool on) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[SmartSwitch] No WiFi — skipping");
        return;
    }
    HTTPClient http;
    String url = "http://" + shellyIP + "/rpc/Switch.Set?id=0&on=" + (on ? "true" : "false");
    Serial.println("[SmartSwitch] " + url);
    http.begin(url);
    http.setTimeout(1000);
    int code = http.GET();
    http.end();
    Serial.printf("[SmartSwitch] %s response: %d\n", on ? "ON" : "OFF", code);
}

void SmartSwitch::saveSettings() {
    Preferences p;
    if (p.begin("shelly", false)) {
        p.putBool("enabled", enabled);
        p.putString("ip", shellyIP);
        p.end();
    }
}

void SmartSwitch::loadSettings() {
    Preferences p;
    if (p.begin("shelly", true)) {
        enabled  = p.getBool("enabled", false);
        shellyIP = p.getString("ip", "");
        p.end();
    }
    Serial.printf("[SmartSwitch] enabled=%d ip=%s\n", enabled, shellyIP.c_str());
}

void SmartSwitch::saveASTData() {
    Preferences p;
    if (!p.begin("shelly_ast", false)) return;
    p.putInt("cnt", astCount);
    for (int i = 0; i < astCount; i++) {
        char key[4];
        snprintf(key, sizeof(key), "d%d", i); p.putFloat(key, astDose[i]);
        snprintf(key, sizeof(key), "r%d", i); p.putFloat(key, astRatio[i]);
        snprintf(key, sizeof(key), "a%d", i); p.putFloat(key, astValue[i]);
    }
    p.end();
}

void SmartSwitch::loadASTData() {
    Preferences p;
    if (!p.begin("shelly_ast", true)) return;
    astCount = constrain(p.getInt("cnt", 0), 0, MAX_AST_ENTRIES);
    for (int i = 0; i < astCount; i++) {
        char key[4];
        snprintf(key, sizeof(key), "d%d", i); astDose[i]  = p.getFloat(key, 0.0f);
        snprintf(key, sizeof(key), "r%d", i); astRatio[i] = p.getFloat(key, 0.0f);
        snprintf(key, sizeof(key), "a%d", i); astValue[i] = p.getFloat(key, DEFAULT_AST);
    }
    p.end();
    Serial.printf("[SmartSwitch] Loaded %d AST entries\n", astCount);
}
