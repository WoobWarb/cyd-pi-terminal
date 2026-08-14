#include <WiFi.h>
#include <WebSocketsClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// =====================================================
// WIFI & HOST CONFIGURATION
// =====================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* PI_IP         = "192.168.1.100";  // Replace with Raspberry Pi / Host IP
const uint16_t PI_PORT    = 8080;
const char* WS_PATH       = "/screen";

// =====================================================
// CYD HARDWARE PINOUT (ESP32-2432S028)
// =====================================================
#define SCREEN_WIDTH      320
#define SCREEN_HEIGHT     240
#define CYD_BACKLIGHT_PIN 21

// Touch Screen XPT2046 Dedicated SPI Pins
#define XPT2046_MOSI      32
#define XPT2046_MISO      39
#define XPT2046_CLK       25
#define XPT2046_CS        33

// =====================================================
// OBJECTS & CONSTANTS
// =====================================================
TFT_eSPI tft = TFT_eSPI();
WebSocketsClient webSocket;

SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS);

// Touch state
bool lastTouchState = false;
unsigned long lastTouchTime = 0;
uint16_t lastSentX = 0;
uint16_t lastSentY = 0;

// =====================================================
// JPEG DECODER CALLBACK
// =====================================================
bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return false;
    tft.pushImage(x, y, w, h, bitmap);
    return true;
}

// =====================================================
// WEBSOCKET EVENT HANDLER
// =====================================================
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[WS] Connected to Server!");
            tft.fillScreen(TFT_BLACK);
            webSocket.sendTXT("REFRESH");
            break;
            
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected from Server");
            break;

        case WStype_BIN:
            // Delta Update Protocol: 4 Bytes Header (uint16 X, uint16 Y) + JPEG Stream
            if (payload != nullptr && length > 4) {
                uint16_t crop_x = (payload[0] << 8) | payload[1];
                uint16_t crop_y = (payload[2] << 8) | payload[3];
                
                TJpgDec.drawJpg(crop_x, crop_y, payload + 4, length - 4);
            }
            break;
            
        default:
            break;
    }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
    Serial.begin(115200);
    
    // Enable CYD LCD Backlight (Pin 21 on ESP32-2432S028)
    pinMode(CYD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(CYD_BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // Initialize Touch Controller with dedicated SPI bus
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSpi);
    ts.setRotation(1);

    // Configure TJpg_Decoder
    TJpgDec.setCallback(jpegOutput);
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);

    // Connect to WiFi
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting WiFi...", 10, 10, 2);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi Connected!", 10, 10, 2);
    tft.drawString(WiFi.localIP().toString().c_str(), 10, 30, 2);

    // Initialize WebSocket Client
    webSocket.begin(PI_IP, PI_PORT, WS_PATH);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(3000);
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
    webSocket.loop();

    // Touch Screen Polling (Rate limited to 25ms)
    if (millis() - lastTouchTime > 25) {
        lastTouchTime = millis();
        
        if (ts.touched()) {
            TS_Point p = ts.getPoint();
            
            uint16_t tx = constrain(map(p.x, 200, 3700, 0, SCREEN_WIDTH), 0, SCREEN_WIDTH);
            uint16_t ty = constrain(map(p.y, 240, 3800, 0, SCREEN_HEIGHT), 0, SCREEN_HEIGHT);
            
            lastSentX = tx;
            lastSentY = ty;
            
            char msg[32];
            sprintf(msg, "M:%d,%d,1", tx, ty);
            webSocket.sendTXT(msg);
            lastTouchState = true;
        } else if (lastTouchState) {
            char msg[32];
            sprintf(msg, "M:%d,%d,0", lastSentX, lastSentY);
            webSocket.sendTXT(msg);
            lastTouchState = false;
        }
    }
}
