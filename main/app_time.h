#ifndef FLOWCOUNT_APP_TIME_H
#define FLOWCOUNT_APP_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int64_t timestamp_ms; // Unix UTC em ms; 0 quando synced=false.
    bool synced;
} app_time_stamp_t;

// Boot, antes de iniciar outras tarefas. Não inicializa rede nem espera SNTP.
esp_err_t app_time_init(void);
// Integração futura: chamar em contexto de tarefa após esp_netif_init e IP válido.
// Idempotente após sucesso. Falhas permitem nova tentativa pelo gestor de rede.
esp_err_t app_time_start_sntp(void);
// Somente leitura, thread-safe. Nunca consulta rede nem altera eventos existentes.
app_time_stamp_t app_time_capture(int64_t occurrence_mono_us);
bool app_time_is_synchronized(void);
// Diagnóstico periódico não bloqueante; chamar somente pela tarefa de coleta.
void app_time_poll(int64_t now_us);
#endif
