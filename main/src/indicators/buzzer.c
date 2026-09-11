#include "indicators/buzzer.h"

#include <stdint.h>
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#define BUZZER_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER LEDC_TIMER_0
#define BUZZER_CHANNEL LEDC_CHANNEL_0
#define BUZZER_DUTY 512
#define TICK_US INT64_C(10000)
#define COUNT_US INT64_C(60000)
#define COUNT_GAP_US INT64_C(40000)
#define ALERT_US INT64_C(180000)
#define ALERT_GAP_US INT64_C(120000)
#define MAX_PENDING_BEEPS 16

static const char *TAG = "BUZZER";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool initialized;
static unsigned pending_beeps;
static bool pending_alert, alert_active, outage_reported;
static bool connected[BUZZER_CONNECTION_COUNT];
static esp_timer_handle_t timer;

// Somente o callback do timer acessa o sequenciador e altera o PWM após init.
static enum { IDLE, COUNT_ON, COUNT_GAP, ALERT_ON, ALERT_GAP } phase;
static unsigned pulses_left;
static int64_t deadline_us;
static bool retry_stop;

static esp_err_t output(bool on)
{
    esp_err_t status = ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL,
                                    on ? BUZZER_DUTY : 0);
    if (status == ESP_OK) status = ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
    return status;
}

static void output_failed(esp_err_t status)
{
    ESP_LOGE(TAG, "Falha no PWM do buzzer: %s", esp_err_to_name(status));
    retry_stop = ledc_stop(BUZZER_MODE, BUZZER_CHANNEL, 0) != ESP_OK;
    phase = IDLE;
    portENTER_CRITICAL(&lock);
    alert_active = false;
    portEXIT_CRITICAL(&lock);
}

static void buzzer_tick(void *arg)
{
    (void)arg;
    if (retry_stop) {
        retry_stop = ledc_stop(BUZZER_MODE, BUZZER_CHANNEL, 0) != ESP_OK;
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (phase != IDLE && now < deadline_us) return;

    if (phase == COUNT_ON || phase == ALERT_ON) {
        const esp_err_t status = output(false);
        if (status != ESP_OK) {
            output_failed(status);
            return;
        }
        const bool alert = phase == ALERT_ON;
        phase = alert ? ALERT_GAP : COUNT_GAP;
        deadline_us = now + (alert ? ALERT_GAP_US : COUNT_GAP_US);
        return;
    }
    if (phase == ALERT_GAP && pulses_left > 0) {
        --pulses_left;
        phase = ALERT_ON;
    } else {
        portENTER_CRITICAL(&lock);
        if (phase == ALERT_GAP) alert_active = false;
        if (pending_alert) {
            pending_alert = false;
            alert_active = true;
            pulses_left = 2;
            phase = ALERT_ON;
        } else if (pending_beeps > 0) {
            --pending_beeps;
            phase = COUNT_ON;
        } else {
            phase = IDLE;
        }
        portEXIT_CRITICAL(&lock);
    }
    if (phase == IDLE) return;
    const esp_err_t status = output(true);
    if (status != ESP_OK) {
        output_failed(status);
        return;
    }
    deadline_us = now + (phase == ALERT_ON ? ALERT_US : COUNT_US);
}

esp_err_t buzzer_init(void)
{
    // Inicialização exclusiva de app_main, antes dos produtores de notificações.
    if (initialized) return ESP_OK;
    const ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER,
        .freq_hz = CONFIG_FLOWCOUNT_BUZZER_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t status = ledc_timer_config(&timer_config);
    if (status != ESP_OK) return status;
    const ledc_channel_config_t channel_config = {
        .gpio_num = CONFIG_FLOWCOUNT_BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    status = ledc_channel_config(&channel_config);
    if (status != ESP_OK) return status;
    const esp_timer_create_args_t args = {
        .callback = buzzer_tick,
        .name = "buzzer",
        .skip_unhandled_events = true,
    };
    status = esp_timer_create(&args, &timer);
    if (status != ESP_OK) return status;
    status = esp_timer_start_periodic(timer, TICK_US);
    if (status != ESP_OK) {
        esp_timer_delete(timer);
        timer = NULL;
        return status;
    }
    portENTER_CRITICAL(&lock);
    initialized = true;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "KC-1206: GPIO=%d PWM=%d Hz; contagem=60 ms; alerta=3 pulsos",
             CONFIG_FLOWCOUNT_BUZZER_GPIO, CONFIG_FLOWCOUNT_BUZZER_FREQUENCY_HZ);
    return ESP_OK;
}

esp_err_t buzzer_beep(void)
{
    portENTER_CRITICAL(&lock);
    esp_err_t status = ESP_ERR_INVALID_STATE;
    if (initialized) {
        status = pending_beeps < MAX_PENDING_BEEPS ? ESP_OK : ESP_ERR_NO_MEM;
        if (status == ESP_OK) ++pending_beeps;
    }
    portEXIT_CRITICAL(&lock);
    return status;
}

void buzzer_connection_changed(buzzer_connection_t connection, bool is_connected)
{
    if ((unsigned)connection >= BUZZER_CONNECTION_COUNT) return;
    portENTER_CRITICAL(&lock);
    const bool lost = connected[connection] && !is_connected;
    connected[connection] = is_connected;
    // Wi-Fi e MQTT podem cair juntos: um único alerta por episódio de queda.
    if (initialized && lost && !outage_reported) {
        if (!alert_active) pending_alert = true;
        outage_reported = true;
    }
    if (connected[BUZZER_CONNECTION_WIFI] && connected[BUZZER_CONNECTION_MQTT]) {
        outage_reported = false;
    }
    portEXIT_CRITICAL(&lock);
}
