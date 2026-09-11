#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "sdkconfig.h"

#include "counter.h"
#include "production_event.h"
#include "app_time.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "buzzer.h"
#include "communication.h"


#define SENSOR_GPIO GPIO_NUM_7
#define SENSOR_SAMPLE_MS 10


static const char *TAG = "CONTADOR";

static portMUX_TYPE sensor_lock =
    portMUX_INITIALIZER_UNLOCKED;

static bool sensor_activity;


/*
 * Não há fila de bordas:
 * qualquer atividade invalida o tempo de estabilidade.
 *
 * A ISR não conta, não registra logs
 * e não realiza alocação de memória.
 */
static void sensor_isr_handler(void *arg)
{
    (void)arg;

    portENTER_CRITICAL_ISR(&sensor_lock);

    sensor_activity = true;

    portEXIT_CRITICAL_ISR(&sensor_lock);
}


/*
 * Obtém:
 *
 * 1. o nível atual do sensor;
 * 2. se houve alguma atividade desde a última amostragem.
 *
 * O sensor é ativo em LOW:
 *
 * GPIO = 0 -> peça presente
 * GPIO = 1 -> sensor livre
 */
static bool sensor_sample(bool *activity)
{
    portENTER_CRITICAL(&sensor_lock);

    *activity = sensor_activity;

    sensor_activity = false;

    const bool present =
        gpio_get_level(SENSOR_GPIO) == 0;

    portEXIT_CRITICAL(&sensor_lock);

    return present;
}


void app_main(void)
{
    /*
     * ============================================================
     * RELÓGIO
     * ============================================================
     *
     * Falha temporal é degradável:
     * ausência de UTC nunca deve impedir a coleta.
     */
    const esp_err_t clock_status =
        app_time_init();

    if (clock_status != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Relogio indisponivel (%d); eventos sem UTC",
            (int)clock_status
        );
    }

    const esp_err_t buzzer_status =
        buzzer_init();

    if (buzzer_status != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Buzzer indisponivel: %s",
            esp_err_to_name(buzzer_status)
        );
    } else {
        ESP_LOGI(
            TAG,
            "TESTE BUZZER: inicializacao OK"
        );

        const esp_err_t test_status =
            buzzer_beep();

        ESP_LOGI(
            TAG,
            "TESTE BUZZER: buzzer_beep() retornou %s",
            esp_err_to_name(test_status)
        );
    }


    /*
     * ============================================================
     * EVENTOS DE PRODUÇÃO
     * ============================================================
     *
     * Inicializamos a sessão/fila antes de ligar o Wi-Fi.
     */
    ESP_ERROR_CHECK(
        production_events_init()
    );


    /*
     * ============================================================
     * SENSOR
     * ============================================================
     */
    const gpio_config_t sensor_config = {
        .pin_bit_mask = (1ULL << SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,

        /*
         * Requer pull-up externo adequado a 3V3.
         * Confirmar montagem no README.
         */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        /*
         * Precisamos observar entrada e saída da peça.
         */
        .intr_type = GPIO_INTR_ANYEDGE,
    };


    ESP_ERROR_CHECK(
        gpio_config(&sensor_config)
    );

    ESP_ERROR_CHECK(
        gpio_install_isr_service(0)
    );

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            SENSOR_GPIO,
            sensor_isr_handler,
            NULL
        )
    );


    /*
     * ============================================================
     * WI-FI
     * ============================================================
     *
     * wifi_manager_init() inicia a infraestrutura,
     * mas não bloqueia esperando obter IP.
     *
     * Se houver falha de comunicação, a coleta continua.
     */
#if CONFIG_FLOWCOUNT_COMM_ENABLED

    const esp_err_t wifi_status =
        wifi_manager_init();

    if (wifi_status != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao inicializar Wi-Fi: %s",
            esp_err_to_name(wifi_status)
        );
    }

#endif

#if CONFIG_FLOWCOUNT_COMM_ENABLED

    const esp_err_t mqtt_status =
        mqtt_manager_init();

    if (mqtt_status != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Falha ao inicializar MQTT: %s",
            esp_err_to_name(mqtt_status)
        );
    }

#endif

#if CONFIG_FLOWCOUNT_COMM_ENABLED

    const esp_err_t comm_status =
        communication_init();

    if (comm_status != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Falha ao iniciar tarefa de comunicacao: %s",
            esp_err_to_name(comm_status)
        );
    }

