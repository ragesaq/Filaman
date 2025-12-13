#ifndef LED_H
#define LED_H

#include <Arduino.h>

typedef enum {
	LED_PATTERN_OFF,
	LED_PATTERN_INITIALIZING,
	LED_PATTERN_INIT_ERROR,
	LED_PATTERN_SEARCHING,
	LED_PATTERN_TAG_FOUND,
	LED_PATTERN_AMS_QUEUED,
	LED_PATTERN_AMS_WAITING,    // Yellow pulse - waiting for empty tray to be filled
	LED_PATTERN_NO_EMPTY_TRAY,  // Red pulse - no empty tray available
	LED_PATTERN_WRITE_QUEUE,
	LED_PATTERN_PREPARE_WRITE,
	LED_PATTERN_WRITING,
	LED_PATTERN_WRITE_SUCCESS,
	LED_PATTERN_WRITE_FAILURE
} LedPattern;

void setupLed();
void setLedDefaultPattern(LedPattern pattern);
void triggerLedPattern(LedPattern pattern, uint32_t durationMs);
LedPattern getLedDefaultPattern();

#endif
