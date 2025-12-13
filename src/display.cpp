#include "display.h"
// Stubs for Display to save memory
// #include <Adafruit_SSD1306.h> // Removed
// Global variables expected by other files
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Removed
bool wifiOn = false;
bool iconToggle = false;
bool displayInitialized = false;
void setupDisplay() {
    // Stub
}
void oledclearline() {
    // Stub
}
void oledcleardata() {
    // Stub
}
int oled_center_h(const String &text) {
    return 0;
}
int oled_center_v(const String &text) {
    return 0;
}
void oledShowProgressBar(const uint8_t step, const uint8_t numSteps, const char* largeText, const char* statusMessage) {
    // Stub
}
void oledShowWeight(uint16_t weight) {
    // Stub
}
void oledShowMessage(const String &message, uint8_t size) {
    // Stub
}
void oledShowTopRow() {
    // Stub
}
void oledShowIcon(const char* icon) {
    // Stub
}
