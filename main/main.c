#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "counter.h"
#include "production_event.h"
#include "app_time.h"

#define SENSOR_GPIO GPIO_NUM_7
#define SENSOR_SAMPLE_MS 10

static const char *TAG = "CONTADOR";
static portMUX_TYPE sensor_lock = portMUX_INITIALIZER_UNLOCKED;
static bool sensor_activity;

// Não há fila de bordas: qualquer atividade invalida o tempo de estabilidade.
// A ISR não conta, não registra logs e não aloca memória.
static void sensor_isr_handler(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&sensor_lock);
    sensor_activity = true;
    portEXIT_CRITICAL_ISR(&sensor_lock);
}

static bool sensor_sample(bool *activity)
{
    portENTER_CRITICAL(&sensor_lock);
    *activity = sensor_activity;
    sensor_activity = false;
    const bool present = gpio_get_level(SENSOR_GPIO) == 0;
    portEXIT_CRITICAL(&sensor_lock);
    return present;
}

void app_main(void)
{
    // Falha temporal é degradável: nunca impedir a coleta por falta de UTC.
    const esp_err_t clock_status = app_time_init();
    if (clock_status != ESP_OK) {
        ESP_LOGW(TAG, "Relogio indisponivel (%d); eventos sem UTC", (int)clock_status);
    }
    ESP_ERROR_CHECK(production_events_init());
    const gpio_config_t sensor_config = {
        .pin_bit_mask = (1ULL << SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        // Requer pull-up externo adequado a 3V3; confirmar montagem no README.
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&sensor_config));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(SENSOR_GPIO, sensor_isr_handler, NULL));

    bool activity;
    const bool present = sensor_sample(&activity);
    counter_t counter;
    counter_init(&counter, present, esp_timer_get_time());
    const TickType_t period = pdMS_TO_TICKS(SENSOR_SAMPLE_MS) > 0
        ? pdMS_TO_TICKS(SENSOR_SAMPLE_MS) : 1;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "GPIO %d ativo em LOW. Aguardando liberacao estavel.", SENSOR_GPIO);
    ESP_LOGI(TAG, "Presenca=%" PRId64 " us; liberacao=%" PRId64
             " us; bloqueio=%" PRId64 " us; amostragem=%lu ticks",
             COUNTER_PRESENCE_US, COUNTER_RELEASE_US, COUNTER_BLOCKED_US,
             (unsigned long)period);

    // Coleta e geração de eventos pertencem exclusivamente à tarefa app_main.
    while (true) {
        // pdFALSE indica prazo já vencido; lacunas são tratadas pela máquina.
        (void)xTaskDelayUntil(&last_wake, period);
        const bool current_present = sensor_sample(&activity);
        const int64_t occurrence_us = esp_timer_get_time();
        const unsigned result = counter_update(&counter, current_present,
                                               activity, occurrence_us);
        if (result & COUNTER_COUNT) {
            // Mesmo instante da confirmação; nunca o horário de consumo da fila.
            const esp_err_t status = production_events_record(occurrence_us);
            if (status != ESP_ERR_NO_MEM) {
                ESP_ERROR_CHECK(status);
            }
            // Overflow já foi registrado e contabilizado; a coleta continua.
        }
        app_time_poll(occurrence_us);
        if (result & COUNTER_RESYNC) {
            ESP_LOGE(TAG, "Lacuna de amostragem: ciclo descartado; possivel perda. "
                     "Aguardando liberacao estavel para rearmar.");
        }
        if (result & COUNTER_READY) {
            ESP_LOGI(TAG, "Coleta pronta: sensor livre.");
        }
        if (result & COUNTER_BLOCKED) {
            ESP_LOGW(TAG, "Presenca prolongada: possivel sensor bloqueado.");
        }
        if (result & COUNTER_CLEARED) {
            ESP_LOGI(TAG, "Condicao de bloqueio encerrada: sensor liberado.");
        }
    }
}
