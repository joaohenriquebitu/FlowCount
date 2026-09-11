idf_component_register(
    SRCS
        "main.c"
        "counter.c"
        "production_event.c"
        "app_time.c"
        "wifi_manager.c"
        "mqtt_manager.c"
        "mqtt_protocol.c"
        "buzzer.c"
        "communication.c"

    INCLUDE_DIRS "."

    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_ledc
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