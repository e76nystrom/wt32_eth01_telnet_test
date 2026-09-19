/*
 * WT32-ETH01 Ethernet -> Telnet client test
 *
 * Brings up the on-board LAN8720 PHY over RMII, waits for an IP address,
 * then opens a plain TCP connection to a telnet server, sends a test
 * string, and logs whatever the server sends back.
 *
 * "Telnet" here just means a TCP connection on port 23 with no special
 * negotiation -- enough to test connectivity against most telnet servers
 * or a netcat listener (`nc -lk 23`) for testing.
 */

#include <cstring>
#include <lwip/dns.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "driver/uart.h"
#include "esp_timer.h"
//#include "dhcpserver/dhcpserver_options.h"
#include "esp32/rom/gpio.h"

#define U8X8

//#define SERVER
//#define CLIENT

#if defined(SERVER)
#pragma message("building SERVER")
#endif	/* SERVER */

#if defined(CLIENT)
#pragma message("building CLIENT")
#endif	/* CLIENT */

#define RTK_SEND

#define UART_NUM UART_NUM_1
#define TX_PIN 17
#define RX_PIN 5

#define TX_PIN_2 14
#define RX_PIN_2 12

// #define DBG0_PIN 2
// #define DBG1_PIN 4

#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 1024

void test();

// #include "dbgPin.h"
// 
// inline void dbg0Set()
// {
//     REG_WRITE(GPIO_OUT_W1TS_REG, (1 << DBG0_PIN));
// }
//
// inline void dbg0Clr()
// {
//  REG_WRITE(GPIO_OUT_W1TC_REG, (1 << DBG0_PIN));
// }
//
// inline void dbg1Set()
// {
//  REG_WRITE(GPIO_OUT_W1TS_REG, (1 << DBG1_PIN));
// }
//
// inline void dbg1Clr()
// {
//  REG_WRITE(GPIO_OUT_W1TC_REG, (1 << DBG1_PIN));
// }

#define GPS_LIB

#if defined(GPS_LIB)
#include "gpsLib.h"
#endif  /* GPS_LIB */

#if defined(U8X8)

#include "oledLib.h"
#include "driver/i2c_master.h"
#include "u8x8.h"

static i2c_master_dev_handle_t dev_handle;

extern "C" uint8_t u8x8_byte_esp_idf_i2c(u8x8_t* u8x8X, uint8_t msg,
                                         uint8_t arg_int, void* arg_ptr)
{
 static uint8_t buffer[32];
 static uint8_t buf_idx;

 switch (msg)
 {
 case U8X8_MSG_BYTE_SEND:
  {
   auto data = static_cast<uint8_t*>(arg_ptr);
   while (arg_int > 0)
   {
    buffer[buf_idx++] = *data++;
    arg_int--;
   }
   break;
  }
 case U8X8_MSG_BYTE_START_TRANSFER:
  buf_idx = 0;
  break;
 case U8X8_MSG_BYTE_END_TRANSFER:
  i2c_master_transmit(dev_handle, buffer, buf_idx, -1);
  break;
 case U8X8_MSG_BYTE_INIT:
 case U8X8_MSG_BYTE_SET_DC:
  break;
 default:
  return 0;
 }
 return 1;
}

extern "C" uint8_t u8x8_gpio_and_delay_esp_idf(u8x8_t* u8x8X, uint8_t msg,
                                               uint8_t arg_int, void* arg_ptr)
{
 switch (msg)
 {
 case U8X8_MSG_GPIO_AND_DELAY_INIT:
  break; // I2C bus/device already set up in app_main()
 case U8X8_MSG_DELAY_MILLI:
  vTaskDelay(pdMS_TO_TICKS(arg_int));
  break;
 case U8X8_MSG_DELAY_10MICRO:
  esp_rom_delay_us(arg_int * 10);
  break;
 case U8X8_MSG_DELAY_100NANO:
  esp_rom_delay_us(1);
  break;
 case U8X8_MSG_GPIO_I2C_CLOCK:
 case U8X8_MSG_GPIO_I2C_DATA:
  break; // hardware I2C peripheral drives these lines
 default:
  return 0;
 }
 return 1;
}

#include "esp_rom_sys.h" // for esp_rom_delay_us

#define I2C_PORT     I2C_NUM_0
#define I2C_SDA_PIN  15
#define I2C_SCL_PIN  14
#define SH1106_ADDR  0x3C

