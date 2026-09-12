#include <assert.h>
#include <stdio.h>

#include "fake_network.h"
#include "communication/communication.h"
#include "communication/mqtt_manager.h"

static production_event_t queue[6];
static unsigned head, length, publications;
static bool connected;
static esp_err_t enqueue_status;
static uint64_t sent[6];

static void enqueue_valid_event(uint64_t sequence)
{
    assert(length < 6);

    queue[length++] = (production_event_t){
        .sequence = sequence,
        .timestamp_ms = INT64_C(1789167600000) + (int64_t)sequence,
        .clock_synced = 1,
    };
}

static void enqueue_unsynced_event(uint64_t sequence)
{
    assert(length < 6);

    queue[length++] = (production_event_t){
        .sequence = sequence,
        .timestamp_ms = 0,
        .clock_synced = 0,
    };
}

bool mqtt_manager_is_connected(void)
{
    return connected;
}

bool production_events_peek(production_event_t *event)
{
    if (head == length)
        return false;

    *event = queue[head];
    return true;
}

bool production_events_acknowledge(const production_event_t *event)
{
    assert(
        head < length &&
        event->sequence == queue[head].sequence
    );

    ++head;
    return true;
}

esp_err_t mqtt_manager_publish_event(
    const production_event_t *event
)
{
    assert(connected);

    if (enqueue_status != ESP_OK)
        return enqueue_status;

    assert(publications < 6);

    sent[publications++] = event->sequence;

    return ESP_OK;
}

esp_err_t production_events_claim_delivery(void)
{
    return ESP_OK;
}

BaseType_t xTaskCreate(
    void (*fn)(void *),
    const char *name,
    unsigned stack,
    void *arg,
    UBaseType_t priority,
    TaskHandle_t *handle
)
{
    (void)fn;
    (void)name;
    (void)stack;
    (void)arg;
    (void)priority;
    (void)handle;

    return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
}

void fake_log(
    const char *tag,
    const char *fmt,
    ...
)
{
    (void)tag;
    (void)fmt;
}

const char *esp_err_to_name(
    esp_err_t status
)
{
    (void)status;
    return "test";
}

static void poll(unsigned count)
{
    while (count--) {
        assert(
            communication_process_once() == ESP_OK
        );
    }
}

int main(void)
{
    /*
     * Sem eventos.
     */
    connected = true;

    poll(100);

    assert(publications == 0);


    /*
     * Evento válido conectado.
     */
    enqueue_valid_event(1);

    poll(100);

    assert(publications == 1);
    assert(head == 1);


    /*
     * Eventos enquanto offline devem permanecer
     * na FIFO.
     */
    connected = false;

    enqueue_valid_event(2);
    enqueue_valid_event(3);

    poll(100);

    assert(publications == 1);
    assert(head == 1);


    /*
     * Falha ao inserir no outbox MQTT também
     * deve preservar o evento.
     */
    connected = true;
    enqueue_status = ESP_ERR_NO_MEM;

    assert(
        communication_process_once() ==
        ESP_ERR_NO_MEM
    );

    assert(head == 1);
    assert(publications == 1);


    /*
     * Após o outbox voltar ao normal,
     * os eventos pendentes devem ser enviados.
     */
    enqueue_status = ESP_OK;

    poll(100);

    assert(publications == 3);

    assert(sent[0] == 1);
    assert(sent[1] == 2);
    assert(sent[2] == 3);


    /*
     * Reconectar não deve reenviar eventos
     * já removidos da FIFO.
     */
    connected = false;

    poll(10);

    connected = true;

    poll(100);

    assert(publications == 3);


    /*
     * Evento sem UTC válido deve ser descartado,
     * permitindo que o próximo evento válido
     * continue fluindo.
     */
    enqueue_unsynced_event(4);
    enqueue_valid_event(5);

    poll(100);

    assert(head == 5);

    assert(publications == 4);
    assert(sent[3] == 5);


    puts(
        "OK: comunicacao publica eventos validos; "
        "offline/falha preservam FIFO; "
        "evento sem UTC e descartado."
    );
}