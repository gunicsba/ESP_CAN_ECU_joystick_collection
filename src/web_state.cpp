#include "web_state.h"

// Provide default definitions for globals that are only defined
// in the ECU file that is NOT being compiled in this build.

#if !defined(ECU_TYPE_MOTOR_DRIVER)
uint16_t g_joyPots[256][3] = {{0}};
uint32_t g_joyUpdateTime[256] = {0};
uint16_t g_solenoidValues[MAX_AXIS_COUNT] = {0};
MotorConfig g_motorCfg;
bool g_pca2Present = false;
CanOutputRule g_canOutputRules[MAX_CAN_OUTPUT_RULES];
#endif

#if !defined(ECU_TYPE_JOYSTICK)
uint16_t g_localPot1 = 512, g_localPot2 = 512;
bool g_localBtn1 = false, g_localBtn2 = false;
uint8_t g_ecuJoystickId = 0;
#endif
