#include "mqtt_helpers.h"
#include <Arduino.h>

const char* mqttStateToString(int state) {
    switch (state) {
        case -4: return "MQTT_CONNECTION_TIMEOUT";
        case -3: return "MQTT_CONNECTION_LOST";
        case -2: return "MQTT_CONNECT_FAILED";
        case -1: return "MQTT_DISCONNECTED";
        case 0:  return "MQTT_CONNECTED";
        case 1:  return "MQTT_CONNECT_BAD_PROTOCOL";
        case 2:  return "MQTT_CONNECT_BAD_CLIENT_ID";
        case 3:  return "MQTT_CONNECT_UNAVAILABLE";
        case 4:  return "MQTT_CONNECT_BAD_CREDENTIALS";
        case 5:  return "MQTT_CONNECT_UNAUTHORIZED";
        default: return "MQTT_UNKNOWN_STATE";
    }
}

// Exponential backoff with jitter (ms)
unsigned long backoffDelayMs(uint8_t attempt, unsigned long baseMs, unsigned long maxMs) {
    if (attempt == 0) return baseMs;
    // calculate exponential backoff
    unsigned long delay = baseMs * (1UL << (attempt > 6 ? 6 : attempt));
    if (delay > maxMs) delay = maxMs;
    // add small random jitter
    unsigned long jitter = random(0, 1000);
    unsigned long result = delay + jitter;
    if (result > maxMs) result = maxMs;
    return result;
}
