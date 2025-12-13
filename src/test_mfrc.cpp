#ifdef TEST_MFRC522
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SSLClient.h>
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 5
// Use a dummy pin for RST to force Soft Reset only (mimic AMSPlusCore)
#define RST_PIN 22 // Using an unused pin or just letting the library handle it if we pass a dummy
#define BUTTON_PIN 27

MFRC522 mfrc522(SS_PIN, RST_PIN);

#ifndef TEST_WIFI_SSID
#define TEST_WIFI_SSID ""
#endif

#ifndef TEST_WIFI_PASSWORD
#define TEST_WIFI_PASSWORD ""
#endif

#ifndef TEST_MQTT_SERVER
#define TEST_MQTT_SERVER ""
#endif

#ifndef TEST_MQTT_PORT
#define TEST_MQTT_PORT 1883
#endif

#ifndef TEST_MQTT_CLIENT_ID
#define TEST_MQTT_CLIENT_ID "filaman-test"
#endif

#ifndef TEST_MQTT_TOPIC
#define TEST_MQTT_TOPIC "filaman/test"
#endif

#ifndef TEST_MQTT_REQUEST_TOPIC
#define TEST_MQTT_REQUEST_TOPIC ""
#endif

#ifndef TEST_MQTT_USERNAME
#define TEST_MQTT_USERNAME ""
#endif

#ifndef TEST_MQTT_PASSWORD
#define TEST_MQTT_PASSWORD ""
#endif

constexpr bool kTestWifiConfigured = sizeof(TEST_WIFI_SSID) > 1 && sizeof(TEST_WIFI_PASSWORD) > 1;
constexpr bool kTestMqttConfigured = sizeof(TEST_MQTT_SERVER) > 1 && sizeof(TEST_MQTT_TOPIC) > 1;
constexpr bool kTestMqttAuthProvided = kTestMqttConfigured && (sizeof(TEST_MQTT_USERNAME) > 1 && sizeof(TEST_MQTT_PASSWORD) > 1);

WiFiClient wifiClient;
SSLClient sslClient(&wifiClient);
PubSubClient mqttClient(sslClient);
bool wifiConnected = false;
bool mqttConnected = false;

// Sweep configuration: sample window per candidate (ms)
#ifndef SWEEP_SAMPLE_MS
#define SWEEP_SAMPLE_MS 500
#endif

static uint8_t clamp8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Received topic ");
  Serial.print(topic);
  Serial.print(" -> length: ");
  Serial.println(length);
  
  // Print payload in chunks to avoid buffer issues, though Serial is stream
  for (unsigned int i = 0; i < length; ++i) {
    Serial.write(payload[i]);
  }
  Serial.println();
}

bool connectToWifi() {
  Serial.printf("[MQTT] Connecting to Wi-Fi '%s'...", TEST_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print('.');
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[MQTT] Failed to connect to Wi-Fi");
    return false;
  }

  Serial.print("\n[MQTT] Wi-Fi connected, IP= ");
  Serial.println(WiFi.localIP());
  return true;
}

bool connectToMqtt() {
  sslClient.setInsecure();
  mqttClient.setServer(TEST_MQTT_SERVER, TEST_MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(40960); // Increase buffer size for large Bambu payloads (40KB)

  unsigned long start = millis();
  while (!mqttClient.connected() && millis() - start < 15000) {
    Serial.printf("[MQTT] Connecting to %s:%d... ", TEST_MQTT_SERVER, TEST_MQTT_PORT);
    bool connected;
    if (kTestMqttAuthProvided) {
      connected = mqttClient.connect(TEST_MQTT_CLIENT_ID, TEST_MQTT_USERNAME, TEST_MQTT_PASSWORD);
    } else {
      connected = mqttClient.connect(TEST_MQTT_CLIENT_ID);
    }
    if (connected) {
      Serial.println("connected");
      break;
    }
    Serial.printf("failed (rc=%d)\n", mqttClient.state());
    delay(1000);
  }

  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Connection attempt timed out");
    return false;
  }

  mqttClient.subscribe(TEST_MQTT_TOPIC);
  mqttClient.publish(TEST_MQTT_TOPIC, "{\"status\":\"connected\"}");
  Serial.println("[MQTT] Subscribed/published test payload");

  return true;
}

