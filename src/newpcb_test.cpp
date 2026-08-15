/*
 * New PCB Hardware Test Sketch
 * ESP32S3R8N8 CAN Board V1.0.0
 * 
 * Tests:
 * - I2C device scanner
 * - ADS1115 16-bit ADC readings (4 channels)
 * - PCA9555APW I/O expander (16 pins as input with pullup)
 * - LED control
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#ifndef I2C_SDA
#define I2C_SDA 39
#endif
#ifndef I2C_SCL
#define I2C_SCL 38
#endif
#ifndef LED1_PIN
#define LED1_PIN 48
#endif
#ifndef LED2_PIN
#define LED2_PIN 47
#endif

// ADS1115 instance (default address 0x48)
Adafruit_ADS1115 ads;

// PCA9555APW I2C addresses - treat 0x21, 0x22, 0x23 identically (A0/A1/A2 pin strapping)
#define PCA9555_ADDR_MAIN 0x20  // Main board buttons
#define PCA9555_ADDR_EXT  0x21  // Extended buttons (also 0x22, 0x23)

// PCA9555 register addresses
#define PCA9555_REG_INPUT0  0x00
#define PCA9555_REG_INPUT1  0x01
#define PCA9555_REG_OUTPUT0 0x02
#define PCA9555_REG_OUTPUT1 0x03
#define PCA9555_REG_POL0    0x04
#define PCA9555_REG_POL1    0x05
#define PCA9555_REG_CONFIG0 0x06
#define PCA9555_REG_CONFIG1 0x07

// Track which addresses are active
uint8_t pca9555_addrs[4] = {0};
uint8_t pca9555_count = 0;

void pca9555_write(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t pca9555_read(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

void pca9555_init() {
  Serial.println("\n--- PCA9555APW Initialization ---");
  pca9555_count = 0;
  
  // Scan for PCA9555 at addresses 0x20-0x23
  for (uint8_t addr = 0x20; addr <= 0x23; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("PCA9555 found at 0x%02X\n", addr);
      pca9555_addrs[pca9555_count++] = addr;
      
      // Configure all pins as inputs
      pca9555_write(addr, PCA9555_REG_CONFIG0, 0xFF);
      pca9555_write(addr, PCA9555_REG_CONFIG1, 0xFF);
      
      // Enable pull-ups
      pca9555_write(addr, PCA9555_REG_OUTPUT0, 0xFF);
      pca9555_write(addr, PCA9555_REG_OUTPUT1, 0xFF);
      
      // Verify
      uint8_t cfg0 = pca9555_read(addr, PCA9555_REG_CONFIG0);
      uint8_t cfg1 = pca9555_read(addr, PCA9555_REG_CONFIG1);
      Serial.printf("  Config: Port0=0x%02X Port1=0x%02X (all input+pullup)\n", cfg0, cfg1);
    }
  }
  
  if (pca9555_count == 0) {
    Serial.println("No PCA9555 found!");
  } else {
    Serial.printf("Total: %d PCA9555(s) found\n", pca9555_count);
  }
}

void pca9555_read_all() {
  for (uint8_t i = 0; i < pca9555_count; i++) {
    uint8_t addr = pca9555_addrs[i];
    uint8_t port0 = pca9555_read(addr, PCA9555_REG_INPUT0);
    uint8_t port1 = pca9555_read(addr, PCA9555_REG_INPUT1);
    
    Serial.printf("0x%02X[", addr);
    for (int j = 0; j < 8; j++) Serial.print((port0 >> j) & 0x01);
    Serial.print("|");
    for (int j = 0; j < 8; j++) Serial.print((port1 >> j) & 0x01);
    Serial.print("] ");
    
    // For 0x21 extender: show button pairs
    if (addr == 0x21) {
      // Button pairs (active low - 0 means pressed):
      // Pair 1: P0_7 + P0_4 (optocoupler on P0_7)
      // Pair 2: P0_3 + P0_5
      // Pair 3: P0_1 + P0_2
      // Pair 4: P1_0 + P1_1
      // Pair 5: P1_2 + P1_3
      // Pair 6: P1_4 + P1_5
      bool p1a = !((port0 >> 7) & 0x01);  // P0_7 (optocoupler)
      bool p1b = !((port0 >> 4) & 0x01);  // P0_4
      bool p2a = !((port0 >> 3) & 0x01);  // P0_3
      bool p2b = !((port0 >> 5) & 0x01);  // P0_5
      bool p3a = !((port0 >> 1) & 0x01);  // P0_1
      bool p3b = !((port0 >> 2) & 0x01);  // P0_2
      bool p4a = !((port1 >> 0) & 0x01);  // P1_0
      bool p4b = !((port1 >> 1) & 0x01);  // P1_1
      bool p5a = !((port1 >> 2) & 0x01);  // P1_2
      bool p5b = !((port1 >> 3) & 0x01);  // P1_3
      bool p6a = !((port1 >> 4) & 0x01);  // P1_4
      bool p6b = !((port1 >> 5) & 0x01);  // P1_5
      
      Serial.printf("Pairs: [%d%d][%d%d][%d%d][%d%d][%d%d][%d%d]",
                    p1a, p1b, p2a, p2b, p3a, p3b, p4a, p4b, p5a, p5b, p6a, p6b);
    }
  }
}

void i2c_scan() {
  Serial.println("\n--- I2C Scanner ---");
  uint8_t count = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X", addr);
      if (addr == 0x48 || addr == 0x49 || addr == 0x4A || addr == 0x4B) {
        Serial.print(" (ADS1115)");
      } else if (addr >= 0x20 && addr <= 0x27) {
        Serial.print(" (PCA9555?)");
      } else if (addr >= 0x40 && addr <= 0x41) {
        Serial.print(" (PCA9685?)");
      }
      Serial.println();
      count++;
    }
  }
  if (count == 0) {
    Serial.println("  No devices found!");
  } else {
    Serial.printf("  Total: %d devices\n", count);
  }
}

void ads1115_init() {
  Serial.println("\n--- ADS1115 Initialization ---");
  if (ads.begin(0x48)) {
    Serial.println("ADS1115 found at 0x48");
    ads.setGain(GAIN_ONE);  // +/- 4.096V
    Serial.println("Gain set to +/- 4.096V");
  } else {
    Serial.println("ADS1115 NOT found at 0x48 - trying other addresses...");
    for (uint8_t addr : {0x48, 0x49, 0x4A, 0x4B}) {
      if (ads.begin(addr)) {
        Serial.printf("ADS1115 found at 0x%02X\n", addr);
        ads.setGain(GAIN_ONE);
        break;
      }
    }
  }
}

void ads1115_read() {
  int16_t x = ads.readADC_SingleEnded(0);    // X axis
  int16_t y = ads.readADC_SingleEnded(1);    // Y axis
  // ch2 unused
  int16_t z = ads.readADC_SingleEnded(3);    // 3rd axis pot
  Serial.printf("X=%d Y=%d Z=%d", x, y, z);
}

void setup() {
  Serial.begin(115200);
  
  // Wait for USB-CDC
  unsigned long startWait = millis();
  while (!Serial && millis() - startWait < 3000) {
    delay(10);
  }
  
  Serial.println("\n============================================");
  Serial.println("  New PCB Hardware Test");
  Serial.println("  ESP32S3R8N8 CAN Board V1.0.0");
  Serial.println("============================================");
  Serial.printf("I2C: SDA=GPIO%d SCL=GPIO%d\n", I2C_SDA, I2C_SCL);
  Serial.printf("LEDs: LED1=GPIO%d LED2=GPIO%d\n", LED1_PIN, LED2_PIN);
  
  // Initialize LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  
  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);  // 400kHz fast mode
  
  // Scan I2C bus
  i2c_scan();
  
  // Initialize ADS1115
  ads1115_init();
  
  // Initialize PCA9555
  pca9555_init();
  
  // LED test
  Serial.println("\n--- LED Test ---");
  digitalWrite(LED1_PIN, HIGH);
  delay(200);
  digitalWrite(LED2_PIN, HIGH);
  delay(200);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  Serial.println("LED test complete");
  
  Serial.println("\n--- Starting main loop ---");
}

void loop() {
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  
  if (now - lastPrint >= 500) {  // Print every 500ms
    lastPrint = now;
    
    Serial.print("T=");
    Serial.print(now / 1000);
    Serial.print("s ");
    
    // Read ADS1115
    ads1115_read();
    Serial.print(" ");
    
    // Read PCA9555 buttons
    pca9555_read_all();
    Serial.println();
  }
  
  yield();
}