void u8x8Init()
{
 i2c_master_bus_config_t bus_config = {};
 bus_config.i2c_port = I2C_PORT;
 bus_config.sda_io_num = static_cast<gpio_num_t>(I2C_SDA_PIN);
 bus_config.scl_io_num = static_cast<gpio_num_t>(I2C_SCL_PIN);
 bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
 bus_config.glitch_ignore_cnt = 7;
 bus_config.flags.enable_internal_pullup = true;

 i2c_master_bus_handle_t bus_handle;
 i2c_new_master_bus(&bus_config, &bus_handle);

 i2c_device_config_t dev_config = {};
 dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
 dev_config.device_address = SH1106_ADDR;
 dev_config.scl_speed_hz = 400000;

 i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);

 #if defined(U8G2)

 u8g2_t u8g2;
 u8g2_Setup_sh1106_i2c_128x64_noname_f(
     &u8g2,
     U8G2_R0,                      // rotation: no rotation
     u8x8_byte_esp_idf_i2c,        // same byte callback as u8x8
     u8x8_gpio_and_delay_esp_idf); // same gpio/delay callback as u8x8

 u8g2_InitDisplay(&u8g2);   // sends init sequence, display still off
 u8g2_SetPowerSave(&u8g2, 0); // wake display up
 u8g2_ClearBuffer(&u8g2);   // clear the RAM-side framebuffer

 u8g2_ClearBuffer(&u8g2);
 u8g2_SetFont(&u8g2, u8g2_font_4x6_tr);
 u8g2_DrawStr(&u8g2, 0, 10, "Hello ESP32!");
 u8g2_SendBuffer(&u8g2); // pushes the framebuffer to the display over I2C

#else

 //u8x8_t u8x8;
u8x8_Setup(&u8x8, u8x8_d_sh1106_128x64_noname,
            u8x8_cad_ssd13xx_i2c,
            u8x8_byte_esp_idf_i2c,
            u8x8_gpio_and_delay_esp_idf);

 u8x8_InitDisplay(&u8x8);
 u8x8_SetPowerSave(&u8x8, 0);
 u8x8_ClearDisplay(&u8x8);

 u8x8_SetFont(&u8x8, u8x8_font_chroma48medium8_r);
 u8x8_DrawString(&u8x8, 0, 6, "Hello ");
 u8x8_DrawString(&u8x8, 0, 7, "0123456789012345");

#endif
}

#endif	/* U8X8 */

void uart1_init()
{
 uart_config_t uart_config;
 uart_config.baud_rate = 115200;
 uart_config.data_bits = UART_DATA_8_BITS;
 uart_config.parity = UART_PARITY_DISABLE;
 uart_config.stop_bits = UART_STOP_BITS_1;
 uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
 uart_config.rx_flow_ctrl_thresh = 0;
 uart_config.source_clk = UART_SCLK_APB;
 uart_config.flags.allow_pd = 0;
 uart_config.flags.backup_before_sleep = 0;

 // Apply configuration
 uart_param_config(UART_NUM, &uart_config);

 // Set pins (TX, RX, RTS, CTS)
 uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

 // Install driver with buffer sizes
 uart_driver_install(UART_NUM, RX_BUF_SIZE, TX_BUF_SIZE, 0, nullptr, 0);
}

void uart2_init()
{
 uart_config_t uart_config;
 uart_config.baud_rate = 115200;
 uart_config.data_bits = UART_DATA_8_BITS;
 uart_config.parity = UART_PARITY_DISABLE;
 uart_config.stop_bits = UART_STOP_BITS_1;
 uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
 uart_config.rx_flow_ctrl_thresh = 0;
 uart_config.source_clk = UART_SCLK_APB;
 uart_config.flags.allow_pd = 0;
 uart_config.flags.backup_before_sleep = 0;

 // Apply configuration
 uart_param_config(UART_NUM_2, &uart_config);

 // Set pins (TX, RX, RTS, CTS)
 uart_set_pin(UART_NUM_2, TX_PIN_2, RX_PIN_2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

 // Install driver with buffer sizes
 uart_driver_install(UART_NUM_2, RX_BUF_SIZE, TX_BUF_SIZE, 0, nullptr, 0);
}

static const char *TAG = "wt32_eth01_tcp";

#define SERVER_HOSTNAME  "Server2"
#define TCP_SERVER_PORT  8088

/* WT32-ETH01 pin mapping (LAN8720 PHY, RMII) */
#define ETH_PHY_ADDR        1
#define ETH_PHY_RST_GPIO    (-1) /* not wired to a GPIO on this module */
#define ETH_PHY_POWER_GPIO  16   /* drives the PHY's power/enable line */
#define ETH_MDC_GPIO        23
#define ETH_MDIO_GPIO       18
#define ETH_RMII_CLK_GPIO   0    /* external 50MHz oscillator feeds in here */

static EventGroupHandle_t eth_event_group;
#define ETH_CONNECTED_BIT BIT0

#if defined(SERVER)
const char *hostname = "Server2";
#endif

#if defined(CLIENT)
const char *hostname = "Client1";
#endif

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *static_cast<esp_eth_handle_t*>(event_data);

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet link up, HW addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet driver started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet driver stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    auto *event = static_cast<ip_event_got_ip_t*>(event_data);
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip_info->ip));

    esp_netif_t *netif = event->esp_netif;

    esp_netif_dns_info_t dns_info;
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    ESP_LOGI(TAG, "DNS Server: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));

    xEventGroupSetBits(eth_event_group, ETH_CONNECTED_BIT);
}

