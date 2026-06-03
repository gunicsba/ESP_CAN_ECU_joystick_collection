#pragma once

#include "ForwarderCAN.h"

void ota_setup(const char* hostname);
void ota_loop();
bool ota_is_active();
void ota_trackModule(uint8_t sa, const CANMessage& msg);
