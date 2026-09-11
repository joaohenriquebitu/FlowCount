// Executa o módulo real com fila/RNG/tarefas simuladas. Não testa o escalonador.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_esp_idf.h"
#include "counting/production_event.h"
#include "counting/counter.h"
#include "time/app_time.h"
#include "fake_time.h"

struct fake_queue {
    size_t item_size;
    unsigned length, count, head;
    unsigned char data[72 * sizeof(production_event_t)];
};

static QueueHandle_t active_queue;
static bool fail_queue, fail_task, entropy_enabled;
static unsigned rng_calls, deleted, overflow_logs, enqueued_logs, task_calls;
static char last_production_log[512];
static int owner, other;
static TaskHandle_t current_task = &owner;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    assert(length == 72 && item_size == sizeof(production_event_t));
    if (fail_queue) return NULL;
    active_queue = calloc(1, sizeof(*active_queue));
    assert(active_queue);
    active_queue->length = length;
    active_queue->item_size = item_size;
    return active_queue;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    assert(wait == 0); // O produtor nunca pode esperar espaço.
    if (q->count == q->length) return pdFALSE;
    memcpy(q->data + ((q->head + q->count) % q->length) * q->item_size,
           item, q->item_size);
    ++q->count;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    (void)wait;
    if (q->count == 0) return pdFALSE;
    memcpy(item, q->data + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->length;
    --q->count;
    return pdTRUE;
}

BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t wait)
{
    (void)wait;

    if (q->count == 0) {
        return pdFALSE;
    }

    memcpy(
        item,
        q->data + q->head * q->item_size,
        q->item_size
    );

    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }

