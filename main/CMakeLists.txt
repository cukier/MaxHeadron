idf_component_register(SRCS "main.c" "gfx.c" "face.c" "glitch.c" "wifi.c" "eth.c" "mqtt.c"
                    INCLUDE_DIRS "." "include"
                    PRIV_REQUIRES driver esp_lcd esp_timer esp_wifi esp_event nvs_flash esp-tls mdns esp_eth)
