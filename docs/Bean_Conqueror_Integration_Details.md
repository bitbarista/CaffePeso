# CaffePeso - Bean Conqueror Integration Details

## Current BLE Implementation for Bean Conqueror Integration

### Advertising Information
- **Device Name**: `CaffePeso`
- **Advertising**: Device advertises continuously when not connected

### Service and Characteristic UUIDs
- **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **GaggiMate Weight Characteristic UUID**: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  - Properties: READ, NOTIFY, INDICATE
  - Used for: GaggiMate weight data (CaffePeso protocol format)
- **Command Characteristic UUID**: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
  - Properties: WRITE, WRITE_NR, NOTIFY
  - Used for: Receiving commands from both apps (tare, timer control)
- **Bean Conqueror Weight Characteristic UUID**: `6E400004-B5A3-F393-E0A9-E50E24DCCA9E`
  - Properties: READ, NOTIFY, INDICATE
  - Used for: Bean Conqueror weight data (simple float format)

### Required API Functions - Implementation Status

#### ✅ Weight Send
- **Status**: ✅ IMPLEMENTED
- **Frequency**: 50ms interval (20 updates/second)
- **Format**: CaffePeso protocol with weight data in grams
- **Precision**: Floating point weight values

#### ✅ Taring
- **Status**: ✅ IMPLEMENTED
- **Command Code**: `0x01`
- **Protocol**: Send `[0x03, 0x0A, 0x01, 0x01, 0x00]` to command characteristic
- **Response**: Confirmation message sent back via weight characteristic
- **Function**: Zeros the scale reading

#### ✅ Timer Start
- **Status**: ✅ IMPLEMENTED
- **Command Code**: `0x02`
- **Protocol**: Send `[0x03, 0x0A, 0x02, 0x01, 0x00]` to command characteristic
- **Response**: Start confirmation `[0x03, 0x0A, 0x02, 0x01, 0x00]`
- **Function**: Starts the brewing timer on CaffePeso

#### ✅ Timer Stop
- **Status**: ✅ IMPLEMENTED
- **Command Code**: `0x03`
- **Protocol**: Send `[0x03, 0x0A, 0x03, 0x01, 0x00]` to command characteristic
- **Response**: Stop confirmation `[0x03, 0x0A, 0x03, 0x01, 0x00]`
- **Function**: Stops the brewing timer on CaffePeso

#### ✅ Timer Reset
- **Status**: ✅ IMPLEMENTED
- **Command Code**: `0x04`
- **Protocol**: Send `[0x03, 0x0A, 0x04, 0x01, 0x00]` to command characteristic
- **Response**: Reset confirmation `[0x03, 0x0A, 0x04, 0x01, 0x00]`
- **Function**: Resets the brewing timer to 0:00.000

## Protocol Details

### Command Message Format
```
[Product_ID, Message_Type, Command, Trigger, Reserved]
```
- **Product_ID**: `0x03` (CaffePeso identifier)
- **Message_Type**: `0x0A` (System command)
- **Command**: Command code (0x01-0x04)
- **Trigger**: `0x01` (Execute command)
- **Reserved**: `0x00` (Future use)

### Weight Data Format
- Sent via weight characteristic notifications
- **Format**: Simple 4-byte float in little-endian byte order
- **Data**: Weight value directly in grams (e.g., 15.67g)
- **Precision**: Full floating-point precision
- **Update Rate**: Automatic notifications every 50ms when connected
- **Byte Layout**: `[float32_little_endian]` (4 bytes total)

### Connection Behavior
- Automatically starts advertising when disconnected
- Stops advertising when connected
- Supports single client connection
- Automatic reconnection support

## Integration Notes for Bean Conqueror

1. **Discovery**: Look for device named "CaffePeso" in BLE scan
2. **Connection**: Connect to service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
3. **Weight Subscription**: Subscribe to notifications on `6E400004-B5A3-F393-E0A9-E50E24DCCA9E` (Bean Conqueror specific)
4. **Command Sending**: Write commands to `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
5. **Timer Control**: All timer functions (start/stop/reset) are available via BLE

### Important: Dual App Support
CaffePeso supports **both Bean Conqueror and GaggiMate simultaneously**:

**Bean Conqueror**:
- Weight data: `6E400004-...` (4-byte float32 little-endian)
- Expected format: Simple float value in grams

**GaggiMate**:
- Weight data: `6E400002-...` (20-byte CaffePeso protocol)
- Expected format: Full protocol with headers and checksums

**Commands**: Both apps share `6E400003-...` for tare and timer commands

## Technical Specifications

- **Platform**: ESP32-S3
- **BLE Stack**: ESP32 Arduino BLE library
- **Update Rate**: 20Hz (50ms intervals)
- **Weight Range**: 0-5000g (depending on load cell)
- **Precision**: 0.1g
- **Timer Precision**: Millisecond accuracy
- **Connection**: Single client BLE connection
- **Power**: Battery operated with monitoring

## Example Usage

```javascript
// Connect to CaffePeso
const device = await navigator.bluetooth.requestDevice({
  filters: [{name: 'CaffePeso'}],
  optionalServices: ['6E400001-B5A3-F393-E0A9-E50E24DCCA9E']
});

// Subscribe to weight notifications
const weightChar = await service.getCharacteristic('6E400002-B5A3-F393-E0A9-E50E24DCCA9E');
await weightChar.startNotifications();

// Send tare command
const commandChar = await service.getCharacteristic('6E400003-B5A3-F393-E0A9-E50E24DCCA9E');
await commandChar.writeValue(new Uint8Array([0x03, 0x0A, 0x01, 0x01, 0x00]));

// Send timer start command
await commandChar.writeValue(new Uint8Array([0x03, 0x0A, 0x02, 0x01, 0x00]));
```

## Relevant Source Files

- `include/BluetoothScale.h` - BeanConquerorCommand enum and method declarations
- `src/BluetoothScale.cpp` - Weight streaming and command handling implementation
- `src/main.cpp` - Display reference for timer control
