# Hardware Validation System

<cite>
**Referenced Files in This Document**
- [README.md](file://README.md)
- [platformio.ini](file://platformio.ini)
- [main.cpp](file://src/main.cpp)
- [ecu_joystick.cpp](file://src/ecu_joystick.cpp)
- [ecu_motor_driver.cpp](file://src/ecu_motor_driver.cpp)
- [can_output.cpp](file://src/can_output.cpp)
- [ota_webserver.cpp](file://src/ota_webserver.cpp)
- [web_state.cpp](file://src/web_state.cpp)
- [ecu_joystick.h](file://src/ecu_joystick.h)
- [ecu_motor_driver.h](file://src/ecu_motor_driver.h)
- [can_output.h](file://src/can_output.h)
- [ota_webserver.h](file://src/ota_webserver.h)
- [web_state.h](file://src/web_state.h)
</cite>

## Table of Contents
1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Core Components](#core-components)
4. [Architecture Overview](#architecture-overview)
5. [Detailed Component Analysis](#detailed-component-analysis)
6. [Dependency Analysis](#dependency-analysis)
7. [Performance Considerations](#performance-considerations)
8. [Troubleshooting Guide](#troubleshooting-guide)
9. [Conclusion](#conclusion)

## Introduction
This Hardware Validation System is an ESP32-based CAN bus control solution designed for agricultural forwarder (logging machine) hydraulic valve blocks. It replaces a factory controller with an open-source, J1939-like addressing system operating at 250 kbps over a 250 kbps CAN bus. The system consists of three ECUs:
- Motor Driver ECU (address 0x20): Controls 8 solenoids via PCA9685 PWM driver
- Joystick 1 ECU (address 0x21): Reads 3 potentiometers and 2 buttons, publishes on CAN
- Joystick 2 ECU (address 0x22): Reads 3 potentiometers and 2 buttons, publishes on CAN

The system implements safety features including address claiming, solenoid timeout protection, bus-off recovery, and heartbeat monitoring. It supports Over-The-Air (OTA) updates via Wi-Fi access point and web interface.

## Project Structure
The project follows a modular architecture with separate ECUs and shared libraries:

```mermaid
graph TB
subgraph "Build Environments"
MD_S3[motor_driver_s3]
MD_S3_UART[motor_driver_s3_uart]
MD_LEGACY[motor_driver]
JS1[joystick1]
JS2[joystick2]
OTA[OTA variants]
end
subgraph "Source Files"
MAIN[src/main.cpp]
ECU_JS[src/ecu_joystick.cpp]
ECU_MD[src/ecu_motor_driver.cpp]
CAN_OUT[src/can_output.cpp]
OTA_WS[src/ota_webserver.cpp]
WEB_STATE[src/web_state.cpp]
end
subgraph "Shared Libraries"
FORWARDER_CAN[ForwarderCAN]
FORWARDER_CONFIG[ForwarderConfig]
end
MD_S3 --> MAIN
JS1 --> MAIN
JS2 --> MAIN
MAIN --> ECU_JS
MAIN --> ECU_MD
ECU_JS --> FORWARDER_CAN
ECU_MD --> FORWARDER_CAN
ECU_MD --> FORWARDER_CONFIG
ECU_JS --> OTA_WS
ECU_MD --> OTA_WS
OTA_WS --> WEB_STATE
```

**Diagram sources**
- [platformio.ini:1-142](file://platformio.ini#L1-L142)
- [main.cpp:1-39](file://src/main.cpp#L1-L39)

**Section sources**
- [platformio.ini:1-142](file://platformio.ini#L1-L142)
- [README.md:175-189](file://README.md#L175-L189)

## Core Components
The system comprises several key components working together to validate hardware functionality:

### CAN Protocol Implementation
The system implements J1939-like addressing with 29-bit extended IDs. The ID structure includes priority bits, PDU format, PDU specific fields, and source/destination addresses. This provides deterministic message routing and collision avoidance.

### ECU Types and Responsibilities
- **Motor Driver ECU**: Manages solenoid outputs through PCA9685 PWM controllers, implements axis mapping with deadband and PWM scaling, and provides safety timeout protection
- **Joystick ECUs**: Read analog inputs from potentiometers and digital inputs from buttons, implement debouncing, and broadcast status information
- **OTA Web Server**: Provides web-based configuration interface with real-time monitoring and remote updates

### Hardware Validation Features
The system includes comprehensive hardware validation through:
- GPIO loopback testing for CAN transceiver verification
- PCA9685 detection and initialization
- LED status indication with different patterns for various states
- Real-time bus monitoring and error reporting

**Section sources**
- [README.md:31-125](file://README.md#L31-L125)
- [ecu_motor_driver.cpp:378-394](file://src/ecu_motor_driver.cpp#L378-L394)
- [ecu_joystick.cpp:89-116](file://src/ecu_joystick.cpp#L89-L116)

## Architecture Overview
The system architecture implements a distributed ECU network with centralized monitoring and control capabilities:

```mermaid
graph TB
subgraph "CAN Bus Network"
JS1[Joystick 1<br/>Address 0x21]
JS2[Joystick 2<br/>Address 0x22]
MD[Motor Driver<br/>Address 0x20]
end
subgraph "Local Processing"
JS1_ESP[ESP32 MCU]
JS2_ESP[ESP32 MCU]
MD_ESP[ESP32 MCU]
end
subgraph "External Interfaces"
PCA9685[PCA9685 PWM<br/>8 Channel Outputs]
POT1[Potentiometer 1]
POT2[Potentiometer 2]
POT3[Potentiometer 3]
BTN1[Button 1]
BTN2[Button 2]
LED[WS2812 LED]
end
subgraph "Communication Layer"
TWAI[TWAI Controller]
TRANSCEIVER[CAN Transceiver]
end
JS1 --> JS1_ESP
JS2 --> JS2_ESP
MD --> MD_ESP
JS1_ESP --> POT1
JS1_ESP --> POT2
JS1_ESP --> POT3
JS1_ESP --> BTN1
JS1_ESP --> BTN2
JS1_ESP --> LED
MD_ESP --> PCA9685
JS1_ESP --> TWAI
JS2_ESP --> TWAI
MD_ESP --> TWAI
TWAI --> TRANSCEIVER
TRANSCEIVER --> CAN_BUS[CAN Bus]
CAN_BUS --> JS1
CAN_BUS --> JS2
CAN_BUS --> MD
```

**Diagram sources**
- [README.md:8-14](file://README.md#L8-L14)
- [platformio.ini:18-87](file://platformio.ini#L18-L87)

## Detailed Component Analysis

### Motor Driver ECU Analysis
The Motor Driver ECU serves as the central controller for hydraulic solenoid actuation:

```mermaid
classDiagram
class MotorDriverECU {
+Adafruit_PWMServoDriver pca1
+Adafruit_PWMServoDriver pca2
+NeoPixelBus strip
+ForwarderCAN* g_can
+MotorConfig g_motorCfg
+uint16_t g_solenoidValues[16]
+processCAN()
+updateAxes()
+mapAxis()
+setPWM()
+allOff()
+autoConfigDefaults()
}
class PCA9685Controller {
+setPWM(channel, value)
+setOscillatorFrequency(freq)
+setPWMFreq(freq)
}
class AxisConfig {
+uint8_t sourceAddress
+uint8_t potIndex
+uint8_t outputChannel
+uint16_t deadbandMin
+uint16_t deadbandMax
+uint8_t pwmMin
+uint8_t pwmMax
+uint8_t flags
+isEnabled()
+isBidirectional()
}
class ForwarderCAN {
+sendBroadcast()
+receive()
+begin()
+getAddress()
+isOnline()
}
MotorDriverECU --> PCA9685Controller : "controls"
MotorDriverECU --> AxisConfig : "manages"
MotorDriverECU --> ForwarderCAN : "communicates via"
```

**Diagram sources**
- [ecu_motor_driver.cpp:39-67](file://src/ecu_motor_driver.cpp#L39-L67)
- [ecu_motor_driver.cpp:104-139](file://src/ecu_motor_driver.cpp#L104-L139)

#### Axis Mapping Algorithm
The Motor Driver implements sophisticated axis mapping with deadband handling and bidirectional support:

```mermaid
flowchart TD
Start([Axis Mapping Entry]) --> CheckEnabled{"Axis Enabled?"}
CheckEnabled --> |No| ReturnZero["Return 0 for both channels"]
CheckEnabled --> |Yes| GetSource["Get source joystick data"]
GetSource --> CheckTimeout{"Source Timeout < 1000ms?"}
CheckTimeout --> |No| ZeroChannels["Set both channels to 0"]
CheckTimeout --> |Yes| CheckDirection{"Pot Value > DeadbandMax?"}
CheckDirection --> |Yes| ForwardCalc["Calculate forward PWM<br/>fwd = map(pot, deadbandMax, 1023, pwmMin, pwmMax)"]
CheckDirection --> |No| CheckReverse{"Pot Value < DeadbandMin?"}
CheckReverse --> |Yes| ReverseCalc["Calculate reverse PWM<br/>rev = map(deadbandMin, pot, pwmMin, pwmMax)"]
CheckReverse --> |No| InDeadband["Both channels = 0"]
ForwardCalc --> ApplyLimits["Apply 4095 limit"]
ReverseCalc --> ApplyLimits
InDeadband --> ApplyLimits
ApplyLimits --> SetOutputs["Set PWM outputs"]
ZeroChannels --> SetOutputs
ReturnZero --> End([Exit])
SetOutputs --> End
```

**Diagram sources**
- [ecu_motor_driver.cpp:104-139](file://src/ecu_motor_driver.cpp#L104-L139)
- [ecu_motor_driver.cpp:143-184](file://src/ecu_motor_driver.cpp#L143-L184)

**Section sources**
- [ecu_motor_driver.cpp:1-479](file://src/ecu_motor_driver.cpp#L1-L479)

### Joystick ECU Analysis
The Joystick ECUs handle input acquisition and status reporting:

```mermaid
sequenceDiagram
participant POT as "Potentiometers"
participant BTN as "Buttons"
participant ECU as "Joystick ECU"
participant CAN as "CAN Bus"
participant MD as "Motor Driver"
Note over ECU : Input Sampling Loop
POT->>ECU : Analog readings (10-bit)
BTN->>ECU : Digital states
ECU->>ECU : Debounce button signals
ECU->>ECU : Update local state
Note over ECU : CAN Transmission (25Hz)
ECU->>CAN : PF_JOYSTICK_POT1 (2 bytes)
ECU->>CAN : PF_JOYSTICK_POT2 (2 bytes)
ECU->>CAN : PF_JOYSTICK_POT3 (2 bytes)
ECU->>CAN : PF_JOYSTICK_BUTTONS (1 byte)
Note over CAN,MD : Motor Driver Processing
CAN->>MD : Receive joystick data
MD->>MD : Update axis mapping
MD->>MD : Calculate PWM values
MD->>MD : Set solenoid outputs
Note over ECU : Heartbeat (1Hz)
ECU->>CAN : PF_HEARTBEAT (8 bytes)
CAN->>MD : Receive heartbeat
MD->>MD : Update module status
```

**Diagram sources**
- [ecu_joystick.cpp:74-87](file://src/ecu_joystick.cpp#L74-L87)
- [ecu_joystick.cpp:227-278](file://src/ecu_joystick.cpp#L227-L278)

#### Input Processing and Debouncing
The Joystick ECU implements robust input processing with debouncing to handle mechanical switch bounce:

```mermaid
flowchart TD
InputRead[Read Raw Inputs] --> CheckBtn1{"Button 1 Changed?"}
CheckBtn1 --> |Yes| ResetBtn1Timer["Reset debounce timer"]
CheckBtn1 --> |No| CheckBtn2{"Button 2 Changed?"}
ResetBtn1Timer --> CheckBtn2
CheckBtn2 --> |Yes| ResetBtn2Timer["Reset debounce timer"]
CheckBtn2 --> |No| CheckStable{"Debounce timers expired?"}
ResetBtn2Timer --> CheckStable
CheckStable --> |No| SkipUpdate["Skip state update"]
CheckStable --> |Yes| UpdateState["Update button states"]
UpdateState --> ApplyDebounce["Apply 50ms debounce"]
ApplyDebounce --> UpdatePrev["Update previous states"]
SkipUpdate --> End([End])
UpdatePrev --> End
```

**Diagram sources**
- [ecu_joystick.cpp:79-86](file://src/ecu_joystick.cpp#L79-L86)

**Section sources**
- [ecu_joystick.cpp:1-281](file://src/ecu_joystick.cpp#L1-L281)

### CAN Output Control Analysis
The CAN Output module provides flexible GPIO control based on CAN message patterns:

```mermaid
classDiagram
class CanOutputRule {
+bool enabled
+uint8_t matchPF
+uint8_t matchSA
+uint8_t gpioPin
+uint8_t mode
+uint16_t momentaryMs
}
class CanOutputManager {
+CanOutputRule rules[4]
+bool outputState[4]
+uint32_t momentaryTimer[4]
+can_output_setup()
+can_output_process_can()
+can_output_loop()
+setOutput()
}
class GPIOOutput {
+pinMode()
+digitalWrite()
}
CanOutputManager --> CanOutputRule : "manages"
CanOutputManager --> GPIOOutput : "controls"
```

**Diagram sources**
- [can_output.cpp:3-6](file://src/can_output.cpp#L3-L6)
- [can_output.cpp:29-48](file://src/can_output.cpp#L29-L48)

**Section sources**
- [can_output.cpp:1-66](file://src/can_output.cpp#L1-L66)

### OTA Web Server Analysis
The OTA Web Server provides comprehensive remote management capabilities:

```mermaid
sequenceDiagram
participant Browser as "Web Browser"
participant ESP as "ESP32 Web Server"
participant CAN as "CAN Bus"
participant Config as "NVS Storage"
Browser->>ESP : GET / (HTML Page)
ESP->>Browser : HTML + JavaScript
loop 1-second intervals
Browser->>ESP : GET /api/state
ESP->>Browser : JSON state data
Browser->>ESP : GET /api/config
ESP->>Browser : JSON configuration
end
Browser->>ESP : POST /api/config
ESP->>Config : Save axis configuration
ESP->>CAN : Broadcast PF_CONFIG_AXIS
ESP->>Browser : {"ok" : true}
Browser->>ESP : POST /api/led
ESP->>CAN : Send PF_LED_COLOR
ESP->>Browser : {"ok" : true}
Browser->>ESP : POST /api/address
ESP->>CAN : Send PF_SET_ADDRESS
ESP->>Browser : {"ok" : true}
Browser->>ESP : POST /update
ESP->>ESP : Start OTA update
ESP->>Browser : {"ok" : true}
```

**Diagram sources**
- [ota_webserver.cpp:600-659](file://src/ota_webserver.cpp#L600-L659)
- [ota_webserver.cpp:787-800](file://src/ota_webserver.cpp#L787-L800)

**Section sources**
- [ota_webserver.cpp:1-933](file://src/ota_webserver.cpp#L1-L933)

## Dependency Analysis
The system exhibits clean separation of concerns with well-defined dependencies:

```mermaid
graph TB
subgraph "Application Layer"
MAIN[src/main.cpp]
ECU_JS[src/ecu_joystick.cpp]
ECU_MD[src/ecu_motor_driver.cpp]
CAN_OUT[src/can_output.cpp]
OTA_WS[src/ota_webserver.cpp]
WEB_STATE[src/web_state.cpp]
end
subgraph "Library Dependencies"
ARDUINO[Arduino Framework]
ADAFRUIT[Adafruit PWM Library]
NEOPIXEL[NeoPixelBus Library]
ESP32[ESP32 SDK]
end
subgraph "Build System"
PIO[PlatformIO]
ENV[Build Environments]
end
MAIN --> ECU_JS
MAIN --> ECU_MD
ECU_JS --> ADAFRUIT
ECU_MD --> ADAFRUIT
ECU_JS --> NEOPIXEL
ECU_MD --> NEOPIXEL
ECU_JS --> ESP32
ECU_MD --> ESP32
OTA_WS --> ESP32
PIO --> ENV
ENV --> MAIN
ENV --> ECU_JS
ENV --> ECU_MD
```

**Diagram sources**
- [platformio.ini:9-11](file://platformio.ini#L9-L11)
- [platformio.ini:18-142](file://platformio.ini#L18-L142)

**Section sources**
- [platformio.ini:1-142](file://platformio.ini#L1-L142)

## Performance Considerations
The system implements several performance optimizations:

### Timing Constraints
- **Joystick sampling rate**: 25 Hz (40ms interval) for potentiometer and button data
- **Heartbeat transmission**: 1 Hz for status reporting
- **Safety timeout**: 500 ms for solenoid shutdown
- **Bus status reporting**: 5 seconds for diagnostic information

### Memory Management
- Static allocation for CAN buffers and state arrays
- Fixed-size configuration structures to minimize heap usage
- Chunked JSON responses to avoid large memory allocations

### CAN Bus Optimization
- Priority-based message scheduling with J1939-like addressing
- Efficient message filtering and processing
- Built-in TWAI status monitoring for bus health

## Troubleshooting Guide

### Common Issues and Solutions

#### CAN Bus Communication Problems
**Symptoms**: No joystick data reaching motor driver, frequent bus errors
**Diagnosis Steps**:
1. Check CAN termination resistors (220Ω) at both ends of the bus
2. Verify CAN transceiver power (ME2107_EN pin)
3. Monitor TWAI status counters for bus errors
4. Test GPIO loopback for physical layer verification

**Section sources**
- [ecu_motor_driver.cpp:378-394](file://src/ecu_motor_driver.cpp#L378-L394)
- [ecu_motor_driver.cpp:450-455](file://src/ecu_motor_driver.cpp#L450-L455)

#### LED Status Indicators
The system uses different LED patterns to indicate various states:
- **Solid green**: System ready and operational
- **Flashing red**: CAN initialization failure
- **Pulsing amber**: Bus offline condition
- **Blinking white**: Identify/blink command active
- **Fast blinking**: Active joystick data reception

**Section sources**
- [ecu_joystick.cpp:107-113](file://src/ecu_joystick.cpp#L107-L113)
- [ecu_motor_driver.cpp:200-212](file://src/ecu_motor_driver.cpp#L200-L212)

#### Address Claiming Conflicts
**Symptoms**: Multiple ECUs claiming the same address
**Resolution**:
1. Use address claiming procedure during startup
2. Manually set forced addresses via web interface
3. Verify address uniqueness on the bus
4. Check for stuck bus conditions requiring restart

**Section sources**
- [ecu_joystick.cpp:154-164](file://src/ecu_joystick.cpp#L154-L164)
- [ecu_motor_driver.cpp:268-278](file://src/ecu_motor_driver.cpp#L268-L278)

#### OTA Update Failures
**Symptoms**: OTA updates failing or timing out
**Troubleshooting**:
1. Ensure proper Wi-Fi credentials and network connectivity
2. Verify sufficient free flash space
3. Check firmware compatibility with target ECU
4. Monitor update progress through web interface

**Section sources**
- [ota_webserver.cpp:567-586](file://src/ota_webserver.cpp#L567-L586)

## Conclusion
The Hardware Validation System provides a robust, open-source solution for agricultural vehicle hydraulic control systems. Its modular architecture, comprehensive safety features, and remote management capabilities make it suitable for industrial applications. The system's J1939-like addressing scheme ensures reliable communication, while the OTA capabilities facilitate maintenance and updates without physical access to field equipment.

Key strengths include:
- Deterministic CAN message routing with priority-based scheduling
- Comprehensive hardware validation through GPIO and bus testing
- Flexible configuration management with persistent storage
- Remote monitoring and control via web interface
- Safety mechanisms including timeout protection and bus-off recovery

The system's design allows for easy expansion and customization for different vehicle configurations while maintaining reliability and safety standards essential for agricultural machinery operation.