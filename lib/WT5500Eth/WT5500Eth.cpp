#include "WT5500Eth.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/netif.h"

WT5500Eth WETH;

// SPI device handle
static spi_device_handle_t _spi_handle = nullptr;

static char _hostname_buf[32] = "esp32-eth";

void WT5500Eth::eth_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data) {
  WT5500Eth *self = (WT5500Eth *)arg;
  switch (event_id) {
  case ETHERNET_EVENT_CONNECTED:
    Serial.println("[WT5500] Ethernet link up");
    break;
  case ETHERNET_EVENT_DISCONNECTED:
    Serial.println("[WT5500] Ethernet link down");
    self->_connected = false;
    break;
  case ETHERNET_EVENT_START:
    Serial.println("[WT5500] Ethernet started");
    break;
  case ETHERNET_EVENT_STOP:
    Serial.println("[WT5500] Ethernet stopped");
    self->_connected = false;
    break;
  default:
    break;
  }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  esp_ip4_addr_t *ip = &event->ip_info.ip;
  Serial.printf("[WT5500] Got IP: %d.%d.%d.%d\n", ip->addr & 0xFF,
                (ip->addr >> 8) & 0xFF, (ip->addr >> 16) & 0xFF,
                (ip->addr >> 24) & 0xFF);
  WETH._connected = true;
}

bool WT5500Eth::begin(int miso, int mosi, int sck, int cs, int rst, int irq,
                      uint8_t phy_addr, int clk_mhz) {
  if (_started) {
    Serial.println("[WT5500] Already started");
    return true;
  }

  // Ensure netif stack is initialized (WiFi init normally does this,
  // but builds without WiFi never call it -> esp_netif_new fails)
  esp_netif_init();
  esp_event_loop_create_default();

  Serial.printf("[WT5500] Initializing SPI Ethernet: MISO=%d MOSI=%d SCLK=%d "
                "CS=%d RST=%d INT=%d\n",
                miso, mosi, sck, cs, rst, irq);

  // Configure SPI bus
  spi_bus_config_t buscfg = {};
  buscfg.miso_io_num = miso;
  buscfg.mosi_io_num = mosi;
  buscfg.sclk_io_num = sck;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 16384;

  // Try to add device first (bus may already be initialized by Arduino)
  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 16;
  devcfg.address_bits = 8;
  devcfg.mode = 0;
  devcfg.clock_speed_hz = clk_mhz * 1000 * 1000;
  devcfg.queue_size = 20;
  devcfg.spics_io_num = cs;

  esp_err_t ret = spi_bus_add_device(SPI2_HOST, &devcfg, &_spi_handle);
  if (ret != ESP_OK) {
    // Bus not initialized yet, try initializing it
    Serial.println("[WT5500] Bus not ready, initializing SPI bus...");
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
      Serial.printf("[WT5500] SPI bus init failed: %d\n", ret);
      return false;
    }
    Serial.println("[WT5500] SPI bus initialized");
    // Now try adding device again
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &_spi_handle);
    if (ret != ESP_OK) {
      Serial.printf("[WT5500] SPI add device failed: %d\n", ret);
      return false;
    }
  }
  Serial.println("[WT5500] SPI device attached");

  // Configure interrupt for INT pin
  esp_err_t isr_ret = gpio_install_isr_service(0);
  if (isr_ret != ESP_OK) {
    Serial.printf("[WT5500] ISR service already installed or failed: %d\n",
                  isr_ret);
  }
  gpio_config_t int_cfg = {};
  int_cfg.pin_bit_mask = 1ULL << irq;
  int_cfg.mode = GPIO_MODE_INPUT;
  int_cfg.intr_type = GPIO_INTR_NEGEDGE;
  int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&int_cfg);

  // Create W5500 MAC
  eth_w5500_config_t mac_config = ETH_W5500_DEFAULT_CONFIG(_spi_handle);
  mac_config.int_gpio_num = irq;

  eth_mac_config_t eth_mac_config = ETH_MAC_DEFAULT_CONFIG();
  Serial.println("[WT5500] Creating W5500 MAC...");
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&mac_config, &eth_mac_config);
  if (!mac) {
    Serial.println("[WT5500] Failed to create W5500 MAC");
    return false;
  }
  Serial.println("[WT5500] W5500 MAC created");

  // Create W5500 PHY
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.reset_gpio_num = rst;
  phy_config.reset_timeout_ms = 100;

  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (!phy) {
    Serial.println("[WT5500] Failed to create W5500 PHY");
    return false;
  }

  // Install Ethernet driver
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  ret = esp_eth_driver_install(&eth_config, &_eth_handle);
  if (ret != ESP_OK || !_eth_handle) {
    Serial.printf("[WT5500] Ethernet driver install failed: %d\n", ret);
    return false;
  }

  // Set default MAC address
  uint8_t eth_mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
  esp_eth_ioctl(_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac);

  // Create default Ethernet netif
  esp_netif_inherent_config_t esp_netif_config =
      ESP_NETIF_INHERENT_DEFAULT_ETH();
  esp_netif_config_t cfg = {};
  cfg.base = &esp_netif_config;
  cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

  _eth_netif = esp_netif_new(&cfg);
  if (!_eth_netif) {
    Serial.println("[WT5500] Failed to create netif");
    return false;
  }

  void *netif_glue = esp_eth_new_netif_glue(_eth_handle);
  if (!netif_glue) {
    Serial.println("[WT5500] Failed to create netif glue");
    return false;
  }
  esp_netif_attach(_eth_netif, netif_glue);

  // Register event handlers
  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler,
                             this);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                             &got_ip_event_handler, nullptr);

  // Start Ethernet
  esp_eth_start(_eth_handle);
  _started = true;

  Serial.println("[WT5500] Ethernet driver started");
  return true;
}

