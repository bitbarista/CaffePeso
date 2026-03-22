# CaffePeso — User Guide

## Overview

CaffePeso is an ESP32-S3 based coffee scale with a real-time OLED display, brew timer, flow rate measurement, and a Wi-Fi web interface. It is designed around two physical buttons and a strain gauge load cell.

---

## Hardware

| Component | Detail |
|-----------|--------|
| Microcontroller | ESP32-S3 SuperMini |
| Display | SSD1306 128×32 OLED |
| Load cell interface | HX711 |
| Tare button | GPIO 4 |
| Timer/power button | GPIO 3 |
| Battery monitoring | GPIO 7 (ADC) |

---

## Buttons — Complete Reference

### Tare button (GPIO 4)

| Gesture | Action |
|---------|--------|
| **Tap** | Zero the scale. Resets the timer if a brew just finished. |
| **Hold 1.5 s** | Save cup weight + tare + **arm** auto-start. Three-stage feedback: "Taring..." at 0.5 s (keep holding) → "Release Now!" at 1.5 s → release → OLED flashes inverted **"ARMED / Ready!"**. |

### Timer/Power button (GPIO 3)

| Gesture | Action |
|---------|--------|
| **Tap** (timer stopped) | Start timer |
| **Tap** (timer running) | Stop/pause timer |
| **Tap** (timer paused) | Reset timer |
| **Hold 3 s** | Start sleep countdown |

---

## OLED Display Layout

```
┌──────────────────────────────────┐
│ 85%      18.52              [BT] │  ← battery% | weight (centred, large) | BT status
│ 30.2T    1:2.1    1.4F           │  ← timer | running ratio | flow rate
└──────────────────────────────────┘
```

- **Top-left corner** — battery percentage (small)
- **Top row (large, centred)** — current weight in grams
- **Top-right corner** — `[BT]` shown only when Bluetooth is connected; blank otherwise
- **Bottom left** — elapsed timer. Format: `SS.t` under 60 s, `M:SS` over 60 s, followed by `T`
- **Bottom centre** — live brew ratio (`1:X`) while weight is building, if dose is set
- **Bottom right** — live flow rate (`g/s`) while brewing; final brew ratio (`1:X R`) after timer stops

---

## Workflows

### Espresso — step by step

1. Place scale on drip tray. Power on.
2. Place empty portafilter on scale → **tap tare** to zero.
3. Add beans to portafilter. Read dose on display.
4. Remove portafilter and grind.
5. **Tap tare** to zero (removes any residual offset).
6. Place cup under group head.
7. **Hold tare 0.5 s** — the scale tares and arms. OLED shows **"Armed / Ready!"**\
   The timer will now start automatically the moment coffee begins to drip (weight increases > 1 g sustained for 0.5 s).
8. Start your espresso machine.
9. First drip hits the cup → **timer starts automatically**.
10. Extraction runs. Flow rate and timer update live.
11. When the target yield alert fires (if configured): OLED flashes inverted for 1 second; web UI weight turns amber.
12. Remove cup → **timer stops automatically** (cup removal detected).
13. Display shows elapsed time and brew ratio.
14. **Tap tare** → timer resets, scale zeroes, device re-arms automatically if the same cup is detected.

> **Next shot:** If you place the same cup back on the scale and tap tare, the device recognises the cup weight and arms automatically — no hold gesture needed.

---

### Pour Over — step by step

1. Place scale on counter. Power on.
2. Place server/jug on scale → **tap tare**.
3. Place dripper on server → **tap tare**.
4. Add filter and rinse if desired.
5. Add ground coffee. Read dose.
6. **Tap tare** to zero.
7. **Press timer button** to start timer.
8. Begin bloom pour. Timer runs.
9. Rest during bloom — timer continues running.
10. Continue pulse pours as required. Timer continues running throughout.
11. When dripper is empty, lift it off → **timer stops automatically**.
12. Display shows total brew time and ratio.
13. **Tap tare** to reset for next brew.

---

## Armed Auto-Start

Armed auto-start removes the need to manually press the timer button at the start of a shot.

**To arm:**
- Hold the tare button until "Release Now!" appears (~1.5 s), then release. The display shows "Taring..." while you hold, then "Release Now!" when you can let go. After release the scale settles, captures the cup weight, tares, and shows the inverted **"ARMED / Ready!"** screen.
- The cup weight is saved to memory automatically.

**Trigger:**
- The timer starts automatically when the weight on the scale increases by more than 1 g and remains above that threshold for 0.5 s. This corresponds to the first drip hitting the cup.

**Auto re-arm — no button press needed:**

Once a cup weight is saved, the scale re-arms automatically whenever it detects the cup returning. Two scenarios are handled:

- **Cup removed and placed straight back** — scale went negative, cup returns to ~0 g → "ARMED" flashes automatically.
- **Cup removed, empty scale tapped to clear, cup placed back** — scale reads ~saved cup weight → "ARMED" flashes automatically.

The saved cup weight persists across reboots (NVS).

> **Note:** Auto re-arm and Auto-Tare on Vessel Placement are mutually exclusive. If auto-tare is enabled, it fires its own tare on cup placement and the cup weight matching logic cannot run. Disable auto-tare to use auto re-arm.

**Disarm:**
- The armed state expires after 2 minutes of no drip activity.
- Starting the timer manually (timer button) also clears the armed state.
- Press the **Arm** button on the web UI dashboard to disarm remotely.
- The saved cup weight persists across reboots.

---

## Pre-Infusion Timing Mode

**Configuration:** Settings → Brew Automation → Pre-Infusion Timing (toggle)

