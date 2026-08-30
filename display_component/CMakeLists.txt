idf_component_register(
    SRCS "display_deps.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_spi
        esp_driver_ledc
        esp_lcd
        esp_lvgl_port
        lvgl
)
