#pragma once

#include <Arduino.h>
#include "ForwarderCAN.h"
#include "ForwarderConfig.h"

void can_output_setup(CanOutputRule rules[MAX_CAN_OUTPUT_RULES]);
void can_output_process_can(CANMessage& msg);
void can_output_loop();
CanOutputRule* can_output_get_rules();
