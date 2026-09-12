#ifndef FLOWCOUNT_OLED_H
#define FLOWCOUNT_OLED_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t oled_init(void);

void oled_update(
    bool wifi_connected,
    const char *ip,
    bool mqtt_connected,
    uint64_t total
);

#endif