bool WT5500Eth::config(IPAddress local_ip, IPAddress gateway,
                       IPAddress subnet) {
  if (!_eth_netif) {
    Serial.println("[WT5500] Not initialized");
    return false;
  }

  esp_netif_ip_info_t ip_info;
  ip_info.ip.addr = (uint32_t)local_ip;
  ip_info.gw.addr = (uint32_t)gateway;
  ip_info.netmask.addr = (uint32_t)subnet;

  // Stop DHCP if running
  esp_netif_dhcpc_stop(_eth_netif);

  esp_err_t err = esp_netif_set_ip_info(_eth_netif, &ip_info);
  if (err != ESP_OK) {
    Serial.printf("[WT5500] Failed to set IP config: %d\n", err);
    return false;
  }

  Serial.printf("[WT5500] Static IP: %d.%d.%d.%d\n", local_ip[0], local_ip[1],
                local_ip[2], local_ip[3]);
  return true;
}

IPAddress WT5500Eth::localIP() {
  if (!_eth_netif)
    return IPAddress();
  esp_netif_ip_info_t ip;
  esp_netif_get_ip_info(_eth_netif, &ip);
  return IPAddress(ip.ip.addr);
}

IPAddress WT5500Eth::subnetMask() {
  if (!_eth_netif)
    return IPAddress();
  esp_netif_ip_info_t ip;
  esp_netif_get_ip_info(_eth_netif, &ip);
  return IPAddress(ip.netmask.addr);
}

IPAddress WT5500Eth::gatewayIP() {
  if (!_eth_netif)
    return IPAddress();
  esp_netif_ip_info_t ip;
  esp_netif_get_ip_info(_eth_netif, &ip);
  return IPAddress(ip.gw.addr);
}

String WT5500Eth::macAddress() {
  uint8_t mac[6];
  if (!_eth_handle)
    return "";
  esp_eth_ioctl(_eth_handle, ETH_CMD_G_MAC_ADDR, mac);
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

uint8_t WT5500Eth::linkSpeed() {
  if (!_eth_handle)
    return 0;
  uint8_t speed = 0;
  esp_eth_ioctl(_eth_handle, ETH_CMD_G_SPEED, &speed);
  return speed;
}

bool WT5500Eth::linkUp() { return _connected; }

const char *WT5500Eth::getHostname() { return _hostname_buf; }

bool WT5500Eth::setHostname(const char *hostname) {
  if (!_eth_netif)
    return false;
  strncpy(_hostname_buf, hostname, sizeof(_hostname_buf) - 1);
  return esp_netif_set_hostname(_eth_netif, hostname) == ESP_OK;
}
