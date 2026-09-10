#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define SENSOR_GPIO GPIO_NUM_7

static const char *TAG = "CONTADOR";

static QueueHandle_t sensor_queue;

// Interrupção chamada quando o sensor detecta um objeto
static void IRAM_ATTR sensor_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;

    BaseType_t higher_priority_task_woken = pdFALSE;

    xQueueSendFromISR(
        sensor_queue,
        &gpio_num,
        &higher_priority_task_woken
    );

    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

void app_main(void)
{
    uint32_t gpio_num;

    uint32_t contador = 0;

    // Usado para evitar contar o mesmo objeto várias vezes
    int64_t ultimo_evento = 0;

    sensor_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_config_t sensor_config = {
        .pin_bit_mask = (1ULL << SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,

        // Pull-up interno do ESP32.
        // Quando colocar resistor externo de 10k para 3V3,
        // pode mudar para GPIO_PULLUP_DISABLE.
        .pull_up_en = GPIO_PULLUP_DISABLE,

        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        // Sensor NPN leva o sinal de HIGH para LOW
        .intr_type = GPIO_INTR_NEGEDGE
    };

    gpio_config(&sensor_config);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(
        SENSOR_GPIO,
        sensor_isr_handler,
        (void *)(uintptr_t)SENSOR_GPIO
    );

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "Contador iniciado");
    ESP_LOGI(TAG, "Sensor conectado no GPIO %d", SENSOR_GPIO);
    ESP_LOGI(TAG, "Passe um objeto na frente do sensor");
    ESP_LOGI(TAG, "================================");

    while (1) {

        if (xQueueReceive(sensor_queue,
                          &gpio_num,
                          portMAX_DELAY)) {

            int64_t agora = esp_timer_get_time();

            /*
             * 100 ms de proteção contra múltiplas contagens.
             * esp_timer_get_time() retorna microssegundos.
             */
            if ((agora - ultimo_evento) > 100000) {

                contador++;

                ultimo_evento = agora;

                ESP_LOGI(
                    TAG,
                    "Objeto detectado! Total = %lu",
                    (unsigned long)contador
                );
            }
        }
    }
}