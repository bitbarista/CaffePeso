<p align="center">
<img src="docs/assets/CaffePeso.png" alt="CaffePeso" width="300"/>
</p>

<h1 align="center">CaffePeso</h1>

<p align="center">The smart automated espresso scale.</p>

[![License](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg?style=for-the-badge)](LICENSE)

This is a fork of [WeighMyBru²](https://github.com/031devstudios/weighmybru2) by 031DevStudios, with additional brewing automation features. It is a smart coffee scale with a web interface hosted on the ESP32-S3, designed to work alongside GaggiMate or as a standalone scale. Inspired by [EspressiScale](https://www.espressiscale.com) with a low-cost, easily sourceable non-custom PCB approach.

<p>
<img src="docs/assets/weighmybru.jpg" alt="WeighMyBru² hardware" width="700" />
</p>

## Documentation

Full user guide: [docs/USER_GUIDE.md](docs/USER_GUIDE.md)

For hardware build guides and video walkthroughs, see the [original project](https://github.com/031devstudios/weighmybru2).

## Features

- Web interface via Wi-Fi
- Bluetooth connectivity to GaggiMate
- Calibration via web interface
- Real-time flow rate display
- Adjustable decimal places
- Display modes for espresso and pour-overs
- **Armed auto-start** — hold tare to arm; timer starts automatically on first drip
- **Auto-re-arm** — scale recognises your cup and arms itself on the next shot
- **Target yield alert** — OLED flashes and web UI turns amber when approaching your target ratio
- **Shot history** — last 10 shots (dose, yield, time, ratio) stored in non-volatile memory


## GaggiMate

GaggiMate now fully supports WeighMyBru scale.

[GaggiMate](https://github.com/jniebuhr/gaggimate)

## Installation

For hardware assembly and wiring, see the [original project's guides](https://github.com/031devstudios/weighmybru2).

```
  this project requires VSCode with PlatformIO extension installed
```

### Important: Filesystem Upload Required

After uploading the firmware, you **must also upload the filesystem** for the web interface to work:

```bash
# Upload filesystem (required for web interface)
pio run -t uploadfs

# Or use the specific environment for your board
pio run -e esp32s3-supermini -t uploadfs  # For ESP32-S3 Supermini
pio run -e esp32s3-xiao -t uploadfs       # For XIAO ESP32S3
```

**Without the filesystem upload:**
- The device will function normally for scale operations
- The web interface will be unavailable
- You'll see a clear message explaining how to fix this issue

### 🌐 Web-Based Installation (Recommended)

For beginners, use the browser-based installer — no software installation required:

1. **Visit the [flash page](https://bitbarista.github.io/caffepeso)** (available once the repo is public)
2. **Connect your ESP32 board** via USB
3. **Click "Install Firmware"** and select your board:
   - ESP32-S3 Supermini
   - XIAO ESP32S3
4. **Follow the prompts** - no software installation required!

**Benefits:**
- ✅ No need to install VS Code or PlatformIO
- ✅ Automatic latest firmware version
- ✅ Complete installation (firmware + filesystem)
- ✅ Works on any modern browser (Chrome, Edge, Opera)
- ✅ Version checking and device information

For advanced users or development, continue with the PlatformIO instructions below.

## Bill Of Materials (BOM)

The hardware is identical to the original project. See the [original WeighMyBru² BOM](https://github.com/031devstudios/weighmybru2#bill-of-materials-bom) for the full parts list with purchase links.



## Printed Parts

The enclosure design is identical to the original project. See the [original WeighMyBru² repository](https://github.com/031devstudios/weighmybru2) for CAD files and print instructions.

---

## Attribution

This project is a derivative of [WeighMyBru](https://github.com/031devstudios/weighmybru2) by 031devstudios, licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

Additions in this fork:
- Armed auto-start: hold tare to arm timer; auto-triggers on first drip; auto-re-arms when same cup detected
- Target yield alert: configurable brew ratio target; OLED flashes and web UI turns amber at approach
- Cup weight persistence across reboots
- Shot history: last 10 shots stored in NVS, displayed in web UI

This derivative is also released under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).
