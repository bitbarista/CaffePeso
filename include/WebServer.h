#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "Scale.h"
#include "FlowRate.h"
#include "BluetoothScale.h"
#include "Display.h"
#include "BatteryMonitor.h"
#include "PowerManager.h"

extern float calibrationFactor;

void setupWebServer(Scale &scale, FlowRate &flowRate, BluetoothScale &bluetoothScale, Display &display, BatteryMonitor &battery, PowerManager &powerManager);
void startWebServer();
void stopWebServer();
void checkPendingWiFiDisable();   // Call from loop() to deferred-disable WiFi safely
void checkPendingShotSave(Display &display, Scale &scale); // Detect timer stop and save shot

#endif
