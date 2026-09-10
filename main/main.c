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

#include <string.h>
#include <lwip/dns.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
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
#include "esp32/rom/gpio.h"

#define SERVER
//#define CLIENT
#define RTK_SEND

#define UART_NUM UART_NUM_1
#define TX_PIN 17
#define RX_PIN 5

#define TX_PIN_2 14
#define RX_PIN_2 12

#define DBG0_PIN 2
#define DBG1_PIN 4

#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 1024

void test();

inline void dbg0Set()
{
    REG_WRITE(GPIO_OUT_W1TS_REG, (1 << DBG0_PIN));
}

inline void dbg0Clr()
{
 REG_WRITE(GPIO_OUT_W1TC_REG, (1 << DBG0_PIN));
}

inline void dbg1Set()
{
 REG_WRITE(GPIO_OUT_W1TS_REG, (1 << DBG1_PIN));
}

inline void dbg1Clr()
{
 REG_WRITE(GPIO_OUT_W1TC_REG, (1 << DBG1_PIN));
}

void uart1_init() {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    // Apply configuration
    uart_param_config(UART_NUM, &uart_config);

    // Set pins (TX, RX, RTS, CTS)
    uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Install driver with buffer sizes
    uart_driver_install(UART_NUM, RX_BUF_SIZE, TX_BUF_SIZE, 0, nullptr, 0);
}

void uart2_init() {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

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
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

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
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ip_info->ip));

    esp_netif_t *netif = event->esp_netif;

    esp_netif_dns_info_t dns_info;
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    ESP_LOGI(TAG, "DNS Server: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));

    xEventGroupSetBits(eth_event_group, ETH_CONNECTED_BIT);
}

static void tx_rx_event_handler(void *arg, esp_event_base_t event_base,
				int32_t event_id, void *event_data) {
    ip_event_tx_rx_t *event = (ip_event_tx_rx_t *)event_data;

    if (event->dir == ESP_NETIF_RX) {
        // Process incoming data from event->len or associated buffers
        ESP_LOGI("TAG", "Got RX event: Interface \"%s\" data len: %d",
                 esp_netif_get_desc(event->esp_netif), event->len);
    }
}

esp_netif_t *eth_netif;

static esp_eth_handle_t eth_init(void)
{
  // gpio_set_direction(ETH_RMII_CLK_GPIO, GPIO_MODE_DISABLE);
  // gpio_set_pull_mode(ETH_RMII_CLK_GPIO, GPIO_FLOATING);

  // gpio_set_direction(ETH_MDIO_GPIO, GPIO_MODE_DISABLE);
  // gpio_set_pull_mode(ETH_MDIO_GPIO, GPIO_FLOATING);

    /* WT32-ETH01 specific: GPIO16 must be driven high to power the PHY */
    gpio_set_direction(ETH_PHY_POWER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ETH_PHY_POWER_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); /* let the PHY power up */

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&netif_cfg);

    esp_err_t err = esp_netif_set_hostname(eth_netif, hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Hostname set to: %s", hostname);
    }

    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    // esp32_emac_config.smi_gpio.mdc_num= ETH_MDC_GPIO;
    // esp32_emac_config.smi_gpio.mdc_num = ETH_MDIO_GPIO;

    /* Board has an external oscillator driving the RMII ref clock into
     * this GPIO, rather than the ESP32 generating the clock itself. */
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_emac_config.clock_config.rmii.clock_gpio = ETH_RMII_CLK_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;

    // esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                                &eth_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                &got_ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_TX_RX,
					       &tx_rx_event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    return eth_handle;
}

/* ── CRC-24Q constants ───────────────────────────────────────────────────── */
 
#define CRC24Q_POLY      0x1864CFBu  /* Generator polynomial                 */
#define RTCM3_PREAMBLE   0xD3u       /* Mandatory first byte of every frame  */
#define RTCM3_HDR_LEN    3           /* Preamble + 2 length/reserved bytes   */
#define RTCM3_CRC_LEN    3           /* 24-bit CRC appended at end           */
#define RTCM3_MIN_FRAME  (RTCM3_HDR_LEN + RTCM3_CRC_LEN)
 
/* ── CRC-24Q lookup table (generated once on first use) ─────────────────── */
 
static uint32_t crc24qTable[256];
 
