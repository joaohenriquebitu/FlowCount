#include "communication.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "mqtt_manager.h"
#include "production_event.h"


#define COMMUNICATION_TASK_STACK 4096
#define COMMUNICATION_TASK_DELAY_MS 100


static const char *TAG = "COMM";


static void communication_task(void *arg)
{
    (void)arg;

    /*
     * Esta tarefa passa a ser a única autorizada
     * a trabalhar com a cabeça da fila.
     */
    ESP_ERROR_CHECK(
        production_events_claim_delivery()
    );


    while (true) {

        /*
         * Sem MQTT:
         *
         * não remove nada;
         * não bloqueia coleta;
         * apenas espera.
         */
        if (!mqtt_manager_is_connected()) {

            vTaskDelay(
                pdMS_TO_TICKS(
                    COMMUNICATION_TASK_DELAY_MS
                )
            );

            continue;
        }


        production_event_t event;


        /*
         * Peek:
         *
         * lê o primeiro evento da fila
         * SEM removê-lo.
         */
        if (!production_events_peek(&event)) {

            vTaskDelay(
                pdMS_TO_TICKS(
                    COMMUNICATION_TASK_DELAY_MS
                )
            );

            continue;
        }


        ESP_LOGI(
            TAG,
            "Tentando enviar evento seq=%" PRIu64,
            event.sequence
        );


        const esp_err_t status =
            mqtt_manager_publish_event(
                &event
            );


        if (status == ESP_OK) {

            ESP_LOGI(
                TAG,
                "Evento enviado ao broker seq=%" PRIu64,
                event.sequence
            );

        } else {

            ESP_LOGW(
                TAG,
                "Falha ao enviar seq=%" PRIu64 ": %s",
                event.sequence,
                esp_err_to_name(status)
            );
        }


        /*
         * Ainda NÃO removemos o evento.
         *
         * Isso será feito quando implementarmos
         * a confirmação de entrega.
         */
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}


esp_err_t communication_init(void)
{
    if (xTaskCreate(
            communication_task,
            "flowcount_comm",
            COMMUNICATION_TASK_STACK,
            NULL,
            tskIDLE_PRIORITY + 2,
            NULL
        ) != pdPASS) {

        ESP_LOGE(
            TAG,
            "Falha ao criar tarefa de comunicacao"
        );

        return ESP_ERR_NO_MEM;
    }


    ESP_LOGI(
        TAG,
        "Tarefa de comunicacao criada"
    );


    return ESP_OK;
}