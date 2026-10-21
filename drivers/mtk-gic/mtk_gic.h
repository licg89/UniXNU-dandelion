/*
 * MTK GIC (Generic Interrupt Controller) Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * ARM GICv2 driver for MT6765
 * Handles all system interrupts
 */

#ifndef _MTK_GIC_H_
#define _MTK_GIC_H_

#include <stdint.h>
#include <stdbool.h>

/* GIC base addresses (typical MTK layout) */
#define MTK_GICD_BASE           0x10221000  /* Distributor */
#define MTK_GICC_BASE           0x10222000  /* CPU Interface */

/* GIC Distributor registers */
#define GICD_CTLR               0x0000  /* Distributor control */
#define GICD_TYPER              0x0004  /* Interrupt controller type */
#define GICD_IIDR               0x0008  /* Implementer identification */
#define GICD_IGROUPR(n)         (0x0080 + ((n) * 4))  /* Interrupt group */
#define GICD_ISENABLER(n)       (0x0100 + ((n) * 4))  /* Enable set */
#define GICD_ICENABLER(n)       (0x0180 + ((n) * 4))  /* Enable clear */
#define GICD_ISPENDR(n)         (0x0200 + ((n) * 4))  /* Pending set */
#define GICD_ICPENDR(n)         (0x0280 + ((n) * 4))  /* Pending clear */
#define GICD_ISACTIVER(n)       (0x0300 + ((n) * 4))  /* Active */
#define GICD_IPRIORITYR(n)      (0x0400 + ((n) * 4))  /* Priority */
#define GICD_ITARGETSR(n)       (0x0800 + ((n) * 4))  /* Target CPU */
#define GICD_ICFGR(n)           (0x0C00 + ((n) * 4))  /* Configuration */
#define GICD_SGIR               0x0F00  /* Software generated interrupt */

/* GIC CPU Interface registers */
#define GICC_CTLR               0x0000  /* CPU interface control */
#define GICC_PMR                0x0004  /* Priority mask */
#define GICC_BPR                0x0008  /* Binary point */
#define GICC_IAR                0x000C  /* Interrupt acknowledge */
#define GICC_EOIR               0x0010  /* End of interrupt */
#define GICC_RPR                0x0014  /* Running priority */
#define GICC_HPPIR              0x0018  /* Highest pending priority */

/* Interrupt types */
#define GIC_SGI_BASE            0   /* Software Generated Interrupts (0-15) */
#define GIC_PPI_BASE            16  /* Private Peripheral Interrupts (16-31) */
#define GIC_SPI_BASE            32  /* Shared Peripheral Interrupts (32-1019) */

/* Interrupt numbers for key peripherals */
#define GIC_IRQ_TIMER0          11  /* System timer */
#define GIC_IRQ_TIMER1          12
#define GIC_IRQ_UART0           51  /* UART */
#define GIC_IRQ_I2C0            106 /* I2C */
#define GIC_IRQ_I2C1            107
#define GIC_IRQ_I2C2            108
#define GIC_IRQ_I2C3            109 /* Touch controller */
#define GIC_IRQ_MSDC0           110 /* eMMC */
#define GIC_IRQ_MSDC1           111 /* SD card */
#define GIC_IRQ_PMIC            161 /* PMIC */
#define GIC_IRQ_TOUCH           17  /* Touch IRQ */

/* Priority levels */
#define GIC_PRIORITY_HIGHEST    0
#define GIC_PRIORITY_HIGH       32
#define GIC_PRIORITY_NORMAL     128
#define GIC_PRIORITY_LOW        224
#define GIC_PRIORITY_LOWEST     255

/* Interrupt handler type */
typedef void (*gic_irq_handler_t)(uint32_t irq, void *data);

/* GIC context */
typedef struct {
    volatile uint32_t *gicd_base;
    volatile uint32_t *gicc_base;
    uint32_t num_irqs;
    uint32_t num_cpus;
    gic_irq_handler_t handlers[1024];
    void *handler_data[1024];
    bool initialized;
} mtk_gic_t;

/* Function prototypes */
int mtk_gic_init(mtk_gic_t *gic);
int mtk_gic_enable_irq(mtk_gic_t *gic, uint32_t irq);
int mtk_gic_disable_irq(mtk_gic_t *gic, uint32_t irq);
int mtk_gic_set_priority(mtk_gic_t *gic, uint32_t irq, uint8_t priority);
int mtk_gic_set_target(mtk_gic_t *gic, uint32_t irq, uint8_t cpu);
int mtk_gic_register_handler(mtk_gic_t *gic, uint32_t irq, gic_irq_handler_t handler, void *data);
int mtk_gic_send_sgi(mtk_gic_t *gic, uint32_t sgi, uint8_t target_cpu);
void mtk_gic_handle_irq(mtk_gic_t *gic);

#endif /* _MTK_GIC_H_ */
