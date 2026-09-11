#include <assert.h>
#include <stdio.h>
#include "fake_network.h"
#include "communication/communication.h"
#include "communication/mqtt_manager.h"

static production_event_t queue[4];
static unsigned head, length, publications;
static bool connected;
static esp_err_t enqueue_status;
static uint64_t sent[4];

bool mqtt_manager_is_connected(void) { return connected; }
bool production_events_peek(production_event_t *event)
{
    if (head == length) return false;
    *event = queue[head];
    return true;
}
bool production_events_acknowledge(const production_event_t *event)
{
    assert(head < length && event->sequence == queue[head].sequence);
    ++head;
    return true;
}
esp_err_t mqtt_manager_publish_event(const production_event_t *event)
{
    assert(connected);
    if (enqueue_status != ESP_OK) return enqueue_status;
    assert(publications < 4);
    sent[publications++] = event->sequence;
    return ESP_OK;
}
esp_err_t production_events_claim_delivery(void) { return ESP_OK; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
    void *arg, UBaseType_t priority, TaskHandle_t *handle)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return pdPASS; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void fake_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
const char *esp_err_to_name(esp_err_t status) { (void)status; return "test"; }
static void poll(unsigned count)
{
    while (count--) assert(communication_process_once() == ESP_OK);
}
int main(void)
{
    connected = true;
    poll(100);
    assert(publications == 0); // Sem contagem não publica.
    queue[length++].sequence = 1;
    poll(100);
    assert(publications == 1 && head == 1); // Regressão: não repete a cabeça.
    connected = false;
    queue[length++].sequence = 2;
    queue[length++].sequence = 3;
    poll(100);
    assert(publications == 1 && head == 1);
    connected = true;
    enqueue_status = ESP_ERR_NO_MEM;
    assert(communication_process_once() == ESP_ERR_NO_MEM);
    assert(head == 1 && publications == 1); // Outbox cheio não perde a contagem.
    enqueue_status = ESP_OK;
    poll(100);
    assert(publications == 3 && sent[0] == 1 && sent[1] == 2 && sent[2] == 3);
    connected = false;
    poll(10);
    connected = true;
    poll(100);
    assert(publications == 3); // Reconexão não reenvia o que já foi transferido.
    puts("OK: comunicacao publica uma vez por contagem; offline/falha preservam FIFO.");
}
