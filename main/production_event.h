#ifndef FLOWCOUNT_PRODUCTION_EVENT_H
#define FLOWCOUNT_PRODUCTION_EVENT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define PRODUCTION_SESSION_BYTES 16

// Ordem evita padding desnecessário. Não é um formato de transporte binário.
// occurred_at_us corresponde à confirmação da liberação, antes de enfileirar.
typedef struct {
    uint64_t sequence;
    int64_t occurred_at_us; // Monotônico desde boot, preservado para diagnóstico.
    int64_t timestamp_ms;   // Unix UTC em ms; 0 quando clock_synced=0.
    uint8_t session[PRODUCTION_SESSION_BYTES];
    uint32_t station;
    uint32_t clock_synced; // 0 ou 1: qualidade temporal na criação, imutável.
} production_event_t;

// Chamar uma única vez em app_main, antes de iniciar ADC/RF.
// Cria sessão e fila; opcionalmente inicia o consumidor de diagnóstico.
esp_err_t production_events_init(void);

// Somente a tarefa que chamou init pode produzir. Contagem local = sequência.
// ESP_OK: evento copiado para fila. ESP_ERR_NO_MEM: evento criado, contado,
// perdido por overflow e registrado no log. Outros erros: falha de programação
// ou esgotamento da sequência; nenhum evento criado. Nunca aguarda espaço.
esp_err_t production_events_record(int64_t occurred_at_us);

// Consumo DESTRUTIVO, exclusivo de teste nesta etapa, com um único consumidor.
// Sucesso remove o evento imediatamente, sem transmissão ou persistência.
// Retorna false se não há evento no prazo ou se os argumentos são inválidos.
bool production_events_diagnostic_receive(production_event_t *event,
                                          TickType_t wait_ticks);
// Um único consumidor de entrega por execução. Peek não remove nada.
esp_err_t production_events_claim_delivery(void);
bool production_events_peek(production_event_t *event);
bool production_events_acknowledge(const production_event_t *identity);
typedef struct {
    uint8_t session[16];
    uint32_t station;
    uint64_t total;
    uint64_t lost;
    uint32_t pending;
} production_stats_t;
production_stats_t production_events_stats(void);
#endif
