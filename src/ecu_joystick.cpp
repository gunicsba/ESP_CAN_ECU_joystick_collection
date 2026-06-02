#include "ecu_joystick.h"

#if defined(ECU_TYPE_JOYSTICK)

#include <NeoPixelBus.h>
#include "ForwarderCAN.h"
#include "ForwarderConfig.h"
#include "ota_webserver.h"
#include "web_state.h"

#ifndef CAN_TX_PIN
#define CAN_TX_PIN 5
#endif
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 4
#endif
#ifndef CAN_SE_PIN
#define CAN_SE_PIN -1
#endif
#ifndef POT1_PIN
#define POT1_PIN 6
#endif
#ifndef POT2_PIN
#define POT2_PIN 7
#endif
#ifndef BTN1_PIN
#define BTN1_PIN 16
#endif
#ifndef BTN2_PIN
#define BTN2_PIN 17
#endif
#ifndef WS2812_PIN
#define WS2812_PIN 48
#endif
#ifndef ECU_JOYSTICK_ID
#define ECU_JOYSTICK_ID 1
#endif

static NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(1, WS2812_PIN);
ForwarderCAN* g_can = nullptr;
static ForwarderConfig cfgMgr("joycfg");

uint16_t g_localPot1 = 512, g_localPot2 = 512;
bool g_localBtn1 = false, g_localBtn2 = false;
uint8_t g_ecuJoystickId = ECU_JOYSTICK_ID;

static uint16_t prevPot1 = 0xFFFF, prevPot2 = 0xFFFF;
static uint8_t prevButtons = 0xFF;
static uint32_t lastSend = 0;
static uint32_t lastHeartbeat = 0;
static uint32_t lastLedUpdate = 0;
static uint8_t ledR = 0, ledG = 20, ledB = 0;
static uint8_t ledBrightness = 20;
static uint32_t identifyTimer = 0;
static bool identifyActive = false;

static const uint8_t ECU_NAME[8] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    (ECU_JOYSTICK_ID & 0xFF)
};

static void readInputs() {
    g_localPot1 = analogRead(POT1_PIN);
    g_localPot2 = analogRead(POT2_PIN);
    g_localBtn1 = digitalRead(BTN1_PIN) == LOW;
    g_localBtn2 = digitalRead(BTN2_PIN) == LOW;
}

static void updateLED() {
    uint32_t now = millis();
    if (now - lastLedUpdate < 50) return;
    lastLedUpdate = now;
    RgbColor color(
        (ledR * ledBrightness) / 255,
        (ledG * ledBrightness) / 255,
        (ledB * ledBrightness) / 255
    );
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
        if ((now / 500) % 2 == 0) {
            color = RgbColor(20, 10, 0);
        } else {
            color = RgbColor(0, 0, 0);
        }
    }
    strip.SetPixelColor(0, color);
    strip.Show();
}

static void sendPot(uint8_t pf, uint16_t value) {
    uint8_t data[2];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    g_can->sendBroadcast(pf, data, 2, 6);
}

static void sendButtons() {
    uint8_t data[1];
    data[0] = 0;
    if (g_localBtn1) data[0] |= 0x01;
    if (g_localBtn2) data[0] |= 0x02;
    g_can->sendBroadcast(PF_JOYSTICK_BUTTONS, data, 1, 6);
}

static void processCAN() {
    CANMessage msg;
    while (g_can->receive(msg, 0)) {
        uint8_t pf = J1939_GET_PF(msg.id);
        uint8_t ps = J1939_GET_PS(msg.id);
        if (pf == PF_LED_COLOR) {
            if (ps == DA_BROADCAST || ps == g_can->getAddress()) {
                if (msg.len >= 3) {
                    ledR = msg.data[0];
                    ledG = msg.data[1];
                    ledB = msg.data[2];
                }
            }
        } else if (pf == PF_IDENTIFY) {
            if (ps == DA_BROADCAST || ps == g_can->getAddress()) {
                identifyActive = true;
                identifyTimer = millis();
            }
        } else if (pf == PF_SET_ADDRESS) {
            if (ps == g_can->getAddress() && msg.len >= 1) {
                uint8_t newAddr = msg.data[0];
                if (newAddr >= 0x20 && newAddr <= 0xEF) {
                    cfgMgr.setForcedAddress(newAddr);
                    Serial.printf("[Joystick%d] New address 0x%02X saved, rebooting...\n", ECU_JOYSTICK_ID, newAddr);
                    delay(200);
                    ESP.restart();
                }
            }
        }
    }
}

