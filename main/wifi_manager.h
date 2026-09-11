#ifndef FLOWCOUNT_WIFI_MANAGER_H
#define FLOWCOUNT_WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t wifi_manager_init(void);
bool wifi_manager_is_connected(void);

#endif