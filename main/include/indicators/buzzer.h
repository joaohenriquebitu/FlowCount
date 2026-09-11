#ifndef FLOWCOUNT_BUZZER_H
#define FLOWCOUNT_BUZZER_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    BUZZER_CONNECTION_WIFI,
    BUZZER_CONNECTION_MQTT,
    BUZZER_CONNECTION_COUNT
} buzzer_connection_t;

// Chamar no boot, em app_main, antes de iniciar Wi-Fi/MQTT. Não em ISR.
esp_err_t buzzer_init(void);
// Enfileira beep de 60 ms; não espera o som. Fila cheia retorna ESP_ERR_NO_MEM.
esp_err_t buzzer_beep(void);
// Notificação dos callbacks de rede, sem bloquear. Boot offline não é queda.
void buzzer_connection_changed(buzzer_connection_t connection, bool connected);

#endif