static void tx_rx_event_handler(void *arg, esp_event_base_t event_base,
				int32_t event_id, void *event_data)
{
    if (auto event = static_cast<ip_event_tx_rx_t*>(event_data);
	event->dir == ESP_NETIF_RX)
    {
        // Process incoming data from event->len or associated buffers
        ESP_LOGI("TAG", "Got RX event: Interface \"%s\" data len: %d",
                 esp_netif_get_desc(event->esp_netif), event->len);
    }
}

esp_netif_t *eth_netif;

static esp_eth_handle_t eth_init()
{
 /* WT32-ETH01 specific: GPIO16 must be driven high to power the PHY */

 gpio_set_direction(static_cast<gpio_num_t>(ETH_PHY_POWER_GPIO), GPIO_MODE_OUTPUT);
 gpio_set_level(static_cast<gpio_num_t>(ETH_PHY_POWER_GPIO), 1);
 vTaskDelay(pdMS_TO_TICKS(100)); /* let the PHY power up */

 esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
 eth_netif = esp_netif_new(&netif_cfg);

 if (esp_err_t err = esp_netif_set_hostname(eth_netif, hostname);
     err != ESP_OK)
 {
  ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
 }
 else
 {
  ESP_LOGI(TAG, "Hostname set to: %s", hostname);
 }

 eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

 /* Board has an external oscillator driving the RMII ref clock into
  * this GPIO, rather than the ESP32 generating the clock itself. */

 esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
 esp32_emac_config.clock_config.rmii.clock_gpio = ETH_RMII_CLK_GPIO;

 eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
 esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

 eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
 phy_config.phy_addr = ETH_PHY_ADDR;
 phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;

 esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);

 esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
 esp_eth_handle_t eth_handle = nullptr;
 ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

 ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

 ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
					    &eth_event_handler, nullptr));

 ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
					    &got_ip_event_handler, nullptr));

 ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_TX_RX,
					    &tx_rx_event_handler, nullptr));

 ESP_ERROR_CHECK(esp_eth_start(eth_handle));
 return eth_handle;
}

#if !defined(GPS_LIB)
//
// /* ── CRC-24Q constants ───────────────────────────────────────────────────── */
//
// #define CRC24Q_POLY      0x1864CFBu  /* Generator polynomial                 */
// #define RTCM3_PREAMBLE   0xD3u       /* Mandatory first byte of every frame  */
// #define RTCM3_HDR_LEN    3           /* Preamble + 2 length/reserved bytes   */
// #define RTCM3_CRC_LEN    3           /* 24-bit CRC appended at end           */
// #define RTCM3_MIN_FRAME  (RTCM3_HDR_LEN + RTCM3_CRC_LEN)
//
// /* ── CRC-24Q lookup table (generated once on first use) ─────────────────── */
//
// static uint32_t crc24qTable[256];
//
// static void buildCRC24qTable()
// {
//  for (uint32_t i = 0; i < 256; i++)
//  {
//   uint32_t crc = i << 16;
//   for (int j = 0; j < 8; j++)
//   {
//    crc <<= 1;
//    if (crc & 0x1000000u)
//     crc ^= CRC24Q_POLY;
//   }
//   crc24qTable[i] = crc & 0xFFFFFFu;
//  }
// }
//
// uint32_t crc24(const uint32_t crc, const unsigned char c)
// {
//  return ((crc << 8) ^ crc24qTable[((crc >> 16) ^ c) & 0xFFu]) & 0xFFFFFFu;
// }
//
// enum RCV_STATE {RCV_IDLE, RCV_GET_LEN, RCV_GET_DATA, RCV_TEXT};
//
// constexpr size_t RTK_BUF_SIZE = 1024;
// uint32_t crcBuf[1024];
//
// typedef struct S_RTK_DATA
// {
//  enum RCV_STATE state;
//  uint64_t t0;
//  uint64_t startTime;
//  uint32_t crc;
//  int count;
//  int len;
//  int fil;
//  unsigned char buf[RTK_BUF_SIZE];
//  unsigned int t0Accum;
//  int rxAccum;
//  //int rxCount;
// } T_RTK_DATA, *P_RTK_DATA;
//
// T_RTK_DATA rtk;

#endif  /* GPS_LIB */

#if defined(SERVER)

#if 0
typedef void (*data_rx_callback_t)(int sock, const uint8_t *data, size_t len);