static void buildCRC24qTable()
{
 for (uint32_t i = 0; i < 256; i++)
 {
  uint32_t crc = i << 16;
  for (int j = 0; j < 8; j++)
  {
   crc <<= 1;
   if (crc & 0x1000000u)
    crc ^= CRC24Q_POLY;
  }
  crc24qTable[i] = crc & 0xFFFFFFu;
 }
}

uint32_t crc24(const uint32_t crc, const unsigned char c)
{
 return ((crc << 8) ^ crc24qTable[((crc >> 16) ^ c) & 0xFFu]) & 0xFFFFFFu;
}

enum RCV_STATE {RCV_IDLE, RCV_GET_LEN, RCV_GET_DATA, RCV_TEXT};

constexpr size_t RTK_BUF_SIZE = 1024;
uint32_t crcBuf[1024];

typedef struct S_RTK_DATA
{
 enum RCV_STATE state;
 uint64_t t0;
 uint64_t startTime;
 uint32_t crc;
 int count;
 int len;
 int fil;
 unsigned char buf[RTK_BUF_SIZE];
 unsigned int t0Accum;
 int rxAccum;
 //int rxCount;
} T_RTK_DATA, *P_RTK_DATA;

T_RTK_DATA rtk;

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

static void processData(const unsigned char *ptr, size_t len)
{
    while (len > 0)
    {
        len -= 1;
        const unsigned char ch = *ptr;
        switch (rtk.state)
        {
        case RCV_IDLE:
            if (ch == 0xd3)
            {
                rtk.count = 2;
                uart_write_bytes(UART_NUM_1, ptr, 1);
                rtk.buf[0] = ch;
                rtk.crc = crc24qTable[(int) ch];
                crcBuf[0] = rtk.crc;
                rtk.fil = 1;
                rtk.t0 = esp_timer_get_time();
                rtk.state = RCV_GET_LEN;
            }
            else //if (ch == '$')
            {
                rtk.t0 = esp_timer_get_time();
                rtk.state = RCV_TEXT;
                uart_write_bytes(UART_NUM_1, ptr, 1);
#if 0
                Serial.print(ch);
                Serial.flush();
#endif
            }
            break;

        case RCV_GET_LEN:
            uart_write_bytes(UART_NUM_1, ptr, 1);
            rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ ch) & 0xFFu]) & 0xFFFFFFu;
            crcBuf[rtk.fil] = rtk.crc;
            rtk.len = (rtk.len << 8) + ch;
            rtk.buf[rtk.fil] = ch;
            rtk.fil += 1;
            rtk.count -= 1;
            if (rtk.count == 0)
            {
                rtk.state = RCV_GET_DATA;
                rtk.len &= 0x3ff;
                // printf("dLen %d\n", dLen);
#if defined(DBG_PRT)
                //if (rtk.len == 19)
                if ((prt == 0) && (rtk.len == 19))
                {
                    prt = 1;
                }
#endif	/* DBG_PRT */
                rtk.len += 3;
            }
            break;

        case RCV_GET_DATA:
            uart_write_bytes(UART_NUM_1, ptr, 1);
            rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ ch) & 0xFFu]) & 0xFFFFFFu;
            crcBuf[rtk.fil] = rtk.crc;
            rtk.buf[rtk.fil] = ch;
            rtk.fil += 1;
            rtk.len -= 1;
            if (rtk.len == 0)
            {
#if 0
                const int type = (rtk.buf[3] << 4) | (rtk.buf[4] >> 4);
                printf("len %4d type %4d CRC %08x\n", rtk.fil, type, rtk.crc);
#endif
#if defined(DBG_PRT)
                if (prt == 1)
                {
                    printHex(reinterpret_cast<const uint8_t *>(rtk.buf), rtk.fil);
                    printHex(reinterpret_cast<const uint8_t *>(crcBuf), rtk.fil << 2);
                    prt = 0;
                }
#endif	/* DBG_PRT */
                rtk.state = RCV_IDLE;
            }
            break;

        case RCV_TEXT:
            uart_write_bytes(UART_NUM_1, ptr, 1);;
#if 0
            Serial.print(ch);
#endif
            if (ch == '\n')
            {
                rtk.state = RCV_IDLE;
            }
            break;
        }
        ptr += 1;
    }
}

