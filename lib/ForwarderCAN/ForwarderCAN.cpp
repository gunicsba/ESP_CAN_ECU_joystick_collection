#include "ForwarderCAN.h"

// Ring buffer helpers
void ForwarderCAN::bufPush(const CANMessage& msg) {
    if (bufFull()) return;  // drop oldest if full
    _rxBuf[_rxBufHead] = msg;
    _rxBufHead = (_rxBufHead + 1) % RX_BUF_SIZE;
    _rxBufCount++;
}

bool ForwarderCAN::bufPop(CANMessage& msg) {
    if (bufEmpty()) return false;
    msg = _rxBuf[_rxBufTail];
    _rxBufTail = (_rxBufTail + 1) % RX_BUF_SIZE;
    _rxBufCount--;
    return true;
}

ForwarderCAN::ForwarderCAN(uint8_t preferredAddress, const uint8_t name[8])
    : _preferredAddress(preferredAddress),
      _currentAddress(SA_CANNOT_CLAIM),
      _state(ACS_WAIT_RETRY),
      _claimTimer(0),
      _claimAttempts(0)
{
    memcpy(_name, name, 8);
}

bool ForwarderCAN::begin(int txPin, int rxPin, uint32_t bitrate) {
    _txPin = txPin;
    _rxPin = rxPin;
    _bitrate = bitrate;

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)txPin, (gpio_num_t)rxPin, TWAI_MODE_NO_ACK);
    g_config.tx_queue_len = 16;
    g_config.rx_queue_len = 32;

    twai_timing_config_t t_config;
    switch (bitrate) {
        case 125000:  t_config = TWAI_TIMING_CONFIG_125KBITS(); break;
        case 250000:  t_config = TWAI_TIMING_CONFIG_250KBITS(); break;
        case 500000:  t_config = TWAI_TIMING_CONFIG_500KBITS(); break;
        case 1000000: t_config = TWAI_TIMING_CONFIG_1MBITS(); break;
        default:      t_config = TWAI_TIMING_CONFIG_250KBITS(); break;
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[CAN] Driver install failed");
        return false;
    }

    if (twai_start() != ESP_OK) {
        Serial.println("[CAN] Start failed");
        return false;
    }

    _twaiStarted = true;

    // Print detailed TWAI status
    twai_status_info_t initStatus;
    if (twai_get_status_info(&initStatus) == ESP_OK) {
        Serial.printf("[CAN] Started: state=%d tx_err=%lu rx_err=%lu msgs_rx=%lu\n",
            initStatus.state, initStatus.tx_error_counter,
            initStatus.rx_error_counter, initStatus.msgs_to_rx);
    }

    // Configure alerts (per manufacturer recommendation)
    uint32_t alerts = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                      TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                      TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA |
                      TWAI_ALERT_RX_QUEUE_FULL;
    twai_reconfigure_alerts(alerts, NULL);

    // Start address claiming
    tryClaimAddress();
    return true;
}

void ForwarderCAN::tryClaimAddress() {
    _currentAddress = _preferredAddress;
    _state = ACS_CLAIMING;
    _claimTimer = millis();
    _claimAttempts++;
    sendAddressClaimed();
    Serial.printf("[CAN] Claiming address 0x%02X (attempt %u)\n", _currentAddress, _claimAttempts);
}

void ForwarderCAN::sendAddressClaimed() {
    uint32_t id = J1939_MAKE_ID(6, 0, J1939_PF_ADDRESS_CLAIMED, DA_BROADCAST, _currentAddress);
    twai_message_t msg;
    msg.identifier = id;
    msg.extd = 1;
    msg.rtr = 0;
    msg.data_length_code = 8;
    memcpy(msg.data, _name, 8);

    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        _txCount++;
    } else {
        _errCount++;
    }
}

