#pragma once

#include <Arduino.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Axis configuration for joystick-to-solenoid mapping
// ---------------------------------------------------------------------------
// Packed into 8 bytes for CAN transport:
// [0] = axis_idx (0-15)
// [1] = source_address (joystick SA, e.g. 0x21)
// [2] = (pot_idx << 6) | (output_channel << 2) | flags
// [3] = deadband_min / 4  (0-255 maps to 0-1020)
// [4] = deadband_max / 4  (0-255 maps to 0-1020)
// [5] = pwm_min (0-255, scaled to 12-bit on output)
// [6] = pwm_max (0-255)
// [7] = reserved
// ---------------------------------------------------------------------------

#define FLAG_AXIS_ENABLED       0x01
#define FLAG_AXIS_BIDIRECTIONAL 0x02

#define MAX_AXIS_COUNT          16
#define MAX_PCA_COUNT           2
#define CHANNELS_PER_PCA        8
#define MAX_CAN_OUTPUT_RULES    4

// CAN-triggered GPIO output rule
struct CanOutputRule {
    bool     enabled;
    uint8_t  matchPF;       // PDU Format to match (required)
    uint8_t  matchSA;       // Source address to match (0 = any)
    uint8_t  gpioPin;       // Output GPIO pin
    uint8_t  mode;          // 0 = toggle, 1 = momentary
    uint16_t momentaryMs;   // Timeout for momentary mode (ms)

    void pack(uint8_t buf[8]) const;
    void unpack(const uint8_t buf[8]);
};

struct AxisConfig {
    uint8_t sourceAddress;   // Joystick source address (0x21, 0x22...)
    uint8_t potIndex;        // 0=Pot1, 1=Pot2, 2=Pot3
    uint8_t outputChannel;   // 0-15 (2x PCA9685)
    uint16_t deadbandMin;    // ADC raw 0-1023
    uint16_t deadbandMax;    // ADC raw 0-1023
    uint8_t pwmMin;          // 0-255 -> 0-4095
    uint8_t pwmMax;          // 0-255 -> 0-4095
    uint8_t flags;           // FLAG_AXIS_ENABLED, FLAG_AXIS_BIDIRECTIONAL

    bool isEnabled() const { return flags & FLAG_AXIS_ENABLED; }
    bool isBidirectional() const { return flags & FLAG_AXIS_BIDIRECTIONAL; }

    void pack(uint8_t buf[8], uint8_t axisIdx) const;
    void unpack(const uint8_t buf[8]);
    static uint8_t packedSize() { return 8; }
};

struct MotorConfig {
    AxisConfig axes[MAX_AXIS_COUNT];
    uint8_t pcaCount = MAX_PCA_COUNT;  // 1 or 2
};

class ForwarderConfig {
public:
    ForwarderConfig(const char* ns = "fwdrcfg");

    bool begin();

    // Address override (stored in NVS)
    uint8_t getForcedAddress(uint8_t defaultAddr);
    void setForcedAddress(uint8_t addr);
    void clearForcedAddress();

    // Motor mapping config
    bool loadMotorConfig(MotorConfig& cfg);
    bool saveMotorConfig(const MotorConfig& cfg);
    bool saveAxisConfig(uint8_t axisIdx, const AxisConfig& axis);

    // CAN output rules
    bool loadCanOutputRules(CanOutputRule rules[MAX_CAN_OUTPUT_RULES]);
    bool saveCanOutputRule(uint8_t index, const CanOutputRule& rule);

    // Factory defaults
    void loadDefaults(MotorConfig& cfg);

private:
    const char* _ns;
    Preferences _prefs;
    bool _started = false;
};