static void tcp_server_client_handler(void *pvParameters)
{
    int sock = (int)(intptr_t)pvParameters;
    uint8_t rx_buffer[1600];
    printf("task start server client handler %d\n", sock);
#if 0
    while (1) {
        int len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        if (len < 0) {
            ESP_LOGE(TAG, "[server] recv failed on sock %d: errno %d", sock, errno);
            break;
        } else if (len == 0) {
            ESP_LOGI(TAG, "[server] client on sock %d closed the connection", sock);
            break;
        }

        printf("rcv len %d\n", len);
        uart_write_bytes(UART_NUM_1, rx_buffer, len);

        // if (s_data_rx_callback) {
        //     s_data_rx_callback(sock, rx_buffer, (size_t)len);
        // }
    }
#else
    while (1) {
        // 1. Check UART
        size_t uart_len = 0;
        uart_get_buffered_data_len(UART_NUM_1, &uart_len);
        if (uart_len > 0) {
            uart_read_bytes(UART_NUM_1, rx_buffer, uart_len, 0);
            const int err = send(sock, rx_buffer, uart_len, MSG_DONTWAIT);
            if (err < 0)
                printf("send failed: errno %d\n", errno);
        }

        // 2. Check Socket
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT);
        if (len > 0) {
            processData(rx_buffer, len);
            //uart_write_bytes(UART_NUM_1, rx_buffer, len);
        } else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Handle socket error or disconnect
            break; 
        }

        // 3. Yield CPU if no data was processed
        vTaskDelay(pdMS_TO_TICKS(10));
    }    
#endif
    printf("loop done\n");

    shutdown(sock, 0);
    close(sock);
    vTaskDelete(nullptr);
    printf("task done\n");
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_storage dest_addr;

    printf("tcp server task addr_family %d\n", addr_family);

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(TCP_SERVER_PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");
    char ip_str[INET_ADDRSTRLEN];

    struct sockaddr_in *ipv4 = (struct sockaddr_in *) &dest_addr;

    // Print IP address
    inet_ntop(AF_INET, &ipv4->sin_addr, ip_str, INET_ADDRSTRLEN);
    uint16_t port = ntohs(ipv4->sin_port);

    printf("IPv4 Address: %s, Port: %d\n", ip_str, port);

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", TCP_SERVER_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(nullptr);
        return;
    }

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (client_sock < 0) {
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
                    (void *)(intptr_t)client_sock, 5, nullptr);
    }
}

#endif	/* SERVER */

#if defined(CLIENT)

char* nextArg(char* p0)
{
 while (true)
 {
  const char c0 = *p0;
  if (c0 == 0)
   break;
  p0 += 1;
  if (c0 == ',')
  {
   break;
  }
 }
 return p0;
}

int getNum(char **p0, int n)
{
 char *p1 = *p0;
 int val = 0;
 while (n > 0)
 {
  const char c1 = *p1++;
  val *= 10;
  val += c1 - '0';
  n -= 1;
 }
 *p0 = p1;
 return val;
}

