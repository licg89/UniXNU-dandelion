/*
 * ARM64 MMU Configuration for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Memory Management Unit setup for 16KB pages (iOS standard)
 */

#ifndef _MMU_MT6765_H_
#define _MMU_MT6765_H_

#include "../memory/memory_map_mt6765.h"

/* ARM64 MMU constants */
#define MMU_PAGE_SHIFT          14          /* 16KB pages */
#define MMU_PAGE_SIZE           (1ULL << MMU_PAGE_SHIFT)
#define MMU_PAGE_MASK           (~(MMU_PAGE_SIZE - 1))

/* Translation table levels */
#define MMU_LEVEL_0             0   /* PGD (Page Global Directory) */
#define MMU_LEVEL_1             1   /* PUD (Page Upper Directory) */
#define MMU_LEVEL_2             2   /* PMD (Page Middle Directory) */
#define MMU_LEVEL_3             3   /* PTE (Page Table Entry) */

/* Table entry counts (for 16KB pages) */
#define MMU_ENTRIES_PER_TABLE   2048    /* 16KB / 8 bytes */

/* Address width */
#define MMU_VA_BITS             39      /* 39-bit virtual address */
#define MMU_PA_BITS             40      /* 40-bit physical address */

/* Translation table base address */
#define MMU_TTBR0_BASE          0x44000000ULL  /* Kernel page tables */
#define MMU_TTBR1_BASE          0x44200000ULL  /* User page tables (if needed) */

/* Page table entry types */
#define MMU_PTE_TYPE_FAULT        0x0   /* Fault entry */
#define MMU_PTE_TYPE_BLOCK        0x1   /* Block entry (1GB/2MB) */
#define MMU_PTE_TYPE_TABLE        0x3   /* Table entry */
#define MMU_PTE_TYPE_PAGE         0x3   /* Page entry (level 3) */

/* Page table entry attributes */
#define MMU_PTE_VALID             (1ULL << 0)
#define MMU_PTE_TABLE             (1ULL << 1)
#define MMU_PTE_PAGE              (1ULL << 1)

/* Memory attributes (MAIR) */
#define MMU_MAIR_DEVICE_nGnRnE    0x00  /* Device non-Gathering, non-Reordering, non-Early ACK */
#define MMU_MAIR_DEVICE_nGnRwE    0x04  /* Device non-Gathering, non-Reordering, Early ACK */
#define MMU_MAIR_DEVICE_nGnRnE    0x00
#define MMU_MAIR_NORMAL_NC        0x44  /* Normal Non-Cacheable */
#define MMU_MAIR_NORMAL_WB        0xFF  /* Normal Write-Back Cacheable */
#define MMU_MAIR_DEVICE_nGnRnE    0x00
#define MMU_MAIR_IDX(idx, attr)   ((attr) << ((idx) * 8))

/* Page table descriptor bits */
#define MMU_PTE_ATTR_AF           (1ULL << 10)  /* Access Flag */
#define MMU_PTE_ATTR_SH_INNER     (3ULL << 8)   /* Inner Shareable */
#define MMU_PTE_ATTR_APTable      (0ULL << 6)   /* No restriction */
#define MMU_PTE_ATTR_XN           (1ULL << 54)  /* Execute Never */
#define MMU_PTE_ATTR_PXN          (1ULL << 53)  /* Privileged Execute Never */
#define MMU_PTE_ATTR_CONTIGUOUS   (1ULL << 52)  /* Contiguous */
#define MMU_PTE_ATTR_nG           (1ULL << 11)  /* Non-global */
#define MMU_PTE_ATTR_AF           (1ULL << 10)  /* Access Flag */

/* Access permissions */
#define MMU_PTE_ATTR_RW           (0ULL << 7)   /* Read-Write */
#define MMU_PTE_ATTR_RO           (2ULL << 7)   /* Read-Only */
#define MMU_PTE_ATTR_USER         (1ULL << 6)   /* User accessible */
#define MMU_PTE_ATTR_PRIV         (0ULL << 6)   /* Privileged only */

/* Shareability */
#define MMU_PTE_ATTR_SH_INNER     (3ULL << 8)   /* Inner Shareable */
#define MMU_PTE_ATTR_SH_OUTER     (2ULL << 8)   /* Outer Shareable */

/* Memory region types */
typedef enum {
    MMU_REGION_KERNEL = 0,      /* Kernel code/data (RW, XN for data) */
    MMU_REGION_DEVICE = 1,      /* Device memory (Device-nGnRnE) */
    MMU_REGION_NORMAL = 2,      /* Normal RAM (Write-Back cacheable) */
    MMU_REGION_NC = 3,          /* Normal Non-Cacheable */
} mmu_region_type_t;

/* Memory region descriptor */
typedef struct {
    uint64_t virt;              /* Virtual address */
    uint64_t phys;              /* Physical address */
    uint64_t size;              /* Size in bytes */
    mmu_region_type_t type;     /* Region type */
    uint32_t flags;             /* Additional flags */
} mmu_region_t;

/* MMU flags */
#define MMU_FLAG_EXEC             (1 << 0)  /* Executable */
#define MMU_FLAG_RO               (1 << 1)  /* Read-only */
#define MMU_FLAG_USER             (1 << 2)  /* User accessible */
#define MMU_FLAG_DEVICE           (1 << 3)  /* Device memory */
#define MMU_FLAG_NC               (1 << 4)  /* Non-cacheable */

/* MMU context */
typedef struct {
    uint64_t *pgd;              /* Page Global Directory */
    uint64_t ttbr0;             /* TTBR0 value */
    uint64_t ttbr1;             /* TTBR1 value */
    uint64_t tcr;               /* Translation Control Register */
    uint64_t mair;              /* Memory Attribute Indirection Register */
    bool enabled;
} mmu_context_t;

/* Function prototypes */
int mmu_init(mmu_context_t *ctx);
int mmu_map_region(mmu_context_t *ctx, uint64_t virt, uint64_t phys, uint64_t size, uint32_t flags);
int mmu_unmap_region(mmu_context_t *ctx, uint64_t virt, uint64_t size);
void mmu_enable(mmu_context_t *ctx);
void mmu_disable(void);
void mmu_invalidate_tlb(void);

#endif /* _MMU_MT6765_H_ */
