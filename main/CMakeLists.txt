idf_component_register(SRCS "main.c" "gfx.c" "face.c" "glitch.c"
                    INCLUDE_DIRS "." "include"
                    PRIV_REQUIRES driver esp_lcd esp_timer)