void processSerial(const int sock, char* buf, size_t len)
{
    while (len > 0)
    {
        len -= 1;
        dbg1Set();
        const unsigned char c = *buf++;
        switch (rtk.state)
        {
        case RCV_IDLE:
            if (c == 0xd3)
            {
                dbg0Set();
                rtk.count = 2;
                rtk.buf[0] = c;
                rtk.crc = crc24qTable[c];
                crcBuf[0] = rtk.crc;
                rtk.fil = 1;
                rtk.t0 = esp_timer_get_time();
                rtk.state = RCV_GET_LEN;
                rtk.startTime = esp_timer_get_time();
            }
            else // if (c == '$')
            {
                rtk.t0 = esp_timer_get_time();
                rtk.state = RCV_TEXT;
                putchar(c);
                rtk.buf[0] = c;
                rtk.fil = 1;
            }
            break;

        case RCV_GET_LEN:
            rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ c) & 0xFFu]) & 0xFFFFFFu;
            crcBuf[rtk.fil] = rtk.crc;
            rtk.len = (rtk.len << 8) + c;
            rtk.buf[rtk.fil] = c;
            rtk.fil += 1;
            rtk.count -= 1;
            if (rtk.count == 0)
            {
                rtk.state = RCV_GET_DATA;
                rtk.len &= 0x3ff;
                // printf("rtkLen %d\n", rtk.len);
#if 0 && defined(DBG_PRT)
                // if ((prt == 0) && (rtk.len == 19))
                if (rtk.len == 19)
                {
                    prt = 1;
                }
#endif	/* DBG_PRT */
                rtk.len += 3;
            }
            break;

        case RCV_GET_DATA:
            rtk.crc = ((rtk.crc << 8) ^ crc24qTable[((rtk.crc >> 16) ^ c) & 0xFFu]) & 0xFFFFFFu;
            crcBuf[rtk.fil] = rtk.crc;
            rtk.buf[rtk.fil] = c;
            rtk.fil += 1;
            rtk.len -= 1;
            if (rtk.len == 0)
            {
                rtk.rxAccum += rtk.fil;
#if 1
                const auto msgT = (esp_timer_get_time() - rtk.startTime);
                int type = (rtk.buf[3] << 4) | (rtk.buf[4] >> 4);
                printf("rtkLen %4d type %4d rtkCRC %08x %5d %llu\n",
                       rtk.fil, type, (unsigned int) rtk.crc, rtk.rxAccum, msgT);
#endif
                rtk.t0Accum = esp_timer_get_time();
#if defined(RTK_SEND)
                const int err = send(sock, rtk.buf, rtk.fil, MSG_DONTWAIT);
                if (err < 0)
                    printf("send failed: errno %d\n", errno);

#endif	/* RTK_SEND */

#if 0 && defined(DBG_PRT)
                if (prt == 1)
                {
                    printHex(reinterpret_cast<const u_int8_t *>(rtk.buf), rtk.fil);
                    printHex(reinterpret_cast<const u_int8_t *>(crcBuf), rtk.fil << 2);
                    prt = 0;
                }
#endif	/* DDBG_PRT */
                dbg0Clr();
                rtk.state = RCV_IDLE;
            }
            break;

        case RCV_TEXT:
            putchar(c);
            rtk.buf[rtk.fil] = c;
            rtk.fil += 1;
            if (c == '\n')
            {
                rtk.buf[rtk.fil] = 0;
                const int err = send(sock, rtk.buf, rtk.fil, MSG_DONTWAIT);
                if (err < 0)
                    printf("send failed: errno %d\n", errno);
                rtk.fil = 0;
                rtk.state = RCV_IDLE;
#if 0
                if (rtk.buf[0] == '$')
                {
                    /* $GNGGA, 091628.00, 3844.78718183,N, 07755.96337656,W, 7,28,0.5,135.9670,M,-33.6653,M, ,*44 */
                    if (strncmp((char *) rtk.buf, "$GNGGA", 6) == 0)
                    {
                        char *p = nextArg((char *) rtk.buf);

                        int gpsTime = getNum(&p, 2) * 60;
                        gpsTime += getNum(&p, 2);
                        gpsTime *= 60;
                        gpsTime += getNum(&p, 2);

                        p = nextArg(p);
                        int tmp = getNum(&p, 2);
                        const double lat = (double) tmp + strtod(p, &p) / 60.0;
                        p = nextArg(p);
                        p = nextArg(p);
                        tmp = getNum(&p, 2);
                        const double lon = -((double) tmp + strtod(p, &p) / 60.0);
                        printf("gpsTime %6d lat %13.10f lon %14.10f\n", gpsTime, lat, lon);
                    }
                }
#endif
            }


        }
    }
    dbg1Clr();
}

ip_addr_t resolved_addr;
int state;
char ip_str[20];

//void dns_callback(const char *name, ip_addr_t *ipaddr, void *arg)
//{aka void (*)(const char *, struct ip_addr *, void *)
//{}
void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr) {
        ip4addr_ntoa_r((const struct ip4_addr *) ipaddr, ip_str, sizeof(ip_str));
        // Handle successful resolution
        printf("Resolved %s to %s\n", name, ip_str);
        resolved_addr.u_addr.ip4.addr = ipaddr->u_addr.ip4.addr;
    } else {
        // Handle failure or timeout
        printf("Failed to resolve %s\n", name);
        state = 0;
    }
    printf("dns_callback state is %d\n", state);
}

