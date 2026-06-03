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
#if ARDUINO_USB_CDC_ON_BOOT
    // Wait for USB-CDC enumeration
    unsigned long startWait = millis();
    while (!Serial && millis() - startWait < 3000) { delay(10); }
#else
    delay(500);
#endif
    Serial.println("\n============================================");
    Serial.println("  Forwarder CAN Controller Starting...");
    Serial.println("============================================");

    ecu_setup();
}

void loop() {
    ecu_loop();
    yield();
}
