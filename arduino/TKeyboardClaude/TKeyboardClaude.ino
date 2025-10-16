/*
 * T-Keyboard-S3 Claude Code Controller
 * Main Arduino sketch for ESP32-S3 with 4 keys and 4 displays
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <esp_task_wdt.h>
#include <FastLED.h>
#include <Arduino_GFX_Library.h>

// Hardware Pin Definitions (from T-Keyboard-S3 specs)
#define KEY1_PIN 10
#define KEY2_PIN 9
#define KEY3_PIN 46
#define KEY4_PIN 3

#define TFT_DC 45
#define TFT_SCLK 47
#define TFT_MOSI 48
#define TFT_RST 38
#define TFT_BL 39

#define TFT_CS1 12
#define TFT_CS2 13
#define TFT_CS3 14
#define TFT_CS4 21

#define WS2812_DATA_PIN 11
#define NUM_LEDS 4

// Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128

// Network Configuration
#define AP_SSID "TKeyboard-Setup"
#define AP_PASSWORD "12345678"
#define WEBSOCKET_RECONNECT_INTERVAL 5000
#define WEBSOCKET_PING_INTERVAL 30000
#define WDT_TIMEOUT 30

// SPIFFS Configuration
#define IMAGE_CACHE_PATH "/images/"
#define MAX_CACHED_IMAGES 50

// Global Objects
Preferences preferences;
WebSocketsClient webSocket;
AsyncWebServer configServer(80);
DNSServer dnsServer;
CRGB leds[NUM_LEDS];

// Display Objects (GC9107 driver)
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS1, TFT_SCLK, TFT_MOSI, -1);
Arduino_GFX *displays[4];

// State Variables
struct SystemState {
    bool wifiConfigMode = false;
    bool wsConnected = false;
    String sessionId = "";
    String bridgeHost = "";
    int bridgePort = 8080;
    uint8_t backlightIntensity = 128;  // 50% default
    unsigned long lastPing = 0;
    unsigned long lastReconnect = 0;
    bool keyStates[4] = {false, false, false, false};
    unsigned long keyDebounce[4] = {0, 0, 0, 0};
} state;

struct KeyOption {
    String text;
    String imagePath;
    uint32_t color;
    bool hasImage;
};

KeyOption currentOptions[4];
bool optionsUpdated = false;

// Claude State
enum ClaudeState {
    IDLE,
    THINKING,
    ERROR,
    RATE_LIMITED,
    WAITING_INPUT
};

ClaudeState claudeState = IDLE;
int rateLimitCountdown = 0;
unsigned long countdownLastUpdate = 0;

// Function Declarations
void setupHardware();
void setupWiFi();
void setupWebSocket();
void setupConfigServer();
void setupSPIFFS();
void handleKeyPress(int key);
void updateDisplays();
void setLEDStatus();
void loadPreferences();
void savePreferences();
void enterConfigMode();
void handleWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void processClaudeMessage(JsonDocument& doc);
void sendKeyPress(int key);
bool loadImageFromSPIFFS(const String& path, uint8_t display);
void drawTextOption(uint8_t display, const String& text, uint32_t color);
void drawCountdown(int seconds);
void initializeDisplays();
void selectDisplay(uint8_t display);

// Interrupt handlers
volatile bool keyInterrupts[4] = {false, false, false, false};

void IRAM_ATTR key1ISR() { keyInterrupts[0] = true; }
void IRAM_ATTR key2ISR() { keyInterrupts[1] = true; }
void IRAM_ATTR key3ISR() { keyInterrupts[2] = true; }
void IRAM_ATTR key4ISR() { keyInterrupts[3] = true; }

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("T-Keyboard Claude Controller Starting...");

    // Enable watchdog
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);

    // Initialize PSRAM
    if (psramFound()) {
        Serial.printf("PSRAM found: %d bytes\n", ESP.getPsramSize());
    } else {
        Serial.println("Warning: PSRAM not found!");
    }

    setupHardware();
    loadPreferences();
    setupSPIFFS();

    if (state.wifiConfigMode) {
        enterConfigMode();
    } else {
        setupWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            setupWebSocket();
        }
    }

    Serial.println("Setup complete!");
}

void loop() {
    esp_task_wdt_reset();  // Reset watchdog

    // Handle key interrupts
    for (int i = 0; i < 4; i++) {
        if (keyInterrupts[i]) {
            keyInterrupts[i] = false;
            if (millis() - keyDebounce[i] > 100) {  // Debounce
                keyDebounce[i] = millis();
                handleKeyPress(i + 1);
            }
        }
    }

    // WebSocket handling
    if (WiFi.status() == WL_CONNECTED) {
        webSocket.loop();

        // Ping to keep connection alive
        if (state.wsConnected && millis() - state.lastPing > WEBSOCKET_PING_INTERVAL) {
            webSocket.sendPing();
            state.lastPing = millis();
        }

        // Reconnect if needed
        if (!state.wsConnected && millis() - state.lastReconnect > WEBSOCKET_RECONNECT_INTERVAL) {
            Serial.println("Attempting WebSocket reconnect...");
            setupWebSocket();
            state.lastReconnect = millis();
        }
    } else if (!state.wifiConfigMode) {
        // Try to reconnect WiFi
        static unsigned long lastWiFiRetry = 0;
        if (millis() - lastWiFiRetry > 30000) {
            Serial.println("WiFi disconnected, attempting reconnect...");
            setupWiFi();
            lastWiFiRetry = millis();
        }
    }

    // Update displays if needed
    if (optionsUpdated) {
        updateDisplays();
        optionsUpdated = false;
    }

    // Update countdown if in rate limit state
    if (claudeState == RATE_LIMITED && millis() - countdownLastUpdate > 1000) {
        if (rateLimitCountdown > 0) {
            rateLimitCountdown--;
            drawCountdown(rateLimitCountdown);
        } else {
            claudeState = IDLE;
            optionsUpdated = true;
        }
        countdownLastUpdate = millis();
    }

    // Update LED status
    static unsigned long lastLEDUpdate = 0;
    if (millis() - lastLEDUpdate > 100) {
        setLEDStatus();
        lastLEDUpdate = millis();
    }

    // Handle config mode
    if (state.wifiConfigMode) {
        dnsServer.processNextRequest();
    }

    delay(10);  // Small delay to prevent tight looping
}

void setupHardware() {
    // Initialize keys with interrupts
    pinMode(KEY1_PIN, INPUT_PULLUP);
    pinMode(KEY2_PIN, INPUT_PULLUP);
    pinMode(KEY3_PIN, INPUT_PULLUP);
    pinMode(KEY4_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(KEY1_PIN), key1ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY2_PIN), key2ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY3_PIN), key3ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY4_PIN), key4ISR, FALLING);

    // Initialize RGB LEDs
    FastLED.addLeds<WS2812C, WS2812_DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(64);  // Start at 25% brightness

    // Initialize displays
    initializeDisplays();

    // Setup backlight PWM
    ledcSetup(0, 5000, 8);  // Channel 0, 5kHz, 8-bit resolution
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, state.backlightIntensity);

    Serial.println("Hardware initialized");
}

void initializeDisplays() {
    // Initialize all 4 displays with shared bus
    pinMode(TFT_CS1, OUTPUT);
    pinMode(TFT_CS2, OUTPUT);
    pinMode(TFT_CS3, OUTPUT);
    pinMode(TFT_CS4, OUTPUT);

    digitalWrite(TFT_CS1, HIGH);
    digitalWrite(TFT_CS2, HIGH);
    digitalWrite(TFT_CS3, HIGH);
    digitalWrite(TFT_CS4, HIGH);

    // Create display objects for each CS pin
    displays[0] = new Arduino_GC9107(bus, TFT_RST, 0 /* rotation */);
    displays[1] = new Arduino_GC9107(bus, TFT_RST, 0);
    displays[2] = new Arduino_GC9107(bus, TFT_RST, 0);
    displays[3] = new Arduino_GC9107(bus, TFT_RST, 0);

    // Initialize each display
    for (int i = 0; i < 4; i++) {
        selectDisplay(i);
        displays[i]->begin();
        displays[i]->fillScreen(BLACK);
        displays[i]->setTextColor(WHITE);
        displays[i]->setTextSize(2);
        displays[i]->setCursor(20, 56);
        displays[i]->print("Ready");
    }

    Serial.println("Displays initialized");
}

