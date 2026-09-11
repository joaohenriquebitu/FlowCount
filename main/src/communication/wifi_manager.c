#include "communication/wifi_manager.h"
#include "esp_check.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_event_group;
static esp_timer_handle_t reconnect_timer;

static void reconnect_timer_callback(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi");

    const esp_err_t status = esp_wifi_connect();

    if (status != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect falhou: %s",
                 esp_err_to_name(status));
    }
}

static void schedule_reconnect(void)
{
    if (esp_timer_is_active(reconnect_timer)) {
        return;
    }

    const int64_t delay_us =
        (int64_t)CONFIG_FLOWCOUNT_WIFI_RETRY_SECONDS * 1000000;

    const esp_err_t status =
        esp_timer_start_once(reconnect_timer, delay_us);

    if (status != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao agendar reconexao: %s",
                 esp_err_to_name(status));
    }
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "Wi-Fi iniciado; conectando");

        const esp_err_t status = esp_wifi_connect();

        if (status != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao iniciar conexao: %s",
                     esp_err_to_name(status));
            schedule_reconnect();
        }

        return;
    }

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(TAG,
                 "Wi-Fi desconectado; reason=%d",
                 event ? event->reason : -1);

        schedule_reconnect();
        return;
    }

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {

        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG,
                 "Wi-Fi conectado, IP=" IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t status = nvs_flash_init();

    if (status == ESP_ERR_NVS_NO_FREE_PAGES ||
        status == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        status = nvs_flash_init();
    }

    return status;
}

esp_err_t wifi_manager_init(void)
{
    if (CONFIG_FLOWCOUNT_WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "SSID nao configurado");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        init_nvs(),
        TAG,
        "Falha ao inicializar NVS"
    );

    ESP_RETURN_ON_ERROR(
        esp_netif_init(),
        TAG,
        "Falha ao inicializar esp_netif"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_loop_create_default(),
        TAG,
        "Falha ao criar event loop"
    );

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_t *netif =
        esp_netif_create_default_wifi_sta();

    if (netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_RETURN_ON_ERROR(
        esp_wifi_init(&init_config),
        TAG,
        "Falha no driver Wi-Fi"
    );

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_retry",
    };

    ESP_RETURN_ON_ERROR(
        esp_timer_create(&timer_args, &reconnect_timer),
        TAG,
        "Falha ao criar timer Wi-Fi"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL
        ),
        TAG,
        "Falha ao registrar WIFI_EVENT"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL
        ),
        TAG,
        "Falha ao registrar IP_EVENT"
    );

    wifi_config_t wifi_config = {0};

    const size_t ssid_len =
        strlen(CONFIG_FLOWCOUNT_WIFI_SSID);

    const size_t password_len =
        strlen(CONFIG_FLOWCOUNT_WIFI_PASSWORD);

    if (ssid_len == 0 ||
        ssid_len > sizeof(wifi_config.sta.ssid) ||
        password_len > sizeof(wifi_config.sta.password)) {

        ESP_LOGE(TAG, "SSID ou senha com tamanho invalido");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(
        wifi_config.sta.ssid,
        CONFIG_FLOWCOUNT_WIFI_SSID,
        ssid_len
    );

    memcpy(
        wifi_config.sta.password,
        CONFIG_FLOWCOUNT_WIFI_PASSWORD,
        password_len
    );

    wifi_config.sta.threshold.authmode =
        password_len == 0
            ? WIFI_AUTH_OPEN
            : WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_mode(WIFI_MODE_STA),
        TAG,
        "Falha ao configurar modo STA"
    );

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        ),
        TAG,
        "Falha ao configurar Wi-Fi"
    );

    ESP_RETURN_ON_ERROR(
        esp_wifi_start(),
        TAG,
        "Falha ao iniciar Wi-Fi"
    );

    ESP_LOGI(TAG, "Wi-Fi inicializado");

    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    if (wifi_event_group == NULL) {
        return false;
    }

    return
        (xEventGroupGetBits(wifi_event_group) &
         WIFI_CONNECTED_BIT) != 0;
}