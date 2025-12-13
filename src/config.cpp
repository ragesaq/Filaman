#include "config.h"

// ################### Config Bereich Start
// ***** RC522 RFID Reader Configuration
// SPI Bus Pins (shared with potential other SPI devices):
//   CLK   = GPIO18
//   MISO  = GPIO19
//   MOSI  = GPIO23
//   SS    = GPIO5  (RC522_SS_PIN)
//   RST   = GPIO22 (RC522_RST_PIN) - not used, soft reset only (follows AMSPlusCore)
const uint8_t PN532_IRQ = 32;
const uint8_t PN532_RESET = 33;
#ifdef USE_RC522
const uint8_t RC522_SS_PIN = 5;    // SPI Chip Select for RC522
const uint8_t RC522_RST_PIN = 22;  // Reset pin (not actively used - PCD_Init handles soft reset)
#endif
// ***** RC522 RFID Reader

// ***** HX711 (Waage)
// HX711 circuit wiring
const uint8_t LOADCELL_DOUT_PIN = 16; //16;
const uint8_t LOADCELL_SCK_PIN = 17; //17;
const uint8_t calVal_eepromAdress = 0;
const uint16_t SCALE_LEVEL_WEIGHT = 500;
// ***** HX711

// ***** TTP223 (Touch Sensor)
// TTP223 circuit wiring
// const uint8_t TTP223_PIN = 25;
// ***** TTP223

// ***** LED
// const uint8_t LED_PIN = 26;
// ***** LED


// ***** Display
const uint8_t OLED_TOP_START = 0;
const uint8_t OLED_TOP_END = 16;
const uint8_t OLED_DATA_START = 17;
const uint8_t OLED_DATA_END = SCREEN_HEIGHT;

// ***** Display

// ***** Webserver
const uint8_t webserverPort = 80;
// ***** Webserver

// ***** API
const char* apiUrl = "/api/v1";
// ***** API

// ***** Bambu Auto Set Spool

// ***** Task Prios
uint8_t rfidTaskCore = 1;
uint8_t rfidTaskPrio = 1;

uint8_t rfidWriteTaskPrio = 1;

uint8_t mqttTaskCore = 1;
uint8_t mqttTaskPrio = 1;

uint8_t scaleTaskCore = 0;
uint8_t scaleTaskPrio = 1;
// ***** Task Prios
