/*
 * MTK Timer Driver Implementation
 */

#include "mtk_timer.h"
#include <stdio.h>

/* Read ARM system registers */
static inline uint64_t read_sysreg(const char *reg)
{
    uint64_t val;
    if (reg == CNTFRQ_EL0) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    } else if (reg == CNTPCT_EL0) {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    } else if (reg == CNTVCT_EL0) {
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    }
    return val;
}

/* Write ARM system registers */
static inline void write_sysreg(const char *reg, uint64_t val)
{
    if (reg == CNTP_CVAL_EL0) {
        __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(val));
    } else if (reg == CNTP_TVAL_EL0) {
        __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(val));
    } else if (reg == CNTP_CTL_EL0) {
        __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(val));
    }
}

/*
 * Initialize MTK timer
 */
int mtk_timer_init(mtk_timer_t *timer)
{
    if (!timer) {
        return -1;
    }

    timer->base = (volatile uint32_t *)MTK_TIMER_BASE;
    timer->initialized = false;
    timer->tick_count = 0;

    /* Get timer frequency from ARM generic timer */
    timer->freq = arm_timer_get_freq();
    if (timer->freq == 0) {
        printf("MTK Timer: Could not get frequency\n");
        return -1;
    }

    timer->ns_per_tick = 1000000000ULL / timer->freq;

    printf("MTK Timer: Initializing...\n");
    printf("  Base: 0x%08X\n", MTK_TIMER_BASE);
    printf("  Frequency: %u Hz\n", timer->freq);
    printf("  ns/tick: %llu\n", timer->ns_per_tick);

    /* Reset timers */
    timer->base[TIMER_RST_CON >> 2] = 0x01;
    __asm__ volatile("" ::: "memory");
    timer->base[TIMER_RST_CON >> 2] = 0x00;

    /* Configure timer 0 for free running */
    timer->base[TIMER_CON >> 2] = TIMER_CON_MODE_FREERUN;

    /* Enable timer 0 */
    timer->base[TIMER_CON >> 2] |= TIMER_CON_EN;

    timer->initialized = true;
    printf("MTK Timer: Initialized\n");

    return 0;
}

/*
 * Get current tick count
 */
uint64_t mtk_timer_get_ticks(mtk_timer_t *timer)
{
    if (!timer || !timer->initialized) {
        return 0;
    }

    return arm_timer_get_count();
}

/*
 * Get time in nanoseconds
 */
uint64_t mtk_timer_get_ns(mtk_timer_t *timer)
{
    if (!timer || !timer->initialized) {
        return 0;
    }

    return arm_timer_get_ns();
}

/*
 * Delay in microseconds
 */
void mtk_timer_delay_us(mtk_timer_t *timer, uint32_t us)
{
    if (!timer || !timer->initialized) {
        return;
    }

    uint64_t start = mtk_timer_get_ticks(timer);
    uint64_t ticks = (uint64_t)us * timer->freq / 1000000ULL;

    while ((mtk_timer_get_ticks(timer) - start) < ticks) {
        __asm__ volatile("" ::: "memory");
    }
}

/*
 * Set one-shot timer
 */
int mtk_timer_set_oneshot(mtk_timer_t *timer, uint64_t ns, timer_callback_t cb, void *data)
{
    if (!timer || !timer->initialized) {
        return -1;
    }

    timer->callback = cb;
    timer->callback_data = data;

    uint64_t ticks = ns / timer->ns_per_tick;
    arm_timer_set_compare(ticks);

    return 0;
}

/*
 * Set periodic timer
 */
int mtk_timer_set_periodic(mtk_timer_t *timer, uint64_t ns, timer_callback_t cb, void *data)
{
    if (!timer || !timer->initialized) {
        return -1;
    }

    timer->callback = cb;
    timer->callback_data = data;

    uint64_t ticks = ns / timer->ns_per_tick;
    arm_timer_set_compare(ticks);

    return 0;
}

/*
 * Stop timer
 */
int mtk_timer_stop(mtk_timer_t *timer)
{
    if (!timer || !timer->initialized) {
        return -1;
    }

    arm_timer_disable();
    timer->callback = NULL;
    timer->callback_data = NULL;

    return 0;
}

/*
 * ARM generic timer: Get frequency
 */
uint64_t arm_timer_get_freq(void)
{
    return read_sysreg(CNTFRQ_EL0);
}

/*
 * ARM generic timer: Get counter value
 */
uint64_t arm_timer_get_count(void)
{
    return read_sysreg(CNTPCT_EL0);
}

/*
 * ARM generic timer: Get time in nanoseconds
 */
uint64_t arm_timer_get_ns(void)
{
    uint64_t count = arm_timer_get_count();
    uint64_t freq = arm_timer_get_freq();
    return (count * 1000000000ULL) / freq;
}

/*
 * ARM generic timer: Set compare value
 */
void arm_timer_set_compare(uint64_t ticks)
{
    uint64_t current = arm_timer_get_count();
    write_sysreg(CNTP_CVAL_EL0, current + ticks);
}

/*
 * ARM generic timer: Enable timer
 */
void arm_timer_enable(void)
{
    write_sysreg(CNTP_CTL_EL0, CNTP_CTL_ENABLE);
}

/*
 * ARM generic timer: Disable timer
 */
void arm_timer_disable(void)
{
    write_sysreg(CNTP_CTL_EL0, 0);
}
