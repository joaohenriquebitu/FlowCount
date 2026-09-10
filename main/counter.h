#ifndef FLOWCOUNT_COUNTER_H
#define FLOWCOUNT_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

// Valores iniciais de bancada; calibrar conforme geometria e sinal reais.
#define COUNTER_PRESENCE_US INT64_C(30000)
#define COUNTER_RELEASE_US INT64_C(50000)
#define COUNTER_BLOCKED_US INT64_C(5000000)
#define COUNTER_MAX_SAMPLE_GAP_US INT64_C(100000)

typedef enum {
    COUNTER_WAIT_CLEAR, // Partida/ressincronização: não contar peça já presente.
    COUNTER_FREE,
    COUNTER_CONFIRM_PRESENCE,
    COUNTER_PRESENT,
    COUNTER_CONFIRM_RELEASE
} counter_state_t;

enum {
    COUNTER_NONE = 0,
    COUNTER_COUNT = 1 << 0,
    COUNTER_BLOCKED = 1 << 1,
    COUNTER_CLEARED = 1 << 2,
    COUNTER_READY = 1 << 3,
    COUNTER_RESYNC = 1 << 4
};

typedef struct {
    counter_state_t state;
    int64_t stable_since_us;
    int64_t occupied_since_us;
    int64_t last_sample_us;
    bool last_present;
    bool blocked;
} counter_t;

void counter_init(counter_t *counter, bool present, int64_t now_us);
// Chamador único. activity indica qualquer borda desde a amostra anterior,
// inclusive oscilações que terminaram no mesmo nível. Tempo monotônico em us.
unsigned counter_update(counter_t *counter, bool present, bool activity,
                        int64_t now_us);
#endif
