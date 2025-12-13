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

  // Use explicit VSPI pins matching Filaman firmware
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
  // Verbose diagnostics each loop
  bool present = mfrc522.PICC_IsNewCardPresent();
  Serial.print("[LOOP] PICC_IsNewCardPresent()="); Serial.println(present);

  bool didRead = false;
  if (present) {
    didRead = mfrc522.PICC_ReadCardSerial();
    Serial.print("[LOOP] PICC_ReadCardSerial()="); Serial.println(didRead);
    Serial.print("[LOOP] uid.size="); Serial.println(mfrc522.uid.size);
    Serial.print("[LOOP] uidBytes=");
    for (byte i = 0; i < 10; i++) {
      // print up to 10 bytes from uid buffer (some data may be stale)
      if (i < mfrc522.uid.size) {
        if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        Serial.print(" ");
      } else {
        Serial.print(".. ");
      }
    }
    Serial.println();

    // Dump a few registers to help identify stale state
    byte regs[] = { mfrc522.VersionReg, mfrc522.CommandReg, mfrc522.ErrorReg, mfrc522.FIFOLevelReg };
    Serial.print("[LOOP] Regs: ");
    for (uint8_t r = 0; r < sizeof(regs); r++) {
      byte val = mfrc522.PCD_ReadRegister(regs[r]);
      if (val < 0x10) Serial.print("0");
      Serial.print(val, HEX); Serial.print(" ");
    }
    Serial.println();

    // If we successfully read a serial, halt to allow next tag
    if (didRead) {
      Serial.println("[LOOP] Halting detected tag");
      mfrc522.PICC_HaltA();
    } else {
      Serial.println("[LOOP] Read failed; clearing uid buffer");
      mfrc522.uid.size = 0;
    }
  }

  delay(200);
}
