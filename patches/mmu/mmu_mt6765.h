/*
 * ARM64 MMU Configuration for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Memory Management Unit setup for 16KB pages (iOS standard).
 *
 * Page table structure (39-bit VA, 16KB granule):
 *   Level 0  PGD  →  Level 1  PUD  →  Level 2  PMD  →  page/block
 *
 * With 2MB block entries at level 2 we can map large regions efficiently:
 *   - Each PGD entry covers 1GB
 *   - Each PUD entry (block) covers 1GB, or points to a PMD
 *   - Each PMD block entry covers 2MB
 *
 * Called from boot_mt6765.s BEFORE MMU is enabled.
 */

#ifndef _MMU_MT6765_H_
#define _MMU_MT6765_H_

#include "unixnu_types.h"
#include "../memory/memory_map_mt6765.h"

/* -------------------------------------------------------------------------
 * Page geometry
 * ----------------------------------------------------------------------- */
#define MMU_PAGE_SHIFT          14                  /* 16KB pages */
#define MMU_PAGE_SIZE           (1ULL << MMU_PAGE_SHIFT)
#define MMU_PAGE_MASK           (~(MMU_PAGE_SIZE - 1))

/* 2MB block at level 2 (16KB granule) */
#define MMU_BLOCK_SHIFT         21                  /* 2MB = 2^21 */
#define MMU_BLOCK_SIZE          (1ULL << MMU_BLOCK_SHIFT)
#define MMU_BLOCK_MASK          (~(MMU_BLOCK_SIZE - 1))

/* Translation table levels */
#define MMU_LEVEL_0             0   /* PGD */
#define MMU_LEVEL_1             1   /* PUD */
#define MMU_LEVEL_2             2   /* PMD */
#define MMU_LEVEL_3             3   /* PTE (page) */

/* Entries per table: 16KB / 8 bytes = 2048 */
#define MMU_ENTRIES_PER_TABLE   2048

/* Address widths */
#define MMU_VA_BITS             39
#define MMU_PA_BITS             40

/* Page-table physical region (must match PAGE_TABLE_BASE in memory_map) */
#define MMU_PT_PHYS_BASE        PAGE_TABLE_BASE          /* 0x44000000 */
#define MMU_PT_PHYS_SIZE        PAGE_TABLE_SIZE          /* 4MB */
#define MMU_PT_PHYS_END         (MMU_PT_PHYS_BASE + MMU_PT_PHYS_SIZE)

/* -------------------------------------------------------------------------
 * Descriptor types  (bits[1:0])
 *
 * Fault:           0b00
 * Table (lvl 0/1): 0b11  → next-level table
 * Block (lvl 1/2): 0b01  → 1GB or 2MB block
 * Page  (lvl 3):   0b11  → 16KB page
 * ----------------------------------------------------------------------- */
#define MMU_DESC_FAULT            0x0ULL
#define MMU_DESC_TABLE            0x3ULL   /* valid table descriptor */
#define MMU_DESC_BLOCK            0x1ULL   /* valid block descriptor */
#define MMU_DESC_PAGE             0x3ULL   /* valid page descriptor */

/* -------------------------------------------------------------------------
 * Memory Attribute Indirection (MAIR) encoding
 * 4 attribute slots (Attr0–Attr3), 8 bits each.
 * ----------------------------------------------------------------------- */
#define MMU_MAIR_DEVICE_nGnRnE    0x00   /* Device-nGnRnE (strictest) */
#define MMU_MAIR_DEVICE_nGnRwE    0x04   /* Device-nGnRwE */
#define MMU_MAIR_NORMAL_NC        0x44   /* Normal Non-Cacheable */
#define MMU_MAIR_NORMAL_WB        0xFF   /* Normal Write-Back cacheable */

#define MMU_MAIR_IDX(idx, attr)   ((uint64_t)(attr) << ((idx) * 8))

/* Pre-built MAIR values */
#define MMU_MAIR_VALUE  ( \
    MMU_MAIR_IDX(0, MMU_MAIR_DEVICE_nGnRnE) |  /* Attr0: Device */ \
    MMU_MAIR_IDX(1, MMU_MAIR_NORMAL_WB)     |  /* Attr1: Normal WB */ \
    MMU_MAIR_IDX(2, MMU_MAIR_NORMAL_NC)     |  /* Attr2: Normal NC */ \
    MMU_MAIR_IDX(3, MMU_MAIR_DEVICE_nGnRwE)    /* Attr3: Device nGnRwE */ \
)

/* -------------------------------------------------------------------------
 * Block / page descriptor attribute bits
 * ----------------------------------------------------------------------- */