void selectDisplay(uint8_t display) {
    // Deselect all displays
    digitalWrite(TFT_CS1, HIGH);
    digitalWrite(TFT_CS2, HIGH);
    digitalWrite(TFT_CS3, HIGH);
    digitalWrite(TFT_CS4, HIGH);

    // Select the target display
    switch(display) {
        case 0: digitalWrite(TFT_CS1, LOW); break;
        case 1: digitalWrite(TFT_CS2, LOW); break;
        case 2: digitalWrite(TFT_CS3, LOW); break;
        case 3: digitalWrite(TFT_CS4, LOW); break;
    }
}

void setupWiFi() {
    if (preferences.getString("wifi_ssid", "").length() == 0) {
        Serial.println("No WiFi credentials, entering config mode");
        state.wifiConfigMode = true;
        enterConfigMode();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(
        preferences.getString("wifi_ssid", "").c_str(),
        preferences.getString("wifi_pass", "").c_str()
    );

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;

        // Pulse blue LED while connecting
        static uint8_t brightness = 0;
        static int8_t direction = 1;
        brightness += direction * 5;
        if (brightness >= 255 || brightness <= 0) direction = -direction;
        fill_solid(leds, NUM_LEDS, CRGB(0, 0, brightness));
        FastLED.show();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        // Green LEDs for success
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
    } else {
        Serial.println("\nWiFi connection failed!");
        state.wifiConfigMode = true;
        enterConfigMode();
    }
}