static void default_data_rx_callback(int sock, const uint8_t *data, size_t len)
{
    /* Treat as text for logging purposes; cap to avoid overrunning a
     * stack buffer if a future caller passes something larger. */
    char text[257];
    size_t copy_len = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
    memcpy(text, data, copy_len);
    text[copy_len] = 0;

    ESP_LOGI(TAG, "[server] rx %d bytes from sock %d: %s", (int)len, sock, text);

    /* Echo back so a telnet client connecting to us sees its own input */
    //send(sock, data, len, 0);
}
static data_rx_callback_t s_data_rx_callback = nullptr;

#endif

#if !defined(GPS_LIB)
//
// static void processData(const unsigned char *ptr, size_t len)
// {
//     while (len > 0)
//     {
//         len -= 1;
//         const unsigned char ch = *ptr;
//         switch (rtk.state)
//         {
//         case RCV_IDLE:
//             if (ch == 0xd3)
//             {
//                 rtk.count = 2;
//                 uart_write_bytes(UART_NUM_1, ptr, 1);
//                 rtk.buf[0] = ch;
//                 rtk.crc = crc24qTable[static_cast<int>(ch)];
//                 crcBuf[0] = rtk.crc;
//                 rtk.fil = 1;
//                 rtk.t0 = esp_timer_get_time();
//                 rtk.state = RCV_GET_LEN;
//             }
//             else //if (ch == '$')
//             {
//                 rtk.t0 = esp_timer_get_time();
//                 rtk.state = RCV_TEXT;
//                 uart_write_bytes(UART_NUM_1, ptr, 1);
// #if 0
//                 Serial.print(ch);
//                 Serial.flush();
// #endif
//             }
//             break;
//
//         case RCV_GET_LEN:
//             uart_write_bytes(UART_NUM_1, ptr, 1);
//             rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ ch) & 0xFFu]) & 0xFFFFFFu;
//             crcBuf[rtk.fil] = rtk.crc;
//             rtk.len = (rtk.len << 8) + ch;
//             rtk.buf[rtk.fil] = ch;
//             rtk.fil += 1;
//             rtk.count -= 1;
//             if (rtk.count == 0)
//             {
//                 rtk.state = RCV_GET_DATA;
//                 rtk.len &= 0x3ff;
//                 // printf("dLen %d\n", dLen);
// #if defined(DBG_PRT)
//                 //if (rtk.len == 19)
//                 if ((prt == 0) && (rtk.len == 19))
//                 {
//                     prt = 1;
//                 }
// #endif	/* DBG_PRT */
//                 rtk.len += 3;
//             }
//             break;
//
//         case RCV_GET_DATA:
//             uart_write_bytes(UART_NUM_1, ptr, 1);
//             rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ ch) & 0xFFu]) & 0xFFFFFFu;
//             crcBuf[rtk.fil] = rtk.crc;
//             rtk.buf[rtk.fil] = ch;
//             rtk.fil += 1;
//             rtk.len -= 1;
//             if (rtk.len == 0)
//             {
// #if 0
//                 const int type = (rtk.buf[3] << 4) | (rtk.buf[4] >> 4);
//                 printf("len %4d type %4d CRC %08x\n", rtk.fil, type, rtk.crc);
// #endif
// #if defined(DBG_PRT)
//                 if (prt == 1)
//                 {
//                     printHex(reinterpret_cast<const uint8_t *>(rtk.buf), rtk.fil);
//                     printHex(reinterpret_cast<const uint8_t *>(crcBuf), rtk.fil << 2);
//                     prt = 0;
//                 }
// #endif	/* DBG_PRT */
//                 rtk.state = RCV_IDLE;
//             }
//             break;
//
//         case RCV_TEXT:
//             uart_write_bytes(UART_NUM_1, ptr, 1);;
// #if 0
//             Serial.print(ch);
// #endif
//             if (ch == '\n')
//             {
//                 rtk.state = RCV_IDLE;
//             }
//             break;
//         }
//         ptr += 1;
//     }
// }

#endif  /* GPS_LIB */

