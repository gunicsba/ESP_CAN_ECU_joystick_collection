#pragma once

#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "can_output.h"
#include <Arduino.h>

// Shared state exposed for the web UI / API

// Motor driver state
extern uint16_t g_joyPots[256][4];
extern uint8_t g_joyButtons[256];
extern uint32_t g_joyUpdateTime[256];
extern uint32_t g_joyButtonUpdateTime[256];
extern uint8_t g_extButtons0[256];
extern uint8_t g_extButtons1[256];
extern uint32_t g_extButtonUpdateTime[256];
extern uint16_t g_solenoidValues[MAX_AXIS_COUNT];
extern MotorConfig g_motorCfg;
extern bool g_pca2Present;
extern ForwarderCAN *g_can;
extern ForwarderConfig cfgMgr;
extern CanOutputRule g_canOutputRules[MAX_CAN_OUTPUT_RULES];
extern ButtonOutputRule g_btnOutputRules[MAX_BUTTON_OUTPUT_RULES];
extern CustomCanButton g_customCanButtons[MAX_CUSTOM_CAN_BUTTONS];
extern JoystickLabel g_joyLabels[MAX_JOYSTICK_LABELS];
extern OutputLabel g_outLabels[MAX_OUTPUT_LABELS];
extern bool g_customBtnStates[MAX_CUSTOM_CAN_BUTTONS];

// Joystick local state (for when running on joystick ECU)
extern uint16_t g_localPot1, g_localPot2, g_localPot3, g_localPot4;
extern bool g_localBtn1, g_localBtn2, g_localBtn3, g_localBtn4;
extern uint8_t g_ecuJoystickId;
