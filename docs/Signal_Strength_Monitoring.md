# Signal Strength Monitoring

This document describes the WiFi and Bluetooth signal strength monitoring feature added to WeighMyBru.

## Overview

The signal strength monitoring feature provides real-time visibility into the quality of WiFi and Bluetooth connections directly in the web interface. This helps with troubleshooting connectivity issues and optimizing device placement.

## Features

### WiFi Signal Strength
- **Real-time monitoring**: WiFi signal strength (RSSI) is displayed in the dashboard header
- **Visual indicators**: Color-coded WiFi icon shows signal quality at a glance
- **Signal strength values**: Displays actual dBm values next to the WiFi icon
- **Connection info**: Shows detailed WiFi connection information including channel, TX power, IP addresses

### Bluetooth Signal Strength  
- **BLE monitoring**: Bluetooth Low Energy signal strength when devices are connected
- **Visual feedback**: Color-coded Bluetooth indicator shows connection quality
- **Connection status**: Real-time indication of BLE connection state

### Visual Color Coding
- **Green**: Excellent signal strength (≥-50 dBm)
- **Orange**: Good/Fair signal strength (-50 to -80 dBm)  
- **Red**: Weak signal strength (≤-80 dBm)
- **Grey**: Disconnected or unavailable

## API Endpoints

### Signal Strength API
**Endpoint**: `/api/signal-strength`  
**Method**: GET  
**Response**: JSON object containing WiFi and Bluetooth connection details

```json
{
  "wifi": {
    "connected": true,
    "mode": "STA",
    "ssid": "YourNetwork",
    "signal_strength": -45,
    "signal_quality": "Very Good",
    "channel": 6,
    "tx_power": 20,
    "ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "dns": "192.168.1.1",
    "mac": "AA:BB:CC:DD:EE:FF"
  },
  "bluetooth": {
    "connected": true,
    "advertising": true,
    "signal_strength": -55,
    "signal_quality": "Good",
    "connection_handle": 1,
    "service_uuid": "6E400001-B5A3-F393-E0A9-E50E24DCCA9E",
    "device_name": "WeighMyBru"
  }
}
```

### Dashboard API Enhancement
The main dashboard API (`/api/dashboard`) now includes signal strength information:
- `wifi_signal_strength`: Current WiFi RSSI in dBm
- `wifi_signal_quality`: Human-readable signal quality description
- `bluetooth_connected`: Boolean indicating BLE connection status
- `bluetooth_signal_strength`: Current Bluetooth RSSI in dBm

## Web Interface

### Header Display
Signal strength indicators are shown in the top-right header next to battery and Bluetooth icons:
- **WiFi Icon**: Shows WiFi signal strength with color coding and dBm value
- **BT Icon**: Shows Bluetooth connection status and signal strength

### Filter State Indicator
The filter state indicator (previously showing "FI" and "EB" due to text overflow) now displays shorter, clearer text:
- **STABLE**: Normal operation
- **BREW**: Active brewing detected
- **TRANS**: Transitioning between states
- **FILT**: Filtering signal noise

## Implementation Details

### WiFi Signal Strength Functions
- `getWiFiSignalStrength()`: Returns current RSSI in dBm
- `getWiFiSignalQuality()`: Returns human-readable quality description
- `getWiFiConnectionInfo()`: Returns detailed connection information as JSON

### Bluetooth Signal Strength Functions
- `getBluetoothSignalStrength()`: Returns current BLE RSSI in dBm
- `getBluetoothConnectionInfo()`: Returns detailed BLE connection information as JSON

### Update Frequency
- Signal strength is updated every 200ms along with the main dashboard data
- No additional API calls are made - signal strength is included in existing dashboard updates for efficiency

## Troubleshooting

### WiFi Signal Issues
- **Red WiFi icon**: Check device placement relative to router
- **"Off" status**: WiFi disconnected - check credentials in settings
- **Weak signal**: Consider moving closer to router or using WiFi extender

### Bluetooth Signal Issues  
- **"Off" status**: No BLE device connected
- **Weak signal**: Move connected device closer to WeighMyBru
- **Connection drops**: Check for interference from other 2.4GHz devices

## SuperMini Antenna Fix Integration

This feature works in conjunction with the SuperMini antenna fix to provide visibility into whether the power boost is helping with connectivity:
- Monitor WiFi signal strength before and after enabling the antenna fix
- Compare signal quality in different locations
- Validate that the power increase improves connection stability

## Benefits

1. **Proactive monitoring**: Identify connectivity issues before they cause problems
2. **Optimal placement**: Find the best location for your WeighMyBru device
3. **Troubleshooting**: Quickly diagnose WiFi or Bluetooth connection problems
4. **Performance validation**: Verify that antenna fixes are improving signal quality
5. **Real-time feedback**: See immediate results when adjusting device positioning
