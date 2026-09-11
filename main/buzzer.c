#include "buzzer.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"


#define BUZZER_GPIO            45

#define BUZZER_FREQUENCY_HZ    2000
#define BUZZER_BEEP_MS         1000

#define BUZZER_SPEED_MODE      LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER           LEDC_TIMER_0
#define BUZZER_CHANNEL         LEDC_CHANNEL_0

/*
 * Resolução de 10 bits:
 *
 * duty máximo = 1023
 * duty 512 ≈ 50%
 */
#define BUZZER_DUTY            512


static const char *TAG = "BUZZER";

static esp_timer_handle_t buzzer_stop_timer;

static bool buzzer_initialized = false;


/*
 * Desliga o buzzer colocando o duty em zero.
 */
static void buzzer_stop_callback(void *arg)
{
    (void)arg;

    const esp_err_t status =
        ledc_set_duty_and_update(
            BUZZER_SPEED_MODE,
            BUZZER_CHANNEL,
            0,
            0
        );

    if (status != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Falha ao desligar buzzer: %s",
            esp_err_to_name(status)
        );
    }
}


esp_err_t buzzer_init(void)
{
    /*
     * Configura o timer responsável
     * pela frequência do PWM.
     */
    const ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER,
        .freq_hz = BUZZER_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t status =
        ledc_timer_config(&timer_config);

    if (status != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao configurar timer LEDC: %s",
            esp_err_to_name(status)
        );

        return status;
    }


    /*
     * Configura o canal PWM no GPIO 45.
     *
     * Começamos com duty = 0,
     * portanto o buzzer inicia desligado.
     */
    const ledc_channel_config_t channel_config = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    status =
        ledc_channel_config(
            &channel_config
        );

    if (status != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao configurar canal LEDC: %s",
            esp_err_to_name(status)
        );

        return status;
    }


    /*
     * ledc_set_duty_and_update() depende do
     * serviço de fade estar instalado.
     *
     * Mesmo não utilizando fade sonoro,
     * a API escolhida necessita dessa
     * infraestrutura inicializada.
     *
     * Deve ser feito UMA VEZ no boot,
     * antes do primeiro beep.
     */
    status =
        ledc_fade_func_install(0);

    if (status != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao instalar servico LEDC fade: %s",
            esp_err_to_name(status)
        );

        return status;
    }


    /*
     * Cria um timer de uma execução.
     *
     * Ele será utilizado para desligar
     * o buzzer depois de BUZZER_BEEP_MS,
     * sem bloquear a tarefa principal.
     */
    const esp_timer_create_args_t timer_args = {
        .callback = buzzer_stop_callback,
        .name = "buzzer_stop",
    };

    status =
        esp_timer_create(
            &timer_args,
            &buzzer_stop_timer
        );

    if (status != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao criar timer do buzzer: %s",
            esp_err_to_name(status)
        );

        /*
         * Como o restante da inicialização falhou,
         * liberamos o serviço LEDC instalado.
         */
        ledc_fade_func_uninstall();

        return status;
    }


    buzzer_initialized = true;


    ESP_LOGI(
        TAG,
        "Buzzer inicializado: GPIO=%d freq=%d Hz beep=%d ms",
        BUZZER_GPIO,
        BUZZER_FREQUENCY_HZ,
        BUZZER_BEEP_MS
    );


    return ESP_OK;
}


esp_err_t buzzer_beep(void)
{
    if (!buzzer_initialized) {
        return ESP_ERR_INVALID_STATE;
    }


    /*
     * Se ainda existe um beep anterior ativo,
     * cancelamos somente o timer responsável
     * por desligá-lo.
     *
     * Depois reiniciamos a duração do beep.
     */
    if (esp_timer_is_active(buzzer_stop_timer)) {

        const esp_err_t stop_status =
            esp_timer_stop(
                buzzer_stop_timer
            );

        if (stop_status != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Falha ao cancelar timer anterior: %s",
                esp_err_to_name(stop_status)
            );

            return stop_status;
        }
    }


    /*
     * Liga o PWM.
     *
     * Frequência: 2 kHz
     * Duty: aproximadamente 50%
     */
    esp_err_t status =
        ledc_set_duty_and_update(
            BUZZER_SPEED_MODE,
            BUZZER_CHANNEL,
            BUZZER_DUTY,
            0
        );

    if (status != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao ligar buzzer: %s",
            esp_err_to_name(status)
        );

        return status;
    }


    /*
     * Agenda o desligamento para daqui
     * a BUZZER_BEEP_MS.
     *
     * esp_timer usa microssegundos.
     */
    status =
        esp_timer_start_once(
            buzzer_stop_timer,
            (uint64_t)BUZZER_BEEP_MS * 1000ULL
        );

    if (status != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Falha ao agendar desligamento do buzzer: %s",
            esp_err_to_name(status)
        );


        /*
         * Se não conseguimos garantir o
         * desligamento posterior, desligamos
         * imediatamente por segurança.
         */
        const esp_err_t off_status =
            ledc_set_duty_and_update(
                BUZZER_SPEED_MODE,
                BUZZER_CHANNEL,
                0,
                0
            );

        if (off_status != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Falha ao desligar buzzer apos erro: %s",
                esp_err_to_name(off_status)
            );
        }


        return status;
    }


    return ESP_OK;
}