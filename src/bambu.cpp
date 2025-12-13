#include "bambu.h"
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <SSLClient.h>
#include "bambu_cert.h"
#include "website.h"
#include "nfc.h"
#include "commonFS.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "display.h"
#include "led.h"
#include <Preferences.h>
#include "mqtt_helpers.h"

#ifndef ASYNC_MQTT
#include <PubSubClient.h>
WiFiClient espClient;
SSLClient sslClient(&espClient);
PubSubClient client(sslClient);
#else
#include "mqtt_adapter.h"
#endif


TaskHandle_t BambuMqttTask;

bool bambuDisabled = false;

bool bambu_connected = false;
uint16_t autoSetToBambuSpoolId = 0;

BambuCredentials bambuCredentials;
SemaphoreHandle_t amsDataMutex = NULL;

// Globale Variablen für AMS-Daten
int ams_count = 0;
String amsJsonData;  // Speichert das fertige JSON für WebSocket-Clients
AMSData ams_data[MAX_AMS];  // Definition des Arrays;

// Pending tray assignment for auto-fill feature
// Fields: tagData, manufacturer, material, brandName, color, dryingTemp, dryingTime, nozzle_temp_min, nozzle_temp_max, queuedTime, valid
PendingTrayAssignment pendingTrayAssignment = {"", "", "", "", "", 0, 0, 0, 0, 0, false};
#define TRAY_ASSIGNMENT_TIMEOUT_MS 120000  // 2 minute timeout

// Track empty trays to detect when they get filled
static uint8_t emptyTrayAmsId = 255;
static uint8_t emptyTrayId = 255;
static bool trackingEmptyTray = false;

bool removeBambuCredentials() {
    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
        BambuMqttTask = NULL;
    }
    
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_BAMBU, false); // false = readwrite
    preferences.remove(NVS_KEY_BAMBU_IP);
    preferences.remove(NVS_KEY_BAMBU_SERIAL);
    preferences.remove(NVS_KEY_BAMBU_ACCESSCODE);
    preferences.remove(NVS_KEY_BAMBU_AUTOSEND_ENABLE);
    preferences.remove(NVS_KEY_BAMBU_AUTOSEND_TIME);
    preferences.end();

    // Löschen der globalen Variablen
    bambuCredentials.ip = "";
    bambuCredentials.serial = "";
    bambuCredentials.accesscode = "";
    bambuCredentials.autosend_enable = false;
    bambuCredentials.autosend_time = BAMBU_DEFAULT_AUTOSEND_TIME;

    autoSetToBambuSpoolId = 0;
    ams_count = 0;
    amsJsonData = "";

    bambuDisabled = true;

    return true;
}

bool saveBambuCredentials(const String& ip, const String& serialnr, const String& accesscode, bool autoSend, const String& autoSendTime) {
    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
        BambuMqttTask = NULL;
    }

    bambuCredentials.ip = ip.c_str();
    bambuCredentials.serial = serialnr.c_str();
    bambuCredentials.accesscode = accesscode.c_str();
    bambuCredentials.autosend_enable = autoSend;
    bambuCredentials.autosend_time = autoSendTime.toInt();

    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_BAMBU, false); // false = readwrite
    preferences.putString(NVS_KEY_BAMBU_IP, bambuCredentials.ip);
    preferences.putString(NVS_KEY_BAMBU_SERIAL, bambuCredentials.serial);
    preferences.putString(NVS_KEY_BAMBU_ACCESSCODE, bambuCredentials.accesscode);
    preferences.putBool(NVS_KEY_BAMBU_AUTOSEND_ENABLE, bambuCredentials.autosend_enable);
    preferences.putInt(NVS_KEY_BAMBU_AUTOSEND_TIME, bambuCredentials.autosend_time);
    preferences.end();

    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (!setupMqtt()) return false;

    return true;
}

bool loadBambuCredentials() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_BAMBU, true);
    String ip = preferences.getString(NVS_KEY_BAMBU_IP, "");
    String serial = preferences.getString(NVS_KEY_BAMBU_SERIAL, "");
    String code = preferences.getString(NVS_KEY_BAMBU_ACCESSCODE, "");
    bool autosendEnable = preferences.getBool(NVS_KEY_BAMBU_AUTOSEND_ENABLE, false);
    int autosendTime = preferences.getInt(NVS_KEY_BAMBU_AUTOSEND_TIME, BAMBU_DEFAULT_AUTOSEND_TIME);
    preferences.end();

    if(ip != ""){
        bambuCredentials.ip = ip.c_str();
        bambuCredentials.serial = serial.c_str();
        bambuCredentials.accesscode = code.c_str();
        bambuCredentials.autosend_enable = autosendEnable;
        bambuCredentials.autosend_time = autosendTime;

        Serial.println("credentials loaded loadCredentials!");
        Serial.println(bambuCredentials.ip);
        Serial.println(bambuCredentials.serial);
        Serial.println(bambuCredentials.accesscode);
        Serial.println(String(bambuCredentials.autosend_enable));
        Serial.println(String(bambuCredentials.autosend_time));

        return true;
    }
    else
    {
        Serial.println("Keine gültigen Bambu-Credentials gefunden.");
        return false;
    }
}

struct FilamentResult {
    String key;
    String type;
};

