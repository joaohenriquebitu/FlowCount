#ifndef FLOWCOUNT_MQTT_MANAGER_H
#define FLOWCOUNT_MQTT_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"
#include "production_event.h"

esp_err_t mqtt_manager_publish_event(
    const production_event_t *event
);
esp_err_t mqtt_manager_init(void);
esp_err_t mqtt_manager_start(void);

bool mqtt_manager_is_connected(void);


#endif