#ifndef FLOWCOUNT_BUZZER_H
#define FLOWCOUNT_BUZZER_H

#include "esp_err.h"

esp_err_t buzzer_init(void);
esp_err_t buzzer_beep(void);

#endif