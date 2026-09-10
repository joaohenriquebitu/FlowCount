#include <assert.h>
#include <stdio.h>
#include "counter.h"

static counter_t c;
static int64_t now;
static unsigned counts, blocked, cleared, ready, resync;

static unsigned sample(bool present, bool activity, int64_t advance)
{
    now += advance;
    const unsigned r = counter_update(&c, present, activity, now);
    counts += !!(r & COUNTER_COUNT);
    blocked += !!(r & COUNTER_BLOCKED);
    cleared += !!(r & COUNTER_CLEARED);
    ready += !!(r & COUNTER_READY);
    resync += !!(r & COUNTER_RESYNC);
    return r;
}

static void hold(bool present, unsigned ms)
{
    assert(ms % 10 == 0);
    for (unsigned i = 0; i < ms; i += 10) {
        sample(present, false, 10000);
    }
}

static void start(bool present)
{
    now = 0;
    counts = blocked = cleared = ready = resync = 0;
    counter_init(&c, present, now);
}

static void arm(void)
{
    start(false);
    hold(false, 50);
    assert(ready == 1);
}

static void test_normal_and_thresholds(void)
{
    arm();
    sample(true, true, 10000);
    sample(true, false, COUNTER_PRESENCE_US - 1);
    assert(c.state == COUNTER_CONFIRM_PRESENCE && counts == 0);
    sample(true, false, 1);
    assert(c.state == COUNTER_PRESENT && counts == 0);
    sample(false, true, 10000);
    sample(false, false, COUNTER_RELEASE_US - 1);
    assert(counts == 0);
    sample(false, false, 1);
    assert(counts == 1 && c.state == COUNTER_FREE);
    hold(false, 1000);
    assert(counts == 1);
}

static void test_stopped_piece(void)
{
    arm();
    for (unsigned i = 0; i < 10; ++i) {
        sample(true, true, 10000);
        hold(true, 6000);
        assert(counts == i && blocked == i + 1);
        // Liberação curta não encerra o bloqueio e não conta.
        sample(false, true, 10000);
        hold(false, 20);
        sample(true, true, 10000);
        hold(true, 1000);
        assert(counts == i && blocked == i + 1 && cleared == i);
        sample(false, true, 10000);
        hold(false, 50);
        assert(counts == i + 1 && cleared == i + 1);
    }
}

static void test_noise(void)
{
    arm();
    for (unsigned i = 0; i < 100; ++i) {
        sample(true, true, 10000);
        hold(true, 10);
        sample(false, true, 10000);
        hold(false, 100);
    }
    assert(counts == 0);
    sample(true, true, 10000);
    // Bordas entre amostras, voltando ao mesmo nível, reiniciam confirmação.
    for (unsigned i = 0; i < 100; ++i) {
        sample(true, true, 20000);
    }
    assert(c.state == COUNTER_CONFIRM_PRESENCE && counts == 0);
    hold(true, 30);
    assert(c.state == COUNTER_PRESENT);
    sample(false, true, 10000);
    for (unsigned i = 0; i < 100; ++i) {
        sample(false, true, 40000);
    }
    assert(counts == 0);
    hold(false, 50);
    assert(counts == 1);
}

static void test_boot_occupied(void)
{
    start(true);
    hold(true, 6000);
    assert(counts == 0 && blocked == 1 && ready == 0);
    sample(false, true, 10000);
    hold(false, 20);
    sample(true, true, 10000);
    hold(true, 100);
    assert(ready == 0);
    sample(false, true, 10000);
    hold(false, 50);
    assert(counts == 0 && ready == 1 && cleared == 1);
    sample(true, true, 10000);
    hold(true, 100);
    sample(false, true, 10000);
    hold(false, 50);
    assert(counts == 1);
}

static void test_200_at_30_per_minute(void)
{
    arm();
    for (unsigned i = 0; i < 200; ++i) {
        sample(true, true, 10000);
        hold(true, 190);
        sample(false, true, 10000);
        hold(false, 1790);
        assert(counts == i + 1);
    }
    assert(blocked == 0 && resync == 0);
}

static void test_gap_and_recovery(void)
{
    arm();
    sample(true, true, 10000);
    hold(true, 100);
    sample(false, true, COUNTER_MAX_SAMPLE_GAP_US + 1);
    hold(false, 50);
    assert(resync == 1 && counts == 0 && ready == 2);
    sample(true, true, 10000);
    hold(true, 100);
    sample(false, true, 10000);
    hold(false, 50);
    assert(counts == 1);
}

int main(void)
{
    test_normal_and_thresholds();
    test_stopped_piece();
    test_noise();
    test_boot_occupied();
    test_200_at_30_per_minute();
    test_gap_and_recovery();
    puts("OK: 6 cenarios (limiares, 10 bloqueios, ruido, partida, 200 ciclos, lacuna).");
    return 0;
}
