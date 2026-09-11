#ifndef FLOWCOUNT_FAKE_BUZZER_H
#define FLOWCOUNT_FAKE_BUZZER_H
#include "fake_esp_idf.h"
#define CONFIG_FLOWCOUNT_BUZZER_GPIO 45
#define CONFIG_FLOWCOUNT_BUZZER_FREQUENCY_HZ 2400
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_AUTO_CLK 0
#define LEDC_INTR_DISABLE 0
#define ESP_FAIL -1
typedef struct {
    int speed_mode, duty_resolution, timer_num, freq_hz, clk_cfg;
} ledc_timer_config_t;
typedef struct {
    int gpio_num, speed_mode, channel, intr_type, timer_sel, duty, hpoint;
} ledc_channel_config_t;
typedef void *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *);
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *config);
esp_err_t ledc_channel_config(const ledc_channel_config_t *config);
esp_err_t ledc_set_duty(int mode, int channel, uint32_t duty);
esp_err_t ledc_update_duty(int mode, int channel);
esp_err_t ledc_stop(int mode, int channel, uint32_t idle);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
const char *esp_err_to_name(esp_err_t status);
#endif