static void tcp_server_client_handler(void* pvParameters)
{
 int sock = (int)reinterpret_cast<intptr_t>(pvParameters);
 uint8_t rx_buffer[1600];
 printf("task start server client handler %d\n", sock);

 while (true)
 {
#if defined(U8X8)
  static int64_t tmr0;
  if (auto t0 = esp_timer_get_time(); (t0 - tmr0) > 1000 * 1000)
  {
   tmr0 = t0;

   uint8_t temp = temprature_sens_read();

   char buf[20];
   snprintf(buf, sizeof(buf), "    %3d %4d ", temp, rtk.rxCount);
   drawString(0, 1, buf);
  }
#endif	/* USE_U8X8 */

#if defined(GPS_LIB)

  pollSerial()

#endif	/* GPS_LIB */

  // 1. Check UART
  size_t uart_len = 0;
  uart_get_buffered_data_len(UART_NUM_1, &uart_len);
  if (uart_len > 0)
  {
   uart_read_bytes(UART_NUM_1, rx_buffer, uart_len, 0);
#if 1

   processSerial(sock, reinterpret_cast<char*>(rx_buffer), uart_len);

#if defined(U8X8)

   if (gpsInfo.update)
   {
    gpsInfo.update = false;
    char buf[20];
    drawString(0, 2, gpsInfo.timeBuf);
    snprintf(buf, sizeof(buf), "%d %2d   ", gpsInfo.fix, gpsInfo.sats);
    drawString(9, 2, buf);  // 9 10 11 12 13 14 15

    snprintf(buf, sizeof(buf), " %13.10f", gpsInfo.lat);
    drawString(0, 3, buf);
    snprintf(buf, sizeof(buf), "%14.10f", gpsInfo.lon);
    drawString(0, 4, buf);
   }

#endif	/* USE_U8X8 */

#else
   const int err = send(sock, rx_buffer, uart_len, MSG_DONTWAIT);
   if (err < 0)
    printf("send failed: errno %d\n", errno);
#endif
  }

  // 2. Check Socket
  if (const int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT);
      len > 0)
  {
   processRemData(rx_buffer, len);
   //uart_write_bytes(UART_NUM_1, rx_buffer, len);
  }
  else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
  {
   // Handle socket error or disconnect
   break;
  }

  // 3. Yield CPU if no data was processed
  vTaskDelay(pdMS_TO_TICKS(10));
 }

 printf("loop done\n");

 shutdown(sock, 0);
 close(sock);
 vTaskDelete(nullptr);
 printf("task done\n");
}

extern "C" { uint8_t temprature_sens_read(void); }

static void tcp_server_task(void* pvParameters)
{
 char addr_str[128];
 int addr_family = reinterpret_cast<int>(pvParameters);
 int ip_protocol = 0;
 struct sockaddr_storage dest_addr = {};

 printf("tcp server task addr_family %d\n", addr_family);

 if (addr_family == AF_INET)
 {
  auto* dest_addr_ip4 = reinterpret_cast<struct sockaddr_in*>(&dest_addr);
  dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr_ip4->sin_family = AF_INET;
  dest_addr_ip4->sin_port = htons(TCP_SERVER_PORT);
  ip_protocol = IPPROTO_IP;
 }

 int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
 if (listen_sock < 0)
 {
  ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
  vTaskDelete(nullptr);
  return;
 }

 int opt = 1;
 setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

 ESP_LOGI(TAG, "Socket created");
 char ip_str[INET_ADDRSTRLEN];

 auto ipv4 = reinterpret_cast<struct sockaddr_in*>(&dest_addr);

 // Print IP address
 inet_ntop(AF_INET, &ipv4->sin_addr, ip_str, INET_ADDRSTRLEN);
 uint16_t port = ntohs(ipv4->sin_port);

 printf("IPv4 Address: %s, Port: %d\n", ip_str, port);

 int err = bind(listen_sock, reinterpret_cast<struct sockaddr*>(&dest_addr),
                sizeof(dest_addr));
 if (err != 0)
 {
  ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
  close(listen_sock);
  vTaskDelete(nullptr);
  return;
 }
 ESP_LOGI(TAG, "Socket bound, port %d", TCP_SERVER_PORT);

 err = listen(listen_sock, 1);
 if (err != 0)
 {
  ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
  close(listen_sock);
  vTaskDelete(nullptr);
  return;
 }

 // ReSharper disable once CppDFAEndlessLoop
 while (true)
 {
#if defined(U8X8)
  static int64_t tmr0;
  if (auto t0 = esp_timer_get_time(); (t0 - tmr0) > 1000 * 1000)
  {
   tmr0 = t0;

   uint8_t temp = temprature_sens_read();

   char buf[20];
   snprintf(buf, sizeof(buf), "    %3d %4d ", temp, rtk.rxCount);
   drawString(0, 1, buf);
  }
#endif	/* USE_U8X8 */

  struct sockaddr_in source_addr = {};
  socklen_t addr_len = sizeof(source_addr);
  int client_sock = accept(listen_sock, reinterpret_cast<struct sockaddr*>(&source_addr),
                           &addr_len);
  if (client_sock < 0)
  {
   ESP_LOGE(TAG, "[server] accept failed: errno %d", errno);
   continue;
  }

  int client_flags = fcntl(client_sock, F_GETFL, 0);
  fcntl(client_sock, F_SETFL, client_flags | O_NONBLOCK);

  inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
  ESP_LOGI(TAG, "[server] accepted connection from %s, sock %d", addr_str, client_sock);

  /* One task per client so multiple connections can be handled
   * concurrently without blocking each other on recv(). */
  char task_name[32];
  snprintf(task_name, sizeof(task_name), "tcp_cli_%d", client_sock);
  printf("client_sock %s\n", task_name);
  xTaskCreate(tcp_server_client_handler, task_name, 4096,
              reinterpret_cast<void*>((intptr_t)client_sock), 5, nullptr);
 }
}

