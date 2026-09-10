#include "app_time.h"

#include <inttypes.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif_sntp.h"
#include "sdkconfig.h"

#define US_PER_SECOND INT64_C(1000000)
#define MIN_UNIX_SECONDS INT64_C(1577836800) // 2020-01-01: rejeitar epoch de boot.
#define MAX_REFERENCE_READ_US INT64_C(10000)

static const char *TAG = "RELOGIO";
static portMUX_TYPE clock_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool initialized;
    bool starting;
    bool started;
    bool has_sync;
    int64_t utc_anchor_us;
    int64_t mono_anchor_us;
    int64_t started_at_us;
} clock_data;
// Exclusivos do chamador de poll (app_main), nunca acessados pelo callback.
static int64_t last_poll_us;
static bool timeout_reported;
static bool stale_reported;

static void on_sntp_sync(struct timeval *notification)
{
    struct timeval wall;
    const int64_t before_us = esp_timer_get_time();
    const int rc = gettimeofday(&wall, NULL);
    const int64_t after_us = esp_timer_get_time();
    // Não confiar apenas em time(): esta referência só é aceita no callback SNTP.
    if (notification == NULL || rc != 0 || wall.tv_sec < MIN_UNIX_SECONDS ||
        wall.tv_sec > (INT64_MAX / US_PER_SECOND) - 1 ||
        wall.tv_usec < 0 || wall.tv_usec >= US_PER_SECOND ||
        after_us < before_us || after_us - before_us > MAX_REFERENCE_READ_US) {
        portENTER_CRITICAL(&clock_lock);
        clock_data.has_sync = false;
        portEXIT_CRITICAL(&clock_lock);
        ESP_LOGW(TAG, "Referencia SNTP invalida/imprecisa: CLOCK_UNSYNCED");
        return;
    }
    const int64_t utc_us = (int64_t)wall.tv_sec * US_PER_SECOND + wall.tv_usec;
    portENTER_CRITICAL(&clock_lock);
    clock_data.utc_anchor_us = utc_us;
    clock_data.mono_anchor_us = before_us + (after_us - before_us) / 2;
    clock_data.has_sync = true;
    portEXIT_CRITICAL(&clock_lock);

    struct tm utc;
    char text[32];
    const time_t seconds = wall.tv_sec;
    if (gmtime_r(&seconds, &utc) != NULL &&
        strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc) != 0) {
        ESP_LOGI(TAG, "CLOCK_SYNCED UTC=%s unix_ms=%" PRId64, text, utc_us / 1000);
    } else {
        ESP_LOGI(TAG, "CLOCK_SYNCED unix_ms=%" PRId64, utc_us / 1000);
    }
}

esp_err_t app_time_init(void)
{
    if (CONFIG_FLOWCOUNT_NTP_SERVER[0] == '\0' ||
        CONFIG_FLOWCOUNT_CLOCK_MAX_AGE_SECONDS * INT64_C(1000) <=
            CONFIG_LWIP_SNTP_UPDATE_DELAY) {
        ESP_LOGE(TAG, "Configuracao temporal invalida; coleta segue sem UTC");
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&clock_lock);
    if (clock_data.initialized) {
        portEXIT_CRITICAL(&clock_lock);
        return ESP_ERR_INVALID_STATE;
    }
    clock_data.initialized = true;
    portEXIT_CRITICAL(&clock_lock);
    ESP_LOGI(TAG, "CLOCK_UNSYNCED: aguardando conectividade; UTC invalido ate SNTP");
    return ESP_OK;
}

esp_err_t app_time_start_sntp(void)
{
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&clock_lock);
    if (!clock_data.initialized || clock_data.starting) {
        portEXIT_CRITICAL(&clock_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (clock_data.started) {
        portEXIT_CRITICAL(&clock_lock);
        return ESP_OK;
    }
    clock_data.starting = true;
    clock_data.started_at_us = now_us;
    portEXIT_CRITICAL(&clock_lock);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_FLOWCOUNT_NTP_SERVER);
    config.wait_for_sync = false;
    config.smooth_sync = false;
    config.sync_cb = on_sntp_sync;
    const esp_err_t status = esp_netif_sntp_init(&config);
    portENTER_CRITICAL(&clock_lock);
    clock_data.starting = false;
    clock_data.started = status == ESP_OK;
    portEXIT_CRITICAL(&clock_lock);
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar SNTP (%d); contagem continua", (int)status);
    } else {
        ESP_LOGI(TAG, "SNTP iniciado: servidor=%s intervalo=%d ms; sem espera da coleta",
                 CONFIG_FLOWCOUNT_NTP_SERVER, CONFIG_LWIP_SNTP_UPDATE_DELAY);
    }
    return status;
}

app_time_stamp_t app_time_capture(int64_t occurrence_mono_us)
{
    app_time_stamp_t result = {0};
    portENTER_CRITICAL(&clock_lock);
    const bool valid = clock_data.has_sync;
    const int64_t mono_us = clock_data.mono_anchor_us;
    const int64_t utc_us = clock_data.utc_anchor_us;
    portEXIT_CRITICAL(&clock_lock);
    if (!valid || occurrence_mono_us < mono_us) {
        return result; // Não reconstruir eventos anteriores à referência aceita.
    }
    const int64_t elapsed_us = occurrence_mono_us - mono_us;
    if (elapsed_us > CONFIG_FLOWCOUNT_CLOCK_MAX_AGE_SECONDS * US_PER_SECOND ||
        utc_us > INT64_MAX - elapsed_us) {
        return result;
    }
    result.timestamp_ms = (utc_us + elapsed_us) / 1000;
    result.synced = true;
    return result;
}

bool app_time_is_synchronized(void)
{
    return app_time_capture(esp_timer_get_time()).synced;
}

void app_time_poll(int64_t now_us)
{
    if (now_us - last_poll_us < US_PER_SECOND) {
        return;
    }
    last_poll_us = now_us;
    portENTER_CRITICAL(&clock_lock);
    const bool started = clock_data.started;
    const bool had_sync = clock_data.has_sync;
    const int64_t start_us = clock_data.started_at_us;
    portEXIT_CRITICAL(&clock_lock);
    const bool synced = app_time_capture(now_us).synced;
    if (synced) {
        timeout_reported = false;
        stale_reported = false;
    } else if (started && !had_sync && !timeout_reported &&
               now_us - start_us >= INT64_C(60000000)) {
        ESP_LOGW(TAG, "SNTP sem horario valido apos 60 s; coleta continua CLOCK_UNSYNCED");
        timeout_reported = true;
    } else if (had_sync && !synced && !stale_reported) {
        ESP_LOGW(TAG, "Referencia temporal vencida/indisponivel: CLOCK_UNSYNCED");
        stale_reported = true;
    }
}
