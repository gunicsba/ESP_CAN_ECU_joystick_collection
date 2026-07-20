# Forwarder CAN Controller

ESP32-based CAN bus control system for a forwarder (logging machine) hydraulic valve block.
Replaces a failed factory controller with a robust, open-source solution using J1939-like addressing over 250 kbps CAN.

## Architecture

3 ECUs on a single 250 kbps CAN bus:

| ECU | Address | Role |
|-----|---------|------|
| Motor Driver | `0x20` | Controls 16 solenoids via 2x PCA9685 PWM drivers |
| Joystick 1 | `0x21` | Reads 3 pots + 2 buttons, publishes on CAN |
| Joystick 2 | `0x22` | Reads 3 pots + 2 buttons, publishes on CAN |

## Hardware

### T-CAN485 (LilyGO) - Joystick ECU

- **MCU**: ESP32 (regular, not S3) with built-in TJA1050 CAN transceiver
- **CAN pins**: TX=GPIO27, RX=GPIO26
- **Transceiver control**: GPIO16 (ME2107_EN, must be HIGH), GPIO23 (SPEED_MODE, LOW = high-speed)
- **Joystick inputs**: 3x potentiometers (GPIO32, 33, 34) + 2x buttons (GPIO12, 5)
- **Status LED**: WS2812B on GPIO18

### Motor Driver ECU (ESP32-S3)

- **MCU**: ESP32-S3 with USB-CDC serial
- **CAN transceiver**: TJA1050 on GPIO16 (TX), GPIO17 (RX)
- **Ethernet**: WT5500 module (SPI) for wired network connectivity
  - Static IP: 192.168.5.40 (configurable via web UI)
- **Motor driver PCB**: 2x PCA9685 I2C PWM controllers with 16 MOSFET outputs
- **Output active indicator**: GPIO4 goes HIGH when any output is active
- **Status LED**: WS2812B RGB LED

### Web Configuration Interface

The Motor Driver ECU includes a comprehensive web UI accessible via Ethernet:

**Motor Mapping Tab (Axis Configuration)**
- Configure up to 16 joystick axes with source address, pot index, and output channel
- Set PWM min/max values (0-255 scale)
- Enable bidirectional mode (uses channel pair for forward/reverse)
- Invert flag swaps forward/reverse channels
- Button gate: axis only active when specific button is pressed
- Exponential curve adjustment (1.0x to 3.0x)

**Virtual Joystick Assignment**
- Assign web UI joystick buttons to output pairs
- 8 pair-based outputs (Out 1 = ch0+ch1, Out 2 = ch2+ch3, etc.)
- Respects axis PWM Max settings from Motor Mapping
- Respects axis Invert flag for direction swapping

**Labels Tab**
- Custom labels for 8 pair-based outputs (Out 1 through Out 8)
- Joystick position labels (left, right, center, rear)
- Joystick source addresses (hexadecimal format)
- Custom axis labels (X, Y, Z, W)

**Dashboard Tab**
- Real-time output monitoring
- Joystick axis visualization with deadband indicators

**Motor Test Tab**
- Manual PWM output control for testing
- 10-second safety timeout

### NVS Versioning and OTA Safety

The firmware includes NVS versioning to prevent corruption during OTA updates:
- Magic number (0xA0F1) and version stored in NVS
- On boot, version is checked; if mismatch, NVS is cleared and defaults applied
- Increment `NVS_VERSION` in `ForwarderConfig.h` when configuration structures change

### Default Configuration (After NVS Clear)

When NVS is cleared (first boot or version mismatch):
- 8 axes enabled (0-7) with pair-based channels (0, 2, 4, 6, 8, 10, 12, 14)
- PWM Min = 20, PWM Max = 255, Bidirectional enabled
- Joystick labels: Joy1=left, Joy2=right, Joy3=center, Joy4=rear
- Joystick addresses: 0x21, 0x22, 0x23, 0x24

## CAN Protocol

- **Bitrate**: 250 kbps
- **Frame format**: 29-bit extended IDs, J1939-style layout

### ID Structure

```
Bits 28-26: Priority (3 bits)
Bit 25:     Extended Data Page (0)
Bit 24:     Data Page (0)
Bits 23-16: PDU Format (PF)
Bits 15-8:  PDU Specific (PS) = Destination Address when PF < 240
Bits 7-0:   Source Address (SA)
```

### Joystick ECU Transmitted Messages (25Hz)

All messages are broadcast (PS = `0xFF`) with priority 6.

| PF | Name | Rate | Data Bytes | Description |
|----|------|------|------------|-------------|
| `0x10` | `PF_JOYSTICK_POT1` | 25Hz | 2 | Potentiometer 1 value, little-endian (10-bit, 0-1023) |
| `0x11` | `PF_JOYSTICK_POT2` | 25Hz | 2 | Potentiometer 2 value, little-endian (10-bit, 0-1023) |
| `0x12` | `PF_JOYSTICK_POT3` | 25Hz | 2 | Potentiometer 3 value, little-endian (10-bit, 0-1023) |
| `0x13` | `PF_JOYSTICK_BUTTONS` | 25Hz | 1 | Button bitmask: bit0=BTN1, bit1=BTN2 |
| `0x30` | `PF_HEARTBEAT` | 1Hz | 8 | Status/heartbeat (see below) |

