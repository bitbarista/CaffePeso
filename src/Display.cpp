#include "Display.h"
#include "Scale.h"
#include "FlowRate.h"
#include "BluetoothScale.h"
#include "PowerManager.h"
#include <WiFi.h>

Display::Display(uint8_t sdaPin, uint8_t sclPin, Scale* scale, FlowRate* flowRate)
    : sdaPin(sdaPin), sclPin(sclPin), scalePtr(scale), flowRatePtr(flowRate), bluetoothPtr(nullptr), powerManagerPtr(nullptr),
      messageStartTime(0), messageDuration(2000), showingMessage(false), currentMode(ScaleMode::FLOW),
      timerStartTime(0), timerPausedTime(0), timerRunning(false), timerPaused(false),
      lastWeight(0.0), lastWeightChangeTime(0), waitingForStabilization(false),
      weightWhenChanged(0.0), stabilizationStartTime(0), autoTareEnabled(true),
      lastFlowRate(0.0), autoTimerStarted(false), autoTareCompletedTime(0), pendingModeTare(false), modeSwitchTime(0), stabilizationMessageShown(false),
      waitingForModeTareStabilization(false), modeTareStabilizationStart(0), lastStableWeight(0.0), 
      fingerPressWeight(0.0), fingerReleaseDetected(false) {
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
    
    String line1 = "WeighMyBru";
    String line2 = "Starting";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
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
    // Always run auto-tare logic even without display for web interface support
    if (scalePtr != nullptr && currentMode == ScaleMode::AUTO) {
        float weight = scalePtr->getCurrentWeight();
        checkAutoTare(weight);
        
        // Also check auto-timer logic
        if (flowRatePtr != nullptr) {
            float currentFlowRate = flowRatePtr->getFlowRate();
            checkAutoTimer(currentFlowRate);
        }
    }
    
    // Handle mode tare stabilization monitoring even without display
    if (waitingForModeTareStabilization && scalePtr != nullptr) {
        unsigned long currentTime = millis();
        float currentWeight = scalePtr->getCurrentWeight();
        
        if (!fingerReleaseDetected) {
            // Stage 1: Wait for finger release (weight drops significantly)
            float weightDrop = fingerPressWeight - currentWeight;
            if (weightDrop >= 0.5f) { // FINGER_RELEASE_THRESHOLD
                fingerReleaseDetected = true;
                modeTareStabilizationStart = currentTime;
                lastStableWeight = currentWeight;
                Serial.println("Finger release detected (weight dropped " + String(weightDrop) + "g), starting stabilization timer");
            }
        } else {
            // Stage 2: Wait for stabilization after finger release
            float weightChange = fabs(currentWeight - lastStableWeight);
            if (weightChange < 0.05f) { // Tighter tolerance for final stabilization (0.05g)
                if (currentTime - modeTareStabilizationStart >= 400) { // MODE_TARE_STABILIZATION_TIME
                    // Weight has been stable long enough after finger release - perform tare
                    scalePtr->tare();
                    waitingForModeTareStabilization = false;
                    fingerReleaseDetected = false;
                    modeSwitchTime = millis(); // Reset mode switch time
                    stabilizationMessageShown = false;
                    Serial.println("Mode tare completed after finger release and stabilization");
                }
            } else {
                // Weight changed - reset stabilization timer
                modeTareStabilizationStart = currentTime;
                lastStableWeight = currentWeight;
            }
        }
    }
    
    // Return early if display is not connected - but auto-tare logic above still runs
    if (!displayConnected) {
        return;
    }
    
    // Check if we're showing a temporary message
    if (showingMessage) {
        if (millis() - messageStartTime > messageDuration) { // Use stored duration
            showingMessage = false;
        } else {
            return; // Keep showing the message
        }
    }
    
    // Get current weight and display it
    if (scalePtr != nullptr) {
        float weight = scalePtr->getCurrentWeight();
        showWeight(weight);
    }
}

