# WeighMyBru² - Dual Board Support

This project now supports two ESP32-S3 development boards, giving users flexibility to choose based on their needs and preferences.

## Supported Boards

### 1. ESP32-S3-DevKitC-1 (SuperMini) - `esp32s3-supermini`
- **Target Name**: `esp32s3-supermini`
- **Board**: ESP32-S3-DevKitC-1
- **Flash**: 4MB
- **Features**: Standard ESP32-S3 development board
- **Use Case**: General purpose, readily available

### 2. XIAO ESP32S3 - `esp32s3-xiao`
- **Target Name**: `esp32s3-xiao` 
- **Board**: Seeed Studio XIAO ESP32S3
- **Flash**: 8MB
- **Features**: Ultra-compact form factor (21x17.8mm)
- **Use Case**: Space-constrained applications, embedded projects

## Pin Compatibility

Both boards use **identical GPIO pin assignments**, making the code 100% compatible:

```cpp
// Pin Configuration (identical for both boards)
HX711 Data Pin:      GPIO5
HX711 Clock Pin:     GPIO6
Touch Tare Pin:      GPIO4 (T0)
Touch Sleep Pin:     GPIO3
Battery Pin:         GPIO7 (ADC1_CH6)
I2C SDA Pin:         GPIO8
I2C SCL Pin:         GPIO9
```

## Building for Different Boards

### Build Commands

```bash
# Build for ESP32-S3-DevKitC-1 (SuperMini)
pio run -e esp32s3-supermini

# Build for XIAO ESP32S3
pio run -e esp32s3-xiao

# Upload to ESP32-S3-DevKitC-1
pio run -e esp32s3-supermini -t upload

# Upload to XIAO ESP32S3
pio run -e esp32s3-xiao -t upload
```

### Board Identification

The firmware automatically detects and displays the board type on startup:

**ESP32-S3-DevKitC-1 Output:**
```
=================================
WeighMyBru² - ESP32-S3-DevKitC-1 (SuperMini)
Board: ESP32-S3 SuperMini with 4MB Flash
Flash Size: 4MB
=================================
```

**XIAO ESP32S3 Output:**
```
=================================
WeighMyBru² - XIAO ESP32S3
Board: XIAO ESP32S3 with 8MB Flash
Flash Size: 8MB
=================================
```

## Hardware Differences

| Feature | ESP32-S3-DevKitC-1 | XIAO ESP32S3 |
|---------|---------------------|---------------|
| **Size** | Standard dev board | Ultra-compact (21x17.8mm) |
| **Flash** | 4MB | 8MB |
| **Form Factor** | Breadboard friendly | Surface mount preferred |
| **Price** | Lower cost | Premium compact design |
| **Availability** | Widely available | Seeed Studio specific |
| **GPIO Pins** | All standard ESP32-S3 | Limited to 11 exposed GPIOs |
| **USB** | Standard micro/USB-C | USB-C |

## Enclosure Compatibility

### ESP32-S3-DevKitC-1 (SuperMini)
- Use existing WeighMyBru² 3D printed enclosures
- Standard mounting holes and dimensions

### XIAO ESP32S3  
- Requires XIAO-specific mounting solution
- **3D Print File**: `cad/3mf/WeighMyBru² - XAIO Clamp.3mf`
- Ultra-compact design possible

## Development Notes

### Code Architecture
- **Unified Codebase**: Single source code works for both boards
- **Build Flags**: Board-specific features controlled via compile flags
  - `BOARD_SUPERMINI` for ESP32-S3-DevKitC-1
  - `BOARD_XIAO` for XIAO ESP32S3
- **Pin Abstraction**: Hardware-specific pins defined in `include/BoardConfig.h`

### Future Extensibility
The board configuration system makes it easy to add support for additional ESP32-S3 boards:

1. Add new environment in `platformio.ini`
2. Define board-specific flags in `include/BoardConfig.h`
3. Update pin mappings if different from current boards

## Choosing the Right Board

### Choose ESP32-S3-DevKitC-1 (SuperMini) if you:
- Want lower cost
- Prefer breadboard prototyping
- Need standard form factor
- Are building your first WeighMyBru²

### Choose XIAO ESP32S3 if you:
- Need ultra-compact size
- Are building embedded/permanent installation
- Want extra flash storage (8MB)
- Prefer premium compact design

Both boards deliver identical functionality with the same excellent WeighMyBru² experience!