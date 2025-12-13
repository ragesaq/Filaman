#include "scale.h"
// Stubs for Scale to save memory
// #include "HX711.h" // Removed
TaskHandle_t ScaleTask = NULL;
int16_t weight = 0;
uint8_t weightCounterToApi = 0;
uint8_t scale_tare_counter = 0;
uint8_t scaleTareRequest = 0;
uint8_t pauseMainTask = 0;
bool scaleCalibrated = false;
bool autoTare = true;
bool scaleCalibrationActive = false;
uint8_t setAutoTare(bool autoTareValue) {
    autoTare = autoTareValue;
    return 1;
}
uint8_t start_scale(bool touchSensorConnected) {
    return 1;
}
uint8_t calibrate_scale() {
    return 1;
}
uint8_t tareScale() {
    return 1;
}
void resetWeightFilter() {
}
float calculateMovingAverage() {
    return 0.0f;
}
float applyLowPassFilter(float newValue) {
    return 0.0f;
}
int16_t processWeightReading(float rawWeight) {
    return 0;
}
int16_t getFilteredDisplayWeight() {
    return 0;
}