void setupWebSocket() {
    String host = preferences.getString("bridge_host", "");
    int port = preferences.getInt("bridge_port", 8080);

    if (host.length() == 0) {
        Serial.println("No bridge server configured");
        return;
    }

    Serial.printf("Connecting to WebSocket: %s:%d\n", host.c_str(), port);

    webSocket.begin(host, port, "/ws");
    webSocket.onEvent(handleWebSocketEvent);
    webSocket.setReconnectInterval(5000);
    webSocket.enableHeartbeat(15000, 3000, 2);
}

void handleWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("WebSocket disconnected");
            state.wsConnected = false;
            // Red LEDs for disconnect
            fill_solid(leds, NUM_LEDS, CRGB::Red);
            FastLED.show();
            break;

        case WStype_CONNECTED:
            Serial.println("WebSocket connected");
            state.wsConnected = true;
            // Green LEDs for connected
            fill_solid(leds, NUM_LEDS, CRGB::Green);
            FastLED.show();

            // Send registration message
            JsonDocument doc;
            doc["type"] = "register";
            doc["device"] = "tkeyboard";
            doc["version"] = "1.0";

            String json;
            serializeJson(doc, json);
            webSocket.sendTXT(json);
            break;

        case WStype_TEXT: {
            Serial.printf("Received: %s\n", payload);

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload, length);

            if (!error) {
                processClaudeMessage(doc);
            } else {
                Serial.printf("JSON parse error: %s\n", error.c_str());
            }
            break;
        }

        case WStype_PING:
            Serial.println("Ping received");
            break;

        case WStype_PONG:
            Serial.println("Pong received");
            break;

        default:
            break;
    }
}

void processClaudeMessage(JsonDocument& doc) {
    String type = doc["type"];

    if (type == "update_options") {
        state.sessionId = doc["session_id"].as<String>();
        JsonArray options = doc["options"];

        for (int i = 0; i < 4 && i < options.size(); i++) {
            currentOptions[i].text = options[i]["text"].as<String>();
            currentOptions[i].imagePath = options[i]["image"].as<String>();
            currentOptions[i].color = strtoul(options[i]["color"].as<String>().substring(1).c_str(), NULL, 16);
            currentOptions[i].hasImage = currentOptions[i].imagePath.length() > 0;
        }

        optionsUpdated = true;

    } else if (type == "status") {
        String stateStr = doc["state"];
        if (stateStr == "thinking") {
            claudeState = THINKING;
        } else if (stateStr == "idle") {
            claudeState = IDLE;
        } else if (stateStr == "error") {
            claudeState = ERROR;
        } else if (stateStr == "limit") {
            claudeState = RATE_LIMITED;
            rateLimitCountdown = doc["countdown"];
            countdownLastUpdate = millis();
        }

        optionsUpdated = true;

    } else if (type == "image") {
        // Handle image data transfer
        String name = doc["name"];
        String data = doc["data"];  // Base64 encoded

        // Save to SPIFFS
        File file = SPIFFS.open(IMAGE_CACHE_PATH + name, FILE_WRITE);
        if (file) {
            // Decode base64 and write
            // Implementation needed for base64 decoding
            file.close();
            Serial.printf("Saved image: %s\n", name.c_str());
        }
    }
}