FilamentResult findFilamentIdx(String brand, String type) {
    // JSON-Dokument für die Filament-Daten erstellen
    JsonDocument doc;
    
    // Laden der own_filaments.json
    String ownFilament = "";
    if (!loadJsonValue("/own_filaments.json", doc)) 
    {
        Serial.println("Fehler beim Laden der eigenen Filament-Daten");
    }
    else
    {
        // Durchsuche direkt nach dem Type als Schlüssel
        if (doc[type].is<String>()) {
            ownFilament = doc[type].as<String>();
        }
        doc.clear();
    }
    doc.clear();

    // Laden der bambu_filaments.json
    if (!loadJsonValue("/bambu_filaments.json", doc)) 
    {
        Serial.println("Fehler beim Laden der Filament-Daten");
        return {"GFL99", "PLA"}; // Fallback auf Generic PLA
    }

    // Wenn eigener Typ
    if (ownFilament != "")
    {
        if (doc[ownFilament].is<String>()) 
        {
            return {ownFilament, doc[ownFilament].as<String>()};
        }
    }

    // 1. Erst versuchen wir die exakte Brand + Type Kombination zu finden
    String searchKey;
    if (brand == "Bambu" || brand == "Bambulab") {
        searchKey = "Bambu " + type;
    } else if (brand == "PolyLite") {
        searchKey = "PolyLite " + type;
    } else if (brand == "eSUN") {
        searchKey = "eSUN " + type;
    } else if (brand == "Overture") {
        searchKey = "Overture " + type;
    } else if (brand == "PolyTerra") {
        searchKey = "PolyTerra " + type;
    }

    // Durchsuche alle Einträge nach der Brand + Type Kombination
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (kv.value().as<String>() == searchKey) {
            return {kv.key().c_str(), kv.value().as<String>()};
        }
    }

    // 2. Wenn nicht gefunden, zerlege den type String in Wörter und suche nach jedem Wort
    // Sammle alle vorhandenen Filamenttypen aus der JSON
    std::vector<String> knownTypes;
    for (JsonPair kv : doc.as<JsonObject>()) {
        String value = kv.value().as<String>();
        // Extrahiere den Typ ohne Markennamen
        if (value.indexOf(" ") != -1) {
            value = value.substring(value.indexOf(" ") + 1);
        }
        if (!value.isEmpty()) {
            knownTypes.push_back(value);
        }
    }

    // Zerlege den Input-Type in Wörter
    String typeStr = type;
    typeStr.trim();
    
    // Durchsuche für jedes bekannte Filament, ob es im Input vorkommt
    for (const String& knownType : knownTypes) {
        if (typeStr.indexOf(knownType) != -1) {
            // Suche nach diesem Typ in der Original-JSON
            for (JsonPair kv : doc.as<JsonObject>()) {
                String value = kv.value().as<String>();
                if (value.indexOf(knownType) != -1) {
                    return {kv.key().c_str(), knownType};
                }
            }
        }
    }

    // 3. Wenn immer noch nichts gefunden, gebe GFL99 zurück (Generic PLA)
    return {"GFL99", "PLA"};
}

bool sendMqttMessage(const String& payload) {
    Serial.println("Sending MQTT message");
    Serial.println(payload);
    if (client.publish(("device/"+bambuCredentials.serial+"/request").c_str(), payload.c_str())) 
    {
        return true;
    }
    
    return false;
}

bool setBambuSpool(String payload) {
    Serial.println("Spool settings in");
    Serial.println(payload);

    // Parse the JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.print("Error parsing JSON: ");
        Serial.println(error.c_str());
        return false;
    }

    int amsId = doc["amsId"];
    int trayId = doc["trayId"];
    String color = doc["color"].as<String>();
    color.toUpperCase();
    int minTemp = doc["nozzle_temp_min"];
    int maxTemp = doc["nozzle_temp_max"];
    String type = doc["type"].as<String>();
    (type == "PLA+") ? type = "PLA" : type;
    String brand = doc["brand"].as<String>();
    String tray_info_idx = (doc["tray_info_idx"].as<String>() != "-1") ? doc["tray_info_idx"].as<String>() : "";
    if (tray_info_idx == "") {
        if (brand != "" && type != "") {
            FilamentResult result = findFilamentIdx(brand, type);
            tray_info_idx = result.key;
            type = result.type;  // Aktualisiere den type mit dem gefundenen Basistyp
        }
    }
    String setting_id = doc["bambu_setting_id"].as<String>();
    String cali_idx = doc["cali_idx"].as<String>();

    doc.clear();

    doc["print"]["sequence_id"] = "0";
    doc["print"]["command"] = "ams_filament_setting";
    doc["print"]["ams_id"] = amsId < 200 ? amsId : 255;
    doc["print"]["tray_id"] = trayId < 200 ? trayId : 254;
    doc["print"]["tray_color"] = color.length() == 8 ? color : color+"FF";
    doc["print"]["nozzle_temp_min"] = minTemp;
    doc["print"]["nozzle_temp_max"] = maxTemp;
    doc["print"]["tray_type"] = type;
    //doc["print"]["cali_idx"] = (cali_idx != "") ? cali_idx : "";
    doc["print"]["tray_info_idx"] = tray_info_idx;
    doc["print"]["setting_id"] = setting_id;
    
    // Serialize the JSON
    String output;
    serializeJson(doc, output);

    if (sendMqttMessage(output)) {
        Serial.println("Spool successfully set");
    }
    else
    {
        Serial.println("Failed to set spool");
        return false;
    }
    
    doc.clear();
    yield();

    if (cali_idx != "") {
        yield();
        doc["print"]["sequence_id"] = "0";
        doc["print"]["command"] = "extrusion_cali_sel";
        doc["print"]["filament_id"] = tray_info_idx;
        doc["print"]["nozzle_diameter"] = "0.4";
        doc["print"]["cali_idx"] = cali_idx.toInt();
        doc["print"]["tray_id"] = trayId < 200 ? trayId : 254;
        //doc["print"]["ams_id"] = amsId < 200 ? amsId : 255;

        // Serialize the JSON
        String output;
        serializeJson(doc, output);

        if (sendMqttMessage(output)) {
            Serial.println("Extrusion calibration successfully set");
        }
        else
        {
            Serial.println("Failed to set extrusion calibration");
            return false;
        }

        doc.clear();
        yield();
    }

    return true;
}

void autoSetSpool(int spoolId, uint8_t trayId) {
    // wenn neue spule erkannt und autoSetToBambu > 0
    JsonDocument spoolInfo = fetchSingleSpoolInfo(spoolId);

    if (!spoolInfo.isNull())
    {
        // AMS und TRAY id ergänzen
        spoolInfo["amsId"] = 0;
        spoolInfo["trayId"] = trayId;

        Serial.println("Auto set spool");
        Serial.println(spoolInfo.as<String>());

        setBambuSpool(spoolInfo.as<String>());

        oledShowMessage("Spool set");
    }

    // id wieder zurücksetzen damit abgeschlossen
    autoSetToBambuSpoolId = 0;
}

