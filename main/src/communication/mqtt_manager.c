#include "communication/mqtt_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "sdkconfig.h"
#include "communication/mqtt_protocol.h"


#define MQTT_CONNECTED_BIT BIT0


static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t mqtt_client;

static EventGroupHandle_t mqtt_event_group;

static bool mqtt_started;

esp_err_t mqtt_manager_publish_event(
    const production_event_t *event
)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mqtt_manager_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[MQTT_PAYLOAD_SIZE];

    if (!mqtt_event_json(
            event,
            payload,
            sizeof(payload))) {

        ESP_LOGE(
            TAG,
            "Falha ao serializar evento MQTT"
        );

        return ESP_FAIL;
    }

    const char *topic =
    "fabrica/setorA/bancada/B01/evento";

	const int msg_id =
	    esp_mqtt_client_publish(
	        mqtt_client,
	        topic,
	        payload,
	        0,
	        1,
	        0
	    );

    if (msg_id < 0) {

        ESP_LOGE(
            TAG,
            "Falha ao publicar evento MQTT"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Evento MQTT publicado: seq=%llu msg_id=%d payload=%s",
        (unsigned long long)event->sequence,
        msg_id,
        payload
    );

    return ESP_OK;
}

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {


    case MQTT_EVENT_CONNECTED:
	    ESP_LOGI(TAG, "MQTT conectado");

	    xEventGroupSetBits(
	        mqtt_event_group,
	        MQTT_CONNECTED_BIT
	    );

	    break;


    case MQTT_EVENT_ERROR:

        ESP_LOGW(TAG, "Erro na conexao MQTT");

        break;


    default:
        break;
    }
}

esp_err_t mqtt_manager_init(void)
{
    if (CONFIG_FLOWCOUNT_MQTT_URI[0] == '\0') {

        ESP_LOGE(
            TAG,
            "Broker MQTT nao configurado"
        );

        return ESP_ERR_INVALID_ARG;
    }


    mqtt_event_group =
        xEventGroupCreate();

    if (mqtt_event_group == NULL) {

        ESP_LOGE(
            TAG,
            "Falha ao criar EventGroup MQTT"
        );

        return ESP_ERR_NO_MEM;
    }


    const esp_mqtt_client_config_t mqtt_config = {

        .broker.address.uri =
            CONFIG_FLOWCOUNT_MQTT_URI,

        .credentials.username =
            CONFIG_FLOWCOUNT_MQTT_USERNAME[0]
                ? CONFIG_FLOWCOUNT_MQTT_USERNAME
                : NULL,

        .credentials.authentication.password =
            CONFIG_FLOWCOUNT_MQTT_PASSWORD[0]
                ? CONFIG_FLOWCOUNT_MQTT_PASSWORD
                : NULL,

        .network.disable_auto_reconnect = false,
    };


    mqtt_client =
        esp_mqtt_client_init(
            &mqtt_config
        );

    if (mqtt_client == NULL) {

        ESP_LOGE(
            TAG,
            "Falha ao criar cliente MQTT"
        );

        return ESP_FAIL;
    }


    const esp_err_t status =
        esp_mqtt_client_register_event(
            mqtt_client,
            ESP_EVENT_ANY_ID,
            mqtt_event_handler,
            NULL
        );


    if (status != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Falha ao registrar callback MQTT: %s",
            esp_err_to_name(status)
        );

        return status;
    }


    ESP_LOGI(
        TAG,
        "Cliente MQTT preparado para broker %s",
        CONFIG_FLOWCOUNT_MQTT_URI
    );


    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }


    if (mqtt_started) {
        return ESP_OK;
    }


    const esp_err_t status =
        esp_mqtt_client_start(
            mqtt_client
        );


    if (status == ESP_OK) {

        mqtt_started = true;

        ESP_LOGI(
            TAG,
            "Cliente MQTT iniciado"
        );
    }


    return status;
}

bool mqtt_manager_is_connected(void)
{
    if (mqtt_event_group == NULL) {
        return false;
    }


    return
        (xEventGroupGetBits(
            mqtt_event_group
        ) & MQTT_CONNECTED_BIT) != 0;
}