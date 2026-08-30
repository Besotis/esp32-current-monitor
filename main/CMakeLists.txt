set(COMMON_SRCS
    "main.c"
    "espnow_comm.c"
)

set(ROLE_SRCS "")
set(ROLE_INCLUDE_DIRS ".")

if(CONFIG_DEVICE_ROLE_SENSOR)

    list(APPEND ROLE_SRCS
        "role_sensor.c"
        "current_sensor.c"
    )

elseif(CONFIG_DEVICE_ROLE_DISPLAY)

    list(APPEND ROLE_SRCS
        "role_display.c"
        "battery_monitor.c"
        "mode_button.c"
        "display_st7789.c"
        "display_ui.c"

        "ui/ui.c"
        "ui/ui_helpers.c"
        "ui/components/ui_comp_hook.c"
        "ui/screens/ui_Screen1.c"
    )

    list(APPEND ROLE_INCLUDE_DIRS
        "ui"
        "ui/screens"
        "ui/components"
    )

endif()

idf_component_register(
    SRCS
        ${COMMON_SRCS}
        ${ROLE_SRCS}

    INCLUDE_DIRS
        ${ROLE_INCLUDE_DIRS}

    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_timer
        esp_adc

    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_ledc
        esp_lcd
        esp_lvgl_port
        lvgl
)