static void sendHeartbeat() {
    uint8_t data[8];
    data[0] = g_can->isOnline() ? 0x01 : 0x00;
    data[1] = (uint8_t)(millis() / 1000);
    data[2] = (uint8_t)((millis() / 1000) >> 8);
    data[3] = ECU_JOYSTICK_ID;
    data[4] = (uint8_t)(g_can->getRxCount() & 0xFF);
    data[5] = (uint8_t)(g_can->getTxCount() & 0xFF);
    data[6] = 0;
    data[7] = 0;
    g_can->sendBroadcast(PF_HEARTBEAT, data, 8, 6);
}

void ecu_setup() {
    strip.Begin();
    strip.SetPixelColor(0, RgbColor(0, 20, 0));
    strip.Show();
    analogReadResolution(10);
    analogSetAttenuation(ADC_11db);
    pinMode(BTN1_PIN, INPUT_PULLUP);
    pinMode(BTN2_PIN, INPUT_PULLUP);
#if CAN_SE_PIN >= 0
    pinMode(CAN_SE_PIN, OUTPUT);
    digitalWrite(CAN_SE_PIN, HIGH);  // Enable CAN transceiver
#endif
    cfgMgr.begin();
    uint8_t forcedAddr = cfgMgr.getForcedAddress(ECU_PREFERRED_ADDRESS);
    Serial.printf("[Joystick%d] Initializing CAN...\n", ECU_JOYSTICK_ID);
    g_can = new ForwarderCAN(forcedAddr, ECU_NAME);
    if (!g_can->begin(CAN_TX_PIN, CAN_RX_PIN, CAN_BITRATE)) {
        Serial.println("[Joystick] CAN init FAILED!");
        while (1) {
            strip.SetPixelColor(0, RgbColor(20, 0, 0));
            strip.Show();
            delay(200);
            strip.SetPixelColor(0, RgbColor(0, 0, 0));
            strip.Show();
            delay(200);
        }
    }
    Serial.printf("[Joystick%d] Ready on address 0x%02X.\n", ECU_JOYSTICK_ID, g_can->getAddress());
#if defined(ENABLE_OTA_WEBSERVER)
    char hostname[24];
    snprintf(hostname, sizeof(hostname), "forwarder-joy%d", ECU_JOYSTICK_ID);
    ota_setup(hostname);
#endif
}

void ecu_loop() {
    uint32_t now = millis();
    g_can->loop();
    readInputs();
    processCAN();
    bool changed = false;
    if (abs((int)g_localPot1 - (int)prevPot1) > 2) {
        prevPot1 = g_localPot1;
        sendPot(PF_JOYSTICK_POT1, g_localPot1);
        changed = true;
    }
    if (abs((int)g_localPot2 - (int)prevPot2) > 2) {
        prevPot2 = g_localPot2;
        sendPot(PF_JOYSTICK_POT2, g_localPot2);
        changed = true;
    }
    uint8_t buttons = 0;
    if (g_localBtn1) buttons |= 0x01;
    if (g_localBtn2) buttons |= 0x02;
    if (buttons != prevButtons) {
        prevButtons = buttons;
        sendButtons();
        changed = true;
    }
    if (now - lastSend >= 100) {
        lastSend = now;
        if (!changed) {
            sendPot(PF_JOYSTICK_POT1, g_localPot1);
            sendPot(PF_JOYSTICK_POT2, g_localPot2);
            sendButtons();
        }
    }
    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        if (g_can->isOnline()) {
            sendHeartbeat();
        }
    }
    updateLED();
#if defined(ENABLE_OTA_WEBSERVER)
    ota_loop();
#endif
}

#endif // ECU_TYPE_JOYSTICK
