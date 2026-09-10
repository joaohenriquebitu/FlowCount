#ifndef FLOWCOUNT_FAKE_TIME_H
#define FLOWCOUNT_FAKE_TIME_H
#include "fake_esp_idf.h"
extern int64_t fake_mono_us;
extern int64_t fake_wall_ms;
extern int64_t fake_read_delay_us;
extern esp_err_t fake_sntp_status;
extern bool fake_wall_failure;
extern unsigned fake_sntp_calls;
void fake_time_sync(int64_t utc_ms);
#endif
