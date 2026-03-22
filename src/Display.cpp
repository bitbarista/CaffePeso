#include "Display.h"
#include "Scale.h"
#include "FlowRate.h"
#include "BluetoothScale.h"
#include "PowerManager.h"
#include "BatteryMonitor.h"
#include <WiFi.h>
#include "WiFiManager.h"
#include <Preferences.h>

Display::Display(uint8_t sdaPin, uint8_t sclPin, Scale* scale, FlowRate* flowRate)
    : sdaPin(sdaPin), sclPin(sclPin), scalePtr(scale), flowRatePtr(flowRate), bluetoothPtr(nullptr), powerManagerPtr(nullptr), batteryPtr(nullptr),
      messageStartTime(0), messageDuration(2000), showingMessage(false), 
      timerStartTime(0), timerPausedTime(0), timerRunning(false), timerPaused(false) {
    display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
}

bool Display::begin() {
    Serial.println("Initializing display...");
    
    // Initialize I2C with custom pins
    Wire.begin(sdaPin, sclPin);
    
    // Test I2C connection first with timeout
    Serial.println("Testing I2C connection to display...");
    unsigned long startTime = millis();
    const unsigned long I2C_TIMEOUT = 3000; // 3 second timeout
    
    bool i2cResponding = false;
    Wire.beginTransmission(SCREEN_ADDRESS);
    
    // Wait for I2C response with timeout
    while (millis() - startTime < I2C_TIMEOUT) {
        if (Wire.endTransmission() == 0) {
            i2cResponding = true;
            Serial.println("I2C device found at display address");
            break;
        }
        delay(100);
        Wire.beginTransmission(SCREEN_ADDRESS);
    }
    
    if (!i2cResponding) {
        Serial.println("ERROR: No I2C device found at display address");
        Serial.println("Display will be disabled - running headless mode");
        Serial.println("Check connections:");
        Serial.printf("- SDA to GPIO %d\n", sdaPin);
        Serial.printf("- SCL to GPIO %d\n", sclPin);
        Serial.println("- VCC to 3.3V");
        Serial.println("- GND to GND");
        displayConnected = false;
        return false;
    }
    
    // Initialize the display
    if (!display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("ERROR: SSD1306 initialization failed");
        Serial.println("Display will be disabled - running headless mode");
        displayConnected = false;
        return false;
    }
    
    Serial.println("Display connected and initialized successfully");
    displayConnected = true;
    setupDisplay();
    
    // Show startup message in same format as welcome message
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    String line1 = "CaffePeso";
    String line2 = "Starting";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;

    display->setCursor(centerX1, 0);
    display->print(line1);
    display->setCursor(centerX2, 16);
    display->print(line2);
    
    display->display();
    
    Serial.println("SSD1306 display initialized on SDA:" + String(sdaPin) + " SCL:" + String(sclPin));
    
    return true;
}

void Display::setupDisplay() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    display->cp437(true); // Use full 256 char 'Code Page 437' font
}