void Display::showWeight(float weight) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    if (showingMessage) return; // Don't override messages
    
    // Handle different modes
    switch (currentMode) {
        case ScaleMode::FLOW:
            drawWeight(weight); // Use existing flow rate display
            break;
        case ScaleMode::TIME:
            showWeightWithTimer(weight); // New timer display
            break;
        case ScaleMode::AUTO:
            showWeightWithFlowAndTimer(weight); // Show both flow rate and timer
            break;
    }
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
    
    // Update duration for this message
    if (duration > 0) {
        // We'll check this in update() method
    }
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
    
    // Show countdown in same large format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    String line1 = "Sleep in";
    String line2 = String(seconds) + "...";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
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
    
    // Show sleep message with large top line and small bottom line
    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    
    // First line: "Sleep in 3" in large text (size 2)
    display->setTextSize(2);
    String line1 = "Sleeping..";
    
    int16_t x1, y1;
    uint16_t w1, h1;
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    
    display->setCursor(centerX1, 0);
    display->print(line1);
    
    // Second line: "Touch to cancel" in small text (size 1)
    display->setTextSize(1);
    String line2 = "Touch to cancel";
    
    uint16_t w2, h2;
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position small text at bottom (24 pixels from top gives us 8 pixels for the text)
    display->setCursor(centerX2, 24);
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
    
    // Show "Touch To / Wake Up" in same format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Calculate text positioning for centering
    String line1 = "Touch To";
    String line2 = "Wake Up";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
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
    
    // Show "Sleep / Cancelled" in same format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Calculate text positioning for centering
    String line1 = "Sleep";
    String line2 = "Cancelled";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
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
    
    // Show "Taring..." in same format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Since "Taring..." is a single word, we'll center it on one line
    // For consistency with WeighMyBru style, we can split it as "Taring" and "..."
    String line1 = "Taring";
    String line2 = "...";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
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
    
    // Show "Tared!" in same format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Split "Tared!" into two lines for better visual impact
    String line1 = "Scale";
    String line2 = "Tared!";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
    display->print(line2);
    
    display->display();
}

void Display::showAutoTaredMessage() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Auto Tared message";
    messageStartTime = millis();
    messageDuration = 1500; // Show for 1.5 seconds (reduced from 3 seconds)
    showingMessage = true;
    
    // Show "Auto Tared!" in same format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Split "Auto Tared!" into two lines for better visual impact
    String line1 = "Auto";
    String line2 = "Tared!";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
    display->print(line2);
    
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
    
    // First show the WeighMyBru Ready message for 3 seconds
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Calculate text positioning for centering
    String line1 = "WeighMyBru";
    String line2 = "Ready";
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
    display->print(line2);
    
    display->display();
    delay(1000);
    
    // Then show IP addresses
    display->clearDisplay();
    display->setTextSize(1);
    display->setTextColor(SSD1306_WHITE);
    
    String apIP = "AP: " + WiFi.softAPIP().toString();
    String staIP = "";
    
    // Check if connected to WiFi station mode
    if (WiFi.status() == WL_CONNECTED) {
        staIP = "STA: " + WiFi.localIP().toString();
    } else {
        staIP = "STA: Not Connected";
    }
    
    // Calculate text positioning for centering IP addresses
    int16_t ipX1, ipY1;
    uint16_t ipW1, ipH1, ipW2, ipH2;
    
    // Get text bounds for both IP lines
    display->getTextBounds(apIP, 0, 0, &ipX1, &ipY1, &ipW1, &ipH1);
    display->getTextBounds(staIP, 0, 0, &ipX1, &ipY1, &ipW2, &ipH2);
    
    // Calculate centered positions
    int ipCenterX1 = (SCREEN_WIDTH - ipW1) / 2;
    int ipCenterX2 = (SCREEN_WIDTH - ipW2) / 2;
    
    // Position lines to fit nicely in 32 pixels
    int ipLine1Y = 4;   // Start a bit from top
    int ipLine2Y = 20;  // Second line lower
    
    // Display AP IP - centered
    display->setCursor(ipCenterX1, ipLine1Y);
    display->print(apIP);
    
    // Display STA IP - centered
    display->setCursor(ipCenterX2, ipLine2Y);
    display->print(staIP);
    
    // Show for 3 seconds
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

void Display::drawBluetoothStatus() {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Only draw if we have a bluetooth instance and it's connected
    if (bluetoothPtr && bluetoothPtr->isConnected()) {
        // Draw a small "BT" in the top-right corner
        display->setTextSize(1);
        display->setCursor(108, 0); // Position at top-right (128-20=108 pixels from left)
        display->print("BT");
    }
}