#### Potentiometer Data Format

```
Byte 0: value & 0xFF        (low byte)
Byte 1: (value >> 8) & 0xFF (high byte)
```

Example: value 512 → `data[0]=0x00, data[1]=0x02`

#### Button Data Format

```
Byte 0: bitmask
  bit 0: Button 1 (0x01) - active when pressed
  bit 1: Button 2 (0x02) - active when pressed
  bits 2-7: reserved (0)
```

Example: both buttons pressed → `data[0]=0x03`

#### Heartbeat Data Format (PF `0x30`)

```
Byte 0: online flag (0x01 = address claimed, 0x00 = not claimed)
Byte 1: uptime seconds (low byte)
Byte 2: uptime seconds (high byte)
Byte 3: ECU joystick ID (1 or 2)
Byte 4: RX message count (low byte)
Byte 5: TX message count (low byte)
Byte 6-7: reserved (0)
```

### Network Management Messages

| PF | PS | Direction | Description | Payload |
|----|----|-----------|-------------|---------|
| `0xEE` | `0xFF` | Broadcast | Address Claimed | 8-byte NAME (unique ECU identifier) |
| `0xEA` | `0xFF` | Broadcast | Request Address Claimed | - |

### Messages Received by Joystick ECU

| PF | PS | Source | Description | Payload |
|----|----|--------|-------------|---------|
| `0x20` | DA or `0xFF` | Any | Set LED Color | `R, G, B` (3 bytes) |
| `0x22` | DA or `0xFF` | Any | Identify (blink LED) | `duration` (1 byte, seconds) |

### Motor Driver Messages

| PF | PS | Direction | Description | Payload |
|----|----|-----------|-------------|--------|
| `0x21` | DA | Any -> Motor | Solenoid Command | `duty0..duty15` (16 bytes, 0-255 each) |

## Pinout (T-CAN485 Joystick)

| Signal | GPIO | Notes |
|--------|------|-------|
| CAN TX | 27 | Built-in TJA1050 transceiver |
| CAN RX | 26 | Built-in TJA1050 transceiver |
| ME2107_EN | 16 | Transceiver power enable (HIGH) |
| SPEED_MODE | 23 | TJA1050 speed mode (LOW = high-speed) |
| WS2812 | 4 | Status LED |
| Pot 1 | 32 | Joystick X (analog input) |
| Pot 2 | 33 | Joystick Y (analog input) |
| Pot 3 | 34 | Joystick Z (analog input) |
| Button 1 | 12 | Active low, internal pullup |
| Button 2 | 5 | Active low, internal pullup |

## Pinout (Motor Driver ECU - ESP32-S3)

| Signal | GPIO | Notes |
|--------|------|-------|
| CAN TX | 16 | TJA1050 transceiver |
| CAN RX | 17 | TJA1050 transceiver |
| I2C SDA | 8 | PCA9685 controllers |
| I2C SCL | 9 | PCA9685 controllers |
| Output Active | 4 | HIGH when any output is active |
| WS2812 | 38 | Status LED |
| WT5500 MISO | 13 | Ethernet SPI |
| WT5500 MOSI | 11 | Ethernet SPI |
| WT5500 SCLK | 12 | Ethernet SPI |
| WT5500 CS | 10 | Ethernet SPI |
| WT5500 RST | 7 | Ethernet reset |
| WT5500 INT | 5 | Ethernet interrupt |

## Joystick ECU LED Status Indicators

The WS2812B RGB LED on the joystick ECU provides visual status feedback:

| LED Pattern | Meaning | Condition |
|-------------|---------|----------|
| **Solid GREEN** (full brightness) | Normal operation | CAN bus online, motor ECU responding |
| **Solid RED** | Motor ECU offline | No CAN messages from motor ECU (`0x20`) for 1+ second |
| **Blinking RED** (500ms) | CAN bus broken | No CAN messages from other devices for 2+ seconds |
| **Flashing WHITE** | Identify mode | `PF_IDENTIFY` message received, lasts 3 seconds |
| **Custom RGB** | Remote command | `PF_LED_COLOR` message overrides default color |

### Detection Logic

**Priority order** (highest to lowest):
1. Identify (white flash, 3 seconds)
2. CAN bus broken (blinking red) — message timeout + TWAI hardware check
3. Motor ECU offline (solid red) — message timeout
4. Custom LED color from `PF_LED_COLOR` command
5. Default green (0, 255, 0)

