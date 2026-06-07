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
uint16_t g_testValues[16] = {0};  // Manual test values for outputs 1-16
uint32_t g_lastTestCmd = 0;       // Auto-disable after 10s timeout

static uint8_t ledR = 0, ledG = 0, ledB = 20;
static bool blinkFast = false;
static uint32_t blinkTimer = 0;
static uint32_t identifyTimer = 0;
static bool identifyActive = false;

uint16_t g_joyPots[256][3] = {{0}};
uint8_t g_joyButtons[256] = {0};
uint32_t g_joyUpdateTime[256] = {0};
uint32_t g_joyButtonUpdateTime[256] = {0};
CanOutputRule g_canOutputRules[MAX_CAN_OUTPUT_RULES];

static const uint8_t ECU_NAME[8] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    (ECU_NAME_MOTOR_DRIVER & 0xFF)
};

static void setPWM(uint8_t channel, uint16_t value) {
    if (value > 4095) value = 4095;
    if (channel < 16) {
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
        lastPWM[channel] = value;
        g_outputDirty = true;
        if (millis() - lastPWMDebug >= 2000) {
            lastPWMDebug = millis();
            Serial.print("[PWM] ");
            for (int c = 0; c < 16; c++) {
                if (lastPWM[c] > 0) Serial.printf("ch%d=%d ", c, lastPWM[c]);
            }
            Serial.println();
        }
    }
    setPWM(channel, value);
}