void Display::drawWeight(float weight) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    display->clearDisplay();
    
    // Apply deadband to prevent flickering between 0.0g and -0.0g
    // Show 0.0g (without negative sign) when weight is between -0.1g and +0.1g
    float displayWeight = weight;
    if (weight >= -0.1 && weight <= 0.1) {
        displayWeight = 0.0; // Force to exactly 0.0 to avoid negative sign
    }
    
    // Format weight string with consistent spacing
    String weightStr;
    if (displayWeight < 0) {
        weightStr = String(displayWeight, 1); // Keep negative sign for values below -0.1g
    } else {
        weightStr = " " + String(displayWeight, 1); // Add space where negative sign would be
    }
    weightStr += "g";
    
    // Calculate text width for centering weight
    display->setTextSize(2);
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    display->getTextBounds(weightStr, 0, 0, &x1, &y1, &textWidth, &textHeight);
    
    // Center the weight text horizontally
    int centerX = (SCREEN_WIDTH - textWidth) / 2;
    
    // Large weight display - centered at top
    display->setCursor(centerX, 0);
    display->print(weightStr);
    
    // Get flow rate and format it
    float currentFlowRate = 0.0;
    if (flowRatePtr != nullptr) {
        currentFlowRate = flowRatePtr->getFlowRate();
    }
    
    // Apply deadband to prevent flickering between 0.0g/s and -0.0g/s
    // Show 0.0g/s (without negative sign) when flow rate is between -0.1g/s and +0.1g/s
    float displayFlowRate = currentFlowRate;
    if (currentFlowRate >= -0.1 && currentFlowRate <= 0.1) {
        displayFlowRate = 0.0; // Force to exactly 0.0 to avoid negative sign
    }
    
    // Format flow rate string with consistent spacing (shorter format like Auto mode)
    String flowRateStr = "FR: ";
    if (displayFlowRate < 0) {
        flowRateStr += String(displayFlowRate, 1); // Keep negative sign for values below -0.1g/s
    } else {
        flowRateStr += String(displayFlowRate, 1); // No extra space needed for shorter format
    }
    flowRateStr += "g/s";
    
    // Small flow rate text at bottom left (same as Auto mode)
    display->setTextSize(1);
    display->setCursor(0, 24);
    display->print(flowRateStr);
    
    // Draw Bluetooth status if connected
    drawBluetoothStatus();
    
    display->display();
}

void Display::showWeightWithTimer(float weight) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    display->clearDisplay();
    
    // Apply deadband to prevent flickering between 0.0g and -0.0g
    float displayWeight = weight;
    if (weight >= -0.1 && weight <= 0.1) {
        displayWeight = 0.0;
    }
    
    // Format weight string with consistent spacing
    String weightStr;
    if (displayWeight < 0) {
        weightStr = String(displayWeight, 1);
    } else {
        weightStr = " " + String(displayWeight, 1);
    }
    weightStr += "g";
    
    // Calculate text width for centering weight
    display->setTextSize(2);
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    display->getTextBounds(weightStr, 0, 0, &x1, &y1, &textWidth, &textHeight);
    
    // Center the weight text horizontally
    int centerX = (SCREEN_WIDTH - textWidth) / 2;
    
    // Large weight display - centered at top
    display->setCursor(centerX, 0);
    display->print(weightStr);
    
    // Show timer instead of flow rate with decimal precision
    float currentTime = getTimerSeconds();
    String timerStr = "Timer: " + String(currentTime, 1) + "s";
    
    // Small timer text at bottom - centered
    display->setTextSize(1);
    display->getTextBounds(timerStr, 0, 0, &x1, &y1, &textWidth, &textHeight);
    int timerCenterX = (SCREEN_WIDTH - textWidth) / 2;
    display->setCursor(timerCenterX, 24);
    display->print(timerStr);
    
    // Draw Bluetooth status if connected
    drawBluetoothStatus();
    
    display->display();
}