void Display::update() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Check if message duration has elapsed
    if (showingMessage) {
        int effectiveDuration = messageDuration;
        // Use shorter duration for tared message
        if (currentMessage == "Tared message") {
            effectiveDuration = 1000; // Half of default duration for quick feedback
        }
        
        if (millis() - messageStartTime > effectiveDuration) {
            showingMessage = false;
            Serial.println("Message cleared, returning to main display");
        }
    }
    
    if (!showingMessage && scalePtr != nullptr) {
        float weight = scalePtr->getCurrentWeight();

        // --- Auto-tare on vessel placement ---
        // autoTareFired is reset in resetTimer() so it fires at most once per brew cycle,
        // preventing a re-tare mid-brew when yield crosses the same threshold.
        if (autoTareEnabled) {
            float absW = fabs(weight);
            // Suppress auto-tare while the sleep/power button is held. Pressing the button
            // can transfer mechanical force through the device structure to the load cell,
            // causing a spurious reading above the threshold.
            bool sleepButtonHeld = powerManagerPtr && powerManagerPtr->isSleepTouchPressed();
            if (sleepButtonHeld) {
                autoTareStableSince = 0; // reset stability timer — don't let it accumulate while held
            } else if (absW < 2.0f) {
                autoTareStableSince = 0; // Reset stability timer when weight is negligible
            } else if (absW > autoTareThreshold && !autoTareFired) {
                if (autoTareStableSince == 0) {
                    autoTareStableSince = millis();
                } else if (millis() - autoTareStableSince >= AUTO_TARE_STABLE_MS) {
                    autoTareStableSince = 0;
                    showTaringMessage();
                    scalePtr->tare();
                    resetTimer();                        // resets autoTareFired to false
                    autoTareFired = true;                // re-lock after reset so brew crossing won't re-tare
                    if (flowRatePtr) flowRatePtr->resetTimerAveraging();
                    showTaredMessage();
                    weight = 0.0f; // reflect post-tare state immediately
                }
            } else if (absW <= autoTareThreshold) {
                autoTareStableSince = 0;
            }
        }

        // --- Post-brew idle reset ---
        if (idleResetEnabled && timerPaused && !timerRunning) {
            if (idleResetWeightStableFrom == 0) {
                idleResetWeightStableFrom = millis();
                idleResetLastWeight = weight;
            } else if (fabs(weight - idleResetLastWeight) > 0.5f) {
                idleResetWeightStableFrom = millis();
                idleResetLastWeight = weight;
            } else if (millis() - idleResetWeightStableFrom >= idleResetTimeout) {
                resetTimer();
                scalePtr->tare();
                if (flowRatePtr) flowRatePtr->resetTimerAveraging();
                idleResetWeightStableFrom = 0;
                weight = 0.0f;
            }
        } else if (!timerPaused) {
            idleResetWeightStableFrom = 0;
        }

        // Cup removal auto-stop — if weight drops faster than any natural flow could cause,
        // the cup has been lifted; stop the timer and preserve stats
        currentWeightForCapture = weight;
        if (timerRunning && !timerPaused) {
            float weightDelta = weight - prevWeightForRemoval;
            if (prevWeightForRemoval > 5.0f && weightDelta < -CUP_REMOVAL_THRESHOLD) {
                stopTimer();
                Serial.println("Cup removal detected — timer stopped");
            }
        }
        prevWeightForRemoval = weight;

        // Armed auto-start: trigger timer when weight increases > threshold sustained 500ms
        if (armedAutoStart && !timerRunning) {
            if (millis() - armStartedAt > ARM_TIMEOUT_MS) {
                disarm();
                Serial.println("Armed auto-start: timed out");
            } else if (weight > ARM_TRIGGER_THRESHOLD) {
                if (armWeightAboveThresholdSince == 0) {
                    armWeightAboveThresholdSince = millis();
                } else if (millis() - armWeightAboveThresholdSince >= ARM_SUSTAIN_MS) {
                    if (timerPaused) resetTimer();
                    startTimer();
                    disarm();
                    Serial.println("Armed auto-start: timer started on weight increase");
                }
            } else {
                armWeightAboveThresholdSince = 0;
            }
        }

        // Target yield alert: invert display for 1s when approaching target yield
        if (timerRunning && !timerPaused && doseWeight > 0.5f && targetRatio > 0.0f && !alertFired) {
            float alertThreshold = doseWeight * targetRatio - 2.0f;
            if (alertThreshold > 0.0f && weight >= alertThreshold) {
                alertFired = true;
                alertFlashActive = true;
                alertFlashStart = millis();
                Serial.printf("Target yield alert: %.1fg >= threshold %.1fg\n", weight, alertThreshold);
            }
        }
        if (alertFlashActive) {
            if (millis() - alertFlashStart < ALERT_FLASH_MS) {
                display->invertDisplay(true);
            } else {
                display->invertDisplay(false);
                alertFlashActive = false;
            }
        }

        showWeightWithFlowAndTimer(weight);
    }
}

