#include "communication/communication.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "communication/mqtt_manager.h"
#include "counting/production_event.h"

static const char *TAG = "COMM";

esp_err_t communication_process_once(void)
{
    if (!mqtt_manager_is_connected())
        return ESP_OK;

    production_event_t event;

    if (!production_events_peek(&event))
        return ESP_OK;

    /*
     * Evento criado antes da sincronização SNTP.
     * Não pode ser enviado porque não possui UTC válido.
     * Removê-lo evita bloquear toda a FIFO.
     */
    if (!event.clock_synced || event.timestamp_ms <= 0) {
        ESP_LOGW(
            TAG,
            "Descartando evento sem UTC valido: sequence=%llu",
            (unsigned long long)event.sequence
        );

        if (!production_events_acknowledge(&event))
            return ESP_ERR_INVALID_STATE;

        return ESP_OK;
    }

    const esp_err_t status =
        mqtt_manager_publish_event(&event);

    if (status != ESP_OK)
        return status;

    if (!production_events_acknowledge(&event))
        return ESP_ERR_INVALID_STATE;

    return ESP_OK;
}

static void communication_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(production_events_claim_delivery());
    esp_err_t previous_status = ESP_OK;
    while (true) {
        const esp_err_t status = communication_process_once();
        if (status != ESP_OK && status != previous_status) {
            ESP_LOGW(TAG, "Evento mantido na fila: %s", esp_err_to_name(status));
        }
        previous_status = status;
        vTaskDelay(pdMS_TO_TICKS(status == ESP_OK ? 100 : 1000));
    }
}

esp_err_t communication_init(void)
{
    if (xTaskCreate(communication_task, "flowcount_comm", 4096, NULL,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Tarefa de comunicacao criada");
    return ESP_OK;
}
