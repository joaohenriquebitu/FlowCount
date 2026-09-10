#include <assert.h>
#include <string.h>
#include "fake_time.h"
int64_t fake_mono_us;
int64_t fake_wall_ms;
int64_t fake_read_delay_us;
esp_err_t fake_sntp_status;
bool fake_wall_failure;
unsigned fake_sntp_calls;
static void (*callback)(struct timeval *);

int64_t esp_timer_get_time(void) { return fake_mono_us; }
int fake_time_gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (fake_wall_failure) return -1;
    tv->tv_sec = (time_t)(fake_wall_ms / 1000);
    tv->tv_usec = (suseconds_t)((fake_wall_ms % 1000) * 1000);
    fake_mono_us += fake_read_delay_us;
    return 0;
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{
    ++fake_sntp_calls;
    assert(config->start && !config->wait_for_sync && !config->smooth_sync);
    assert(strcmp(config->server, CONFIG_FLOWCOUNT_NTP_SERVER) == 0);
    if (fake_sntp_status != ESP_OK) return fake_sntp_status;
    callback = config->sync_cb;
    assert(callback != NULL);
    return ESP_OK;
}
void fake_time_sync(int64_t utc_ms)
{
    assert(callback);
    fake_wall_ms = utc_ms;
    struct timeval tv = { .tv_sec = (time_t)(utc_ms / 1000),
                          .tv_usec = (suseconds_t)((utc_ms % 1000) * 1000) };
    callback(&tv);
}
