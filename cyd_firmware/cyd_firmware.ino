#include <WiFi.h>
#include <WebSocketsClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

// =====================================================
// WIFI & HOST CONFIGURATION
// =====================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* PI_IP         = "192.168.1.100";  // Replace with Raspberry Pi / Host IP
const uint16_t PI_PORT    = 8080;
const char* WS_PATH       = "/screen";

// =====================================================
// OBJECTS & CONSTANTS
// =====================================================
TFT_eSPI tft = TFT_eSPI();
WebSocketsClient webSocket;

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define CYD_BACKLIGHT_PIN 21

// Touch state
bool lastTouchState = false;
unsigned long lastTouchTime = 0;

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
            break;
            
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected from Server");
            break;

        case WStype_BIN:
            // Delta Update Protocol: 4 Bytes Header (uint16 X, uint16 Y) + JPEG Stream
            if (payload != nullptr && length > 4) {
                uint16_t crop_x = (payload[0] << 8) | payload[1];
                uint16_t crop_y = (payload[2] << 8) | payload[3];
                
                // Draw decoded JPEG directly to TFT at bounding box offset
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

    // Touch Screen Polling (Rate limited to 50ms)
    if (millis() - lastTouchTime > 50) {
        lastTouchTime = millis();
        
        uint16_t tx = 0, ty = 0;
        bool pressed = tft.getTouch(&tx, &ty);
        
        // Report touch coordinate & button state: "M:x,y,state"
        if (pressed != lastTouchState || pressed) {
            char msg[32];
            sprintf(msg, "M:%d,%d,%d", tx, ty, pressed ? 1 : 0);
            webSocket.sendTXT(msg);
            lastTouchState = pressed;
        }
    }
}
