#include "can_output.h"

static CanOutputRule* s_rules = nullptr;
static bool s_outputState[MAX_CAN_OUTPUT_RULES] = {false};
static uint32_t s_momentaryTimer[MAX_CAN_OUTPUT_RULES] = {0};

void can_output_setup(CanOutputRule rules[MAX_CAN_OUTPUT_RULES]) {
    s_rules = rules;
    for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
        if (rules[i].enabled && rules[i].gpioPin > 0) {
            pinMode(rules[i].gpioPin, OUTPUT);
            digitalWrite(rules[i].gpioPin, LOW);
            s_outputState[i] = false;
            Serial.printf("[CANOutput] Rule %d: PF=0x%02X SA=0x%02X pin=%d mode=%s\n",
                i, rules[i].matchPF, rules[i].matchSA, rules[i].gpioPin,
                rules[i].mode == 0 ? "toggle" : "momentary");
        }
    }
}

static void setOutput(uint8_t ruleIdx, bool state) {
    if (!s_rules || s_rules[ruleIdx].gpioPin == 0) return;
    s_outputState[ruleIdx] = state;
    digitalWrite(s_rules[ruleIdx].gpioPin, state ? HIGH : LOW);
    Serial.printf("[CANOutput] Rule %d: pin %d -> %s\n",
        ruleIdx, s_rules[ruleIdx].gpioPin, state ? "HIGH" : "LOW");
}

void can_output_process_can(CANMessage& msg) {
    if (!s_rules) return;
    uint8_t pf = J1939_GET_PF(msg.id);
    uint8_t sa = J1939_GET_SA(msg.id);

    for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
        if (!s_rules[i].enabled || s_rules[i].gpioPin == 0) continue;
        if (s_rules[i].matchPF != pf) continue;
        if (s_rules[i].matchSA != 0 && s_rules[i].matchSA != sa) continue;

        // Matched!
        if (s_rules[i].mode == 0) {
            // Toggle mode
            setOutput(i, !s_outputState[i]);
        } else {
            // Momentary mode - pulse HIGH then back LOW
            setOutput(i, true);
            s_momentaryTimer[i] = millis();
        }
    }
}

void can_output_loop() {
    if (!s_rules) return;
    uint32_t now = millis();
    for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
        if (!s_rules[i].enabled || s_rules[i].mode != 1) continue;
        if (!s_outputState[i]) continue;
        if (s_rules[i].momentaryMs > 0 && (now - s_momentaryTimer[i] >= s_rules[i].momentaryMs)) {
            setOutput(i, false);
        }
    }
}

CanOutputRule* can_output_get_rules() {
    return s_rules;
}
