/*
 * Max Headron - a generative, glitchy CG-talking-head animation for a
 * 128x64 (or 128x32) SSD1306 OLED, in the spirit of Max Headroom.
 *
 * Everything on screen is drawn procedurally each frame (no bitmap
 * assets): a scrolling background grid, a vector bust with sunglasses
 * that talks and bobs, and an on-top glitch layer that tears rows,
 * injects noise, flashes inverts, freezes frames and flickers a
 * stuttering text ticker. Optionally, phrases published over MQTT (see
 * mqtt.c) preempt the ticker to let a remote conversation "speak"
 * through it.
 */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ssd1306.h"
#include "driver/i2c_master.h"

#include "include/gfx.h"
#include "include/face.h"
#include "include/glitch.h"
#include "include/wifi.h"
#include "include/mqtt.h"

static const char *TAG = "max_headron";

#define LCD_H_RES 128
#if CONFIG_MAXHEADRON_OLED_HEIGHT_32
#define LCD_V_RES 32
#else
#define LCD_V_RES 64
#endif

#define I2C_BUS_PORT 0
#define I2C_CLOCK_HZ (400 * 1000)

static uint8_t s_framebuf[LCD_H_RES * LCD_V_RES / 8];

static esp_lcd_panel_handle_t init_display(void) {
    ESP_LOGI(TAG, "Initializing I2C bus (SDA=%d, SCL=%d)",
             CONFIG_MAXHEADRON_I2C_SDA_GPIO, CONFIG_MAXHEADRON_I2C_SCL_GPIO);
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = CONFIG_MAXHEADRON_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_MAXHEADRON_I2C_SCL_GPIO,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    // Wait here (instead of crashing) until something answers on the bus -
    // this is almost always a wiring/power issue, and it's friendlier to
    // patiently retry while the wires get fixed than to crash-reboot loop.
    int found = 0;
    while (found == 0) {
        ESP_LOGI(TAG, "Scanning I2C bus for devices...");
        for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
            if (i2c_master_probe(i2c_bus, addr, 50) == ESP_OK) {
                ESP_LOGI(TAG, "  found device at 0x%02X", addr);
                found++;
            }
        }
        if (found == 0) {
            ESP_LOGE(TAG, "  no I2C devices found on SDA=%d SCL=%d - check wiring/power/pull-ups, retrying in 2s",
                      CONFIG_MAXHEADRON_I2C_SDA_GPIO, CONFIG_MAXHEADRON_I2C_SCL_GPIO);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = CONFIG_MAXHEADRON_I2C_ADDRESS,
        .scl_speed_hz = I2C_CLOCK_HZ,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    return panel_handle;
}

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// wifi_connect_blocking() really does block (indefinitely, retrying) until
// a Wi-Fi connection succeeds. Run it off the main task so a slow/unreachable
// AP can't freeze the animation - MQTT is meant to be an add-on, not
// something the display's liveliness depends on.
static void network_setup_task(void *arg) {
    (void)arg;
    wifi_connect_blocking();
    mqtt_link_start();
    vTaskDelete(NULL);
}

void app_main(void) {
    esp_lcd_panel_handle_t panel = init_display();

    xTaskCreate(network_setup_task, "network_setup", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);

    gfx_t g;
    gfx_init(&g, s_framebuf, LCD_H_RES, LCD_V_RES);

    glitch_state_t glitch;
    uint32_t t = now_ms();
    glitch_init(&glitch, t);

    int mouth_open = 0;
    uint32_t next_mouth_change = t;
    bool glint_active = false;
    uint32_t glint_until = 0;
    uint32_t next_glint_at = t + 2000 + (esp_random() % 3000);

    ESP_LOGI(TAG, "Max Headron is alive. 20 minutes into the future...");

    while (1) {
        t = now_ms();
        glitch_update(&glitch, t);

        if (!glitch_is_frozen(&glitch)) {
            if (t >= next_mouth_change) {
                mouth_open = (int)(esp_random() % 5);
                next_mouth_change = t + 90 + (esp_random() % 180);
            }
            if (mqtt_consume_spark_request()) {
                glint_active = true;
                glint_until = t + 120 + (esp_random() % 150);
            } else if (!glint_active && t >= next_glint_at) {
                glint_active = true;
                glint_until = t + 120 + (esp_random() % 150);
            } else if (glint_active && t >= glint_until) {
                glint_active = false;
                next_glint_at = t + 2000 + (esp_random() % 3000);
            }

            float bob_phase = 2.0f * 3.14159265f * (float)(t % 6000) / 6000.0f;
            float sway_phase = 2.0f * 3.14159265f * (float)(t % 9000) / 9000.0f;

            face_pose_t pose = {
                .dx = 0,
                .dy = (int)(1.5f * sinf(bob_phase)),
                .shear = (int)(2.0f * sinf(sway_phase)),
                .mouth_open = mouth_open,
                .lens_glint = glint_active,
                .grid_scroll = (int)(t / 120),
            };
            glitch_pose_shove(&glitch, &pose.dx, &pose.dy, &pose.shear);

            face_draw(&g, &pose);
            glitch_apply_post(&glitch, &g);

            char external_msg[24];
            if (mqtt_get_external_message(external_msg, sizeof(external_msg), t)) {
                int len = (int)strlen(external_msg);
                int x = (LCD_H_RES - len * 8) / 2;
                gfx_draw_text(&g, x < 0 ? 0 : x, LCD_V_RES - 9, external_msg);
            } else {
                glitch_apply_ticker(&glitch, &g);
            }

            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, s_framebuf));
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_MAXHEADRON_FRAME_DELAY_MS));
    }
}