// ============= Empty Tray Detection and Auto-Assignment Functions =============

bool hasEmptyAmsTray() {
    for (int i = 0; i < ams_count; i++) {
        // Skip external spool (ams_id 255)
        if (ams_data[i].ams_id == 255) continue;
        
        int maxTrays = 4;
        for (int j = 0; j < maxTrays; j++) {
            // Empty tray has empty tray_type
            if (ams_data[i].trays[j].tray_type == "" || ams_data[i].trays[j].tray_type.length() == 0) {
                return true;
            }
        }
    }
    return false;
}

bool findEmptyAmsTray(uint8_t& amsId, uint8_t& trayId) {
    for (int i = 0; i < ams_count; i++) {
        // Skip external spool (ams_id 255)
        if (ams_data[i].ams_id == 255) continue;
        
        int maxTrays = 4;
        for (int j = 0; j < maxTrays; j++) {
            // Empty tray has empty tray_type
            if (ams_data[i].trays[j].tray_type == "" || ams_data[i].trays[j].tray_type.length() == 0) {
                amsId = ams_data[i].ams_id;
                trayId = ams_data[i].trays[j].id;
                return true;
            }
        }
    }
    return false;
}

void queueTagForTrayAssignment(const String& tagData, const String& manufacturer, const String& material,
                                const String& brandName, const String& color, int dryingTemp, int dryingTime,
                                int tempMin, int tempMax) {
    // Check if there's an empty tray to wait for
    uint8_t amsId, trayId;
    if (!findEmptyAmsTray(amsId, trayId)) {
        // No empty tray - pulse red and return to normal
        Serial.println("No empty AMS tray available for tag assignment");
        triggerLedPattern(LED_PATTERN_NO_EMPTY_TRAY, 3000);
        oledShowMessage("No empty tray");
        return;
    }
    
    // Queue the tag for assignment
    pendingTrayAssignment.tagData = tagData;
    pendingTrayAssignment.manufacturer = manufacturer;
    pendingTrayAssignment.material = material;
    pendingTrayAssignment.brandName = brandName;
    pendingTrayAssignment.color = color;
    pendingTrayAssignment.dryingTemp = dryingTemp;
    pendingTrayAssignment.dryingTime = dryingTime;
    pendingTrayAssignment.nozzle_temp_min = tempMin;
    pendingTrayAssignment.nozzle_temp_max = tempMax;
    pendingTrayAssignment.queuedTime = millis();
    pendingTrayAssignment.valid = true;
    
    // Track which empty tray we're waiting for
    emptyTrayAmsId = amsId;
    emptyTrayId = trayId;
    trackingEmptyTray = true;
    
    Serial.printf("Tag queued for tray assignment (AMS %d, Tray %d). Waiting for tray to be filled...\n", amsId, trayId);
    setLedDefaultPattern(LED_PATTERN_AMS_WAITING);
    oledShowMessage("Waiting for tray");
}

void clearPendingTrayAssignment() {
    pendingTrayAssignment.tagData = "";
    pendingTrayAssignment.manufacturer = "";
    pendingTrayAssignment.material = "";
    pendingTrayAssignment.brandName = "";
    pendingTrayAssignment.color = "";
    pendingTrayAssignment.dryingTemp = 0;
    pendingTrayAssignment.dryingTime = 0;
    pendingTrayAssignment.nozzle_temp_min = 0;
    pendingTrayAssignment.nozzle_temp_max = 0;
    pendingTrayAssignment.queuedTime = 0;
    pendingTrayAssignment.valid = false;
    trackingEmptyTray = false;
    emptyTrayAmsId = 255;
    emptyTrayId = 255;
    
    setLedDefaultPattern(LED_PATTERN_SEARCHING);
}

bool hasPendingTrayAssignment() {
    return pendingTrayAssignment.valid;
}

void checkPendingTrayAssignment() {
    if (!pendingTrayAssignment.valid) return;
    
    // Check for timeout (2 minutes)
    if (millis() - pendingTrayAssignment.queuedTime >= TRAY_ASSIGNMENT_TIMEOUT_MS) {
        Serial.println("Pending tray assignment timed out after 2 minutes");
        oledShowMessage("Tray assign timeout");
        clearPendingTrayAssignment();
        return;
    }
}

// Called when a tray changes from empty to filled
void checkTrayFilled(uint8_t amsId, uint8_t trayId, bool isBambuSpool) {
    if (!pendingTrayAssignment.valid || !trackingEmptyTray) return;
    
    // Check if this is the tray we're tracking
    if (amsId != emptyTrayAmsId || trayId != emptyTrayId) return;
    
    Serial.printf("Tracked tray (AMS %d, Tray %d) has been filled\n", amsId, trayId);
    
    // If it's a Bambu spool, it will be auto-identified - don't override
    if (isBambuSpool) {
        Serial.println("Bambu spool detected - not overriding with queued tag data");
        clearPendingTrayAssignment();
        return;
    }
    
    // Non-Bambu spool (generic/third-party) - assign from queued tag data
    Serial.println("Non-Bambu spool detected - assigning from queued tag data");
    
    // Create payload for setBambuSpool
    JsonDocument doc;
    doc["amsId"] = amsId;
    doc["trayId"] = trayId;
    doc["color"] = pendingTrayAssignment.color;
    doc["nozzle_temp_min"] = pendingTrayAssignment.nozzle_temp_min;
    doc["nozzle_temp_max"] = pendingTrayAssignment.nozzle_temp_max;
    doc["type"] = pendingTrayAssignment.material;
    doc["brand"] = pendingTrayAssignment.manufacturer;  // Use manufacturer for brand field
    doc["tray_info_idx"] = "-1";  // Let setBambuSpool find the correct index
    doc["bambu_setting_id"] = "";
    doc["cali_idx"] = "";
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("Auto-assigning queued tag to filled tray:");
    Serial.println(payload);
    
    if (setBambuSpool(payload)) {
        oledShowMessage("Tray assigned!");
        triggerLedPattern(LED_PATTERN_WRITE_SUCCESS, 2000);
    } else {
        oledShowMessage("Assign failed");
        triggerLedPattern(LED_PATTERN_WRITE_FAILURE, 2000);
    }
    
    clearPendingTrayAssignment();
}

