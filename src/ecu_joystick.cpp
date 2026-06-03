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
#ifndef CAN_SPEED_MODE_PIN
#define CAN_SPEED_MODE_PIN -1
#endif
#ifndef CAN_ME2107_EN_PIN
#define CAN_ME2107_EN_PIN -1
#endif
#ifndef POT1_PIN
#define POT1_PIN 6
#endif
#ifndef POT2_PIN
#define POT2_PIN 7
#endif
#ifndef POT3_PIN
#define POT3_PIN 34
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

uint16_t g_localPot1 = 512, g_localPot2 = 512, g_localPot3 = 512;
bool g_localBtn1 = false, g_localBtn2 = false;
uint8_t g_ecuJoystickId = ECU_JOYSTICK_ID;

static uint16_t prevPot1 = 0xFFFF, prevPot2 = 0xFFFF, prevPot3 = 0xFFFF;
static uint8_t prevButtons = 0xFF;
static uint32_t lastSend = 0;
static uint32_t lastHeartbeat = 0;
static uint32_t lastLedUpdate = 0;
static uint8_t ledR = 0, ledG = 8, ledB = 0;
static uint8_t ledBrightness = 255;
static uint32_t identifyTimer = 0;
static bool identifyActive = false;

// Button debounce: require stable state for 50ms
static uint32_t btn1Debounce = 0, btn2Debounce = 0;
static bool btn1Raw = false, btn2Raw = false;
static constexpr uint32_t DEBOUNCE_MS = 50;

static const uint8_t ECU_NAME[8] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    (ECU_JOYSTICK_ID & 0xFF)
};

static void readInputs() {
    g_localPot1 = analogRead(POT1_PIN);
    g_localPot2 = analogRead(POT2_PIN);
    g_localPot3 = analogRead(POT3_PIN);

    // Debounce buttons: only accept state change after stable for DEBOUNCE_MS
    uint32_t now = millis();
    bool b1 = digitalRead(BTN1_PIN) == LOW;
    bool b2 = digitalRead(BTN2_PIN) == LOW;
    if (b1 != btn1Raw) { btn1Raw = b1; btn1Debounce = now; }
    if (b2 != btn2Raw) { btn2Raw = b2; btn2Debounce = now; }
    if (btn1Raw != g_localBtn1 && now - btn1Debounce >= DEBOUNCE_MS) g_localBtn1 = btn1Raw;
    if (btn2Raw != g_localBtn2 && now - btn2Debounce >= DEBOUNCE_MS) g_localBtn2 = btn2Raw;
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
    int count = 0;
    while (g_can->receive(msg, 0)) {
        count++;
        if (count > 30) break;  // prevent lockup under heavy bus load
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
    // LED startup test: flash RGB
    strip.SetPixelColor(0, RgbColor(255, 0, 0)); strip.Show(); delay(200);
    strip.SetPixelColor(0, RgbColor(0, 255, 0)); strip.Show(); delay(200);
    strip.SetPixelColor(0, RgbColor(0, 0, 255)); strip.Show(); delay(200);
    strip.SetPixelColor(0, RgbColor(0, 20, 0));  // dim green = ready
    strip.Show();
    analogReadResolution(10);
    analogSetAttenuation(ADC_11db);
    pinMode(BTN1_PIN, INPUT_PULLUP);
    pinMode(BTN2_PIN, INPUT_PULLUP);
#if CAN_ME2107_EN_PIN >= 0
    pinMode(CAN_ME2107_EN_PIN, OUTPUT);
    digitalWrite(CAN_ME2107_EN_PIN, HIGH);  // Enable CAN transceiver power supply
#endif
#if CAN_SPEED_MODE_PIN >= 0
    pinMode(CAN_SPEED_MODE_PIN, OUTPUT);
    digitalWrite(CAN_SPEED_MODE_PIN, LOW);  // High-speed mode (LOW = up to 1Mbps)
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
    Serial.printf("[Joystick%d] CAN_TX=%d CAN_RX=%d SPEED_MODE=%d ME2107_EN=%d bitrate=%d\n",
        ECU_JOYSTICK_ID, CAN_TX_PIN, CAN_RX_PIN, CAN_SPEED_MODE_PIN, CAN_ME2107_EN_PIN, CAN_BITRATE);
#if defined(ENABLE_OTA_WEBSERVER)
    // Start WiFi AFTER CAN. With proper transceiver init, TWAI is stable.
    char hostname[24];
    snprintf(hostname, sizeof(hostname), "forwarder-joy%d-%02X", ECU_JOYSTICK_ID, forcedAddr);
    ota_setup(hostname);
#endif
}

void ecu_loop() {
    uint32_t now = millis();
    yield();
    g_can->loop();
    readInputs();
    processCAN();
    yield();

    // Send all data at max 25Hz (40ms interval)
    if (now - lastSend >= 40) {
        lastSend = now;
        sendPot(PF_JOYSTICK_POT1, g_localPot1);
        sendPot(PF_JOYSTICK_POT2, g_localPot2);
        sendPot(PF_JOYSTICK_POT3, g_localPot3);
        sendButtons();
        yield();
        prevPot1 = g_localPot1;
        prevPot2 = g_localPot2;
        prevPot3 = g_localPot3;
        prevButtons = (g_localBtn1 ? 0x01 : 0) | (g_localBtn2 ? 0x02 : 0);
    }
    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        if (g_can->isOnline()) {
            sendHeartbeat();
        }
        // TWAI-level diagnostics
        twai_status_info_t twai_status;
        if (twai_get_status_info(&twai_status) == ESP_OK) {
            const char* state_names[] = {"STOPPED","RUNNING","BUS_OFF","REC"};
            const char* sname = (twai_status.state < 4) ? state_names[twai_status.state] : "???";
            Serial.printf("[CAN] TX:%lu RX:%lu ERR:%lu state=%d pots=%d,%d,%d btn=%d,%d\n",
                g_can->getTxCount(), g_can->getRxCount(), g_can->getErrorCount(),
                g_can->getState(), g_localPot1, g_localPot2, g_localPot3,
                g_localBtn1, g_localBtn2);
            Serial.printf("[TWAI] hw_state=%s msgs_to_tx=%lu msgs_to_rx=%lu tx_err=%lu rx_err=%lu arb_lost=%lu bus_err=%lu\n",
                sname, twai_status.msgs_to_tx, twai_status.msgs_to_rx,
                twai_status.tx_error_counter, twai_status.rx_error_counter,
                twai_status.arb_lost_count, twai_status.bus_error_count);
        }
    }
    updateLED();
#if defined(ENABLE_OTA_WEBSERVER)
    yield();
    ota_loop();
#endif
}

#endif // ECU_TYPE_JOYSTICK
