#include "sdkconfig.h" // must come before the #if below - nothing else here pulls it in
#include "include/eth.h"

// The W5500 GPIO Kconfig options only exist when CONFIG_MAXHEADRON_USE_ETHERNET
// is set (see Kconfig.projbuild's `if MAXHEADRON_USE_ETHERNET`), so this
// whole implementation has to be compiled out when it isn't.
#if CONFIG_MAXHEADRON_USE_ETHERNET

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char *TAG = "max_headron_eth";
static EventGroupHandle_t s_eth_event_group;
#define ETH_CONNECTED_BIT BIT0

#define ETH_SPI_CLOCK_MHZ (6)
#define ETH_SPI_HOST (SPI2_HOST)

static void spi_bus_init(void) {
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_MAXHEADRON_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_MAXHEADRON_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_MAXHEADRON_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
}

static esp_eth_handle_t eth_init_w5500(void) {
    uint8_t base_mac_addr[ETH_ADDR_LEN] = {0};
    uint8_t local_mac[ETH_ADDR_LEN] = {0};
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(base_mac_addr));
    ESP_ERROR_CHECK(esp_derive_local_mac(local_mac, base_mac_addr));

    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = CONFIG_MAXHEADRON_ETH_SPI_CS_GPIO,
    };
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = CONFIG_MAXHEADRON_ETH_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = 2048;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_MAXHEADRON_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_MAXHEADRON_ETH_PHY_RESET_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, local_mac));

    return eth_handle;
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "link up");
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "link down");
                xEventGroupClearBits(s_eth_event_group, ETH_CONNECTED_BIT);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);
    }
}

void eth_connect_blocking(void) {
    s_eth_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    spi_bus_init();
    esp_eth_handle_t eth_handle = eth_init_w5500();

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    esp_eth_netif_glue_handle_t eth_netif_glue = esp_eth_new_netif_glue(eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, eth_netif_glue));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    ESP_LOGI(TAG, "waiting for link + DHCP...");
    // If the link comes up but DHCP never completes, restart the DHCP
    // client rather than waiting forever - same defensive stance as
    // wifi.c's DHCP timeout, even though the W5500's link is a lot more
    // deterministic than the Wi-Fi association issues that motivated that.
    const TickType_t dhcp_timeout = pdMS_TO_TICKS(15000);
    while ((xEventGroupWaitBits(s_eth_event_group, ETH_CONNECTED_BIT, pdFALSE, pdTRUE, dhcp_timeout)
            & ETH_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "no IP after %u ms, restarting DHCP client...", (unsigned)pdTICKS_TO_MS(dhcp_timeout));
        esp_netif_dhcpc_stop(eth_netif);
        esp_netif_dhcpc_start(eth_netif);
    }
    ESP_LOGI(TAG, "Ethernet connected.");
}

#else

void eth_connect_blocking(void) {
}

#endif
