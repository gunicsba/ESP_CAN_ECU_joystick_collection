#pragma once

#include "ForwarderConfig.h"
#include <stdint.h>

void ecu_setup();
void ecu_loop();

// Motor test mode - allows manual control of outputs via web UI
extern bool g_testMode;
extern uint16_t g_testValues[16];
extern uint32_t g_lastTestCmd;

// Joystick command interface (from web UI)
extern bool g_joyCmdActive[MAX_JOY_FUNCTIONS];
extern JoystickOutputMapping g_joyMappings[MAX_JOY_FUNCTIONS];
extern uint32_t g_lastJoyCmd;
