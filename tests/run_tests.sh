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
compiler="${CC:-cc}"
flags=(-D_POSIX_C_SOURCE=200809L -pthread -std=c11 -Wall -Wextra -Werror -pedantic -g)
if [[ "${SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
"$compiler" "${flags[@]}" -I main/include main/src/counting/counter.c tests/test_counter.c -o "$test_dir/counter"
"$test_dir/counter"
"$compiler" "${flags[@]}" -I "$test_dir" -I tests -I main/include \
    main/src/time/app_time.c tests/fake_time.c tests/test_app_time.c -o "$test_dir/clock"
"$test_dir/clock"
for profile in "0 0" "0 1" "1 0"; do
    read -r communication diagnostic <<< "$profile"
    "$compiler" "${flags[@]}" -I "$test_dir" -I tests -I main/include \
        -DCONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER="$diagnostic" \
        -DCONFIG_FLOWCOUNT_COMM_ENABLED="$communication" \
        main/src/counting/counter.c main/src/counting/production_event.c main/src/time/app_time.c tests/fake_time.c tests/test_production_event.c \
        -o "$test_dir/events-$communication-$diagnostic"
    "$test_dir/events-$communication-$diagnostic"
done

# Use cJSON real: instalado no host ou fornecido pelo ESP-IDF ativo.
cjson_dir="${CJSON_DIR:-${IDF_PATH:+$IDF_PATH/components/json/cJSON}}"
if [[ -n "$cjson_dir" && -f "$cjson_dir/cJSON.c" ]]; then
    cjson_flags=(-I "$cjson_dir")
    cjson_link=("$cjson_dir/cJSON.c" -lm)
elif command -v pkg-config >/dev/null && pkg-config --exists libcjson; then
    read -r -a cjson_flags <<< "$(pkg-config --cflags libcjson)"
    read -r -a cjson_link <<< "$(pkg-config --libs libcjson)"
else
    echo 'Instale pkg-config e libcjson-dev, ative o ESP-IDF ou defina CJSON_DIR.' >&2
    exit 1
fi
"$compiler" "${flags[@]}" -I "$test_dir" -I tests -I main/include "${cjson_flags[@]}" \
    main/src/communication/mqtt_protocol.c tests/test_mqtt_protocol.c \
    "${cjson_link[@]}" -o "$test_dir/mqtt"
"$test_dir/mqtt"

mkdir -p "$test_dir/driver"
printf '#include "fake_buzzer.h"\n' > "$test_dir/driver/ledc.h"
"$compiler" "${flags[@]}" -I "$test_dir" -I tests -I main/include \
    -include tests/fake_buzzer.h main/src/indicators/buzzer.c tests/test_buzzer.c \
    -o "$test_dir/buzzer"
"$test_dir/buzzer"
for failure in 1 2 3 4; do
    "$test_dir/buzzer" "$failure"
done

for header in esp_check.h esp_event.h esp_netif.h esp_wifi.h nvs_flash.h \
              mqtt_client.h freertos/event_groups.h; do
    printf '#include "fake_network.h"\n' > "$test_dir/$header"
done
"$compiler" "${flags[@]}" -I "$test_dir" -I tests -I main/include \
    -include tests/fake_network.h main/src/communication/wifi_manager.c \
    main/src/communication/mqtt_manager.c tests/test_network_alerts.c \
    -o "$test_dir/network-alerts"
"$test_dir/network-alerts"

"$compiler" "${flags[@]}" -I "$test_dir" -I tests -I main/include \
    -include tests/fake_network.h main/src/communication/communication.c \
    tests/test_communication.c -o "$test_dir/communication"
"$test_dir/communication"
