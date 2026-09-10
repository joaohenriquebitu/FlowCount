#include "counter.h"

void counter_init(counter_t *c, bool present, int64_t now_us)
{
    *c = (counter_t) {
        .state = COUNTER_WAIT_CLEAR,
        .stable_since_us = now_us,
        .occupied_since_us = present ? now_us : -1,
        .last_sample_us = now_us,
        .last_present = present,
    };
}

unsigned counter_update(counter_t *c, bool present, bool activity, int64_t now_us)
{
    if (now_us < c->last_sample_us ||
        now_us - c->last_sample_us > COUNTER_MAX_SAMPLE_GAP_US) {
        // Sem observação suficiente, não presumir que o ciclo foi válido.
        counter_init(c, present, now_us);
        return COUNTER_RESYNC;
    }
    c->last_sample_us = now_us;
    if (activity || present != c->last_present) {
        c->stable_since_us = now_us;
    }
    c->last_present = present;
    const int64_t stable_us = now_us - c->stable_since_us;
    unsigned result = COUNTER_NONE;

    switch (c->state) {
    case COUNTER_WAIT_CLEAR:
        if (present && c->occupied_since_us < 0) {
            c->occupied_since_us = now_us;
        }
        if (!present && stable_us >= COUNTER_RELEASE_US) {
            c->state = COUNTER_FREE;
            result |= COUNTER_READY;
        }
        break;
    case COUNTER_FREE:
        if (present) {
            c->state = COUNTER_CONFIRM_PRESENCE;
            c->occupied_since_us = now_us;
        }
        break;
    case COUNTER_CONFIRM_PRESENCE:
        if (!present) {
            c->state = COUNTER_FREE;
            c->occupied_since_us = -1;
        } else if (stable_us >= COUNTER_PRESENCE_US) {
            c->state = COUNTER_PRESENT;
        }
        break;
    case COUNTER_PRESENT:
        if (!present) {
            c->state = COUNTER_CONFIRM_RELEASE;
        }
        break;
    case COUNTER_CONFIRM_RELEASE:
        if (present) {
            c->state = COUNTER_PRESENT;
        } else if (stable_us >= COUNTER_RELEASE_US) {
            c->state = COUNTER_FREE;
            result |= COUNTER_COUNT;
        }
        break;
    }

    if (c->state == COUNTER_FREE) {
        if (c->blocked) {
            result |= COUNTER_CLEARED;
        }
        c->blocked = false;
        c->occupied_since_us = -1;
    } else if (!c->blocked && c->occupied_since_us >= 0 &&
               now_us - c->occupied_since_us >= COUNTER_BLOCKED_US) {
        c->blocked = true;
        result |= COUNTER_BLOCKED;
    }
    return result;
}
