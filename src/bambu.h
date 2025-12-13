#ifndef BAMBU_H
#define BAMBU_H

#include <Arduino.h>
#include <ArduinoJson.h>

struct TrayData {
    uint8_t id;
    String tray_info_idx;
    String tray_type;
    String tray_sub_brands;
    String tray_color;
    int nozzle_temp_min;
    int nozzle_temp_max;
    String setting_id;
    String cali_idx;
    int remain;
    String tray_uuid;
    String tag_uid;
};

struct BambuCredentials {
    String ip;
    String serial;
    String accesscode;
    bool autosend_enable;
    int autosend_time;
};

#define MAX_AMS 17  // 16 normale AMS + 1 externe Spule
extern String amsJsonData;  // Für die vorbereiteten JSON-Daten

struct AMSData {
    uint8_t ams_id;
    TrayData trays[4]; // Annahme: Maximal 4 Trays pro AMS
};

// Structure for queued tag waiting for empty tray assignment
struct PendingTrayAssignment {
    String tagData;           // JSON data from the scanned tag
    String manufacturer;      // Manufacturer (Bambu Lab, Sunlu, etc.)
    String material;          // Material type (PLA, PETG, etc.)
    String brandName;         // Brand product name (PLA Tough+, PLA Basic, Rapid PETG)
    String color;             // Color hex code
    int dryingTemp;           // Drying temperature
    int dryingTime;           // Drying time in hours
    int nozzle_temp_min;
    int nozzle_temp_max;
    unsigned long queuedTime; // When the tag was queued
    bool valid;
};

extern bool bambu_connected;

extern int ams_count;
extern AMSData ams_data[MAX_AMS];
extern SemaphoreHandle_t amsDataMutex;
//extern bool autoSendToBambu;
extern uint16_t autoSetToBambuSpoolId;
extern bool bambuDisabled;
extern BambuCredentials bambuCredentials;

bool removeBambuCredentials();
bool loadBambuCredentials();
bool saveBambuCredentials(const String& bambu_ip, const String& bambu_serialnr, const String& bambu_accesscode, const bool autoSend, const String& autoSendTime);
bool setupMqtt();
void mqtt_loop(void * parameter);
bool setBambuSpool(String payload);
void bambu_restart();

// Empty tray detection and auto-assignment functions
bool hasEmptyAmsTray();
bool findEmptyAmsTray(uint8_t& amsId, uint8_t& trayId);
void queueTagForTrayAssignment(const String& tagData, const String& manufacturer, const String& material,
                                const String& brandName, const String& color, int dryingTemp, int dryingTime,
                                int tempMin, int tempMax);
void clearPendingTrayAssignment();
bool hasPendingTrayAssignment();
void checkPendingTrayAssignment();
void checkTrayFilled(uint8_t amsId, uint8_t trayId, bool isBambuSpool);

extern PendingTrayAssignment pendingTrayAssignment;
extern TaskHandle_t BambuMqttTask;
extern SemaphoreHandle_t amsDataMutex;
#endif