void Display::showWeight(float weight) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }

    if (showingMessage) return; // Don't override messages
    
    // Use the unified display showing weight, flow rate, and timer
    showWeightWithFlowAndTimer(weight);
}

void Display::showMessage(const String& message, int duration) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    currentMessage = message;
    messageStartTime = millis();
    messageDuration = duration; // Store the duration
    showingMessage = true;
    
    display->clearDisplay();
    display->setTextSize(1);
    display->setCursor(0, 0);
    
    // Word wrap for longer messages
    int lineHeight = 8;
    int maxCharsPerLine = 21; // For 128px width
    int currentLine = 0;
    
    for (int i = 0; i < message.length() && currentLine < 4; i += maxCharsPerLine) {
        String line = message.substring(i, min(i + maxCharsPerLine, (int)message.length()));
        display->setCursor(0, currentLine * lineHeight);
        display->print(line);
        currentLine++;
    }
    
    display->display();
}

void Display::showBatteryLowMessage(float voltage, int duration) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    String line1 = "Bat Low";
    String line2 = String(voltage, 1) + "V";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;

    display->setCursor(centerX1, 0);
    display->print(line1);
    display->setCursor(centerX2, 16);
    display->print(line2);
    
    display->display();
    
    // Set message state for auto-clearing
    currentMessage = line1 + " " + line2;
    messageStartTime = millis();
    messageDuration = duration;
    showingMessage = true;
}

void Display::showSleepCountdown(int seconds) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Sleep countdown active";
    messageStartTime = millis();
    showingMessage = true;
    
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    String line1 = "Sleep in";
    String line2 = String(seconds) + "...";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;

    display->setCursor(centerX1, 0);
    display->print(line1);
    display->setCursor(centerX2, 16);
    display->print(line2);
    
    display->display();
}

void Display::showSleepMessage() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Sleep message active";
    messageStartTime = millis();
    showingMessage = true;
    
    // Large top line, small bottom line (different sizes so handled separately)
    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    display->setTextSize(2);
    String line1 = "Sleeping..";
    
    int16_t x1, y1;
    uint16_t w1, h1;
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    
    display->setCursor(centerX1, 0);
    display->print(line1);
    
    display->setTextSize(1);
    String line2 = "Touch to cancel";
    uint16_t w2, h2;
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    display->setCursor(centerX2, 24); // size-1 text at bottom row
    display->print(line2);
    
    display->display();
}

void Display::showGoingToSleepMessage() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Going to sleep message";
    messageStartTime = millis();
    showingMessage = true;
    
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    String line1 = "Touch To";
    String line2 = "Wake Up";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;

    display->setCursor(centerX1, 0);
    display->print(line1);
    display->setCursor(centerX2, 16);
    display->print(line2);
    
    display->display();
}

void Display::showSleepCancelledMessage() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Sleep cancelled message";
    messageStartTime = millis();
    showingMessage = true;
    
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    String line1 = "Sleep";
    String line2 = "Cancelled";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;

    display->setCursor(centerX1, 0);
    display->print(line1);
    display->setCursor(centerX2, 16);
    display->print(line2);
    
    display->display();
}

void Display::showTaringMessage() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Taring message";
    messageStartTime = millis();
    showingMessage = true;
    
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    String line1 = "Taring";
    String line2 = "...";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;

    display->setCursor(centerX1, 0);
    display->print(line1);
    display->setCursor(centerX2, 16);
    display->print(line2);
    
    display->display();
}

void Display::showTaredMessage() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Tared message";
    messageStartTime = millis();
    showingMessage = true;
    
    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);

    // "Tared!" prominent in size 2
    display->setTextSize(2);
    String line1 = "Tared!";
    int16_t x1, y1;
    uint16_t w1, h1;
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->setCursor((SCREEN_WIDTH - w1) / 2, 0);
    display->print(line1);

    // Hint in size 1 at bottom
    display->setTextSize(1);
    String hint = "Hold tare: arm";
    uint16_t wh, hh;
    display->getTextBounds(hint, 0, 0, &x1, &y1, &wh, &hh);
    display->setCursor((SCREEN_WIDTH - wh) / 2, 24);
    display->print(hint);

    display->display();
}