void handleKeyPress(int key) {
    Serial.printf("Key %d pressed\n", key);

    // Visual feedback - flash the corresponding LED
    CRGB originalColor = leds[key - 1];
    leds[key - 1] = CRGB::White;
    FastLED.show();
    delay(50);
    leds[key - 1] = originalColor;
    FastLED.show();

    // Handle special states
    if (claudeState == RATE_LIMITED && key == 1) {
        // Continue button during rate limit
        if (rateLimitCountdown <= 0) {
            sendKeyPress(key);
            claudeState = IDLE;
            optionsUpdated = true;
        }
        return;
    }

    // Handle config mode key combo (keys 1+4)
    static unsigned long key1PressTime = 0;
    static unsigned long key4PressTime = 0;

    if (key == 1) key1PressTime = millis();
    if (key == 4) key4PressTime = millis();

    if (abs((int)(key1PressTime - key4PressTime)) < 500) {
        // Both keys pressed within 500ms
        Serial.println("Entering config mode...");
        state.wifiConfigMode = true;
        ESP.restart();
        return;
    }

    // Normal key press
    sendKeyPress(key);
}

void sendKeyPress(int key) {
    if (!state.wsConnected) {
        Serial.println("WebSocket not connected");
        return;
    }

    JsonDocument doc;
    doc["type"] = "key_press";
    doc["session_id"] = state.sessionId;
    doc["key"] = key;
    doc["text"] = currentOptions[key - 1].text;

    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);

    Serial.printf("Sent: %s\n", json.c_str());
}

void updateDisplays() {
    for (int i = 0; i < 4; i++) {
        selectDisplay(i);

        if (claudeState == RATE_LIMITED) {
            // Special display for rate limit
            if (i == 0) {
                // Show countdown on first display
                drawCountdown(rateLimitCountdown);
            } else {
                displays[i]->fillScreen(BLACK);
            }
        } else if (currentOptions[i].hasImage) {
            // Try to load and display image
            if (!loadImageFromSPIFFS(currentOptions[i].imagePath, i)) {
                // Fall back to text if image fails
                drawTextOption(i, currentOptions[i].text, currentOptions[i].color);
            }
        } else {
            // Display text option
            drawTextOption(i, currentOptions[i].text, currentOptions[i].color);
        }
    }
}

bool loadImageFromSPIFFS(const String& path, uint8_t display) {
    String fullPath = IMAGE_CACHE_PATH + path;

    if (!SPIFFS.exists(fullPath)) {
        Serial.printf("Image not found: %s\n", fullPath.c_str());
        return false;
    }

    File file = SPIFFS.open(fullPath, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open image: %s\n", fullPath.c_str());
        return false;
    }

    // Read RGB565 image data (128x128x2 = 32768 bytes)
    const size_t imageSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;
    uint8_t* buffer = (uint8_t*)ps_malloc(imageSize);  // Use PSRAM if available

    if (!buffer) {
        buffer = (uint8_t*)malloc(imageSize);  // Fall back to regular RAM
    }

    if (!buffer) {
        Serial.println("Failed to allocate image buffer");
        file.close();
        return false;
    }

    size_t bytesRead = file.read(buffer, imageSize);
    file.close();

    if (bytesRead != imageSize) {
        Serial.printf("Image size mismatch: %d != %d\n", bytesRead, imageSize);
        free(buffer);
        return false;
    }

    // Draw image to display
    selectDisplay(display);
    displays[display]->draw16bitRGBBitmap(0, 0, (uint16_t*)buffer, SCREEN_WIDTH, SCREEN_HEIGHT);

    free(buffer);
    return true;
}

