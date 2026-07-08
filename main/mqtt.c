#include "include/mqtt.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#define EXTERNAL_MSG_MAX 24

// main.c's animation loop polls mqtt_get_external_message() and
// mqtt_consume_spark_request() from app_main() itself, while mqtt_link_start()
// runs from a background task (see main.c's network_setup_task) so a slow
// Wi-Fi connect can't freeze the display. s_lock stays NULL until that task
// gets around to calling mqtt_link_start(), so these need to tolerate that.
static SemaphoreHandle_t s_lock;
static char s_external_msg[EXTERNAL_MSG_MAX];
static uint32_t s_external_until_ms;
static bool s_spark_requested;

bool mqtt_get_external_message(char *out, size_t out_len, uint32_t now) {
    if (s_lock == NULL) {
        return false; // mqtt_link_start() hasn't run yet
    }
    bool active = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_external_msg[0] != '\0' && now < s_external_until_ms) {
        strncpy(out, s_external_msg, out_len - 1);
        out[out_len - 1] = '\0';
        active = true;
    }
    xSemaphoreGive(s_lock);
    return active;
}

bool mqtt_consume_spark_request(void) {
    if (s_lock == NULL) {
        return false;
    }
    bool requested = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    requested = s_spark_requested;
    s_spark_requested = false;
    xSemaphoreGive(s_lock);
    return requested;
}

#include <stdlib.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "mdns.h"

static const char *TAG = "max_headron_mqtt";

// lwIP's own ".local" resolution (LWIP_DNS_SUPPORT_MDNS_QUERIES, used
// transparently by getaddrinfo/esp-tls when the broker URI's host ends in
// ".local") is unreliable on this stack - it's what was crashing
// (tcpip.c:454 "Invalid mbox") when the broker URI pointed at
// orangepipc.local. Resolve mDNS hostnames ourselves via the espressif/mdns
// component instead (same approach as kiot's device/kb) and hand esp-mqtt
// a plain IP, so it never has to go through that path.
static bool parse_broker_uri(const char *uri, char *scheme, size_t scheme_len,
                              char *host, size_t host_len, int *port) {
    const char *sep = strstr(uri, "://");
    if (sep == NULL) {
        return false;
    }
    size_t slen = (size_t)(sep - uri);
    if (slen >= scheme_len) {
        slen = scheme_len - 1;
    }
    memcpy(scheme, uri, slen);
    scheme[slen] = '\0';

    const char *host_start = sep + 3;
    const char *host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/') {
        host_end++;
    }
    size_t hlen = (size_t)(host_end - host_start);
    if (hlen >= host_len) {
        hlen = host_len - 1;
    }
    memcpy(host, host_start, hlen);
    host[hlen] = '\0';

    *port = 0;
    if (*host_end == ':') {
        *port = atoi(host_end + 1);
    }
    return true;
}

// If `host` ends in ".local", resolves it via mDNS and overwrites `host`
// with the resulting dotted-quad IP. Leaves `host` untouched otherwise (or
// on resolve failure, so the caller falls back to the original hostname).
static void resolve_mdns_host(char *host, size_t host_len) {
    size_t hlen = strlen(host);
    static const char suffix[] = ".local";
    size_t suffix_len = sizeof(suffix) - 1;
    if (hlen <= suffix_len || strcmp(host + hlen - suffix_len, suffix) != 0) {
        return;
    }
    char bare[64];
    size_t barelen = hlen - suffix_len;
    if (barelen >= sizeof(bare)) {
        barelen = sizeof(bare) - 1;
    }
    memcpy(bare, host, barelen);
    bare[barelen] = '\0';

    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { // INVALID_STATE = already initialized
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    esp_ip4_addr_t addr = {0};
    err = mdns_query_a(bare, 5000, &addr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_query_a('%s') failed: %s - falling back to '%s'", bare, esp_err_to_name(err), host);
        return;
    }
    snprintf(host, host_len, IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "resolved %s.local -> %s", bare, host);
}

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void handle_express_payload(const char *data, int len) {
    cJSON *root = cJSON_ParseWithLength(data, (size_t)len);
    if (root == NULL) {
        ESP_LOGW(TAG, "ignoring malformed JSON payload");
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    const cJSON *say = cJSON_GetObjectItemCaseSensitive(root, "say");
    if (cJSON_IsString(say) && say->valuestring != NULL) {
        strncpy(s_external_msg, say->valuestring, EXTERNAL_MSG_MAX - 1);
        s_external_msg[EXTERNAL_MSG_MAX - 1] = '\0';

        int hold_ms = 4000;
        const cJSON *hold = cJSON_GetObjectItemCaseSensitive(root, "hold_ms");
        if (cJSON_IsNumber(hold)) {
            hold_ms = hold->valueint;
        }
        if (hold_ms < 500) {
            hold_ms = 500;
        }
        if (hold_ms > 15000) {
            hold_ms = 15000;
        }
        s_external_until_ms = now_ms() + (uint32_t)hold_ms;
    }

    const cJSON *spark = cJSON_GetObjectItemCaseSensitive(root, "spark");
    if (cJSON_IsTrue(spark)) {
        s_spark_requested = true;
    }

    xSemaphoreGive(s_lock);
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "connected, subscribing to %s", CONFIG_MAXHEADRON_MQTT_TOPIC);
            esp_mqtt_client_subscribe(client, CONFIG_MAXHEADRON_MQTT_TOPIC, 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "disconnected");
            break;
        case MQTT_EVENT_DATA:
            handle_express_payload(event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "mqtt error, type=%d", event->error_handle->error_type);
            break;
        default:
            break;
    }
}

void mqtt_link_start(void) {
    s_lock = xSemaphoreCreateMutex();
    s_external_msg[0] = '\0';

    esp_mqtt_client_config_t cfg = {
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = CONFIG_MAXHEADRON_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_MAXHEADRON_MQTT_PASSWORD,
    };

    char scheme[8];
    char host[64];
    int port = 0;
    bool is_local_broker = false;
    if (parse_broker_uri(CONFIG_MAXHEADRON_MQTT_BROKER_URI, scheme, sizeof(scheme), host, sizeof(host), &port)) {
        size_t hlen = strlen(host);
        static const char suffix[] = ".local";
        is_local_broker = hlen > sizeof(suffix) - 1 && strcmp(host + hlen - (sizeof(suffix) - 1), suffix) == 0
                           && (strcasecmp(scheme, "mqtt") == 0 || strcasecmp(scheme, "mqtts") == 0);
    }

    if (is_local_broker) {
        // Bare mqtt(s):// broker on a ".local" mDNS name: resolve it ourselves
        // and hand esp-mqtt a plain IP/port/transport, bypassing its own URI
        // parsing (and the lwIP getaddrinfo path) entirely for this case.
        resolve_mdns_host(host, sizeof(host));
        cfg.broker.address.hostname = host;
        cfg.broker.address.port = port ? port : (strcasecmp(scheme, "mqtts") == 0 ? 8883 : 1883);
        cfg.broker.address.transport = strcasecmp(scheme, "mqtts") == 0 ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    } else {
        // Everything else (ws(s):// for the Render broker, non-mDNS mqtt(s)://
        // hosts) - let esp-mqtt's own URI parsing handle it as before.
        cfg.broker.address.uri = CONFIG_MAXHEADRON_MQTT_BROKER_URI;
    }

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
