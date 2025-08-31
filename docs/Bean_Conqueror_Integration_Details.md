# WeighMyBru - Bean Conqueror Integration Details

## Current BLE Implementation for Bean Conqueror Integration

### Advertising Information
- **Device Name**: `WeighMyBru`
- **Advertising**: Device advertises continuously when not connected

### Service and Characteristic UUIDs
- **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **GaggiMate Weight Characteristic UUID**: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  - Properties: READ, NOTIFY, INDICATE
  - Used for: GaggiMate weight data (WeighMyBru protocol format)
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
- **Format**: WeighMyBru protocol with weight data in grams
- **Precision**: Floating point weight values

#### ✅ Taring
- **Status**: ✅ IMPLEMENTED
- **Command Code**: `0x01`
- **Protocol**: Send `[0x03, 0x0A, 0x01, 0x01, 0x00]` to command characteristic
- **Response**: Confirmation message sent back via weight characteristic
- **Function**: Zeros the scale reading

#### ✅ Timer Start
- **Status**: ✅ IMPLEMENTED (NEW)
- **Command Code**: `0x02`
- **Protocol**: Send `[0x03, 0x0A, 0x02, 0x01, 0x00]` to command characteristic
- **Response**: Start confirmation `[0x03, 0x0A, 0x02, 0x01, 0x00]`
- **Function**: Starts the brewing timer on WeighMyBru scale

#### ✅ Timer Stop
- **Status**: ✅ IMPLEMENTED (NEW)
- **Command Code**: `0x03`
- **Protocol**: Send `[0x03, 0x0A, 0x03, 0x01, 0x00]` to command characteristic
- **Response**: Stop confirmation `[0x03, 0x0A, 0x03, 0x01, 0x00]`
- **Function**: Stops the brewing timer on WeighMyBru scale

#### ✅ Timer Reset
- **Status**: ✅ IMPLEMENTED (NEW)
- **Command Code**: `0x04`
- **Protocol**: Send `[0x03, 0x0A, 0x04, 0x01, 0x00]` to command characteristic
- **Response**: Reset confirmation `[0x03, 0x0A, 0x04, 0x01, 0x00]`
- **Function**: Resets the brewing timer to 0:00.000

## Protocol Details

### Command Message Format
```
[Product_ID, Message_Type, Command, Trigger, Reserved]
```
- **Product_ID**: `0x03` (WeighMyBru identifier)
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

1. **Discovery**: Look for device named "WeighMyBru" in BLE scan
2. **Connection**: Connect to service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
3. **Weight Subscription**: Subscribe to notifications on `6E400004-B5A3-F393-E0A9-E50E24DCCA9E` (Bean Conqueror specific)
4. **Command Sending**: Write commands to `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
5. **Timer Control**: All timer functions (start/stop/reset) are now available via BLE

### Important: Dual App Support
The WeighMyBru scale now supports **both Bean Conqueror and GaggiMate simultaneously**:

**Bean Conqueror**: 
- Weight data: `6E400004-...` (4-byte float32 little-endian)
- Expected format: Simple float value in grams

**GaggiMate**: 
- Weight data: `6E400002-...` (20-byte WeighMyBru protocol)
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
// Connect to WeighMyBru
const device = await navigator.bluetooth.requestDevice({
  filters: [{name: 'WeighMyBru'}],
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

## Changes Made for Bean Conqueror Integration

### New Features Added:
1. **Timer Control API**: Added BLE commands for start/stop/reset timer
2. **Command Processing**: Enhanced message processing to handle timer commands
3. **Display Integration**: Linked BLE timer commands to physical display timer
4. **Confirmation Messages**: Added response confirmations for all commands

### Files Modified:
- `include/BluetoothScale.h` - Added timer command enums and methods
- `src/BluetoothScale.cpp` - Implemented timer command handling
- `src/main.cpp` - Added display reference for timer control

All required API functions for Bean Conqueror integration are now implemented and ready for testing.