void drawTextOption(uint8_t display, const String& text, uint32_t color) {
    selectDisplay(display);
    displays[display]->fillScreen(BLACK);

    // Set text color from RGB value
    uint16_t color565 = ((color & 0xF80000) >> 8) |
                        ((color & 0x00FC00) >> 5) |
                        ((color & 0x0000F8) >> 3);
    displays[display]->setTextColor(color565);

    // Center text
    int16_t x = (SCREEN_WIDTH - text.length() * 12) / 2;
    int16_t y = (SCREEN_HEIGHT - 16) / 2;

    displays[display]->setCursor(x, y);
    displays[display]->setTextSize(2);
    displays[display]->print(text);
}

void drawCountdown(int seconds) {
    selectDisplay(0);
    displays[0]->fillScreen(BLACK);
    displays[0]->setTextColor(YELLOW);
    displays[0]->setTextSize(4);

    String timeStr;
    if (seconds > 0) {
        int mins = seconds / 60;
        int secs = seconds % 60;
        timeStr = String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
    } else {
        timeStr = "NOW";
    }

    int16_t x = (SCREEN_WIDTH - timeStr.length() * 24) / 2;
    displays[0]->setCursor(x, 40);
    displays[0]->print(timeStr);

    // Show "Continue" text below
    displays[0]->setTextSize(1);
    displays[0]->setCursor(35, 90);
    displays[0]->print("Press to");
    displays[0]->setCursor(35, 100);
    displays[0]->print("continue");
}

void setLEDStatus() {
    static uint8_t pulseValue = 0;
    static int8_t pulseDirection = 1;

    switch(claudeState) {
        case IDLE:
            // Solid green when ready
            fill_solid(leds, NUM_LEDS, CRGB(0, 64, 0));
            break;

        case THINKING:
            // Pulsing yellow
            pulseValue += pulseDirection * 2;
            if (pulseValue >= 64 || pulseValue <= 0) pulseDirection = -pulseDirection;
            fill_solid(leds, NUM_LEDS, CRGB(pulseValue, pulseValue, 0));
            break;

        case ERROR:
            // Solid red
            fill_solid(leds, NUM_LEDS, CRGB(64, 0, 0));
            break;

        case RATE_LIMITED:
            // Slow pulsing red
            pulseValue += pulseDirection;
            if (pulseValue >= 64 || pulseValue <= 10) pulseDirection = -pulseDirection;
            fill_solid(leds, NUM_LEDS, CRGB(pulseValue, 0, 0));
            break;

        case WAITING_INPUT:
            // Solid blue
            fill_solid(leds, NUM_LEDS, CRGB(0, 0, 64));
            break;
    }

    if (!state.wsConnected && !state.wifiConfigMode) {
        // Override with connection status - fast red blink
        static bool blink = false;
        blink = !blink;
        fill_solid(leds, NUM_LEDS, blink ? CRGB(64, 0, 0) : CRGB(0, 0, 0));
    } else if (state.wifiConfigMode) {
        // Config mode - blue breathe
        pulseValue += pulseDirection * 2;
        if (pulseValue >= 64 || pulseValue <= 0) pulseDirection = -pulseDirection;
        fill_solid(leds, NUM_LEDS, CRGB(0, 0, pulseValue));
    }

    FastLED.show();
}

void setupSPIFFS() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed!");
        return;
    }

    Serial.printf("SPIFFS: %d bytes used of %d\n",
                  SPIFFS.usedBytes(), SPIFFS.totalBytes());

    // Create image cache directory if it doesn't exist
    if (!SPIFFS.exists(IMAGE_CACHE_PATH)) {
        // SPIFFS doesn't support directories, but we use path prefixes
        Serial.println("SPIFFS ready for image caching");
    }
}

void loadPreferences() {
    preferences.begin("tkeyboard", false);

    state.backlightIntensity = preferences.getUChar("backlight", 128);
    state.bridgeHost = preferences.getString("bridge_host", "");
    state.bridgePort = preferences.getInt("bridge_port", 8080);

    Serial.println("Preferences loaded");
}

