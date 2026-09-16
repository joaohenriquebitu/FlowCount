#ifndef FLOWCOUNT_LEDS_H
#define FLOWCOUNT_LEDS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Uso exclusivo de app_main, sem chamadas concorrentes ou de ISR.
// LEDs ativos em HIGH: GPIO -> resistor -> LED -> GND.
// Inicialização idempotente: verde apagado; vermelho aceso se comunicação habilitada.
esp_err_t leds_init(void);

// Chamar a cada iteração, com now_us monotônico (esp_timer_get_time()). Não bloqueia.
// product_counted indica contagem local, mesmo offline; uma nova peça renova o pulso.
// O verde apaga na primeira atualização após o prazo. Vermelho acende enquanto
// Wi-Fi ou MQTT estiver desconectado; fica apagado com comunicação desabilitada.
// Falhas de escrita retornam erro e são tentadas novamente na próxima atualização.
esp_err_t leds_update(bool product_counted, bool wifi_connected,
                      bool mqtt_connected, int64_t now_us);

#endif