**CAN Bus Broken** is detected when any of:
- No CAN messages from **other devices** for 2+ seconds (own messages filtered to avoid loopback false negatives in `TWAI_MODE_NO_ACK`)
- `TWAI_STATE_STOPPED` — TWAI driver stopped
- `TWAI_STATE_BUS_OFF` — Bus-off due to errors
- `tx_error_counter >= 127` — High transmit error count

> **Note:** In `TWAI_MODE_NO_ACK`, the ESP32 may receive its own transmitted messages (loopback). The detection filters these out by checking `sa != g_can->getAddress()` so that a disconnected CAN cable is properly detected.

**Motor ECU Offline** is detected when:
- No CAN messages received from source address `0x20` for more than 1 second
- Only triggers if CAN bus is NOT broken (bus check has priority)

## Building & Flashing

Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).

### Finding COM Ports

To list available COM ports on Windows, open Command Prompt and run:
```cmd
mode
```
This will show all available serial ports (e.g., COM7).

### Building

```bash
# Build motor driver (ESP32-S3 with OTA web UI)
pio run -e motor_driver_s3_ota

# Build motor driver (legacy ESP32)
pio run -e motor_driver

# Build joystick 1
pio run -e joystick1

# Build joystick 2
pio run -e joystick2
```

### Flashing with PlatformIO

```bash
# Flash motor driver S3 (USB-CDC)
pio run -e motor_driver_s3_ota --target upload

# Flash joystick 1
pio run -e joystick1 --target upload
```

### Flashing with esptool

For direct flashing without PlatformIO, use the included `esptool.exe`. The ESP32-S3 requires multiple files at specific flash offsets:

**Full flash (required when switching from different firmware):**

```bash
# First erase flash
esptool.exe --chip esp32s3 --port COM7 erase_flash

# Then flash all components at correct offsets
esptool.exe --chip esp32s3 --port COM7 --baud 115200 write_flash -z ^
  0x0 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin
```

**Application-only flash (for updates on same firmware family):**

```bash
# Only update the application (requires existing compatible bootloader)
esptool.exe --chip esp32s3 --port COM7 --baud 115200 write_flash -z 0x10000 firmware.bin
```

**Flash offsets explained:**
| Offset | File | Description |
|--------|------|-------------|
| `0x0` | `bootloader.bin` | ESP32-S3 bootloader |
| `0x8000` | `partitions.bin` | Partition table |
| `0x10000` | `firmware.bin` | Application firmware |

**Parameters:**
- `--chip esp32s3` - Target chip type
- `--port COM7` - Serial port (use `mode` command to find available ports)
- `--baud 115200` - Upload speed (use `460800` or `921600` for faster flashing)
- `write_flash -z` - Write compressed

**If the board is crash-looping** (e.g., Guru Meditation Error), you must manually enter download mode:
1. Hold the BOOT button
2. Press and release RESET while holding BOOT
3. Release BOOT
4. Run the esptool command above

**Note:** All binary files are included in the GitHub release. Use the files from the `motor_driver_s3_ota` build.

### OTA Updates

The Motor Driver ECU supports OTA updates via the web interface:

1. Access the web UI at http://192.168.5.40 (or configured IP) via AgIo set subnet PGN
2. Navigate to the "OTA Update" tab
3. Upload the `firmware.bin` file from `.pio/build/motor_driver_s3_ota/`

The firmware includes NVS versioning - if the NVS version doesn't match, configuration is reset to defaults.

**Note:** Always run `clang-format` on modified C++ files before building.

### Wi-Fi OTA (Joystick ECUs)

Joystick ECUs can include Wi-Fi OTA support:

```bash
# Build & upload joystick 1 with OTA support
pio run -e joystick1_ota --target upload
```

After booting:
1. Connect to the AP named `forwarder-joy1-21` (or `forwarder-joy2-22`)
2. Password: `12345678`
3. Open http://192.168.4.1
4. Upload a `.bin` firmware file

## Safety Features

- **Address claiming**: J1939-style startup arbitration ensures no address collisions
- **Solenoid timeout**: Motor driver shuts off all solenoids if no CAN command received within 500 ms
- **Bus-off recovery**: Automatic TWAI recovery on CAN errors
- **Heartbeat**: All ECUs broadcast status every 1 second

## Project Structure

```
ESP_CAN_ECU_joystick_collection/
├── lib/
│   ├── ForwarderCAN/         # Shared CAN/J1939 library
│   └── ForwarderConfig/      # NVS configuration management with versioning
├── src/
│   ├── main.cpp              # Entry point (build flag selects ECU type)
│   ├── ecu_motor_driver.cpp  # Motor driver logic (ESP32-S3)
│   ├── ecu_joystick.cpp      # Joystick logic
│   ├── ota_webserver.cpp     # Web UI and OTA update server
│   ├── can_output.cpp        # CAN output rules processing
│   ├── web_state.cpp         # Real-time state management
│   └── *.h                   # Headers
├── 3D/                       # 3D models for joystick enclosure
├── img/                      # Documentation images
├── platformio.ini            # Build environments
└── README.md                 # This file
```

## License

MIT