#endif	/* SERVER */

#if defined(CLIENT)

#if !defined(GPS_LIB)
//
// char* nextArg(char* p0)
// {
//  while (true)
//  {
//   const char c0 = *p0;
//   if (c0 == 0)
//    break;
//   p0 += 1;
//   if (c0 == ',')
//   {
//    break;
//   }
//  }
//  return p0;
// }
//
// int getNum(char **p0, int n)
// {
//  char *p1 = *p0;
//  int val = 0;
//  while (n > 0)
//  {
//   const char c1 = *p1++;
//   val *= 10;
//   val += c1 - '0';
//   n -= 1;
//  }
//  *p0 = p1;
//  return val;
// }
//
// void processSerial(const int sock, char* buf, size_t len)
// {
//     while (len > 0)
//     {
//         len -= 1;
//         dbg1Set();
//         const unsigned char c = *buf++;
//         switch (rtk.state)
//         {
//         case RCV_IDLE:
//             if (c == 0xd3)
//             {
//                 dbg0Set();
//                 rtk.count = 2;
//                 rtk.buf[0] = c;
//                 rtk.crc = crc24qTable[c];
//                 crcBuf[0] = rtk.crc;
//                 rtk.fil = 1;
//                 rtk.t0 = esp_timer_get_time();
//                 rtk.state = RCV_GET_LEN;
//                 rtk.startTime = esp_timer_get_time();
//             }
//             else // if (c == '$')
//             {
//                 rtk.t0 = esp_timer_get_time();
//                 rtk.state = RCV_TEXT;
//                 putchar(c);
//                 rtk.buf[0] = c;
//                 rtk.fil = 1;
//             }
//             break;
//
//         case RCV_GET_LEN:
//             rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ c) & 0xFFu]) & 0xFFFFFFu;
//             crcBuf[rtk.fil] = rtk.crc;
//             rtk.len = (rtk.len << 8) + c;
//             rtk.buf[rtk.fil] = c;
//             rtk.fil += 1;
//             rtk.count -= 1;
//             if (rtk.count == 0)
//             {
//                 rtk.state = RCV_GET_DATA;
//                 rtk.len &= 0x3ff;
//                 // printf("rtkLen %d\n", rtk.len);
// #if 0 && defined(DBG_PRT)
//                 // if ((prt == 0) && (rtk.len == 19))
//                 if (rtk.len == 19)
//                 {
//                     prt = 1;
//                 }
// #endif	/* DBG_PRT */
//                 rtk.len += 3;
//             }
//             break;
//
//         case RCV_GET_DATA:
//             rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ c) & 0xFFu]) & 0xFFFFFFu;
//             crcBuf[rtk.fil] = rtk.crc;
//             rtk.buf[rtk.fil] = c;
//             rtk.fil += 1;
//             rtk.len -= 1;
//             if (rtk.len == 0)
//             {
//                 rtk.rxAccum += rtk.fil;
// #if 1
//                 const auto msgT = (esp_timer_get_time() - rtk.startTime);
//                 int type = (rtk.buf[3] << 4) | (rtk.buf[4] >> 4);
//                 printf("rtkLen %4d type %4d rtkCRC %08x %5d %llu\n",
//                        rtk.fil, type, (unsigned int) rtk.crc, rtk.rxAccum, msgT);
// #endif
//                 rtk.t0Accum = esp_timer_get_time();
// #if defined(RTK_SEND)
//                 const int err = send(sock, rtk.buf, rtk.fil, MSG_DONTWAIT);
//                 if (err < 0)
//                     printf("send failed: errno %d\n", errno);
//
// #endif	/* RTK_SEND */
//
// #if 0 && defined(DBG_PRT)
//                 if (prt == 1)
//                 {
//                     printHex(reinterpret_cast<const u_int8_t *>(rtk.buf), rtk.fil);
//                     printHex(reinterpret_cast<const u_int8_t *>(crcBuf), rtk.fil << 2);
//                     prt = 0;
//                 }
// #endif	/* DDBG_PRT */
//                 dbg0Clr();
//                 rtk.state = RCV_IDLE;
//             }
//             break;
//
//         case RCV_TEXT:
//             putchar(c);
//             rtk.buf[rtk.fil] = c;
//             rtk.fil += 1;
//             if (c == '\n')
//             {
//                 rtk.buf[rtk.fil] = 0;
//                 const int err = send(sock, rtk.buf, rtk.fil, MSG_DONTWAIT);
//                 if (err < 0)
//                     printf("send failed: errno %d\n", errno);
//                 rtk.fil = 0;
//                 rtk.state = RCV_IDLE;
// #if 0
//                 if (rtk.buf[0] == '$')
//                 {
//                     /* $GNGGA, 091628.00, 3844.78718183,N, 07755.96337656,W, 7,28,0.5,135.9670,M,-33.6653,M, ,*44 */
//                     if (strncmp((char *) rtk.buf, "$GNGGA", 6) == 0)
//                     {
//                         char *p = nextArg((char *) rtk.buf);
//
//                         int gpsTime = getNum(&p, 2) * 60;
//                         gpsTime += getNum(&p, 2);
//                         gpsTime *= 60;
//                         gpsTime += getNum(&p, 2);
//
//                         p = nextArg(p);
//                         int tmp = getNum(&p, 2);
//                         const double lat = (double) tmp + strtod(p, &p) / 60.0;
//                         p = nextArg(p);
//                         p = nextArg(p);
//                         tmp = getNum(&p, 2);
//                         const double lon = -((double) tmp + strtod(p, &p) / 60.0);
//                         printf("gpsTime %6d lat %13.10f lon %14.10f\n", gpsTime, lat, lon);
//                     }
//                 }
// #endif
//             }
//
//
//         }
//     }
//     dbg1Clr();
// }

