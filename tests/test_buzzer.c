#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "fake_buzzer.h"
#include "indicators/buzzer.h"

static void (*callback)(void *);
static int64_t now;
static unsigned duty, staged, starts, stops, creates, deletes;
static int fail_init, step;
static bool in_callback, fail_duty, fail_update, fail_stop;

void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t status) { (void)status; return "test"; }
int64_t esp_timer_get_time(void) { return now; }
static esp_err_t init_step(void) { return ++step == fail_init ? ESP_FAIL : ESP_OK; }
esp_err_t ledc_timer_config(const ledc_timer_config_t *c)
{
    assert(c->freq_hz == 2400 && c->duty_resolution == 10);
    return init_step();
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c)
{
    assert(c->gpio_num == 45 && c->duty == 0);
    return init_step();
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer)
{
    ++creates;
    callback = args->callback;
    assert(args->skip_unhandled_events);
    *timer = &creates;
    return init_step();
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    assert(timer == &creates && period == 10000);
    return init_step();
}
esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    assert(timer == &creates);
    ++deletes;
    return ESP_OK;
}
esp_err_t ledc_set_duty(int mode, int channel, uint32_t value)
{
    assert(in_callback && mode == 0 && channel == 0);
    assert(value == 0 || value == 512);
    if (fail_duty) { fail_duty = false; return ESP_FAIL; }
    staged = value;
    return ESP_OK;
}
esp_err_t ledc_update_duty(int mode, int channel)
{
    assert(in_callback && mode == 0 && channel == 0);
    if (fail_update) { fail_update = false; return ESP_FAIL; }
    duty = staged;
    if (duty) ++starts; else ++stops;
    return ESP_OK;
}
esp_err_t ledc_stop(int mode, int channel, uint32_t idle)
{
    assert(in_callback && mode == 0 && channel == 0 && idle == 0);
    if (fail_stop) { fail_stop = false; return ESP_FAIL; }
    duty = 0;
    return ESP_OK;
}
static void tick(unsigned ms)
{
    now += (int64_t)ms * 1000;
    in_callback = true;
    callback(NULL);
    in_callback = false;
}
static void drain(unsigned ms)
{
    for (unsigned i = 0; i < ms; i += 10) tick(10);
}
static void connect_all(void)
{
    buzzer_connection_changed(BUZZER_CONNECTION_WIFI, true);
    buzzer_connection_changed(BUZZER_CONNECTION_MQTT, true);
}

int main(int argc, char **argv)
{
    assert(buzzer_beep() == ESP_ERR_INVALID_STATE);
    if (argc > 1) {
        fail_init = atoi(argv[1]);
        assert(buzzer_init() == ESP_FAIL);
        assert(buzzer_beep() == ESP_ERR_INVALID_STATE);
        assert(deletes == (fail_init == 4 ? 1u : 0u));
        fail_init = step = 0;
    }
    assert(buzzer_init() == ESP_OK);
    const unsigned created = creates;
    assert(buzzer_init() == ESP_OK && creates == created);
    buzzer_connection_changed(BUZZER_CONNECTION_WIFI, false);
    buzzer_connection_changed(BUZZER_CONNECTION_MQTT, false);
    buzzer_connection_changed((buzzer_connection_t)-1, true);
    drain(100);
    assert(starts == 0); // Sem teste sonoro no boot ou alarme de primeira conexão.

    assert(buzzer_beep() == ESP_OK);
    assert(duty == 0); // Produtores nunca acessam o PWM nem esperam som.
    tick(10);
    assert(duty == 512);
    tick(59);
    assert(duty == 512);
    assert(buzzer_beep() == ESP_OK); // Não estende o primeiro beep.
    tick(1);
    assert(duty == 0);
    tick(40);
    assert(duty == 512);
    drain(100);
    assert(duty == 0 && starts == 2);

    connect_all();
    buzzer_connection_changed(BUZZER_CONNECTION_WIFI, false);
    tick(10);
    assert(duty == 512);
    // A mesma queda MQTT não inicia outro alerta. Contagem aguarda o alerta.
    buzzer_connection_changed(BUZZER_CONNECTION_MQTT, false);
    assert(buzzer_beep() == ESP_OK);
    for (int i = 0; i < 3; ++i) {
        tick(179);
        assert(duty == 512);
        tick(1);
        assert(duty == 0);
        tick(119);
        assert(duty == 0);
        tick(1);
        assert(duty == 512); // Último intervalo inicia o beep pendente.
    }
    tick(60);
    assert(duty == 0);
    drain(100);
    assert(starts == 6);
    buzzer_connection_changed(BUZZER_CONNECTION_WIFI, false);
    drain(1000);
    assert(starts == 6);
    connect_all();
    buzzer_connection_changed(BUZZER_CONNECTION_MQTT, false);
    buzzer_connection_changed(BUZZER_CONNECTION_MQTT, true); // Queda entre ticks.
    drain(1000);
    assert(starts == 9 && duty == 0);

    for (int i = 0; i < 16; ++i) assert(buzzer_beep() == ESP_OK);
    assert(buzzer_beep() == ESP_ERR_NO_MEM);
    drain(2000);
    assert(starts == 25 && duty == 0);
    fail_duty = true;
    assert(buzzer_beep() == ESP_OK);
    tick(10);
    assert(duty == 0);
    assert(buzzer_beep() == ESP_OK);
    tick(10);
    assert(duty == 512);
    fail_update = fail_stop = true;
    tick(60);
    tick(10); // Repete desligamento quando o primeiro ledc_stop falha.
    assert(duty == 0);
    assert(buzzer_beep() == ESP_OK);
    drain(200);
    assert(duty == 0);
    printf("OK: buzzer PWM/60ms/alerta/reconexao/fila/erros; falha init=%s\n", argc > 1 ? argv[1] : "nenhuma");
    return 0;
}