// ============= End Empty Tray Functions =============

void updateAmsWsData(JsonDocument& doc, JsonArray& amsArray, int& ams_count, JsonObject& vtTray) {
    // Fortfahren mit der bestehenden Verarbeitung, da Änderungen gefunden wurden
    int normalAms = amsArray.size();
    if (normalAms > MAX_AMS - 1) {
        normalAms = MAX_AMS - 1; // Reserve one slot for the optional external spool
    }
    ams_count = normalAms;
        
    for (int i = 0; i < ams_count; i++) {
        JsonObject amsObj = amsArray[i];
        JsonArray trayArray = amsObj["tray"].as<JsonArray>();

        // ams_data[i].ams_id = i; // Setze die AMS-ID
        ams_data[i].ams_id = amsObj["id"].as<uint8_t>(); // Use the ID from JSON
        
        Serial.print("Processing AMS ID: ");
        Serial.println(ams_data[i].ams_id);

        for (int j = 0; j < trayArray.size() && j < 4; j++) { // Annahme: Maximal 4 Trays pro AMS
            JsonObject trayObj = trayArray[j];

            ams_data[i].trays[j].id = trayObj["id"].as<uint8_t>();
            ams_data[i].trays[j].tray_info_idx = trayObj["tray_info_idx"].as<String>();
            ams_data[i].trays[j].tray_type = trayObj["tray_type"].as<String>();
            ams_data[i].trays[j].tray_sub_brands = trayObj["tray_sub_brands"].as<String>();
            ams_data[i].trays[j].tray_color = trayObj["tray_color"].as<String>();
            ams_data[i].trays[j].nozzle_temp_min = trayObj["nozzle_temp_min"].as<int>();
            ams_data[i].trays[j].nozzle_temp_max = trayObj["nozzle_temp_max"].as<int>();
            if (trayObj["tray_type"].as<String>() == "") ams_data[i].trays[j].setting_id = "";
            ams_data[i].trays[j].cali_idx = trayObj["cali_idx"].as<String>();
            ams_data[i].trays[j].remain = trayObj["remain"].as<int>();
            ams_data[i].trays[j].tray_uuid = trayObj["tray_uuid"].as<String>();
            String uid = trayObj["tag_uid"].as<String>();
            if (uid == "null") uid = "";
            ams_data[i].trays[j].tag_uid = uid;
            Serial.print("Tray "); Serial.print(j); Serial.print(" Tag UID: "); Serial.println(ams_data[i].trays[j].tag_uid);
        }
    }
    
    // Wenn externe Spule vorhanden, füge sie hinzu
    if (doc["print"]["vt_tray"].is<JsonObject>() && ams_count < MAX_AMS) {
        int extIdx = ams_count;  // Index für externe Spule
        ams_data[extIdx].ams_id = 255;  // Spezielle ID für externe Spule
        ams_data[extIdx].trays[0].id = 254;  // Spezielle ID für externes Tray
        ams_data[extIdx].trays[0].tray_info_idx = vtTray["tray_info_idx"].as<String>();
        ams_data[extIdx].trays[0].tray_type = vtTray["tray_type"].as<String>();
        ams_data[extIdx].trays[0].tray_sub_brands = vtTray["tray_sub_brands"].as<String>();
        ams_data[extIdx].trays[0].tray_color = vtTray["tray_color"].as<String>();
        ams_data[extIdx].trays[0].nozzle_temp_min = vtTray["nozzle_temp_min"].as<int>();
        ams_data[extIdx].trays[0].nozzle_temp_max = vtTray["nozzle_temp_max"].as<int>();
        ams_data[extIdx].trays[0].remain = vtTray["remain"].as<int>();
        ams_data[extIdx].trays[0].tray_uuid = vtTray["tray_uuid"].as<String>();
        String uid = vtTray["tag_uid"].as<String>();
        if (uid == "null") uid = "";
        ams_data[extIdx].trays[0].tag_uid = uid;

        if (doc["print"]["vt_tray"]["tray_type"].as<String>() != "")
        {
            //ams_data[extIdx].trays[0].setting_id = vtTray["setting_id"].as<String>();
            ams_data[extIdx].trays[0].cali_idx = vtTray["cali_idx"].as<String>();
        }
        else
        {
            ams_data[extIdx].trays[0].setting_id = "";
            ams_data[extIdx].trays[0].cali_idx = "";
        }
        ams_count++;  // Erhöhe ams_count für die externe Spule
    }

    // Erstelle JSON für WebSocket-Clients
    JsonDocument wsDoc;
    JsonArray wsArray = wsDoc.to<JsonArray>();

    for (int i = 0; i < ams_count; i++) {
        JsonObject amsObj = wsArray.add<JsonObject>();
        amsObj["ams_id"] = ams_data[i].ams_id;

        JsonArray trays = amsObj["tray"].to<JsonArray>();
        int maxTrays = (ams_data[i].ams_id == 255) ? 1 : 4;
        
        for (int j = 0; j < maxTrays; j++) {
            JsonObject trayObj = trays.add<JsonObject>();
            trayObj["id"] = ams_data[i].trays[j].id;
            trayObj["tray_info_idx"] = ams_data[i].trays[j].tray_info_idx;
            trayObj["tray_type"] = ams_data[i].trays[j].tray_type;
            trayObj["tray_sub_brands"] = ams_data[i].trays[j].tray_sub_brands;
            trayObj["tray_color"] = ams_data[i].trays[j].tray_color;
            trayObj["nozzle_temp_min"] = ams_data[i].trays[j].nozzle_temp_min;
            trayObj["nozzle_temp_max"] = ams_data[i].trays[j].nozzle_temp_max;
            trayObj["setting_id"] = ams_data[i].trays[j].setting_id;
            trayObj["cali_idx"] = ams_data[i].trays[j].cali_idx;
            trayObj["remain"] = ams_data[i].trays[j].remain;
            trayObj["tray_uuid"] = ams_data[i].trays[j].tray_uuid;
            trayObj["tag_uid"] = ams_data[i].trays[j].tag_uid;
        }
    }

    if (amsDataMutex != NULL) {
        if (xSemaphoreTake(amsDataMutex, portMAX_DELAY) == pdTRUE) {
            amsJsonData = "";
            serializeJson(wsArray, amsJsonData);
            xSemaphoreGive(amsDataMutex);
        }
    } else {
        amsJsonData = "";
        serializeJson(wsArray, amsJsonData);
    }

    wsDoc.clear();
    Serial.print("AMS data updated. JSON length: ");
    Serial.println(amsJsonData.length());
    // Serial.println(amsJsonData);
    sendAmsData(nullptr);
}

