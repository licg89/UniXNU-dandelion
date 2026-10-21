/*
 * MTK GIC (Generic Interrupt Controller) Driver Implementation
 */

#include "mtk_gic.h"
#include <stdio.h>
#include <string.h>

/*
 * Initialize GIC
 */
int mtk_gic_init(mtk_gic_t *gic)
{
    if (!gic) {
        return -1;
    }

    gic->gicd_base = (volatile uint32_t *)MTK_GICD_BASE;
    gic->gicc_base = (volatile uint32_t *)MTK_GICC_BASE;
    gic->initialized = false;

    printf("MTK GIC: Initializing...\n");
    printf("  GICD base: 0x%08X\n", MTK_GICD_BASE);
    printf("  GICC base: 0x%08X\n", MTK_GICC_BASE);

    /* Clear handlers */
    memset(gic->handlers, 0, sizeof(gic->handlers));
    memset(gic->handler_data, 0, sizeof(gic->handler_data));

    /* Read GIC type */
    uint32_t typer = gic->gicd_base[GICD_TYPER >> 2];
    gic->num_irqs = ((typer & 0x1F) + 1) * 32;
    gic->num_cpus = ((typer >> 5) & 0x07) + 1;

    printf("  IRQs: %u\n", gic->num_irqs);
    printf("  CPUs: %u\n", gic->num_cpus);

    /* Disable distributor */
    gic->gicd_base[GICD_CTLR >> 2] = 0;

    /* Disable all interrupts */
    for (uint32_t i = 1; i < gic->num_irqs / 32; i++) {
        gic->gicd_base[GICD_ICENABLER(i) >> 2] = 0xFFFFFFFF;
    }

    /* Set all interrupts to group 1 (non-secure) */
    for (uint32_t i = 1; i < gic->num_irqs / 32; i++) {
        gic->gicd_base[GICD_IGROUPR(i) >> 2] = 0xFFFFFFFF;
    }

    /* Set default priority for all interrupts */
    for (uint32_t i = 0; i < gic->num_irqs / 4; i++) {
        gic->gicd_base[GICD_IPRIORITYR(i) >> 2] = 0x80808080;
    }

    /* Set all interrupts to target CPU 0 */
    for (uint32_t i = 0; i < gic->num_irqs / 4; i++) {
        gic->gicd_base[GICD_ITARGETSR(i) >> 2] = 0x01010101;
    }

    /* Set all SPIs as level-triggered */
    for (uint32_t i = 2; i < gic->num_irqs / 16; i++) {
        gic->gicd_base[GICD_ICFGR(i) >> 2] = 0x00000000;
    }

    /* Enable distributor */
    gic->gicd_base[GICD_CTLR >> 2] = 1;

    /* Configure CPU interface */
    gic->gicc_base[GICC_PMR >> 2] = 0xFF;  /* Accept all priorities */
    gic->gicc_base[GICC_BPR >> 2] = 0x00;  /* No preemption */

    /* Enable CPU interface */
    gic->gicc_base[GICC_CTLR >> 2] = 1;

    gic->initialized = true;
    printf("MTK GIC: Initialized\n");

    return 0;
}

/*
 * Enable interrupt
 */
int mtk_gic_enable_irq(mtk_gic_t *gic, uint32_t irq)
{
    if (!gic || !gic->initialized || irq >= gic->num_irqs) {
        return -1;
    }

    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;

    gic->gicd_base[GICD_ISENABLER(reg) >> 2] = (1 << bit);

    return 0;
}

/*
 * Disable interrupt
 */
int mtk_gic_disable_irq(mtk_gic_t *gic, uint32_t irq)
{
    if (!gic || !gic->initialized || irq >= gic->num_irqs) {
        return -1;
    }

    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;

    gic->gicd_base[GICD_ICENABLER(reg) >> 2] = (1 << bit);

    return 0;
}

/*
 * Set interrupt priority
 */
int mtk_gic_set_priority(mtk_gic_t *gic, uint32_t irq, uint8_t priority)
{
    if (!gic || !gic->initialized || irq >= gic->num_irqs) {
        return -1;
    }

    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;

    uint32_t val = gic->gicd_base[GICD_IPRIORITYR(reg) >> 2];
    val &= ~(0xFF << shift);
    val |= (priority << shift);
    gic->gicd_base[GICD_IPRIORITYR(reg) >> 2] = val;

    return 0;
}

/*
 * Set interrupt target CPU
 */
int mtk_gic_set_target(mtk_gic_t *gic, uint32_t irq, uint8_t cpu)
{
    if (!gic || !gic->initialized || irq >= gic->num_irqs || cpu >= gic->num_cpus) {
        return -1;
    }

    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;

    uint32_t val = gic->gicd_base[GICD_ITARGETSR(reg) >> 2];
    val &= ~(0xFF << shift);
    val |= (cpu << shift);
    gic->gicd_base[GICD_ITARGETSR(reg) >> 2] = val;

    return 0;
}

/*
 * Register interrupt handler
 */
int mtk_gic_register_handler(mtk_gic_t *gic, uint32_t irq, gic_irq_handler_t handler, void *data)
{
    if (!gic || !gic->initialized || irq >= gic->num_irqs) {
        return -1;
    }

    gic->handlers[irq] = handler;
    gic->handler_data[irq] = data;

    return 0;
}

/*
 * Send software generated interrupt (SGI)
 */
int mtk_gic_send_sgi(mtk_gic_t *gic, uint32_t sgi, uint8_t target_cpu)
{
    if (!gic || !gic->initialized || sgi > 15 || target_cpu >= gic->num_cpus) {
        return -1;
    }

    /* SGI register format: [25:24] = target list filter, [23:16] = target list, [3:0] = SGI ID */
    uint32_t val = (1 << 24) | (1 << target_cpu) | sgi;
    gic->gicd_base[GICD_SGIR >> 2] = val;

    return 0;
}

/*
 * Handle interrupt (called from exception handler)
 */
void mtk_gic_handle_irq(mtk_gic_t *gic)
{
    if (!gic || !gic->initialized) {
        return;
    }

    /* Read interrupt ID */
    uint32_t iar = gic->gicc_base[GICC_IAR >> 2];
    uint32_t irq = iar & 0x3FF;

    /* Check for spurious interrupt */
    if (irq >= 1020) {
        return;
    }

    /* Call handler if registered */
    if (gic->handlers[irq]) {
        gic->handlers[irq](irq, gic->handler_data[irq]);
    } else {
        printf("MTK GIC: Unhandled IRQ %u\n", irq);
    }

    /* Signal end of interrupt */
    gic->gicc_base[GICC_EOIR >> 2] = iar;
}