void Display::clearMessageState() {
    showingMessage = false;
    currentMessage = "";
    messageStartTime = 0;
}

void Display::showIPAddresses() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }

    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);

    // Top row: project name (size 2, centred)
    display->setTextSize(2);
    String name = "CaffePeso";
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
    display->setCursor((SCREEN_WIDTH - w) / 2, 0);
    display->print(name);

    // Bottom row: IP address (size 1, centred) — STA IP if connected, AP IP otherwise
    display->setTextSize(1);
    String ip = (WiFi.status() == WL_CONNECTED)
        ? WiFi.localIP().toString()
        : WiFi.softAPIP().toString();
    display->getTextBounds(ip, 0, 0, &x1, &y1, &w, &h);
    display->setCursor((SCREEN_WIDTH - w) / 2, 24);
    display->print(ip);

    display->display();
    delay(2000);
}

void Display::clear() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    display->clearDisplay();
    display->display();
}

void Display::setBrightness(uint8_t brightness) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // SSD1306 doesn't have brightness control, but we can simulate with contrast
    display->ssd1306_command(SSD1306_SETCONTRAST);
    display->ssd1306_command(brightness);
}

void Display::setBluetoothScale(BluetoothScale* bluetooth) {
    bluetoothPtr = bluetooth;
}

void Display::setPowerManager(PowerManager* powerManager) {
    powerManagerPtr = powerManager;
}

void Display::setBatteryMonitor(BatteryMonitor* battery) {
    batteryPtr = battery;
}


void Display::drawBluetoothStatus() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Only draw when connected — no indicator when scanning/disconnected
    if (bluetoothPtr && bluetoothPtr->isConnected()) {
        display->setTextSize(1);
        display->setCursor(115, 0);
        display->print("BT");
        display->drawRect(113, 0, 15, 9, SSD1306_WHITE);
    }
}

void Display::drawBatteryStatus() {
    // Return early if display is not connected or no battery monitor
    if (!displayConnected || !batteryPtr) {
        return;
    }
    
    // Get battery percentage and critical status
    int batteryPercentage = batteryPtr->getBatteryPercentage();
    bool isCritical = batteryPtr->isCriticalBattery();
    
    // Format percentage string
    String percentStr = String(batteryPercentage) + "%";
    
    // Set small text size for percentage display
    display->setTextSize(1);
    
    // For critical battery, make it flash (every 500ms)
    if (isCritical && (millis() % 1000 < 500)) {
        // Flash state - draw text with inverted colors (black text on white background)
        int16_t x1, y1;
        uint16_t textWidth, textHeight;
        display->getTextBounds(percentStr, 0, 0, &x1, &y1, &textWidth, &textHeight);
        
        // Fill background white and draw black text
        display->fillRect(0, 0, textWidth + 2, textHeight + 2, SSD1306_WHITE);
        display->setTextColor(SSD1306_BLACK);
        display->setCursor(1, 1);
        display->print(percentStr);
        display->setTextColor(SSD1306_WHITE); // Reset text color
    } else {
        // Normal percentage display in top-left corner
        display->setCursor(0, 0);
        display->print(percentStr);
    }
}

