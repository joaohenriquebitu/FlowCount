#ifndef FLOWCOUNT_FAKE_NETWORK_H
#define FLOWCOUNT_FAKE_NETWORK_H
#include <assert.h>
#include "fake_buzzer.h"
#define BIT0 1u
#define CONFIG_FLOWCOUNT_WIFI_RETRY_SECONDS 5
#define CONFIG_FLOWCOUNT_WIFI_SSID "test-network"
#define CONFIG_FLOWCOUNT_WIFI_PASSWORD "test-password"
#define CONFIG_FLOWCOUNT_MQTT_URI "mqtt://test.invalid"
#define CONFIG_FLOWCOUNT_MQTT_USERNAME ""
#define CONFIG_FLOWCOUNT_MQTT_PASSWORD ""
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
#define ESP_RETURN_ON_ERROR(call, tag, message) do { \
    esp_err_t status_ = (call); (void)(tag); (void)(message); \
    if (status_ != ESP_OK) return status_; \
} while (0)
typedef uint32_t EventBits_t;
typedef EventBits_t *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
typedef const char *esp_event_base_t;
extern const char wifi_base[], ip_base[];
#define WIFI_EVENT wifi_base
#define IP_EVENT ip_base
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define IP_EVENT_STA_LOST_IP 4
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t esp_event_loop_create_default(void);
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef struct { struct { struct { unsigned addr; } ip; } ip_info; } ip_event_got_ip_t;
#define IPSTR "%u"
#define IP2STR(ip) ((ip)->addr)
typedef int esp_netif_t;
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
typedef struct { int placeholder; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() {0}
typedef struct { struct {
    char ssid[32], password[64];
    struct { int authmode; } threshold;
} sta; } wifi_config_t;
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 1
#define WIFI_MODE_STA 0
#define WIFI_IF_STA 0
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
bool esp_timer_is_active(esp_timer_handle_t timer);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout);
typedef void *esp_mqtt_client_handle_t;
typedef enum { MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_ERROR } esp_mqtt_event_id_t;
typedef struct {
    struct { struct { const char *uri; } address; } broker;
    struct { const char *username; struct { const char *password; } authentication; } credentials;
    struct { bool disable_auto_reconnect; } network;
} esp_mqtt_client_config_t;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain, bool store);
#endif
