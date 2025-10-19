/*
 * T-Keyboard-S3 Claude Code Controller
 * Main Arduino sketch for ESP32-S3 with 4 keys and 4 displays
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <T-Keyboard_S3_Drive.h>
#include <vector>
#include <esp_task_wdt.h>

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
#define WATCHDOG_TIMEOUT_MS 5000  // 5 second watchdog timeout

// Global Objects
Preferences preferences;
WebSocketsClient webSocket;
WebServer configServer(80);
DNSServer dnsServer;
CRGB leds[NUM_LEDS];

// Display Object - Single TFT_eSPI instance with manual CS switching via N085_Screen_Set()
TFT_eSPI tft = TFT_eSPI();  // Single display object for all 4 screens

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
    String text;        // Display text (label shown on button)
    String action;      // Action text (what gets sent when pressed) - if empty, uses text
    String imagePath;   // Image filename (e.g., "stop.rgb")
    uint32_t color;     // Button background color
    bool hasImage;      // Whether an image should be displayed
};

KeyOption currentOptions[4];
bool optionsUpdated = false;

// Forward Declarations (needed for FSM code)
void drawTextOption(uint8_t displayIndex, const String& text, uint32_t color);
void drawLargeText(uint8_t displayIndex, const String& text, uint32_t color);
void drawRateLimitTimer();
void drawErrorIcon();
bool loadImageFromSPIFFS(const String& path, uint8_t display);
bool downloadImageHTTP(const String& imagePath, const String& serverHost, uint16_t serverPort);
void ensureSPIFFSSpace(size_t requiredBytes);
void deleteRandomImage();
void disableWatchdog();
void enableWatchdog();
void feedWatchdog();

// ============================================================================
// FINITE STATE MACHINE ARCHITECTURE
// ============================================================================

// State definitions
enum ClaudeState {
    CONNECTING,      // Waiting for WebSocket connection
    IDLE,
    THINKING,
    ERROR,
    RATE_LIMITED,
    WAITING_INPUT
};

// FSM state tracking
struct FSMState {
    ClaudeState current;
    ClaudeState previous;

    // State-specific data
    unsigned long rateLimitStartTime;
    int rateLimitCountdown;  // 0 = unknown duration

    // Display override flags - allow display_update messages to persist
    bool displayOverride[4];  // True if display was manually set via display_update

    // State entry time (for animations and timeouts)
    unsigned long stateEntryTime;
} fsm;

// Initialize FSM
void initFSM() {
    fsm.current = CONNECTING;
    fsm.previous = CONNECTING;
    fsm.rateLimitStartTime = 0;
    fsm.rateLimitCountdown = 0;
    fsm.stateEntryTime = millis();
    for (int i = 0; i < 4; i++) {
        fsm.displayOverride[i] = false;
    }
}

// State transition function - ensures clean transitions
void transitionToState(ClaudeState newState) {
    if (fsm.current == newState) return;  // No-op if already in this state

    // Exit actions for current state
    exitState(fsm.current);

    // Update state
    fsm.previous = fsm.current;
    fsm.current = newState;
    fsm.stateEntryTime = millis();

    // Clear display overrides on state change
    for (int i = 0; i < 4; i++) {
        fsm.displayOverride[i] = false;
    }

    // Entry actions for new state
    enterState(newState);

    Serial.printf("FSM: %d -> %d\n", fsm.previous, fsm.current);
}

// State entry actions
void enterState(ClaudeState state) {
    switch (state) {
        case CONNECTING:
            // Waiting for WebSocket connection
            break;

        case IDLE:
            // Clear any state-specific data
            break;

        case THINKING:
            // Nothing special needed
            break;

        case ERROR:
            // Reset error animation
            break;

        case RATE_LIMITED:
            // Record start time if we don't have one
            if (fsm.rateLimitStartTime == 0) {
                fsm.rateLimitStartTime = millis();
            }
            break;

        case WAITING_INPUT:
            // Nothing special needed
            break;
    }

    // Render the new state's UI
    renderStateUI();
}

// State exit actions
void exitState(ClaudeState state) {
    switch (state) {
        case RATE_LIMITED:
            // Clear rate limit data when exiting
            fsm.rateLimitStartTime = 0;
            fsm.rateLimitCountdown = 0;
            break;

        default:
            // Most states don't need exit actions
            break;
    }
}

// Render UI for current state - only draws non-overridden displays
void renderStateUI() {
    for (int i = 0; i < 4; i++) {
        // Skip displays that have manual overrides
        if (fsm.displayOverride[i]) continue;

        renderDisplayForState(i, fsm.current);
    }
}

// Render a single display based on current state
void renderDisplayForState(int displayIndex, ClaudeState state) {
    switch (state) {
        case CONNECTING:
            // Waiting for WebSocket - show "Wait | ing | ..." on keys 2-4, blank on key 1
            switch (displayIndex) {
                case 0:
                    // Key 1 - blank
                    selectDisplay(0);
                    tft.fillScreen(TFT_BLACK);
                    break;
                case 1:
                    // Key 2 - "Wait"
                    drawTextOption(1, "Wait", 0x00FFFF);  // Cyan
                    break;
                case 2:
                    // Key 3 - "ing"
                    drawTextOption(2, "ing", 0xFFFFFF);  // White
                    break;
                case 3:
                    // Key 4 - "..."
                    drawTextOption(3, "...", 0x00FF00);  // Green
                    break;
            }
            break;

        case IDLE:
        case THINKING:
        case WAITING_INPUT:
            // Show current options
            if (currentOptions[displayIndex].hasImage) {
                if (!loadImageFromSPIFFS(currentOptions[displayIndex].imagePath, displayIndex)) {
                    drawTextOption(displayIndex, currentOptions[displayIndex].text, currentOptions[displayIndex].color);
                }
            } else {
                drawTextOption(displayIndex, currentOptions[displayIndex].text, currentOptions[displayIndex].color);
            }
            break;

        case RATE_LIMITED:
            // Spread rate limit UI across 4 displays
            switch (displayIndex) {
                case 0:
                    drawLargeText(0, "RATE", 0xFFFF00);  // Yellow
                    break;
                case 1:
                    drawLargeText(1, "LIMIT", 0xFFFF00);  // Yellow
                    break;
                case 2:
                    // Timer display - updated separately in loop()
                    drawRateLimitTimer();
                    break;
                case 3:
                    drawTextOption(3, "Continue", 0x00FF00);  // Green
                    break;
            }
            break;

        case ERROR:
            // Spread error UI across 4 displays
            switch (displayIndex) {
                case 0:
                    drawLargeText(0, "ERROR", 0xFF0000);  // Red
                    break;
                case 1:
                    // Animated error icon - updated separately in loop()
                    drawErrorIcon();
                    break;
                case 2:
                    drawTextOption(2, "Continue", 0x00FF00);  // Green
                    break;
                case 3:
                    drawTextOption(3, "Retry", 0xFFA500);  // Orange
                    break;
            }
            break;
    }
}

// Update displays - called when options change
void updateDisplaysIfNeeded() {
    // Only update displays that are not manually overridden
    renderStateUI();
}

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
void drawTextOption(uint8_t displayIndex, const String& text, uint32_t color);
void drawLargeText(uint8_t displayIndex, const String& text, uint32_t color);
void drawRateLimitTimer();
void drawErrorIcon();
void initializeDisplays();
void handleSerialCommands();

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

    // Initialize PSRAM
    if (psramFound()) {
        Serial.printf("PSRAM found: %d bytes\n", ESP.getPsramSize());
    } else {
        Serial.println("Warning: PSRAM not found!");
    }

    // Initialize FSM
    initFSM();

    setupHardware();
    loadPreferences();

    setupSPIFFS();

    // Enable watchdog timer after SPIFFS is set up
    enableWatchdog();

    Serial.println("Checking WiFi config mode flag...");
    if (state.wifiConfigMode) {
        Serial.println("WiFi config mode flag is SET");
        enterConfigMode();
    } else {
        Serial.println("WiFi config mode flag is NOT set, checking credentials...");
        setupWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            setupWebSocket();
        }
    }

    Serial.println("Setup complete!");
}

void loop() {
    // Handle key interrupts
    for (int i = 0; i < 4; i++) {
        if (keyInterrupts[i]) {
            keyInterrupts[i] = false;
            if (millis() - state.keyDebounce[i] > 100) {  // Debounce
                state.keyDebounce[i] = millis();
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
        // Try to reconnect WiFi with exponential backoff
        static unsigned long lastWiFiRetry = 0;
        static unsigned long retryDelay = 5000;  // Start with 5 seconds
        static const unsigned long MAX_RETRY_DELAY = 60000;  // Cap at 1 minute
        static int retryCount = 0;

        if (millis() - lastWiFiRetry > retryDelay) {
            retryCount++;
            Serial.printf("WiFi disconnected, retry attempt #%d (delay: %lus)...\n",
                         retryCount, retryDelay / 1000);

            // Update displays to show retry status (using different Y positions)
            N085_Screen_Set(N085_Screen_1);
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(TFT_ORANGE);
            tft.setCursor(15, 10);  // Top of screen
            tft.print("Retry");

            N085_Screen_Set(N085_Screen_2);
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(3);
            tft.setTextColor(TFT_YELLOW);
            tft.setCursor(40, 10);  // Top of screen
            tft.print("#");
            tft.print(retryCount);

            N085_Screen_Set(N085_Screen_3);
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(TFT_CYAN);
            tft.setCursor(25, 10);  // Top of screen
            tft.print(retryDelay / 1000);
            tft.setCursor(30, 35);
            tft.print("sec");

            N085_Screen_Set(N085_Screen_4);
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(TFT_WHITE);
            tft.setCursor(25, 10);  // Top of screen
            tft.print("wait");

            // Attempt reconnection
            WiFi.reconnect();
            delay(5000);  // Give it 5 seconds to connect

            if (WiFi.status() != WL_CONNECTED) {
                // Failed - increase backoff (double the delay, capped at 1 minute)
                retryDelay = min(retryDelay * 2, MAX_RETRY_DELAY);
            } else {
                // Success - reset backoff
                Serial.println("WiFi reconnected!");
                retryDelay = 5000;
                retryCount = 0;
            }

            lastWiFiRetry = millis();
        }
    }

    // Update displays if needed - only when WebSocket is connected
    if (optionsUpdated && state.wsConnected) {
        updateDisplaysIfNeeded();
        optionsUpdated = false;
    }

    // State-specific periodic updates
    switch (fsm.current) {
        case RATE_LIMITED: {
            // Update timer every second
            static unsigned long lastUpdate = 0;
            if (millis() - lastUpdate > 1000) {
                // Countdown if we have a value
                if (fsm.rateLimitCountdown > 0) {
                    fsm.rateLimitCountdown--;
                }
                // Redraw the timer display (only if not overridden)
                if (!fsm.displayOverride[2]) {
                    drawRateLimitTimer();
                }
                lastUpdate = millis();
            }
            break;
        }

        case ERROR: {
            // Update pulsing error icon every 50ms
            static unsigned long lastUpdate = 0;
            if (millis() - lastUpdate > 50) {
                // Redraw the error icon (only if not overridden)
                if (!fsm.displayOverride[1]) {
                    drawErrorIcon();
                }
                lastUpdate = millis();
            }
            break;
        }

        default:
            // No periodic updates for other states
            break;
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
        configServer.handleClient();
    }

    // Handle serial commands
    handleSerialCommands();

    // Feed the watchdog
    feedWatchdog();
    yield();
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
    FastLED.addLeds<WS2812, WS2812_DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(64);  // Start at 25% brightness

    // Initialize displays (includes backlight setup)
    initializeDisplays();

    Serial.println("Hardware initialized");
}

void selectDisplay(uint8_t index) {
    // Use N085_Screen_Set() from T-Keyboard_S3_Drive library
    // Maps logical index (0-3) to physical screen constants
    // Physical layout: leftmost key = 1, rightmost key = 4
    switch (index) {
        case 0: N085_Screen_Set(N085_Screen_1); break;  // Logical 0 → Physical Key 1 (leftmost)
        case 1: N085_Screen_Set(N085_Screen_2); break;  // Logical 1 → Physical Key 2
        case 2: N085_Screen_Set(N085_Screen_3); break;  // Logical 2 → Physical Key 3
        case 3: N085_Screen_Set(N085_Screen_4); break;  // Logical 3 → Physical Key 4 (rightmost)
        default: N085_Screen_Set(N085_Screen_1); break;
    }
}

void initializeDisplays() {
    Serial.println("\n=== DISPLAY INITIALIZATION ===");

    // Setup backlight PWM (ESP32-Arduino v3.x API)
    Serial.println("Setting up backlight...");
    ledcAttach(TFT_BL, 2000, 8);  // Pin, 2kHz, 8-bit
    ledcWrite(TFT_BL, 255);  // Full brightness
    Serial.println("Backlight ON");

    // Initialize the N085 screen driver (sets up CS pins)
    Serial.println("Initializing N085 screen driver...");
    N085_Screen_Set(N085_Initialize);

    // CRITICAL: Select ALL screens before initializing
    // This is how LILYGO does it - init once with all screens active
    Serial.println("Selecting all screens for initialization...");
    N085_Screen_Set(N085_Screen_ALL);

    // Initialize TFT_eSPI library ONCE for all displays
    Serial.println("Initializing TFT_eSPI (all displays at once)...");
    tft.begin();

    // Set rotation for GC9A01 driver (fixes upside-down display)
    Serial.println("Setting rotation...");
    tft.setRotation(2);

    // Clear ALL displays at once
    Serial.println("Clearing all displays...");
    tft.fillScreen(TFT_BLACK);

    // Skip individual display test - causes overlapping text
    // Displays are already cleared and ready to use
    Serial.println("\nDisplays cleared and ready");

    // Keep display 0 selected
    N085_Screen_Set(N085_Screen_1);

    Serial.println("=== INITIALIZATION COMPLETE ===\n");
}

void setupWiFi() {
    String ssid = preferences.getString("wifi_ssid", "");
    Serial.printf("Checking WiFi credentials... SSID: '%s'\n", ssid.c_str());

    if (ssid.length() == 0) {
        Serial.println("No WiFi credentials, entering config mode");
        state.wifiConfigMode = true;
        enterConfigMode();
        return;
    }

    Serial.println("WiFi credentials found, attempting connection");
    WiFi.mode(WIFI_STA);
    WiFi.begin(
        ssid.c_str(),
        preferences.getString("wifi_pass", "").c_str()
    );

    // Show connecting status on displays using selectDisplay()
    selectDisplay(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(15, 50);
    tft.print("WiFi");

    selectDisplay(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(5, 50);
    tft.print("Connect");

    selectDisplay(2);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(25, 50);
    tft.print("ing");

    selectDisplay(3);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(30, 50);
    tft.print("...");

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    uint8_t brightness = 0;
    int8_t direction = 1;

    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;

        // Pulse blue LED while connecting
        brightness += direction * 5;
        if (brightness >= 255 || brightness <= 0) direction = -direction;
        fill_solid(leds, NUM_LEDS, CRGB(0, 0, brightness));
        FastLED.show();

        // Feed watchdog to prevent resets during long WiFi connection
        yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        // Start mDNS
        if (MDNS.begin("tkeyboard")) {
            Serial.println("mDNS responder started: tkeyboard.local");
        } else {
            Serial.println("Error setting up mDNS responder!");
        }

        // Green LEDs for success
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();

        // Show waiting status on displays - use FSM CONNECTING state
        String host = preferences.getString("bridge_host", "tkeyboard-bridge.local");
        int port = preferences.getInt("bridge_port", 8080);

        // Clear all displays first
        for (int i = 0; i < 4; i++) {
            selectDisplay(i);
            tft.fillScreen(TFT_BLACK);
        }

        // FSM will handle display rendering via CONNECTING state
        renderStateUI();

        Serial.printf("Server: %s:%d\n", host.c_str(), port);
    } else {
        Serial.println("\nInitial WiFi connection failed, will retry with exponential backoff");
        // Don't enter config mode automatically - loop() will handle retries
    }
}

void setupWebSocket() {
    String host = preferences.getString("bridge_host", "tkeyboard-bridge.local");
    int port = preferences.getInt("bridge_port", 8080);

    if (host.length() == 0) {
        Serial.println("No bridge server configured");
        return;
    }

    Serial.printf("Connecting to WebSocket: %s:%d\n", host.c_str(), port);

    // Try to resolve hostname first if it's a .local address
    if (host.endsWith(".local")) {
        Serial.printf("Resolving mDNS hostname: %s\n", host.c_str());
        IPAddress resolvedIP = MDNS.queryHost(host.substring(0, host.length() - 6));  // Remove .local

        if (resolvedIP == IPAddress(0, 0, 0, 0)) {
            Serial.println("ERROR: mDNS resolution failed!");
            Serial.println("  - Check that bridge server is running");
            Serial.println("  - Verify both devices are on same network");
            Serial.println("  - Try configuring an IP address instead");
            return;
        }

        Serial.printf("Resolved %s to %s\n", host.c_str(), resolvedIP.toString().c_str());
    }

    webSocket.begin(host, port, "/ws");
    webSocket.onEvent(handleWebSocketEvent);
    webSocket.setReconnectInterval(5000);
    webSocket.enableHeartbeat(15000, 3000, 2);
}

void handleWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED: {
            Serial.println("WebSocket disconnected");
            state.wsConnected = false;
            // Red LEDs for disconnect
            fill_solid(leds, NUM_LEDS, CRGB::Red);
            FastLED.show();
            break;
        }

        case WStype_CONNECTED: {
            Serial.println("WebSocket connected");
            state.wsConnected = true;

            // Transition from CONNECTING to IDLE
            transitionToState(IDLE);

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
        }

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

        case WStype_PING: {
            Serial.println("Ping received");
            break;
        }

        case WStype_PONG: {
            Serial.println("Pong received");
            break;
        }

        case WStype_ERROR: {
            Serial.printf("WebSocket ERROR! Error code: %u\n", type);
            if (length > 0 && payload != nullptr) {
                Serial.printf("Error payload: %s\n", payload);
            }
            break;
        }

        default: {
            Serial.printf("Unhandled WebSocket event type: %u\n", type);
            break;
        }
    }
}

void processClaudeMessage(JsonDocument& doc) {
    String type = doc["type"];
    Serial.printf("[DEBUG] Processing message type: %s\n", type.c_str());

    if (type == "update_options") {
        Serial.println("[DEBUG] Handler: update_options");
        state.sessionId = doc["session_id"].as<String>();
        JsonArray options = doc["options"];

        for (int i = 0; i < 4 && i < options.size(); i++) {
            currentOptions[i].text = options[i]["text"].as<String>();
            currentOptions[i].action = options[i]["action"].as<String>();
            currentOptions[i].imagePath = options[i]["image"].as<String>();
            currentOptions[i].color = strtoul(options[i]["color"].as<String>().substring(1).c_str(), NULL, 16);
            currentOptions[i].hasImage = currentOptions[i].imagePath.length() > 0;

            // If no action specified, use display text as action
            if (currentOptions[i].action.length() == 0) {
                currentOptions[i].action = currentOptions[i].text;
            }

            Serial.printf("[DEBUG]   Key %d: text='%s', action='%s', image='%s', hasImage=%d\n",
                i+1, currentOptions[i].text.c_str(), currentOptions[i].action.c_str(),
                currentOptions[i].imagePath.c_str(), currentOptions[i].hasImage);
        }

        optionsUpdated = true;

    } else if (type == "status") {
        String stateStr = doc["state"];
        Serial.printf("[DEBUG] Handler: status, state=%s\n", stateStr.c_str());

        // Translate status string to FSM state
        ClaudeState newState;
        if (stateStr == "thinking") {
            newState = THINKING;
        } else if (stateStr == "idle") {
            newState = IDLE;
        } else if (stateStr == "error") {
            newState = ERROR;
        } else if (stateStr == "limit") {
            newState = RATE_LIMITED;
            // Get countdown if provided (0 if not)
            fsm.rateLimitCountdown = doc["countdown"] | 0;
        } else {
            Serial.printf("[DEBUG] WARNING: Unknown state: %s\n", stateStr.c_str());
            return;
        }

        // Transition to new state using FSM
        transitionToState(newState);

    } else if (type == "display_update") {
        Serial.println("[DEBUG] Handler: display_update");
        int displayNum = doc["display"] | -1;
        String title = doc["title"] | "";
        String content = doc["content"] | "";

        Serial.printf("[DEBUG]   Display: %d, Title: %s, Content: %s\n",
                      displayNum, title.c_str(), content.c_str());

        if (displayNum >= 0 && displayNum < 4) {
            // Mark this display as manually overridden
            fsm.displayOverride[displayNum] = true;

            // Update the specified display with title and content
            selectDisplay(displayNum);
            tft.fillScreen(TFT_BLACK);

            // Draw title at top
            tft.setTextSize(2);
            tft.setTextColor(TFT_YELLOW);
            tft.setCursor(5, 10);
            tft.print(title);

            // Draw content in center
            tft.setTextSize(2);
            tft.setTextColor(TFT_WHITE);
            tft.setCursor(5, 50);
            tft.print(content);

            Serial.printf("[DEBUG]   Display %d updated successfully (override set)\n", displayNum);
        } else {
            Serial.printf("[DEBUG]   ERROR: Invalid display number: %d\n", displayNum);
        }

    } else if (type == "image") {
        Serial.println("[DEBUG] Handler: image");
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
    } else {
        Serial.printf("[DEBUG] WARNING: Unknown message type: %s\n", type.c_str());
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

    // Handle state-specific key presses
    if (fsm.current == RATE_LIMITED) {
        if (key == 4) {  // Key 4 is Continue during rate limit
            sendKeyPress(key);
            // Try to resume normal state
            transitionToState(IDLE);
            return;
        }
    }

    if (fsm.current == ERROR) {
        if (key == 3) {  // Key 3 is Continue during error
            sendKeyPress(key);
            transitionToState(IDLE);
            return;
        } else if (key == 4) {  // Key 4 is Retry during error
            sendKeyPress(key);
            return;
        }
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
    doc["text"] = currentOptions[key - 1].action;  // Send action text, not display text

    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);

    Serial.printf("Sent key %d: action='%s' (display='%s')\n",
        key, currentOptions[key - 1].action.c_str(), currentOptions[key - 1].text.c_str());
}

void updateDisplays() {
    // DEPRECATED: This function is replaced by FSM architecture
    // Redirecting to FSM version for compatibility
    updateDisplaysIfNeeded();
}

bool loadImageFromSPIFFS(const String& path, uint8_t displayIndex) {
    Serial.printf("[IMG] loadImageFromSPIFFS called: path='%s', display=%d\n", path.c_str(), displayIndex);
    String fullPath = IMAGE_CACHE_PATH + path;

    // If image doesn't exist in SPIFFS, try to download it
    if (!SPIFFS.exists(fullPath)) {
        Serial.printf("[IMG] Image not cached: %s - attempting download\n", path.c_str());

        // Get bridge server settings from Preferences
        String bridgeHost = preferences.getString("bridge_host", "");
        int bridgePort = preferences.getInt("bridge_port", 8080);

        Serial.printf("[IMG] Bridge config: host='%s', port=%d\n", bridgeHost.c_str(), bridgePort);

        if (bridgeHost.isEmpty()) {
            Serial.println("[IMG] Bridge host not configured, cannot download image");
            return false;
        }

        // Try to download the image
        if (!downloadImageHTTP(path, bridgeHost, bridgePort)) {
            Serial.printf("[IMG] Failed to download image: %s\n", path.c_str());
            return false;
        }

        // Image should now be in SPIFFS, continue with loading
        Serial.printf("[IMG] Image downloaded successfully: %s\n", path.c_str());
    } else {
        Serial.printf("[IMG] Image found in cache: %s\n", fullPath.c_str());
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
    selectDisplay(displayIndex);
    tft.pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)buffer);

    free(buffer);
    return true;
}

// Delete a random .rgb image file from SPIFFS to free up space
void deleteRandomImage() {
    File root = SPIFFS.open(IMAGE_CACHE_PATH);
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open image cache directory");
        return;
    }

    // Count and collect image files
    std::vector<String> imageFiles;
    File file = root.openNextFile();
    while (file) {
        String filename = String(file.name());
        if (filename.endsWith(".rgb")) {
            imageFiles.push_back(filename);
        }
        file = root.openNextFile();
    }

    if (imageFiles.empty()) {
        Serial.println("No images to delete");
        return;
    }

    // Pick a random file
    int randomIndex = random(0, imageFiles.size());
    String fileToDelete = imageFiles[randomIndex];

    if (SPIFFS.remove(fileToDelete)) {
        Serial.printf("Deleted random image: %s\n", fileToDelete.c_str());
    } else {
        Serial.printf("Failed to delete: %s\n", fileToDelete.c_str());
    }
}

// Ensure SPIFFS has enough space, deleting random images if needed
void ensureSPIFFSSpace(size_t requiredBytes) {
    const size_t SAFETY_MARGIN = 4096;  // 4KB safety margin

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    Serial.printf("SPIFFS: %u/%u bytes used, %u free\n", usedBytes, totalBytes, freeBytes);

    // Keep deleting until we have enough space
    while (freeBytes < (requiredBytes + SAFETY_MARGIN)) {
        Serial.printf("Insufficient space: need %u, have %u\n", requiredBytes + SAFETY_MARGIN, freeBytes);
        deleteRandomImage();

        // Recalculate free space
        usedBytes = SPIFFS.usedBytes();
        freeBytes = totalBytes - usedBytes;

        // Safety check to prevent infinite loop
        if (freeBytes == (totalBytes - usedBytes)) {
            Serial.println("No more images to delete!");
            break;
        }
    }

    Serial.printf("SPIFFS space ensured: %u bytes free\n", freeBytes);
}

// Download an image from bridge server HTTP API and cache in SPIFFS
bool downloadImageHTTP(const String& imagePath, const String& serverHost, uint16_t serverPort) {
    HTTPClient http;

    // Note: Bridge server HTTP API is on port 8081, WebSocket is on 8080
    // Construct URL: http://{serverHost}:8081/images/{imagePath}
    String url = "http://" + serverHost + ":8081/images/" + imagePath;

    Serial.printf("Downloading image: %s\n", url.c_str());

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP GET failed: %d\n", httpCode);
        http.end();
        return false;
    }

    // Get image size
    int contentLength = http.getSize();
    const size_t expectedSize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;  // 128x128x2 = 32768

    if (contentLength != expectedSize) {
        Serial.printf("Invalid image size: %d (expected %d)\n", contentLength, expectedSize);
        http.end();
        return false;
    }

    // Ensure SPIFFS has space
    ensureSPIFFSSpace(expectedSize);

    // Open file for writing
    String fullPath = IMAGE_CACHE_PATH + imagePath;
    File file = SPIFFS.open(fullPath, FILE_WRITE);

    if (!file) {
        Serial.printf("Failed to create file: %s\n", fullPath.c_str());
        http.end();
        return false;
    }

    // Download and write to SPIFFS
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[128];
    size_t bytesWritten = 0;
    bool writeError = false;

    while (http.connected() && bytesWritten < expectedSize && !writeError) {
        size_t availableBytes = stream->available();
        if (availableBytes) {
            size_t bytesToRead = min(availableBytes, sizeof(buffer));
            size_t bytesRead = stream->readBytes(buffer, bytesToRead);

            size_t written = file.write(buffer, bytesRead);
            if (written != bytesRead) {
                Serial.printf("[SPIFFS] ERROR: Write failed! Expected %u bytes, wrote %u bytes\n", bytesRead, written);
                writeError = true;
                break;
            }

            bytesWritten += bytesRead;
        }
        feedWatchdog();  // Feed watchdog during long downloads
        delay(1);
    }

    file.close();
    http.end();

    if (writeError) {
        Serial.printf("[SPIFFS] ERROR: Write error during download of %s\n", imagePath.c_str());
        SPIFFS.remove(fullPath);  // Clean up failed download
        return false;
    } else if (bytesWritten == expectedSize) {
        Serial.printf("[SPIFFS] Downloaded and cached: %s (%u bytes)\n", imagePath.c_str(), bytesWritten);
        return true;
    } else {
        Serial.printf("[SPIFFS] ERROR: Download incomplete: %u/%u bytes\n", bytesWritten, expectedSize);
        SPIFFS.remove(fullPath);  // Clean up incomplete download
        return false;
    }
}

void drawTextOption(uint8_t displayIndex, const String& text, uint32_t color) {
    selectDisplay(displayIndex);
    tft.fillScreen(TFT_BLACK);

    // Set text color from RGB value
    uint16_t color565 = ((color & 0xF80000) >> 8) |
                        ((color & 0x00FC00) >> 5) |
                        ((color & 0x0000F8) >> 3);
    tft.setTextColor(color565);

    // Center text
    int16_t x = (SCREEN_WIDTH - text.length() * 12) / 2;
    int16_t y = (SCREEN_HEIGHT - 16) / 2;

    tft.setCursor(x, y);
    tft.setTextSize(2);
    tft.print(text);
}

void drawLargeText(uint8_t displayIndex, const String& text, uint32_t color) {
    selectDisplay(displayIndex);
    tft.fillScreen(TFT_BLACK);

    // Convert RGB888 to RGB565
    uint16_t color565 = ((color & 0xF80000) >> 8) |
                        ((color & 0x00FC00) >> 5) |
                        ((color & 0x0000F8) >> 3);
    tft.setTextColor(color565);

    // Use large text size (4x) and center
    tft.setTextSize(4);

    // Calculate center position (4x size = 24 pixels wide per char, 32 pixels high)
    int16_t charWidth = text.length() * 24;
    int16_t x = (SCREEN_WIDTH - charWidth) / 2;
    int16_t y = (SCREEN_HEIGHT - 32) / 2;

    tft.setCursor(x, y);
    tft.print(text);
}

void drawRateLimitTimer() {
    N085_Screen_Set(N085_Screen_3);
    tft.fillScreen(TFT_BLACK);

    int mins, secs;
    String timeStr;

    if (fsm.rateLimitCountdown > 0) {
        // Countdown mode - we know when it ends
        mins = fsm.rateLimitCountdown / 60;
        secs = fsm.rateLimitCountdown % 60;

        tft.setTextColor(TFT_YELLOW);
    } else {
        // Elapsed time mode - duration unknown
        unsigned long elapsed = (millis() - fsm.rateLimitStartTime) / 1000;
        mins = elapsed / 60;
        secs = elapsed % 60;

        tft.setTextColor(TFT_ORANGE);
    }

    // Format time as MM:SS
    timeStr = String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);

    // Display in large text (size 4)
    tft.setTextSize(4);

    // Center the time
    int16_t charWidth = timeStr.length() * 24;
    int16_t x = (SCREEN_WIDTH - charWidth) / 2;
    int16_t y = (SCREEN_HEIGHT - 32) / 2;

    tft.setCursor(x, y);
    tft.print(timeStr);

    // Add small label below
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    if (fsm.rateLimitCountdown > 0) {
        tft.setCursor(30, 100);
        tft.print("remaining");
    } else {
        // Animated dots for elapsed time
        tft.setCursor(35, 100);
        tft.print("elapsed");

        static int dotCount = 0;
        static unsigned long lastDotUpdate = 0;
        if (millis() - lastDotUpdate > 500) {
            dotCount = (dotCount + 1) % 4;
            lastDotUpdate = millis();
        }

        tft.setCursor(75, 100);
        for (int i = 0; i < dotCount; i++) {
            tft.print(".");
        }
    }
}

void drawErrorIcon() {
    N085_Screen_Set(N085_Screen_2);
    tft.fillScreen(TFT_BLACK);

    // Pulsing red exclamation mark
    static uint8_t brightness = 128;
    static int8_t direction = 4;

    brightness += direction;
    if (brightness >= 255 || brightness <= 128) {
        direction = -direction;
    }

    // Create pulsing red color
    uint16_t pulseColor = tft.color565(brightness, 0, 0);
    tft.setTextColor(pulseColor);

    // Draw large exclamation mark centered
    tft.setTextSize(8);
    tft.setCursor(45, 30);  // Manually center the "!" character
    tft.print("!");

    // Add small "Error" text at bottom
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(45, 110);
    tft.print("Error");
}


void setLEDStatus() {
    static uint8_t pulseValue = 0;
    static int8_t pulseDirection = 1;

    switch(fsm.current) {
        case CONNECTING:
            // Pulsing cyan while waiting for WebSocket
            pulseValue += pulseDirection * 2;
            if (pulseValue >= 64 || pulseValue <= 0) pulseDirection = -pulseDirection;
            fill_solid(leds, NUM_LEDS, CRGB(0, pulseValue, pulseValue));
            break;

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

// ============================================================================
// WATCHDOG MANAGEMENT
// ============================================================================

void disableWatchdog() {
    Serial.println("[WDT] Disabling watchdog");
    esp_task_wdt_deinit();
}

void enableWatchdog() {
    Serial.println("[WDT] Enabling watchdog");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_MS,
        .idle_core_mask = (1 << 0) | (1 << 1),  // Monitor both cores
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);  // Add current task to watchdog
}

void feedWatchdog() {
    esp_task_wdt_reset();
}

// ============================================================================
// SPIFFS MANAGEMENT
// ============================================================================

void setupSPIFFS() {
    Serial.println("[SPIFFS] Initializing...");

    // Try to mount SPIFFS, format if mount fails
    if (!SPIFFS.begin(false)) {
        Serial.println("[SPIFFS] Mount failed, formatting...");

        // Disable watchdog during format (can take 10+ seconds)
        disableWatchdog();

        if (SPIFFS.begin(true)) {  // true = format if mount fails
            Serial.println("[SPIFFS] Formatted and mounted successfully");
            enableWatchdog();
        } else {
            Serial.println("[SPIFFS] ERROR: Format failed - image caching disabled");
            enableWatchdog();
            return;
        }
    } else {
        Serial.println("[SPIFFS] Mounted successfully");
    }

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    Serial.printf("[SPIFFS] Total: %u bytes, Used: %u bytes, Free: %u bytes (%.1f%% free)\n",
                  totalBytes, usedBytes, freeBytes,
                  (freeBytes * 100.0f) / totalBytes);

    // Create image cache directory if it doesn't exist
    if (!SPIFFS.exists(IMAGE_CACHE_PATH)) {
        // SPIFFS doesn't support directories, but we use path prefixes
        Serial.println("[SPIFFS] Ready for image caching");
    }
}

void loadPreferences() {
    preferences.begin("tkeyboard", false);

    state.backlightIntensity = preferences.getUChar("backlight", 128);
    state.bridgeHost = preferences.getString("bridge_host", "tkeyboard-bridge.local");
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
    Serial.println("Setting WiFi mode to AP...");
    WiFi.mode(WIFI_AP);

    Serial.printf("Starting AP: SSID='%s' Pass='%s'\n", AP_SSID, AP_PASSWORD);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    // Setup DNS server to redirect all requests
    Serial.println("Starting DNS server...");
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Setup web server
    Serial.println("Starting config web server...");
    setupConfigServer();

    // Display config mode on screens
    Serial.println("Updating displays for config mode...");

    // Key 1: Config mode indicator
    N085_Screen_Set(N085_Screen_1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(10, 30);
    tft.print("CONFIG");
    tft.setCursor(10, 55);
    tft.print("MODE");
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(10, 90);
    tft.print("AP Active");

    // Key 2: SSID
    N085_Screen_Set(N085_Screen_2);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 30);
    tft.print("WiFi SSID:");
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(5, 55);
    tft.print(AP_SSID);

    // Key 3: Password
    N085_Screen_Set(N085_Screen_3);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 30);
    tft.print("Password:");
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(15, 55);
    tft.print(AP_PASSWORD);

    // Key 4: URL to visit
    N085_Screen_Set(N085_Screen_4);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 30);
    tft.print("Then visit:");
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(5, 55);
    tft.print("192.168");
    tft.setCursor(15, 75);
    tft.print(".4.1");

    state.wifiConfigMode = true;
    Serial.println("Config mode setup complete!");
}

void setupConfigServer() {
    Serial.println("Setting up config server routes...");

    // Serve configuration page
    configServer.on("/", []() {
        const char* html = R"html(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>T-Keyboard Configuration</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 400px; margin: 0 auto; }
        h1 { color: #4CAF50; text-align: center; }
        input { width: 100%; padding: 10px; margin: 10px 0; box-sizing: border-box;
                background: #2a2a2a; border: 1px solid #4CAF50; color: #fff; }
        button { background: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0;
                 border: none; cursor: pointer; width: 100%; }
        .section { background: #2a2a2a; padding: 20px; margin: 20px 0; border-radius: 5px; }
        label { display: block; margin-top: 10px; color: #aaa; }
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
                <input type='text' name='host' value='tkeyboard-bridge.local' required>
                <label>Bridge Server Port:</label>
                <input type='number' name='port' value='8080' required>
                <button type='submit'>Save & Restart</button>
            </form>
        </div>
    </div>
</body>
</html>
)html";
        configServer.send(200, "text/html", html);
    });

    // Handle form submission
    configServer.on("/save", []() {
        if (configServer.hasArg("ssid") && configServer.hasArg("pass")) {
            String ssid = configServer.arg("ssid");
            String pass = configServer.arg("pass");
            String host = configServer.arg("host");
            int port = configServer.arg("port").toInt();

            // Save to preferences
            preferences.putString("wifi_ssid", ssid);
            preferences.putString("wifi_pass", pass);
            preferences.putString("bridge_host", host);
            preferences.putInt("bridge_port", port);

            configServer.send(200, "text/html", "<html><body><h1>Settings Saved!</h1><p>Restarting...</p></body></html>");

            delay(2000);
            ESP.restart();
        } else {
            configServer.send(400, "text/html", "Missing parameters");
        }
    });

    Serial.println("Starting HTTP server...");
    configServer.begin();
    Serial.println("Config server started successfully!");
}

void handleSerialCommands() {
    if (!Serial.available()) return;

    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.length() == 0) return;

    Serial.printf("Received command: %s\n", command.c_str());

    // WIFI:SSID:PASSWORD
    if (command.startsWith("WIFI:")) {
        int firstColon = command.indexOf(':', 5);
        if (firstColon > 5) {
            String ssid = command.substring(5, firstColon);
            String pass = command.substring(firstColon + 1);

            preferences.putString("wifi_ssid", ssid);
            preferences.putString("wifi_pass", pass);

            Serial.printf("WiFi configured: SSID='%s'\n", ssid.c_str());
            Serial.println("Restart device to apply changes");
        } else {
            Serial.println("Error: WIFI command format: WIFI:SSID:PASSWORD");
        }
    }
    // HOST:hostname
    else if (command.startsWith("HOST:")) {
        String host = command.substring(5);
        preferences.putString("bridge_host", host);
        state.bridgeHost = host;

        Serial.printf("Bridge host set to: %s\n", host.c_str());
        Serial.println("Restart device to apply changes");
    }
    // PORT:8080
    else if (command.startsWith("PORT:")) {
        int port = command.substring(5).toInt();
        if (port > 0 && port < 65536) {
            preferences.putInt("bridge_port", port);
            state.bridgePort = port;

            Serial.printf("Bridge port set to: %d\n", port);
            Serial.println("Restart device to apply changes");
        } else {
            Serial.println("Error: Invalid port number");
        }
    }
    // CONFIG
    else if (command == "CONFIG") {
        Serial.println("Entering AP config mode...");
        state.wifiConfigMode = true;
        ESP.restart();
    }
    // STATUS
    else if (command == "STATUS") {
        Serial.println("\n=== T-Keyboard Status ===");
        Serial.printf("WiFi SSID: %s\n", preferences.getString("wifi_ssid", "(not set)").c_str());
        Serial.printf("WiFi Status: %s\n", WiFi.isConnected() ? "Connected" : "Disconnected");
        if (WiFi.isConnected()) {
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        }
        Serial.printf("Bridge Host: %s\n", preferences.getString("bridge_host", "(not set)").c_str());
        Serial.printf("Bridge Port: %d\n", preferences.getInt("bridge_port", 8080));
        Serial.printf("WebSocket: %s\n", state.wsConnected ? "Connected" : "Disconnected");
        Serial.printf("Config Mode: %s\n", state.wifiConfigMode ? "Yes" : "No");
        Serial.println("========================\n");
    }
    // RESTART
    else if (command == "RESTART") {
        Serial.println("Restarting...");
        delay(1000);
        ESP.restart();
    }
    else {
        Serial.println("Unknown command. Available commands:");
        Serial.println("  WIFI:SSID:PASSWORD - Set WiFi credentials");
        Serial.println("  HOST:hostname - Set bridge server host");
        Serial.println("  PORT:8080 - Set bridge server port");
        Serial.println("  CONFIG - Enter AP config mode");
        Serial.println("  STATUS - Show current settings");
        Serial.println("  RESTART - Restart device");
    }
}