#endif  /* GPS_LIB */

ip_addr_t resolved_addr;
int state;
char ip_str[20];

void dns_callback(const char* name, const ip_addr_t* ipaddr, void* arg)
{
 if (ipaddr)
 {
  ip4addr_ntoa_r(reinterpret_cast<const struct ip4_addr*>(ipaddr), ip_str, sizeof(ip_str));
  // Handle successful resolution
  printf("Resolved %s to %s\n", name, ip_str);
  resolved_addr.u_addr.ip4.addr = ipaddr->u_addr.ip4.addr;
 }
 else
 {
  // Handle failure or timeout
  printf("Failed to resolve %s\n", name);
  state = 0;
 }
 printf("dns_callback state is %d\n", state);
}

extern "C" { uint8_t temprature_sens_read(void); }

static void tcp_client_task(void* pvParameters)
{
 state = 0;
 while (true)
 {
  printf("state: %d\n", state);
  if (state == 0)
  {
   const err_t err = dns_gethostbyname(SERVER_HOSTNAME,
				       &resolved_addr, dns_callback, nullptr);
   if (err == ERR_OK)
   {
    // Hostname was already cached or is a valid IP string
    ip4addr_ntoa_r(reinterpret_cast<const struct ip4_addr*>(&resolved_addr),
		   ip_str, sizeof(ip_str));
    printf("ip %s\n", ip_str);
    break;
   }
   else if (err == ERR_INPROGRESS)
   {
    // Query sent to DNS server; wait for callback
    printf("Lookup in progress...\n");
    state = 1;
   }
  }
  else if (state == 1)
  {
   if (resolved_addr.u_addr.ip4.addr != 0)
    break;
  }
  vTaskDelay(pdMS_TO_TICKS(4000));
  printf("lookup loop timeout state %d\n", state);
 }

 // ReSharper disable once CppDFAEndlessLoop
 while (true)
 {

  struct sockaddr_in dest_addr{};
  dest_addr.sin_addr.s_addr = resolved_addr.u_addr.ip4.addr;
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(TCP_SERVER_PORT);

  printf("resolved IP Address: %s\n", inet_ntoa(resolved_addr.u_addr.ip4.addr));
  printf("dest_addr IP Address: %s\n", inet_ntoa(dest_addr.sin_addr));

  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0)
  {
   ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
   vTaskDelay(pdMS_TO_TICKS(4000));
   continue;
  }

  ESP_LOGI(TAG, "Connecting to %s:%d ...", ip_str, TCP_SERVER_PORT);
  if (const int err = connect(sock, reinterpret_cast<struct sockaddr*>(&dest_addr),
			      sizeof(dest_addr));
      err != 0)
  {
   ESP_LOGE(TAG, "Connect failed: err %d errno %d %x", err, errno, errno);
   close(sock);
   vTaskDelay(pdMS_TO_TICKS(4000));
   continue;
  }
  ESP_LOGI(TAG, "Connected.");

  size_t uart_len = 0;
  uart_get_buffered_data_len(UART_NUM_1, &uart_len);
  printf("flushing %d bytes\n", uart_len);
  while (uart_len > 0)
  {
   char uart_buf[128];
   size_t len = uart_len > sizeof(uart_buf) ? sizeof(uart_buf) : uart_len;
   len = uart_read_bytes(UART_NUM_1, uart_buf, len, 0);
   uart_len -= len;
  }

  while (true)
  {
#if defined(U8X8)
   static int64_t tmr0;
   if (auto t0 = esp_timer_get_time(); (t0 - tmr0) > 1000 * 1000)
   {
    tmr0 = t0;

    uint8_t temp = temprature_sens_read();

    char buf[20];
    snprintf(buf, sizeof(buf), "    %3d %4d ", temp, rtk.rxCount);
    drawString(0, 1, buf);
   }
#endif	/* USE_U8X8 */

#if defined(GPS_LIB)

   pollSerial();

#endif  /* GSP_LIB */

   // 1. Check UART
   uart_get_buffered_data_len(UART_NUM_1, &uart_len);
   while (uart_len > 0)
   {
    char uart_buf[1024];
    size_t len = uart_len > sizeof(uart_buf) ? sizeof(uart_buf) : uart_len;
    len = uart_read_bytes(UART_NUM_1, uart_buf, len, 0);

    processSerial(sock, uart_buf, len);

#if defined(U8X8)

   if (gpsInfo.update)
   {
    gpsInfo.update = false;
    char buf[20];
    drawString(0, 2, gpsInfo.timeBuf);
    snprintf(buf, sizeof(buf), "%d %2d   ", gpsInfo.fix, gpsInfo.sats);
    drawString(9, 2, buf);  // 9 10 11 12 13 14 15

    snprintf(buf, sizeof(buf), " %13.10f", gpsInfo.lat);
    drawString(0, 3, buf);
    snprintf(buf, sizeof(buf), "%14.10f", gpsInfo.lon);
    drawString(0, 4, buf);
   }

#endif	/* USE_U8X8 */

    uart_len -= len;
    // rtk.t0 = esp_timer_get_time();
   }

   // 2. Check Socket
   char sock_buf[128];
   if (int len = recv(sock, sock_buf, sizeof(sock_buf) - 1, MSG_DONTWAIT);
       len > 0)
   {
    sock_buf[len] = '\0';
#if 1
    processRemData(sock_buf, len);
#else
    // Send socket data to UART
    uart_write_bytes(UART_NUM_1, sock_buf, len);
#endif
   }
   else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
   {
    // Handle socket error or disconnect
    break;
   }

   // 3. Yield CPU if no data was processed
   vTaskDelay(pdMS_TO_TICKS(10));
  }

  shutdown(sock, 0);
  close(sock);
  ESP_LOGI(TAG, "Reconnecting in 4s...");
  vTaskDelay(pdMS_TO_TICKS(4000));
 }
}

