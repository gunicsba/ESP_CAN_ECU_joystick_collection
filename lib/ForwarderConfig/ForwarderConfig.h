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
// [7] = button_gate (0=none, 1=BTN1 pressed, 2=BTN1 released)
// ---------------------------------------------------------------------------

// Button gate modes for axis activation based on joystick button state
#define BUTTON_GATE_NONE 0          // Always active (no gating)
#define BUTTON_GATE_BTN1_PRESSED 1  // Active only when BTN1 is pressed
#define BUTTON_GATE_BTN1_RELEASED 2 // Active only when BTN1 is NOT pressed

#define FLAG_AXIS_ENABLED 0x01
#define FLAG_AXIS_BIDIRECTIONAL 0x02
#define FLAG_AXIS_INVERT 0x04

#define MAX_AXIS_COUNT 16
#define MAX_PCA_COUNT 2
#define CHANNELS_PER_PCA 8
#define MAX_CAN_OUTPUT_RULES 4
#define MAX_JOYSTICK_LABELS 4
#define MAX_OUTPUT_LABELS 16
#define MAX_BUTTON_OUTPUT_RULES 16
#define MAX_CUSTOM_CAN_BUTTONS 8

// CAN-triggered GPIO output rule
struct CanOutputRule {
  bool enabled;
  uint8_t matchPF;      // PDU Format to match (required)
  uint8_t matchSA;      // Source address to match (0 = any)
  uint8_t gpioPin;      // Output GPIO pin
  uint8_t mode;         // 0 = toggle, 1 = momentary
  uint16_t momentaryMs; // Timeout for momentary mode (ms)

  void pack(uint8_t buf[8]) const;
  void unpack(const uint8_t buf[8]);
};

struct AxisConfig {
  uint8_t sourceAddress; // Joystick source address (0x21, 0x22...)
  uint8_t potIndex;      // 0=Pot1, 1=Pot2, 2=Pot3
  uint8_t outputChannel; // 0-15 (2x PCA9685)
  uint16_t deadbandMin;  // ADC raw 0-1023
  uint16_t deadbandMax;  // ADC raw 0-1023
  uint8_t pwmMin;        // 0-255 -> 0-4095
  uint8_t pwmMax;        // 0-255 -> 0-4095
  uint8_t flags;         // FLAG_AXIS_ENABLED, FLAG_AXIS_BIDIRECTIONAL
  uint8_t buttonGate;    // BUTTON_GATE_NONE, BUTTON_GATE_BTN1_PRESSED,
                         // BUTTON_GATE_BTN1_RELEASED
  uint8_t curveExp; // Exponent * 2: 2=linear(1.0), 3=1.5, 4=2.0, 5=2.5, 6=3.0

  bool isEnabled() const { return flags & FLAG_AXIS_ENABLED; }
  bool isBidirectional() const { return flags & FLAG_AXIS_BIDIRECTIONAL; }
  bool isInverted() const { return flags & FLAG_AXIS_INVERT; }

  void pack(uint8_t buf[8], uint8_t axisIdx) const;
  void unpack(const uint8_t buf[8]);
  static uint8_t packedSize() { return 8; }
};

struct MotorConfig {
  AxisConfig axes[MAX_AXIS_COUNT];
  uint8_t pcaCount = MAX_PCA_COUNT; // 1 or 2
};

// ---------------------------------------------------------------------------
// Label structures for user-friendly UI mapping
// ---------------------------------------------------------------------------
struct JoystickLabel {
  uint8_t sourceAddress;  // 0x21, 0x22, etc.
  char position[8];       // "left", "right", etc.
  char axisLabels[4][12]; // Label per axis: "fore", "side", etc.
};

struct OutputLabel {
  uint8_t channel; // 0-15
  char label[16];  // User-defined name
};

// ---------------------------------------------------------------------------
// Button-to-output rule: press a joystick button to drive PWM output
// ---------------------------------------------------------------------------
struct ButtonOutputRule {
  bool enabled;
  uint8_t outputChannel; // PWM output channel (0-15)
  uint8_t btnSourceSA;   // Joystick source address (0x21, 0x22...)
  uint8_t btnIndex;      // Button index 0-3
  uint8_t btnMode;       // 0=maxPWM when pressed, 1=minPWM when pressed
  uint8_t pwmTarget;     // PWM value 0-255

  void pack(uint8_t buf[8]) const;
  void unpack(const uint8_t buf[8]);
};

// ---------------------------------------------------------------------------
// Custom CAN button: trigger from any CAN message on the bus
// ---------------------------------------------------------------------------
struct CustomCanButton {
  bool enabled;
  uint32_t canId;    // Full 29-bit CAN ID to match
  uint8_t byteIndex; // Data byte 0-7
  uint8_t bitIndex;  // Bit 0-7 within that byte
  char name[16];     // Pretty name for UI

  void pack(uint8_t buf[8]) const;
  void unpack(const uint8_t buf[8]);
};

class ForwarderConfig {
public:
  ForwarderConfig(const char *ns = "fwdrcfg");

  bool begin();

  // Address override (stored in NVS)
  uint8_t getForcedAddress(uint8_t defaultAddr);
  void setForcedAddress(uint8_t addr);
  void clearForcedAddress();

  // Motor mapping config
  bool loadMotorConfig(MotorConfig &cfg);
  bool saveMotorConfig(const MotorConfig &cfg);
  bool saveAxisConfig(uint8_t axisIdx, const AxisConfig &axis);

  // CAN output rules
  bool loadCanOutputRules(CanOutputRule rules[MAX_CAN_OUTPUT_RULES]);
  bool saveCanOutputRule(uint8_t index, const CanOutputRule &rule);

  // Labels
  bool loadJoystickLabels(JoystickLabel labels[MAX_JOYSTICK_LABELS]);
  bool saveJoystickLabel(uint8_t index, const JoystickLabel &label);
  bool loadOutputLabels(OutputLabel labels[MAX_OUTPUT_LABELS]);
  bool saveOutputLabel(uint8_t index, const OutputLabel &label);

  // Button output rules
  bool loadButtonOutputRules(ButtonOutputRule rules[MAX_BUTTON_OUTPUT_RULES]);
  bool saveButtonOutputRule(uint8_t index, const ButtonOutputRule &rule);

  // Custom CAN buttons
  bool loadCustomCanButtons(CustomCanButton buttons[MAX_CUSTOM_CAN_BUTTONS]);
  bool saveCustomCanButton(uint8_t index, const CustomCanButton &button);

  // Factory defaults
  void loadDefaults(MotorConfig &cfg);

private:
  const char *_ns;
  Preferences _prefs;
  bool _started = false;
};
