#ifndef FLOWCOUNT_FAKE_LEDS_H
#define FLOWCOUNT_FAKE_LEDS_H

#include "fake_esp_idf.h"

#ifndef CONFIG_FLOWCOUNT_LED_GREEN_GPIO
#define CONFIG_FLOWCOUNT_LED_GREEN_GPIO 1
#endif
#ifndef CONFIG_FLOWCOUNT_LED_RED_GPIO
#define CONFIG_FLOWCOUNT_LED_RED_GPIO 40
#endif
#ifndef CONFIG_FLOWCOUNT_LED_PULSE_MS
#define CONFIG_FLOWCOUNT_LED_PULSE_MS 100
#endif
#define CONFIG_FLOWCOUNT_BUZZER_GPIO 45
#define ESP_FAIL -1

typedef int gpio_num_t;
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) \
    ((pin) >= 0 && (pin) <= 48 && ((pin) <= 21 || (pin) >= 26))

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
const char *esp_err_to_name(esp_err_t status);

#endif
