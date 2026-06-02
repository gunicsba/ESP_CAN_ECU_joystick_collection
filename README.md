# Forwarder CAN Controller

ESP32-S3 based CAN bus control system for a forwarder (logging machine) hydraulic valve block.
Replaces a failed factory controller with a robust, open-source solution using J1939-like addressing over 250 kbps CAN.

## Architecture

3 ECUs on a single 250 kbps CAN bus:

| ECU | Address | Role |
|-----|---------|------|
| Motor Driver | `0x20` | Controls 8 solenoids via PCA9685 PWM driver |
| Joystick 1 | `0x21` | Reads 3 pots + 2 buttons, publishes on CAN |
| Joystick 2 | `0x22` | Reads 3 pots + 2 buttons, publishes on CAN |

## Hardware

- **MCU**: LilyGO T-CAN board (ESP32-S3 with built-in CAN transceiver)
- **Motor driver PCB**: PCA9685 I2C PWM controller with 8 MOSFET outputs
- **Joysticks**: 3 potentiometers + 2 buttons each

## CAN Protocol

29-bit extended IDs, J1939-style layout:
```
Priority(3) | DP(1) | PF(8) | PS/DA(8) | SA(8)
```

### Message Definitions

| PF | PS | Direction | Description | Payload |
|----|----|-----------|-------------|---------|
| `0xEE` | `0xFF` | Broadcast | Address Claimed | 8-byte NAME |
| `0xEA` | `0xFF` | Broadcast | Request Address Claimed | - |
| `0x10` | `0xFF` | Joystick -> All | Joystick Pot 1 | `value_lo, value_hi` (10-bit) |
| `0x11` | `0xFF` | Joystick -> All | Joystick Pot 2 | `value_lo, value_hi` (10-bit) |
| `0x12` | `0xFF` | Joystick -> All | Joystick Pot 3 | `value_lo, value_hi` (10-bit) |
| `0x13` | `0xFF` | Joystick -> All | Joystick Buttons | `0x01=btn1, 0x02=btn2` |
| `0x20` | `DA` | Any -> ECU | Set LED Color | `R, G, B` |
| `0x21` | `DA` | Any -> Motor | Solenoid Command | `duty0..duty7` (0-255, scaled to 12-bit) |
| `0x30` | `0xFF` | Any -> All | Heartbeat / Status | uptime, counters, flags |

### Distinguishing Joystick Controllers

The two joystick controllers are distinguished at **compile time** via `ECU_JOYSTICK_ID` and `ECU_PREFERRED_ADDRESS` build flags.
Use the PlatformIO environments `joystick1` and `joystick2` to build the correct firmware for each unit.

## Pinout (LilyGO T-CAN defaults)

| Signal | GPIO | Notes |
|--------|------|-------|
| CAN TX | 5 | Built-in transceiver |
| CAN RX | 4 | Built-in transceiver |
| WS2812 | 48 | Status LED (onboard) |
| I2C SDA | 21 | PCA9685 (motor driver only) |
| I2C SCL | 22 | PCA9685 (motor driver only) |
| Pot 1 | 6 | Joystick X (joystick only) |
| Pot 2 | 7 | Joystick Y (joystick only) |
| Pot 3 | 15 | Joystick Z (joystick only) |
| Button 1 | 16 | Active low, internal pullup |
| Button 2 | 17 | Active low, internal pullup |

## Building & Flashing

Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).

```bash
# Build motor driver
pio run -e motor_driver

# Build joystick 1
pio run -e joystick1

# Build joystick 2
pio run -e joystick2

# Flash motor driver
pio run -e motor_driver --target upload

# Flash joystick 1
pio run -e joystick1 --target upload
```

### OTA Updates

OTA builds include a Wi-Fi access point and web upload interface.

```bash
# Build & upload motor driver with OTA support
pio run -e motor_driver_ota --target upload
```

After booting:
1. Connect to the AP named `forwarder-motor` (or `forwarder-joy1` / `forwarder-joy2`)
2. Password: `forwarder123`
3. Open http://192.168.4.1 or http://forwarder-motor.local
4. Upload a `.bin` firmware file

To produce a `.bin` for OTA:
```bash
pio run -e motor_driver
# The .bin will be in .pio/build/motor_driver/firmware.bin
```

## Safety Features

- **Address claiming**: J1939-style startup arbitration ensures no address collisions
- **Solenoid timeout**: Motor driver shuts off all solenoids if no CAN command received within 500 ms
- **Bus-off recovery**: Automatic TWAI recovery on CAN errors
- **Heartbeat**: All ECUs broadcast status every 1 second

## Project Structure

```
forwarderke/
├── lib/
│   └── ForwarderCAN/         # Shared CAN/J1939 library
├── src/
│   ├── main.cpp              # Entry point (build flag selects ECU type)
│   ├── ecu_motor_driver.cpp  # Motor driver logic
│   ├── ecu_joystick.cpp      # Joystick logic
│   ├── ota_webserver.cpp     # Optional Wi-Fi OTA web UI
│   └── *.h                   # Headers
├── platformio.ini            # Build environments
└── README.md                 # This file
```

## License

MIT
