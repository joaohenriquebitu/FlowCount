#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fake_network.h"
#include "communication/wifi_manager.h"
#include "communication/mqtt_manager.h"
#include "communication/mqtt_protocol.h"
#include "indicators/buzzer.h"

const char wifi_base[] = "WIFI", ip_base[] = "IP";
static EventBits_t groups[2];
static unsigned group_count, notices, publishes, attempts;
static bool states[BUZZER_CONNECTION_COUNT], retry_active, outbox_full;
static esp_event_handler_t wifi_handler, ip_handler, mqtt_handler;
static void (*retry_callback)(void *);
static int client, netif;
void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t status) { (void)status; return "test"; }
void buzzer_connection_changed(buzzer_connection_t source, bool connected)
{
    assert((unsigned)source < BUZZER_CONNECTION_COUNT);
    states[source] = connected;
    ++notices;
}
EventGroupHandle_t xEventGroupCreate(void) { assert(group_count < 2); return &groups[group_count++]; }
EventBits_t xEventGroupGetBits(EventGroupHandle_t g) { return *g; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t b) { return *g |= b; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t b) { EventBits_t old = *g; *g &= ~b; return old; }
esp_err_t esp_event_handler_register(esp_event_base_t b, int32_t id, esp_event_handler_t fn, void *arg)
{
    (void)id; assert(arg == NULL);
    if (b == WIFI_EVENT) wifi_handler = fn;
    else { assert(b == IP_EVENT); ip_handler = fn; }
    return ESP_OK;
}
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &netif; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t esp_wifi_init(const wifi_init_config_t *c) { assert(c); return ESP_OK; }
esp_err_t esp_wifi_connect(void) { ++attempts; return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { assert(mode == WIFI_MODE_STA); return ESP_OK; }
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *c)
{
    assert(interface == WIFI_IF_STA && strcmp(c->sta.ssid, CONFIG_FLOWCOUNT_WIFI_SSID) == 0);
    return ESP_OK;
}
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer)
{
    retry_callback = args->callback; *timer = &retry_active; return ESP_OK;
}
bool esp_timer_is_active(esp_timer_handle_t timer) { assert(timer == &retry_active); return retry_active; }
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout)
{
    assert(timer == &retry_active && timeout == 5000000);
    retry_active = true; return ESP_OK;
}
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    assert(!config->network.disable_auto_reconnect);
    return &client;
}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c, int32_t id, esp_event_handler_t fn, void *arg)
{
    assert(c == &client && id == ESP_EVENT_ANY_ID && arg == NULL);
    mqtt_handler = fn; return ESP_OK;
}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c) { assert(c == &client); return ESP_OK; }
bool mqtt_event_json(const production_event_t *event, char *buffer, size_t size)
{
    assert(event && size > 2); strcpy(buffer, "{}"); return true;
}
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t c, const char *topic, const char *data, int len, int qos, int retain, bool store)
{
    assert(c == &client && topic && data && len == 0 && qos == 1 && retain == 0 && store);
    assert(strcmp(topic, "fabrica/setorA/bancada/B01/evento") == 0);
    if (outbox_full) return -2;
    return (int)++publishes;
}
int main(void)
{
    assert(!wifi_manager_is_connected() && !mqtt_manager_is_connected());
    assert(wifi_manager_init() == ESP_OK && mqtt_manager_init() == ESP_OK);
    assert(notices == 0);
    wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
    assert(attempts == 1);
    wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(notices == 1 && !states[BUZZER_CONNECTION_WIFI] && retry_active);
    retry_active = false;
    retry_callback(NULL);
    assert(attempts == 2);
    ip_event_got_ip_t ip = { .ip_info.ip.addr = 1234 };
    ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
    assert(wifi_manager_is_connected() && states[BUZZER_CONNECTION_WIFI]);
    assert(mqtt_manager_start() == ESP_OK);
    mqtt_handler(NULL, NULL, MQTT_EVENT_CONNECTED, NULL);
    assert(mqtt_manager_is_connected() && states[BUZZER_CONNECTION_MQTT]);
    production_event_t event = {0};
    assert(mqtt_manager_publish_event(&event) == ESP_OK && publishes == 1);
    mqtt_handler(NULL, NULL, MQTT_EVENT_DISCONNECTED, NULL);
    assert(!mqtt_manager_is_connected() && !states[BUZZER_CONNECTION_MQTT]);
    assert(mqtt_manager_publish_event(&event) == ESP_ERR_INVALID_STATE && publishes == 1);
    mqtt_handler(NULL, NULL, MQTT_EVENT_CONNECTED, NULL);
    assert(mqtt_manager_is_connected() && states[BUZZER_CONNECTION_MQTT]);
    assert(mqtt_manager_publish_event(&event) == ESP_OK && publishes == 2);
    outbox_full = true;
    assert(mqtt_manager_publish_event(&event) == ESP_FAIL && publishes == 2);
    outbox_full = false;
    wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    assert(!wifi_manager_is_connected() && !states[BUZZER_CONNECTION_WIFI]);
    puts("OK: callbacks reais Wi-Fi/MQTT notificam buzzer; queda limpa estado e impede publish.");
    return 0;
}
