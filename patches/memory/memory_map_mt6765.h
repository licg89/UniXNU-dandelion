/*
 * XNU Memory Map for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Memory layout for iOS on MT6765
 */

#ifndef _MEMORY_MAP_MT6765_H_
#define _MEMORY_MAP_MT6765_H_

#include "platform_mt6765.h"

/*
 * Memory Regions
 *
 * Total RAM: ~4GB (0xFCC00000)
 * Base: 0x40000000
 */

/* Kernel code and data */
#define KERNEL_TEXT_BASE        PLATFORM_KERNEL_BASE          /* 0x40080000 */
#define KERNEL_TEXT_SIZE        0x01000000                    /* 16MB code */
#define KERNEL_DATA_BASE        (KERNEL_TEXT_BASE + KERNEL_TEXT_SIZE)  /* 0x41080000 */
#define KERNEL_DATA_SIZE        0x00800000                    /* 8MB data */
#define KERNEL_BSS_BASE         (KERNEL_DATA_BASE + KERNEL_DATA_SIZE)  /* 0x41880000 */
#define KERNEL_BSS_SIZE         0x00200000                    /* 2MB BSS */
#define KERNEL_END              (KERNEL_BSS_BASE + KERNEL_BSS_SIZE)    /* 0x41A80000 */

/* Page tables */
#define PAGE_TABLE_BASE         PLATFORM_TTBR0_BASE           /* 0x44000000 */
#define PAGE_TABLE_SIZE         0x00400000                    /* 4MB */
#define PAGE_TABLE_END          (PAGE_TABLE_BASE + PAGE_TABLE_SIZE)    /* 0x44400000 */

/* Early boot stack */
#define BOOT_STACK_BASE         0x44800000ULL
#define BOOT_STACK_SIZE         0x00040000                    /* 256KB */
#define BOOT_STACK_TOP          (BOOT_STACK_BASE + BOOT_STACK_SIZE)    /* 0x44840000 */

/* Device tree */
#define DTB_BASE                PLATFORM_DTB_ADDR             /* 0x40001000 */
#define DTB_SIZE                0x00040000                    /* 256KB */

/* Boot arguments */
#define BOOTARGS_BASE           PLATFORM_BOOTARGS_ADDR        /* 0x40000100 */
#define BOOTARGS_SIZE           0x00001000                    /* 4KB */

/* Initrd (root filesystem) */
#define INITRD_BASE             PLATFORM_INITRD_ADDR          /* 0x42000000 */
#define INITRD_SIZE             0x10000000                    /* 256MB max */
#define INITRD_END              (INITRD_BASE + INITRD_SIZE)   /* 0x52000000 */

/* Kernel heap */
#define KERNEL_HEAP_BASE        0x52000000ULL
#define KERNEL_HEAP_SIZE        0x20000000                    /* 512MB */
#define KERNEL_HEAP_END         (KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE)  /* 0x72000000 */

/* Reserved regions */
#define RESERVED_RAMOOPS_BASE   0x47C90000ULL                 /* Panic log */
#define RESERVED_RAMOOPS_SIZE   0x00010000                    /* 64KB */

/* Firmware reserved (from LK) */
#define RESERVED_FIRMWARE_BASE  0x47000000ULL
#define RESERVED_FIRMWARE_SIZE  0x00C90000                    /* ~12.5MB */

/* I/O memory regions */
#define IOMEM_BASE              0x10000000ULL
#define IOMEM_SIZE              0x05000000                    /* 80MB I/O space */
#define IOMEM_END               (IOMEM_BASE + IOMEM_SIZE)     /* 0x15000000 */

/*
 * Memory Map Layout
 *
 * 0x40000000 +-------------------+
 *            | Boot args         | 4KB
 * 0x40001000 +-------------------+
 *            | Device tree       | 256KB
 * 0x40080000 +-------------------+
 *            | Kernel text       | 16MB
 * 0x41080000 +-------------------+
 *            | Kernel data       | 8MB
 * 0x41880000 +-------------------+
 *            | Kernel BSS        | 2MB
 * 0x41A80000 +-------------------+
 *            | (free)            |
 * 0x42000000 +-------------------+
 *            | Initrd            | 256MB
 * 0x52000000 +-------------------+
 *            | Kernel heap       | 512MB
 * 0x72000000 +-------------------+
 *            | (free)            |
 *            | ...               |
 * 0xDFC00000 +-------------------+
 *            | End of RAM        |
 *            +-------------------+
 */

/* Memory region types */
typedef enum {
    MEM_TYPE_FREE = 0,
    MEM_TYPE_KERNEL = 1,
    MEM_TYPE_PAGE_TABLE = 2,
    MEM_TYPE_BOOT_STACK = 3,
    MEM_TYPE_DTB = 4,
    MEM_TYPE_BOOTARGS = 5,
    MEM_TYPE_INITRD = 6,
    MEM_TYPE_HEAP = 7,
    MEM_TYPE_RESERVED = 8,
    MEM_TYPE_IOMEM = 9,
} mem_type_t;

/* Memory region descriptor */
typedef struct {
    uint64_t base;
    uint64_t size;
    mem_type_t type;
    const char *name;
} mem_region_t;

/* Memory regions array */
static const mem_region_t memory_regions[] = {
    { BOOTARGS_BASE,        BOOTARGS_SIZE,      MEM_TYPE_BOOTARGS,    "Boot args" },
    { DTB_BASE,             DTB_SIZE,           MEM_TYPE_DTB,         "Device tree" },
    { KERNEL_TEXT_BASE,     KERNEL_TEXT_SIZE,   MEM_TYPE_KERNEL,      "Kernel text" },
    { KERNEL_DATA_BASE,     KERNEL_DATA_SIZE,   MEM_TYPE_KERNEL,      "Kernel data" },
    { KERNEL_BSS_BASE,      KERNEL_BSS_SIZE,    MEM_TYPE_KERNEL,      "Kernel BSS" },
    { PAGE_TABLE_BASE,      PAGE_TABLE_SIZE,    MEM_TYPE_PAGE_TABLE,  "Page tables" },
    { BOOT_STACK_BASE,      BOOT_STACK_SIZE,    MEM_TYPE_BOOT_STACK,  "Boot stack" },
    { INITRD_BASE,          INITRD_SIZE,        MEM_TYPE_INITRD,      "Initrd" },
    { KERNEL_HEAP_BASE,     KERNEL_HEAP_SIZE,   MEM_TYPE_HEAP,        "Kernel heap" },
    { IOMEM_BASE,           IOMEM_SIZE,         MEM_TYPE_IOMEM,       "I/O memory" },
    { RESERVED_RAMOOPS_BASE, RESERVED_RAMOOPS_SIZE, MEM_TYPE_RESERVED, "Ramoops" },
    { 0, 0, MEM_TYPE_FREE, NULL }  /* Terminator */
};

/* Helper macros */
#define IS_KERNEL_ADDR(addr) \
    ((addr) >= KERNEL_TEXT_BASE && (addr) < KERNEL_END)

#define IS_IOMEM_ADDR(addr) \
    ((addr) >= IOMEM_BASE && (addr) < IOMEM_END)

#define IS_RAM_ADDR(addr) \
    ((addr) >= PLATFORM_RAM_BASE && (addr) < (PLATFORM_RAM_BASE + PLATFORM_RAM_SIZE))

#endif /* _MEMORY_MAP_MT6765_H_ */
