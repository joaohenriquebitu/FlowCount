#include "indicators/leds.h"

#include "driver/gpio.h"
#include "sdkconfig.h"

static bool initialized;
static int green_level = -1;
static int red_level = -1;
static int64_t green_deadline_us;

static bool pin_reserved(int pin)
{
    // Pinos usados pelo buzzer, sensor e OLED nesta placa.
    return pin == CONFIG_FLOWCOUNT_BUZZER_GPIO || pin == 7 ||
           pin == 17 || pin == 18 || pin == 21 || pin == 36;
}

static bool connection_alert(bool wifi_connected, bool mqtt_connected)
{
#if defined(CONFIG_FLOWCOUNT_COMM_ENABLED) && CONFIG_FLOWCOUNT_COMM_ENABLED
    return !wifi_connected || !mqtt_connected;
#else
    (void)wifi_connected;
    (void)mqtt_connected;
    return false;
#endif
}

static esp_err_t set_output(int pin, bool on, int *last_level)
{
    const int level = on ? 1 : 0;
    if (*last_level == level) return ESP_OK;
    const esp_err_t status = gpio_set_level((gpio_num_t)pin, level);
    // Após uma falha, o nível físico é desconhecido: tentar de novo no próximo update.
    *last_level = status == ESP_OK ? level : -1;
    return status;
}

esp_err_t leds_init(void)
{
    if (initialized) return ESP_OK;
    const int green_pin = CONFIG_FLOWCOUNT_LED_GREEN_GPIO;
    const int red_pin = CONFIG_FLOWCOUNT_LED_RED_GPIO;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(green_pin) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(red_pin) || green_pin == red_pin ||
        pin_reserved(green_pin) || pin_reserved(red_pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_config_t config = {
        .pin_bit_mask = (UINT64_C(1) << green_pin) | (UINT64_C(1) << red_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t config_status = gpio_config(&config);
    if (config_status != ESP_OK) return config_status;

    green_level = -1;
    red_level = -1;
    const esp_err_t green_status = set_output(green_pin, false, &green_level);
    const esp_err_t red_status = set_output(red_pin, connection_alert(false, false),
                                           &red_level);
    if (green_status != ESP_OK) return green_status;
    if (red_status != ESP_OK) return red_status;
    green_deadline_us = 0;
    initialized = true;
    return ESP_OK;
}

esp_err_t leds_update(bool product_counted, bool wifi_connected,
                      bool mqtt_connected, int64_t now_us)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (product_counted) {
        green_deadline_us = now_us + (int64_t)CONFIG_FLOWCOUNT_LED_PULSE_MS * 1000;
    }
    const esp_err_t green_status = set_output(CONFIG_FLOWCOUNT_LED_GREEN_GPIO,
                                              now_us < green_deadline_us, &green_level);
    const esp_err_t red_status = set_output(CONFIG_FLOWCOUNT_LED_RED_GPIO,
                                            connection_alert(wifi_connected, mqtt_connected),
                                            &red_level);
    return green_status != ESP_OK ? green_status : red_status;
}
