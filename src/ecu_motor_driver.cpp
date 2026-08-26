#include "ecu_motor_driver.h"

#if defined(ECU_TYPE_MOTOR_DRIVER)

#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "WT5500Eth.h"
#include "can_output.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ota_webserver.h"
#include "web_state.h"
#include <Adafruit_PWMServoDriver.h>
#include <NeoPixelBus.h>
#include <Wire.h>
#include "esp_task_wdt.h"

#ifndef CAN_TX_PIN
#define CAN_TX_PIN 5
#endif
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 4
#endif
#ifndef PCA9685_SDA
#define PCA9685_SDA 21
#endif
#ifndef PCA9685_SCL
#define PCA9685_SCL 22
#endif
#ifndef PCA9685_I2C_ADDR1
#define PCA9685_I2C_ADDR1 0x40
#endif
#ifndef PCA9685_I2C_ADDR2
#define PCA9685_I2C_ADDR2 0x41
#endif
#ifndef SAFETY_TIMEOUT_MS
#define SAFETY_TIMEOUT_MS 500
#endif
// Task watchdog: auto-reset if loop() wedges (I2C hang, etc.)
#ifndef WATCHDOG_TIMEOUT_S
#define WATCHDOG_TIMEOUT_S 8
#endif
#ifndef WS2812_PIN
#define WS2812_PIN 48
#endif
#ifndef OUTPUT_ACTIVE_PIN
#define OUTPUT_ACTIVE_PIN                                                      \
  4 // GPIO4: HIGH when any output channel is active  (F1 output green led)
#endif

// WT5500 SPI Ethernet pins (matching Arduino sample)
#define WT5500_MISO 37
#define WT5500_MOSI 35
#define WT5500_SCLK 36
#define WT5500_CS 38
#define WT5500_INT 45
#define WT5500_RST 48

// UDP ports for AgOpenGPS communication
#define UDP_AOG_PORT 8888
#define UDP_AGIO_PORT 9999

static Adafruit_PWMServoDriver pca1 =
    Adafruit_PWMServoDriver(PCA9685_I2C_ADDR1);
static Adafruit_PWMServoDriver pca2 =
    Adafruit_PWMServoDriver(PCA9685_I2C_ADDR2);
bool g_pca1Present = false;
bool g_pca2Present = false;

static NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(1, WS2812_PIN);
ForwarderCAN *g_can = nullptr;
ForwarderConfig cfgMgr("motorcfg");

MotorConfig g_motorCfg;
uint16_t g_solenoidValues[MAX_AXIS_COUNT] = {0};
static uint32_t lastSolenoidUpdate = 0;
static uint32_t lastHeartbeat = 0;
static bool g_outputDirty = false;
static uint32_t lastOutputBroadcast = 0;
static uint32_t lastLedUpdate = 0;

// Motor test mode - allows manual control of outputs via web UI
bool g_testMode = false;
uint16_t g_testValues[16] = {0}; // Manual test values for outputs 1-16
uint32_t g_lastTestCmd = 0;      // Auto-disable after 10s timeout

static uint8_t ledR = 0, ledG = 0, ledB = 20;
static bool blinkFast = false;
static uint32_t blinkTimer = 0;
static uint32_t identifyTimer = 0;
static bool identifyActive = false;

uint16_t g_joyPots[256][4] = {{0}};
uint8_t g_joyButtons[256] = {0};
uint8_t g_extButtons0[256] = {0}; // Extender PCA9555 port 0 buttons
uint8_t g_extButtons1[256] = {0}; // Extender PCA9555 port 1 buttons
uint32_t g_joyUpdateTime[256] = {0};
uint32_t g_joyButtonUpdateTime[256] = {0};
uint32_t g_extButtonUpdateTime[256] = {0};
CanOutputRule g_canOutputRules[MAX_CAN_OUTPUT_RULES];
ButtonOutputRule g_btnOutputRules[MAX_BUTTON_OUTPUT_RULES];
CustomCanButton g_customCanButtons[MAX_CUSTOM_CAN_BUTTONS];
JoystickLabel g_joyLabels[MAX_JOYSTICK_LABELS];
OutputLabel g_outLabels[MAX_OUTPUT_LABELS];
bool g_customBtnStates[MAX_CUSTOM_CAN_BUTTONS] = {false};

// Ethernet state
static bool g_ethConnected = false;
// lwIP UDP sockets for Ethernet
static int g_sockAOG = -1;  // Socket for AOG (port 8888)
static int g_sockAGIO = -1; // Socket for AGIO (port 9999)

// Joystick command state (from web UI)
bool g_joyCmdActive[MAX_JOY_FUNCTIONS] = {false};
JoystickOutputMapping g_joyMappings[MAX_JOY_FUNCTIONS];
uint32_t g_lastJoyCmd = 0;
// Tracks which output channels are being driven by web joy buttons
// (so updateAxes skips them to prevent flickering)
bool g_joyOverrideChannels[16] = {false};
// Channels currently driven by Button Mapping rules (rules win over axes
// while the mapped button is held)
static bool g_ruleDrivenChannels[16] = {false};

static const uint8_t ECU_NAME[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, (ECU_NAME_MOTOR_DRIVER & 0xFF)};

static void setPWM(uint8_t channel, uint16_t value) {
  if (value > 4095)
    value = 4095;
  if (channel < 16 && g_pca1Present) {
    pca1.setPWM(channel, 0, value);
  } else if (channel < 32 && g_pca2Present) {
    pca2.setPWM(channel - 16, 0, value);
  }
}

// Track last written PWM values for change detection
static uint16_t lastPWM[32] = {0};
static uint32_t lastPWMDebug = 0;
static void setPWMTracked(uint8_t channel, uint16_t value) {
  if (channel < 32 && value != lastPWM[channel]) {
    Serial.printf("[PWM] ch%d %d -> %d\n", channel, lastPWM[channel], value);
    lastPWM[channel] = value;
    g_outputDirty = true;
    if (millis() - lastPWMDebug >= 2000) {
      lastPWMDebug = millis();
      Serial.print("[PWM] ");
      for (int c = 0; c < 16; c++) {
        if (lastPWM[c] > 0)
          Serial.printf("ch%d=%d ", c, lastPWM[c]);
      }
      Serial.println();
    }
  }
  setPWM(channel, value);
}

static void allOff(const char *caller) {
  Serial.printf("[PWM] allOff() called from %s\n", caller);
  for (int i = 0; i < MAX_AXIS_COUNT; i++) {
    g_solenoidValues[i] = 0;
    setPWMTracked(i, 0);
  }
}