static void tcp_client_task(void *pvParameters)
{
    state = 0;
    while (true)
    {
        printf("state: %d\n", state);
        if (state == 0)
        {
            const err_t err = dns_gethostbyname(/*"www.google.com"*/ SERVER_HOSTNAME ,
                &resolved_addr, dns_callback, NULL);
            if (err == ERR_OK) {
                // Hostname was already cached or is a valid IP string
                ip4addr_ntoa_r((const struct ip4_addr *) &resolved_addr, ip_str, sizeof(ip_str));
                printf("ip %s\n", ip_str);
                break;
            } else if (err == ERR_INPROGRESS) {
                // Query sent to DNS server; wait for callback
                printf("Lookup in progress...\n");
                state = 1;
            }
        } else if (state == 1) {
            if (resolved_addr.u_addr.ip4.addr != 0)
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(4000));
        printf("lookup loop timeout state %d\n", state);
    }

    // ReSharper disable once CppDFAEndlessLoop
    while (1) {
        // char ip_str[20];
        // struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
        // struct addrinfo *res = nullptr;
        // if (getaddrinfo(SERVER_HOSTNAME, nullptr, &hints, &res) == 0 &&
        //     res != NULL) {
        //     size_t ip_str_len = 0;
        //     struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
        //     inet_ntoa_r(sa->sin_addr, ip_str, ip_str_len);
        //     freeaddrinfo(res);
        //     ESP_LOGI(TAG, "DNS resolved %s -> %s", SERVER_HOSTNAME, ip_str);
        // } else {
        //     ESP_LOGE(TAG, "Could not resolve host '%s'", SERVER_HOSTNAME);
        //     vTaskDelay(pdMS_TO_TICKS(4000));
        //     continue;
        // }

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = resolved_addr.u_addr.ip4.addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(TCP_SERVER_PORT);

        printf("resolved IP Address: %s\n", inet_ntoa(resolved_addr.u_addr.ip4.addr));
        printf("dest_addr IP Address: %s\n", inet_ntoa(dest_addr.sin_addr));

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(4000));
            continue;
        }

        ESP_LOGI(TAG, "Connecting to %s:%d ...", ip_str, TCP_SERVER_PORT);
        const int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Connect failed: err %d errno %d %x", err, errno, errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(4000));
            continue;
        }
        ESP_LOGI(TAG, "Connected.");

#if 0
        const char *payload = "Hello from WT32-ETH01!\r\n";
        send(sock, payload, strlen(payload), 0);

        /* Read whatever the server sends back until it closes the connection */
        while (1) {
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            } else if (len == 0) {
                ESP_LOGW(TAG, "Server closed the connection");
                break;
            } else {
                rx_buffer[len] = 0;
                ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
            }
        }
#else
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

        while (1)
        {
            if (rtk.state != RCV_IDLE)
            {
                const unsigned int delta = (unsigned int) (esp_timer_get_time() - rtk.t0);
                if (delta > (100 * 1000))
                {
                    printf("receive timeout %d %u\n", rtk.state, delta);
                    rtk.state = RCV_IDLE;
                }
            }

            // 1. Check UART
            uart_get_buffered_data_len(UART_NUM_1, &uart_len);
            while (uart_len > 0)
            {
                char uart_buf[1024];
                size_t len = uart_len > sizeof(uart_buf) ? sizeof(uart_buf) : uart_len;
                len = uart_read_bytes(UART_NUM_1, uart_buf, len, 0);
                processSerial(sock, uart_buf, len);
                uart_len -= len;
                rtk.t0 = esp_timer_get_time();
            }

            // 2. Check Socket
            char sock_buf[128];
            int len = recv(sock, sock_buf, sizeof(sock_buf) - 1, MSG_DONTWAIT);
            if (len > 0)
            {
                sock_buf[len] = '\0';
                // Send socket data to UART
                uart_write_bytes(UART_NUM_1, sock_buf, len);
            }
            else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                // Handle socket error or disconnect
                break;
            }

            // 3. Yield CPU if no data was processed
            vTaskDelay(pdMS_TO_TICKS(10));
        }
#endif

        shutdown(sock, 0);
        close(sock);
        ESP_LOGI(TAG, "Reconnecting in 4s...");
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}

#endif	/* CLIENT */

void app_main(void)
{
    buildCRC24qTable();
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DBG0_PIN),     // Select GPIO
        .mode = GPIO_MODE_OUTPUT,               // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,      // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE          // Disable interrupts
    };
    gpio_config(&io_conf);
    io_conf.pin_bit_mask = (1ULL << DBG1_PIN);
    gpio_config(&io_conf);

    eth_event_group = xEventGroupCreate();

    uart1_init();
    uart_write_bytes(UART_NUM_1, "Hello UART1\n", strlen("Hello UART1\n"));

    test();

    eth_init();

    ESP_LOGI(TAG, "Waiting for an IP address...");
    xEventGroupWaitBits(eth_event_group, ETH_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    esp_netif_dns_info_t dns_info;
    // Get the main DNS server for the WiFi station interface
    const esp_err_t err = esp_netif_get_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns_info);

    if (err == ESP_OK) {
        // Print IPv4 address
        ESP_LOGI(TAG, "Main DNS Server1: %s", inet_ntoa(dns_info.ip.u_addr.ip4));
    } else {
        ESP_LOGE(TAG, "Failed to get DNS info: %d", err);
    }