#endif


    /*
     * ============================================================
     * CONTADOR
     * ============================================================
     *
     * Inicializamos a máquina de estados depois das
     * inicializações mais demoradas.
     *
     * Isso evita que o primeiro counter_update()
     * veja uma grande lacuna temporal causada pelo boot
     * da infraestrutura Wi-Fi.
     */
    bool activity;

    const bool present =
        sensor_sample(&activity);

    counter_t counter;

    counter_init(
        &counter,
        present,
        esp_timer_get_time()
    );


    /*
     * ============================================================
     * CONTROLE DO SNTP
     * ============================================================
     *
     * sntp_started:
     *      indica que a infraestrutura SNTP foi iniciada.
     *
     * Não significa necessariamente que o relógio já está
     * sincronizado.
     *
     * next_sntp_attempt_us:
     *      impede tentativas agressivas caso a inicialização falhe.
     */
    bool sntp_started = false;

    int64_t next_sntp_attempt_us = 0;

    bool previous_clock_state = false;


    /*
     * ============================================================
     * PERÍODO DE AMOSTRAGEM
     * ============================================================
     */
    const TickType_t period =
        pdMS_TO_TICKS(SENSOR_SAMPLE_MS) > 0
            ? pdMS_TO_TICKS(SENSOR_SAMPLE_MS)
            : 1;

    TickType_t last_wake =
        xTaskGetTickCount();


    ESP_LOGI(
        TAG,
        "GPIO %d ativo em LOW. Aguardando liberacao estavel.",
        SENSOR_GPIO
    );

    ESP_LOGI(
        TAG,
        "Presenca=%" PRId64
        " us; liberacao=%" PRId64
        " us; bloqueio=%" PRId64
        " us; amostragem=%lu ticks",

        COUNTER_PRESENCE_US,
        COUNTER_RELEASE_US,
        COUNTER_BLOCKED_US,
        (unsigned long)period
    );


    /*
     * ============================================================
     * LOOP PRINCIPAL
     * ============================================================
     *
     * Coleta e geração de eventos pertencem exclusivamente
     * à tarefa app_main.
     */
    bool mqtt_start_requested = false;
    while (true) {

        /*
         * Mantém período de amostragem aproximadamente constante.
         *
         * pdFALSE significa que o prazo já havia vencido.
         * Lacunas são tratadas pela máquina de estados.
         */
        (void)xTaskDelayUntil(
            &last_wake,
            period
        );


        /*
         * --------------------------------------------------------
         * AMOSTRAGEM DO SENSOR
         * --------------------------------------------------------
         */
        const bool current_present =
            sensor_sample(&activity);

        /*
         * Este instante será utilizado tanto pela máquina
         * de estados quanto pelo evento de produção.
         */
        const int64_t occurrence_us =
            esp_timer_get_time();

#if CONFIG_FLOWCOUNT_COMM_ENABLED

        if (!mqtt_start_requested &&
            wifi_manager_is_connected()) {

            const esp_err_t mqtt_status =
                mqtt_manager_start();

            if (mqtt_status == ESP_OK) {

                ESP_LOGI(
                    TAG,
                    "Conexao MQTT solicitada"
                );

                mqtt_start_requested = true;

            } else {

                ESP_LOGW(
                    TAG,
                    "Falha ao iniciar MQTT: %s",
                    esp_err_to_name(mqtt_status)
                );
            }
        }

#endif


        /*
         * --------------------------------------------------------
         * SNTP
         * --------------------------------------------------------
         *
         * Só tentamos iniciar SNTP depois que:
         *
         * 1. comunicação está habilitada;
         * 2. Wi-Fi realmente possui IP;
         * 3. SNTP ainda não foi iniciado;
         * 4. o intervalo de retry já passou.
         */
#if CONFIG_FLOWCOUNT_COMM_ENABLED

        if (!sntp_started &&
            wifi_manager_is_connected() &&
            occurrence_us >= next_sntp_attempt_us) {

            const esp_err_t sntp_status =
                app_time_start_sntp();

            if (sntp_status == ESP_OK) {

                ESP_LOGI(
                    TAG,
                    "SNTP solicitado apos conexao Wi-Fi"
                );

                sntp_started = true;

            } else {

                ESP_LOGW(
                    TAG,
                    "Falha ao iniciar SNTP: %s",
                    esp_err_to_name(sntp_status)
                );

                /*
                 * Nova tentativa somente daqui a 5 segundos.
                 */
                next_sntp_attempt_us =
                    occurrence_us +
                    INT64_C(5000000);
            }
        }

#endif


        /*
         * --------------------------------------------------------
         * MÁQUINA DE ESTADOS DO CONTADOR
         * --------------------------------------------------------
         */
        const unsigned result =
            counter_update(
                &counter,
                current_present,
                activity,
                occurrence_us
            );


        /*
         * --------------------------------------------------------
         * PASSAGEM VÁLIDA
         * --------------------------------------------------------
         *
         * O mesmo instante monotônico da confirmação é entregue
         * ao módulo de produção.
         *
         * Nunca usamos posteriormente o horário de consumo
         * da fila como horário da ocorrência.
         */
        if (result & COUNTER_COUNT) {

            const esp_err_t beep_status =
                buzzer_beep();

            if (beep_status != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "Falha ao acionar buzzer: %s",
                    esp_err_to_name(beep_status)
                );
            }


            const esp_err_t status =
                production_events_record(
                    occurrence_us
                );

            if (status != ESP_ERR_NO_MEM) {
                ESP_ERROR_CHECK(status);
            }
        }


        /*
         * --------------------------------------------------------
         * MANUTENÇÃO / DIAGNÓSTICO DO RELÓGIO
         * --------------------------------------------------------
         */
        app_time_poll(
            occurrence_us
        );


        /*
         * Detecta mudança do estado temporal sem imprimir
         * mensagens em todos os ciclos.
         */
        const bool clock_synced =
            app_time_is_synchronized();

        if (clock_synced != previous_clock_state) {

            ESP_LOGI(
                TAG,
                "Clock synced = %s",
                clock_synced
                    ? "true"
                    : "false"
            );

            previous_clock_state =
                clock_synced;
        }


        /*
         * --------------------------------------------------------
         * DIAGNÓSTICOS DA MÁQUINA DE ESTADOS
         * --------------------------------------------------------
         */
        if (result & COUNTER_RESYNC) {

            ESP_LOGE(
                TAG,
                "Lacuna de amostragem: ciclo descartado; "
                "possivel perda. "
                "Aguardando liberacao estavel para rearmar."
            );
        }


        if (result & COUNTER_READY) {

            ESP_LOGI(
                TAG,
                "Coleta pronta: sensor livre."
            );
        }


        if (result & COUNTER_BLOCKED) {

            ESP_LOGW(
                TAG,
                "Presenca prolongada: "
                "possivel sensor bloqueado."
            );
        }


        if (result & COUNTER_CLEARED) {

            ESP_LOGI(
                TAG,
                "Condicao de bloqueio encerrada: "
                "sensor liberado."
            );
        }
    }
}