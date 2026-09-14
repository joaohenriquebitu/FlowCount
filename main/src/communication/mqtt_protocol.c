#include "communication/mqtt_protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include <time.h>
#include "sdkconfig.h"

static bool timestamp_to_iso8601(
    int64_t timestamp_ms,
    char *buffer,
    size_t size
)
{
    if (timestamp_ms <= 0 ||
        buffer == NULL ||
        size == 0) {

        return false;
    }

    const time_t seconds =
        (time_t)(timestamp_ms / 1000);

    const int64_t milliseconds =
        timestamp_ms % 1000;

    struct tm utc;

    if (gmtime_r(&seconds, &utc) == NULL) {
        return false;
    }

    char date[24];

    if (strftime(
            date,
            sizeof(date),
            "%Y-%m-%dT%H:%M:%S",
            &utc) == 0) {

        return false;
    }

    /*
     * O evento atualmente possui precisão de milissegundos.
     *
     * Portanto:
     *
     * 123 ms -> 123000 us
     */
    const int written =
        snprintf(
            buffer,
            size,
            "%s.%03" PRId64 "000Z",
            date,
            milliseconds
        );

    return written >= 0 &&
           (size_t)written < size;
}

void mqtt_session_text(const uint8_t session[16], char text[33])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; ++i) {
        text[i * 2] = hex[session[i] >> 4];
        text[i * 2 + 1] = hex[session[i] & 15];
    }
    text[32] = 0;
}
bool mqtt_topic_prefix_valid(const char *prefix)
{
    size_t n = strlen(prefix);
    if (n == 0 || n > 32) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = prefix[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
bool mqtt_event_json(
    const production_event_t *event,
    char *buffer,
    size_t size
)
{
    if (event == NULL ||
        buffer == NULL ||
        size == 0) {

        return false;
    }


    /*
     * A Raspberry exige um timestamp UTC válido.
     *
     * Não usamos o horário de transmissão como substituto,
     * pois o evento deve manter o horário real da passagem.
     */
    if (!event->clock_synced ||
        event->timestamp_ms <= 0) {

        return false;
    }


    char timestamp[40];

    if (!timestamp_to_iso8601(
            event->timestamp_ms,
            timestamp,
            sizeof(timestamp))) {

        return false;
    }


    /*
     * evt_id identifica o evento de forma estável entre reenvios:
     * sessão do boot atual (16 bytes -> 32 hex) + sequência monotônica
     * dentro dessa sessão.
     *
     * O servidor usa (bancada, evt_id) como chave de deduplicação forte
     * (ver postgres/init/001_schema.sql no repositório do servidor).
     * Sem evt_id, ele cai numa deduplicação mais frágil, só por
     * timestamp — reenviar o mesmo evento nunca deve contar duas vezes.
     */
    char session_text[33];

    mqtt_session_text(
        event->session,
        session_text
    );

    char evt_id[64]; // 32 (sessão) + '-' + até 20 digitos (uint64) + '\0' = 54 no pior caso.

    const int evt_id_written =
        snprintf(
            evt_id,
            sizeof(evt_id),
            "%s-%" PRIu64,
            session_text,
            event->sequence
        );

    if (evt_id_written < 0 ||
        (size_t)evt_id_written >= sizeof(evt_id)) {

        return false;
    }


    const int written =
        snprintf(
            buffer,
            size,

            "{"
            "\"bancada\":\"%s\","
            "\"evt_id\":\"%s\","
            "\"ts\":\"%s\","
            "\"delta\":1"
            "}",

            CONFIG_FLOWCOUNT_BANCADA,
            evt_id,
            timestamp
        );


    return written >= 0 &&
           (size_t)written < size;
}
bool mqtt_ack_parse(const char *buffer, size_t length, production_event_t *identity)
{
    if (!buffer || !identity || length == 0 || length >= MQTT_ACK_SIZE ||
        memchr(buffer, 0, length)) return false;
    char json[MQTT_ACK_SIZE];
    memcpy(json, buffer, length);
    json[length] = 0;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length + 1, NULL, true);
    if (!root) return false;
    bool valid = false;
    cJSON *station = cJSON_GetObjectItemCaseSensitive(root, "station");
    cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session");
    cJSON *sequence = cJSON_GetObjectItemCaseSensitive(root, "sequence");
    production_event_t ack = {0};
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 3 ||
        !cJSON_IsNumber(station) || station->valuedouble < 1 ||
        station->valuedouble > UINT32_MAX ||
        !cJSON_IsString(session) || strlen(session->valuestring) != 32 ||
        !cJSON_IsString(sequence)) goto done;
    ack.station = (uint32_t)station->valuedouble;
    if (station->valuedouble != ack.station) goto done;
    const char *digits = sequence->valuestring;
    if (digits[0] < '1' || digits[0] > '9') goto done;
    for (; *digits; ++digits) {
        if (*digits < '0' || *digits > '9' ||
            ack.sequence > (UINT64_MAX - (unsigned)(*digits - '0')) / 10) goto done;
        ack.sequence = ack.sequence * 10 + (unsigned)(*digits - '0');
    }
    for (unsigned i = 0; i < 32; ++i) {
        char c = session->valuestring[i];
        unsigned value;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else goto done;
        ack.session[i / 2] = (uint8_t)((ack.session[i / 2] << 4) | value);
    }
    *identity = ack;
    valid = true;
done:
    cJSON_Delete(root);
    return valid;
}
void delivery_begin(delivery_t *d, const production_event_t *event)
{
    *d = (delivery_t){ .event = *event, .active = true };
}
bool delivery_due(const delivery_t *d, int64_t now_us)
{
    return d->active && now_us >= d->retry_at_us;
}
void delivery_attempt(delivery_t *d, int64_t now_us, bool success, int timeout_s)
{
    if (success) d->published = true;
    d->retry_at_us = now_us + (success ? timeout_s : 5) * INT64_C(1000000);
}
bool delivery_ack_matches(const delivery_t *d, const production_event_t *ack)
{
    return d->active && d->published && d->event.station == ack->station &&
        d->event.sequence == ack->sequence &&
        memcmp(d->event.session, ack->session, 16) == 0;
}