#endif	/* CLIENT */

#define UART1_MSG "Hello UART1\n"

extern "C" void app_main()
{
 buildCRC24qTable();
 ESP_ERROR_CHECK(nvs_flash_init());
 ESP_ERROR_CHECK(esp_netif_init());
 ESP_ERROR_CHECK(esp_event_loop_create_default());

 gpio_config_t io_conf =
  {
   .pin_bit_mask = (1ULL << DBG0_PIN), // Select GPIO
   .mode = GPIO_MODE_OUTPUT, // Set as output
   .pull_up_en = GPIO_PULLUP_DISABLE, // Disable pull-up
   .pull_down_en = GPIO_PULLDOWN_DISABLE, // Disable pull-down
   .intr_type = GPIO_INTR_DISABLE // Disable interrupts
  };
 gpio_config(&io_conf);
 io_conf.pin_bit_mask = (1ULL << DBG1_PIN);
 gpio_config(&io_conf);

 eth_event_group = xEventGroupCreate();

 uart1_init();
 uart_write_bytes(UART_NUM_1, UART1_MSG, sizeof(UART1_MSG) - 1);

#if defined(U8X8)
 u8x8Init();
#endif	/* U8X8 */

 eth_init();

 ESP_LOGI(TAG, "Waiting for an IP address...");
 xEventGroupWaitBits(eth_event_group, ETH_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

 esp_netif_dns_info_t dns_info;

 if (const esp_err_t err = esp_netif_get_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns_info);
     err == ESP_OK)
 {
  // Print IPv4 address
  ESP_LOGI(TAG, "Main DNS Server1: %s", inet_ntoa(dns_info.ip.u_addr.ip4));
 }
 else
 {
  ESP_LOGE(TAG, "Failed to get DNS info: %d", err);
 }

 esp_netif_t *netIf = esp_netif_get_handle_from_ifkey("ETH_DEF");
 esp_netif_ip_info_t ipInfo;
 esp_netif_get_ip_info(netIf, &ipInfo);

#if defined(U8X8)
 char tmp[20];
 snprintf(tmp, sizeof(tmp), IPSTR " %c", IP2STR(&ipInfo.ip), hostname[0]);
 drawString(0, 0, tmp);
#endif	/* USE_U8X8 */


#if defined(CLIENT)
 xTaskCreate(tcp_client_task, "tcp_client", 4096, nullptr, 5, nullptr);
#endif	/* CLIENT */

#if defined(SERVER)
 // s_data_rx_callback = default_data_rx_callback;
 xTaskCreate(tcp_server_task, "tcp_server", 4096, reinterpret_cast<void*>(AF_INET), 5, nullptr);
#endif	/* SERVER */

 // ReSharper disable once CppDFAEndlessLoop
 while (true)
 {
  vTaskDelay(pdMS_TO_TICKS(4000));
 }
}
