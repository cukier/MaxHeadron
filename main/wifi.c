#include "sdkconfig.h" // must come before the #if below - nothing else here pulls it in
#include "include/wifi.h"

// Wi-Fi SSID/password/channel Kconfig options only exist when Ethernet
// isn't selected (see Kconfig.projbuild's `if !MAXHEADRON_USE_ETHERNET`),
// so this whole implementation has to be compiled out when it is.
#if !CONFIG_MAXHEADRON_USE_ETHERNET

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "max_headron_wifi";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// Exponential backoff between reconnect attempts, adapted from the wifi
// manager in kiot's kb device (device/kb/main/comm.c) - a bare immediate
// retry on every WIFI_EVENT_STA_DISCONNECTED hot-loops (and floods the log)
// when the AP is genuinely down, instead of a router reboot or a dropped
// frame here and there.
#define BACKOFF_MIN_MS (1000)
#define BACKOFF_MAX_MS (300000) // 5 minutes
static int s_backoff_ms = BACKOFF_MIN_MS;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "disconnected, retrying in %d ms...", s_backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
        esp_wifi_connect();
        s_backoff_ms = (s_backoff_ms * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : s_backoff_ms * 2;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_backoff_ms = BACKOFF_MIN_MS; // reset backoff once we're actually connected
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_connect_blocking(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_MAXHEADRON_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_MAXHEADRON_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(CONFIG_MAXHEADRON_WIFI_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.channel = CONFIG_MAXHEADRON_WIFI_CHANNEL; // 0 = scan all channels

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to '%s'...", CONFIG_MAXHEADRON_WIFI_SSID);
    // The L2 link can associate fine while DHCP never completes (seen on
    // this network: block-ack sessions churn nonstop after "connected",
    // and no WIFI_EVENT_STA_DISCONNECTED ever fires to trigger the backoff
    // reconnect above) - so don't just wait forever on one attempt. Force a
    // fresh disconnect/reconnect if we haven't got an IP within a bounded
    // window; the disconnect handler's own backoff paces repeated retries.
    const TickType_t dhcp_timeout = pdMS_TO_TICKS(15000);
    while ((xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, dhcp_timeout)
            & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "no IP after %u ms, forcing reconnect...", (unsigned)pdTICKS_TO_MS(dhcp_timeout));
        esp_wifi_disconnect();
    }
    ESP_LOGI(TAG, "Wi-Fi connected.");
}

#else

void wifi_connect_blocking(void) {
}

#endif
