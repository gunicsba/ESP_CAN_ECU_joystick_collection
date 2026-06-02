# Configuration Management

<cite>
**Referenced Files in This Document**
- [README.md](file://README.md)
- [platformio.ini](file://platformio.ini)
- [ForwarderConfig.h](file://lib/ForwarderConfig/ForwarderConfig.h)
- [ForwarderConfig.cpp](file://lib/ForwarderConfig/ForwarderConfig.cpp)
- [can_output.h](file://src/can_output.h)
- [can_output.cpp](file://src/can_output.cpp)
- [web_state.h](file://src/web_state.h)
- [web_state.cpp](file://src/web_state.cpp)
- [ota_webserver.h](file://src/ota_webserver.h)
- [ota_webserver.cpp](file://src/ota_webserver.cpp)
- [ecu_motor_driver.h](file://src/ecu_motor_driver.h)
- [ecu_motor_driver.cpp](file://src/ecu_motor_driver.cpp)
- [ecu_joystick.h](file://src/ecu_joystick.h)
- [ecu_joystick.cpp](file://src/ecu_joystick.cpp)
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
10. [Appendices](#appendices)

## Introduction
This document describes the configuration management system for ForwarderKE, focusing on persistent storage and device settings. It explains how NVS (Non-Volatile Storage) is used to persist device configurations, axis mappings, and operational parameters. It documents the configuration data structures, default loading, validation, persistence across reboots, and the web interface integration for configuration management and synchronization between ECUs.

## Project Structure
The configuration system spans shared libraries and per-ECU modules:
- Shared configuration library defines data structures and NVS-backed persistence APIs.
- Motor driver and joystick ECUs consume these structures and integrate with CAN and hardware.
- Web server and state expose runtime configuration for UI and OTA updates.

```mermaid
graph TB
subgraph "Shared"
CFG["ForwarderConfig<br/>NVS-backed config"]
CANLIB["ForwarderCAN<br/>J1939-like CAN"]
end
subgraph "Motor Driver ECU"
MD["ecu_motor_driver.cpp"]
CO["can_output.cpp"]
WS["web_state.cpp"]
end
subgraph "Joystick ECU"
JS["ecu_joystick.cpp"]
end
subgraph "Web"
OTA["ota_webserver.cpp"]
end
CFG --> MD
CFG --> JS
CFG --> CO
CFG --> WS
CANLIB --> MD
CANLIB --> JS
OTA --> WS
WS --> CO
```

**Diagram sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)
- [can_output.h:1-11](file://src/can_output.h#L1-L11)
- [web_state.h:8-23](file://src/web_state.h#L8-L23)
- [ota_webserver.h](file://src/ota_webserver.h)

**Section sources**
- [README.md:112-126](file://README.md#L112-L126)
- [platformio.ini:1-80](file://platformio.ini#L1-L80)

## Core Components
- ForwarderConfig: NVS-backed configuration manager providing:
  - Address override persistence
  - Motor mapping configuration (AxisConfig arrays)
  - CAN output rules (CanOutputRule)
  - Factory defaults loader
- Data structures:
  - AxisConfig: joystick-to-solenoid mapping with deadbands, PWM scaling, and flags
  - MotorConfig: container for up to 16 axes and PCA count
  - CanOutputRule: CAN-triggered GPIO output rules with matching and timing modes
- Runtime state:
  - web_state exposes shared state for web UI and OTA, including current motor config and CAN output rules

Key responsibilities:
- Persist and load configuration across reboots using NVS
- Provide safe defaults and validation helpers
- Enable real-time updates via web UI and synchronize with CAN

**Section sources**
- [ForwarderConfig.h:28-91](file://lib/ForwarderConfig/ForwarderConfig.h#L28-L91)
- [web_state.h:8-23](file://src/web_state.h#L8-L23)

## Architecture Overview
The configuration lifecycle integrates NVS persistence, CAN transport, and web UI:

```mermaid
sequenceDiagram
participant NVS as "NVS (Preferences)"
participant CFG as "ForwarderConfig"
participant MD as "Motor Driver"
participant JS as "Joystick"
participant CAN as "CAN Bus"
participant WEB as "Web UI"
Note over NVS,CFG : On boot, load defaults and NVS-persisted values
CFG->>NVS : loadDefaults(cfg)
CFG->>NVS : loadMotorConfig(cfg)
CFG->>NVS : loadCanOutputRules(rules)
Note over MD,JS : Runtime updates
WEB->>MD : Apply new MotorConfig / Rules
MD->>CFG : saveMotorConfig(cfg)
MD->>CFG : saveCanOutputRule(i, rule)
CFG->>NVS : commit to NVS
Note over CAN,MD : Synchronize across ECUs
CAN->>MD : Receive CAN messages
MD->>MD : Update solenoid outputs
MD->>WEB : Publish state for UI
```

**Diagram sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)
- [ForwarderConfig.cpp](file://lib/ForwarderConfig/ForwarderConfig.cpp)
- [web_state.h:8-23](file://src/web_state.h#L8-L23)
- [can_output.h:7-11](file://src/can_output.h#L7-L11)

## Detailed Component Analysis

### ForwarderConfig: NVS-backed Configuration Manager
Responsibilities:
- Initialize NVS namespace and manage lifecycle
- Load/save address override
- Load/save MotorConfig and individual AxisConfig entries
- Load/save CAN output rules
- Provide factory defaults loader

Data structures and packing:
- AxisConfig is packed into 8 bytes for CAN transport, including axis index, source address, flags, and scaling parameters.
- CanOutputRule stores matching criteria and GPIO behavior.

Validation and safety:
- Defaults loader sets sane initial values for axes and rules.
- Flags and scaling bounds are validated during packing/unpacking and runtime updates.

Persistence model:
- Uses Arduino Preferences for NVS-backed storage under a dedicated namespace.
- Separate keys for address override, motor config, and CAN output rules.

```mermaid
classDiagram
class ForwarderConfig {
-const char* _ns
-Preferences _prefs
-bool _started
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
+AxisConfig axes[16]
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
ForwarderConfig --> MotorConfig : "loads/saves"
ForwarderConfig --> AxisConfig : "manages"
ForwarderConfig --> CanOutputRule : "loads/saves"
MotorConfig --> AxisConfig : "contains"
```

**Diagram sources**
- [ForwarderConfig.h:28-91](file://lib/ForwarderConfig/ForwarderConfig.h#L28-L91)

**Section sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)

### AxisConfig: Joystick-to-Solenoid Mapping
Purpose:
- Maps joystick inputs to solenoid outputs with configurable deadbands and PWM scaling.
- Supports enabling/disabling and bidirectional operation flags.

Packing for CAN:
- Encodes axis index, source address, flags, and scaling parameters into 8 bytes for compact transport.

Runtime usage:
- Motor driver reads current axis mapping and applies scaled PWM to PCA channels.
- Web UI displays and edits axis mappings in real time.

Validation:
- Deadband min/max must be within ADC range and respect ordering.
- PWM min/max scale to 12-bit output; out-of-range values are clamped by packing/unpacking routines.

**Section sources**
- [ForwarderConfig.h:41-57](file://lib/ForwarderConfig/ForwarderConfig.h#L41-L57)

### MotorConfig: Driver Settings Container
Purpose:
- Aggregates axis mappings and PCA configuration.
- Tracks number of PCA9685 boards present.

Usage:
- Loaded from NVS at boot and updated via web UI.
- Applied to motor driver logic to control solenoids.

**Section sources**
- [ForwarderConfig.h:59-62](file://lib/ForwarderConfig/ForwarderConfig.h#L59-L62)

### CanOutputRule: GPIO Control Rules
Purpose:
- Define GPIO actions triggered by incoming CAN messages.
- Support toggling and momentary modes with timeouts.

Matching:
- Match on PDU Format (PF) and optional Source Address (SA).
- GPIO pin and mode selection enable flexible external control.

Persistence:
- Stored in NVS and synchronized across ECUs via CAN.

**Section sources**
- [ForwarderConfig.h:28-39](file://lib/ForwarderConfig/ForwarderConfig.h#L28-L39)

### NVS Implementation and Persistence
- Namespace: Uses a dedicated NVS namespace for ForwarderKE configuration.
- Keys:
  - Address override
  - Motor configuration (serialized array of axes)
  - CAN output rules (array of rules)
- Commit model: Writes are committed on explicit save calls.
- Boot flow: Defaults loaded first, then NVS overrides applied.

Safety:
- If NVS is uninitialized or corrupted, defaults are used.
- Packing/unpacking routines validate ranges and flags.

**Section sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)
- [ForwarderConfig.cpp](file://lib/ForwarderConfig/ForwarderConfig.cpp)

### Default Configuration Loading and Validation
- Factory defaults:
  - Initialize all axes disabled with neutral deadbands and mid-range PWM.
  - Initialize CAN output rules as disabled.
- Validation:
  - During packing/unpacking, enforce bounds and flags.
  - Runtime checks ensure axis indices and channel assignments are valid.

Fallback mechanism:
- On missing or invalid NVS entries, defaults are applied transparently.

**Section sources**
- [ForwarderConfig.h:84-86](file://lib/ForwarderConfig/ForwarderConfig.h#L84-L86)

### Web Interface Integration and Real-Time Updates
- Shared state:
  - web_state exposes global buffers for joystick readings, solenoid values, motor config, PCA presence, CAN pointer, and CAN output rules.
- OTA web server:
  - Provides upload endpoint and UI for firmware and configuration updates.
- Real-time updates:
  - Web UI can push new MotorConfig and CanOutputRule updates.
  - Motor driver saves to NVS and applies immediately.
  - State published for UI refresh.

```mermaid
sequenceDiagram
participant UI as "Web UI"
participant OTA as "ota_webserver.cpp"
participant WS as "web_state.cpp"
participant CFG as "ForwarderConfig"
participant NVS as "NVS"
participant MD as "ecu_motor_driver.cpp"
UI->>OTA : Submit new MotorConfig / Rules
OTA->>WS : Update shared state
WS->>CFG : saveMotorConfig(cfg)
CFG->>NVS : commit
WS->>CFG : saveCanOutputRule(i, rule)
CFG->>NVS : commit
CFG-->>MD : Updated config available
MD-->>UI : Publish state for UI refresh
```

**Diagram sources**
- [web_state.h:8-23](file://src/web_state.h#L8-L23)
- [ota_webserver.h](file://src/ota_webserver.h)
- [ForwarderConfig.h:75-83](file://lib/ForwarderConfig/ForwarderConfig.h#L75-L83)

**Section sources**
- [web_state.h:8-23](file://src/web_state.h#L8-L23)
- [ota_webserver.cpp](file://src/ota_webserver.cpp)

### Configuration Synchronization Between ECUs
- CAN transport:
  - Motor driver receives CAN commands and updates solenoid outputs.
  - Joystick ECUs publish joystick data; motor driver consumes and reflects state.
- Rule propagation:
  - CAN output rules can trigger GPIO actions on any ECU; rules are stored locally and can be mirrored via web UI.

**Section sources**
- [README.md:29-41](file://README.md#L29-L41)
- [can_output.h:7-11](file://src/can_output.h#L7-L11)

### Practical Examples

- Modify axis mapping:
  - Use web UI to select joystick source, adjust deadbands, PWM range, and flags.
  - Save; NVS persists the change; motor driver applies immediately.
  - Verify by observing solenoid response on PCA outputs.

- Configure CAN output rules:
  - Set PF/SA match, GPIO pin, and mode (toggle/momentary).
  - Optionally set momentary timeout.
  - Save and test by sending matching CAN frames.

- Validate configuration:
  - Confirm axis indices and channel assignments are within limits.
  - Ensure deadband min ≤ deadband max and PWM ranges map to 12-bit output.

- Troubleshoot configuration issues:
  - If settings revert after reboot, check NVS initialization and write permissions.
  - If rules have no effect, verify PF/SA matching and GPIO pin availability.

[No sources needed since this section provides practical guidance]

## Dependency Analysis
- ForwarderConfig depends on Preferences/NVS for persistence and is consumed by motor driver and web state.
- can_output module depends on ForwarderConfig for CAN output rules and on web_state for runtime state.
- web_state aggregates configuration and CAN state for the UI.
- OTA web server interacts with web_state to apply configuration updates.

```mermaid
graph LR
CFG["ForwarderConfig"] --> MD["ecu_motor_driver.cpp"]
CFG --> CO["can_output.cpp"]
CFG --> WS["web_state.cpp"]
WS --> CO
OTA["ota_webserver.cpp"] --> WS
CANLIB["ForwarderCAN"] --> MD
CANLIB --> JS["ecu_joystick.cpp"]
```

**Diagram sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)
- [can_output.h:1-11](file://src/can_output.h#L1-L11)
- [web_state.h:8-23](file://src/web_state.h#L8-L23)
- [ota_webserver.h](file://src/ota_webserver.h)

**Section sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)
- [can_output.h:1-11](file://src/can_output.h#L1-L11)
- [web_state.h:8-23](file://src/web_state.h#L8-L23)

## Performance Considerations
- NVS writes are relatively slow; batch configuration updates and avoid excessive save calls.
- CAN packing/unpacking is lightweight but should still be performed only when necessary.
- Keep the number of CAN output rules reasonable to minimize processing overhead.
- Prefer incremental updates via web UI rather than full reloads.

[No sources needed since this section provides general guidance]

## Troubleshooting Guide
Common issues and resolutions:
- Settings reset after reboot:
  - Verify NVS namespace exists and Preferences operations succeed.
  - Ensure save calls are invoked after updates.
- Axes not responding:
  - Check axis is enabled and within valid channel range.
  - Confirm deadbands and PWM ranges are sensible.
- CAN output rules inactive:
  - Verify PF/SA match criteria and GPIO pin assignment.
  - Confirm rule is enabled and mode matches intended behavior.
- OTA update fails:
  - Confirm Wi-Fi AP connectivity and upload endpoint accessibility.
  - Re-attempt upload and monitor serial logs.

**Section sources**
- [ForwarderConfig.h:64-91](file://lib/ForwarderConfig/ForwarderConfig.h#L64-L91)

## Conclusion
ForwarderKE’s configuration management combines NVS-backed persistence with clear data structures for axis mapping and CAN-triggered GPIO rules. Defaults ensure safe operation, while the web interface enables real-time updates and synchronization across ECUs. Robust validation and fallback mechanisms improve reliability, and OTA support simplifies deployment and maintenance.

[No sources needed since this section summarizes without analyzing specific files]

## Appendices

### Configuration Data Structures Reference
- AxisConfig
  - Fields: sourceAddress, potIndex, outputChannel, deadbandMin, deadbandMax, pwmMin, pwmMax, flags
  - Flags: enabled, bidirectional
  - Packing: 8 bytes for CAN transport
- MotorConfig
  - Fields: axes[16], pcaCount
- CanOutputRule
  - Fields: enabled, matchPF, matchSA, gpioPin, mode, momentaryMs
  - Modes: toggle, momentary

**Section sources**
- [ForwarderConfig.h:28-62](file://lib/ForwarderConfig/ForwarderConfig.h#L28-L62)

### Build and Environment Notes
- Build flags define ECU type, preferred address, pins, and OTA enablement.
- Environments: motor_driver, joystick1, joystick2, and OTA variants.

**Section sources**
- [platformio.ini:17-79](file://platformio.ini#L17-L79)