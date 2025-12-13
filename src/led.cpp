#include "led.h"
#include "config.h"
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
SemaphoreHandle_t ledMutex = NULL;
TaskHandle_t ledTaskHandle = NULL;

static LedPattern ledDefaultPattern = LED_PATTERN_OFF;
static LedPattern ledOverridePattern = LED_PATTERN_OFF;
static bool ledOverrideActive = false;
static uint32_t ledOverrideExpires = 0;

static void applyColor(uint32_t color);
static void ledControllerTask(void *parameter);
static LedPattern getCurrentPattern(uint32_t now);

void setupLed() {
    ledMutex = xSemaphoreCreateMutex();
    strip.begin();
    strip.setBrightness(120);
    strip.show();
    xTaskCreate(ledControllerTask, "LedCtrl", 4096, NULL, 1, &ledTaskHandle);
}

void setLedDefaultPattern(LedPattern pattern) {
    ledDefaultPattern = pattern;
}

void triggerLedPattern(LedPattern pattern, uint32_t durationMs) {
    if (durationMs == 0) {
        ledOverrideActive = false;
        return;
    }
    ledOverridePattern = pattern;
    ledOverrideActive = true;
    ledOverrideExpires = millis() + durationMs;
}

LedPattern getLedDefaultPattern() {
    return ledDefaultPattern;
}

static void applyColor(uint32_t color) {
    strip.setPixelColor(0, color);
    if (ledMutex != NULL && xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
        strip.show();
        xSemaphoreGive(ledMutex);
    }
}

static uint32_t scaleColor(uint8_t value, uint8_t intensity) {
    return (uint32_t)value * intensity / 255U;
}

static void applyPulse(LedPattern active, uint8_t r, uint8_t g, uint8_t b, uint8_t step) {
    static int16_t pulseLevel = 0;
    static int8_t direction = 1;
    static LedPattern lastPulse = LED_PATTERN_OFF;

    if (active != lastPulse) {
        pulseLevel = 0;
        direction = 1;
        lastPulse = active;
    }

    pulseLevel += step * direction;
    if (pulseLevel >= 255) {
        pulseLevel = 255;
        direction = -1;
    } else if (pulseLevel <= 0) {
        pulseLevel = 0;
        direction = 1;
    }

    uint8_t intensity = (uint8_t)pulseLevel;
    uint8_t scaledR = (uint8_t)scaleColor(r, intensity);
    uint8_t scaledG = (uint8_t)scaleColor(g, intensity);
    uint8_t scaledB = (uint8_t)scaleColor(b, intensity);
    applyColor(strip.Color(scaledR, scaledG, scaledB));
}

static void applyFlash(LedPattern active, uint32_t color, uint32_t intervalMs) {
    static uint32_t lastToggle = 0;
    static bool state = false;
    static LedPattern lastFlash = LED_PATTERN_OFF;

    if (active != lastFlash) {
        state = false;
        lastToggle = 0;
        lastFlash = active;
    }

    uint32_t now = millis();
    if (now - lastToggle >= intervalMs) {
        state = !state;
        lastToggle = now;
    }

    applyColor(state ? color : strip.Color(0, 0, 0));
}

static void applySequence(LedPattern active, const uint32_t *sequence, size_t count, uint32_t intervalMs) {
    static uint32_t lastStep = 0;
    static size_t index = 0;
    static LedPattern lastSequence = LED_PATTERN_OFF;

    if (active != lastSequence) {
        index = 0;
        lastStep = 0;
        lastSequence = active;
    }

    uint32_t now = millis();
    if (now - lastStep >= intervalMs) {
        index = (index + 1) % count;
        lastStep = now;
    }
    applyColor(sequence[index]);
}

static LedPattern getCurrentPattern(uint32_t now) {
    if (ledOverrideActive) {
        if ((int32_t)(now - ledOverrideExpires) >= 0) {
            ledOverrideActive = false;
        }
    }
    return ledOverrideActive ? ledOverridePattern : ledDefaultPattern;
}

static void ledControllerTask(void *parameter) {
    (void)parameter;
    while (true) {
        uint32_t now = millis();
        LedPattern active = getCurrentPattern(now);

        switch (active) {
            case LED_PATTERN_INITIALIZING:
                applyPulse(active, 0, 0, 200, 6);
                break;
            case LED_PATTERN_INIT_ERROR:
                applyFlash(active, strip.Color(255, 0, 0), 300);
                break;
            case LED_PATTERN_SEARCHING:
                applyPulse(active, 255, 255, 255, 10);
                break;
            case LED_PATTERN_TAG_FOUND: {
                const uint32_t colors[] = { strip.Color(255, 255, 255), strip.Color(255, 0, 0), strip.Color(0, 255, 0), strip.Color(0, 0, 255) };
                applySequence(active, colors, 4, 150);
                break;
            }
            case LED_PATTERN_AMS_QUEUED:
                applyPulse(active, 255, 190, 0, 5);
                break;
            case LED_PATTERN_AMS_WAITING:
                applyPulse(active, 255, 200, 0, 8);  // Yellow pulse - waiting for tray fill
                break;
            case LED_PATTERN_NO_EMPTY_TRAY:
                applyPulse(active, 255, 0, 0, 10);   // Red pulse - no empty tray
                break;
            case LED_PATTERN_WRITE_QUEUE:
                applyPulse(active, 255, 220, 100, 5);
                break;
            case LED_PATTERN_PREPARE_WRITE:
                applyPulse(active, 150, 0, 150, 3);
                break;
            case LED_PATTERN_WRITING:
                applyFlash(active, strip.Color(120, 0, 180), 100);
                break;
            case LED_PATTERN_WRITE_SUCCESS:
                applyFlash(active, strip.Color(0, 255, 0), 80);
                break;
            case LED_PATTERN_WRITE_FAILURE:
                applyFlash(active, strip.Color(255, 0, 0), 80);
                break;
            case LED_PATTERN_OFF:
            default:
                applyColor(strip.Color(0, 0, 0));
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
