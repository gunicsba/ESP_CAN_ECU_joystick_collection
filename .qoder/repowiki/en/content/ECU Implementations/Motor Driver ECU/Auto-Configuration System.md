# Auto-Configuration System

<cite>
**Referenced Files in This Document**
- [README.md](file://README.md)
- [platformio.ini](file://platformio.ini)
- [main.cpp](file://src/main.cpp)
- [ecu_motor_driver.cpp](file://src/ecu_motor_driver.cpp)
- [ecu_joystick.cpp](file://src/ecu_joystick.cpp)
- [ForwarderConfig.h](file://lib/ForwarderConfig/ForwarderConfig.h)
- [ForwarderConfig.cpp](file://lib/ForwarderConfig/ForwarderConfig.cpp)
- [ota_webserver.cpp](file://src/ota_webserver.cpp)
- [web_state.h](file://src/web_state.h)
- [web_state.cpp](file://src/web_state.cpp)
</cite>

## Table of Contents
1. [Introduction](#introduction)
2. [System Architecture](#system-architecture)
3. [Auto-Configuration Components](#auto-configuration-components)
4. [Configuration Data Model](#configuration-data-model)
5. [Build System Integration](#build-system-integration)
6. [Runtime Configuration Management](#runtime-configuration-management)
7. [Web-Based Configuration Interface](#web-based-configuration-interface)
8. [Safety and Defaults](#safety-and-defaults)
9. [Implementation Patterns](#implementation-patterns)
10. [Troubleshooting Guide](#troubleshooting-guide)
11. [Conclusion](#conclusion)

## Introduction

The Auto-Configuration System is a comprehensive framework designed for the Forwarder CAN Controller project, enabling automatic configuration and management of ECU (Electronic Control Unit) devices in an agricultural vehicle hydraulic system. This system provides intelligent auto-configuration capabilities, persistent storage management, and remote configuration through a web-based interface.

The system manages three distinct ECUs operating on a shared 250 kbps CAN bus: a motor driver ECU controlling 8 solenoids via PCA9685 PWM drivers, and two joystick ECUs that read analog potentiometers and buttons for operator input. The auto-configuration system ensures seamless operation by automatically detecting and configuring hardware components, managing persistent settings, and providing runtime reconfiguration capabilities.

## System Architecture

The auto-configuration system follows a modular architecture with clear separation of concerns between hardware abstraction, configuration management, and user interface components.

```mermaid
graph TB
subgraph "Hardware Layer"
CAN_BUS[CAN Bus 250kbps]
JOYSTICK1[Joystick ECU 1<br/>Address 0x21]
JOYSTICK2[Joystick ECU 2<br/>Address 0x22]
MOTOR_DRIVER[Motor Driver ECU<br/>Address 0x20]
end
subgraph "Application Layer"
MAIN_APP[Main Application<br/>Entry Point]
CONFIG_MGR[Configuration Manager]
CAN_HANDLER[CAN Message Handler]
WEB_UI[Web Configuration UI]
end
subgraph "Storage Layer"
NVS_STORAGE[NVS Preferences<br/>Persistent Storage]
DEFAULTS[Factory Defaults]
end
subgraph "User Interface"
WEB_BROWSER[Web Browser]
MOBILE_APP[Mobile App]
end
CAN_BUS --> JOYSTICK1
CAN_BUS --> JOYSTICK2
CAN_BUS --> MOTOR_DRIVER
MAIN_APP --> CONFIG_MGR
MAIN_APP --> CAN_HANDLER
MAIN_APP --> WEB_UI
CONFIG_MGR --> NVS_STORAGE
CONFIG_MGR --> DEFAULTS
WEB_BROWSER --> WEB_UI
MOBILE_APP --> WEB_UI
WEB_UI --> CONFIG_MGR
CONFIG_MGR --> CAN_HANDLER
```

**Diagram sources**
- [platformio.ini:17-142](file://platformio.ini#L17-L142)
- [main.cpp:11-39](file://src/main.cpp#L11-L39)

## Auto-Configuration Components

The auto-configuration system consists of several interconnected components that work together to provide seamless device configuration and management.

### Core Configuration Classes

The system centers around several key configuration classes that manage different aspects of device behavior:

```mermaid
classDiagram
class ForwarderConfig {
-const char* _ns
-Preferences _prefs
-bool _started
+ForwarderConfig(namespace)
+begin() bool
+getForcedAddress(defaultAddr) uint8_t
+setForcedAddress(addr) void
+clearForcedAddress() void
+loadMotorConfig(cfg) bool
+saveMotorConfig(cfg) bool
+saveAxisConfig(axisIdx, axis) bool
+loadCanOutputRules(rules) bool
+saveCanOutputRule(index, rule) bool
+loadDefaults(cfg) void
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
+isEnabled() bool
+isBidirectional() bool
+pack(buf, axisIdx) void
+unpack(buf) void
+packedSize() uint8_t
}
class MotorConfig {
+AxisConfig axes[MAX_AXIS_COUNT]
+uint8_t pcaCount
}
class CanOutputRule {
+bool enabled
+uint8_t matchPF
+uint8_t matchSA
+uint8_t gpioPin
+uint8_t mode
+uint16_t momentaryMs
+pack(buf) void
+unpack(buf) void
}
ForwarderConfig --> MotorConfig : "manages"
MotorConfig --> AxisConfig : "contains"
ForwarderConfig --> CanOutputRule : "manages"
ForwarderConfig --> AxisConfig : "serializes"
```

**Diagram sources**
- [ForwarderConfig.h:64-92](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L92)
- [ForwarderConfig.cpp:41-57](file://lib/ForwarderConfig/ForwarderConfig.cpp#L41-L57)

### ECU-Specific Configuration Management

Each ECU type implements its own configuration management with auto-configuration capabilities:

```mermaid
sequenceDiagram
participant BOOT as Boot Process
participant CFG as ForwarderConfig
participant DEF as Default Config
participant NVS as NVS Storage
participant ECU as ECU Instance
BOOT->>CFG : begin()
CFG->>NVS : preferences.begin()
CFG->>CFG : loadMotorConfig()
CFG->>NVS : getUChar("pca_count")
loop For each axis
CFG->>NVS : getString("axis_%d")
alt Axis exists
CFG->>CFG : axis.unpack()
else No axis config
CFG->>DEF : loadDefaults()
DEF->>CFG : default values
end
end
CFG->>CFG : autoConfigDefaults()
alt No existing config
CFG->>DEF : apply default mapping
DEF->>NVS : saveAxisConfig()
else Already configured
CFG->>CFG : skip auto-config
end
CFG->>ECU : configuration complete
```

**Diagram sources**
- [ecu_motor_driver.cpp:361-418](file://src/ecu_motor_driver.cpp#L361-L418)
- [ForwarderConfig.cpp:76-127](file://lib/ForwarderConfig/ForwarderConfig.cpp#L76-L127)

**Section sources**
- [ForwarderConfig.h:1-92](file://lib/ForwarderConfig/ForwarderConfig.h#L1-L92)
- [ForwarderConfig.cpp:1-184](file://lib/ForwarderConfig/ForwarderConfig.cpp#L1-L184)
- [ecu_motor_driver.cpp:330-359](file://src/ecu_motor_driver.cpp#L330-L359)

## Configuration Data Model

The auto-configuration system uses a structured data model to represent and manage configuration settings across different ECU types.

### Axis Configuration Structure

The axis configuration represents the mapping between joystick inputs and solenoid outputs:

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| sourceAddress | uint8_t | 0x21-0x22 | Joystick source address (0x21 or 0x22) |
| potIndex | uint8_t | 0-2 | Potentiometer index (0=POT1, 1=POT2, 2=POT3) |
| outputChannel | uint8_t | 0-15 | PWM output channel (0-7 or 8-15) |
| deadbandMin | uint16_t | 0-1023 | Deadband minimum threshold |
| deadbandMax | uint16_t | 0-1023 | Deadband maximum threshold |
| pwmMin | uint8_t | 0-255 | Minimum PWM value (scaled to 12-bit) |
| pwmMax | uint8_t | 0-255 | Maximum PWM value (scaled to 12-bit) |
| flags | uint8_t | Bit flags | Configuration flags (enabled, bidirectional) |

### CAN Output Rule Structure

CAN-triggered GPIO output rules define how incoming CAN messages control external GPIO pins:

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| enabled | bool | true/false | Rule activation status |
| matchPF | uint8_t | 0-255 | PDU Format pattern to match |
| matchSA | uint8_t | 0-255 | Source Address pattern to match (0=any) |
| gpioPin | uint8_t | 0-39 | Target GPIO pin number |
| mode | uint8_t | 0-1 | Operation mode (0=toggle, 1=momentary) |
| momentaryMs | uint16_t | 50-10000 | Momentary duration in milliseconds |

### Configuration Serialization

The system uses compact 8-byte serialization for efficient CAN transmission:

```mermaid
flowchart TD
START([Configuration Data]) --> PACK[Pack to 8 Bytes]
PACK --> AXIS_PACK[AxisConfig.pack()]
AXIS_PACK --> BYTE0["Byte 0: axis_idx (0-15)"]
AXIS_PACK --> BYTE1["Byte 1: source_address (0x21,0x22)"]
AXIS_PACK --> BYTE2["Byte 2: (pot_idx<<6)|(output_ch<<2)|flags"]
AXIS_PACK --> BYTE3["Byte 3: deadband_min/4 (0-255)"]
AXIS_PACK --> BYTE4["Byte 4: deadband_max/4 (0-255)"]
AXIS_PACK --> BYTE5["Byte 5: pwm_min (0-255)"]
AXIS_PACK --> BYTE6["Byte 6: pwm_max (0-255)"]
AXIS_PACK --> BYTE7["Byte 7: reserved (0)"]
PACK --> RULE_PACK[CanOutputRule.pack()]
RULE_PACK --> R_BYTE0["Byte 0: enabled flag"]
RULE_PACK --> R_BYTE1["Byte 1: matchPF"]
RULE_PACK --> R_BYTE2["Byte 2: matchSA"]
RULE_PACK --> R_BYTE3["Byte 3: gpioPin"]
RULE_PACK --> R_BYTE4["Byte 4: mode"]
RULE_PACK --> R_BYTE5["Byte 5: momentaryMs LSB"]
RULE_PACK --> R_BYTE6["Byte 6: momentaryMs MSB"]
RULE_PACK --> R_BYTE7["Byte 7: reserved (0)"]
BYTE0 --> UNPACK[Unpack from 8 Bytes]
BYTE1 --> UNPACK
BYTE2 --> UNPACK
BYTE3 --> UNPACK
BYTE4 --> UNPACK
BYTE5 --> UNPACK
BYTE6 --> UNPACK
BYTE7 --> UNPACK
UNPACK --> AXIS_UNPACK[AxisConfig.unpack()]
UNPACK --> RULE_UNPACK[CanOutputRule.unpack()]
```

**Diagram sources**
- [ForwarderConfig.cpp:6-49](file://lib/ForwarderConfig/ForwarderConfig.cpp#L6-L49)

**Section sources**
- [ForwarderConfig.h:28-62](file://lib/ForwarderConfig/ForwarderConfig.h#L28-L62)
- [ForwarderConfig.cpp:6-49](file://lib/ForwarderConfig/ForwarderConfig.cpp#L6-L49)

## Build System Integration

The auto-configuration system integrates seamlessly with PlatformIO's build system through environment-specific configurations that define hardware parameters and capabilities.

### Environment Configuration Structure

Each build environment defines specific parameters for different hardware configurations:

| Parameter | Motor Driver | Joystick 1 | Joystick 2 |
|-----------|--------------|------------|------------|
| Board | esp32s3box | esp32dev | esp32dev |
| Preferred Address | 0x20 | 0x21 | 0x22 |
| PCA9685 Pins | SDA=8, SCL=18 | N/A | N/A |
| CAN Pins | TX=16, RX=17 | TX=27, RX=26 | TX=27, RX=26 |
| LED Pin | 39 | 4 | 4 |
| Pot Pins | N/A | 32,33,34 | 32,33,34 |
| Button Pins | N/A | 12,5 | 12,5 |

### Build Flags and Hardware Definitions

The system uses compile-time flags to configure hardware-specific parameters:

```mermaid
graph LR
subgraph "Build Flags"
BITRATE[CAN_BITRATE=250000]
PRIORITY[PROTOCOL_PRIORITY_DEFAULT=6]
WATCHDOG[WATCHDOG_TIMEOUT_MS=1000]
SAFETY[SAFETY_TIMEOUT_MS=500]
end
subgraph "ECU Types"
MOTOR[MOTOR_DRIVER<br/>ECU_TYPE_MOTOR_DRIVER]
JOYSTICK[JOYSTICK<br/>ECU_TYPE_JOYSTICK]
end
subgraph "Hardware Pins"
TX_PIN[CAN_TX_PIN]
RX_PIN[CAN_RX_PIN]
LED_PIN[WS2812_PIN]
POT_PINS[POT1-POT3]
BTN_PINS[BTN1-BTN2]
end
BITRATE --> MOTOR
BITRATE --> JOYSTICK
PRIORITY --> MOTOR
PRIORITY --> JOYSTICK
SAFETY --> MOTOR
WATCHDOG --> JOYSTICK
TX_PIN --> MOTOR
RX_PIN --> MOTOR
LED_PIN --> MOTOR
POT_PINS --> JOYSTICK
BTN_PINS --> JOYSTICK
```

**Diagram sources**
- [platformio.ini:12-16](file://platformio.ini#L12-L16)
- [platformio.ini:18-142](file://platformio.ini#L18-L142)

**Section sources**
- [platformio.ini:1-142](file://platformio.ini#L1-L142)
- [main.cpp:11-17](file://src/main.cpp#L11-L17)

## Runtime Configuration Management

The auto-configuration system operates during both initialization and runtime to ensure optimal device operation and user flexibility.

### Initialization Sequence

The configuration management follows a structured initialization process:

```mermaid
sequenceDiagram
participant SETUP as ecu_setup()
participant CFG as ForwarderConfig
participant NVS as NVS Storage
participant AUTO as Auto-Config
participant HARDWARE as Hardware Init
SETUP->>CFG : begin()
CFG->>NVS : preferences.begin()
SETUP->>CFG : getForcedAddress()
CFG->>NVS : getUChar("forced_addr")
SETUP->>CFG : loadMotorConfig()
CFG->>NVS : getString("axis_%d")
SETUP->>AUTO : autoConfigDefaults()
alt No existing config
AUTO->>CFG : saveAxisConfig()
CFG->>NVS : putBytes("axis_%d")
else Already configured
AUTO->>AUTO : Skip auto-config
end
SETUP->>HARDWARE : initPCA()
SETUP->>HARDWARE : initCAN()
SETUP->>SETUP : Complete
```

**Diagram sources**
- [ecu_motor_driver.cpp:361-418](file://src/ecu_motor_driver.cpp#L361-L418)
- [ForwarderConfig.cpp:76-127](file://lib/ForwarderConfig/ForwarderConfig.cpp#L76-L127)

### Runtime Configuration Updates

The system supports dynamic configuration updates through CAN messages and web interface:

```mermaid
flowchart TD
CAN_MSG[CAN Message Received] --> MSG_TYPE{Message Type?}
MSG_TYPE --> |PF_CONFIG_AXIS| AXIS_UPDATE[Axis Configuration Update]
MSG_TYPE --> |PF_SET_ADDRESS| ADDR_UPDATE[Address Change Request]
MSG_TYPE --> |PF_REQUEST_CONFIG| CFG_REQUEST[Configuration Request]
MSG_TYPE --> |Other Messages| NORMAL_PROCESS[Normal Processing]
AXIS_UPDATE --> VALIDATE[Validate Configuration]
VALIDATE --> VALID{Valid?}
VALID --> |Yes| SAVE_CFG[Save to NVS]
VALID --> |No| ERROR[Error Response]
SAVE_CFG --> APPLY_CFG[Apply Configuration]
ADDR_UPDATE --> ADDR_VALID{Valid Address?}
ADDR_VALID --> |Yes| SAVE_ADDR[Save Forced Address]
ADDR_VALID --> |No| ADDR_ERROR[Address Error]
SAVE_ADDR --> REBOOT[Reboot Device]
CFG_REQUEST --> SEND_RESPONSE[Send Configuration Response]
APPLY_CFG --> UPDATE_COMPLETE[Configuration Updated]
SEND_RESPONSE --> UPDATE_COMPLETE
NORMAL_PROCESS --> UPDATE_COMPLETE
```

**Diagram sources**
- [ecu_motor_driver.cpp:217-315](file://src/ecu_motor_driver.cpp#L217-L315)
- [ecu_joystick.cpp:133-166](file://src/ecu_joystick.cpp#L133-L166)

**Section sources**
- [ecu_motor_driver.cpp:361-479](file://src/ecu_motor_driver.cpp#L361-L479)
- [ecu_joystick.cpp:181-281](file://src/ecu_joystick.cpp#L181-L281)

## Web-Based Configuration Interface

The auto-configuration system includes a comprehensive web interface that allows remote configuration and monitoring of ECU devices through a browser-based dashboard.

### Web Interface Architecture

The web interface consists of multiple interconnected components working together to provide a seamless configuration experience:

```mermaid
graph TB
subgraph "Web Interface Components"
DASHBOARD[Dashboard Tab]
MODULES[Modules Tab]
MAPPING[Motor Mapping Tab]
CANOUT[CAN Output Tab]
LED_TEST[LED Test Tab]
OTA_UPDATE[OTA Update Tab]
end
subgraph "Backend API"
STATE_API[/api/state]
CONFIG_API[/api/config]
LED_API[/api/led]
ADDR_API[/api/address]
IDENTIFY_API[/api/identify]
OTA_API[/update]
end
subgraph "Real-time Updates"
POLLING[1-second polling]
WEBSOCKET[WebSocket Connection]
AUTOREFRESH[Auto-refresh]
end
DASHBOARD --> STATE_API
MODULES --> STATE_API
MAPPING --> CONFIG_API
LED_TEST --> LED_API
OTA_UPDATE --> OTA_API
POLLING --> STATE_API
WEBSOCKET --> STATE_API
AUTOREFRESH --> CONFIG_API
```

**Diagram sources**
- [ota_webserver.cpp:183-190](file://src/ota_webserver.cpp#L183-L190)
- [ota_webserver.cpp:600-659](file://src/ota_webserver.cpp#L600-L659)

### Configuration Management Endpoints

The web interface provides RESTful APIs for comprehensive configuration management:

| Endpoint | Method | Purpose | Response |
|----------|--------|---------|----------|
| `/api/state` | GET | Real-time system state | JSON with joystick data, solenoid values, module info |
| `/api/config` | GET/POST | Motor configuration management | JSON array of axis configurations |
| `/api/led` | POST | LED color control | JSON success response |
| `/api/address` | POST | Address assignment | JSON success response |
| `/api/identify` | POST | Module identification | JSON success response |
| `/api/canoutput` | GET/POST | CAN output rules | JSON array of rule configurations |
| `/update` | POST | Firmware OTA update | Binary firmware update |

### Real-time Monitoring and Control

The web interface provides comprehensive real-time monitoring capabilities:

```mermaid
sequenceDiagram
participant BROWSER as Web Browser
participant SERVER as Web Server
participant CAN as CAN Bus
participant ECU as ECU Device
BROWSER->>SERVER : GET /api/state
SERVER->>CAN : Poll ECU State
CAN->>ECU : Request Heartbeat
ECU->>CAN : Respond with State
CAN->>SERVER : Return State Data
SERVER->>BROWSER : JSON State Response
loop Every 1 second
BROWSER->>SERVER : GET /api/state
SERVER->>BROWSER : Update Dashboard
end
BROWSER->>SERVER : POST /api/config
SERVER->>CAN : Broadcast Configuration
CAN->>ECU : Apply New Settings
ECU->>CAN : Confirm Changes
CAN->>SERVER : Acknowledge
SERVER->>BROWSER : Success Response
```

**Diagram sources**
- [ota_webserver.cpp:604-659](file://src/ota_webserver.cpp#L604-L659)
- [ota_webserver.cpp:683-722](file://src/ota_webserver.cpp#L683-L722)

**Section sources**
- [ota_webserver.cpp:1-933](file://src/ota_webserver.cpp#L1-L933)
- [web_state.h:1-23](file://src/web_state.h#L1-L23)
- [web_state.cpp:1-20](file://src/web_state.cpp#L1-L20)

## Safety and Defaults

The auto-configuration system implements comprehensive safety mechanisms and factory default configurations to ensure reliable operation and easy recovery from misconfigurations.

### Safety Mechanisms

The system incorporates multiple layers of safety protection:

```mermaid
flowchart TD
START([System Startup]) --> CHECK_CONFIG[Check Existing Configuration]
CHECK_CONFIG --> HAS_CONFIG{Has Valid Config?}
HAS_CONFIG --> |Yes| SKIP_AUTO[Skip Auto-Configuration]
HAS_CONFIG --> |No| APPLY_DEFAULTS[Apply Factory Defaults]
APPLY_DEFAULTS --> DEFAULT_MAPPING[Default Joystick-to-Solenoid Mapping]
DEFAULT_MAPPING --> SAVE_DEFAULTS[Save Defaults to NVS]
SKIP_AUTO --> INIT_HARDWARE[Initialize Hardware]
SAVE_DEFAULTS --> INIT_HARDWARE
INIT_HARDWARE --> SAFETY_CHECK[Safety Timeout Check]
SAFETY_CHECK --> TIMEOUT{Timeout Exceeded?}
TIMEOUT --> |Yes| SHUTDOWN_ALL[Shutdown All Solenoids]
TIMEOUT --> |No| CONTINUE_OPERATION[Continue Normal Operation]
SHUTDOWN_ALL --> SAFETY_CHECK
CONTINUE_OPERATION --> RUNTIME_MONITOR[Runtime Monitoring]
RUNTIME_MONITOR --> SAFETY_CHECK
```

**Diagram sources**
- [ecu_motor_driver.cpp:456-461](file://src/ecu_motor_driver.cpp#L456-L461)
- [ecu_motor_driver.cpp:330-359](file://src/ecu_motor_driver.cpp#L330-L359)

### Factory Default Configuration

The system provides intelligent factory defaults that establish sensible initial behavior:

| Component | Default Setting | Purpose |
|-----------|----------------|---------|
| PCA Count | 2 | Support dual PCA9685 expansion |
| Deadband Range | 472-552 | Prevent accidental solenoid activation |
| PWM Range | 50-200 | Safe operational range (20%-78%) |
| Bidirectional Axes | Enabled for first 4 axes | Primary joystick controls |
| Channel Mapping | Joy1 Pot1→Ch0+1, Joy1 Pot2→Ch2+3 | Standard vehicle control layout |
| Safety Timeout | 500ms | Immediate solenoid shutdown on communication loss |

### Recovery Mechanisms

The system includes multiple recovery mechanisms for fault tolerance:

```mermaid
stateDiagram-v2
[*] --> NormalOperation
NormalOperation --> ConfigurationUpdate : "User Request"
NormalOperation --> AddressChange : "Remote Config"
NormalOperation --> CommunicationLoss : "CAN Bus Error"
ConfigurationUpdate --> NormalOperation : "Success"
ConfigurationUpdate --> FactoryReset : "Invalid Config"
AddressChange --> NormalOperation : "Address Applied"
AddressChange --> FactoryReset : "Invalid Address"
CommunicationLoss --> SafetyShutdown : "Timeout > 500ms"
CommunicationLoss --> NormalOperation : "Bus Recovered"
SafetyShutdown --> NormalOperation : "System Restart"
FactoryReset --> NormalOperation : "Defaults Loaded"
note right of SafetyShutdown
All solenoids
shut down
immediately
end note
note right of FactoryReset
NVS cleared
defaults applied
end note
```

**Diagram sources**
- [ecu_motor_driver.cpp:456-479](file://src/ecu_motor_driver.cpp#L456-L479)
- [ForwarderConfig.cpp:171-183](file://lib/ForwarderConfig/ForwarderConfig.cpp#L171-L183)

**Section sources**
- [ecu_motor_driver.cpp:330-359](file://src/ecu_motor_driver.cpp#L330-L359)
- [ecu_motor_driver.cpp:456-479](file://src/ecu_motor_driver.cpp#L456-L479)
- [ForwarderConfig.cpp:171-183](file://lib/ForwarderConfig/ForwarderConfig.cpp#L171-L183)

## Implementation Patterns

The auto-configuration system demonstrates several advanced implementation patterns that contribute to its reliability and maintainability.

### Template Method Pattern

The system uses the template method pattern to provide a consistent interface across different ECU types while allowing specific implementations:

```mermaid
classDiagram
class ECUBase {
<<abstract>>
+setup() void*
+loop() void*
+processCAN() void*
+updateLED() void*
}
class MotorDriverECU {
+initPCA() void
+mapAxis() void
+allOff() void
+autoConfigDefaults() void
}
class JoystickECU {
+readInputs() void
+sendPot() bool
+sendButtons() bool
+sendHeartbeat() void
}
class ForwarderConfig {
+begin() bool
+loadMotorConfig() bool
+saveMotorConfig() bool
+loadDefaults() void
}
ECUBase <|-- MotorDriverECU
ECUBase <|-- JoystickECU
MotorDriverECU --> ForwarderConfig : "uses"
JoystickECU --> ForwarderConfig : "uses"
```

**Diagram sources**
- [ecu_motor_driver.h:1-5](file://src/ecu_motor_driver.h#L1-L5)
- [ecu_joystick.h:1-5](file://src/ecu_joystick.h#L1-L5)
- [ForwarderConfig.h:64-92](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L92)

### Factory Pattern for Configuration

The system implements a factory pattern for creating and managing configuration instances:

```mermaid
sequenceDiagram
participant APP as Application
participant FACTORY as ConfigFactory
participant MOTOR_CFG as MotorConfig
participant JOY_CFG as JoystickConfig
participant STORAGE as NVS Storage
APP->>FACTORY : createConfig(namespace)
FACTORY->>MOTOR_CFG : new MotorConfig()
FACTORY->>JOY_CFG : new JoystickConfig()
APP->>MOTOR_CFG : loadMotorConfig()
MOTOR_CFG->>STORAGE : getString("axis_%d")
MOTOR_CFG->>MOTOR_CFG : axis.unpack()
APP->>JOY_CFG : loadJoystickConfig()
JOY_CFG->>STORAGE : getString("joy_%d")
JOY_CFG->>JOY_CFG : joy.unpack()
APP->>MOTOR_CFG : saveMotorConfig()
MOTOR_CFG->>STORAGE : putBytes("axis_%d")
APP->>JOY_CFG : saveJoystickConfig()
JOY_CFG->>STORAGE : putBytes("joy_%d")
```

**Diagram sources**
- [ForwarderConfig.cpp:76-127](file://lib/ForwarderConfig/ForwarderConfig.cpp#L76-L127)
- [ForwarderConfig.cpp:129-169](file://lib/ForwarderConfig/ForwarderConfig.cpp#L129-L169)

### Observer Pattern for State Management

The web interface implements an observer pattern for real-time state updates:

```mermaid
graph LR
subgraph "State Publishers"
JOYSTICK_STATE[Joystick State]
MOTOR_STATE[Motor State]
CAN_STATE[CAN Bus State]
end
subgraph "State Observers"
DASHBOARD[Dashboard UI]
MODULES[Module Monitor]
ALERTS[Alert System]
LOGGING[Logging System]
end
subgraph "State Management"
STATE_MANAGER[State Manager]
POLLING_SERVICE[Polling Service]
WEBSOCKET_SERVICE[WebSocket Service]
end
JOYSTICK_STATE --> STATE_MANAGER
MOTOR_STATE --> STATE_MANAGER
CAN_STATE --> STATE_MANAGER
STATE_MANAGER --> POLLING_SERVICE
STATE_MANAGER --> WEBSOCKET_SERVICE
POLLING_SERVICE --> DASHBOARD
POLLING_SERVICE --> MODULES
POLLING_SERVICE --> ALERTS
WEBSOCKET_SERVICE --> DASHBOARD
WEBSOCKET_SERVICE --> MODULES
WEBSOCKET_SERVICE --> LOGGING
```

**Diagram sources**
- [web_state.h:10-23](file://src/web_state.h#L10-L23)
- [web_state.cpp:6-19](file://src/web_state.cpp#L6-L19)

**Section sources**
- [ecu_motor_driver.cpp:1-479](file://src/ecu_motor_driver.cpp#L1-L479)
- [ecu_joystick.cpp:1-281](file://src/ecu_joystick.cpp#L1-L281)
- [ForwarderConfig.cpp:1-184](file://lib/ForwarderConfig/ForwarderConfig.cpp#L1-L184)

## Troubleshooting Guide

The auto-configuration system includes comprehensive diagnostic capabilities and troubleshooting procedures to assist with common issues.

### Common Configuration Issues

| Issue | Symptoms | Diagnosis | Solution |
|-------|----------|-----------|----------|
| No Axis Configuration | Motors don't respond to joystick input | Check NVS storage for axis configurations | Run auto-configuration or restore defaults |
| Invalid Address Assignment | Device fails to respond on CAN bus | Verify address conflicts and NVS storage | Clear forced address and reassign |
| Communication Loss | Dashboard shows offline status | Check CAN bus connectivity and termination | Verify wiring and bus termination |
| Safety Shutdown | All solenoids deactivated | Check communication timeout and watchdog | Restore communication or reset device |

### Diagnostic Commands and Procedures

The system provides several diagnostic commands for troubleshooting:

```mermaid
flowchart TD
DIAG_START[Start Diagnostics] --> CHECK_NVS[Check NVS Storage]
CHECK_NVS --> NVS_OK{NVS Accessible?}
NVS_OK --> |Yes| LOAD_CONFIG[Load Configuration]
NVS_OK --> |No| FORMAT_NVS[Format NVS Storage]
LOAD_CONFIG --> VERIFY_CONFIG[Verify Configuration Integrity]
VERIFY_CONFIG --> CONFIG_OK{Configuration Valid?}
CONFIG_OK --> |Yes| INIT_HARDWARE[Initialize Hardware]
CONFIG_OK --> |No| APPLY_DEFAULTS[Apply Factory Defaults]
APPLY_DEFAULTS --> SAVE_DEFAULTS[Save Defaults to NVS]
SAVE_DEFAULTS --> INIT_HARDWARE
INIT_HARDWARE --> BUS_TEST[Perform Bus Tests]
BUS_TEST --> TEST_RESULT{Tests Passed?}
TEST_RESULT --> |Yes| SYSTEM_READY[System Ready]
TEST_RESULT --> |No| HARDWARE_DIAG[Hardware Diagnostics]
HARDWARE_DIAG --> CHECK_WIRING[Check Wiring Connections]
HARDWARE_DIAG --> REPLACE_COMPONENTS[Replace Faulty Components]
HARDWARE_DIAG --> SYSTEM_READY
```

**Diagram sources**
- [ecu_motor_driver.cpp:378-394](file://src/ecu_motor_driver.cpp#L378-L394)
- [ecu_joystick.cpp:205-215](file://src/ecu_joystick.cpp#L205-L215)

### Remote Diagnostics via Web Interface

The web interface provides comprehensive remote diagnostic capabilities:

```mermaid
sequenceDiagram
participant TECH as Technician
participant WEB as Web Interface
participant ECU as ECU Device
participant CAN as CAN Bus
participant LOG as Log System
TECH->>WEB : Open Diagnostics Tab
WEB->>ECU : Request System State
ECU->>CAN : Query CAN Status
CAN->>ECU : Return Bus Statistics
ECU->>WEB : Send Complete State Report
WEB->>TECH : Display Bus Statistics
WEB->>TECH : Show Error Counts
WEB->>TECH : Present Component Status
TECH->>WEB : Trigger Hardware Test
WEB->>ECU : Send Test Command
ECU->>ECU : Execute Self-Test
ECU->>WEB : Return Test Results
WEB->>TECH : Display Test Results
TECH->>WEB : View Logs
WEB->>LOG : Retrieve Log Entries
LOG->>WEB : Return Log Data
WEB->>TECH : Display Log Information
```

**Diagram sources**
- [ota_webserver.cpp:604-659](file://src/ota_webserver.cpp#L604-L659)
- [ecu_motor_driver.cpp:436-448](file://src/ecu_motor_driver.cpp#L436-L448)

**Section sources**
- [ecu_motor_driver.cpp:378-394](file://src/ecu_motor_driver.cpp#L378-L394)
- [ecu_joystick.cpp:205-215](file://src/ecu_joystick.cpp#L205-L215)
- [ota_webserver.cpp:604-659](file://src/ota_webserver.cpp#L604-L659)

## Conclusion

The Auto-Configuration System represents a sophisticated approach to embedded system configuration management, combining hardware abstraction, persistent storage, and user-friendly interfaces into a cohesive solution. The system's modular architecture, comprehensive safety mechanisms, and web-based management capabilities make it well-suited for complex industrial applications requiring reliable and flexible configuration management.

Key strengths of the system include its intelligent auto-configuration capabilities, robust persistence layer using NVS storage, comprehensive web interface for remote management, and extensive safety mechanisms ensuring reliable operation. The modular design allows for easy extension and modification while maintaining system stability and performance.

The implementation demonstrates best practices in embedded systems development, including proper resource management, error handling, and user experience design. The system's ability to recover from faults and provide comprehensive diagnostics makes it particularly valuable for field deployments where remote maintenance and troubleshooting are essential.

Future enhancements could include expanded configuration options, additional safety features, and integration with external monitoring systems. The solid foundation established by the current implementation provides an excellent platform for continued development and improvement.