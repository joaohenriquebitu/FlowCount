#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
test_dir=$(mktemp -d /tmp/flowcount-tests.XXXXXX)
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/freertos"
# O adaptador é criado apenas no diretório temporário; o build ESP-IDF não o usa.
for header in esp_err.h sdkconfig.h esp_log.h esp_random.h bootloader_random.h \
              freertos/FreeRTOS.h freertos/queue.h freertos/task.h esp_netif_sntp.h esp_timer.h; do
    printf '#include "fake_esp_idf.h"\n' > "$test_dir/$header"
done
printf '#define gettimeofday fake_time_gettimeofday\n' >> "$test_dir/esp_timer.h"
flags=(-D_POSIX_C_SOURCE=200809L -pthread -std=c11 -Wall -Wextra -Werror -pedantic -g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined)
fi
cc "${flags[@]}" -I main main/counter.c tests/test_counter.c -o "$test_dir/counter"
"$test_dir/counter"
cc "${flags[@]}" -I "$test_dir" -I tests -I main \
    main/app_time.c tests/fake_time.c tests/test_app_time.c -o "$test_dir/clock"
"$test_dir/clock"
for diagnostic in 0 1; do
    cc "${flags[@]}" -I "$test_dir" -I tests -I main \
        -DCONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER="$diagnostic" \
        main/counter.c main/production_event.c main/app_time.c tests/fake_time.c tests/test_production_event.c \
        -o "$test_dir/events-$diagnostic"
    "$test_dir/events-$diagnostic"
done
