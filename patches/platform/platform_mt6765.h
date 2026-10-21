/*
 * XNU Platform Configuration for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Platform-specific definitions for MediaTek MT6765 (Helio G25)
 */

#ifndef _PLATFORM_MT6765_H_
#define _PLATFORM_MT6765_H_

/* SoC identification */
#define PLATFORM_SOC_VENDOR     "MediaTek"
#define PLATFORM_SOC_NAME       "MT6765"
#define PLATFORM_SOC_FAMILY     "Helio G25"

/* CPU configuration */
#define PLATFORM_CPU_TYPE       0x01  /* ARM64 */
#define PLATFORM_CPU_SUBTYPE    0x02  /* Cortex-A53 */
#define PLATFORM_CPU_COUNT      4
#define PLATFORM_CPU_FREQ_HZ    2000000000ULL  /* 2.0 GHz */

/* Memory configuration */
#define PLATFORM_RAM_BASE       0x40000000ULL
#define PLATFORM_RAM_SIZE       0xFCC00000ULL  /* ~4GB */
#define PLATFORM_KERNEL_BASE    0x40080000ULL
#define PLATFORM_KERNEL_SIZE    0x02000000ULL  /* 32MB */
#define PLATFORM_KERNEL_END     (PLATFORM_KERNEL_BASE + PLATFORM_KERNEL_SIZE)

/* Page table configuration */
#define PLATFORM_PAGE_SIZE      16384  /* 16KB pages (iOS standard) */
#define PLATFORM_PAGE_SHIFT     14
#define PLATFORM_TTBR0_BASE     0x44000000ULL  /* Page table base */

/* Interrupt controller */
#define PLATFORM_GICD_BASE      0x10221000ULL
#define PLATFORM_GICC_BASE      0x10222000ULL
#define PLATFORM_GIC_VERSION    2

/* Timer */
#define PLATFORM_TIMER_BASE     0x10008000ULL
#define PLATFORM_TIMER_FREQ     13000000ULL  /* 13MHz typical */

/* UART */
#define PLATFORM_UART_BASE      0x11002000ULL
#define PLATFORM_UART_BAUD      115200
#define PLATFORM_UART_IRQ       219

/* PMIC */
#define PLATFORM_PMIC_BASE      0x1000D000ULL
#define PLATFORM_PMIC_IRQ       161

/* Display */
#define PLATFORM_DDP_BASE       0x14000000ULL
#define PLATFORM_DSI_BASE       0x14014000ULL

/* Storage */
#define PLATFORM_MSDC0_BASE     0x11230000ULL
#define PLATFORM_MSDC0_IRQ      110

/* I2C */
#define PLATFORM_I2C3_BASE      0x1100F000ULL
#define PLATFORM_I2C3_IRQ       226

/* Touch */
#define PLATFORM_TOUCH_IRQ       17

/* Boot parameters */
#define PLATFORM_BOOTARGS_ADDR  0x40000100ULL  /* Boot arguments location */
#define PLATFORM_DTB_ADDR       0x40001000ULL  /* Device tree location */
#define PLATFORM_INITRD_ADDR    0x42000000ULL  /* Initrd location */

/* Cache configuration */
#define PLATFORM_CACHE_LINE_SIZE    64
#define PLATFORM_L1_CACHE_SIZE      32768  /* 32KB */
#define PLATFORM_L2_CACHE_SIZE      262144  /* 256KB */

/* Platform capabilities */
#define PLATFORM_HAS_GICv2      1
#define PLATFORM_HAS_GENERIC_TIMER 1
#define PLATFORM_HAS_PSCI       1
#define PLATFORM_HAS_SMP        1

/* Power state coordination */
#define PLATFORM_PSCI_VERSION   0x00010000  /* PSCI v1.0 */
#define PLATFORM_PSCI_FN_BASE   0x84000000

/* Debug configuration */
#define PLATFORM_EARLY_CONSOLE  1
#define PLATFORM_EARLY_PRINTK   1
#define PLATFORM_PANIC_CONSOLE  1

#endif /* _PLATFORM_MT6765_H_ */
