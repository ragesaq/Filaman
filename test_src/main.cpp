#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 5
#define RST_PIN 15

MFRC522 mfrc522(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("MFRC522 minimal test starting...");

  // Initialize SPI with pins matching the board wiring
  SPI.begin(18, 19, 23, SS_PIN);
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, HIGH);
  delay(50);

  mfrc522.PCD_Init();
  delay(100);

  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print("Test RC522 Version: 0x"); Serial.println(v, HEX);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("WARNING: RC522 appears not present or not responding");
  }

  mfrc522.PCD_AntennaOn();
}

void loop() {
  // Poll for new cards
  if (mfrc522.PICC_IsNewCardPresent()) {
    if (mfrc522.PICC_ReadCardSerial()) {
      Serial.print("UID: ");
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
      mfrc522.PICC_HaltA();
      delay(500);
    }
  } else {
    static unsigned long lastDiag = 0;
    if (millis() - lastDiag > 1000) {
      byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
      Serial.print("[DIAG] VersionReg=0x"); Serial.println(v, HEX);
      lastDiag = millis();
    }
  }
  delay(50);
}
