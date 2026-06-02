#include "ecu_motor_driver.h"

#if defined(ECU_TYPE_MOTOR_DRIVER)

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <NeoPixelBus.h>
#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "ota_webserver.h"
#include "web_state.h"
#include "can_output.h"

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
#ifndef WS2812_PIN
#define WS2812_PIN 48
#endif

static Adafruit_PWMServoDriver pca1 = Adafruit_PWMServoDriver(PCA9685_I2C_ADDR1);
static Adafruit_PWMServoDriver pca2 = Adafruit_PWMServoDriver(PCA9685_I2C_ADDR2);
bool g_pca2Present = false;

static NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(1, WS2812_PIN);
ForwarderCAN* g_can = nullptr;
static ForwarderConfig cfgMgr("motorcfg");

MotorConfig g_motorCfg;
uint16_t g_solenoidValues[MAX_AXIS_COUNT] = {0};
static uint32_t lastSolenoidUpdate = 0;
static uint32_t lastHeartbeat = 0;
static uint32_t lastLedUpdate = 0;

static uint8_t ledR = 0, ledG = 0, ledB = 20;
static bool blinkFast = false;
static uint32_t blinkTimer = 0;
static uint32_t identifyTimer = 0;
static bool identifyActive = false;

uint16_t g_joyPots[256][3] = {{0}};
uint32_t g_joyUpdateTime[256] = {0};
CanOutputRule g_canOutputRules[MAX_CAN_OUTPUT_RULES];

static const uint8_t ECU_NAME[8] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    (ECU_NAME_MOTOR_DRIVER & 0xFF)
};

static void setPWM(uint8_t channel, uint16_t value) {
    if (value > 4095) value = 4095;
    if (channel < 8) {
        pca1.setPWM(channel, 0, value);
    } else if (channel < 16 && g_pca2Present) {
        pca2.setPWM(channel - 8, 0, value);
    }
}

static void allOff() {
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        g_solenoidValues[i] = 0;
        setPWM(i, 0);
    }
}

static void initPCA() {
    Wire.setPins(PCA9685_SDA, PCA9685_SCL);
    Wire.begin();
    pca1.begin();
    pca1.setOscillatorFrequency(25000000);
    pca1.setPWMFreq(200);
    Wire.beginTransmission(PCA9685_I2C_ADDR2);
    g_pca2Present = (Wire.endTransmission() == 0);
    if (g_pca2Present) {
        pca2.begin();
        pca2.setOscillatorFrequency(25000000);
        pca2.setPWMFreq(200);
        Serial.println("[MotorDriver] 2nd PCA9685 detected at 0x41");
    }
}

