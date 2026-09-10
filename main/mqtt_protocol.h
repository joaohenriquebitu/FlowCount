#ifndef FLOWCOUNT_MQTT_PROTOCOL_H
#define FLOWCOUNT_MQTT_PROTOCOL_H
#include <stddef.h>
#include "production_event.h"
#define MQTT_PAYLOAD_SIZE 512
#define MQTT_ACK_SIZE 256
#define MQTT_TOPIC_SIZE 64
void mqtt_session_text(const uint8_t session[16], char text[33]);
bool mqtt_event_json(const production_event_t *event, char *buffer, size_t size);
bool mqtt_ack_parse(const char *buffer, size_t length, production_event_t *identity);
bool mqtt_topic_prefix_valid(const char *prefix);
// Stop-and-wait: cópia do evento retida também na fila até ACK aplicativo.
typedef struct {
    production_event_t event;
    bool active;
    bool published;
    int64_t retry_at_us;
} delivery_t;
void delivery_begin(delivery_t *delivery, const production_event_t *event);
bool delivery_due(const delivery_t *delivery, int64_t now_us);
void delivery_attempt(delivery_t *delivery, int64_t now_us, bool success, int timeout_s);
bool delivery_ack_matches(const delivery_t *delivery, const production_event_t *ack);
#endif
