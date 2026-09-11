#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "time/app_time.h"
#include "fake_time.h"

static unsigned timeout_logs;
void fake_log(const char *tag, const char *format, ...)
{
    (void)tag;
    char text[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(text));
    if (strstr(text, "apos 60 s")) ++timeout_logs;
}
int main(void)
{
    const int64_t epoch_ms = INT64_C(1700000000123);
    assert(!app_time_is_synchronized());
    assert(app_time_start_sntp() == ESP_ERR_INVALID_STATE);
    assert(app_time_init() == ESP_OK);
    assert(app_time_init() == ESP_ERR_INVALID_STATE);
    fake_wall_ms = epoch_ms; // Data plausível sem callback nunca basta.
    assert(!app_time_capture(1000).synced);
    fake_sntp_status = ESP_ERR_NO_MEM;
    assert(app_time_start_sntp() == ESP_ERR_NO_MEM);
    assert(!app_time_is_synchronized());
    fake_sntp_status = ESP_OK;
    assert(app_time_start_sntp() == ESP_OK);
    assert(app_time_start_sntp() == ESP_OK);
    assert(fake_sntp_calls == 2);
    fake_mono_us = INT64_C(61000000);
    app_time_poll(fake_mono_us);
    app_time_poll(fake_mono_us + 1000000);
    assert(timeout_logs == 1);
    const app_time_stamp_t before_sync = app_time_capture(fake_mono_us);
    assert(!before_sync.synced && before_sync.timestamp_ms == 0);

    fake_time_sync(epoch_ms);
    assert(app_time_is_synchronized());
    assert(!app_time_capture(fake_mono_us - 1).synced);
    const app_time_stamp_t first = app_time_capture(fake_mono_us + 2000999);
    assert(first.synced && first.timestamp_ms == epoch_ms + 2000);
    fake_mono_us += 30000000;
    assert(first.timestamp_ms == epoch_ms + 2000); // Cópia imutável após 30 s.
    assert(before_sync.timestamp_ms == 0 && !before_sync.synced);

    fake_time_sync(epoch_ms - 60000); // Ajuste para trás afeta só novas capturas.
    assert(app_time_capture(fake_mono_us).timestamp_ms == epoch_ms - 60000);
    assert(first.timestamp_ms == epoch_ms + 2000);
    app_time_poll(fake_mono_us);
    fake_mono_us += CONFIG_FLOWCOUNT_CLOCK_MAX_AGE_SECONDS * INT64_C(1000000) + 1;
    assert(!app_time_is_synchronized());
    assert(app_time_capture(fake_mono_us).timestamp_ms == 0);
    app_time_poll(fake_mono_us);
    fake_time_sync(epoch_ms + 86400000);
    assert(app_time_is_synchronized());
    fake_time_sync(0); // Rejeitar 1970 mesmo se callback for disparado.
    assert(!app_time_is_synchronized());
    fake_time_sync(epoch_ms);
    fake_wall_failure = true;
    fake_time_sync(epoch_ms);
    assert(!app_time_is_synchronized());
    fake_wall_failure = false;
    fake_read_delay_us = 10001;
    fake_time_sync(epoch_ms);
    assert(!app_time_is_synchronized());
    fake_read_delay_us = 0;
    fake_time_sync(epoch_ms);
    assert(app_time_is_synchronized());
    puts("OK: clock boot/UTC/SNTP falho/retry/timeout/quantizacao/30s/resync/expiracao/tempo invalido.");
    return 0;
}