unsigned long lastPushTime = 0;

// Manual RF sweep routine (call from serial command)
void runRfSweep() {
  Serial.println("Manual RF sweep: starting...");
  const MFRC522::PCD_Register regs[] = { mfrc522.TxControlReg, mfrc522.TxASKReg, mfrc522.RFCfgReg, mfrc522.ModWidthReg };
  const char* regNames[] = { "TxControlReg", "TxASKReg", "RFCfgReg", "ModWidthReg" };
  const int offsets[] = { -32, -16, -8, -4, 0, 4, 8, 16, 32 };

  const size_t regCount = sizeof(regs) / sizeof(regs[0]);
  uint8_t orig[regCount];
  for (size_t i = 0; i < regCount; ++i) orig[i] = mfrc522.PCD_ReadRegister(regs[i]);

  for (size_t i = 0; i < regCount; ++i) {
    Serial.print("Sweep: "); Serial.print(regNames[i]); Serial.print(" orig=0x"); Serial.println(orig[i], HEX);
    for (size_t o = 0; o < sizeof(offsets)/sizeof(offsets[0]); ++o) {
      uint8_t val = clamp8((int)orig[i] + offsets[o]);
      mfrc522.PCD_WriteRegister(regs[i], val);
      delay(20);

      unsigned long end = millis() + SWEEP_SAMPLE_MS;
      int tries = 0, succ = 0;
      while (millis() < end) {
        ++tries;
        if (mfrc522.PICC_IsNewCardPresent()) {
          if (mfrc522.PICC_ReadCardSerial()) {
            ++succ;
            mfrc522.PICC_HaltA();
            delay(30);
          }
        }
        delay(30);
      }
      Serial.print("  val=0x"); Serial.print(val, HEX);
      Serial.print(" offs="); Serial.print(offsets[o]);
      Serial.print(" succ="); Serial.print(succ);
      Serial.print("/tries="); Serial.println(tries);
    }
    mfrc522.PCD_WriteRegister(regs[i], orig[i]);
    delay(50);
  }
  Serial.println("Manual RF sweep: done; restored registers.");
}