When enabled, the timer starts **immediately when you arm** the scale, rather than waiting for the first drip. This captures the full pre-infusion phase in your shot time.

Use this if your machine has a notable pre-infusion phase and you want total extraction time to include it.

When disabled (default), the timer waits for the first drip (weight increases > 1 g sustained for 0.5 s) before starting.

---

## Auto-Stop on Flow Cessation

**Configuration:** Settings → Brew Automation → Auto-Stop on Flow Cessation (toggle)

When enabled, the timer stops automatically when espresso flow ends — no need to manually stop it.

**Conditions:**
- Flow must first exceed **1.0 g/s** (confirms brewing is active)
- Brew must have been running for at least **8 seconds** (prevents false stops during pre-infusion)
- Flow must then drop below **0.5 g/s** and remain there for **2 seconds**

Works alongside armed auto-start for a fully hands-free timing experience.

---

## Target Yield Alert

Alerts you when the yield in the cup is approaching your target ratio, so you can be ready to stop the shot.

**Configuration:** Settings → Brew Automation → Target Yield Ratio

| Value | Meaning |
|-------|---------|
| `0` | Disabled |
| `2.0` | 1:2 ratio (e.g. 18 g dose → 36 g yield) |
| `2.5` | 1:2.5 ratio (e.g. 18 g dose → 45 g yield) |

**Alert fires at:** `dose × ratio − 2 g` (2 g before the exact target, giving you time to react).

**What happens:**
- **OLED**: entire display inverts (white on black) for 1 second
- **Web UI**: weight display turns amber

> Requires dose to be set. Enter your dose in the Dose field on the dashboard before brewing.

---

## Auto-Tare on Vessel Placement

**Configuration:** Settings → Brew Automation → Auto-Tare on Vessel Placement (toggle + threshold)

When enabled, the scale tares automatically when a stable weight above the threshold is detected. Useful if you want a completely hands-free setup where placing the cup zeroes the scale without touching anything.

**How it works:**
- Weight must exceed the configured threshold (default 20 g) and remain stable for ~0.6 s
- Does not fire while the timer is running or while armed
- Resets (ready to fire again) only when the scale returns to near-zero (< 2 g)

**Threshold guidance:** Set this above your empty cup weight but below any weight you'd place deliberately (e.g. if your cup is 150 g, a threshold of 100 g would work). The default 20 g is intentionally low — raise it if it fires unexpectedly on vibration or light contact.

> **Incompatible with auto re-arm.** If auto-tare is on, placing the cup fires an automatic tare and bypasses the manual tap tare that triggers re-arm. Disable auto-tare if you want to use the cup weight memory / auto re-arm feature.

---

## Shot History

The last 10 shots are automatically saved to non-volatile memory and displayed in a table on the dashboard.

**A shot is recorded when:**
- The timer stops (cup removed or manual stop via timer button or web UI Stop button)
- Dose > 0.5 g, yield > 5 g, and brew time > 3 s

**Table columns:** Shot #, Dose (g), Yield (g), Time (s), Ratio

**Clear history:** Click the **Clear** link next to "Shot History" on the dashboard.

Shot history survives reboots and power loss.

---

## Web Interface

Connect to the device's Wi-Fi network, then open a browser to the device's IP address.

### Dashboard

- Live weight, flow rate, and timer
- **Dose** entry — enter your dose in grams and press Set before brewing
- **Tare** / **Arm** / **Stop** / **Reset** buttons
  - **Arm** tares the scale and arms auto-start; press again to disarm. Turns amber when armed.
- Real-time weight/flow rate graph (records while timer is running)
- Brew ratio shown after timer stops
- Shot history table

### Settings

| Section | What you can configure |
|---------|----------------------|
| Wi-Fi | SSID and password for home network |
| Display | Decimal places (0/1/2) |
| Sleep | Inactivity timeout (minutes) |
| Brew Automation | Auto-tare on vessel placement, post-brew idle reset, pre-infusion timing, auto-stop on flow cessation, target yield ratio |
| Filter | Advanced HX711 filter tuning |

### Calibration

Step-by-step scale calibration using a known weight. Access via the Calibration page.

### Updates

OTA firmware upload. Upload a new `.bin` file directly from the browser without a USB cable.

---

## Wi-Fi

**Default (AP mode):** On first boot or if no network credentials are saved, the device creates its own Wi-Fi access point.

| Setting | Value |
|---------|-------|
| SSID | `WeighMyBru-AP` |
| Default IP | `192.168.4.1` |

**Connecting to your home network:**
1. Connect to the device's AP
2. Open `192.168.4.1` in a browser
3. Go to Settings → Wi-Fi Configuration
4. Enter your SSID and password and save

Once connected to your home network, find the device's assigned IP address from your router's DHCP table, or read it from the OLED on the next boot (the boot screen shows the IP address below "CaffePeso").

---

## Power / Sleep

- **Inactivity sleep:** Device enters deep sleep after a configurable period of no button activity (default 10 minutes). Configurable in Settings → Sleep.
- **Manual sleep:** Hold the timer button for 3 s. OLED counts down 3…2…1 then the display blanks and the device sleeps.
- **Cancel sleep:** Touch either button during the countdown.
- **Wake:** Touch either button. Device restarts from boot.
- **Battery low:** OLED shows "Bat Low" with voltage on boot. Device sleeps immediately if voltage is below 3.2 V.

---

## Factory Reset

Hold the **tare button** while powering on. This clears saved Wi-Fi credentials. All other settings (calibration, dose, cup weight, shot history) are unaffected.

To clear all NVS settings including calibration, use the hidden endpoint:\
`POST /api/reset-nvs` with body `confirm=yes`
