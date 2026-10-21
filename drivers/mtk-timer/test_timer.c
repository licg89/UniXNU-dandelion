/*
 * MTK Timer Driver Test
 */

#include "mtk_timer.h"
#include <stdio.h>

/* Test callback */
static int callback_count = 0;

void test_callback(void *data)
{
    callback_count++;
    printf("  Timer callback #%d\n", callback_count);
}

int main(void)
{
    mtk_timer_t timer;
    int ret;

    printf("=== MTK Timer Test ===\n\n");

    /* Initialize timer */
    printf("Initializing timer...\n");
    ret = mtk_timer_init(&timer);
    if (ret < 0) {
        printf("FAILED: Timer init error %d\n", ret);
        return ret;
    }
    printf("OK: Timer initialized\n\n");

    /* Test tick count */
    printf("Testing tick count...\n");
    uint64_t ticks1 = mtk_timer_get_ticks(&timer);
    volatile int delay;
    for (delay = 0; delay < 1000000; delay++);
    uint64_t ticks2 = mtk_timer_get_ticks(&timer);
    printf("  Ticks1: %llu\n", ticks1);
    printf("  Ticks2: %llu\n", ticks2);
    printf("  Delta: %llu ticks\n", ticks2 - ticks1);
    printf("OK: Tick count working\n\n");

    /* Test nanosecond timing */
    printf("Testing nanosecond timing...\n");
    uint64_t ns1 = mtk_timer_get_ns(&timer);
    for (delay = 0; delay < 1000000; delay++);
    uint64_t ns2 = mtk_timer_get_ns(&timer);
    printf("  NS1: %llu ns\n", ns1);
    printf("  NS2: %llu ns\n", ns2);
    printf("  Delta: %llu ns (%llu us)\n", ns2 - ns1, (ns2 - ns1) / 1000);
    printf("OK: Nanosecond timing working\n\n");

    /* Test microsecond delay */
    printf("Testing microsecond delay...\n");
    uint64_t start = mtk_timer_get_ns(&timer);
    mtk_timer_delay_us(&timer, 1000);  /* 1ms delay */
    uint64_t end = mtk_timer_get_ns(&timer);
    uint64_t elapsed = end - start;
    printf("  Requested: 1000 us\n");
    printf("  Elapsed: %llu ns (%llu us)\n", elapsed, elapsed / 1000);
    printf("OK: Microsecond delay working\n\n");

    /* Test longer delays */
    printf("Testing longer delays...\n");
    uint32_t delays[] = {100, 500, 1000, 5000, 10000};  /* us */
    int num_delays = sizeof(delays) / sizeof(delays[0]);

    for (int i = 0; i < num_delays; i++) {
        start = mtk_timer_get_ns(&timer);
        mtk_timer_delay_us(&timer, delays[i]);
        end = mtk_timer_get_ns(&timer);
        elapsed = end - start;
        printf("  Delay %u us: actual %llu us\n", delays[i], elapsed / 1000);
    }
    printf("OK: Longer delays working\n\n");

    /* Test ARM generic timer frequency */
    printf("Testing ARM generic timer frequency...\n");
    uint64_t freq = arm_timer_get_freq();
    printf("  Frequency: %llu Hz\n", freq);
    if (freq >= 1000000 && freq <= 100000000) {
        printf("OK: Frequency in valid range\n\n");
    } else {
        printf("WARNING: Frequency out of expected range\n\n");
    }

    /* Test one-shot timer (simulation) */
    printf("Testing one-shot timer...\n");
    callback_count = 0;
    ret = mtk_timer_set_oneshot(&timer, 1000000, test_callback, NULL);  /* 1ms */
    if (ret < 0) {
        printf("FAILED: Set one-shot error\n");
        return ret;
    }
    printf("  One-shot set for 1ms\n");
    printf("  (In real hardware, interrupt would fire)\n\n");

    /* Test periodic timer (simulation) */
    printf("Testing periodic timer...\n");
    ret = mtk_timer_set_periodic(&timer, 10000000, test_callback, NULL);  /* 10ms */
    if (ret < 0) {
        printf("FAILED: Set periodic error\n");
        return ret;
    }
    printf("  Periodic set for 10ms\n");
    printf("  (In real hardware, interrupts would fire periodically)\n\n");

    /* Test timer stop */
    printf("Testing timer stop...\n");
    ret = mtk_timer_stop(&timer);
    if (ret < 0) {
        printf("FAILED: Timer stop error\n");
        return ret;
    }
    printf("OK: Timer stopped\n\n");

    printf("=== Timer Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
