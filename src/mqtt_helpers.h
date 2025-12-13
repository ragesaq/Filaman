// MQTT helper utilities
#pragma once

#include <PubSubClient.h>

const char* mqttStateToString(int state);
unsigned long backoffDelayMs(uint8_t attempt, unsigned long baseMs = 5000, unsigned long maxMs = 300000);
