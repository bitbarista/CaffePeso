# CaffePeso User Guide

**Version 2.4.3**

---

*Weigh smarter. Pull better.*

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Hardware Overview](#2-hardware-overview)
3. [First-Time Setup](#3-first-time-setup)
4. [Physical Controls](#4-physical-controls)
5. [OLED Display](#5-oled-display)
6. [Web Interface](#6-web-interface)
7. [Brewing Workflows](#7-brewing-workflows)
8. [Brew Automation Features](#8-brew-automation-features)
9. [Settings Reference](#9-settings-reference)
10. [Calibration](#10-calibration)
11. [Firmware Updates](#11-firmware-updates)
12. [Power and Battery](#12-power-and-battery)
13. [GaggiMate Bluetooth Integration](#13-gaggimate-bluetooth-integration)
14. [Troubleshooting](#14-troubleshooting)

---

## 1. Introduction

CaffePeso is a smart espresso scale built on the ESP32-S3 microcontroller. It combines a precision load cell, a live OLED display, and a Wi-Fi web interface to give you real-time weight, flow rate, timer, and brew ratio — all in a compact, battery-powered unit that sits under your cup.

It is designed to work seamlessly at the espresso machine without requiring a phone or app. Every parameter you need to pull a consistent shot is either visible on the OLED or accessible through a browser on your home network.

**Key capabilities:**

- Real-time weight display with configurable decimal places
- Brew timer with automatic start on first drip (armed auto-start)
- Live flow rate (g/s) and brew ratio (1:X)
- Target yield alert — OLED flashes when approaching your target
- Shot history — last 10 shots stored through power cycles
- Cup weight auto re-arm — recognises your cup and arms itself for the next shot
- Auto-tare on vessel placement
- Auto-stop when espresso flow ceases
- Wi-Fi web interface with real-time graph, calibration, settings, and OTA updates
- GaggiMate Bluetooth scale compatibility

---

## 2. Hardware Overview

### 2.1 Supported Boards

| Board | Flash | Notes |
|-------|-------|-------|
| ESP32-S3 SuperMini | 4 MB | Default — recommended |
| XIAO ESP32S3 | 8 MB | Alternative |

### 2.2 Pin Assignments

| Function | GPIO | Notes |
|----------|------|-------|
| HX711 Data | 5 | Load cell amplifier data |
| HX711 Clock | 6 | Load cell amplifier clock |
| Tare button | 4 | Capacitive touch module |
| Sleep/timer button | 3 | Capacitive touch module |
| Battery voltage | 7 | ADC via 2× 100 kΩ voltage divider |
| I²C SDA (display) | 8 | SSD1306 OLED |
| I²C SCL (display) | 9 | SSD1306 OLED |

### 2.3 Bill of Materials

| Qty | Component |
|-----|-----------|
| 1 | 500 g I-shaped mini load cell |
| 1 | HX711 load cell amplifier module |
| 1 | ESP32-S3 SuperMini (or XIAO ESP32S3) |
| 1 | 0.91″ SSD1306 OLED display (128×32, I²C) |
| 2 | Capacitive touch pad modules |
| 1 | 800 mAh Li-ion battery |
| 1 | JST-PH 2.0 male connector |
| 1 | 5 mm slide switch (power) |
| 2 | 100 kΩ 1% resistors (battery voltage divider) |
| 4 | Self-adhesive rubber feet |

Purchase links for all components are available in the [WeighMyBru² repository](https://github.com/031devstudios/weighmybru2) — the hardware is identical.

### 2.4 Wiring

GPIO pin numbers are identical on both supported boards. The physical pin *locations* differ — refer to the pinout diagram for your specific board when connecting wires.

#### HX711 Load Cell Amplifier

| HX711 Pin | Connect to |
|-----------|-----------|
| VCC | 3.3 V |
| GND | GND |
| DT | GPIO 5 |
| SCK | GPIO 6 |

Connect the load cell wires to the HX711 E+/E−/A+/A− terminals. Load cell wire colours vary by manufacturer — consult the datasheet for your specific cell (typically: red = E+, black = E−, white = A−, green = A+).

#### OLED Display (SSD1306, I²C)

| Display Pin | Connect to |
|-------------|-----------|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO 8 |
| SCL | GPIO 9 |

#### Capacitive Touch Pads

| Touch Pad | Signal Pin | Connect to |
|-----------|-----------|-----------|
| Tare button | SIG | GPIO 4 |
| Sleep/timer button | SIG | GPIO 3 |

Both touch pads share 3.3 V and GND.

#### Battery Voltage Divider and Power Switch

The battery voltage is monitored via a resistor divider on GPIO 7 (ADC), halving the voltage to fall within the 3.3 V ADC range. The divider connects directly to Battery (+) — before the switch — so the low-battery check can run at boot. Use 1% tolerance resistors for accurate readings.

The slide switch is wired in series between Battery (+) and the VCC rail. When open, the device is fully powered off.

```
Battery (+) ──┬── [Switch] ── VCC rail
              │
              └── 100 kΩ ──┬── 100 kΩ ── GND
                           │
                         GPIO 7
```

#### Complete Circuit Schematic

```
+=========================================================================+
|                       CaffePeso -- Circuit Schematic                    |
+=========================================================================+

POWER
                              .-- [SW1 Power Switch] ----- VCC (3.3V)
  Battery (+) --------+------'
                      |
                      +-- [R1 100k] --+-- [R2 100k] ------- GND
                                      |
                                    GPIO7 (ADC)

  Battery (-) -------------------------------------------------- GND


LOAD CELL & HX711

  Load Cell             HX711 Module               ESP32-S3
  +------------+       +-------------+
  | E+  (RED)  |--E+---| E+  VCC     |------------ 3.3V
  | E-  (BLK)  |--E----|  E- GND     |------------ GND
  | A-  (WHT)  |--A----|  A- DT      |------------ GPIO5
  | A+  (GRN)  |--A+---|  A+ SCK     |------------ GPIO6
  +------------+       +-------------+


OLED DISPLAY (SSD1306, 0.91" I2C)

  +-------+
  |  VCC  |------ 3.3V
  |  GND  |------ GND
  |  SDA  |------ GPIO8
  |  SCL  |------ GPIO9
  +-------+


TOUCH PADS (TTP223B capacitive modules)

  Tare Button                    Sleep / Timer Button
  +-------+                      +-------+
  |  VCC  |------ 3.3V           |  VCC  |------ 3.3V
  |  GND  |------ GND            |  GND  |------ GND
  |  SIG  |------ GPIO4          |  SIG  |------ GPIO3
  +-------+                      +-------+


All GND labels share a common ground.
All 3.3V labels connect to the VCC rail (Battery+ -> SW1 ->
ESP32-S3 built-in regulator).
```

---

## 3. First-Time Setup

### 3.1 Flash the Firmware

**Recommended — browser-based (no software required):**

1. Visit the [CaffePeso flash page](https://bitbarista.github.io/CaffePeso/flash.html) in Chrome, Edge, or Opera.
2. Connect your ESP32-S3 board via USB.
3. Click **Install Firmware**, select your board type (SuperMini or XIAO), and follow the prompts.
4. Both firmware and web interface files are installed in a single step.

**Alternative — PlatformIO:**

```bash
# Flash firmware
pio run -e esp32s3-supermini --target upload

# Flash web interface files (required separately)
pio run -e esp32s3-supermini --target uploadfs
```

> **Important:** Always use the same environment for firmware and filesystem. Mixing environments (e.g. xiao filesystem with supermini firmware) causes a crash at boot due to flash size differences.

### 3.2 Connect to Wi-Fi

On first boot, CaffePeso starts in **Access Point mode**:

1. The OLED displays the AP IP address (`192.168.4.1`).
2. Connect your phone or computer to the Wi-Fi network **CaffePeso** (no password).
3. Open a browser and navigate to `http://192.168.4.1`.
4. Go to **Settings → Wi-Fi Configuration**.
5. Either scan for networks or type your SSID and password, then click **Save Wi-Fi Settings**.
6. The device connects and switches to Station mode. The OLED displays the new IP address.
7. Reconnect your device to your home network and navigate to the displayed IP address.

> **Tip:** Assign a static DHCP lease to CaffePeso's MAC address on your router so the IP address never changes.

### 3.3 Calibrate the Scale

Before first use, the scale must be calibrated to your specific load cell. See [Section 10 — Calibration](#10-calibration) for the full procedure.

### 3.4 Set Your Dose Weight

Open the **Dashboard** in the web interface and enter your coffee dose in the **Dose** field, then click **Set**. This value is persisted through power cycles and is used for live brew ratio display and shot history logging.

---

## 4. Physical Controls

CaffePeso has two capacitive touch buttons. They are labelled here as **Tare** and **Timer**.

### 4.1 Tare Button

The Tare button zeros the scale and, with a longer hold, arms the auto-start timer.

| Gesture | Action |
|---------|--------|
| **Tap** | Zero the scale. Resets the timer if not mid-brew. |
| **Hold ~0.5 s** | OLED shows **"Taring…"** — keep holding as a prompt. |
| **Hold ~1.5 s** | OLED shows **"Release!"** |
| **Release after 1.5 s** | Tare + arm. Saves cup weight, arms for auto-start. OLED shows **"Armed"** (inverted display). |

After a tap-tare, the scale waits ~1.5 seconds before executing the tare. This brief delay gives you time to lift your hand cleanly so press force does not skew the zero reference.

After a hold-tare (arm), the scale waits ~0.3 seconds after release before reading the cup weight, for the same reason.

### 4.2 Timer Button

The Timer button controls the brew timer and puts the device to sleep.

| Gesture | Action |
|---------|--------|
| **Tap** | Cycle timer: **Stopped → Running → Paused → Stopped** |
| **Hold 3 s** | Start sleep countdown (3–2–1 on OLED). Touch again during countdown to cancel. |

**Waking from sleep:** Touch either button. The device boots in a few seconds.

### 4.3 Factory Reset

To clear all Wi-Fi credentials, hold the **Tare** button while powering on (slide the power switch whilst keeping the tare pad touched). The OLED flashes and the device restarts in Access Point mode.

---

## 5. OLED Display

The 0.91″ SSD1306 display (128×32 pixels) shows a two-row layout during normal operation.

### 5.1 Main Display Layout

```
┌────────────────────────────┐
│  45.3g              BT [■■]│  ← weight (large), status icons top-right
│  0:28.5   2.1 g/s   1:2.2  │  ← timer, flow rate, ratio (or avg flow)
└────────────────────────────┘
```

**Top row:** Current weight in grams. Size scales with the value.

**Bottom row (during brew):** Timer elapsed time · Flow rate (g/s) · Live brew ratio (1:X)

**Bottom row (after brew):** Timer elapsed time · Average flow rate for the shot

**Status icons (top right):**
- **BT** — Bluetooth connected to GaggiMate
- **Battery** — 3-segment indicator. Flashes when low.

### 5.2 OLED Messages

| Message | Trigger |
|---------|---------|
| **Taring…** | Tare button held past 0.5 s — keep holding |
| **Release!** | Tare button held past 1.5 s — release now |
| **Scale Tared** (normal) | Tap-tare completed |
| **Scale Tared** (inverted) | Auto-tare or cup-weight auto re-arm tare completed |
| **Armed** (inverted) | Hold-tare arm completed, or auto re-arm triggered |
| **Battery Low** | Battery voltage below threshold at boot |
| **Going to Sleep** | Sleep countdown finished, entering deep sleep |
| **Sleep Cancelled** | Timer button touched during sleep countdown |
| Countdown **3… 2… 1…** | Sleep countdown in progress |

An inverted display (white background, black text) is used whenever an automatic tare or arm fires — visually distinguishing automated actions from manual ones.

---

## 6. Web Interface

The web interface is served directly from the ESP32-S3 over Wi-Fi. Open a browser and navigate to the device IP address shown on the OLED at boot.

> **Tip:** The interface can be installed as a Progressive Web App (PWA) on Android and iOS — use "Add to Home Screen" from your browser menu for quick access.

### 6.1 Dashboard

The Dashboard is the main working screen during brewing.

**Real-time metrics (updated 10 times per second):**

| Field | Description |
|-------|-------------|
| **Weight** | Current weight in grams |
| **Flow Rate** | Rate of weight increase in g/s |
| **Time** | Elapsed brew time (mm:ss.mmm) |
| **Brew Ratio** | Live 1:X ratio based on dose and current yield |
| **Avg Flow Rate** | Average flow rate for the completed shot (shown after brew) |

**Status indicators (header bar):**
- Wi-Fi signal strength and quality
- Bluetooth connection status
- Scale connection status
- Battery level and percentage

**Live graph:** Weight and flow rate plotted over time. Recording follows the timer — starts when the timer starts, stops when it stops.

**Dose and Cup Weight inputs:** Enter your coffee dose and cup weight here, then click **Set**. Both values are persisted through reboots.

**Action buttons:**

| Button | Action |
|--------|--------|
| **Tare** | Zero the scale via the web interface |
| **Arm** / **Disarm** | Arm or disarm the auto-start timer |
| **Stop** | Stop the brew timer |
| **Reset** | Reset the timer to 00:00 |

**Shot History:** The last 10 shots are shown in a table with dose, yield, time, and ratio. Click **Clear** to erase the history.

**Target yield alert:** When the current weight approaches the target yield (dose × target ratio), the weight display turns amber and the OLED flashes briefly.

### 6.2 Calibration

See [Section 10 — Calibration](#10-calibration).

### 6.3 Settings

All persistent settings are configured here. Settings are grouped into sections — see [Section 9 — Settings Reference](#9-settings-reference) for full details.

### 6.4 Updates

Shows the current firmware version, board, and build date. Provides Over-The-Air (OTA) firmware upload. See [Section 11 — Firmware Updates](#11-firmware-updates).

---

## 7. Brewing Workflows

### 7.1 Basic Manual Workflow

1. Place your cup on the scale.
2. **Tap Tare** to zero.
3. Start your espresso machine.
4. **Tap Timer** when the first drip hits the cup.
5. **Tap Timer** again to stop when you reach your target weight.
6. The OLED shows elapsed time and average flow rate.

### 7.2 Armed Auto-Start Workflow

This is the recommended workflow. The timer starts automatically on the first drip — no button press at the machine needed.

1. Place your cup on the scale.
2. **Hold Tare for ~1.5 s** until you see **"Release!"**, then release.
   - The scale tares with the cup weight zeroed.
   - The cup weight is saved.
   - The OLED shows **"Armed"** (inverted display).
3. Start your espresso machine.
4. The timer starts automatically when the first drip exceeds ~1 g.
5. Stop the pump manually when you reach your target weight.
   - Or enable **Target Yield Alert** to get a visual cue.
   - Or enable **Auto-Stop on Flow Cessation** to stop the timer automatically.

### 7.3 Repeated Shots — Auto Re-Arm Workflow

With **Cup Weight Auto Re-arm** enabled (the default), CaffePeso recognises your cup and arms itself automatically for the next shot.

**After the first shot (armed auto-start):**

1. Remove the cup when the shot is done.
2. Knock out the spent puck, rinse the portafilter, grind and tamp a fresh dose.
3. Place the same cup back on the scale.
   - The scale detects the cup weight, tares, and arms — OLED shows **"Armed"** (inverted).
4. Start the next shot. The timer starts on the first drip.

This works because CaffePeso remembers the cup weight from the hold-tare. It compares the weight of whatever is placed on the scale against the saved value, and arms automatically when they match.

> **Note:** Auto re-arm requires a step-change in weight (the cup being placed on the scale) to fire. Milk being gradually poured through the cup weight window does not trigger re-arm. This prevents false triggers during milk-based drink preparation.

### 7.4 Tap-Tare Workflow (No Auto Re-arm)

If you prefer to arm manually each time:

1. Tap Tare to zero the scale with the cup on it.
2. Press **Arm** in the web interface, or hold-tare, to arm.
3. Proceed as in the armed auto-start workflow.

---

## 8. Brew Automation Features

### 8.1 Armed Auto-Start

When armed, the timer starts automatically when weight increases above ~1 g and stays there for 500 ms. This captures the precise moment the first drip hits the cup without requiring a button press at the machine.

**To arm:** Hold the Tare button for ~1.5 s until **"Release!"** appears, then release. The OLED confirms with **"Armed"** on an inverted display.

**To arm via the web interface:** Click the **Arm** button on the Dashboard.

**Arm timeout:** If no drip is detected within 2 minutes of arming, the scale disarms automatically.

### 8.2 Cup Weight Auto Re-Arm

Automatically arms the scale when the saved cup weight is placed on it, eliminating the need to manually arm between shots.

**Requirements:**
- A cup weight must have been saved by at least one hold-tare.
- **Cup Weight Auto Re-arm** must be enabled in Settings → Brew Automation.

**Detection logic:**
The scale monitors weight against the saved cup weight (±5 g window) and requires a step-change of at least 8 g per update cycle to count as a cup placement. This prevents a gradual pour (e.g. milk) from falsely triggering re-arm.

**Re-arm paths:**
- **Tared path:** After a tap-tare (scale reads 0 g), placing the cup reads approximately the saved cup weight. Step-change detected → stable 200 ms → auto re-arm.
- **Direct path:** After removing the cup (scale goes very negative), placing it back directly (without tap-taring) reads ~0 g against the original cup-tare reference. Scale went negative → weight returns near 0 → auto re-arm.

### 8.3 Pre-Infusion Timing Mode

When enabled, the timer starts immediately when the scale is armed — capturing the pre-infusion phase. When disabled (default), the timer starts on the first drip.

Enable in **Settings → Brew Automation → Pre-Infusion Timing**.

Use this if your machine has a notable pre-infusion phase (a period of low pressure before full extraction pressure) that you want included in the total shot time.

### 8.4 Auto-Stop on Flow Cessation

When enabled, the timer stops automatically when espresso flow ceases — no manual stop needed at the end of a shot.

**How it works:**
- Flow must first exceed 1.0 g/s (confirming extraction is underway).
- Flow must then drop below 0.5 g/s and remain there for 2 seconds.
- This check does not engage within the first 8 seconds of the brew (prevents false stops during pre-infusion or slow starts).

Enable in **Settings → Brew Automation → Auto-Stop on Flow Cessation**.

### 8.5 Auto-Tare on Vessel Placement

When enabled, the scale automatically tares whenever a stable weight above the configured threshold is placed on it — useful if you always use the same vessel and don't want to think about taring.

**How it works:**
- When a weight above the threshold is detected, a 600 ms stability timer starts.
- If the weight remains stable for 600 ms, the scale tares automatically.
- The OLED shows **"Scale Tared"** on an inverted display (same visual cue as armed auto-start).
- Auto-tare does not re-fire mid-brew when yield crosses the same threshold — it locks after firing and resets only when the vessel is removed (scale reads negative).

**Interaction with Cup Weight Auto Re-arm:** When both features are enabled, the auto re-arm fires first (200 ms stability requirement vs 600 ms for auto-tare). The tracked cup triggers re-arm, not auto-tare. Other vessels trigger auto-tare normally.

Enable in **Settings → Brew Automation → Auto-Tare on Vessel Placement**.

Configure the **Minimum Weight to Trigger** (default: 20 g) to suit your lightest vessel.

### 8.6 Post-Brew Idle Reset

After a brew finishes (timer paused), if no weight change is detected for a configurable period, the scale auto-resets and re-tares — ready for the next drink without intervention.

Enable in **Settings → Brew Automation → Post-Brew Idle Reset**.

Configure the **Idle Timeout** (5–300 seconds, default 30 s).

> **Note:** If Cup Weight Auto Re-arm is enabled, the idle reset skips the re-tare step — re-taring would corrupt the cup weight reference and prevent auto re-arm from recognising the cup.

### 8.7 Target Yield Alert

When a dose and target ratio are configured, the scale alerts you as the brew approaches the target yield.

- The **weight display on the Dashboard turns amber** when current weight approaches the target.
- The **OLED flashes** (inverts briefly) at the target weight.

Configure in **Settings → Brew Automation → Target Yield Ratio**.

Set the ratio as a decimal multiplier — e.g. `2.0` for a 1:2 ratio (18 g dose → 36 g yield), `2.5` for a 1:2.5 ratio.

Set to `0` to disable.

---

## 9. Settings Reference

All settings are accessed via the web interface at **Settings**.

### 9.1 Wi-Fi Configuration

| Setting | Description |
|---------|-------------|
| **SSID** | Your home Wi-Fi network name |
| **Password** | Your Wi-Fi password |
| **Scan Networks** | Scan for nearby networks and select from a list |
| **Clear Wi-Fi Credentials** | Remove stored credentials and return to AP mode |

After saving, the device connects to the new network immediately. If connection fails, it returns to AP mode.

### 9.2 Display Settings

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| **Decimal Places** | 0, 1, 2 | 1 | Number of decimal places shown on the OLED weight display |

### 9.3 Filtering Settings

These control how the scale readings are processed. The defaults are suitable for most setups and rarely need adjustment.

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| **Brewing Detection Threshold** | 0.05–1.0 g | 0.15 g | Weight change required to detect active brewing |
| **Stability Timeout** | 500–10000 ms | 2000 ms | Time before returning to stable (average) filter mode |
| **Brewing Mode Samples** | 1–50 | 3 | Median filter sample count during brewing |
| **Stable Mode Samples** | 1–50 | 5 | Average filter sample count when stable |

### 9.4 Sleep Settings

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| **Auto Sleep** | On/Off | On | Enable sleep after a period of inactivity |
| **Timeout Duration** | 1–60 min | 10 min | Time with no weight change or button press before sleep starts |

Weight changes of more than 0.5 g reset the inactivity timer. Button presses also reset it.

### 9.5 Brew Automation

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| **Cup Weight Auto Re-arm** | Toggle | On | Automatically arm when the saved cup weight is placed on the scale |
| **Auto-Tare on Vessel Placement** | Toggle | Off | Automatically tare when a stable weight is placed |
| **Minimum Weight to Trigger** (auto-tare) | 5–500 g | 20 g | Lightest vessel that triggers auto-tare |
| **Post-Brew Idle Reset** | Toggle | Off | Auto-reset after brew if no weight change |
| **Idle Timeout** | 5–300 s | 30 s | Seconds of inactivity before idle reset fires |
| **Pre-Infusion Timing** | Toggle | Off | Start timer on arm instead of first drip |
| **Auto-Stop on Flow Cessation** | Toggle | Off | Stop timer automatically when flow ends |
| **Target Yield Ratio** | 0–20 | 0 | Target ratio for yield alert (0 = disabled) |

---

## 10. Calibration

### 10.1 Scale Calibration

The scale must be calibrated once after assembly with a known reference weight. Re-calibrate if readings drift noticeably over time.

**You will need:** A known reference weight (e.g. 100 g calibration weight or any item with a known mass).

**Procedure:**

1. Navigate to **Calibration** in the web interface.
2. Ensure the scale platform is empty.
3. Click **Tare** (Step 1). Wait for confirmation.
4. Place your known reference weight on the scale.
5. Enter the exact weight in grams in the **Known Weight** field (Step 2).
6. Click **Calibrate**. The new calibration factor is applied immediately.
7. **Optionally** — verify accuracy with a second different known weight (Step 3). The result is shown as measured vs expected with a pass/recalibrate rating.

The calibration factor is stored in non-volatile memory and survives reboots.

### 10.2 Battery Calibration

The battery voltage is read through a voltage divider and may have a small offset due to component tolerances. Calibrating it ensures accurate battery percentage readings.

**You will need:** A multimeter.

**Procedure:**

1. Navigate to **Calibration → Battery Calibration** in the web interface.
2. Note the **Current Voltage** displayed.
3. Use a multimeter to measure the actual battery voltage at the BAT+ and BAT− terminals.
4. Enter the measured voltage in the **Measured Battery Voltage** field.
5. Click **Calibrate Battery**.

The correction factor is applied and stored. Future readings use this correction automatically.

---

## 11. Firmware Updates

### 11.1 Over-The-Air (OTA) Update

OTA updates allow you to update the firmware without USB access.

1. Obtain a `.bin` firmware file (from a PlatformIO build or a release).
2. Navigate to **Updates** in the web interface.
3. Click **Choose File** and select the `.bin` file.
4. Click **Upload Firmware**.
5. A progress bar shows upload progress.
6. The device reboots automatically when complete. The page polls for reconnection and confirms the new version.

> **Important:** Do not close the browser tab or power off the device during upload.

### 11.2 Web-Based Flash (Full Reflash)

For a complete reinstall of both firmware and filesystem (web interface files), use the [flash page](https://bitbarista.github.io/CaffePeso/flash.html) — requires a USB connection to a computer running Chrome, Edge, or Opera.

---

## 12. Power and Battery

### 12.1 Power Switch

The slide switch on the enclosure cuts power to the entire device. Use it to fully power off when the scale is not in use for an extended period.

### 12.2 Sleep Mode

CaffePeso uses ESP32 deep sleep to conserve battery. In deep sleep, current draw drops to ~10 µA. The OLED turns off and Wi-Fi disconnects.

**Entering sleep manually:** Hold the Timer button for 3 seconds. The OLED counts down 3–2–1, shows **"Touch to Wake Up"**, then enters deep sleep.

**Cancelling sleep countdown:** Touch either button during the countdown. The OLED shows **"Sleep Cancelled"**.

**Auto sleep:** After the configured inactivity timeout (default 10 minutes), the same countdown begins automatically.

**Waking from sleep:** Touch either button. The device boots fully in a few seconds and reconnects to Wi-Fi.

### 12.3 Battery Indicator

The OLED shows a 3-segment battery icon in the top-right corner.

| Segments | Approximate level |
|----------|------------------|
| 3 filled | >60% |
| 2 filled | 30–60% |
| 1 filled | 10–30% |
| 0 filled (flashing) | <10% — charge soon |

**Low battery warning:** If battery voltage is below 3.2 V at boot, the OLED shows a large battery-low message and the device enters deep sleep immediately to prevent damage to the Li-ion cell.

### 12.4 Charging

Charge via the ESP32 board's USB-C port. Most ESP32-S3 boards include an onboard Li-ion charger — the battery charges while connected to USB regardless of whether the scale is switched on.

---

## 13. GaggiMate Bluetooth Integration

CaffePeso implements the same Bluetooth Low Energy (BLE) scale protocol as WeighMyBru², allowing it to work with [GaggiMate](https://github.com/jniebuhr/gaggimate) without any configuration on either device.

**Setup:**

1. In GaggiMate, select **WeighMyBru²** as the Bluetooth scale type.
2. Ensure CaffePeso is powered on and within Bluetooth range.
3. GaggiMate will discover and connect automatically.

**Status indicator:** When a GaggiMate (or other BLE client) is connected, a small **BT** icon appears in the top-right corner of the OLED display.

**What GaggiMate can do:**
- Read real-time weight from CaffePeso

CaffePeso also implements the WeighMyBru² command characteristic, which handles timer start/stop/reset instructions if GaggiMate sends them. Whether GaggiMate uses this in practice depends on the GaggiMate firmware version.

**Note:** BLE and Wi-Fi operate simultaneously on the ESP32-S3 without conflict. Both features are available at all times.

---

## 14. Troubleshooting

### Scale reads incorrectly after setup

Recalibrate using a known reference weight. See [Section 10.1](#101-scale-calibration).

### Scale drifts slowly when nothing is on it

A 10-second auto-zero correction runs in the background whenever the timer is stopped and weight is within ±0.3 g. If drift is more than ~0.3 g, tap Tare to re-zero manually.

### Timer starts on its own without drips

Check that the load cell is firmly mounted and the scale platform is not vibrating from machine operation. Reduce **Brewing Detection Threshold** in Filtering Settings if false triggers persist.

### Auto Re-arm fires when pouring milk

This can occur if milk is poured in a fast slug that passes through the cup weight window. The REARM\_STEP\_THRESHOLD (8 g per update cycle) is designed to block gradual pours. If your milk jug passes through the window in a single large step, consider disabling Auto Re-arm and arming manually between milk drinks.

### Device not found on Wi-Fi

- Check the IP address on the OLED at boot.
- If no IP is shown, the device is in AP mode — connect to the **CaffePeso** AP and reconfigure Wi-Fi credentials.
- Assign a static DHCP lease on your router to avoid address changes.

### Web interface not loading (blank page / 404)

The filesystem image (web files) may not have been uploaded. Flash the filesystem using the [flash page](https://bitbarista.github.io/CaffePeso/flash.html) or via PlatformIO:
```bash
pio run -e esp32s3-supermini --target uploadfs
```

### OTA update fails or device does not reboot

- Ensure you selected the correct `.bin` file for your board.
- Do not close the browser tab during upload.
- If the device appears unresponsive after a failed OTA, reflash via USB using the flash page.

### Factory reset

Hold the Tare button while powering on (slide the power switch while touching the tare pad). All Wi-Fi credentials are cleared and the device restarts in AP mode. Scale calibration and all other settings are preserved.

---

## Appendix A — Shot Quality Guide

| Measurement | Typical espresso range |
|-------------|----------------------|
| Dose | 14–22 g |
| Yield | 28–50 g |
| Brew ratio | 1:1.5 – 1:3.0 |
| Brew time | 25–35 s |
| Peak flow rate | 1.5–3.0 g/s |

---

## Appendix B — Attribution

CaffePeso is a derivative of [WeighMyBru²](https://github.com/031devstudios/weighmybru2) by 031devstudios, licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

This derivative is also released under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

---

*Built with care by a home espresso enthusiast. Not affiliated with any espresso machine manufacturer.*
