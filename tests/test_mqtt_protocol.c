#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "communication/mqtt_protocol.h"

#define SESSION "00112233445566778899aabbccddeeff"

static void test_payload(void)
{
    production_event_t event = {.clock_synced = 1, .timestamp_ms = INT64_C(1700000000123)};
    char payload[MQTT_PAYLOAD_SIZE];
    assert(mqtt_event_json(&event, payload, sizeof(payload)));
    assert(strcmp(payload, "{\"bancada\":\"B01\",\"evt_id\":\"00000000000000000000000000000000-0\",\"ts\":\"2023-11-14T22:13:20.123000Z\",\"delta\":1}") == 0);
    const size_t length = strlen(payload);
    assert(!mqtt_event_json(&event, payload, length));
    assert(mqtt_event_json(&event, payload, length + 1));
    assert(!mqtt_event_json(NULL, payload, sizeof(payload)));
    assert(!mqtt_event_json(&event, NULL, sizeof(payload)));
    assert(!mqtt_event_json(&event, payload, 0));
    event.clock_synced = 0;
    assert(!mqtt_event_json(&event, payload, sizeof(payload)));
    event.clock_synced = 1;
    event.timestamp_ms = 0;
    assert(!mqtt_event_json(&event, payload, sizeof(payload)));
    event.timestamp_ms = -1;
    assert(!mqtt_event_json(&event, payload, sizeof(payload)));
    event.timestamp_ms = INT64_C(1700000000000);
    assert(mqtt_event_json(&event, payload, sizeof(payload)));
    assert(strstr(payload, "20.000000Z"));
}

static void reject_ack(const char *json, size_t length)
{
    production_event_t identity;
    memset(&identity, 0xa5, sizeof(identity));
    production_event_t original = identity;
    assert(!mqtt_ack_parse(json, length, &identity));
    assert(memcmp(&identity, &original, sizeof(identity)) == 0);
}

static void test_ack(void)
{
    const char valid[] = "{\"station\":4294967295,\"session\":\"" SESSION "\",\"sequence\":\"18446744073709551615\"}";
    production_event_t identity;
    // A mensagem MQTT é delimitada por tamanho, sem exigir terminador NUL.
    char bytes[sizeof(valid) - 1];
    memcpy(bytes, valid, sizeof(bytes));
    assert(mqtt_ack_parse(bytes, sizeof(bytes), &identity));
    assert(identity.station == UINT32_MAX && identity.sequence == UINT64_MAX);
    char session[33];
    mqtt_session_text(identity.session, session);
    assert(strcmp(session, SESSION) == 0);
    assert(!mqtt_ack_parse(valid, strlen(valid), NULL));
    reject_ack(NULL, 10);
    reject_ack(valid, 0);
    reject_ack(valid, sizeof(valid)); // NUL dentro do payload.
    reject_ack(valid, strlen(valid) - 1);
    char oversized[MQTT_ACK_SIZE];
    memset(oversized, ' ', sizeof(oversized));
    reject_ack(oversized, sizeof(oversized));
    const char *invalid[] = {
        "{}", "[]", "null", "not json",
        "{\"station\":1,\"session\":\"" SESSION "\"}",
        "{\"station\":1,\"session\":\"" SESSION "\",\"sequence\":\"1\",\"extra\":0}",
        "{\"station\":1,\"session\":\"" SESSION "\",\"sequence\":\"1\"} trailing",
        "{\"station\":1,\"station\":2,\"session\":\"" SESSION "\",\"sequence\":\"1\"}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        reject_ack(invalid[i], strlen(invalid[i]));
    const char *stations[] = {"0", "-1", "1.5", "4294967296", "\"1\"", "null"};
    char json[MQTT_ACK_SIZE];
    for (size_t i = 0; i < sizeof(stations) / sizeof(stations[0]); ++i) {
        snprintf(json, sizeof(json), "{\"station\":%s,\"session\":\"" SESSION "\",\"sequence\":\"1\"}", stations[i]);
        reject_ack(json, strlen(json));
    }
    const char *sequences[] = {"\"\"", "\"0\"", "\"01\"", "\"-1\"", "\"1.5\"", "1", "null", "\"18446744073709551616\"", "\"1x\""};
    for (size_t i = 0; i < sizeof(sequences) / sizeof(sequences[0]); ++i) {
        snprintf(json, sizeof(json), "{\"station\":1,\"session\":\"" SESSION "\",\"sequence\":%s}", sequences[i]);
        reject_ack(json, strlen(json));
    }
    const char *sessions[] = {"", "0011", "00112233445566778899AABBCCDDEEFF", "00112233445566778899aabbccddeefg"};
    for (size_t i = 0; i < sizeof(sessions) / sizeof(sessions[0]); ++i) {
        snprintf(json, sizeof(json), "{\"station\":1,\"session\":\"%s\",\"sequence\":\"1\"}", sessions[i]);
        reject_ack(json, strlen(json));
    }
}

static void test_topics_and_delivery(void)
{
    assert(mqtt_topic_prefix_valid("flowcount-A_09"));
    char prefix[34];
    memset(prefix, 'a', 33);
    prefix[33] = 0;
    assert(!mqtt_topic_prefix_valid(prefix));
    prefix[32] = 0;
    assert(mqtt_topic_prefix_valid(prefix));
    const char *invalid[] = {"", "a/b", "+", "#", "has space"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!mqtt_topic_prefix_valid(invalid[i]));

    production_event_t event = {.station = 7, .sequence = 42, .session = {1}};
    production_event_t ack = event;
    delivery_t delivery = {0};
    assert(!delivery_due(&delivery, 0));
    assert(!delivery_ack_matches(&delivery, &ack));
    delivery_begin(&delivery, &event);
    event.sequence = 99; // A tentativa guarda uma cópia do evento.
    assert(delivery.event.sequence == 42 && delivery_due(&delivery, 0));
    assert(!delivery_ack_matches(&delivery, &ack));
    delivery_attempt(&delivery, 100, false, 30);
    assert(!delivery_due(&delivery, 5000099));
    assert(delivery_due(&delivery, 5000100));
    assert(!delivery_ack_matches(&delivery, &ack));
    delivery_attempt(&delivery, 5000100, true, 30);
    assert(!delivery_due(&delivery, 35000099));
    assert(delivery_due(&delivery, 35000100));
    assert(delivery_ack_matches(&delivery, &ack));
    ++ack.station;
    assert(!delivery_ack_matches(&delivery, &ack));
    --ack.station;
    ++ack.sequence;
    assert(!delivery_ack_matches(&delivery, &ack));
    --ack.sequence;
    ++ack.session[15];
    assert(!delivery_ack_matches(&delivery, &ack));
    --ack.session[15];
    // ACK atrasado continua válido após uma tentativa de reenvio falhar.
    delivery_attempt(&delivery, 35000100, false, 30);
    assert(delivery_ack_matches(&delivery, &ack));
    delivery_begin(&delivery, &event);
    assert(!delivery.published && delivery.retry_at_us == 0);
    assert(!delivery_ack_matches(&delivery, &ack));
}

int main(void)
{
    test_payload();
    test_ack();
    test_topics_and_delivery();
    puts("OK: MQTT payload UTC/buffers, ACK valido/invalido/overflow, topicos, retry e identidade.");
    return 0;
}
