#include <WiFi.h>
#include <WebSocketsClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// =====================================================
// WIFI & PI CONFIG
// =====================================================
const char* WIFI_SSID     = "DeathWolf_AP";
const char* WIFI_PASSWORD = "00001111";

const char* PI_IP         = "192.168.100.99";
const uint16_t PI_PORT    = 8080;
const char* WS_PATH       = "/screen";

// =====================================================
// CYD HARDWARE PINOUT (ESP32-2432S028)
// =====================================================
#define SCREEN_WIDTH      320
#define SCREEN_HEIGHT     240
#define CYD_BACKLIGHT_PIN 21

// Touch Screen XPT2046 Dedicated SPI Pins
#define XPT2046_IRQ       36
#define XPT2046_MOSI      32
#define XPT2046_MISO      39
#define XPT2046_CLK       25
#define XPT2046_CS        33

// Touch Calibration Min/Max (CYD Landscape)
#define TS_MINX           200
#define TS_MAXX           3800
#define TS_MINY           200
#define TS_MAXY           3800

// =====================================================
// OBJECTS
// =====================================================
TFT_eSPI tft = TFT_eSPI();
WebSocketsClient webSocket;

SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Touch state
bool lastTouchState = false;
unsigned long lastTouchTime = 0;
uint16_t lastSentX = 0;
uint16_t lastSentY = 0;

// =====================================================
// JPEG CALLBACK
// =====================================================
bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return false;
    tft.pushImage(x, y, w, h, bitmap);
    return true;
}

// =====================================================
// WEBSOCKET EVENT
// =====================================================
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[WS] Connected to Raspberry Pi!");
            tft.fillScreen(TFT_BLACK);
            webSocket.sendTXT("REFRESH");
            break;
            
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected from Raspberry Pi");
            break;

        case WStype_BIN:
            // Delta Update Protocol: 4 Bytes Header (X, Y) + JPEG Data
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
    
    // เปิดไฟ Backlight หน้าจอ CYD (Pin 21)
    pinMode(CYD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(CYD_BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // เริ่มต้นระบบ Touch ผ่าน XPT2046 Dedicated SPI Bus
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSpi);
    ts.setRotation(1);

    // ตั้งค่าตัวถอดรหัส JPEG
    TJpgDec.setCallback(jpegOutput);
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);

    // เชื่อมต่อ WiFi
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting WiFi...", 10, 10, 2);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi Connected", 10, 10, 2);

    // เชื่อมต่อ WebSocket
    webSocket.begin(PI_IP, PI_PORT, WS_PATH);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(3000);
}

// =====================================================
// LOOP
// =====================================================
void loop() {
    webSocket.loop();

    // จัดการระบบ Touch Screen แบบ Hardware XPT2046 (เช็คทุก 30ms)
    if (millis() - lastTouchTime > 30) {
        lastTouchTime = millis();
        
        bool pressed = ts.touched();
        if (pressed) {
            TS_Point p = ts.getPoint();
            
            // แปลงค่า Raw พิกัดจาก XPT2046 ให้อยู่ในช่วง 0-320 และ 0-240
            uint16_t tx = constrain(map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH), 0, SCREEN_WIDTH);
            uint16_t ty = constrain(map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT), 0, SCREEN_HEIGHT);
            
            lastSentX = tx;
            lastSentY = ty;
            
            char msg[32];
            sprintf(msg, "M:%d,%d,1", tx, ty);
            webSocket.sendTXT(msg);
            lastTouchState = true;
        } else if (lastTouchState) {
            // ปล่อยนิ้ว (Release)
            char msg[32];
            sprintf(msg, "M:%d,%d,0", lastSentX, lastSentY);
            webSocket.sendTXT(msg);
            lastTouchState = false;
        }
    }
}