/*
 * MTK Timer Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * MediaTek timer driver for MT6765
 * Supports ARM generic timer and MTK system timer
 */

#ifndef _MTK_TIMER_H_
#define _MTK_TIMER_H_

#include <stdint.h>
#include <stdbool.h>

/* MTK Timer base addresses */
#define MTK_TIMER_BASE          0x10008000
#define MTK_TIMER_SIZE          0x1000

/* MTK Timer registers */
#define TIMER_CON               0x00    /* Control */
#define TIMER_CLK_CON           0x04    /* Clock control */
#define TIMER_RST_CON           0x08    /* Reset control */
#define TIMER_INT_EN            0x0C    /* Interrupt enable */
#define TIMER_INT_STA           0x10    /* Interrupt status */
#define TIMER_VAL0              0x80    /* Timer 0 value */
#define TIMER_CMP0              0x84    /* Timer 0 compare */
#define TIMER_VAL1              0x88    /* Timer 1 value */
#define TIMER_CMP1              0x8C    /* Timer 1 compare */
#define TIMER_VAL2              0x90    /* Timer 2 value */
#define TIMER_CMP2              0x94    /* Timer 2 compare */
#define TIMER_VAL3              0x98    /* Timer 3 value */
#define TIMER_CMP3              0x9C    /* Timer 3 compare */

/* Timer control bits */
#define TIMER_CON_EN            (1 << 0)    /* Timer enable */
#define TIMER_CON_CLR           (1 << 1)    /* Clear timer */
#define TIMER_CON_MODE_MASK     (3 << 4)    /* Mode mask */
#define TIMER_CON_MODE_FREERUN  (0 << 4)    /* Free running */
#define TIMER_CON_MODE_REPEAT   (1 << 4)    /* Repeat */
#define TIMER_CON_MODE_ONESHOT  (2 << 4)    /* One-shot */

/* Clock control */
#define TIMER_CLK_13M           0
#define TIMER_CLK_26M           1
#define TIMER_CLK_1K            2

/* ARM Generic Timer registers (system registers) */
#define CNTFRQ_EL0              "CNTFRQ_EL0"      /* Counter frequency */
#define CNTPCT_EL0              "CNTPCT_EL0"      /* Physical counter */
#define CNTP_CVAL_EL0           "CNTP_CVAL_EL0"   /* Physical timer compare */
#define CNTP_TVAL_EL0           "CNTP_TVAL_EL0"   /* Physical timer value */
#define CNTP_CTL_EL0            "CNTP_CTL_EL0"    /* Physical timer control */
#define CNTVCT_EL0              "CNTVCT_EL0"      /* Virtual counter */
#define CNTV_CVAL_EL0           "CNTV_CVAL_EL0"   /* Virtual timer compare */
#define CNTV_TVAL_EL0           "CNTV_TVAL_EL0"   /* Virtual timer value */
#define CNTV_CTL_EL0            "CNTV_CTL_EL0"    /* Virtual timer control */

/* Timer control bits (ARM) */
#define CNTP_CTL_ENABLE         (1 << 0)
#define CNTP_CTL_IMASK          (1 << 1)
#define CNTP_CTL_ISTATUS        (1 << 2)

/* Timer callback type */
typedef void (*timer_callback_t)(void *data);

/* Timer context */
typedef struct {
    volatile uint32_t *base;
    uint32_t freq;            /* Timer frequency in Hz */
    uint64_t tick_count;      /* Total ticks */
    uint64_t ns_per_tick;     /* Nanoseconds per tick */
    bool initialized;
    timer_callback_t callback;
    void *callback_data;
} mtk_timer_t;

/* Function prototypes */
int mtk_timer_init(mtk_timer_t *timer);
uint64_t mtk_timer_get_ticks(mtk_timer_t *timer);
uint64_t mtk_timer_get_ns(mtk_timer_t *timer);
void mtk_timer_delay_us(mtk_timer_t *timer, uint32_t us);
int mtk_timer_set_oneshot(mtk_timer_t *timer, uint64_t ns, timer_callback_t cb, void *data);
int mtk_timer_set_periodic(mtk_timer_t *timer, uint64_t ns, timer_callback_t cb, void *data);
int mtk_timer_stop(mtk_timer_t *timer);

/* ARM generic timer functions */
uint64_t arm_timer_get_freq(void);
uint64_t arm_timer_get_count(void);
uint64_t arm_timer_get_ns(void);
void arm_timer_set_compare(uint64_t ticks);
void arm_timer_enable(void);
void arm_timer_disable(void);

#endif /* _MTK_TIMER_H_ */
