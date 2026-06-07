#include "ForwarderConfig.h"

// ---------------------------------------------------------------------------
// AxisConfig serialization
// ---------------------------------------------------------------------------
void AxisConfig::pack(uint8_t buf[8], uint8_t axisIdx) const {
    buf[0] = axisIdx;
    buf[1] = sourceAddress;
    buf[2] = ((potIndex & 0x03) << 6) | ((outputChannel & 0x0F) << 2) | (flags & 0x03);
    buf[3] = (uint8_t)(deadbandMin / 4);
    buf[4] = (uint8_t)(deadbandMax / 4);
    buf[5] = pwmMin;
    buf[6] = pwmMax;
    buf[7] = (buttonGate & 0x03) | ((flags & FLAG_AXIS_INVERT) ? 0x04 : 0x00);
}

void AxisConfig::unpack(const uint8_t buf[8]) {
    sourceAddress = buf[1];
    potIndex      = (buf[2] >> 6) & 0x03;
    outputChannel = (buf[2] >> 2) & 0x0F;
    flags         = buf[2] & 0x03;
    deadbandMin   = ((uint16_t)buf[3]) * 4;
    deadbandMax   = ((uint16_t)buf[4]) * 4;
    pwmMin        = buf[5];
    pwmMax        = buf[6];
    buttonGate    = buf[7] & 0x03;
    if (buf[7] & 0x04) flags |= FLAG_AXIS_INVERT;
}

// ---------------------------------------------------------------------------
// CanOutputRule serialization
// ---------------------------------------------------------------------------
void CanOutputRule::pack(uint8_t buf[8]) const {
    buf[0] = enabled ? 1 : 0;
    buf[1] = matchPF;
    buf[2] = matchSA;
    buf[3] = gpioPin;
    buf[4] = mode;
    buf[5] = (uint8_t)(momentaryMs & 0xFF);
    buf[6] = (uint8_t)(momentaryMs >> 8);
    buf[7] = 0;
}

void CanOutputRule::unpack(const uint8_t buf[8]) {
    enabled    = buf[0] != 0;
    matchPF    = buf[1];
    matchSA    = buf[2];
    gpioPin    = buf[3];
    mode       = buf[4];
    momentaryMs = (uint16_t)buf[5] | ((uint16_t)buf[6] << 8);
}

// ---------------------------------------------------------------------------
// ForwarderConfig
// ---------------------------------------------------------------------------
ForwarderConfig::ForwarderConfig(const char* ns) : _ns(ns) {}

bool ForwarderConfig::begin() {
    _started = _prefs.begin(_ns, false);
    return _started;
}

uint8_t ForwarderConfig::getForcedAddress(uint8_t defaultAddr) {
    if (!_started) return defaultAddr;
    return _prefs.getUChar("forced_addr", defaultAddr);
}

void ForwarderConfig::setForcedAddress(uint8_t addr) {
    if (!_started) return;
    _prefs.putUChar("forced_addr", addr);
}

void ForwarderConfig::clearForcedAddress() {
    if (!_started) return;
    _prefs.remove("forced_addr");
}

bool ForwarderConfig::loadMotorConfig(MotorConfig& cfg) {
    if (!_started) {
        loadDefaults(cfg);
        return false;
    }
    cfg.pcaCount = _prefs.getUChar("pca_count", MAX_PCA_COUNT);
    if (cfg.pcaCount < 1 || cfg.pcaCount > MAX_PCA_COUNT) cfg.pcaCount = MAX_PCA_COUNT;

    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        char key[12];
        snprintf(key, sizeof(key), "axis_%d", i);
        if (_prefs.isKey(key)) {
            size_t len = _prefs.getBytesLength(key);
            if (len >= 8) {
                uint8_t buf[8];
                _prefs.getBytes(key, buf, sizeof(buf));
                cfg.axes[i].unpack(buf);
            }
        } else {
            cfg.axes[i].flags = 0;
            cfg.axes[i].sourceAddress = 0;
            cfg.axes[i].potIndex = 0;
            cfg.axes[i].outputChannel = i;
            cfg.axes[i].deadbandMin = 492;  // ~48%
            cfg.axes[i].deadbandMax = 532;  // ~52%
            cfg.axes[i].pwmMin = 64;        // ~25%
            cfg.axes[i].pwmMax = 128;       // ~50%
            cfg.axes[i].buttonGate = BUTTON_GATE_NONE;
        }
    }
    return true;
}

bool ForwarderConfig::saveMotorConfig(const MotorConfig& cfg) {
    if (!_started) return false;
    _prefs.putUChar("pca_count", cfg.pcaCount);
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        char key[12];
        snprintf(key, sizeof(key), "axis_%d", i);
        uint8_t buf[8];
        cfg.axes[i].pack(buf, i);
        _prefs.putBytes(key, buf, 8);
    }
    return true;
}

bool ForwarderConfig::saveAxisConfig(uint8_t axisIdx, const AxisConfig& axis) {
    if (!_started || axisIdx >= MAX_AXIS_COUNT) return false;
    char key[12];
    snprintf(key, sizeof(key), "axis_%d", axisIdx);
    uint8_t buf[8];
    axis.pack(buf, axisIdx);
    _prefs.putBytes(key, buf, 8);
    return true;
}

bool ForwarderConfig::loadCanOutputRules(CanOutputRule rules[MAX_CAN_OUTPUT_RULES]) {
    if (!_started) {
        for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
            rules[i].enabled = false;
            rules[i].matchPF = 0;
            rules[i].matchSA = 0;
            rules[i].gpioPin = 0;
            rules[i].mode = 0;
            rules[i].momentaryMs = 500;
        }
        return false;
    }
    for (int i = 0; i < MAX_CAN_OUTPUT_RULES; i++) {
        char key[12];
        snprintf(key, sizeof(key), "canout_%d", i);
        if (_prefs.isKey(key)) {
            size_t len = _prefs.getBytesLength(key);
            if (len >= 7) {
                uint8_t buf[8];
                _prefs.getBytes(key, buf, sizeof(buf));
                rules[i].unpack(buf);
            }
        } else {
            rules[i].enabled = false;
            rules[i].matchPF = 0;
            rules[i].matchSA = 0;
            rules[i].gpioPin = 0;
            rules[i].mode = 0;
            rules[i].momentaryMs = 500;
        }
    }
    return true;
}

bool ForwarderConfig::saveCanOutputRule(uint8_t index, const CanOutputRule& rule) {
    if (!_started || index >= MAX_CAN_OUTPUT_RULES) return false;
    char key[12];
    snprintf(key, sizeof(key), "canout_%d", index);
    uint8_t buf[8];
    rule.pack(buf);
    _prefs.putBytes(key, buf, 8);
    return true;
}

void ForwarderConfig::loadDefaults(MotorConfig& cfg) {
    cfg.pcaCount = MAX_PCA_COUNT;
    for (int i = 0; i < MAX_AXIS_COUNT; i++) {
        cfg.axes[i].flags = 0;
        cfg.axes[i].sourceAddress = 0;
        cfg.axes[i].potIndex = 0;
        cfg.axes[i].outputChannel = i;
        cfg.axes[i].deadbandMin = 492;
        cfg.axes[i].deadbandMax = 532;
        cfg.axes[i].pwmMin = 64;
        cfg.axes[i].pwmMax = 128;
        cfg.axes[i].buttonGate = BUTTON_GATE_NONE;
    }
}