void Display::showWeightWithFlowAndTimer(float weight) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Don't run auto-tare checks if we're showing a message
    if (!showingMessage) {
        checkAutoTare(weight);
    }
    
    // If we're showing a message, don't override it with weight display
    if (showingMessage) {
        return;
    }
    
    display->clearDisplay();
    
    // Apply deadband to prevent flickering between 0.0g and -0.0g
    float displayWeight = weight;
    if (weight >= -0.1 && weight <= 0.1) {
        displayWeight = 0.0;
    }
    
    // Format weight string with consistent spacing
    String weightStr;
    if (displayWeight < 0) {
        weightStr = String(displayWeight, 1);
    } else {
        weightStr = " " + String(displayWeight, 1);
    }
    weightStr += "g";
    
    // Calculate text width for centering weight
    display->setTextSize(2);
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    display->getTextBounds(weightStr, 0, 0, &x1, &y1, &textWidth, &textHeight);
    
    // Center the weight text horizontally
    int centerX = (SCREEN_WIDTH - textWidth) / 2;
    
    // Large weight display - centered at top
    display->setCursor(centerX, 0);
    display->print(weightStr);
    
    // Get flow rate and format it for left side
    float currentFlowRate = 0.0;
    if (flowRatePtr != nullptr) {
        currentFlowRate = flowRatePtr->getFlowRate();
    }
    
    // Auto timer functionality - check flow rate changes
    checkAutoTimer(currentFlowRate);
    
    // Apply deadband to flow rate
    float displayFlowRate = currentFlowRate;
    if (currentFlowRate >= -0.1 && currentFlowRate <= 0.1) {
        displayFlowRate = 0.0;
    }
    
    // Format flow rate string (shorter format for space)
    String flowRateStr = "FR: ";
    if (displayFlowRate < 0) {
        flowRateStr += String(displayFlowRate, 1);
    } else {
        flowRateStr += String(displayFlowRate, 1);
    }
    flowRateStr += "g/s";
    
    // Get timer for right side
    float currentTime = getTimerSeconds();
    String timerStr = String(currentTime, 1) + "s";
    
    // Small text at bottom - flow rate on left, timer on right
    display->setTextSize(1);
    
    // Flow rate on bottom left
    display->setCursor(0, 24);
    display->print(flowRateStr);
    
    // Timer on bottom right (adjusted for Bluetooth indicator if present)
    display->getTextBounds(timerStr, 0, 0, &x1, &y1, &textWidth, &textHeight);
    int timerX = SCREEN_WIDTH - textWidth;
    // If Bluetooth is connected, move timer left a bit to avoid overlap
    if (bluetoothPtr && bluetoothPtr->isConnected()) {
        timerX -= 25; // Move 25 pixels left to avoid "BT" text
    }
    display->setCursor(timerX, 24);
    display->print(timerStr);
    
    // Draw Bluetooth status if connected
    drawBluetoothStatus();
    
    display->display();
}

void Display::showModeMessage(ScaleMode mode) {
    showModeMessage(mode, 1000); // Default 1 second duration
}

void Display::showModeMessage(ScaleMode mode, int duration) {
    // Return early if display is not connected
    if (!displayConnected) {
        return;
    }
    
    // Set message state to prevent weight display interference
    currentMessage = "Mode change message";
    messageStartTime = millis();
    messageDuration = duration;
    showingMessage = true;
    
    // Show mode name in same format as WeighMyBru Ready
    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);
    
    // Get mode name
    String line1, line2;
    switch (mode) {
        case ScaleMode::FLOW:
            line1 = "Flow";
            line2 = "Mode";
            break;
        case ScaleMode::TIME:
            line1 = "Time";
            line2 = "Mode";
            break;
        case ScaleMode::AUTO:
            line1 = "Auto";
            line2 = "Mode";
            break;
    }
    
    int16_t x1, y1;
    uint16_t w1, h1, w2, h2;
    
    // Get text bounds for both lines
    display->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    display->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    
    // Calculate centered positions - tighter spacing for size 2 text
    int centerX1 = (SCREEN_WIDTH - w1) / 2;
    int centerX2 = (SCREEN_WIDTH - w2) / 2;
    
    // Position lines closer together to fit in 32 pixels
    int line1Y = 0;  // Start at top
    int line2Y = 16; // Second line at pixel 16
    
    // Display first line
    display->setCursor(centerX1, line1Y);
    display->print(line1);
    
    // Display second line
    display->setCursor(centerX2, line2Y);
    display->print(line2);
    
    display->display();
}

