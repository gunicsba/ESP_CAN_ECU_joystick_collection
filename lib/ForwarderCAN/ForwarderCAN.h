#pragma once

#include <Arduino.h>
#include <driver/twai.h>

// ---------------------------------------------------------------------------
// J1939-like 29-bit ID layout
// ---------------------------------------------------------------------------
// Bits 28-26 : Priority (3)
// Bit  25    : Extended Data Page (1) - not used, 0
// Bit  24    : Data Page (1)
// Bits 23-16 : PDU Format (PF)
// Bits 15-8  : PDU Specific (PS) = DA when PF < 240, Group Ext when PF >= 240
// Bits 7-0   : Source Address (SA)
// ---------------------------------------------------------------------------

#define J1939_PRIORITY_SHIFT    26
#define J1939_DP_SHIFT          24
#define J1939_PF_SHIFT          16
#define J1939_PS_SHIFT          8

#define J1939_MAKE_ID(prio, dp, pf, ps, sa) \
    (((uint32_t)(prio & 0x7) << J1939_PRIORITY_SHIFT) | \
     ((uint32_t)(dp  & 0x1) << J1939_DP_SHIFT) | \
     ((uint32_t)(pf  & 0xFF) << J1939_PF_SHIFT) | \
     ((uint32_t)(ps  & 0xFF) << J1939_PS_SHIFT) | \
     ((uint32_t)(sa  & 0xFF)))

#define J1939_GET_PRIORITY(id)  (((id) >> J1939_PRIORITY_SHIFT) & 0x7)
#define J1939_GET_DP(id)        (((id) >> J1939_DP_SHIFT) & 0x1)
#define J1939_GET_PF(id)        (((id) >> J1939_PF_SHIFT) & 0xFF)
#define J1939_GET_PS(id)        (((id) >> J1939_PS_SHIFT) & 0xFF)
#define J1939_GET_SA(id)        ((id) & 0xFF)

#define J1939_PF_ADDRESS_CLAIMED    0xEE  // 238 (>=240? No, 238 < 240, but J1939 uses 254/255 for network management)
#define J1939_PF_REQUEST_AC         0xEA  // Request for Address Claimed

// Custom PF definitions (all < 240 so PS = Destination Address)
#define PF_JOYSTICK_POT1            0x10
#define PF_JOYSTICK_POT2            0x11
#define PF_JOYSTICK_POT3            0x12
#define PF_JOYSTICK_BUTTONS         0x13
#define PF_LED_COLOR                0x20
#define PF_SOLENOID_CMD             0x21
#define PF_IDENTIFY                 0x22
#define PF_SET_ADDRESS              0x23
#define PF_CONFIG_AXIS              0x24
#define PF_REQUEST_CONFIG           0x25
#define PF_CONFIG_RESPONSE          0x26
#define PF_HEARTBEAT                0x30

// Broadcast destination address
#define DA_BROADCAST                0xFF

// Special source addresses
#define SA_CANNOT_CLAIM             0xFE
#define SA_BROADCAST                0xFF

struct CANMessage {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  len;
    bool     ext;
};

class ForwarderCAN {
public:
    ForwarderCAN(uint8_t preferredAddress, const uint8_t name[8]);

    bool begin(int txPin, int rxPin, uint32_t bitrate);
    void loop();

    // Address claiming state
    enum AddrClaimState {
        ACS_CLAIMING,       // Sending address claimed, waiting
        ACS_CLAIMED,        // Address successfully claimed
        ACS_CANNOT_CLAIM,   // Address conflict, using 0xFE
        ACS_WAIT_RETRY      // Waiting to retry claim
    };

    bool isOnline() const { return _state == ACS_CLAIMED; }
    uint8_t getAddress() const { return _currentAddress; }
    AddrClaimState getState() const { return _state; }

    // Send a message using current source address
    bool send(uint8_t pf, uint8_t ps, const uint8_t* data, uint8_t len, uint8_t priority = 6);
    bool sendBroadcast(uint8_t pf, const uint8_t* data, uint8_t len, uint8_t priority = 6);

    // Receive
    bool receive(CANMessage& msg, uint32_t timeoutMs = 0);
    bool hasMessage() const;

    // Statistics
    uint32_t getTxCount() const { return _txCount; }
    uint32_t getRxCount() const { return _rxCount; }
    uint32_t getErrorCount() const { return _errCount; }

private:
    void sendAddressClaimed();
    void processNetworkManagement(const CANMessage& msg);
    void tryClaimAddress();

    uint8_t  _preferredAddress;
    uint8_t  _currentAddress;
    uint8_t  _name[8];
    AddrClaimState _state;

    uint32_t _claimTimer;
    uint32_t _claimAttempts;
    static constexpr uint32_t CLAIM_TIMEOUT_MS = 250;
    static constexpr uint32_t CLAIM_RETRY_MS = 1000;
    static constexpr uint32_t MAX_CLAIM_ATTEMPTS = 5;

    uint32_t _txCount = 0;
    uint32_t _rxCount = 0;
    uint32_t _errCount = 0;

    bool _twaiStarted = false;
    int _txPin = 0;
    int _rxPin = 0;
    uint32_t _bitrate = 250000;

    // Ring buffer for non-network-management messages consumed by loop()
    static constexpr uint8_t RX_BUF_SIZE = 32;
    CANMessage _rxBuf[RX_BUF_SIZE];
    uint8_t _rxBufHead = 0;
    uint8_t _rxBufTail = 0;
    uint8_t _rxBufCount = 0;

    bool bufEmpty() const { return _rxBufCount == 0; }
    bool bufFull() const { return _rxBufCount >= RX_BUF_SIZE; }
    void bufPush(const CANMessage& msg);
    bool bufPop(CANMessage& msg);
};