#define MMU_PTE_ATTR_AF           (1ULL << 10)  /* Access Flag (must be 1) */
#define MMU_PTE_ATTR_SH_INNER     (3ULL << 8)   /* Inner Shareable */
#define MMU_PTE_ATTR_SH_OUTER     (2ULL << 8)   /* Outer Shareable */
#define MMU_PTE_ATTR_XN           (1ULL << 54)  /* Execute Never */
#define MMU_PTE_ATTR_PXN          (1ULL << 53)  /* Privileged Execute Never */
#define MMU_PTE_ATTR_CONTIGUOUS   (1ULL << 52)
#define MMU_PTE_ATTR_nG           (1ULL << 11)  /* Non-global */

/* AP bits [6:7] */
#define MMU_PTE_ATTR_RW           (0ULL << 7)   /* Read-Write */
#define MMU_PTE_ATTR_RO           (2ULL << 7)   /* Read-Only */
/* AP[2] = bit 6 */
#define MMU_PTE_ATTR_USER         (1ULL << 6)   /* EL0 accessible */

/* AttrIndx [4:2] — selects MAIR slot */
#define MMU_PTE_ATTR_IDX(idx)     (((uint64_t)(idx) & 0x7) << 2)

/* -------------------------------------------------------------------------
 * Convenience: common block-descriptor templates
 * ----------------------------------------------------------------------- */

/* Device memory: Attr0 (nGnRnE), XN, AF, inner-shareable */
#define MMU_BLOCK_DEVICE(pa)  ( \
    ((uint64_t)(pa) & MMU_BLOCK_MASK) | \
    MMU_DESC_BLOCK | \
    MMU_PTE_ATTR_AF | \
    MMU_PTE_ATTR_SH_INNER | \
    MMU_PTE_ATTR_XN | \
    MMU_PTE_ATTR_IDX(0) \
)

/* Normal cached RAM: Attr1 (WB), RW, AF, inner-shareable */
#define MMU_BLOCK_NORMAL(pa)  ( \
    ((uint64_t)(pa) & MMU_BLOCK_MASK) | \
    MMU_DESC_BLOCK | \
    MMU_PTE_ATTR_AF | \
    MMU_PTE_ATTR_SH_INNER | \
    MMU_PTE_ATTR_IDX(1) \
)

/* -------------------------------------------------------------------------
 * Flags used by mmu_map_region() (page-level mapping, post-MMU)
 * ----------------------------------------------------------------------- */
#define MMU_FLAG_EXEC             (1 << 0)
#define MMU_FLAG_RO               (1 << 1)
#define MMU_FLAG_USER             (1 << 2)
#define MMU_FLAG_DEVICE           (1 << 3)
#define MMU_FLAG_NC               (1 << 4)

/* -------------------------------------------------------------------------
 * Region types (for mmu_region_t)
 * ----------------------------------------------------------------------- */
typedef enum {
    MMU_REGION_KERNEL = 0,
    MMU_REGION_DEVICE = 1,
    MMU_REGION_NORMAL = 2,
    MMU_REGION_NC     = 3,
} mmu_region_type_t;

typedef struct {
    uint64_t virt;
    uint64_t phys;
    uint64_t size;
    mmu_region_type_t type;
    uint32_t flags;
} mmu_region_t;

/* -------------------------------------------------------------------------
 * Runtime MMU context (used after MMU is enabled)
 * ----------------------------------------------------------------------- */
typedef struct {
    uint64_t *pgd;       /* Page Global Directory virtual address */
    uint64_t ttbr0;
    uint64_t ttbr1;
    uint64_t tcr;
    uint64_t mair;
    bool     enabled;
} mmu_context_t;

/* -------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/*
 * mmu_build_page_tables()
 *   Called from assembly BEFORE MMU is enabled.
 *   Allocates from the static page-table region and creates:
 *     - Identity-map IO space   0x10000000–0x17FFFFFF (Device-nGnRnE)
 *     - Identity-map RAM region 0x40000000–0x5FFFFFFF (Normal-WB)
 *   Uses 2MB block entries at PMD level (no PT allocation needed).
 *   Returns 0 on success, -1 on error.
 */
int mmu_build_page_tables(void);

/*
 * mmu_init() / mmu_map_region() / mmu_enable()  — post-boot, with MMU on.
 * These are for later kernel use (full page-granularity mapping).
 */
int  mmu_init(mmu_context_t *ctx);
int  mmu_map_region(mmu_context_t *ctx, uint64_t virt, uint64_t phys,
                    uint64_t size, uint32_t flags);
int  mmu_unmap_region(mmu_context_t *ctx, uint64_t virt, uint64_t size);
void mmu_enable(mmu_context_t *ctx);
void mmu_disable(void);
void mmu_invalidate_tlb(void);

#endif /* _MMU_MT6765_H_ */
