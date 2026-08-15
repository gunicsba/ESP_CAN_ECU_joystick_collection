# Joystick Full Documentation

## Overview

Unified joystick firmware for the ESP32S3R8N8 CAN Board V1.0.0 that exposes ALL inputs and outputs.

## Hardware Configuration

### Common Hardware
- **MCU**: ESP32-S3 (ESP32S3R8N8)
- **I2C Bus**: SDA=GPIO39, SCL=GPIO38
- **CAN Bus**: TX=GPIO10, RX=GPIO11
- **ADS1115 ADC**: I2C address 0x48 (4 channels)
- **Main PCA9555**: I2C address 0x20 (8 button inputs)
- **Extender PCA9555**: I2C address 0x21 (16 pins: 8 inputs + 8 outputs)

---

## Features

### Analog Inputs (4 channels)
All 4 ADS1115 channels are exposed:

| Channel | CAN PF | Description |
|---------|--------|-------------|
| 0 | `0x10` | Pot 1 (X axis) |
| 1 | `0x11` | Pot 2 (Y axis) |
| 2 | `0x12` | Pot 3 |
| 3 | `0x14` | Pot 4 |

### Button Inputs (24 total)

#### Main PCA9555 @ 0x20 (8 buttons)
Sent via `PF_JOYSTICK_BUTTONS` (0x13):

| Bit | Pin | Description |
|-----|-----|-------------|
| 0 | P0_0 | Button 1 |
| 1 | P0_1 | Button 2 |
| 2 | P0_2 | Button 3 |
| 3 | P0_3 | Button 4 |
| 4 | P0_4 | Button 5 |
| 5 | P0_5 | Button 6 |
| 6 | P0_6 | Button 7 |
| 7 | P0_7 | Button 8 |

#### Extender PCA9555 @ 0x21 (16 pins)
Port 0 (8 buttons) sent via `PF_EXTENDER_BUTTONS` (0x15):

| Bit | Pin | Description |
|-----|-----|-------------|
| 0 | P0_0 | Ext Button 1 |
| 1 | P0_1 | Ext Button 2 |
| 2 | P0_2 | Ext Button 3 |
| 3 | P0_3 | Ext Button 4 |
| 4 | P0_4 | Ext Button 5 |
| 5 | P0_5 | Ext Button 6 |
| 6 | P0_6 | Ext Button 7 |
| 7 | P0_7 | Ext Button 8 |

Port 1 (8 buttons) sent via `PF_EXTENDER_BUTTONS2` (0x16):

| Bit | Pin | Description |
|-----|-----|-------------|
| 0 | P1_0 | Ext Button 9 |
| 1 | P1_1 | Ext Button 10 |
| 2 | P1_2 | Ext Button 11 |
| 3 | P1_3 | Ext Button 12 |
| 4 | P1_4 | Ext Button 13 |
| 5 | P1_5 | Ext Button 14 |
| 6 | P1_6 | Ext Button 15 |
| 7 | P1_7 | Ext Button 16 |

### Optocoupler Output

The optocoupler is controlled by **ESP32 GPIO18** (not the extender PCA9555).

**Control via CAN:**
- Send PF `0x40` with data[0] = 1 to activate
- Send PF `0x40` with data[0] = 0 to deactivate

**Status:**
- Heartbeat byte 6 indicates opto state (0x01 = active)

---

## CAN Protocol

### Message Format (J1939-style)

```
CAN ID: 0x00FFxx80
  xx = PF (Parameter Group Number)
  80 = Source Address (this joystick)
```

### PF Codes

| PF | Name | Data | Direction |
|----|------|------|-----------|
| `0x10` | `PF_JOYSTICK_POT1` | 2 bytes (16-bit) | TX |
| `0x11` | `PF_JOYSTICK_POT2` | 2 bytes (16-bit) | TX |
| `0x12` | `PF_JOYSTICK_POT3` | 2 bytes (16-bit) | TX |
| `0x14` | `PF_JOYSTICK_POT4` | 2 bytes (16-bit) | TX |
| `0x13` | `PF_JOYSTICK_BUTTONS` | 1 byte (8 bits) | TX |
| `0x15` | `PF_EXTENDER_BUTTONS` | 1 byte (8 bits) | TX |
| `0x16` | `PF_EXTENDER_BUTTONS2` | 1 byte (8 bits) | TX |
| `0x30` | `PF_HEARTBEAT` | 8 bytes | TX |
| `0x40` | Optocoupler control | 1 byte | RX |

### Heartbeat Format

| Byte | Content |
|------|---------|
| 0 | Online status (0x01 = online) |
| 1-2 | Uptime (seconds, 16-bit) |
| 3 | ECU address (0x80) |
| 4 | RX count (low byte) |
| 5 | TX count (low byte) |
| 6 | Optocoupler status (0x01 = active) |
| 7 | Reserved (0) |

---

## Build and Flash

### Build
```bash
pio run -e joystick_full
```

### Flash (manual download mode required)
1. Hold BOOT button
2. Press and release RESET button
3. Release BOOT button
4. Flash:
```bash
esptool.exe --chip esp32s3 --port COM5 --baud 460800 write_flash -z 0x0000 .pio/build/joystick_full/bootloader.bin 0x8000 .pio/build/joystick_full/partitions.bin 0x10000 .pio/build/joystick_full/firmware.bin
```

### Serial Monitor
After flashing, press RESET on the board. Output shows:
```
J: pot=12345,12456,0,8000 btn=00 ext=00,00 opto=0
```

---

## Testing

Use `newpcb_test` environment to verify hardware before flashing:
```bash
pio run -e newpcb_test
```

This will show:
- I2C device scan
- ADS1115 readings (all 4 channels)
- PCA9555 raw pin states
- Button pair states (for 0x21 extender)

---

## Pin Mapping Summary

### Button Pairs (for reference)
The extender buttons can be paired for dual-action control:

| Pair | Pin A | Pin B |
|------|-------|-------|
| 1 | P0_7 (opto) | P0_4 |
| 2 | P0_3 | P0_5 |
| 3 | P0_1 | P0_2 |
| 4 | P1_0 | P1_1 |
| 5 | P1_2 | P1_3 |
| 6 | P1_4 | P1_5 |

---

## Future Ideas / TODO

### LED Backlighting
The joystick PCB should have LEDs to light up in dark conditions. Potential implementation:
- Use the extender PCA9555 Port 1 outputs to drive LEDs
- Or use dedicated ESP32 GPIO pins (e.g., GPIO7/8/9 from H7 connector)
- Could be controlled via CAN messages for brightness/RGB control
- Consider PWM dimming for adjustable brightness
