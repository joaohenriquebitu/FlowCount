idf_component_register(SRCS "main.c" "counter.c" "production_event.c" "app_time.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_timer esp_hw_support bootloader_support esp_netif)
