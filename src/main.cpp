#include <Arduino.h>

// =============================================================================
// Forwarder CAN Controller - Main Application
// =============================================================================
// Build flags select the ECU type:
//   ECU_TYPE_MOTOR_DRIVER  -> Valve block controller with PCA9685
//   ECU_TYPE_JOYSTICK      -> Joystick controller with pots & buttons
// =============================================================================

#if defined(ECU_TYPE_MOTOR_DRIVER)
    #include "ecu_motor_driver.h"
#elif defined(ECU_TYPE_JOYSTICK)
    #include "ecu_joystick.h"
#else
    #error "No ECU_TYPE defined! Define ECU_TYPE_MOTOR_DRIVER or ECU_TYPE_JOYSTICK"
#endif

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n============================================");
    Serial.println("  Forwarder CAN Controller Starting...");
    Serial.println("============================================");

    ecu_setup();
}

void loop() {
    ecu_loop();
}
