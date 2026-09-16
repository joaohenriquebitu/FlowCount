#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fake_leds.h"
#include "indicators/leds.h"

#ifndef TEST_EXPECT_INVALID_PINS
#define TEST_EXPECT_INVALID_PINS 0
#endif

static unsigned configs, writes;
static int green = -1, red = -1;
static bool fail_config;
static int fail_pin = -1;

void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t status) { (void)status; return "test"; }

esp_err_t gpio_config(const gpio_config_t *config)
{
    ++configs;
    assert(!TEST_EXPECT_INVALID_PINS);
    assert(config->pin_bit_mask == ((UINT64_C(1) << CONFIG_FLOWCOUNT_LED_GREEN_GPIO)
                                   | (UINT64_C(1) << CONFIG_FLOWCOUNT_LED_RED_GPIO)));
    assert(config->mode == GPIO_MODE_OUTPUT);
    assert(config->pull_up_en == GPIO_PULLUP_DISABLE);
    assert(config->pull_down_en == GPIO_PULLDOWN_DISABLE);
    assert(config->intr_type == GPIO_INTR_DISABLE);
    if (fail_config) { fail_config = false; return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    ++writes;
    assert(!TEST_EXPECT_INVALID_PINS);
    assert(pin == CONFIG_FLOWCOUNT_LED_GREEN_GPIO || pin == CONFIG_FLOWCOUNT_LED_RED_GPIO);
    assert(level <= 1);
    if (pin == fail_pin) { fail_pin = -1; return ESP_FAIL; }
    if (pin == CONFIG_FLOWCOUNT_LED_GREEN_GPIO) green = (int)level;
    else red = (int)level;
    return ESP_OK;
}

#if !TEST_EXPECT_INVALID_PINS
static int disconnected(bool wifi, bool mqtt)
{
    return CONFIG_FLOWCOUNT_COMM_ENABLED && !(wifi && mqtt);
}

static void update(bool product, bool wifi, bool mqtt, int64_t now)
{
    assert(leds_update(product, wifi, mqtt, now) == ESP_OK);
    assert(red == disconnected(wifi, mqtt));
}

static void connections(void)
{
    // First connection, Wi-Fi-only, MQTT loss, Wi-Fi loss, and reconnection.
    const bool states[][2] = {
        {false, false}, {true, false}, {true, true}, {true, false},
        {true, true}, {false, true}, {false, false}, {true, false}, {true, true}
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        const unsigned previous_writes = writes;
        const int previous_red = red;
        update(false, states[i][0], states[i][1], 0);
        assert(green == 0);
        assert(writes == previous_writes + (unsigned)(previous_red != red));
        update(false, states[i][0], states[i][1], 0);
        assert(writes == previous_writes + (unsigned)(previous_red != red));
    }
}

static void pulses(void)
{
    const int64_t start = INT64_C(1000000);
    const int64_t duration = (int64_t)CONFIG_FLOWCOUNT_LED_PULSE_MS * 1000;
    update(true, true, true, start);
    assert(green == 1); // The counting iteration already lights green.
    const unsigned on_writes = writes;
    update(false, true, true, start + duration - 1);
    assert(green == 1 && writes == on_writes);
    update(false, true, true, start + duration);
    assert(green == 0 && writes == on_writes + 1);

    const int64_t next = start + 2 * duration;
    update(true, true, true, next);
    const unsigned renewed_writes = writes;
    update(true, true, true, next + duration / 2);
    assert(green == 1 && writes == renewed_writes);
    update(false, true, true, next + duration);
    assert(green == 1 && writes == renewed_writes);
    update(false, true, true, next + duration + duration / 2);
    assert(green == 0 && writes == renewed_writes + 1);

    // Counting continues while offline; the red status stays independent.
    const int64_t offline = start + 5 * duration;
    update(true, false, false, offline);
    assert(green == 1);
    update(false, false, false, offline + duration - 1);
    assert(green == 1);
    update(false, false, false, offline + duration);
    assert(green == 0);
    const unsigned offline_writes = writes;
    update(false, false, false, offline + 10 * duration);
    assert(green == 0 && writes == offline_writes); // Red is steady, not blinking.
}

static void output_errors(void)
{
    const int64_t start = INT64_C(10000000);
    const int64_t duration = (int64_t)CONFIG_FLOWCOUNT_LED_PULSE_MS * 1000;
    update(false, true, true, start);
    fail_pin = CONFIG_FLOWCOUNT_LED_GREEN_GPIO;
    assert(leds_update(true, false, false, start) == ESP_FAIL);
    assert(green == 0 && red == disconnected(false, false)); // Red still updates.
    update(false, false, false, start + 1);
    assert(green == 1); // Retry retains the pending product's pulse.

    fail_pin = CONFIG_FLOWCOUNT_LED_GREEN_GPIO;
    assert(leds_update(false, false, false, start + duration) == ESP_FAIL);
    assert(green == 1);
    update(false, false, false, start + duration + 1);
    assert(green == 0);

    // A failed on write must not light a pulse whose duration already expired.
    fail_pin = CONFIG_FLOWCOUNT_LED_GREEN_GPIO;
    assert(leds_update(true, true, true, start + 2 * duration) == ESP_FAIL);
    const unsigned failed_writes = writes;
    update(false, true, true, start + 3 * duration);
    assert(green == 0 && writes == failed_writes + 1);

#if CONFIG_FLOWCOUNT_COMM_ENABLED
    fail_pin = CONFIG_FLOWCOUNT_LED_RED_GPIO;
    assert(leds_update(false, true, false, start + 4 * duration) == ESP_FAIL);
    assert(red == 0);
    update(false, true, false, start + 4 * duration + 1);
    assert(red == 1);
    fail_pin = CONFIG_FLOWCOUNT_LED_RED_GPIO;
    assert(leds_update(true, true, true, start + 5 * duration) == ESP_FAIL);
    assert(green == 1 && red == 1); // Product indication survives a red write error.
    update(false, true, true, start + 5 * duration + 1);
    assert(green == 1 && red == 0);
#endif
}
#endif

int main(int argc, char **argv)
{
    assert(leds_update(false, false, false, 0) == ESP_ERR_INVALID_STATE);
    assert(configs == 0 && writes == 0);
#if TEST_EXPECT_INVALID_PINS
    (void)argc;
    (void)argv;
    assert(leds_init() == ESP_ERR_INVALID_ARG);
    assert(leds_update(true, true, true, 0) == ESP_ERR_INVALID_STATE);
    assert(configs == 0 && writes == 0);
    puts("OK: LEDs reject invalid/conflicting pins before accessing GPIO");
#else
    if (argc > 1) {
        if (strcmp(argv[1], "config") == 0) fail_config = true;
        else if (strcmp(argv[1], "green") == 0) fail_pin = CONFIG_FLOWCOUNT_LED_GREEN_GPIO;
        else { assert(strcmp(argv[1], "red") == 0); fail_pin = CONFIG_FLOWCOUNT_LED_RED_GPIO; }
        assert(leds_init() == ESP_FAIL);
        assert(leds_update(true, true, true, 0) == ESP_ERR_INVALID_STATE);
    }
    assert(leds_init() == ESP_OK);
    assert(green == 0 && red == disconnected(false, false));
    const unsigned initial_configs = configs, initial_writes = writes;
    assert(leds_init() == ESP_OK);
    assert(configs == initial_configs && writes == initial_writes);
    connections();
    pulses();
    output_errors();
    printf("OK: LEDs pulse/status/retry/init; COMM=%d; init failure=%s\n",
           CONFIG_FLOWCOUNT_COMM_ENABLED, argc > 1 ? argv[1] : "none");
#endif
    return 0;
}
