#pragma once

#include <Arduino.h>
#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "can_output.h"

// Shared state exposed for the web UI / API

// Motor driver state
extern uint16_t g_joyPots[256][3];
extern uint32_t g_joyUpdateTime[256];
extern uint16_t g_solenoidValues[MAX_AXIS_COUNT];
extern MotorConfig g_motorCfg;
extern bool g_pca2Present;
extern ForwarderCAN* g_can;
extern CanOutputRule g_canOutputRules[MAX_CAN_OUTPUT_RULES];

// Joystick local state (for when running on joystick ECU)
extern uint16_t g_localPot1, g_localPot2;
extern bool g_localBtn1, g_localBtn2;
extern uint8_t g_ecuJoystickId;
