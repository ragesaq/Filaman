# RC522 RFID Configuration Guide

## Hardware Wiring

Connect your RC522 RFID module to the ESP32 using the following pins:

| RC522 Pin | ESP32 GPIO | Purpose |
|-----------|-----------|---------|
| SDA (SS)  | GPIO5     | SPI Chip Select |
| SCK (CLK) | GPIO18    | SPI Clock |
| MOSI      | GPIO23    | SPI Master Out, Slave In |
| MISO      | GPIO19    | SPI Master In, Slave Out |
| RST       | GPIO22    | Reset (not used - soft reset via firmware) |
| GND       | GND       | Ground |
| 3.3V      | 3.3V      | Power (use capacitor 100nF across power pins) |

## Firmware Configuration

### Enable RC522 Mode

In `platformio.ini`, build with the RC522 environment:

```ini
[env:esp32dev-rc522]
extends = env:esp32dev
build_flags = 
    ${env:esp32dev.build_flags}
    -DUSE_RC522
```

Or use the test environment:
```ini
[env:esp32dev-rc522-test]
extends = env:esp32dev-rc522
...
```

### Build & Upload

```bash
# Test RC522 initialization
pio run -e esp32dev-rc522-test -t upload

# Production build
pio run -e esp32dev-rc522 -t upload
```

## Testing

### Test Program (test_mfrc.cpp)

The test program runs the RC522 through several diagnostic checks:

1. **SPI Communication Test**: Verifies VersionReg reads correctly
2. **Antenna OFF/ON Cycle**: Tests antenna control and RF field
3. **Register Diagnostics**: Snapshots key registers for troubleshooting

Expected output:
```
[RC522 TEST] Initializing SPI bus...
[RC522 TEST] Calling PCD_Init (software reset)...
[RC522 TEST] Setting antenna gain to 43dB...
[RC522 TEST] Antenna OFF - Reading VersionReg (expect 0xB2)...
v=0xB2 v=0xB2 v=0xB2 ...
[RC522 TEST] Antenna ON (43dB) - Reading VersionReg...
v=0xB2 v=0xB2 v=0xB2 ...
[RC522 TEST] Final VersionReg: 0x92
SUCCESS: RC522 detected and responding
```

### Troubleshooting

#### Problem: Reading 0xFF or 0x00 from VersionReg

**Likely Causes:**
- SPI wiring is incorrect
- RC522 board is not powered
- Power supply is unstable (add 100nF capacitor across 3.3V and GND near RC522)
- ESP32 GPIO pins are not correctly initialized

**Solution:**
1. Verify all SPI connections (CLK, MISO, MOSI, SS)
2. Check power supply voltage is stable 3.3V
3. Add decoupling capacitor (100nF) close to RC522 power pins
4. Try the test program to get detailed diagnostics
5. Check for GPIO conflicts with other peripherals

#### Problem: Card detection not working

**Check:**
1. Antenna is ON (firmware does this automatically)
2. RF field is present (test program verifies this)
3. Card is within antenna range (~5-10 cm)
4. Card is NTAG216 format (for Bambu RFID data)

## Initialization Details

### SPI Bus Configuration

```cpp
SPI.begin(18, 19, 23, SS_PIN);  // CLK, MISO, MOSI, SS
delay(100);  // Allow SPI to stabilize
```

### RC522 Module Initialization

```cpp
mfrc522.PCD_Init();  // Software reset - no manual RST toggle needed
delay(100);
mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_43dB);
mfrc522.PCD_AntennaOn();
```

### Register Verification

After initialization, read VersionReg to confirm communication:

```cpp
byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
// Expected value: 0x92 (after antenna operations)
// Before antenna ON: 0xB2
```

## Reference Implementation

This code follows the best practices from **AMSPlusCore**:
- Soft-reset via `PCD_Init()` (no manual RST pin toggle)
- Single SPI bus with multiple CS pins for scaling
- Antenna power management for field stability
- Event-driven RFID detection

## MQTT Integration (Production)

When RC522 is working correctly:
1. Start the main firmware: `pio run -e esp32dev-rc522 -t upload`
2. Configure MQTT in web interface
3. RFID tags will be read and sent via MQTT
4. AMS filament data will sync automatically

## Firmware Build Flags

When compiling with RC522 enabled:
```
-DUSE_RC522           # Enable RC522 mode
-DMQTT_MAX_PACKET_SIZE=32768  # Buffer for large MQTT messages
```

See [platformio.ini](platformio.ini) for all build configurations.
