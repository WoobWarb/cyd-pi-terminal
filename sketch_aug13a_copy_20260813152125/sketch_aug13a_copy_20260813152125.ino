#include <WiFi.h>
#include <WebSocketsClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
// =====================================================
// WIFI & PI CONFIG
// =====================================================
const char* WIFI_SSID = "DeathWolf_AP";
const char* WIFI_PASSWORD = "00001111";
const char* PI_IP = "192.168.100.99";
const uint16_t PI_PORT = 8080;
const char* WS_PATH = "/screen";
// =====================================================
// OBJECTS
// =====================================================
TFT_eSPI tft = TFT_eSPI();
WebSocketsClient webSocket;
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
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