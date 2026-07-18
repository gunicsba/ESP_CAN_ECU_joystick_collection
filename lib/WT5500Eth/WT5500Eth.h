#pragma once

#include "WiFi.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_system.h"

class WT5500Eth {
public:
  bool begin(int miso, int mosi, int sck, int cs, int rst, int irq,
             uint8_t phy_addr = 1, int clk_mhz = 36);
  bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet);

  IPAddress localIP();
  IPAddress subnetMask();
  IPAddress gatewayIP();
  String macAddress();
  uint8_t linkSpeed();
  bool linkUp();

  const char *getHostname();
  bool setHostname(const char *hostname);

  bool isConnected() const { return _connected; }

  // Public so event handlers can update it
  bool _connected = false;

  // Expose netif for WebServer binding if needed
  esp_netif_t *getNetif() { return _eth_netif; }

private:
  esp_eth_handle_t _eth_handle = nullptr;
  esp_netif_t *_eth_netif = nullptr;
  bool _started = false;

  static void eth_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);
};

extern WT5500Eth WETH;