static void initPCA() {
  Wire.setPins(PCA9685_SDA, PCA9685_SCL);
  Wire.begin();
  // Check if PCA1 is present
  Wire.beginTransmission(PCA9685_I2C_ADDR1);
  g_pca1Present = (Wire.endTransmission() == 0);
  if (g_pca1Present) {
    pca1.begin();
    pca1.setOscillatorFrequency(25000000);
    pca1.setPWMFreq(200);
    Serial.println("[MotorDriver] PCA1 detected at 0x40");
  } else {
    Serial.println("[MotorDriver] PCA1 NOT detected at 0x40 - PWM disabled");
  }
  // Check if PCA2 is present
  Wire.beginTransmission(PCA9685_I2C_ADDR2);
  g_pca2Present = (Wire.endTransmission() == 0);
  if (g_pca2Present) {
    pca2.begin();
    pca2.setOscillatorFrequency(25000000);
    pca2.setPWMFreq(200);
    Serial.println("[MotorDriver] PCA2 detected at 0x41");
  }
}

// Apply exponential curvature to a value 0-255
// curveExp: 2=linear(1.0), 3=1.5, 4=2.0, 5=2.5, 6=3.0
static uint32_t applyCurve(uint32_t t, uint8_t curveExp) {
  if (curveExp <= 2)
    return t; // Linear
  float normalized = t / 255.0f;
  float exponent = curveExp / 2.0f;
  float curved = powf(normalized, exponent);
  return (uint32_t)(curved * 255.0f);
}