static uint16_t mapAxis(const AxisConfig& axis, uint16_t potValue) {
    if (!axis.isEnabled()) return 0;
    uint16_t out = 0;
    if (axis.isBidirectional()) {
        if (potValue < axis.deadbandMin) {
            uint16_t range = axis.deadbandMin;
            if (range == 0) range = 1;
            uint32_t t = ((uint32_t)(axis.deadbandMin - potValue) * 255u) / range;
            if (t > 255) t = 255;
            uint8_t rev = axis.pwmMax - (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
            out = ((uint16_t)rev * 4095u) / 255u;
        } else if (potValue > axis.deadbandMax) {
            uint16_t range = 1023 - axis.deadbandMax;
            if (range == 0) range = 1;
            uint32_t t = ((uint32_t)(potValue - axis.deadbandMax) * 255u) / range;
            if (t > 255) t = 255;
            uint8_t fwd = axis.pwmMin + (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
            out = ((uint16_t)fwd * 4095u) / 255u;
        } else {
            out = 0;
        }
    } else {
        if (potValue <= axis.deadbandMax) {
            out = 0;
        } else {
            uint16_t range = 1023 - axis.deadbandMax;
            if (range == 0) range = 1;
            uint32_t t = ((uint32_t)(potValue - axis.deadbandMax) * 255u) / range;
            if (t > 255) t = 255;
            uint8_t pwm = axis.pwmMin + (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
            out = ((uint16_t)pwm * 4095u) / 255u;
        }
    }
    return out;
}

static void updateAxes() {
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        const AxisConfig& axis = g_motorCfg.axes[i];
        if (!axis.isEnabled() || axis.sourceAddress == 0) continue;
        uint16_t pot = g_joyPots[axis.sourceAddress][axis.potIndex];
        uint32_t lastUpdate = g_joyUpdateTime[axis.sourceAddress];
        if (millis() - lastUpdate < 1000) {
            uint16_t out = mapAxis(axis, pot);
            if (out != g_solenoidValues[axis.outputChannel]) {
                g_solenoidValues[axis.outputChannel] = out;
                setPWM(axis.outputChannel, out);
            }
        }
    }
}

static void updateLED() {
    uint32_t now = millis();
    if (now - lastLedUpdate < 50) return;
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
    while (g_can->receive(msg, 0)) {
        can_output_process_can(msg);
        uint8_t pf = J1939_GET_PF(msg.id);
        uint8_t sa = J1939_GET_SA(msg.id);
        uint8_t da = J1939_GET_PS(msg.id);
        switch (pf) {
            case PF_JOYSTICK_POT1:
            case PF_JOYSTICK_POT2:
            case PF_JOYSTICK_POT3: {
                if (msg.len >= 2 && sa < 256) {
                    uint8_t potIdx = pf - PF_JOYSTICK_POT1;
                    uint16_t val = msg.data[0] | ((uint16_t)msg.data[1] << 8);
                    g_joyPots[sa][potIdx] = val;
                    g_joyUpdateTime[sa] = millis();
                    lastSolenoidUpdate = millis();
                    blinkFast = true;
                    blinkTimer = millis();
                }
                break;
            }
            case PF_SOLENOID_CMD: {
                if (msg.len >= 8) {
                    for (int i = 0; i < 8 && i < MAX_AXIS_COUNT; i++) {
                        uint16_t val = ((uint16_t)msg.data[i] * 4095u) / 255u;
                        g_solenoidValues[i] = val;
                        setPWM(i, val);
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
                        Serial.printf("[MotorDriver] New address 0x%02X saved, rebooting...\n", newAddr);
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
                        Serial.printf("[MotorDriver] Axis %d config updated via CAN\n", axisIdx);
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
    data[7] = 0;
    g_can->sendBroadcast(PF_HEARTBEAT, data, 8, 6);
}

void ecu_setup() {
    strip.Begin();
    strip.SetPixelColor(0, RgbColor(0, 0, 20));
    strip.Show();
    Serial.begin(115200);
    delay(200);
    Serial.println("[MotorDriver] Initializing config...");
    cfgMgr.begin();
    uint8_t forcedAddr = cfgMgr.getForcedAddress(ECU_PREFERRED_ADDRESS);
    cfgMgr.loadMotorConfig(g_motorCfg);
    cfgMgr.loadCanOutputRules(g_canOutputRules);
    Serial.println("[MotorDriver] Initializing PCA9685...");
    initPCA();
    allOff();
    Serial.println("[MotorDriver] Initializing CAN...");
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
    can_output_setup(g_canOutputRules);
#if defined(ENABLE_OTA_WEBSERVER)
    ota_setup("forwarder-motor");
#endif
}

void ecu_loop() {
    uint32_t now = millis();
    g_can->loop();
    processCAN();
    updateAxes();
    if (now - lastSolenoidUpdate > SAFETY_TIMEOUT_MS) {
        if (lastSolenoidUpdate != 0) {
            allOff();
            lastSolenoidUpdate = 0;
        }
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
    updateLED();
    can_output_loop();
#if defined(ENABLE_OTA_WEBSERVER)
    ota_loop();
#endif
}

#endif // ECU_TYPE_MOTOR_DRIVER
