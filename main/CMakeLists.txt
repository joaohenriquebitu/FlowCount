idf_component_register(
    SRCS
        "src/main.c"
        "src/counting/counter.c"
        "src/counting/production_event.c"
        "src/time/app_time.c"
        "src/communication/wifi_manager.c"
        "src/communication/mqtt_manager.c"
        "src/communication/mqtt_protocol.c"
        "src/indicators/buzzer.c"
        "src/indicators/leds.c"
        "src/indicators/oled.c"
        "src/communication/communication.c"

    INCLUDE_DIRS "include"

    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_i2c
        esp_lcd
        esp_timer
        esp_hw_support
        bootloader_support
        esp_netif
        esp_wifi
        esp_event
        nvs_flash
        json
        mqtt
)
