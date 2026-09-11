#ifndef FLOWCOUNT_COMMUNICATION_H
#define FLOWCOUNT_COMMUNICATION_H

#include "esp_err.h"

esp_err_t communication_init(void);
// Uma iteração, executada exclusivamente pelo dono da fila de entrega.
// Mantém o evento se offline/erro; remove após aceitação pelo outbox MQTT.
esp_err_t communication_process_once(void);

#endif