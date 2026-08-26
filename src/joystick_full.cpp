/*
 * Joystick Full - ESP32S3R8N8 CAN Board V1.0.0
 * 
 * Single unified joystick that exposes:
 * - All 4 ADS1115 channels (16-bit ADC)
 * - All 8 buttons from main PCA9555 @ 0x20
 * - All 16 buttons from extender PCA9555 (auto-detected at 0x21-0x23)
 * - Optocoupler control via extender PCA9555
 * 
 * CAN Address: 0x80
 * 
 * CAN Messages:
 *   - PF_JOYSTICK_POT1 (0x10): ADS channel 0
 *   - PF_JOYSTICK_POT2 (0x11): ADS channel 1
 *   - PF_JOYSTICK_POT3 (0x12): ADS channel 2
 *   - PF_JOYSTICK_POT4 (0x14): ADS channel 3
 *   - PF_JOYSTICK_BUTTONS (0x13): Main PCA9555 buttons (8 bits)
 *   - PF_EXTENDER_BUTTONS (0x15): Extender PCA9555 buttons byte 0 (8 bits)
 *   - PF_EXTENDER_BUTTONS2 (0x16): Extender PCA9555 buttons byte 1 (8 bits)
 *   - PF_HEARTBEAT (0x30): Heartbeat with status
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "esp_task_wdt.h"
#include "ForwarderCAN.h"

// I2C pins
#define I2C_SDA 39
#define I2C_SCL 38

// CAN pins
#define CAN_TX_PIN 10
#define CAN_RX_PIN 11

// LED pins
#define LED1_PIN 48
#define LED2_PIN 47

// CAN address for this joystick
#define JOYSTICK_ADDR 0x80

// PCA9555 addresses
#define PCA9555_MAIN_ADDR 0x20
// Extender address is auto-detected at boot (0x21-0x23, depends on A0/A1/A2 strapping)
static uint8_t pcaExtAddr = 0;

// PCA9555 registers
#define PCA9555_REG_INPUT0  0x00
#define PCA9555_REG_INPUT1  0x01
#define PCA9555_REG_OUTPUT0 0x02
#define PCA9555_REG_OUTPUT1 0x03
#define PCA9555_REG_CONFIG0 0x06
#define PCA9555_REG_CONFIG1 0x07

// CAN function codes
#define PF_JOYSTICK_POT1      0x10
#define PF_JOYSTICK_POT2      0x11
#define PF_JOYSTICK_POT3      0x12
#define PF_JOYSTICK_POT4      0x14
#define PF_JOYSTICK_BUTTONS   0x13
#define PF_EXTENDER_BUTTONS   0x15  // Extender PCA9555 byte 0
#define PF_EXTENDER_BUTTONS2  0x16  // Extender PCA9555 byte 1
#define PF_HEARTBEAT          0x30

// Optocoupler control via ESP32 GPIO
#define OPTO_PIN 18

Adafruit_ADS1115 ads;
ForwarderCAN *g_can = nullptr;

// Button states (active low)
uint8_t mainButtons = 0;      // 8 buttons from 0x20
uint8_t extButtons0 = 0;      // 8 buttons from 0x21 port 0
uint8_t extButtons1 = 0;      // 8 buttons from 0x21 port 1

// Analog values
uint16_t pot1 = 0, pot2 = 0, pot3 = 0, pot4 = 0;

// Optocoupler state
bool optoActive = false;

static uint32_t lastSend = 0;
static uint32_t lastHeartbeat = 0;

const uint8_t ECU_NAME[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, JOYSTICK_ADDR};

void pca9555_write(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t pca9555_read(uint8_t addr, uint8_t reg) {
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0)
      continue;
    if (Wire.requestFrom(addr, (uint8_t)1) == 1)
      return Wire.read();
  }
  return 0xFF;
}

void setOpto(bool state) {
  digitalWrite(OPTO_PIN, state ? HIGH : LOW);
  optoActive = state;
}

void readInputs() {
  // Read ADS1115 (all 4 channels)
  pot1 = ads.readADC_SingleEnded(0);
  pot2 = ads.readADC_SingleEnded(1);
  pot3 = ads.readADC_SingleEnded(2);
  pot4 = ads.readADC_SingleEnded(3);
  
  // Read main PCA9555 buttons (active low, invert)
  uint8_t port0 = pca9555_read(PCA9555_MAIN_ADDR, PCA9555_REG_INPUT0);
  uint8_t port1 = pca9555_read(PCA9555_MAIN_ADDR, PCA9555_REG_INPUT1);
  mainButtons = ~port0;  // Invert: 1 = pressed
  
  // Read extender PCA9555 buttons (active low, invert)
  if (pcaExtAddr != 0) {
    port0 = pca9555_read(pcaExtAddr, PCA9555_REG_INPUT0);
    port1 = pca9555_read(pcaExtAddr, PCA9555_REG_INPUT1);
    extButtons0 = ~port0;  // Invert: 1 = pressed
    extButtons1 = ~port1;  // Invert: 1 = pressed
  } else {
    extButtons0 = 0;
    extButtons1 = 0;
  }
  
  // Control optocoupler based on specific button: P0_6 on extender (bit 6 of extButtons0 = 0x40)
  bool optoButton = (extButtons0 & 0x40) != 0;
  setOpto(optoButton);
}

void sendPot(uint8_t pf, uint16_t value) {
  uint8_t data[2];
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  g_can->sendBroadcast(pf, data, 2, 6);
}

void sendButtons() {
  // Main PCA9555 buttons
  uint8_t data[1];
  data[0] = mainButtons;
  g_can->sendBroadcast(PF_JOYSTICK_BUTTONS, data, 1, 6);
  
  // Extender PCA9555 buttons (2 bytes)
  data[0] = extButtons0;
  g_can->sendBroadcast(PF_EXTENDER_BUTTONS, data, 1, 6);
  
  data[0] = extButtons1;
  g_can->sendBroadcast(PF_EXTENDER_BUTTONS2, data, 1, 6);
}

void sendHeartbeat() {
  uint8_t data[8];
  data[0] = g_can->isOnline() ? 0x01 : 0x00;
  data[1] = (uint8_t)(millis() / 1000);
  data[2] = (uint8_t)((millis() / 1000) >> 8);
  data[3] = JOYSTICK_ADDR;
  data[4] = (uint8_t)(g_can->getRxCount() & 0xFF);
  data[5] = (uint8_t)(g_can->getTxCount() & 0xFF);
  data[6] = optoActive ? 0x01 : 0x00;  // Opto status
  data[7] = HB_TYPE_JOYSTICK_UNIFIED;   // capability announcement
  g_can->sendBroadcast(PF_HEARTBEAT, data, 8, 6);
}

void processCAN() {
  CANMessage msg;
  while (g_can->receive(msg, 0)) {
    uint8_t pf = (msg.id >> 16) & 0xFF;
    
    // Control optocoupler via CAN (example: PF 0x40 with data[0] = 0/1)
    if (pf == 0x40 && msg.len >= 1) {
      setOpto(msg.data[0] != 0);
    }
  }
}

void i2c_scan() {
  Serial.println("--- I2C bus scan ---");
  uint8_t count = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Device at 0x%02X", addr);
      if (addr >= 0x48 && addr <= 0x4B) Serial.print(" (ADS1115)");
      else if (addr == PCA9555_MAIN_ADDR) Serial.print(" (PCA9555 main)");
      else if (addr >= 0x21 && addr <= 0x23) Serial.print(" (PCA9555 extender)");
      Serial.println();
      count++;
    }
  }
  if (count == 0) Serial.println("  No devices found!");
  Serial.printf("  Total: %d device(s)\n", count);
}

void setup() {
  Serial.begin(115200);
  unsigned long startWait = millis();
  while (!Serial && millis() - startWait < 3000) delay(10);
  
  Serial.println("\n=== Joystick Full ===");
  Serial.printf("CAN Address: 0x%02X\n", JOYSTICK_ADDR);
  
  // Initialize LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  digitalWrite(LED1_PIN, HIGH);  // LED on during init
  digitalWrite(LED2_PIN, LOW);
  
  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  
  // Scan the I2C bus and show all detected devices
  i2c_scan();
  
  // Initialize ADS1115
  if (!ads.begin(0x48)) {
    Serial.println("ADS1115 NOT found!");
    while (1) { delay(100); }
  }
  ads.setGain(GAIN_ONE);  // +/- 4.096V
  Serial.println("ADS1115 OK");
  
  // Initialize main PCA9555 @ 0x20
  pca9555_write(PCA9555_MAIN_ADDR, PCA9555_REG_CONFIG0, 0xFF);
  pca9555_write(PCA9555_MAIN_ADDR, PCA9555_REG_CONFIG1, 0xFF);
  pca9555_write(PCA9555_MAIN_ADDR, PCA9555_REG_OUTPUT0, 0xFF);  // Pull-ups
  pca9555_write(PCA9555_MAIN_ADDR, PCA9555_REG_OUTPUT1, 0xFF);
  Serial.println("Main PCA9555 @ 0x20 OK");
  
  // Initialize optocoupler GPIO
  pinMode(OPTO_PIN, OUTPUT);
  digitalWrite(OPTO_PIN, LOW);
  
  // Detect extender PCA9555 (0x21-0x23 depending on A0/A1/A2 strapping)
  for (uint8_t addr = 0x21; addr <= 0x23; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      pcaExtAddr = addr;
      pca9555_write(addr, PCA9555_REG_CONFIG0, 0xFF);  // Port 0 all inputs
      pca9555_write(addr, PCA9555_REG_CONFIG1, 0xFF);  // Port 1 all inputs
      pca9555_write(addr, PCA9555_REG_OUTPUT0, 0xFF);  // Pull-ups on port 0
      pca9555_write(addr, PCA9555_REG_OUTPUT1, 0xFF);  // Pull-ups on port 1
      Serial.printf("Extender PCA9555 OK @ 0x%02X\n", addr);
      break;
    }
  }
  if (pcaExtAddr == 0) {
    Serial.println("WARNING: no extender PCA9555 found at 0x21-0x23");
  }
  
  // Initialize CAN
  g_can = new ForwarderCAN(JOYSTICK_ADDR, ECU_NAME);
  if (!g_can->begin(CAN_TX_PIN, CAN_RX_PIN, 250000)) {
    Serial.println("CAN init FAILED!");
    while (1) { delay(100); }
  }
  Serial.printf("CAN Ready on 0x%02X\n", JOYSTICK_ADDR);
  
  digitalWrite(LED1_PIN, LOW);  // LED off = ready

  // Auto-reset watchdog: if the loop stops for 8s (e.g. I2C bus wedges)
  // the chip resets itself instead of sitting dead until a manual reset
  esp_task_wdt_init(8, true);
  esp_task_wdt_add(NULL);
}

void loop() {
  uint32_t now = millis();
  esp_task_wdt_reset();  // Feed the auto-reset watchdog
  
  g_can->loop();
  processCAN();
  
  // Read inputs and send data at 25Hz (40ms)
  if (now - lastSend >= 40) {
    lastSend = now;
    readInputs();
    sendPot(PF_JOYSTICK_POT1, pot1);
    sendPot(PF_JOYSTICK_POT2, pot2);
    sendPot(PF_JOYSTICK_POT3, pot3);
    sendPot(PF_JOYSTICK_POT4, pot4);
    sendButtons();
  }
  
  // Heartbeat at 1Hz
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    sendHeartbeat();
    Serial.printf("J: pot=%d,%d,%d,%d btn=%02X ext=%02X,%02X opto=%d\n", 
                  pot1, pot2, pot3, pot4, mainButtons, extButtons0, extButtons1, optoActive);
  }
  
  yield();
}