// Layout (128x32):
//   y=0..15  (16px): weight size 2, centred
//   y=16..23 ( 8px): gap
//   y=24..31 ( 8px): timer+"T" left | running ratio centre | flow right
//                    (centre ratio only when timer running and dose set;
//                     right shows final ratio when paused, flow otherwise)
void Display::showWeightWithFlowAndTimer(float weight) {
    if (!displayConnected) return;
    if (showingMessage) return;

    display->clearDisplay();

    // --- Weight: size 2, centred, top row ---
    float displayWeight = weight;
    if (weight >= -0.1f && weight <= 0.1f) displayWeight = 0.0f;
    String weightStr = String(displayWeight, weightDecimals);
    int16_t wx, wy;
    uint16_t ww, wh;
    display->setTextSize(2);
    display->getTextBounds(weightStr, 0, 0, &wx, &wy, &ww, &wh);
    display->setCursor((SCREEN_WIDTH - ww) / 2, 0);
    display->print(weightStr);

    // --- Timer string (M:SS or SS.t) ---
    float absTimer = fabs(getTimerSeconds());
    String timerStr;
    if (absTimer >= 60.0f) {
        int totalSec = (int)absTimer;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d:%02d", totalSec / 60, totalSec % 60);
        timerStr = String(buf);
    } else {
        int timerSec = (int)absTimer;
        int timerDec = (int)((absTimer - timerSec) * 10 + 0.5f);
        if (timerDec >= 10) { timerSec++; timerDec = 0; }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d.%d", timerSec, timerDec);
        timerStr = String(buf);
    }

    // --- Bottom row (y=24): timer size 1 left + "T"; flow/ratio size 1 right ---
    int16_t x1, y1;
    uint16_t timerW, timerH;
    display->setTextSize(1);
    display->getTextBounds(timerStr, 0, 0, &x1, &y1, &timerW, &timerH);
    display->setCursor(0, 24);
    display->print(timerStr);
    display->setCursor(timerW + 1, 24);
    display->print("T");

    // --- Centre: running ratio (timer running, dose set, weight positive) ---
    bool showRunningRatio = !timerPaused && doseWeight > 1.0f && weight > 0.5f;
    if (showRunningRatio) {
        char buf[10];
        snprintf(buf, sizeof(buf), "1:%.1f", weight / doseWeight);
        uint16_t w, h;
        display->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display->setCursor((SCREEN_WIDTH - w) / 2, 24);
        display->print(buf);
    }

    // --- Right: flow rate (brewing) or final ratio (paused) ---
    float currentFlowRate = 0.0f;
    if (flowRatePtr != nullptr) {
        if (timerPaused && flowRatePtr->hasTimerAverage()) {
            currentFlowRate = flowRatePtr->getTimerAverageFlowRate();
        } else {
            currentFlowRate = flowRatePtr->getFlowRate();
        }
    }
    if (currentFlowRate >= -0.1f && currentFlowRate <= 0.1f) currentFlowRate = 0.0f;

    bool showFinalRatio = timerPaused && doseWeight > 1.0f && weight > 0.5f;

    if (showFinalRatio) {
        char buf[12];
        snprintf(buf, sizeof(buf), "1:%.1fR", weight / doseWeight);
        uint16_t w, h;
        display->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display->setCursor(SCREEN_WIDTH - w, 24);
        display->print(buf);
    } else if (!timerRunning && !timerPaused && !armedAutoStart) {
        // Idle state — hint at armed auto-start instead of showing 0.0F
        const char* hint = "hold tare:arm";
        uint16_t w, h;
        display->getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        display->setCursor(SCREEN_WIDTH - w, 24);
        display->print(hint);
    } else {
        float absFlow = fabs(currentFlowRate);
        int flowInt = (int)absFlow;
        int flowDec = (int)((absFlow - flowInt) * 10 + 0.5f);
        if (flowDec >= 10) { flowInt++; flowDec = 0; }
        char buf[12];
        snprintf(buf, sizeof(buf), currentFlowRate < 0 ? "-%d.%dF" : "%d.%dF", flowInt, flowDec);
        uint16_t w, h;
        display->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display->setCursor(SCREEN_WIDTH - w, 24);
        display->print(buf);
    }

    // --- Persistent status indicators in the weight row corners (size 1, y=0) ---
    // Drawn last so they sit on top of any partial weight text overlap at the edges.
    drawBatteryStatus();    // top-left: battery %
    drawBluetoothStatus();  // top-right: BT with box when connected

    display->display();
}

