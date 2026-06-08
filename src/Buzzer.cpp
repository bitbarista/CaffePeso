#include "Buzzer.h"

// ---------------------------------------------------------------------------
// Beep patterns
//
// The piezo disc is sharply resonant at ~4200 Hz, so it is much louder there
// than off-resonance. Patterns are therefore differentiated mainly by RHYTHM
// (number and length of beeps) at the resonant frequency, with only small
// pitch shifts where a distinct "feel" helps. Tune freely here — this is the
// single place beep sounds are defined.
//
// A Tone of {0, ms} is a silent gap. RES is the resonant frequency.
// ---------------------------------------------------------------------------

static const uint16_t RES = 4200;

// One long tone — the headline target-yield alert (matches the OLED flash).
static const Buzzer::Tone PAT_TARGET[]    = { {RES, 220} };
// Two medium beeps — the shot was stopped.
static const Buzzer::Tone PAT_SMART[]     = { {RES, 110}, {0, 90}, {RES, 110} };
// Short rising pair — armed / re-armed for the next shot.
static const Buzzer::Tone PAT_ARMED[]     = { {3600, 70}, {RES, 110} };
// Quick double high tick — BLE client connected.
static const Buzzer::Tone PAT_BLE_CONN[]  = { {RES, 50}, {0, 45}, {RES, 50} };
// Single longer lower tone — BLE client dropped (falling feel).
static const Buzzer::Tone PAT_BLE_DISC[]  = { {3200, 260} };
// Three slow descending beeps — battery low/critical (insistent).
static const Buzzer::Tone PAT_BATTERY[]   = { {RES, 150}, {0, 140}, {3600, 150}, {0, 140}, {3000, 200} };
// Single short chirp — "I'm awake".
static const Buzzer::Tone PAT_BOOT[]      = { {RES, 80} };
// Single short tick — scale tared.
static const Buzzer::Tone PAT_TARE[]      = { {RES, 60} };

const Buzzer::Tone* Buzzer::patternFor(BuzzerEvent ev, uint8_t& lenOut) {
    switch (ev) {
        case BuzzerEvent::TargetYield:      lenOut = 1; return PAT_TARGET;
        case BuzzerEvent::SmartSwitchFired: lenOut = 3; return PAT_SMART;
        case BuzzerEvent::Armed:            lenOut = 2; return PAT_ARMED;
        case BuzzerEvent::BleConnected:     lenOut = 3; return PAT_BLE_CONN;
        case BuzzerEvent::BleDisconnected:  lenOut = 1; return PAT_BLE_DISC;
        case BuzzerEvent::BatteryLow:       lenOut = 5; return PAT_BATTERY;
        case BuzzerEvent::BootReady:        lenOut = 1; return PAT_BOOT;
        case BuzzerEvent::Tare:             lenOut = 1; return PAT_TARE;
        default:                            lenOut = 0; return nullptr;
    }
}

const char* Buzzer::eventKey(BuzzerEvent ev) {
    switch (ev) {
        case BuzzerEvent::TargetYield:      return "target";
        case BuzzerEvent::SmartSwitchFired: return "smartstop";
        case BuzzerEvent::Armed:            return "armed";
        case BuzzerEvent::BleConnected:     return "bleconn";
        case BuzzerEvent::BleDisconnected:  return "bledisc";
        case BuzzerEvent::BatteryLow:       return "battlow";
        case BuzzerEvent::BootReady:        return "boot";
        case BuzzerEvent::Tare:             return "tare";
        default:                            return "?";
    }
}

const char* Buzzer::eventLabel(BuzzerEvent ev) {
    switch (ev) {
        case BuzzerEvent::TargetYield:      return "Target yield reached";
        case BuzzerEvent::SmartSwitchFired: return "Smart switch stopped shot";
        case BuzzerEvent::Armed:            return "Armed for next shot";
        case BuzzerEvent::BleConnected:     return "Bluetooth connected";
        case BuzzerEvent::BleDisconnected:  return "Bluetooth disconnected";
        case BuzzerEvent::BatteryLow:       return "Battery low";
        case BuzzerEvent::BootReady:        return "Startup chime";
        case BuzzerEvent::Tare:             return "Tare";
        default:                            return "?";
    }
}

const char* Buzzer::eventDescription(BuzzerEvent ev) {
    switch (ev) {
        case BuzzerEvent::TargetYield:      return "One long beep when the weight reaches your target yield (dose x ratio). Same trigger as the OLED flash; needs Target Yield Ratio set above 0.";
        case BuzzerEvent::SmartSwitchFired: return "Two beeps when the smart switch cuts pump power via the Shelly relay.";
        case BuzzerEvent::Armed:            return "A short rising beep when the scale arms or auto-re-arms for the next shot.";
        case BuzzerEvent::BleConnected:     return "A quick double tick when a Bluetooth app (GaggiMate / Bean Conqueror) connects.";
        case BuzzerEvent::BleDisconnected:  return "A lower tone when the Bluetooth app disconnects.";
        case BuzzerEvent::BatteryLow:       return "Three descending beeps when the battery drops to low or critical.";
        case BuzzerEvent::BootReady:        return "A single chirp when the scale finishes starting up.";
        case BuzzerEvent::Tare:             return "A short tick every time the scale is tared — tap, hold, or the web Tare button.";
        default:                            return "";
    }
}