// Mode management methods
void Display::setMode(ScaleMode mode) {
    setMode(mode, false); // Default behavior - immediate tare
}

void Display::setMode(ScaleMode mode, bool delayTare) {
    if (currentMode != mode) {
        currentMode = mode;
        modeSwitchTime = millis(); // Record when mode was switched
        
        if (delayTare) {
            // Mark that we need to tare after touch release
            pendingModeTare = true;
            Serial.println("Mode switched - tare pending until touch release");
        } else {
            // Auto-tare the scale immediately when switching modes (silent - no message)
            if (scalePtr != nullptr) {
                scalePtr->tare();
            }
        }
        
        // Reset timer for all modes to start fresh
        resetTimer();
        
        // Reset auto mode variables when switching to AUTO mode
        if (mode == ScaleMode::AUTO) {
            autoTareEnabled = true;
            waitingForStabilization = false;
            autoTimerStarted = false;
            autoTareCompletedTime = 0; // Reset timing
            lastWeight = 0.0;
            lastFlowRate = 0.0;
            Serial.println("Auto mode activated - auto-tare and auto-timer enabled");
        }
        
        showModeMessage(mode, delayTare ? 2500 : 1000); // Longer duration for delayed tare (2.5 seconds)
    }
}

ScaleMode Display::getMode() const {
    return currentMode;
}

void Display::nextMode() {
    nextMode(false); // Default behavior - immediate tare
}

void Display::nextMode(bool delayTare) {
    switch (currentMode) {
        case ScaleMode::FLOW:
            setMode(ScaleMode::TIME, delayTare);
            break;
        case ScaleMode::TIME:
            setMode(ScaleMode::AUTO, delayTare);
            break;
        case ScaleMode::AUTO:
            setMode(ScaleMode::FLOW, delayTare);
            break;
    }
}

// Timer management methods
void Display::startTimer() {
    if (!timerRunning) {
        timerStartTime = millis();
        timerRunning = true;
        timerPaused = false;
        
        // Start flow rate averaging when timer starts
        if (flowRatePtr != nullptr) {
            flowRatePtr->startTimerAveraging();
        }
    }
}

