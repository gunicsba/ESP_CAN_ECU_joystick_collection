#pragma once

#include <stdint.h>

void ecu_setup();
void ecu_loop();

// Motor test mode - allows manual control of outputs via web UI
extern bool g_testMode;
extern uint16_t g_testValues[16];
extern uint32_t g_lastTestCmd;
