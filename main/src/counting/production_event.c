#include "counting/production_event.h"
#include "time/app_time.h"

#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "bootloader_random.h"

_Static_assert(CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY >= 60,
               "A fila deve comportar pelo menos 60 eventos");
_Static_assert(sizeof(production_event_t) == 48, "Rever estimativa de RAM");

static const char *TAG = "PRODUCAO";
static QueueHandle_t event_queue;
static TaskHandle_t producer_task;
static TaskHandle_t delivery_task;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
// Escrita/leitura apenas pelo produtor, exceto sessão copiada dentro dos eventos.
static uint8_t session[PRODUCTION_SESSION_BYTES];
static uint64_t sequence;
static uint64_t dropped;

static void session_hex(const uint8_t *bytes, char text[33])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < PRODUCTION_SESSION_BYTES; ++i) {
        text[2 * i] = hex[bytes[i] >> 4];
        text[2 * i + 1] = hex[bytes[i] & 15];
    }
    text[32] = '\0';
}

bool production_events_diagnostic_receive(production_event_t *event,
                                          TickType_t wait_ticks)
{
    if (CONFIG_FLOWCOUNT_COMM_ENABLED || event_queue == NULL || event == NULL) {
        return false;
    }
    return xQueueReceive(event_queue, event, wait_ticks) == pdTRUE;
}

#if CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER && !CONFIG_FLOWCOUNT_COMM_ENABLED
static void diagnostic_consumer(void *arg)
{
    (void)arg;
    if (CONFIG_FLOWCOUNT_DIAGNOSTIC_DELAY_SECONDS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_FLOWCOUNT_DIAGNOSTIC_DELAY_SECONDS * 1000));
    }
    production_event_t event;
    for (;;) {
        if (!production_events_diagnostic_receive(&event, portMAX_DELAY)) {
            ESP_LOGE(TAG, "Diagnostico: falha ao receber; tentando novamente");
            vTaskDelay(1);
            continue;
        }
        char id[33];
        session_hex(event.session, id);
        ESP_LOGI(TAG, "DIAG removido (nao persistido): station=%" PRIu32
                 " session=%s sequence=%" PRIu64 " mono_us=%" PRId64
                 " timestamp_ms=%" PRId64 " clock_synced=%" PRIu32 " queue=%u/%u",
                 event.station, id, event.sequence, event.occurred_at_us,
             event.timestamp_ms, event.clock_synced, (unsigned)uxQueueMessagesWaiting(event_queue),
                 (unsigned)CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY);
    }
}
#endif