void ForwarderCAN::loop() {
    if (!_twaiStarted) return;

    // Check bus state and recover
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        if (status.state == TWAI_STATE_STOPPED) {
            Serial.printf("[CAN] TWAI stopped (tx_err=%lu rx_err=%lu), restarting...\n",
                status.tx_error_counter, status.rx_error_counter);
            twai_start();
            delay(10);
            return;
        }
        if (status.state == TWAI_STATE_BUS_OFF) {
            twai_initiate_recovery();
            _errCount++;
        }
    }

    // Read alerts to clear them
    uint32_t alerts;
    twai_read_alerts(&alerts, 0);

    // Address claiming state machine
    if (_state == ACS_CLAIMING) {
        if (millis() - _claimTimer >= CLAIM_TIMEOUT_MS) {
            _state = ACS_CLAIMED;
            Serial.printf("[CAN] Address 0x%02X claimed successfully\n", _currentAddress);
        }
    } else if (_state == ACS_CANNOT_CLAIM) {
        if (millis() - _claimTimer >= CLAIM_RETRY_MS) {
            if (_claimAttempts < MAX_CLAIM_ATTEMPTS) {
                tryClaimAddress();
            } else {
                _preferredAddress = 0x30 + (_name[7] & 0x0F);
                _claimAttempts = 0;
                tryClaimAddress();
            }
        }
    }

    yield();  // Allow FreeRTOS background tasks to run between phases

    // Process incoming network management messages, buffer the rest
    CANMessage rx;
    int rxCount = 0;
    while (receive(rx, 0)) {
        rxCount++;
        if (rxCount > 30) break;  // prevent lockup under heavy bus load
        uint8_t pf = J1939_GET_PF(rx.id);
        if (pf == J1939_PF_ADDRESS_CLAIMED || pf == J1939_PF_REQUEST_AC) {
            processNetworkManagement(rx);
        } else {
            bufPush(rx);  // preserve for application layer
        }
    }

    yield();  // Yield after RX processing
}

void ForwarderCAN::processNetworkManagement(const CANMessage& msg) {
    uint8_t sa = J1939_GET_SA(msg.id);

    if (sa == _currentAddress && _state == ACS_CLAIMING) {
        // Someone else is claiming our address
        // J1939 arbitration: lower NAME value wins
        if (msg.len >= 8) {
            int cmp = memcmp(_name, msg.data, 8);
            if (cmp > 0) {
                // We lose, must claim new address
                _state = ACS_CANNOT_CLAIM;
                _currentAddress = SA_CANNOT_CLAIM;
                _claimTimer = millis();
                Serial.println("[CAN] Lost address claim arbitration");
            } else if (cmp < 0) {
                // We win, re-claim to assert dominance
                sendAddressClaimed();
            }
            // If equal, it's our own message echoed back
        }
    }
}

bool ForwarderCAN::send(uint8_t pf, uint8_t ps, const uint8_t* data, uint8_t len, uint8_t priority) {
    if (!_twaiStarted) return false;
    if (_state != ACS_CLAIMED && pf != J1939_PF_ADDRESS_CLAIMED) return false;
    if (len > 8) len = 8;

    // Check if TWAI is actually running before attempting transmit
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK || status.state != TWAI_STATE_RUNNING) {
        return false;
    }

    uint32_t id = J1939_MAKE_ID(priority, 0, pf, ps, _currentAddress);
    twai_message_t msg;
    msg.identifier = id;
    msg.extd = 1;
    msg.rtr = 0;
    msg.data_length_code = len;
    if (len > 0 && data != nullptr) {
        memcpy(msg.data, data, len);
    }

    esp_err_t ret = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (ret == ESP_OK) {
        _txCount++;
        return true;
    } else {
        _errCount++;
        return false;
    }
}

bool ForwarderCAN::sendBroadcast(uint8_t pf, const uint8_t* data, uint8_t len, uint8_t priority) {
    return send(pf, DA_BROADCAST, data, len, priority);
}

bool ForwarderCAN::receive(CANMessage& msg, uint32_t timeoutMs) {
    if (!_twaiStarted) return false;

    // Return buffered messages first (preserved by loop())
    if (bufPop(msg)) {
        return true;
    }

    twai_message_t rxMsg;
    esp_err_t ret = twai_receive(&rxMsg, timeoutMs == 0 ? 0 : pdMS_TO_TICKS(timeoutMs));
    if (ret == ESP_OK) {
        msg.id = rxMsg.identifier;
        msg.ext = rxMsg.extd;
        msg.len = rxMsg.data_length_code;
        if (msg.len > 8) msg.len = 8;
        memcpy(msg.data, rxMsg.data, msg.len);
        _rxCount++;
        return true;
    }
    return false;
}

bool ForwarderCAN::hasMessage() const {
    if (!_twaiStarted) return false;
    if (!bufEmpty()) return true;
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        return status.msgs_to_rx > 0;
    }
    return false;
}