// Paired channel output: fwdCh and revCh get PWM values
// For bidirectional axes: outputChannel = forward, outputChannel+1 = reverse
// For unidirectional axes: only outputChannel is used
// Invert flag swaps forward <-> reverse directions
static void mapAxis(const AxisConfig &axis, uint16_t potValue, uint16_t &fwdCh,
                    uint16_t &revCh) {
  fwdCh = 0;
  revCh = 0;
  if (!axis.isEnabled())
    return;
  if (axis.isBidirectional()) {
    if (potValue < axis.deadbandMin) {
      // Reverse direction -> paired channel
      uint16_t range = axis.deadbandMin;
      if (range == 0)
        range = 1;
      uint32_t t = ((uint32_t)(axis.deadbandMin - potValue) * 255u) / range;
      if (t > 255)
        t = 255;
      t = applyCurve(t, axis.curveExp); // Apply curvature
      uint8_t pwm =
          axis.pwmMin +
          (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
      revCh = ((uint16_t)pwm * 4095u) / 255u;
      fwdCh = 0;
    } else if (potValue > axis.deadbandMax) {
      // Forward direction -> base channel
      uint16_t range = 1023 - axis.deadbandMax;
      if (range == 0)
        range = 1;
      uint32_t t = ((uint32_t)(potValue - axis.deadbandMax) * 255u) / range;
      if (t > 255)
        t = 255;
      t = applyCurve(t, axis.curveExp); // Apply curvature
      uint8_t pwm =
          axis.pwmMin +
          (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
      fwdCh = ((uint16_t)pwm * 4095u) / 255u;
      revCh = 0;
    }
    // In deadband: both stay 0
  } else {
    if (potValue > axis.deadbandMax) {
      uint16_t range = 1023 - axis.deadbandMax;
      if (range == 0)
        range = 1;
      uint32_t t = ((uint32_t)(potValue - axis.deadbandMax) * 255u) / range;
      if (t > 255)
        t = 255;
      t = applyCurve(t, axis.curveExp); // Apply curvature
      uint8_t pwm =
          axis.pwmMin +
          (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
      fwdCh = ((uint16_t)pwm * 4095u) / 255u;
    }
  }
  // Invert: swap forward and reverse channels
  if (axis.isInverted()) {
    uint16_t tmp = fwdCh;
    fwdCh = revCh;
    revCh = tmp;
  }
}

static uint32_t lastAxisDebug = 0;

// Evaluate whether an axis's button gate condition is met
// Gate encoding: 0=none/always active, odd=button pressed, even=button released
// Button index = (gate - 1) / 2 for gate >= 1
static bool isAxisGateActive(const AxisConfig &axis) {
  if (axis.buttonGate == BUTTON_GATE_NONE)
    return true;
  
  bool btnPressed = false;
  int btnIdx = (axis.buttonGate - 1) / 2;
  
  // Check button based on source address range
  if (axis.sourceAddress >= 0x80) {
    // Unified joystick: 0-7 main PCA9555, 8-15 extender port0, 16-23 port1
    if (btnIdx < 8) {
      btnPressed = (g_joyButtons[axis.sourceAddress] >> btnIdx) & 0x01;
    } else if (btnIdx < 16) {
      btnPressed = (g_extButtons0[axis.sourceAddress] >> (btnIdx - 8)) & 0x01;
    } else {
      btnPressed = (g_extButtons1[axis.sourceAddress] >> (btnIdx - 16)) & 0x01;
    }
  } else {
    // Regular joystick buttons
    uint8_t btnByte = g_joyButtons[axis.sourceAddress];
    btnPressed = (btnByte >> btnIdx) & 0x01;
  }
  
  bool wantPressed =
      ((axis.buttonGate - 1) % 2) == 0; // odd gate values (1,3,5,7) = pressed
  return btnPressed == wantPressed;
}

static void zeroAxisChannels(const AxisConfig &axis) {
  uint8_t chFwd = axis.outputChannel;
  uint8_t chRev = axis.outputChannel + 1;
  // Never zero channels driven by web virtual buttons (they have priority)
  if (!g_joyOverrideChannels[chFwd] && g_solenoidValues[chFwd] != 0) {
    g_solenoidValues[chFwd] = 0;
    setPWMTracked(chFwd, 0);
  }
  if (axis.isBidirectional() && !g_joyOverrideChannels[chRev] &&
      g_solenoidValues[chRev] != 0) {
    g_solenoidValues[chRev] = 0;
    setPWMTracked(chRev, 0);
  }
}

static void updateAxes() {
  // Test mode: use manual test values instead of CAN-controlled values
  if (g_testMode && g_lastTestCmd > 0 && millis() - g_lastTestCmd < 10000) {
    for (int i = 0; i < 16; i++) {
      if (g_testValues[i] != g_solenoidValues[i]) {
        g_solenoidValues[i] = g_testValues[i];
        setPWMTracked(i, g_testValues[i]);
      }
    }
    return; // Skip CAN-based axis processing
  }
  // Auto-disable test mode after timeout
  if (g_testMode && g_lastTestCmd > 0 && millis() - g_lastTestCmd >= 10000) {
    g_testMode = false;
    // Zero all test values for safety
    for (int i = 0; i < 16; i++)
      g_testValues[i] = 0;
  }

  bool debugPrint = (millis() - lastAxisDebug >= 1000);
  if (debugPrint)
    lastAxisDebug = millis();
  for (int i = 0; i < MAX_AXIS_COUNT; i++) {
    const AxisConfig &axis = g_motorCfg.axes[i];
    if (!axis.isEnabled() || axis.sourceAddress == 0)
      continue;

    // Button gate check: if gate is inactive, zero outputs and skip
    if (!isAxisGateActive(axis)) {
      zeroAxisChannels(axis);
      if (debugPrint) {
        Serial.printf("[Axis %d] src=0x%02X pot%d ch=%d GATE_INACTIVE "
                      "(btnGate=%d btn=0x%02X)\n",
                      i, axis.sourceAddress, axis.potIndex, axis.outputChannel,
                      axis.buttonGate, g_joyButtons[axis.sourceAddress]);
      }
      continue;
    }

    uint16_t pot = g_joyPots[axis.sourceAddress][axis.potIndex];
    uint32_t lastUpdate = g_joyUpdateTime[axis.sourceAddress];
    if (debugPrint) {
      Serial.printf(
          "[Axis %d] src=0x%02X pot%d=%d ch=%d bidir=%d gate=%d upd=%lums\n", i,
          axis.sourceAddress, axis.potIndex, pot, axis.outputChannel,
          axis.isBidirectional() ? 1 : 0, axis.buttonGate,
          lastUpdate > 0 ? (millis() - lastUpdate) : 99999UL);
    }
    if (millis() - lastUpdate < 1000) {
      uint16_t fwd, rev;
      mapAxis(axis, pot, fwd, rev);
      uint8_t chFwd = axis.outputChannel;
      uint8_t chRev = axis.outputChannel + 1;
      // Skip channels driven by web joy buttons or button rules (priority)
      if (!g_joyOverrideChannels[chFwd] && !g_ruleDrivenChannels[chFwd] &&
          fwd != g_solenoidValues[chFwd]) {
        g_solenoidValues[chFwd] = fwd;
        setPWMTracked(chFwd, fwd);
      }
      if (axis.isBidirectional() && !g_joyOverrideChannels[chRev] &&
          (chRev >= 16 || !g_ruleDrivenChannels[chRev]) &&
          rev != g_solenoidValues[chRev]) {
        g_solenoidValues[chRev] = rev;
        setPWMTracked(chRev, rev);
      }
    } else {
      // Joystick timed out, zero both channels (unless joy/rule-driven)
      if (!g_joyOverrideChannels[axis.outputChannel] &&
          !g_ruleDrivenChannels[axis.outputChannel] &&
          (axis.outputChannel >= 15 ||
           !g_ruleDrivenChannels[axis.outputChannel + 1])) {
        zeroAxisChannels(axis);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Custom CAN button processing
// ---------------------------------------------------------------------------
static void processCustomCanButtons(const CANMessage &msg) {
  for (int i = 0; i < MAX_CUSTOM_CAN_BUTTONS; i++) {
    if (!g_customCanButtons[i].enabled)
      continue;
    if (msg.id != g_customCanButtons[i].canId)
      continue;
    if (g_customCanButtons[i].byteIndex >= msg.len)
      continue;
    bool pressed = (msg.data[g_customCanButtons[i].byteIndex] >>
                    g_customCanButtons[i].bitIndex) &
                   0x01;
    g_customBtnStates[i] = pressed;
  }
}

// ---------------------------------------------------------------------------
// Button-to-output processing
// ---------------------------------------------------------------------------
static void updateButtonOutputs() {
  static bool lastRulePressed[MAX_BUTTON_OUTPUT_RULES] = {false};
  bool chDriven[16] = {false};
  for (int i = 0; i < MAX_BUTTON_OUTPUT_RULES; i++) {
    const ButtonOutputRule &rule = g_btnOutputRules[i];
    if (!rule.enabled)
      continue;
    if (rule.outputChannel >= 16)
      continue;

    bool pressed = false;
    // Check if button source is a custom CAN button (SA >= 0xF0)
    if (rule.btnSourceSA >= 0xF0) {
      uint8_t customIdx = rule.btnSourceSA - 0xF0;
      if (customIdx < MAX_CUSTOM_CAN_BUTTONS) {
        pressed = g_customBtnStates[customIdx];
      }
    } else if (rule.btnSourceSA >= 0x80) {
      // Unified joystick: 0-7 main PCA9555, 8-15 extender port0, 16-23 port1
      if (rule.btnIndex < 8) {
        pressed = (g_joyButtons[rule.btnSourceSA] >> rule.btnIndex) & 0x01;
      } else if (rule.btnIndex < 16) {
        pressed = (g_extButtons0[rule.btnSourceSA] >> (rule.btnIndex - 8)) & 0x01;
      } else {
        pressed = (g_extButtons1[rule.btnSourceSA] >> (rule.btnIndex - 16)) & 0x01;
      }
    } else {
      // Physical joystick button
      uint8_t btnByte = g_joyButtons[rule.btnSourceSA];
      pressed = (btnByte >> rule.btnIndex) & 0x01;
    }

    bool wasPressed = lastRulePressed[i];
    if (pressed != lastRulePressed[i]) {
      lastRulePressed[i] = pressed;
      Serial.printf("[BtnRule %d] SA=0x%02X btn=%d -> %s (ch%d mode%d)\n", i,
                    rule.btnSourceSA, rule.btnIndex, pressed ? "PRESSED" : "released",
                    rule.outputChannel, rule.btnMode);
    }
    if (pressed) {
      // Motor-pair semantics (like the virtual joystick):
      // mode 0 = "+" drives the fwd channel, mode 1 = "-" drives rev (fwd+1)
      uint8_t outCh = rule.outputChannel + ((rule.btnMode == 1) ? 1 : 0);
      if (outCh >= 16)
        continue;
      // A stored target of 0 is a stale artifact - use 255
      uint16_t target = rule.pwmTarget ? rule.pwmTarget : 255;
      uint16_t pwmVal = (target * 4095u) / 255u;
      // Always force the write while pressed: a stale value left in
      // g_solenoidValues (e.g. channel owned by a silent axis) would
      // otherwise make this press a silent no-op
      if (g_solenoidValues[outCh] != pwmVal || !wasPressed) {
        g_solenoidValues[outCh] = pwmVal;
        setPWMTracked(outCh, pwmVal);
        if (!wasPressed) {
          Serial.printf("[BtnRule %d] OUT ch%d <- %d (target=%d)\n", i,
                        outCh, pwmVal, rule.pwmTarget);
        }
      }
      chDriven[outCh] = true;
    }
  }
  bool prevRuleDriven[16];
  memcpy(prevRuleDriven, g_ruleDrivenChannels, sizeof(prevRuleDriven));
  memcpy(g_ruleDrivenChannels, chDriven, sizeof(chDriven));
  // Release handling: momentary behavior - channels driven by rules last
  // cycle but no longer pressed are switched off (like the virtual buttons).
  // Skip channels owned by an enabled axis (the axis system drives them).
  for (int ch = 0; ch < 16; ch++) {
    if (!prevRuleDriven[ch] || chDriven[ch] || g_joyOverrideChannels[ch])
      continue;
    bool axisOwns = false;
    for (int ax = 0; ax < MAX_AXIS_COUNT; ax++) {
      const AxisConfig &a = g_motorCfg.axes[ax];
      // Only a LIVE axis (recent source data) may keep the channel;
      // a silent one must not hold a stale button value
      if (a.isEnabled() && (a.outputChannel == ch || a.outputChannel + 1 == ch) &&
          g_joyUpdateTime[a.sourceAddress] > 0 &&
          millis() - g_joyUpdateTime[a.sourceAddress] < 1000) {
        axisOwns = true;
        break;
      }
    }
    if (!axisOwns && g_solenoidValues[ch] != 0) {
      g_solenoidValues[ch] = 0;
      setPWMTracked(ch, 0);
      Serial.printf("[BtnRule] OUT ch%d <- 0 (released)\n", ch);
    }
  }
}

static void updateLED() {
  uint32_t now = millis();
  if (now - lastLedUpdate < 50)
    return;
  lastLedUpdate = now;
  RgbColor color(ledR, ledG, ledB);
  if (identifyActive) {
    if ((now / 150) % 2 == 0) {
      color = RgbColor(255, 255, 255);
    } else {
      color = RgbColor(0, 0, 0);
    }
    if (now - identifyTimer > 3000) {
      identifyActive = false;
    }
  } else if (!g_can->isOnline()) {
    if ((now / 200) % 2 == 0) {
      color = RgbColor(20, 0, 0);
    } else {
      color = RgbColor(0, 0, 0);
    }
  } else if (blinkFast) {
    if ((now / 100) % 2 == 0) {
      color = RgbColor(20, 20, 0);
    } else {
      color = RgbColor(0, 0, 0);
    }
  }
  strip.SetPixelColor(0, color);
  strip.Show();
}

static void processCAN() {
  CANMessage msg;
  int count = 0;
  while (g_can->receive(msg, 0)) {
    count++;
    if (count > 30)
      break; // prevent lockup under heavy bus load
    can_output_process_can(msg);
    processCustomCanButtons(msg);
    uint8_t pf = J1939_GET_PF(msg.id);
    uint8_t sa = J1939_GET_SA(msg.id);
    uint8_t da = J1939_GET_PS(msg.id);
    switch (pf) {
    case PF_JOYSTICK_POT1:
    case PF_JOYSTICK_POT2:
    case PF_JOYSTICK_POT3:
    case PF_JOYSTICK_POT4: {
      if (msg.len >= 2 && sa < 256) {
        uint8_t potIdx = pf - PF_JOYSTICK_POT1;
        // PF_JOYSTICK_POT4 (0x14) is not contiguous, handle specially
        if (pf == PF_JOYSTICK_POT4)
          potIdx = 3;
        uint16_t val = msg.data[0] | ((uint16_t)msg.data[1] << 8);
        // Scale 16-bit ADC (0-32767) to 10-bit (0-1023) for axis mapping
        g_joyPots[sa][potIdx] = val >> 5;
        g_joyUpdateTime[sa] = millis();
        lastSolenoidUpdate = millis(); // Refresh safety timer on new CAN data
        blinkFast = true;
        blinkTimer = millis();
      }
      break;
    }
    case PF_JOYSTICK_BUTTONS: {
      if (msg.len >= 1 && sa < 256) {
        g_joyButtons[sa] = msg.data[0];
        g_joyButtonUpdateTime[sa] = millis();
      }
      break;
    }
    case PF_EXTENDER_BUTTONS: {
      if (msg.len >= 1 && sa < 256) {
        if (g_extButtons0[sa] != msg.data[0]) {
          Serial.printf("[ExtBtn] SA=0x%02X port0=0x%02X\n", sa, msg.data[0]);
        }
        g_extButtons0[sa] = msg.data[0];
        g_extButtonUpdateTime[sa] = millis();
      }
      break;
    }
    case PF_EXTENDER_BUTTONS2: {
      if (msg.len >= 1 && sa < 256) {
        if (g_extButtons1[sa] != msg.data[0]) {
          Serial.printf("[ExtBtn] SA=0x%02X port1=0x%02X\n", sa, msg.data[0]);
        }
        g_extButtons1[sa] = msg.data[0];
        g_extButtonUpdateTime[sa] = millis();
      }
      break;
    }
    case PF_SOLENOID_CMD: {
      if (msg.len >= 8) {
        for (int i = 0; i < 8 && i < MAX_AXIS_COUNT; i++) {
          uint16_t val = ((uint16_t)msg.data[i] * 4095u) / 255u;
          g_solenoidValues[i] = val;
          setPWMTracked(i, val);
        }
        lastSolenoidUpdate = millis();
        blinkFast = true;
        blinkTimer = millis();
      }
      break;
    }
    case PF_LED_COLOR: {
      if (msg.len >= 3 && (da == DA_BROADCAST || da == g_can->getAddress())) {
        ledR = msg.data[0];
        ledG = msg.data[1];
        ledB = msg.data[2];
      }
      break;
    }
    case PF_IDENTIFY: {
      if (da == DA_BROADCAST || da == g_can->getAddress()) {
        identifyActive = true;
        identifyTimer = millis();
      }
      break;
    }
    case PF_SET_ADDRESS: {
      if (da == g_can->getAddress() && msg.len >= 1) {
        uint8_t newAddr = msg.data[0];
        if (newAddr >= 0x20 && newAddr <= 0xEF) {
          cfgMgr.setForcedAddress(newAddr);
          Serial.printf(
              "[MotorDriver] New address 0x%02X saved, rebooting...\n",
              newAddr);
          delay(200);
          ESP.restart();
        }
      }
      break;
    }
    case PF_CONFIG_AXIS: {
      if (da == g_can->getAddress() && msg.len >= 7) {
        uint8_t axisIdx = msg.data[0];
        if (axisIdx < MAX_AXIS_COUNT) {
          g_motorCfg.axes[axisIdx].unpack(msg.data);
          cfgMgr.saveAxisConfig(axisIdx, g_motorCfg.axes[axisIdx]);
          Serial.printf("[MotorDriver] Axis %d config updated via CAN\n",
                        axisIdx);
        }
      }
      break;
    }
    case PF_REQUEST_CONFIG: {
      if (da == g_can->getAddress()) {
        for (int i = 0; i < MAX_AXIS_COUNT; i++) {
          uint8_t buf[8];
          g_motorCfg.axes[i].pack(buf, i);
          g_can->sendBroadcast(PF_CONFIG_RESPONSE, buf, 8, 6);
          delay(5);
        }
      }
      break;
    }
    case PF_HEARTBEAT: {
      // Track module heartbeats for OTA web UI
#if defined(ENABLE_OTA_WEBSERVER)
      if (sa < 256) {
        ota_trackModule(sa, msg);
      }
#endif
      break;
    }
    default:
      break;
    }
  }
}

static void sendHeartbeat() {
  uint8_t data[8];
  data[0] = g_can->isOnline() ? 0x01 : 0x00;
  data[1] = (uint8_t)(millis() / 1000);
  data[2] = (uint8_t)((millis() / 1000) >> 8);
  data[3] = (uint8_t)(g_can->getRxCount() & 0xFF);
  data[4] = (uint8_t)(g_can->getTxCount() & 0xFF);
  data[5] = g_pca2Present ? 16 : 8;
  data[6] = g_motorCfg.pcaCount;
  data[7] = HB_TYPE_MOTOR_DRIVER; // capability announcement
  g_can->sendBroadcast(PF_HEARTBEAT, data, 8, 6);
}

// Auto-configure on first boot: set up bidirectional paired channels
// Joy1 Pot1 -> ch0+1, Joy1 Pot2 -> ch2+3, Joy2 Pot1 -> ch4+5, Joy2 Pot2 ->
// ch6+7
static void autoConfigDefaults() {
  bool anyEnabled = false;
  for (int i = 0; i < MAX_AXIS_COUNT; i++) {
    if (g_motorCfg.axes[i].isEnabled()) {
      anyEnabled = true;
      break;
    }
  }
  if (anyEnabled)
    return; // already configured, don't overwrite

  Serial.println("[MotorDriver] No axis config found, applying defaults...");
  struct {
    uint8_t src;
    uint8_t pot;
    uint8_t ch;
  } defaults[] = {
      {0x21, 0, 0},  // Joy1 Pot1 -> Out 1 (ch0+ch1 pair)
      {0x21, 1, 2},  // Joy1 Pot2 -> Out 3 (ch2+ch3 pair)
      {0x22, 0, 4},  // Joy2 Pot1 -> Out 5 (ch4+ch5 pair)
      {0x22, 1, 6},  // Joy2 Pot2 -> Out 7 (ch6+ch7 pair)
      {0x23, 0, 8},  // Joy3 Pot1 -> Out 9 (ch8+ch9 pair)
      {0x23, 1, 10}, // Joy3 Pot2 -> Out 11 (ch10+ch11 pair)
      {0x24, 0, 12}, // Joy4 Pot1 -> Out 13 (ch12+ch13 pair)
      {0x24, 1, 14}, // Joy4 Pot2 -> Out 15 (ch14+ch15 pair)
  };
  for (int i = 0; i < 8; i++) {
    AxisConfig &ax = g_motorCfg.axes[i];
    ax.sourceAddress = defaults[i].src;
    ax.potIndex = defaults[i].pot;
    ax.outputChannel = defaults[i].ch;
    ax.deadbandMin = 307; // midpoint ±20%
    ax.deadbandMax = 717; // midpoint ±20%
    ax.pwmMin = 20;       // ~8%
    ax.pwmMax = 255;      // full PWM
    ax.flags = FLAG_AXIS_ENABLED | FLAG_AXIS_BIDIRECTIONAL;
    cfgMgr.saveAxisConfig(i, ax);
  }

  // Set default joystick position labels
  const char *defaultPositions[] = {"left", "right", "center", "rear"};
  const uint8_t joyAddresses[] = {0x21, 0x22, 0x23, 0x24};
  for (int i = 0; i < MAX_JOYSTICK_LABELS; i++) {
    JoystickLabel &jl = g_joyLabels[i];
    jl.sourceAddress = joyAddresses[i];
    strncpy(jl.position, defaultPositions[i], sizeof(jl.position) - 1);
    jl.position[sizeof(jl.position) - 1] = '\0';
    // Set default axis labels
    const char *defaultAxisLabels[] = {"X", "Y", "Z", "W"};
    for (int a = 0; a < 4; a++) {
      strncpy(jl.axisLabels[a], defaultAxisLabels[a],
              sizeof(jl.axisLabels[a]) - 1);
      jl.axisLabels[a][sizeof(jl.axisLabels[a]) - 1] = '\0';
    }
    cfgMgr.saveJoystickLabel(i, jl);
  }
  Serial.println("[MotorDriver] Default joystick labels saved to NVS");

  Serial.println("[MotorDriver] Default config saved to NVS");
}

// ---------------------------------------------------------------------------
// Default button mapping (unified joystick @ 0x80, Ext N = button index N+7)
// Applied on first boot / when no enabled rules exist, then saved to NVS
// ---------------------------------------------------------------------------
static void defaultButtonRules() {
  struct {
    uint8_t pair;   // motor pair index 0-7
    uint8_t posBtn; // "+" button index (drives fwd channel)
    uint8_t negBtn; // "-" button index (drives rev channel)
  } defaults[] = {
      {0, 11, 13}, // Out 1: Ext 4 / Ext 6
      {1, 9, 10},  // Out 2: Ext 2 / Ext 3
      {2, 17, 16}, // Out 3: Ext 10 / Ext 9
      {3, 15, 12}, // Out 4: Ext 8 / Ext 5
      {4, 18, 21}, // Out 5: Ext 11 / Ext 14
      {5, 19, 20}, // Out 6: Ext 12 / Ext 13
  };
  Serial.println("[MotorDriver] Applying default button mapping...");
  int idx = 0;
  for (int d = 0; d < 6 && idx < MAX_BUTTON_OUTPUT_RULES - 1; d++) {
    for (int dir = 0; dir < 2; dir++) {
      ButtonOutputRule r;
      r.enabled = true;
      r.outputChannel = defaults[d].pair * 2;
      r.btnSourceSA = 0x80;
      r.btnIndex = (dir == 0) ? defaults[d].posBtn : defaults[d].negBtn;
      r.btnMode = dir; // 0 = "+" fwd, 1 = "-" rev
      r.pwmTarget = 255;
      g_btnOutputRules[idx] = r;
      cfgMgr.saveButtonOutputRule(idx, r);
      idx++;
    }
  }
}

// ---------------------------------------------------------------------------
// WT5500 Ethernet initialization
// ---------------------------------------------------------------------------
static void initEthernet() {
  Serial.println("[MotorDriver] Initializing WT5500 Ethernet...");

  if (!WETH.begin(WT5500_MISO, WT5500_MOSI, WT5500_SCLK, WT5500_CS, WT5500_RST,
                  WT5500_INT)) {
    Serial.println("[MotorDriver] WT5500 init FAILED!");
    return;
  }

  // Configure static IP from NVS config
  uint8_t ip3 = 40;
  IPAddress localIP(g_motorCfg.ethIP0, g_motorCfg.ethIP1, g_motorCfg.ethIP2,
                    ip3);
  IPAddress gateway(g_motorCfg.ethIP0, g_motorCfg.ethIP1, g_motorCfg.ethIP2, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WETH.config(localIP, gateway, subnet)) {
    Serial.println("[MotorDriver] Ethernet config failed");
  } else {
    Serial.printf("[MotorDriver] Ethernet IP: %d.%d.%d.%d\n", g_motorCfg.ethIP0,
                  g_motorCfg.ethIP1, g_motorCfg.ethIP2, ip3);
  }

  // Wait briefly for link
  for (int i = 0; i < 10 && !WETH.isConnected(); i++) {
    delay(500);
    Serial.printf("[MotorDriver] Waiting for Ethernet link... (%d)\n", i + 1);
  }

  if (WETH.isConnected()) {
    g_ethConnected = true;
    Serial.printf("[MotorDriver] Ethernet connected! IP: %s\n",
                  WETH.localIP().toString().c_str());
  } else {
    Serial.println("[MotorDriver] Ethernet: no link yet, will retry in loop");
  }

  // Start UDP listeners using lwIP sockets
  // Create AOG socket (port 8888)
  g_sockAOG = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_sockAOG >= 0) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_AOG_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sockAOG, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
      Serial.printf("[MotorDriver] Failed to bind AOG socket: %d\n", errno);
      close(g_sockAOG);
      g_sockAOG = -1;
    } else {
      // Set non-blocking
      int flags = fcntl(g_sockAOG, F_GETFL, 0);
      fcntl(g_sockAOG, F_SETFL, flags | O_NONBLOCK);
    }
  }

  // Create AGIO socket (port 9999)
  g_sockAGIO = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_sockAGIO >= 0) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_AGIO_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sockAGIO, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
      Serial.printf("[MotorDriver] Failed to bind AGIO socket: %d\n", errno);
      close(g_sockAGIO);
      g_sockAGIO = -1;
    } else {
      // Set non-blocking
      int flags = fcntl(g_sockAGIO, F_GETFL, 0);
      fcntl(g_sockAGIO, F_SETFL, flags | O_NONBLOCK);
    }
  }
  Serial.printf("[MotorDriver] UDP listening on ports %d, %d\n", UDP_AOG_PORT,
                UDP_AGIO_PORT);
}

// ---------------------------------------------------------------------------
// Process incoming UDP for PGN 32503 (subnet change) and AGIO PGN 201
// ---------------------------------------------------------------------------
static void processUDP() {
  uint8_t data[128];
  struct sockaddr_in src_addr;
  socklen_t addr_len = sizeof(src_addr);

  // Drain ALL pending AOG UDP packets (port 8888) to prevent buffer buildup
  if (g_sockAOG >= 0) {
    int len;
    while ((len = recvfrom(g_sockAOG, data, sizeof(data), MSG_DONTWAIT,
                           (struct sockaddr *)&src_addr, &addr_len)) > 0) {
      // PGN 32503: header bytes 247, 126
      if (len >= 6 && data[0] == 247 && data[1] == 126) {
        g_motorCfg.ethIP0 = data[2];
        g_motorCfg.ethIP1 = data[3];
        g_motorCfg.ethIP2 = data[4];
        cfgMgr.saveMotorConfig(g_motorCfg);
        Serial.printf("[MotorDriver] PGN 32503 subnet update: %d.%d.%d.x, "
                      "restarting...\n",
                      g_motorCfg.ethIP0, g_motorCfg.ethIP1, g_motorCfg.ethIP2);
        delay(200);
        ESP.restart();
      }
    }
  }

  // Drain ALL pending AGIO UDP packets (port 9999) to prevent buffer buildup
  if (g_sockAGIO >= 0) {
    addr_len = sizeof(src_addr);
    int len;
    while ((len = recvfrom(g_sockAGIO, data, sizeof(data), MSG_DONTWAIT,
                           (struct sockaddr *)&src_addr, &addr_len)) > 0) {
      // AGIO PGN 201: header 128, 129, 127, 201, 5, 201, 201
      if (len >= 10 && data[0] == 128 && data[1] == 129 && data[2] == 127 &&
          data[3] == 201 && data[4] == 5 && data[5] == 201 && data[6] == 201) {
        g_motorCfg.ethIP0 = data[7];
        g_motorCfg.ethIP1 = data[8];
        g_motorCfg.ethIP2 = data[9];
        cfgMgr.saveMotorConfig(g_motorCfg);
        Serial.printf("[MotorDriver] AGIO PGN 201 subnet update: %d.%d.%d.x, "
                      "restarting...\n",
                      g_motorCfg.ethIP0, g_motorCfg.ethIP1, g_motorCfg.ethIP2);
        delay(200);
        ESP.restart();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Suspend/resume UDP sockets during OTA to free WT5500 buffer space
// ---------------------------------------------------------------------------
static int g_savedSockAOG = -1;
static int g_savedSockAGIO = -1;

void suspendUDP() {
  if (g_sockAOG >= 0) {
    g_savedSockAOG = g_sockAOG;
    close(g_sockAOG);
    g_sockAOG = -1;
    Serial.println("[OTA] UDP socket AOG closed");
  }
  if (g_sockAGIO >= 0) {
    g_savedSockAGIO = g_sockAGIO;
    close(g_sockAGIO);
    g_sockAGIO = -1;
    Serial.println("[OTA] UDP socket AGIO closed");
  }
}

void resumeUDP() {
  if (g_savedSockAOG >= 0) {
    // Recreate AOG socket
    struct sockaddr_in addr;
    g_sockAOG = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sockAOG >= 0) {
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(UDP_AOG_PORT);
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(g_sockAOG, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(g_sockAOG);
        g_sockAOG = -1;
      } else {
        int flags = fcntl(g_sockAOG, F_GETFL, 0);
        fcntl(g_sockAOG, F_SETFL, flags | O_NONBLOCK);
      }
    }
    g_savedSockAOG = -1;
    Serial.println("[OTA] UDP socket AOG reopened");
  }
  if (g_savedSockAGIO >= 0) {
    // Recreate AGIO socket
    struct sockaddr_in addr;
    g_sockAGIO = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sockAGIO >= 0) {
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(UDP_AGIO_PORT);
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(g_sockAGIO, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(g_sockAGIO);
        g_sockAGIO = -1;
      } else {
        int flags = fcntl(g_sockAGIO, F_GETFL, 0);
        fcntl(g_sockAGIO, F_SETFL, flags | O_NONBLOCK);
      }
    }
    g_savedSockAGIO = -1;
    Serial.println("[OTA] UDP socket AGIO reopened");
  }
}

// ---------------------------------------------------------------------------
// Process joystick commands from web UI -> drive PWM outputs directly
// Web joy buttons have priority over CAN joystick axis on the same channels.
// ---------------------------------------------------------------------------
static void processJoystickCommands() {
  // Safety: auto-clear all joystick commands after 5s of no updates
  // (web UI keep-alive heartbeat runs every 1s)
  if (g_lastJoyCmd > 0 && millis() - g_lastJoyCmd > 5000) {
    for (int i = 0; i < MAX_JOY_FUNCTIONS; i++) {
      g_joyCmdActive[i] = false;
    }
    g_lastJoyCmd = 0;
  }

  // Clear override flags each cycle - only active commands will set them
  memset(g_joyOverrideChannels, 0, sizeof(g_joyOverrideChannels));

  // Apply each active joystick command directly to its output
  // Even index (pos/fel/bal/nyit) → mapping.outputChannel
  // Odd index (neg/le/jobb/csuk)  → mapping.outputChannel + 1
  // Note: "mindketto" (both) buttons are handled by frontend which sends
  // both individual commands (e.g. fokeret-nyit + segedkeret-nyit)
  for (int i = 0; i < MAX_JOY_FUNCTIONS; i++) {
    if (!g_joyCmdActive[i])
      continue;

    const JoystickOutputMapping &mapping = g_joyMappings[i];
    uint8_t baseCh = mapping.outputChannel;
    if (baseCh >= 16)
      continue;

    // Find the axis config for this channel pair to get PWM Max and invert flag
    uint16_t pwmValue = 4095; // default full PWM
    bool inverted = false;
    for (int ax = 0; ax < MAX_AXIS_COUNT; ax++) {
      if (g_motorCfg.axes[ax].outputChannel == baseCh) {
        // pwmMax is 0-255, scale to 0-4095
        pwmValue = (uint16_t)g_motorCfg.axes[ax].pwmMax * 16;
        if (pwmValue > 4095)
          pwmValue = 4095;
        // Check invert flag (FLAG_AXIS_INVERT = 4)
        inverted = (g_motorCfg.axes[ax].flags & 4) != 0;
        break;
      }
    }

    // Determine output channel based on direction and invert flag
    uint8_t outCh;
    bool isPos = (i % 2 == 0);
    if (inverted) {
      // Swap pos/neg when inverted
      outCh = isPos ? (baseCh + 1) : baseCh;
    } else {
      outCh = isPos ? baseCh : (baseCh + 1);
    }
    if (outCh >= 16)
      continue;

    // Direct PWM - overrides axis system on this channel
    setPWMTracked(outCh, pwmValue);
    g_solenoidValues[outCh] = pwmValue;
    g_joyOverrideChannels[outCh] = true;
  }

  // Release handling: zero channels driven in the previous cycle that are
  // no longer commanded (momentary behavior, also with all axes disabled).
  // Never zero a channel another live subsystem owns (button rules, axes).
  static bool prevOverride[16] = {false};
  for (int ch = 0; ch < 16; ch++) {
    bool ruleDriven = g_ruleDrivenChannels[ch];
    bool axisLive = false;
    for (int ax = 0; ax < MAX_AXIS_COUNT; ax++) {
      const AxisConfig &a = g_motorCfg.axes[ax];
      if (!a.isEnabled() || a.sourceAddress == 0)
        continue;
      if ((a.outputChannel == ch || a.outputChannel + 1 == ch) &&
          g_joyUpdateTime[a.sourceAddress] > 0 &&
          millis() - g_joyUpdateTime[a.sourceAddress] < 1000) {
        axisLive = true;
        break;
      }
    }
    if (prevOverride[ch] && !g_joyOverrideChannels[ch] && !ruleDriven &&
        !axisLive && g_solenoidValues[ch] != 0) {
      g_solenoidValues[ch] = 0;
      setPWMTracked(ch, 0);
    }
    prevOverride[ch] = g_joyOverrideChannels[ch];
  }
}

void ecu_setup() {
  strip.Begin();
  strip.SetPixelColor(0, RgbColor(0, 0, 20));
  strip.Show();
  // Output active indicator pin
  pinMode(OUTPUT_ACTIVE_PIN, OUTPUT);
  digitalWrite(OUTPUT_ACTIVE_PIN, LOW);
  Serial.begin(115200);
  delay(200);
  Serial.println("[MotorDriver] Initializing config...");
  cfgMgr.begin();
  if (!cfgMgr.checkVersion()) {
    Serial.println("[MotorDriver] NVS cleared - using defaults");
  }
  uint8_t forcedAddr = cfgMgr.getForcedAddress(ECU_PREFERRED_ADDRESS);
  cfgMgr.loadMotorConfig(g_motorCfg);
  cfgMgr.loadCanOutputRules(g_canOutputRules);
  cfgMgr.loadButtonOutputRules(g_btnOutputRules);
  {
    int n = 0;
    for (int i = 0; i < MAX_BUTTON_OUTPUT_RULES; i++)
      if (g_btnOutputRules[i].enabled)
        n++;
    Serial.printf("[MotorDriver] Loaded %d button rule(s) from NVS\n", n);
    // Defaults only when rules were NEVER saved (key existence, not content -
    // an intentionally cleared mapping must survive reboot)
    if (!cfgMgr.hasButtonRules()) {
      defaultButtonRules();
    }
  }
  cfgMgr.loadCustomCanButtons(g_customCanButtons);
  cfgMgr.loadJoystickLabels(g_joyLabels);
  cfgMgr.loadOutputLabels(g_outLabels);
  cfgMgr.loadJoystickMappings(g_joyMappings);
  // Only apply factory defaults on a genuinely fresh flash (no axis config
  // ever saved). An intentionally all-disabled config must survive reboot.
  if (!cfgMgr.hasAxisConfig()) {
    autoConfigDefaults();
  }

  // Set default joy mappings if not configured (all channels 0 after flash
  // erase) Check if pair 0 pos is still at default (ch 0) and pair 1 is also at
  // ch 0 (which means no saved config exists)
  if (g_joyMappings[0].outputChannel == 0 &&
      g_joyMappings[2].outputChannel == 0) {
    Serial.println("[MotorDriver] Setting default joy mappings...");
    // Default: each pair maps to its corresponding output pair
    const uint8_t defaultJoyCh[] = {0, 2, 4, 6, 8, 10}; // pair 0-5
    for (int pi = 0; pi < 6; pi++) {
      g_joyMappings[pi * 2].outputChannel = defaultJoyCh[pi]; // pos
      g_joyMappings[pi * 2].invert = false;
      g_joyMappings[pi * 2 + 1].outputChannel = defaultJoyCh[pi]; // neg
      g_joyMappings[pi * 2 + 1].invert = false;
    }
    // Mark remaining functions (12-19) as off
    for (int i = 12; i < MAX_JOY_FUNCTIONS; i++) {
      g_joyMappings[i].outputChannel = 255; // off
      g_joyMappings[i].invert = false;
    }
    cfgMgr.saveJoystickMappings(g_joyMappings);
  }
  // Dump loaded config to serial
  Serial.println("[MotorDriver] Loaded axis config:");
  for (int i = 0; i < MAX_AXIS_COUNT; i++) {
    const AxisConfig &a = g_motorCfg.axes[i];
    if (a.flags) {
      Serial.printf("[MotorDriver]   axis%d src=0x%02X pot=%d ch=%d db=%d-%d "
                    "pwm=%d-%d flags=%d inv=%d gate=%d\n",
                    i, a.sourceAddress, a.potIndex, a.outputChannel,
                    a.deadbandMin, a.deadbandMax, a.pwmMin, a.pwmMax, a.flags,
                    a.isInverted() ? 1 : 0, a.buttonGate);
    }
  }
  Serial.println("[MotorDriver] Initializing PCA9685...");
  initPCA();
  allOff("boot");
  Serial.println("[MotorDriver] Initializing CAN...");

  // GPIO loopback test: MCP2562 loops TXD→RXD internally
  // Toggle GPIO16 (TXD) and read GPIO17 (RXD) to verify physical connection
  Serial.println("[GPIO Test] Testing MCP2562 TXD->RXD loopback...");
  pinMode(CAN_TX_PIN, OUTPUT);
  pinMode(CAN_RX_PIN, INPUT);
  int pass = 1;
  for (int i = 0; i < 4; i++) {
    int level = (i % 2 == 0) ? LOW : HIGH;
    digitalWrite(CAN_TX_PIN, level);
    delayMicroseconds(50);
    int rxRead = digitalRead(CAN_RX_PIN);
    Serial.printf("[GPIO Test] TX=%d -> RX=%d %s\n", level, rxRead,
                  (rxRead == level) ? "OK" : "FAIL");
    if (rxRead != level)
      pass = 0;
  }
  digitalWrite(CAN_TX_PIN, HIGH); // idle state
  Serial.printf("[GPIO Test] Result: %s\n",
                pass ? "PASS - MCP2562 data path OK"
                     : "FAIL - MCP2562 RXD not connected to IO17");

  g_can = new ForwarderCAN(forcedAddr, ECU_NAME);
  if (!g_can->begin(CAN_TX_PIN, CAN_RX_PIN, CAN_BITRATE)) {
    Serial.println("[MotorDriver] CAN init FAILED!");
    while (1) {
      strip.SetPixelColor(0, RgbColor(20, 0, 0));
      strip.Show();
      delay(200);
      strip.SetPixelColor(0, RgbColor(0, 0, 0));
      strip.Show();
      delay(200);
    }
  }
  Serial.printf("[MotorDriver] Ready. Address=0x%02X Channels=%d\n",
                g_can->getAddress(), g_pca2Present ? 16 : 8);
  Serial.printf("[MotorDriver] CAN pins: TX=IO%d RX=IO%d\n", CAN_TX_PIN,
                CAN_RX_PIN);
  can_output_setup(g_canOutputRules);
#if defined(ENABLE_OTA_WEBSERVER)
  char hostname[24];
  snprintf(hostname, sizeof(hostname), "forwarder-motor-%02X",
           g_can->getAddress());
  ota_setup(hostname);
#endif
  Serial.println("[MotorDriver] Setup complete, entering loop...");

  // Initialize WT5500 Ethernet after CAN is up (can be disabled to rule it
  // out as a hang source: build with -DDISABLE_ETHERNET)
#if !defined(DISABLE_ETHERNET)
  initEthernet();
#else
  Serial.println("[MotorDriver] Ethernet DISABLED by build flag");
#endif

  // Auto-reset watchdog: if the loop stops for WATCHDOG_TIMEOUT_S the chip
  // resets itself instead of sitting dead until a manual reset
  esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
}

static uint32_t lastStatusPrint = 0;
static uint32_t lastSelfTest = 0;
static uint32_t selfTestCount = 0;
static uint32_t loopCount = 0;

void ecu_loop() {
  uint32_t now = millis();
  loopCount++;
  esp_task_wdt_reset(); // Feed the auto-reset watchdog

  // Prioritize web server during OTA
#if defined(ENABLE_OTA_WEBSERVER)
  ota_loop();
  if (ota_is_active()) {
    // During OTA, call handleClient multiple times to keep up with upload
    ota_loop();
    ota_loop();
  }
#endif

  yield();
  g_can->loop();
  processCAN();
  // Call web server after CAN processing to improve responsiveness
#if defined(ENABLE_OTA_WEBSERVER)
  ota_loop();
#endif
  // Skip most processing during OTA to maximize network resources
#if defined(ENABLE_OTA_WEBSERVER)
  if (!ota_is_active()) {
#endif
#if !defined(DISABLE_ETHERNET)
    processUDP();
#endif
    yield();
    updateAxes();
    updateButtonOutputs();
    processJoystickCommands();
    // Call web server after heavy processing
#if defined(ENABLE_OTA_WEBSERVER)
    ota_loop();
#endif
    // Update output active indicator pin (GPIO5)
    {
      bool anyActive = false;
      for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        if (g_solenoidValues[i] > 0) {
          anyActive = true;
          break;
        }
      }
      digitalWrite(OUTPUT_ACTIVE_PIN, anyActive ? HIGH : LOW);
    }
    yield();

    // Self-loopback test: send a test frame every 3s
    if (now - lastSelfTest >= 3000) {
      lastSelfTest = now;
      uint8_t testData[8] = {
          0xAA, 0xBB, (uint8_t)(selfTestCount & 0xFF), 0, 0, 0, 0, 0};
      selfTestCount++;
      bool sent = g_can->sendBroadcast(0xEF, testData, 8, 6); // PGN 0xEF00 test
      Serial.printf("[SelfTest #%lu] sent=%d (TX_pin=IO%d RX_pin=IO%d)\n",
                    selfTestCount, sent ? 1 : 0, CAN_TX_PIN, CAN_RX_PIN);
      if (!sent) {
        Serial.printf("[SelfTest] TX FAILED - check CAN bus\n");
      }
      yield();
    }

    // Periodic CAN bus status
    if (now - lastStatusPrint >= 5000) {
      lastStatusPrint = now;
      Serial.printf("[CAN Status] online=%d tx=%lu rx=%lu err=%lu\n",
                    g_can->isOnline() ? 1 : 0, g_can->getTxCount(),
                    g_can->getRxCount(), g_can->getErrorCount());
    }
    // Web virtual joystick commands count as live operator input:
    // keep the safety timer fresh while they are being sent
    if (g_lastJoyCmd > 0 && g_lastJoyCmd > lastSolenoidUpdate) {
      lastSolenoidUpdate = g_lastJoyCmd;
    }
    if (lastSolenoidUpdate != 0 && now > lastSolenoidUpdate &&
        now - lastSolenoidUpdate > SAFETY_TIMEOUT_MS) {
      Serial.printf("[SAFETY] No CAN data for %lums, all outputs OFF "
                    "(last=%lu now=%lu)\n",
                    (unsigned long)(now - lastSolenoidUpdate),
                    (unsigned long)lastSolenoidUpdate, (unsigned long)now);
      allOff("safety");
      lastSolenoidUpdate =
          0; // Prevent re-firing until fresh CAN data arrives
    }
    if (blinkFast && (now - blinkTimer > 100)) {
      blinkFast = false;
    }
    if (now - lastHeartbeat >= 1000) {
      lastHeartbeat = now;
      if (g_can->isOnline()) {
        sendHeartbeat();
      }
    }
    // Broadcast PCA9685 output values when PWM changes (rate-limited to 20Hz
    // max)
    if (g_outputDirty && (now - lastOutputBroadcast >= 50)) {
      lastOutputBroadcast = now;
      g_outputDirty = false;
      // Message 1: axes 0-3 (channels 0-7)
      {
        uint8_t data[8];
        for (int i = 0; i < 4; i++) {
          int16_t val = (int16_t)g_solenoidValues[i * 2] -
                        (int16_t)g_solenoidValues[i * 2 + 1];
          data[i * 2] = (uint8_t)(val & 0xFF);
          data[i * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
        }
        g_can->sendBroadcast(PF_MOTOR_OUTPUT1, data, 8, 6);
      }
      // Message 2: axes 4-7 (channels 8-15)
      {
        uint8_t data[8];
        for (int i = 0; i < 4; i++) {
          int idx = 4 + i;
          int16_t val = (int16_t)g_solenoidValues[idx * 2] -
                        (int16_t)g_solenoidValues[idx * 2 + 1];
          data[i * 2] = (uint8_t)(val & 0xFF);
          data[i * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
        }
        g_can->sendBroadcast(PF_MOTOR_OUTPUT2, data, 8, 6);
      }
    }
    updateLED();
    can_output_loop();
#if defined(ENABLE_OTA_WEBSERVER)
  }
#endif
}

#endif // ECU_TYPE_MOTOR_DRIVER