esp_err_t production_events_init(void)
{
    if (event_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    event_queue = xQueueCreate(CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY,
                               sizeof(production_event_t));
    if (event_queue == NULL) {
        ESP_LOGE(TAG, "Nao foi possivel criar fila de producao");
        return ESP_ERR_NO_MEM;
    }

    // Sem Wi-Fi, habilitar explicitamente entropia durante a geração da sessão.
    // Executado antes de qualquer uso de ADC/RF; desabilitar antes de prosseguir.
    bootloader_random_enable();
    esp_fill_random(session, sizeof(session));
    bootloader_random_disable();
    producer_task = xTaskGetCurrentTaskHandle();
    sequence = 0;
    dropped = 0;

#if CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER && !CONFIG_FLOWCOUNT_COMM_ENABLED
    if (xTaskCreate(diagnostic_consumer, "event_diag", 4096, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Nao foi possivel criar tarefa de diagnostico");
        vQueueDelete(event_queue);
        event_queue = NULL;
        producer_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "DIAGNOSTICO DESTRUTIVO habilitado: espera inicial=%d s",
             CONFIG_FLOWCOUNT_DIAGNOSTIC_DELAY_SECONDS);
#endif
    char id[33];
    session_hex(session, id);
    ESP_LOGI(TAG, "station=%u session=%s evento=%u bytes fila=%u eventos payload=%u bytes",
             (unsigned)CONFIG_FLOWCOUNT_STATION_ID, id,
             (unsigned)sizeof(production_event_t),
             (unsigned)CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY,
             (unsigned)(sizeof(production_event_t) * CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY));
    return ESP_OK;
}

esp_err_t production_events_record(int64_t occurred_at_us)
{
    if (event_queue == NULL || xTaskGetCurrentTaskHandle() != producer_task) {
        ESP_LOGE(TAG, "Producao fora da tarefa proprietaria ou antes de init");
        return ESP_ERR_INVALID_STATE;
    }
    if (occurred_at_us < 0) {
        ESP_LOGE(TAG, "Tempo monotônico invalido");
        return ESP_ERR_INVALID_ARG;
    }
    if (sequence == UINT64_MAX) {
        ESP_LOGE(TAG, "Sequencia esgotada; nao reutilizar identidade na sessao");
        return ESP_ERR_INVALID_STATE;
    }
    const app_time_stamp_t time = app_time_capture(occurred_at_us);
    production_event_t event = {
        .sequence = sequence + 1,
        .occurred_at_us = occurred_at_us,
        .station = CONFIG_FLOWCOUNT_STATION_ID,
        .timestamp_ms = time.timestamp_ms,
        .clock_synced = time.synced ? 1 : 0,
    };
    for (unsigned i = 0; i < PRODUCTION_SESSION_BYTES; ++i) {
        event.session[i] = session[i];
    }
    // O evento existe mesmo se não houver espaço. Não reutilizar sua identidade.
    portENTER_CRITICAL(&stats_lock);
    sequence = event.sequence;
    portEXIT_CRITICAL(&stats_lock);
    const bool stored = xQueueSend(event_queue, &event, 0) == pdTRUE;
    if (!stored) {
        portENTER_CRITICAL(&stats_lock);
        ++dropped;
        portEXIT_CRITICAL(&stats_lock);
    }
    char id[33];
    session_hex(event.session, id);
    const unsigned queued = (unsigned)uxQueueMessagesWaiting(event_queue);
    if (!stored) {
        ESP_LOGE(TAG, "OVERFLOW evento perdido: station=%" PRIu32
                 " session=%s sequence=%" PRIu64 " mono_us=%" PRId64
                 " timestamp_ms=%" PRId64 " clock_synced=%" PRIu32
                 " total=%" PRIu64 " queue=%u/%u perdidos=%" PRIu64,
                 event.station, id, event.sequence, event.occurred_at_us,
                 event.timestamp_ms, event.clock_synced, sequence,
                 queued, (unsigned)CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY, dropped);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Passagem valida/evento enfileirado: station=%" PRIu32
             " session=%s sequence=%" PRIu64 " mono_us=%" PRId64
             " timestamp_ms=%" PRId64 " clock_synced=%" PRIu32
             " total=%" PRIu64 " queue=%u/%u perdidos=%" PRIu64,
             event.station, id, event.sequence, event.occurred_at_us,
             event.timestamp_ms, event.clock_synced, sequence,
             queued, (unsigned)CONFIG_FLOWCOUNT_EVENT_QUEUE_CAPACITY, dropped);
    return ESP_OK;
}


esp_err_t production_events_claim_delivery(void)
{
    if (event_queue == NULL) return ESP_ERR_INVALID_STATE;
#if CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER && !CONFIG_FLOWCOUNT_COMM_ENABLED
    return ESP_ERR_INVALID_STATE;
#else
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&stats_lock);
    const bool allowed = delivery_task == NULL || delivery_task == caller;
    if (allowed) delivery_task = caller;
    portEXIT_CRITICAL(&stats_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
#endif
}

bool production_events_peek(production_event_t *event)
{
    if (event == NULL || event_queue == NULL ||
        xTaskGetCurrentTaskHandle() != delivery_task) return false;
    return xQueuePeek(event_queue, event, 0) == pdTRUE;
}

bool production_events_acknowledge(const production_event_t *identity)
{
    production_event_t head;
    if (identity == NULL || !production_events_peek(&head) ||
        head.station != identity->station || head.sequence != identity->sequence ||
        memcmp(head.session, identity->session, sizeof(head.session)) != 0) return false;
    // Só este consumidor remove; o produtor apenas acrescenta na cauda.
    return xQueueReceive(event_queue, &head, 0) == pdTRUE;
}

production_stats_t production_events_stats(void)
{
    production_stats_t stats = { .station = CONFIG_FLOWCOUNT_STATION_ID };
    portENTER_CRITICAL(&stats_lock);
    memcpy(stats.session, session, sizeof(session));
    stats.total = sequence;
    stats.lost = dropped;
    portEXIT_CRITICAL(&stats_lock);
    stats.pending = event_queue ? uxQueueMessagesWaiting(event_queue) : 0;
    return stats;
}