static void allOff(const char* caller) {
    Serial.printf("[PWM] allOff() called from %s\n", caller);
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        g_solenoidValues[i] = 0;
        setPWMTracked(i, 0);
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

// Apply exponential curvature to a value 0-255
// curveExp: 2=linear(1.0), 3=1.5, 4=2.0, 5=2.5, 6=3.0
static uint32_t applyCurve(uint32_t t, uint8_t curveExp) {
    if (curveExp <= 2) return t;  // Linear
    float normalized = t / 255.0f;
    float exponent = curveExp / 2.0f;
    float curved = powf(normalized, exponent);
    return (uint32_t)(curved * 255.0f);
}

// Paired channel output: fwdCh and revCh get PWM values
// For bidirectional axes: outputChannel = forward, outputChannel+1 = reverse
// For unidirectional axes: only outputChannel is used
// Invert flag swaps forward <-> reverse directions
static void mapAxis(const AxisConfig& axis, uint16_t potValue, uint16_t& fwdCh, uint16_t& revCh) {
    fwdCh = 0;
    revCh = 0;
    if (!axis.isEnabled()) return;
    if (axis.isBidirectional()) {
        if (potValue < axis.deadbandMin) {
            // Reverse direction -> paired channel
            uint16_t range = axis.deadbandMin;
            if (range == 0) range = 1;
            uint32_t t = ((uint32_t)(axis.deadbandMin - potValue) * 255u) / range;
            if (t > 255) t = 255;
            t = applyCurve(t, axis.curveExp);  // Apply curvature
            uint8_t pwm = axis.pwmMin + (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
            revCh = ((uint16_t)pwm * 4095u) / 255u;
            fwdCh = 0;
        } else if (potValue > axis.deadbandMax) {
            // Forward direction -> base channel
            uint16_t range = 1023 - axis.deadbandMax;
            if (range == 0) range = 1;
            uint32_t t = ((uint32_t)(potValue - axis.deadbandMax) * 255u) / range;
            if (t > 255) t = 255;
            t = applyCurve(t, axis.curveExp);  // Apply curvature
            uint8_t pwm = axis.pwmMin + (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
            fwdCh = ((uint16_t)pwm * 4095u) / 255u;
            revCh = 0;
        }
        // In deadband: both stay 0
    } else {
        if (potValue > axis.deadbandMax) {
            uint16_t range = 1023 - axis.deadbandMax;
            if (range == 0) range = 1;
            uint32_t t = ((uint32_t)(potValue - axis.deadbandMax) * 255u) / range;
            if (t > 255) t = 255;
            t = applyCurve(t, axis.curveExp);  // Apply curvature
            uint8_t pwm = axis.pwmMin + (uint8_t)(((uint32_t)(axis.pwmMax - axis.pwmMin) * t) / 255u);
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
static bool isAxisGateActive(const AxisConfig& axis) {
    switch (axis.buttonGate) {
        case BUTTON_GATE_BTN1_PRESSED:
            return (g_joyButtons[axis.sourceAddress] & 0x01) != 0;
        case BUTTON_GATE_BTN1_RELEASED:
            return (g_joyButtons[axis.sourceAddress] & 0x01) == 0;
        case BUTTON_GATE_NONE:
        default:
            return true;  // No gating, always active
    }
}

static void zeroAxisChannels(const AxisConfig& axis) {
    uint8_t chFwd = axis.outputChannel;
    uint8_t chRev = axis.outputChannel + 1;
    if (g_solenoidValues[chFwd] != 0) {
        g_solenoidValues[chFwd] = 0;
        setPWMTracked(chFwd, 0);
    }
    if (axis.isBidirectional() && g_solenoidValues[chRev] != 0) {
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
        return;  // Skip CAN-based axis processing
    }
    // Auto-disable test mode after timeout
    if (g_testMode && g_lastTestCmd > 0 && millis() - g_lastTestCmd >= 10000) {
        g_testMode = false;
        // Zero all test values for safety
        for (int i = 0; i < 16; i++) g_testValues[i] = 0;
    }
    
    bool debugPrint = (millis() - lastAxisDebug >= 1000);
    if (debugPrint) lastAxisDebug = millis();
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        const AxisConfig& axis = g_motorCfg.axes[i];
        if (!axis.isEnabled() || axis.sourceAddress == 0) continue;

        // Button gate check: if gate is inactive, zero outputs and skip
        if (!isAxisGateActive(axis)) {
            zeroAxisChannels(axis);
            if (debugPrint) {
                Serial.printf("[Axis %d] src=0x%02X pot%d ch=%d GATE_INACTIVE (btnGate=%d btn=0x%02X)\n",
                    i, axis.sourceAddress, axis.potIndex, axis.outputChannel,
                    axis.buttonGate, g_joyButtons[axis.sourceAddress]);
            }
            continue;
        }

        uint16_t pot = g_joyPots[axis.sourceAddress][axis.potIndex];
        uint32_t lastUpdate = g_joyUpdateTime[axis.sourceAddress];
        if (debugPrint) {
            Serial.printf("[Axis %d] src=0x%02X pot%d=%d ch=%d bidir=%d gate=%d upd=%lums\n",
                i, axis.sourceAddress, axis.potIndex, pot, axis.outputChannel,
                axis.isBidirectional()?1:0, axis.buttonGate,
                lastUpdate > 0 ? (millis() - lastUpdate) : 99999UL);
        }
        if (millis() - lastUpdate < 1000) {
            uint16_t fwd, rev;
            mapAxis(axis, pot, fwd, rev);
            uint8_t chFwd = axis.outputChannel;
            uint8_t chRev = axis.outputChannel + 1;
            if (fwd != g_solenoidValues[chFwd]) {
                g_solenoidValues[chFwd] = fwd;
                setPWMTracked(chFwd, fwd);
            }
            if (axis.isBidirectional() && rev != g_solenoidValues[chRev]) {
                g_solenoidValues[chRev] = rev;
                setPWMTracked(chRev, rev);
            }
        } else {
            // Joystick timed out, zero both channels
            zeroAxisChannels(axis);
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
    int count = 0;
    while (g_can->receive(msg, 0)) {
        count++;
        if (count > 30) break;  // prevent lockup under heavy bus load
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
                    lastSolenoidUpdate = millis();  // Refresh safety timer on new CAN data
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
    data[7] = 0;
    g_can->sendBroadcast(PF_HEARTBEAT, data, 8, 6);
}

// Auto-configure on first boot: set up bidirectional paired channels
// Joy1 Pot1 -> ch0+1, Joy1 Pot2 -> ch2+3, Joy2 Pot1 -> ch4+5, Joy2 Pot2 -> ch6+7
static void autoConfigDefaults() {
    bool anyEnabled = false;
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        if (g_motorCfg.axes[i].isEnabled()) { anyEnabled = true; break; }
    }
    if (anyEnabled) return;  // already configured, don't overwrite

    Serial.println("[MotorDriver] No axis config found, applying defaults...");
    struct { uint8_t src; uint8_t pot; uint8_t ch; } defaults[] = {
        { 0x21, 0, 0 },  // Joy1 Pot1 -> ch0(fwd)+ch1(rev)
        { 0x21, 1, 2 },  // Joy1 Pot2 -> ch2(fwd)+ch3(rev)
        { 0x22, 0, 4 },  // Joy2 Pot1 -> ch4(fwd)+ch5(rev)
        { 0x22, 1, 6 },  // Joy2 Pot2 -> ch6(fwd)+ch7(rev)
    };
    for (int i = 0; i < 4; i++) {
        AxisConfig& ax = g_motorCfg.axes[i];
        ax.sourceAddress = defaults[i].src;
        ax.potIndex = defaults[i].pot;
        ax.outputChannel = defaults[i].ch;
        ax.deadbandMin = 307;   // midpoint ±20%
        ax.deadbandMax = 717;   // midpoint ±20%
        ax.pwmMin = 20;         // ~8%
        ax.pwmMax = 100;        // ~39%
        ax.flags = FLAG_AXIS_ENABLED | FLAG_AXIS_BIDIRECTIONAL;
        cfgMgr.saveAxisConfig(i, ax);
    }
    Serial.println("[MotorDriver] Default config saved to NVS");
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
    autoConfigDefaults();
    // Dump loaded config to serial
    Serial.println("[MotorDriver] Loaded axis config:");
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        const AxisConfig& a = g_motorCfg.axes[i];
        if (a.flags) {
            Serial.printf("[MotorDriver]   axis%d src=0x%02X pot=%d ch=%d db=%d-%d pwm=%d-%d flags=%d inv=%d gate=%d\n",
                i, a.sourceAddress, a.potIndex, a.outputChannel,
                a.deadbandMin, a.deadbandMax, a.pwmMin, a.pwmMax,
                a.flags, a.isInverted()?1:0, a.buttonGate);
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
        if (rxRead != level) pass = 0;
    }
    digitalWrite(CAN_TX_PIN, HIGH);  // idle state
    Serial.printf("[GPIO Test] Result: %s\n", pass ? "PASS - MCP2562 data path OK" : "FAIL - MCP2562 RXD not connected to IO17");

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
    Serial.printf("[MotorDriver] CAN pins: TX=IO%d RX=IO%d\n", CAN_TX_PIN, CAN_RX_PIN);
    can_output_setup(g_canOutputRules);
#if defined(ENABLE_OTA_WEBSERVER)
    char hostname[24];
    snprintf(hostname, sizeof(hostname), "forwarder-motor-%02X", g_can->getAddress());
    ota_setup(hostname);
#endif
    Serial.println("[MotorDriver] Setup complete, entering loop...");
}

static uint32_t lastStatusPrint = 0;
static uint32_t lastSelfTest = 0;
static uint32_t selfTestCount = 0;
static uint32_t loopCount = 0;

void ecu_loop() {
    uint32_t now = millis();
    loopCount++;
    yield();
    g_can->loop();
    processCAN();
    yield();
    updateAxes();
    yield();

    // Self-loopback test: send a test frame every 3s
    if (now - lastSelfTest >= 3000) {
        lastSelfTest = now;
        uint8_t testData[8] = {0xAA, 0xBB, (uint8_t)(selfTestCount & 0xFF), 0,0,0,0,0};
        selfTestCount++;
        bool sent = g_can->sendBroadcast(0xEF, testData, 8, 6);  // PGN 0xEF00 test
        Serial.printf("[SelfTest #%lu] sent=%d (TX_pin=IO%d RX_pin=IO%d)\n",
            selfTestCount, sent?1:0, CAN_TX_PIN, CAN_RX_PIN);
        if (!sent) {
            Serial.printf("[SelfTest] TX FAILED - check CAN bus\n");
        }
        yield();
    }

    // Periodic CAN bus status
    if (now - lastStatusPrint >= 5000) {
        lastStatusPrint = now;
        Serial.printf("[CAN Status] online=%d tx=%lu rx=%lu err=%lu\n",
            g_can->isOnline()?1:0, g_can->getTxCount(), g_can->getRxCount(), g_can->getErrorCount());
    }
    if (now - lastSolenoidUpdate > SAFETY_TIMEOUT_MS) {
        if (lastSolenoidUpdate != 0) {
            Serial.printf("[SAFETY] Timeout %lums since last update, all outputs OFF\n",
                (unsigned long)(now - lastSolenoidUpdate));
            allOff("safety");
            lastSolenoidUpdate = 0;  // Prevent re-firing until fresh CAN data arrives
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
    // Broadcast PCA9685 output values when PWM changes (rate-limited to 20Hz max)
    if (g_outputDirty && (now - lastOutputBroadcast >= 50)) {
        lastOutputBroadcast = now;
        g_outputDirty = false;
        // Message 1: axes 0-3 (channels 0-7)
        {
            uint8_t data[8];
            for (int i = 0; i < 4; i++) {
                int16_t val = (int16_t)g_solenoidValues[i * 2] - (int16_t)g_solenoidValues[i * 2 + 1];
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
                int16_t val = (int16_t)g_solenoidValues[idx * 2] - (int16_t)g_solenoidValues[idx * 2 + 1];
                data[i * 2] = (uint8_t)(val & 0xFF);
                data[i * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
            }
            g_can->sendBroadcast(PF_MOTOR_OUTPUT2, data, 8, 6);
        }
    }
    updateLED();
    can_output_loop();
#if defined(ENABLE_OTA_WEBSERVER)
    yield();
    ota_loop();
#endif
}

#endif // ECU_TYPE_MOTOR_DRIVER