#if defined(CLIENT)
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, nullptr);
#endif
#if defined(SERVER)
    // s_data_rx_callback = default_data_rx_callback;
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void *) AF_INET, 5, nullptr);
#endif
    // ReSharper disable once CppDFAEndlessLoop
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}
#if 0
/*
GP: GPS satellites
GL: GLONASS satellites
GA: Galileo satellites
GB or BD: BeiDou satellites

Total Messages: The total number of GSV sentences in the current cycle.

Message Number: The current sentence number (1 to Total).
 
Satellites in View: The total number of satellites currently visible to the receiver.
 
Satellite Data Blocks: Up to four sets of four values each:
PRN: The satellite's PRN (Pseudo-Random Noise) number.
Elevation: The satellite's elevation in degrees (00–90).
Azimuth: The satellite's azimuth in degrees from true north (000–359).
SNR: The Signal-to-Noise Ratio in dB (00–99); this field may be empty if the satellite is not currently tracked. 


$GPGSV,2,1, 05, 29,05,194,31, 15,49,046,30, 18,75,200,44, 05,14,095,31, 1*6D
$GPGSV,2,2, 05, 24,52,122,46, 1*53

$GPGSV,1,1, 03, 29,05,194,33,18,75,200,42,24,52,122,39,4*55

$GPGSV,1,1,03,18,75,200,49,23,56,321,31,24,52,122,47,8*59

$GLGSV,1,1,04,71,51,018,26,86,59,197,46,72,49,281,32,73,00,000,29,1*71

$GLGSV,1,1,03,86,59,197,42,72,49,281,31,73,00,000,30,3*44

$GBGSV,2,1,06,11,14,172,38,20,42,051,32,29,30,189,49,22,24,265,32,1*7B
$GBGSV,2,2,06,19,68,321,24,35,49,262,37,1*79

$GBGSV,1,1,04,20,42,051,26,29,30,189,46,22,24,265,29,35,49,262,33,3*7F

$GBGSV,2,1,06,11,14,172,35,20,42,051,25,29,30,189,42,12,60,033,27,8*74
$GBGSV,2,2,06,22,24,265,35,35,49,262,36,8*70

$GAGSV,2,1,06,19,89,120,30,28,57,222,47,33,49,140,47,04,32,225,42,1*7D
$GAGSV,2,2,06,06,07,225,29,18,,,42,1*43

$GAGSV,2,1,06,19,89,120,29,28,57,222,46,33,49,140,43,04,32,225,43,2*72
$GAGSV,2,2,06,29,37,046,27,18,,,36,2*44

$GAGSV,1,1,04,19,89,120,29,28,57,222,44,33,49,140,42,04,32,225,43,5*77

$GAGSV,2,1,06,19,89,120,25,28,57,222,42,33,49,140,43,04,32,225,43,7*7F
$GAGSV,2,2,06,06,07,225,22,18,,,42,7*4E

GPS              GLONASS               Galileo          BeiDou (BDS)       
ID Signal        ID Signal             ID Signal        ID Signal     
0  All signals   0  All signals        0  All signals   0  All signals
1  L1 C/A        1  G1 C/A             1  E5a           1  B1I        
2  L1 P(Y)       2  G1 P               2  E5b           2  B1Q        
3  L1 M          3  G2 C/A             3  E5 (a+b)      3  B1C        
4  L2 P(Y)       4  G2 P (GLONASS-M)   4  E6-A          4  B1A        
5  L2C-M         5–F Reserved          5  E6-BC         5  B2-a       
6  L2C-L                               6  L1-A          6  B2-b       
7  L5-I                                7  L1-BC         7  B2 (a+b)   
8  L5-Q                                8–F Reserved     8  B3I         
9–F Reserved                                            9  B3Q        
                                                        A  B3A        
                                                        B  B2I        
                                                        C  B2Q        
                                                        D–F Reserved   
*/
#endif
