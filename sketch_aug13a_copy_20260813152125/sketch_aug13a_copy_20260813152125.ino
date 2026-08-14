#include <WiFi.h>
#include <WebSocketsClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

// =====================================================
// WIFI & PI CONFIG
// =====================================================
const char* WIFI_SSID     = "DeathWolf_AP";
const char* WIFI_PASSWORD = "00001111";

const char* PI_IP         = "192.168.100.99";
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
            Serial.println("Connected to Raspberry Pi!");
            tft.fillScreen(TFT_BLACK);
            webSocket.sendTXT("REFRESH");
            break;
            
        case WStype_DISCONNECTED:
            Serial.println("Disconnected from Raspberry Pi");
            break;

        case WStype_BIN:
            // Delta Update Protocol: 4 Bytes Header (X, Y) + JPEG Data
            if (payload != nullptr && length > 4) {
                uint16_t crop_x = (payload[0] << 8) | payload[1];
                uint16_t crop_y = (payload[2] << 8) | payload[3];
                
                // ส่ง JPEG ไบต์ที่เหลือเข้า Decoder
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
    
    // ตั้งค่า Touch Calibration สำหรับจอ CYD ในแนวนอน (Rotation 1)
    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    tft.setTouch(calData);

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

    // จัดการระบบ Touch Screen (เช็คทุก 40ms)
    if (millis() - lastTouchTime > 40) {
        lastTouchTime = millis();
        
        uint16_t tx = 0, ty = 0;
        bool pressed = tft.getTouch(&tx, &ty, 400);
        
        // ส่งข้อความไปหา Pi เมื่อมีการกด หรือตอนปล่อยนิ้ว
        if (pressed != lastTouchState || pressed) {
            char msg[32];
            sprintf(msg, "M:%d,%d,%d", tx, ty, pressed ? 1 : 0);
            webSocket.sendTXT(msg);
            
            lastTouchState = pressed;
        }
    }
}