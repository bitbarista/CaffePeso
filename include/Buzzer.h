#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
#include <Preferences.h>

// Audible-feedback events. Each maps to a beep pattern (see Buzzer.cpp).
// Keep this enum, eventKey() and eventLabel() in sync.
enum class BuzzerEvent : uint8_t {
    TargetYield = 0,    // weight approaching dose x target ratio (mirrors OLED flash)
    SmartSwitchFired,   // Shelly relay cut by smart switch
    Armed,              // armed / auto-re-armed for next shot
    BleConnected,       // GaggiMate / Bean Conqueror connected
    BleDisconnected,    // BLE client dropped
    BatteryLow,         // battery entered low/critical
    BootReady,          // firmware finished booting
    Tare,               // scale tared (tap, hold, or web button)
    COUNT
};

class Buzzer {
public:
    struct Tone { uint16_t freq; uint16_t ms; }; // freq == 0 -> silent gap

    // pin: GPIO wired to the piezo (non-inverted half).
    // invPin: second GPIO driven in antiphase for differential/BTL drive — roughly
    //         doubles the voltage swing across the disc (louder). 255 = single-ended.
    // channel: LEDC channel (0-15, must be unused). resonantHz: disc resonance (loudest).
    // differential: true = drive invPin in antiphase (BTL, louder, ~2x current). false =
    //   hold invPin statically at GND so only `pin` drives (single-ended, ~half current).
    //   Lets the same wiring A/B-test a suspected brownout without resoldering to GND.
    explicit Buzzer(uint8_t pin, uint8_t invPin = 255, uint8_t ledcChannel = 0,
                    uint16_t resonantHz = 4200, bool differential = true);

    void begin();                  // configure LEDC + load enables from NVS
    void update();                 // non-blocking pattern player — call every loop()
    void trigger(BuzzerEvent ev);  // play the event's pattern if enabled
    void silence();                // stop any tone immediately (e.g. before sleep)

    // Master + per-event enable. Changes are persisted by save().
    void setMasterEnabled(bool en) { masterEnabled = en; }
    bool getMasterEnabled() const  { return masterEnabled; }
    void setEventEnabled(BuzzerEvent ev, bool en);
    bool getEventEnabled(BuzzerEvent ev) const;
    void save();                   // persist master + all enables to NVS

    bool isPlaying() const { return playing; }

    // Stable identifiers for web API / NVS keys, human labels and tooltip text.
    static const char* eventKey(BuzzerEvent ev);
    static const char* eventLabel(BuzzerEvent ev);
    static const char* eventDescription(BuzzerEvent ev);

private:
    void play(const Tone* seq, uint8_t len);
    static const Tone* patternFor(BuzzerEvent ev, uint8_t& lenOut);

    uint8_t  pin;
    uint8_t  invPin;
    uint8_t  channel;
    uint16_t resonant;
    bool     differential; // true = antiphase BTL drive; false = invPin held at GND

    bool masterEnabled = true;
    bool enabled[(uint8_t)BuzzerEvent::COUNT];

    const Tone*   curSeq   = nullptr;
    uint8_t       curLen   = 0;
    uint8_t       curIdx   = 0;
    unsigned long stepStart = 0;
    bool          playing  = false;

    // trigger() may be called from the async web-server task (e.g. /api/tare) as
    // well as loop(). To keep the LEDC peripheral and the playback state on a
    // single task, trigger() only records a request in these volatiles; update()
    // (loop task only) consumes it and does all the hardware work. This removes a
    // cross-task data race / concurrent peripheral access that could crash.
    volatile bool    pendingTrigger = false;
    volatile uint8_t pendingEvent   = 0;

    Preferences prefs;
};

#endif // BUZZER_H
