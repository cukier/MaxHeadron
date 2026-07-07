/*
 * Max Headron - a generative animation for a 128x64 (or 128x32) SSD1306
 * OLED: HAL 9000's lens. A single unblinking, concentric-ringed eye in a
 * recessed panel, almost perfectly still - a slow breathing dilation and
 * an occasional glint are the only idle motion. Everything is drawn
 * procedurally each frame (no bitmap assets). An on-top glitch layer
 * tears rows, injects noise, flashes inverts, freezes frames and
 * occasionally has it speak a line - all read as rare malfunctions
 * against the otherwise calm stillness, not constant chatter.
 */
#include <math.h>
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

void app_main(void) {
    esp_lcd_panel_handle_t panel = init_display();

    gfx_t g;
    gfx_init(&g, s_framebuf, LCD_H_RES, LCD_V_RES);

    glitch_state_t glitch;
    uint32_t t = now_ms();
    glitch_init(&glitch, t);

    bool spark_active = false;
    uint32_t spark_until = 0;
    uint32_t next_spark_at = t + 4000 + (esp_random() % 5000);

    ESP_LOGI(TAG, "Max Headron is alive. Good morning.");

    while (1) {
        t = now_ms();
        glitch_update(&glitch, t);

        if (!glitch_is_frozen(&glitch)) {
            if (!spark_active && t >= next_spark_at) {
                spark_active = true;
                spark_until = t + 150 + (esp_random() % 150);
            } else if (spark_active && t >= spark_until) {
                spark_active = false;
                next_spark_at = t + 4000 + (esp_random() % 5000);
            }

            // The only idle motion: a slow, shallow breathing dilation.
            // No spin, no drift - HAL does not fidget.
            float breathe_phase = 2.0f * 3.14159265f * (float)(t % 8000) / 8000.0f;

            face_pose_t pose = {
                .dx = 0,
                .dy = 0,
                .pulse = (int)(1.5f * sinf(breathe_phase)),
                .spark = spark_active,
            };
            glitch_pose_shove(&glitch, &pose.dx, &pose.dy);

            face_draw(&g, &pose);
            glitch_apply_post(&glitch, &g);
            glitch_apply_ticker(&glitch, &g);

            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, s_framebuf));
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_MAXHEADRON_FRAME_DELAY_MS));
    }
}
