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
| **Tap** | Zero the scale. If a brew just finished (timer paused), also resets the timer ready for the next shot. |
| **Hold 0.5 s** | Tare + **arm** the auto-start + save current cup weight to memory. OLED shows **"Armed / Ready!"**. |

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
- **Top-right corner** — Bluetooth status: `BT` plain when scanning, `[BT]` boxed when connected
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
- Hold the tare button for 0.5 s (before starting your machine). The scale tares simultaneously and saves your cup weight to memory.

**Trigger:**
- The timer starts automatically when the weight on the scale increases by more than 1 g and remains above that threshold for 0.5 s. This corresponds to the first drip hitting the cup.

**Auto re-arm:**
- On your next shot, simply place the same cup on the scale and tap tare as normal. If the pre-tare weight matches the saved cup weight within ±5 g, the device arms automatically without needing a hold gesture.

**Disarm:**
- The armed state expires after 2 minutes of no drip activity.
- Starting the timer manually (timer button) also clears the armed state.
- The saved cup weight persists across reboots.

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
- **Tare** / **Stop** / **Reset** buttons
- Real-time weight/flow rate graph (records while timer is running)
- Brew ratio shown after timer stops
- Shot history table

### Settings

| Section | What you can configure |
|---------|----------------------|
| Wi-Fi | SSID and password for home network |
| Display | Decimal places (0/1/2) |
| Sleep | Inactivity timeout (minutes) |
| Brew Automation | Auto-tare on vessel placement, post-brew idle reset, target yield ratio |
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