void vQueueDelete(QueueHandle_t q) { free(q); active_queue = NULL; ++deleted; }

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *arg, UBaseType_t priority, TaskHandle_t *handle)
{
    assert(task && name && stack == 4096 && arg == NULL && priority == 1);
    (void)handle;
    ++task_calls;
    // Não iniciar o loop infinito: consumo é controlado explicitamente no teste.
    return fail_task ? pdFALSE : pdPASS;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

void bootloader_random_enable(void) { assert(!entropy_enabled); entropy_enabled = true; }

void bootloader_random_disable(void) { assert(entropy_enabled); entropy_enabled = false; }

void esp_fill_random(void *buffer, size_t size)
{
    assert(entropy_enabled && size == PRODUCTION_SESSION_BYTES);
    ++rng_calls;
    memset(buffer, (int)rng_calls, size);
}

void fake_log(const char *tag, const char *format, ...)
{
    (void)tag;
    char text[512];
    va_list args;
    va_start(args, format);
    const int size = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    assert(size >= 0 && (size_t)size < sizeof(text));
    if (strstr(text, "OVERFLOW")) ++overflow_logs;
    if (strstr(text, "evento enfileirado")) ++enqueued_logs;
    if (strstr(text, "OVERFLOW") || strstr(text, "evento enfileirado")) {
        memcpy(last_production_log, text, (size_t)size + 1);
    }
}

static counter_t counter;
static int64_t now;
static unsigned valid_passages;
static void sample(bool present, bool edge, unsigned duration_ms)
{
    for (unsigned i = 0; i < duration_ms; i += 10) {
        now += 10000;
        fake_mono_us = now;
        unsigned result = counter_update(&counter, present, edge && i == 0, now);
        if (result & COUNTER_COUNT) {
            ++valid_passages;
            esp_err_t expected = valid_passages <= 72 ? ESP_OK : ESP_ERR_NO_MEM;
            assert(production_events_record(now) == expected);
        }
    }
}

// Exercita a fila pelo caminho público de entrega quando a comunicação está ativa.
static bool receive_event(production_event_t *event, TickType_t wait)
{
#if CONFIG_FLOWCOUNT_COMM_ENABLED
    (void)wait;
    if (!production_events_peek(event)) return false;
    if (!production_events_acknowledge(event)) return false;
    // Um ACK duplicado não pode remover o próximo evento da fila.
    assert(!production_events_acknowledge(event));
    return true;
#else
    return production_events_diagnostic_receive(event, wait);
#endif
}

static void test_delivery_ownership(void)
{
    production_event_t event;
    assert(!production_events_peek(&event));
    assert(!production_events_acknowledge(&event));
#if CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER
    assert(production_events_claim_delivery() == ESP_ERR_INVALID_STATE);
#else
    assert(production_events_claim_delivery() == ESP_OK);
    assert(production_events_claim_delivery() == ESP_OK);
    current_task = &other;
    assert(production_events_claim_delivery() == ESP_ERR_INVALID_STATE);
    assert(!production_events_peek(&event));
    assert(!production_events_acknowledge(&event));
    current_task = &owner;
    assert(!production_events_peek(NULL));
    assert(!production_events_acknowledge(NULL));
#endif
}

static void test_ack_retains_head(void)
{
#if !CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER
    production_event_t head, again;
    const unsigned pending = active_queue->count;
    assert(production_events_peek(&head));
    assert(production_events_peek(&again));
    assert(memcmp(&head, &again, sizeof(head)) == 0);
    assert(active_queue->count == pending);
    ++again.sequence;
    assert(!production_events_acknowledge(&again));
    again = head;
    ++again.station;
    assert(!production_events_acknowledge(&again));
    again = head;
    ++again.session[0];
    assert(!production_events_acknowledge(&again));
    assert(active_queue->count == pending);
#if CONFIG_FLOWCOUNT_COMM_ENABLED
    assert(!production_events_diagnostic_receive(&again, 0));
    assert(active_queue->count == pending);
#endif
#endif
}

int main(void)
{
    production_event_t event;
    assert(app_time_init() == ESP_OK);
    assert(production_events_record(1) == ESP_ERR_INVALID_STATE);
    assert(production_events_claim_delivery() == ESP_ERR_INVALID_STATE);
    assert(!receive_event(&event, 0));
    fail_queue = true;
    assert(production_events_init() == ESP_ERR_NO_MEM);
    assert(rng_calls == 0);
    fail_queue = false;
#if CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER
    fail_task = true;
    assert(production_events_init() == ESP_ERR_NO_MEM);
    assert(deleted == 1 && active_queue == NULL && !entropy_enabled);
    fail_task = false;
#endif
    assert(production_events_init() == ESP_OK);
    assert(!entropy_enabled);
    assert(production_events_init() == ESP_ERR_INVALID_STATE);
    assert(!production_events_diagnostic_receive(NULL, 0));
    assert(production_events_record(-1) == ESP_ERR_INVALID_ARG);
    current_task = &other;
    assert(production_events_record(1) == ESP_ERR_INVALID_STATE);
    current_task = &owner;

    test_delivery_ownership();
    counter_init(&counter, false, 0);
    sample(false, false, 50);
    // Ruído não produz evento nem sequência.
    for (unsigned i = 0; i < 10; ++i) {
        sample(true, true, 10);
        sample(false, true, 100);
    }
    assert(active_queue->count == 0);
    // Peça parada não produz evento antes da liberação confirmada.
    sample(true, true, 6000);
    assert(active_queue->count == 0);
    sample(false, true, 70);
    const int64_t first_time = now - 10000;
    assert(valid_passages == 1 && active_queue->count == 1);
    // O primeiro evento fica sem UTC mesmo após sincronizar e consumir mais tarde.
    assert(app_time_start_sntp() == ESP_OK);
    const int64_t sync_mono = now;
    const int64_t epoch_ms = INT64_C(1700000000123);
    fake_time_sync(epoch_ms);
    for (unsigned i = 1; i < 74; ++i) {
        sample(true, true, 100);
        sample(false, true, 100);
    }
    assert(valid_passages == 74 && active_queue->count == 72);
    assert(overflow_logs == 2 && enqueued_logs == 72);
    assert(strstr(last_production_log, "total=74 queue=72/72 perdidos=2"));
    // Avançar 30 s e corrigir o relógio antes de consumir: nada na fila muda.
    fake_mono_us = now + 30000000;
    fake_time_sync(epoch_ms + 500000);
    test_ack_retains_head();
    production_stats_t stats = production_events_stats();
    assert(stats.total == 74 && stats.lost == 2 && stats.pending == 72);
    int64_t previous_time = -1;
    for (unsigned i = 1; i <= 72; ++i) {
        assert(receive_event(&event, 0));
        assert(event.sequence == i && event.station == 7);
        if (i == 1) {
            assert(event.clock_synced == 0 && event.timestamp_ms == 0);
        } else {
            assert(event.clock_synced == 1);
            assert(event.timestamp_ms == epoch_ms + (event.occurred_at_us - sync_mono) / 1000);
        }
        assert(event.occurred_at_us > previous_time);
        if (i == 1) assert(event.occurred_at_us == first_time);
        previous_time = event.occurred_at_us;
        for (unsigned b = 0; b < PRODUCTION_SESSION_BYTES; ++b) {
            assert(event.session[b] == rng_calls);
        }
    }
    assert(!receive_event(&event, 0));
    // Depois de liberar espaço, a sequência não reutiliza as identidades perdidas.
    now = fake_mono_us;
    assert(production_events_record(now + 10000) == ESP_OK);
    assert(strstr(last_production_log, "total=75 queue=1/72 perdidos=2"));
    assert(receive_event(&event, 0));
    assert(event.sequence == 75 && event.occurred_at_us == now + 10000);
    assert(event.clock_synced == 1 && event.timestamp_ms == epoch_ms + 500010);
    // Repetir ocupação/consumo evidencia cópia por valor, ordem e reutilização RAM.
    for (unsigned i = 76; i < 1076; ++i) {
        assert(production_events_record(now + i * INT64_C(10000)) == ESP_OK);
        assert(receive_event(&event, 0));
        assert(event.sequence == i);
    }
    stats = production_events_stats();
    assert(stats.total == 1075 && stats.lost == 2 && stats.pending == 0);
    free(active_queue);
    printf("OK: integracao contador/eventos; FIFO 72; 2 overflows; timestamps; "
           "retomada seq=75; 1000 ciclos; falhas init; consumidor=%d comunicacao=%d; ownership/ACK/stats\n",
           CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER, CONFIG_FLOWCOUNT_COMM_ENABLED);
    return 0;
}