void savePreferences() {
    preferences.putUChar("backlight", state.backlightIntensity);
    preferences.putString("bridge_host", state.bridgeHost);
    preferences.putInt("bridge_port", state.bridgePort);

    Serial.println("Preferences saved");
}

void enterConfigMode() {
    Serial.println("Entering WiFi configuration mode");

    // Create access point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    // Setup DNS server to redirect all requests
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Setup web server
    setupConfigServer();

    // Display config mode on screens
    for (int i = 0; i < 4; i++) {
        selectDisplay(i);
        displays[i]->fillScreen(BLACK);
        displays[i]->setTextColor(CYAN);
        displays[i]->setTextSize(1);

        if (i == 0) {
            displays[i]->setCursor(10, 30);
            displays[i]->print("WiFi Setup");
            displays[i]->setCursor(10, 50);
            displays[i]->print("Connect to:");
            displays[i]->setCursor(10, 70);
            displays[i]->print(AP_SSID);
        } else if (i == 1) {
            displays[i]->setCursor(10, 30);
            displays[i]->print("Password:");
            displays[i]->setCursor(10, 50);
            displays[i]->print(AP_PASSWORD);
        } else if (i == 2) {
            displays[i]->setCursor(10, 30);
            displays[i]->print("Then visit:");
            displays[i]->setCursor(10, 50);
            displays[i]->print("192.168.4.1");
        }
    }

    state.wifiConfigMode = true;
}

void setupConfigServer() {
    // Serve configuration page
    configServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>T-Keyboard Configuration</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 400px; margin: 0 auto; }
        h1 { color: #4CAF50; text-align: center; }
        input, select { width: 100%; padding: 10px; margin: 10px 0; box-sizing: border-box;
                        background: #2a2a2a; border: 1px solid #4CAF50; color: #fff; }
        button { background: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0;
                 border: none; cursor: pointer; width: 100%; }
        button:hover { background: #45a049; }
        .section { background: #2a2a2a; padding: 20px; margin: 20px 0; border-radius: 5px; }
        label { display: block; margin-top: 10px; color: #aaa; }
        .range-value { text-align: center; color: #4CAF50; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>T-Keyboard Setup</h1>

        <div class='section'>
            <h2>WiFi Configuration</h2>
            <form action='/save' method='POST'>
                <label>Network Name (SSID):</label>
                <input type='text' name='ssid' required>

                <label>Password:</label>
                <input type='password' name='pass' required>

                <label>Bridge Server Host:</label>
                <input type='text' name='host' placeholder='192.168.1.100' required>

                <label>Bridge Server Port:</label>
                <input type='number' name='port' value='8080' required>

                <label>Backlight Intensity:</label>
                <input type='range' name='backlight' min='10' max='255' value='128'
                       oninput='this.nextElementSibling.value = Math.round(this.value/2.55) + "%"'>
                <div class='range-value'>50%</div>

                <button type='submit'>Save & Restart</button>
            </form>
        </div>

        <div class='section'>
            <h3>Status</h3>
            <p>Device: T-Keyboard-S3</p>
            <p>Version: 1.0</p>
            <p>Free Heap: )" + String(ESP.getFreeHeap()) + R"( bytes</p>
            <p>PSRAM: )" + String(ESP.getPsramSize()) + R"( bytes</p>
        </div>
    </div>
</body>
</html>
)";
        request->send(200, "text/html", html);
    });

    // Handle form submission
    configServer.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("pass", true)) {
            String ssid = request->getParam("ssid", true)->value();
            String pass = request->getParam("pass", true)->value();
            String host = request->getParam("host", true)->value();
            int port = request->getParam("port", true)->value().toInt();
            int backlight = request->getParam("backlight", true)->value().toInt();

            // Save to preferences
            preferences.putString("wifi_ssid", ssid);
            preferences.putString("wifi_pass", pass);
            preferences.putString("bridge_host", host);
            preferences.putInt("bridge_port", port);
            preferences.putUChar("backlight", backlight);

            request->send(200, "text/html",
                "<html><body><h1>Settings Saved!</h1><p>Device will restart in 3 seconds...</p></body></html>");

            delay(3000);
            ESP.restart();
        } else {
            request->send(400, "text/html", "Missing parameters");
        }
    });

    configServer.begin();
    Serial.println("Config server started");
}