void Display::stopTimer() {
    if (timerRunning && !timerPaused) {
        timerPausedTime = millis() - timerStartTime;
        timerPaused = true;
        
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
    
    // Reset flow rate averaging when timer is reset
    if (flowRatePtr != nullptr) {
        flowRatePtr->resetTimerAveraging();
    }
}

bool Display::isTimerRunning() const {
    return timerRunning && !timerPaused;
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

bool Display::isAutoTimerActive() const {
    return autoTimerStarted && timerRunning;
}

void Display::stopAutoTimer() {
    if (autoTimerStarted && timerRunning) {
        stopTimer();
        Serial.println("Auto-timer stopped by power button");
    }
}

void Display::completePendingModeTare() {
    if (pendingModeTare) {
        // Start the two-stage stabilization monitoring process
        // Stage 1: Wait for finger release detection
        waitingForModeTareStabilization = true;
        fingerReleaseDetected = false;
        if (scalePtr != nullptr) {
            fingerPressWeight = scalePtr->getCurrentWeight(); // Record weight with finger on scale
        }
        pendingModeTare = false;
        Serial.println("Started mode tare: waiting for finger release (current weight: " + String(fingerPressWeight) + "g)");
    }
}

void Display::checkAutoTare(float weight) {
    if (!autoTareEnabled) {
        // Check for cup removal when auto-timer is active
        if (autoTimerStarted && timerRunning && weight < 2.0) { // Cup removed (weight < 2g threshold)
            resetAutoSequence();
            Serial.println("Cup removed detected - auto-timer stopped and sequence reset");
            return;
        }
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Don't start auto-tare detection for 5 seconds after mode switch
    // This prevents false triggers from weight changes during mode switching
    if (currentTime - modeSwitchTime < 5000) {
        lastWeight = weight; // Update lastWeight to prevent false detection later
        return;
    } else {
        // Show debug message only once when stabilization period ends
        if (!stabilizationMessageShown) {
            Serial.println("Auto-tare detection now active - looking for cup placement");
            stabilizationMessageShown = true;
        }
    }
    
    float weightChange = weight - lastWeight; // Keep sign to distinguish increase vs decrease
    
    // Detect significant weight INCREASE (more than 10g) - cup placed
    // Increased threshold from 5g to 10g to reduce false triggers from vibrations/noise
    // Only trigger on positive weight changes to avoid false triggers from finger removal
    if (weightChange > 10.0 && !waitingForStabilization) {
        waitingForStabilization = true;
        weightWhenChanged = weight;
        stabilizationStartTime = currentTime;
        lastWeightChangeTime = currentTime;
        Serial.printf("Cup detected - weight change: %.2fg (from %.2fg to %.2fg) - waiting for weight to stabilize...\n", 
                     weightChange, lastWeight, weight);
    }
    
    // Check if weight is stabilizing
    if (waitingForStabilization) {
        float currentChange = abs(weight - weightWhenChanged);
        
        // If weight changed significantly from when we started waiting, reset timer
        if (currentChange > 1.0) { // Reduced from 2.0g to 1.0g for faster detection
            weightWhenChanged = weight;
            stabilizationStartTime = currentTime;
            Serial.printf("Weight still changing: %.2fg - resetting stabilization timer\n", currentChange);
        }
        // If weight has been stable for 1 second, auto-tare (reduced from 2 seconds)
        else if (currentTime - stabilizationStartTime > 1000) {
            if (scalePtr != nullptr) {
                scalePtr->tare();
                Serial.println("Auto-tare executed - cup weight zeroed");
                showAutoTaredMessage(); // Use dedicated method
            }
            waitingForStabilization = false;
            autoTareEnabled = false; // Disable auto-tare, now ready for timer
            autoTareCompletedTime = millis(); // Set completion time to add delay before timer
            Serial.println("Auto-tare complete - auto-timer enabled with delay");
        }
    }
    
    lastWeight = weight;
}

void Display::checkAutoTimer(float flowRate) {
    // Only check auto timer in AUTO mode and when auto-tare is disabled
    if (!autoTareEnabled && !waitingForStabilization) {
        unsigned long currentTime = millis();
        
        // Simple 2-second delay after any tare operation
        if (autoTareCompletedTime > 0 && currentTime - autoTareCompletedTime < 2000) {
            return;
        }
        
        // Start timer when flow rate increases above threshold (0.2g/s)
        if (!timerRunning && !autoTimerStarted && flowRate > 0.2) {
            startTimer();
            autoTimerStarted = true;
            
            // Reset PowerManager timer state to sync when auto timer starts
            if (powerManagerPtr != nullptr) {
                powerManagerPtr->resetTimerState();
            }
            
            Serial.printf("Auto-timer started - coffee flow detected (%.3f g/s)\n", flowRate);
        }
    }
    
    lastFlowRate = flowRate;
}

void Display::resetAutoSequence() {
    if (currentMode == ScaleMode::AUTO) {
        // Stop timer if it was auto-started (preserve the time, don't reset)
        if (timerRunning && autoTimerStarted) {
            stopTimer(); // Stop timer instead of reset to preserve final time
            Serial.println("Auto-timer stopped - cup removed, time preserved");
        }
        
        autoTareEnabled = true;
        waitingForStabilization = false;
        autoTimerStarted = false;
        autoTareCompletedTime = 0; // Reset timing
        lastWeight = 0.0;
        lastFlowRate = 0.0;
        
        // Clear flow rate buffer to ensure clean start for next cycle
        if (flowRatePtr != nullptr) {
            flowRatePtr->clearFlowRateBuffer();
        }
        
        Serial.println("Auto sequence reset - ready for new auto-tare cycle, flow rate cleared");
    }
}

void Display::completeTareOperation() {
    if (currentMode == ScaleMode::AUTO) {
        // Manual tare completed - reset for next auto-tare cycle
        autoTareEnabled = true;           // Re-enable auto-tare for next cycle
        waitingForStabilization = false;  // Clear any pending stabilization
        autoTimerStarted = false;         // Reset timer state
        autoTareCompletedTime = millis(); // Set completion time for delay
        
        // Clear flow rate buffer to prevent false triggers from tare operation
        if (flowRatePtr != nullptr) {
            flowRatePtr->clearFlowRateBuffer();
        }
        
        Serial.println("Manual tare completed - auto-tare re-enabled for next cycle");
    }
}