// init
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    // Log which topic and size
    Serial.printf("MQTT Msg: %u bytes on %s\n", length, topic);
    
    // Check if this is a REPORT message (which should have AMS data)
    if (strstr(topic, "/report") != NULL) {
        Serial.println("  [REPORT topic] This message should contain AMS data");
    } else if (strstr(topic, "/request") != NULL) {
        Serial.println("  [REQUEST topic] This is an echo/ack of our request");
    }

    static uint32_t lastMqttProcessTime = 0;
    // Always process if we don't have data yet, otherwise throttle
    if (ams_count > 0 && millis() - lastMqttProcessTime < 5000) {
        return;  // Throttle within a poll session
    }
    
    if (ESP.getFreeHeap() < 15000) {
        Serial.printf("Low memory (%u), skipping MQTT processing\n", ESP.getFreeHeap());
        return;
    }

    Serial.println("Processing MQTT message...");
    lastMqttProcessTime = millis();

    // Filter definieren, um Speicher zu sparen
    static JsonDocument filter;
    if (filter.size() == 0) {
        filter["print"]["command"] = true;
        filter["print"]["sequence_id"] = true;
        filter["print"]["upgrade_state"] = true;
        filter["print"]["ams"]["ams"][0]["id"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["id"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["tray_info_idx"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["tray_type"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["tray_sub_brands"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["tray_color"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["nozzle_temp_min"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["nozzle_temp_max"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["setting_id"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["cali_idx"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["remain"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["tray_uuid"] = true;
        filter["print"]["ams"]["ams"][0]["tray"][0]["tag_uid"] = true;
        
        filter["print"]["vt_tray"]["tray_info_idx"] = true;
        filter["print"]["vt_tray"]["tray_type"] = true;
        filter["print"]["vt_tray"]["tray_sub_brands"] = true;
        filter["print"]["vt_tray"]["tray_color"] = true;
        filter["print"]["vt_tray"]["nozzle_temp_min"] = true;
        filter["print"]["vt_tray"]["nozzle_temp_max"] = true;
        filter["print"]["vt_tray"]["setting_id"] = true;
        filter["print"]["vt_tray"]["cali_idx"] = true;
        filter["print"]["vt_tray"]["remain"] = true;
        filter["print"]["vt_tray"]["tray_uuid"] = true;
        filter["print"]["vt_tray"]["tag_uid"] = true;

        filter["print"]["ams_id"] = true;
        filter["print"]["tray_id"] = true;
        filter["print"]["setting_id"] = true;
    }

    // JSON-Dokument parsen
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length, DeserializationOption::Filter(filter));
    // DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) 
    {
        Serial.print("Fehler beim Parsen des JSON: ");
        Serial.println(error.c_str());
        return;
    }

    serializeJson(doc, Serial);
    Serial.println();

    if (doc.containsKey("print")) {
        Serial.println("JSON contains 'print' key");
        if (doc["print"].containsKey("ams")) {
             Serial.println("JSON contains 'ams' key");
        } else {
             Serial.println("JSON missing 'ams' key");
        }
    } else {
        Serial.println("JSON missing 'print' key");
    }

    // Prüfen, ob "print->upgrade_state" und "print.ams.ams" existieren
    if (doc["print"]["upgrade_state"].is<JsonObject>() || (doc["print"]["command"].is<String>() && doc["print"]["command"] == "push_status") || doc["print"]["ams"].is<JsonObject>()) 
    {
        // Prüfen ob AMS-Daten vorhanden sind
        if (!doc["print"]["ams"].is<JsonObject>() || !doc["print"]["ams"]["ams"].is<JsonArray>()) 
        {
            return;
        }

        JsonArray amsArray = doc["print"]["ams"]["ams"].as<JsonArray>();

        // Prüfe ob sich die AMS-Daten geändert haben
        bool hasChanges = false;
        
        // Vergleiche jedes AMS und seine Trays
        for (int i = 0; i < amsArray.size() && !hasChanges; i++) {
            JsonObject amsObj = amsArray[i];
            int amsId = amsObj["id"].as<uint8_t>();
            JsonArray trayArray = amsObj["tray"].as<JsonArray>();
            
            // Finde das entsprechende AMS in unseren Daten
            int storedIndex = -1;
            for (int k = 0; k < ams_count; k++) {
                if (ams_data[k].ams_id == amsId) {
                    storedIndex = k;
                    break;
                }
            }
            
            if (storedIndex == -1) {
                hasChanges = true;
                break;
            }

            // Vergleiche die Trays
            for (int j = 0; j < trayArray.size() && j < 4 && !hasChanges; j++) {
                JsonObject trayObj = trayArray[j];

                if (trayObj["setting_id"].isNull()) trayObj["setting_id"] = "";
                
                // Check if tray was empty and is now filled (for pending tray assignment)
                String oldTrayType = ams_data[storedIndex].trays[j].tray_type;
                String newTrayType = trayObj["tray_type"].as<String>();
                bool wasEmpty = (oldTrayType == "" || oldTrayType.length() == 0);
                bool isNowFilled = (newTrayType != "" && newTrayType.length() > 0);
                
                if (wasEmpty && isNowFilled && hasPendingTrayAssignment()) {
                    // Check if this is a Bambu spool (has valid tag_uid or tray_uuid)
                    String newTagUid = trayObj["tag_uid"].as<String>();
                    String newTrayUuid = trayObj["tray_uuid"].as<String>();
                    bool isBambuSpool = (newTagUid != "" && newTagUid != "0000000000000000") ||
                                        (newTrayUuid != "" && newTrayUuid != "00000000000000000000000000000000");
                    
                    Serial.printf("Tray filled detected: AMS %d Tray %d, type=%s, isBambu=%d\n", 
                        amsId, trayObj["id"].as<uint8_t>(), newTrayType.c_str(), isBambuSpool);
                    
                    checkTrayFilled(amsId, trayObj["id"].as<uint8_t>(), isBambuSpool);
                }
                
                if (trayObj["tray_info_idx"].as<String>() != ams_data[storedIndex].trays[j].tray_info_idx ||
                    trayObj["tray_type"].as<String>() != ams_data[storedIndex].trays[j].tray_type ||
                    trayObj["tray_color"].as<String>() != ams_data[storedIndex].trays[j].tray_color ||
                    trayObj["setting_id"].as<String>() != ams_data[storedIndex].trays[j].setting_id ||
                    trayObj["cali_idx"].as<String>() != ams_data[storedIndex].trays[j].cali_idx ||
                    trayObj["remain"].as<int>() != ams_data[storedIndex].trays[j].remain ||
                    trayObj["tray_uuid"].as<String>() != ams_data[storedIndex].trays[j].tray_uuid ||
                    trayObj["tag_uid"].as<String>() != ams_data[storedIndex].trays[j].tag_uid) {
                    hasChanges = true;

                    if (bambuCredentials.autosend_enable && autoSetToBambuSpoolId > 0 && hasChanges)
                    {
                        autoSetSpool(autoSetToBambuSpoolId, ams_data[storedIndex].trays[j].id);
                    }

                    break;
                }
            }
        }

        // Prüfe die externe Spule
        JsonObject vtTray = doc["print"]["vt_tray"];
        if (doc["print"]["vt_tray"].is<JsonObject>()) {
            for (int i = 0; i < ams_count; i++) {
                if (ams_data[i].ams_id == 255) {
                    if (vtTray["setting_id"].isNull()) vtTray["setting_id"] = "";
                    if (vtTray["tray_info_idx"].as<String>() != ams_data[i].trays[0].tray_info_idx ||
                        vtTray["tray_type"].as<String>() != ams_data[i].trays[0].tray_type ||
                        vtTray["tray_color"].as<String>() != ams_data[i].trays[0].tray_color ||
                        vtTray["setting_id"].as<String>() != ams_data[i].trays[0].setting_id ||
                        (vtTray["tray_type"].as<String>() != "" && vtTray["cali_idx"].as<String>() != ams_data[i].trays[0].cali_idx) ||
                        vtTray["remain"].as<int>() != ams_data[i].trays[0].remain ||
                        vtTray["tray_uuid"].as<String>() != ams_data[i].trays[0].tray_uuid ||
                        vtTray["tag_uid"].as<String>() != ams_data[i].trays[0].tag_uid) {
                        hasChanges = true;

                        if (bambuCredentials.autosend_enable && autoSetToBambuSpoolId > 0 && hasChanges)
                        {
                            autoSetSpool(autoSetToBambuSpoolId, 254);
                        }
                    }
                    break;
                }
            }
        }

        if (!hasChanges) return;

        updateAmsWsData(doc, amsArray, ams_count, vtTray);
    }
    
    // Neue Bedingung für ams_filament_setting
    if (doc["print"]["command"] == "ams_filament_setting") {
        int amsId = doc["print"]["ams_id"].as<int>();
        int trayId = doc["print"]["tray_id"].as<int>();
        String settingId = (doc["print"]["setting_id"].is<String>()) ? doc["print"]["setting_id"].as<String>() : "";

        // Finde das entsprechende AMS und Tray
        for (int i = 0; i < ams_count; i++) {
            if (ams_data[i].ams_id == amsId) {
                if (trayId == 254)
                {
                    // Suche AMS mit ID 255 (externe Spule)
                    for (int j = 0; j < ams_count; j++) {
                        if (ams_data[j].ams_id == 255) {
                            ams_data[j].trays[0].setting_id = settingId;
                            break;
                        }
                    }
                }
                else
                {
                    ams_data[i].trays[trayId].setting_id = settingId;
                }
               
                // Sende an WebSocket Clients
                Serial.println("Filament setting updated");
                sendAmsData(nullptr);
                break;
            }
        }
    }
}

// Returns true if connected, false if should stop trying
// Track consecutive restart failures for backoff
static unsigned long lastRestartAttempt = 0;
static uint8_t consecutiveRestartFailures = 0;

bool reconnect() {
    // Non-blocking reconnect with exponential backoff + jitter and richer logging
    uint8_t attempt = 0;

    // Make local copies of credentials to avoid String invalidation during reconnect
    String localSerial = bambuCredentials.serial;
    String localAccesscode = bambuCredentials.accesscode;
    String localIp = bambuCredentials.ip;

    if (localSerial.isEmpty() || localAccesscode.isEmpty() || localIp.isEmpty()) {
        Serial.println("Bambu credentials not set, cannot reconnect");
        bambu_connected = false;
        return false;
    }

    IPAddress serverIp;
    if (!serverIp.fromString(localIp)) {
        Serial.println("Invalid Bambu IP address in reconnect");
        bambu_connected = false;
        return false;
    }

    // Full cleanup of SSL/TCP stack before reconnecting
    Serial.printf("Reconnect cleanup: Stopping MQTT client (heap: %u)\n", ESP.getFreeHeap());
    if (client.connected()) client.disconnect();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Cleanup sockets via the MQTT adapter (async client manages TLS)
    Serial.println("Reconnect cleanup: disconnecting MQTT client and letting adapter reset sockets");
    if (client.connected()) client.disconnect();
    vTaskDelay(300 / portTICK_PERIOD_MS);

    Serial.printf("Reconnect cleanup: Complete (heap: %u)\n", ESP.getFreeHeap());

    client.setServer(serverIp, 8883);
    client.setSocketTimeout(15);  // 15 second socket timeout
    
    // CRITICAL: Set callback before attempting connection
    client.setCallback(mqtt_callback);

    // Try to connect with exponential backoff and jitter
    while (!client.connected()) {
        attempt++;

        // Respect WiFi
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected during MQTT reconnect, waiting...");
            // wait in small intervals and pet WDT
            for (int i = 0; i < 25 && WiFi.status() != WL_CONNECTED; ++i) {
                esp_task_wdt_reset();
                vTaskDelay(200 / portTICK_PERIOD_MS);
            }
            continue;
        }

        // Check heap
        if (ESP.getFreeHeap() < 30000) {
            Serial.printf("Low heap (%u) - waiting before reconnect\n", ESP.getFreeHeap());
            // wait and reset WDT periodically
            for (int i = 0; i < 20 && ESP.getFreeHeap() < 30000; ++i) {
                esp_task_wdt_reset();
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
            continue;
        }

        String clientId = localSerial + "_" + String(random(0, 1000));
        Serial.printf("Attempting MQTT connection (attempt %u) clientId=%s\n", attempt, clientId.c_str());
        bambu_connected = false;
        oledShowTopRow();

        if (client.connect(clientId.c_str(), BAMBU_USERNAME, localAccesscode.c_str())) {
            Serial.printf("MQTT connected on attempt %u\n", attempt);
            String reportTopic = "device/" + localSerial + "/report";
            String requestTopic = "device/" + localSerial + "/request";
            client.subscribe(reportTopic.c_str());
            client.subscribe(requestTopic.c_str());
            bambu_connected = true;
            consecutiveRestartFailures = 0;
            oledShowTopRow();
            return true;
        }

        int st = client.state();
        Serial.printf("MQTT connect failed (state=%d:%s)\n", st, mqttStateToString(st));
        bambu_connected = false;
        oledShowTopRow();

        // After several attempts escalate failure count and give up
        if (attempt >= 8) {
            Serial.println("Disable Bambu MQTT Task after repeated failures");
            consecutiveRestartFailures++;
            return false;
        }

        // Wait using exponential backoff with jitter
        unsigned long waitMs = backoffDelayMs(attempt, 3000, 300000);
        Serial.printf("Waiting %lu ms before next MQTT reconnect attempt\n", waitMs);
        unsigned long start = millis();
        while (millis() - start < waitMs) {
            // keep system responsive and pet WDT
            if (WiFi.status() != WL_CONNECTED) break;
            esp_task_wdt_reset();
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
    return true;
}

// Poll interval in milliseconds (60 seconds between polls)
#define MQTT_POLL_INTERVAL 60000
// How long to stay connected waiting for data (30 seconds max - Bambu can be slow)
#define MQTT_CONNECT_TIMEOUT 30000

void mqtt_loop(void * parameter) {
    Serial.println("Bambu MQTT Task gestartet (Poll Mode)");
    static unsigned long lastPollTime = 0;
    static unsigned long connectStartTime = 0;
    static bool waitingForData = false;
    static bool dataReceived = false;
    
    // If we're already connected (from setupMqtt), initialize the waiting state
    if (client.connected()) {
        connectStartTime = millis();
        waitingForData = true;
        dataReceived = false;
        Serial.println("MQTT Poll: Already connected from setup, waiting for data...");
    }
    
    for(;;) {
            if (pauseBambuMqttTask) {
                // If paused, disconnect to save resources
                if (client.connected()) {
                    Serial.println("MQTT: Pausing - disconnecting");
                    client.disconnect();
                }
                // Break long sleep into smaller chunks and pet the task WDT
                for (int i = 0; i < 20 && pauseBambuMqttTask; ++i) {
                    esp_task_wdt_reset();
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                }
                continue;
            }

        unsigned long now = millis();
        
        // Check if it's time to poll (or if we need initial data)
        bool shouldPoll = (ams_count == 0) || (now - lastPollTime >= MQTT_POLL_INTERVAL);
        
        if (!client.connected()) {
            // If we're not connected and should poll, connect
            if (shouldPoll) {
                Serial.printf("MQTT Poll: Connecting (heap: %u)...\n", ESP.getFreeHeap());
                
                bool shouldContinue = reconnect();
                if (!shouldContinue) {
                    Serial.println("Exiting MQTT task after failed reconnection attempts");
                    BambuMqttTask = NULL;
                    vTaskDelete(NULL);
                    return;
                }
                
                if (client.connected()) {
                    // Request data
                    JsonDocument doc;
                    doc["pushing"]["sequence_id"] = "0";
                    doc["pushing"]["command"] = "pushall";
                    doc["pushing"]["version"] = 1;
                    String output;
                    serializeJson(doc, output);
                    sendMqttMessage(output);
                    
                    connectStartTime = now;
                    waitingForData = true;
                    dataReceived = false;
                    Serial.println("MQTT Poll: Connected, waiting for data...");
                }
            } else {
                // Not time to poll yet, just wait
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
        }
        
        // We're connected, process messages
        if (client.connected()) {
            client.loop();
            
            // Check if we got AMS data (ams_count > 0 means we have data)
            if (waitingForData && ams_count > 0) {
                Serial.printf("MQTT Poll: Received AMS data (ams_count=%d), will disconnect\n", ams_count);
                dataReceived = true;
            }
            
            // Disconnect after receiving data OR timeout
            bool shouldDisconnect = false;
            
            if (dataReceived) {
                Serial.println("MQTT Poll: Data received, disconnecting to save resources");
                shouldDisconnect = true;
            } else if (now - connectStartTime > MQTT_CONNECT_TIMEOUT) {
                Serial.printf("MQTT Poll: Timeout waiting for data (ams_count=%d), disconnecting\n", ams_count);
                shouldDisconnect = true;
            }
            
            if (shouldDisconnect) {
                // Full cleanup to ensure clean reconnect
                Serial.printf("MQTT Poll: Disconnecting (state=%d:%s)\n", client.state(), mqttStateToString(client.state()));
                client.disconnect();
                vTaskDelay(100 / portTICK_PERIOD_MS);
                // Let the adapter manage TLS/socket cleanup; small pause to let resources free
                vTaskDelay(200 / portTICK_PERIOD_MS);
                
                waitingForData = false;
                lastPollTime = now;
                bambu_connected = false;
                oledShowTopRow();
                
                Serial.printf("MQTT Poll: Disconnected. Next poll in %d seconds. Free heap: %u\n", 
                    MQTT_POLL_INTERVAL / 1000, ESP.getFreeHeap());
                
                // Wait a bit before next iteration
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
        }

        yield();
        esp_task_wdt_reset();
        vTaskDelay(100);
    }
}

bool setupMqtt() {
    // Wenn Bambu Daten vorhanden
    //bool success = loadBambuCredentials();

    if (bambuCredentials.ip != "" && bambuCredentials.accesscode != "" && bambuCredentials.serial != "") 
    {
        if (amsDataMutex == NULL) {
            amsDataMutex = xSemaphoreCreateMutex();
        }

        oledShowProgressBar(4, 7, DISPLAY_BOOT_TEXT, "Bambu init");
        bambuDisabled = false;
        
        // Ensure any existing connection is cleanly disconnected
        if (client.connected()) client.disconnect();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        Serial.println("Using async MQTT adapter; TLS managed by adapter (verify certs if issues persist)");
        
        // Make local copies to ensure stability during connection
        String localIp = bambuCredentials.ip;
        String localSerial = bambuCredentials.serial;
        String localAccesscode = bambuCredentials.accesscode;
        
        // Convert IP to IPAddress for safer usage
        IPAddress serverIp;
        if (!serverIp.fromString(localIp)) {
            Serial.println("Invalid Bambu IP address");
            bambuDisabled = true;
            return false;
        }
        
        client.setServer(serverIp, 8883);
        client.setSocketTimeout(15);
        // Set conservative socket timeout on adapter
        client.setSocketTimeout(15);
        // Mirror previous behavior: disable cert validation (OpenSpool-style insecure TLS)
        client.setInsecure();
        
        // CRITICAL: Set callback BEFORE connecting so messages are handled immediately
        client.setCallback(mqtt_callback);

        // Verbinden mit dem MQTT-Server
        bool connected = true;
        String clientId = localSerial + "_" + String(random(0, 100));
        if (client.connect(clientId.c_str(), BAMBU_USERNAME, localAccesscode.c_str()))
        {
            // Try 48KB
            if (!client.setBufferSize(49152)) {
                Serial.println("Bambu: Failed to allocate 48KB MQTT buffer!");
                // Try 32KB (32768)
                if (!client.setBufferSize(32768)) {
                    Serial.println("Bambu: Failed to allocate 32KB MQTT buffer!");
                    // Try 25KB
                    if (!client.setBufferSize(25600)) {
                        Serial.println("Bambu: Failed to allocate 25KB MQTT buffer!");
                        // Fallback to 20KB
                        if (!client.setBufferSize(20480)) {
                            Serial.println("Bambu: Failed to allocate 20KB MQTT buffer!");
                        } else {
                            Serial.println("Bambu: 20KB MQTT buffer allocated.");
                        }
                    } else {
                        Serial.println("Bambu: 25KB MQTT buffer allocated.");
                    }
                } else {
                    Serial.println("Bambu: 32KB MQTT buffer allocated.");
                }
            } else {
                Serial.println("Bambu: 48KB MQTT buffer allocated.");
            }
            
            String reportTopic = "device/" + localSerial + "/report";
            String requestTopic = "device/" + localSerial + "/request";
            client.subscribe(reportTopic.c_str());
            client.subscribe(requestTopic.c_str());
            Serial.println("MQTT-Client initialisiert");

            // Request full data
            JsonDocument doc;
            doc["pushing"]["sequence_id"] = "0";
            doc["pushing"]["command"] = "pushall";
            doc["pushing"]["version"] = 1;
            String output;
            serializeJson(doc, output);
            sendMqttMessage(output);

            oledShowMessage("Bambu Connected");
            bambu_connected = true;
            oledShowTopRow();

            xTaskCreatePinnedToCore(
                mqtt_loop, /* Function to implement the task */
                "BambuMqtt", /* Name of the task */
                8192,  /* Stack size in words */
                NULL,  /* Task input parameter */
                mqttTaskPrio,  /* Priority of the task */
                &BambuMqttTask,  /* Task handle. */
                mqttTaskCore); /* Core where the task should run */
        } 
        else 
        {
            int st = client.state();
            Serial.printf("Error: Could not connect to MQTT server (rc=%d:%s)\n", st, mqttStateToString(st));
            oledShowMessage("Bambu Connection Failed");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            connected = false;
            oledShowTopRow();
            autoSetToBambuSpoolId = 0;
        }

        if (!connected) return false;
    } 
    else 
    {
        bambuDisabled = true;
        return false;
    }
    return true;
}

void bambu_restart() {
    Serial.println("Bambu restart");

    // Check for backoff - if we've failed multiple times, wait longer
    unsigned long now = millis();
    unsigned long backoffTime = 0;
    
    if (consecutiveRestartFailures > 0) {
        // Exponential backoff: 30s, 60s, 120s, max 5min
        backoffTime = min((unsigned long)(30000 * (1 << (consecutiveRestartFailures - 1))), (unsigned long)300000);
        
        if (now - lastRestartAttempt < backoffTime) {
            Serial.printf("Bambu restart backoff: waiting %lu more seconds (failures: %d)\n", 
                (backoffTime - (now - lastRestartAttempt)) / 1000, consecutiveRestartFailures);
            return;
        }
    }
    
    lastRestartAttempt = now;

    // First disconnect MQTT client cleanly
    if (client.connected()) {
        client.disconnect();
    }
    
    // Underlying TLS/sockets are cleaned up by the adapter on disconnect

    if (BambuMqttTask) {
        vTaskDelete(BambuMqttTask);
        BambuMqttTask = NULL;
        vTaskDelay(100 / portTICK_PERIOD_MS);  // Give time for task cleanup
    }
    
    // Reset connection state
    bambu_connected = false;
    
    // Longer delay to ensure clean state and let resources free
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Force garbage collection by yielding
    yield();
    
    Serial.printf("Bambu restart: Free heap before setupMqtt: %u\n", ESP.getFreeHeap());
    
    if (!setupMqtt()) {
        Serial.println("Bambu restart: setupMqtt failed");
        consecutiveRestartFailures++;
    }
}