void setup() {
  Serial.begin(921600);
  while (!Serial) { delay(10); }
  
  Serial.println("Starting MFRC522 test immediately...");
  delay(2000); 

  if (kTestWifiConfigured) {
    wifiConnected = connectToWifi();
  } else {
    Serial.println("[MQTT] Wi-Fi credentials not defined; skipping network checks.");
  }

  if (kTestMqttConfigured) {
    if (wifiConnected) {
      mqttConnected = connectToMqtt();
    } else {
      Serial.println("[MQTT] Skipping MQTT because Wi-Fi is unavailable");
    }
  } else {
    Serial.println("[MQTT] MQTT broker not configured for this test");
  }

  // Initialize SPI with pins matching the board wiring
  SPI.begin(18, 19, 23, SS_PIN);
  
  // AMSPlusCore uses Soft Reset. We skip manual RST toggle here.
  // pinMode(RST_PIN, OUTPUT);
  // digitalWrite(RST_PIN, HIGH);
  // delay(50);

  mfrc522.PCD_Init();
  delay(100);

  // --- DIAGNOSTIC: Antenna Power Impact Test ---
  // 1. Turn Antenna OFF and check stability
  mfrc522.PCD_AntennaOff();
  Serial.println("TEST: Antenna OFF. Reading VersionReg (expect 0xB2)...");
  for(int i=0; i<10; i++) {
     byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
     Serial.print("v="); Serial.print(v, HEX); Serial.print(" ");
     delay(100);
  }
  Serial.println();

  // 2. Turn Antenna ON (Max Gain) and check stability
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_43dB);
  mfrc522.PCD_AntennaOn();
  Serial.println("TEST: Antenna ON (43dB). Reading VersionReg...");
  for(int i=0; i<10; i++) {
     byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
     Serial.print("v="); Serial.print(v, HEX); Serial.print(" ");
     delay(100);
  }
  Serial.println();
  // ---------------------------------------------

  Serial.println("Applied AMSPlusCore settings: Soft Reset & Gain (43dB)");

  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print("Test RC522 Version: 0x"); Serial.println(v, HEX);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("WARNING: RC522 appears not present or not responding");
  }

  mfrc522.PCD_AntennaOn();
  // Snapshot important registers for diagnosis (pre-sweep baseline)
  {
    const MFRC522::PCD_Register regs_snapshot[] = {
      mfrc522.VersionReg,
      mfrc522.TxControlReg,
      mfrc522.TxASKReg,
      mfrc522.ModWidthReg,
      mfrc522.RFCfgReg,
      mfrc522.TxModeReg,
      mfrc522.RxModeReg,
      mfrc522.ModeReg,
      mfrc522.BitFramingReg,
      mfrc522.CollReg,
      mfrc522.ErrorReg,
      mfrc522.FIFOLevelReg,
      mfrc522.ControlReg
    };
    const char* names[] = {
      "VersionReg",
      "TxControlReg",
      "TxASKReg",
      "ModWidthReg",
      "RFCfgReg",
      "TxModeReg",
      "RxModeReg",
      "ModeReg",
      "BitFramingReg",
      "CollReg",
      "ErrorReg",
      "FIFOLevelReg",
      "ControlReg"
    };
    Serial.println("MFRC522 Register Snapshot (pre-sweep):");
    const size_t n = sizeof(regs_snapshot)/sizeof(regs_snapshot[0]);
    for (size_t i = 0; i < n; ++i) {
      uint8_t rv = mfrc522.PCD_ReadRegister(regs_snapshot[i]);
      Serial.print("  "); Serial.print(names[i]); Serial.print(" = 0x"); Serial.println(rv, HEX);
    }
  }
  Serial.println("Serial: send 's' to run manual RF sweep");
}

void loop() {
  // Poll for new cards
  // Explicitly flush FIFO before starting a new cycle
  mfrc522.PCD_WriteRegister(mfrc522.FIFOLevelReg, 0x80);
  
  if (mfrc522.PICC_IsNewCardPresent()) {
    // Clear UID struct manually
    memset(&mfrc522.uid, 0, sizeof(mfrc522.uid));
    
    if (mfrc522.PICC_ReadCardSerial()) {
      Serial.print("UID: ");
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        Serial.print(" ");
      }
      Serial.print(" | SAK: 0x");
      Serial.println(mfrc522.uid.sak, HEX);
      
      // Force stop crypto and halt
      mfrc522.PCD_StopCrypto1();
      mfrc522.PICC_HaltA();
      
      // Brute Force: Turn antenna off/on to force field reset
      // This ensures the card loses power and resets its state, preventing "Sticky UID"
      mfrc522.PCD_AntennaOff();
      delay(50);
      mfrc522.PCD_AntennaOn();
      
      delay(500);
    }
  }
  delay(50);

  if (kTestWifiConfigured && WiFi.status() != WL_CONNECTED) {
    wifiConnected = connectToWifi();
  }

  if (kTestMqttConfigured && wifiConnected) {
    if (!mqttConnected) {
      mqttConnected = connectToMqtt();
    } else {
      mqttClient.loop();
      if (millis() - lastPushTime > 10000) {
        lastPushTime = millis();
        if (sizeof(TEST_MQTT_REQUEST_TOPIC) > 1) {
            const char* pushAllCmd = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\",\"version\":1}}";
            if (mqttClient.publish(TEST_MQTT_REQUEST_TOPIC, pushAllCmd)) {
                Serial.printf("[MQTT] Sent pushall command to %s\n", TEST_MQTT_REQUEST_TOPIC);
            } else {
                Serial.printf("[MQTT] Failed to send pushall command to %s\n", TEST_MQTT_REQUEST_TOPIC);
            }
        }
      }
    }
  }
}
#endif