// Timer management methods
void Display::startTimer() {
    if (!timerRunning) {
        // Fresh start
        timerStartTime = millis();
        timerRunning = true;
        timerPaused = false;

        // Start flow rate averaging if not already started (auto-detect starts it earlier)
        if (flowRatePtr != nullptr && !flowRatePtr->isTimerAveragingActive()) {
            flowRatePtr->startTimerAveraging();
        }
    } else if (timerPaused) {
        // Resume from paused state
        timerStartTime = millis() - timerPausedTime;
        timerPaused = false;
        
        // Resume flow rate averaging when timer resumes
        if (flowRatePtr != nullptr) {
            flowRatePtr->startTimerAveraging();
        }
    }
    // If timer is already running and not paused, do nothing
}

void Display::stopTimer() {
    if (timerRunning && !timerPaused) {
        timerPausedTime = millis() - timerStartTime;
        timerPaused = true;

        // Capture brew stats for shot history
        lastBrewYield = currentWeightForCapture;
        lastBrewTime  = timerPausedTime / 1000.0f;
        pendingShot   = (doseWeight > 0.5f && lastBrewYield > 5.0f && lastBrewTime > 3.0f);

        // Stop flow rate averaging when timer stops
        if (flowRatePtr != nullptr) {
            flowRatePtr->stopTimerAveraging();
        }
    }
}

void Display::resetTimer() {
    timerStartTime = 0;
    timerPausedTime = 0;
    timerRunning = false;
    timerPaused = false;
    autoTareFired = false;    // Allow auto-tare to fire again on the next cup placement
    idleResetWeightStableFrom = 0;
    prevWeightForRemoval = 0.0f;
    pendingShot = false;
    alertFired = false;
    if (alertFlashActive && displayConnected) {
        display->invertDisplay(false);
        alertFlashActive = false;
    }

    // Reset flow rate averaging when timer is reset
    if (flowRatePtr != nullptr) {
        flowRatePtr->resetTimerAveraging();
    }

    // Sync PowerManager state
    if (powerManagerPtr != nullptr) {
        powerManagerPtr->resetTimerState();
    }
}

bool Display::isTimerRunning() const {
    return timerRunning && !timerPaused;
}

bool Display::isTimerPaused() const {
    return timerPaused;
}

float Display::getTimerSeconds() const {
    if (!timerRunning) {
        return 0.0;
    } else if (timerPaused) {
        return timerPausedTime / 1000.0;
    } else {
        return (millis() - timerStartTime) / 1000.0;
    }
}


unsigned long Display::getElapsedTime() const {
    if (!timerRunning) {
        return 0;
    } else if (timerPaused) {
        return timerPausedTime;
    } else {
        return millis() - timerStartTime;
    }
}

void Display::arm(float cupWeightBeforeTare) {
    savedTareWeight = cupWeightBeforeTare;
    armedAutoStart  = true;
    armStartedAt    = millis();
    armWeightAboveThresholdSince = 0;
    alertFired = false; // fresh brew — reset alert

    // Persist saved cup weight across reboots
    Preferences prefs;
    if (prefs.begin("display", false)) {
        prefs.putFloat("saved_tare", savedTareWeight);
        prefs.end();
    }
    Serial.printf("Armed: cup weight = %.1fg\n", savedTareWeight);
}

void Display::disarm() {
    armedAutoStart = false;
    armWeightAboveThresholdSince = 0;
}

void Display::showArmedMessage() {
    if (!displayConnected) return;

    currentMessage    = "Armed message";
    messageStartTime  = millis();
    messageDuration   = 2000;
    showingMessage    = true;

    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);

    String line1 = "Armed";
    String line2 = "Ready!";

    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);

    display->setCursor((SCREEN_WIDTH - w1) / 2, 0);
    display->print(line1);
    display->setCursor((SCREEN_WIDTH - w2) / 2, 16);
    display->print(line2);
    display->display();
}