Buzzer::Buzzer(uint8_t pin, uint8_t invPin, uint8_t ledcChannel, uint16_t resonantHz,
               bool differential)
    : pin(pin), invPin(invPin), channel(ledcChannel), resonant(resonantHz),
      differential(differential) {
    for (uint8_t i = 0; i < (uint8_t)BuzzerEvent::COUNT; ++i) enabled[i] = true;
}

void Buzzer::begin() {
    // LEDC tone output. 8-bit resolution is plenty for a square-wave beep.
    ledcSetup(channel, resonant, 8);
    ledcAttachPin(pin, channel);

    // Optional differential (BTL) drive: route the SAME LEDC channel to a second
    // pin but inverted via the GPIO matrix. The disc then sees +V then -V across
    // its terminals (~2x the swing of single-ended), which is clearly louder.
    // One timer drives both pins, so the two halves stay perfectly 180° apart.
    if (invPin != 255) {
        pinMode(invPin, OUTPUT);
        digitalWrite(invPin, LOW); // idle: 0V across the disc until a beep plays
    }

    ledcWriteTone(channel, 0); // ensure silent at boot

    // Load persisted enables (default to the in-code values on first run).
    prefs.begin("buzzer", true); // read-only
    masterEnabled = prefs.getBool("master", masterEnabled);
    for (uint8_t i = 0; i < (uint8_t)BuzzerEvent::COUNT; ++i) {
        enabled[i] = prefs.getBool(eventKey((BuzzerEvent)i), enabled[i]);
    }
    prefs.end();
}

void Buzzer::silence() {
    ledcWriteTone(channel, 0);
    if (invPin != 255 && differential) {
        // Detach the inverted matrix and park the pin low so the disc sits at 0V
        // when idle (no standing DC bias across the ceramic). In single-ended mode
        // invPin is never matrix-attached — it stays a static GND, so nothing to do.
        pinMatrixOutDetach(invPin, false, false);
        digitalWrite(invPin, LOW);
    }
    playing = false;
    curSeq = nullptr;
}

void Buzzer::play(const Tone* seq, uint8_t len) {
    if (seq == nullptr || len == 0) return;
    curSeq = seq;
    curLen = len;
    curIdx = 0;
    stepStart = millis();
    playing = true;
    if (invPin != 255 && differential) {
        // Differential drive: route the same LEDC channel to invPin, inverted, so
        // the disc sees +V then -V across its terminals (~2x swing => louder).
        // One timer drives both pins, keeping them perfectly 180° apart.
        uint8_t sig = LEDC_LS_SIG_OUT0_IDX + (channel % SOC_LEDC_CHANNEL_NUM);
        pinMatrixOutAttach(invPin, sig, true /*invertOut*/, false);
    }
    // Single-ended mode leaves invPin held LOW (set in begin()), so the disc's
    // second terminal is a real ground reference and only `pin` swings (~half the
    // current of differential) — the same wiring, for a brownout A/B test.
    // Apply first step immediately.
    if (seq[0].freq > 0) ledcWriteTone(channel, seq[0].freq);
    else                 ledcWriteTone(channel, 0);
}

void Buzzer::trigger(BuzzerEvent ev) {
    if ((uint8_t)ev >= (uint8_t)BuzzerEvent::COUNT) return;
    // Safe to call from any task: only record the request. update() (loop task)
    // applies the enable checks and starts playback, so no two tasks ever touch
    // the LEDC peripheral or playback state at once. Last write wins, which
    // matches the old "a new trigger overrides any in-progress beep" behaviour.
    pendingEvent   = (uint8_t)ev;
    pendingTrigger = true;
}

void Buzzer::update() {
    // Consume a pending trigger first — all state/peripheral changes happen here.
    if (pendingTrigger) {
        pendingTrigger = false;
        BuzzerEvent ev = (BuzzerEvent)pendingEvent;
        if (masterEnabled && enabled[(uint8_t)ev]) {
            uint8_t len = 0;
            const Tone* seq = patternFor(ev, len);
            play(seq, len); // a new trigger overrides any in-progress beep
        }
    }

    if (!playing || curSeq == nullptr) return;
    if (millis() - stepStart < curSeq[curIdx].ms) return;

    // Current step elapsed — advance.
    curIdx++;
    if (curIdx >= curLen) {
        silence();
        return;
    }
    stepStart = millis();
    uint16_t f = curSeq[curIdx].freq;
    if (f > 0) ledcWriteTone(channel, f);
    else       ledcWriteTone(channel, 0);
}

void Buzzer::setEventEnabled(BuzzerEvent ev, bool en) {
    if ((uint8_t)ev < (uint8_t)BuzzerEvent::COUNT) enabled[(uint8_t)ev] = en;
}

bool Buzzer::getEventEnabled(BuzzerEvent ev) const {
    if ((uint8_t)ev >= (uint8_t)BuzzerEvent::COUNT) return false;
    return enabled[(uint8_t)ev];
}

void Buzzer::save() {
    prefs.begin("buzzer", false); // read-write
    prefs.putBool("master", masterEnabled);
    for (uint8_t i = 0; i < (uint8_t)BuzzerEvent::COUNT; ++i) {
        prefs.putBool(eventKey((BuzzerEvent)i), enabled[i]);
    }
    prefs.end();
}
