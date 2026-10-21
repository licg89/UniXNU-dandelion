/*
 * ARM64 MMU Implementation for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Two entry points:
 *
 *   mmu_build_page_tables()
 *     Called from boot assembly BEFORE MMU is enabled.
 *     Builds a minimal identity-mapped page table using 2MB block entries.
 *     No stdlib, no heap — writes directly to the reserved PT memory region.
 *
 *   mmu_init() / mmu_map_region() / mmu_enable()
 *     Used after the kernel is running with MMU already on.
 *     Supports full page-granularity (16KB) mapping.
 *
 * Memory layout (16KB pages, 39-bit VA):
 *   PGD  (level 0): 0x44000000
 *   PUD  (level 1): 0x44010000 …
 *   PMD  (level 2): 0x44020000 …
 */

#include "mmu_mt6765.h"
#include "../boot/early_console.h"

/* -------------------------------------------------------------------------
 * Minimal memset — cannot use <string.h> in freestanding boot environment
 * ----------------------------------------------------------------------- */
static void *kmemset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

/* -------------------------------------------------------------------------
 * Page-table memory allocator (pre-MMU, fixed region)
 *
 * The linker reserves PAGE_TABLE_SIZE bytes at PAGE_TABLE_BASE (0x44000000).
 * We hand out 16KB-aligned chunks from it.
 * ----------------------------------------------------------------------- */
static uint64_t pt_next = MMU_PT_PHYS_BASE;

static uint64_t *alloc_pt_table(void)
{
    if (pt_next + MMU_PAGE_SIZE > MMU_PT_PHYS_END) {
        early_console_puts("MMU: ERROR - page table region exhausted\n");
        return (uint64_t *)0;
    }
    uint64_t *table = (uint64_t *)pt_next;
    kmemset(table, 0, MMU_PAGE_SIZE);
    pt_next += MMU_PAGE_SIZE;
    return table;
}

/* -------------------------------------------------------------------------
 * VA → table indices (39-bit VA, 16KB granule)
 *
 *   PGD index: VA[38:30]  (each entry covers 1GB)
 *   PUD index: VA[29:21]  (each block covers 1GB or points to PMD)
 *   PMD index: VA[20:14]  (each block covers 2MB)
 * ----------------------------------------------------------------------- */
#define PGD_IDX(va)  (((va) >> 30) & 0x1FF)
#define PUD_IDX(va)  (((va) >> 21) & 0x1FF)
#define PMD_IDX(va)  (((va) >> 14) & 0x1FF)

/* -------------------------------------------------------------------------
 * mmu_build_page_tables()  — called from boot_mt6765.s
 *
 * Creates an identity-mapped page table with 2MB block entries:
 *
 *   IO space   0x10000000 – 0x17FFFFFF  → Device-nGnRnE (Attr0)
 *   RAM region 0x40000000 – 0x5FFFFFFF  → Normal-WB     (Attr1)
 *
 * This covers all peripherals (UART, GIC, PMIC, DSI, MSDC, I2C), the
 * kernel text/data/BSS, bootargs, DTB, page tables, boot stack, initrd
 * start, and the start of the kernel heap.
 *
 * Memory used from PT region (0x44000000):
 *   0x44000000  PGD          (1 table, 16KB)
 *   0x44010000  PUD_IO       (1 table, 16KB)
 *   0x44020000  PMD_IO       (1 table, 16KB)
 *   0x44030000  PUD_RAM      (1 table, 16KB)
 *   0x44040000  PMD_RAM       (1 table, 16KB)
 *   Total: 5 tables = 80KB
 * ----------------------------------------------------------------------- */
int mmu_build_page_tables(void)
{
    uint64_t *pgd, *pud_io, *pmd_io, *pud_ram, *pmd_ram;
    int i;

    early_console_puts("MMU: Building page tables...\n");
    early_console_puts("  PT region: 0x44000000 - 0x443FFFFF\n");

    /* Reset allocator (in case this is called more than once) */
    pt_next = MMU_PT_PHYS_BASE;

    /* Allocate all five tables */
    pgd     = alloc_pt_table();   /* 0x44000000 */
    pud_io  = alloc_pt_table();   /* 0x44010000 */
    pmd_io  = alloc_pt_table();   /* 0x44020000 */
    pud_ram = alloc_pt_table();   /* 0x44030000 */
    pmd_ram = alloc_pt_table();   /* 0x44040000 */

    if (!pgd || !pud_io || !pmd_io || !pud_ram || !pmd_ram)
        return -1;

    /* ------------------------------------------------------------------
     * IO region: 0x10000000 – 0x17FFFFFF (64MB = 32 × 2MB blocks)
     *
     * PGD[0]  → PUD_IO  (table descriptor)
     * PUD_IO[0] → PMD_IO (table descriptor)
     * PMD_IO[0..31] → block entries, Device-nGnRnE
     * ---------------------------------------------------------------- */
    pgd[PGD_IDX(0x10000000)] = (uint64_t)pud_io | MMU_DESC_TABLE;

    pud_io[PUD_IDX(0x10000000)] = (uint64_t)pmd_io | MMU_DESC_TABLE;

    for (i = 0; i < 32; i++) {
        uint64_t pa = 0x10000000ULL + (uint64_t)i * MMU_BLOCK_SIZE;
        pmd_io[i] = MMU_BLOCK_DEVICE(pa);
    }

    early_console_puts("  IO  [0x10000000-0x17FFFFFF] → 32 device blocks\n");

    /* ------------------------------------------------------------------
     * RAM region: 0x40000000 – 0x5FFFFFFF (512MB = 256 × 2MB blocks)
     *
     * PGD[1]  → PUD_RAM  (table descriptor)
     * PUD_RAM[0] → PMD_RAM (table descriptor)
     * PMD_RAM[0..255] → block entries, Normal-WB
     * ---------------------------------------------------------------- */
    pgd[PGD_IDX(0x40000000)] = (uint64_t)pud_ram | MMU_DESC_TABLE;

    pud_ram[PUD_IDX(0x40000000)] = (uint64_t)pmd_ram | MMU_DESC_TABLE;

    for (i = 0; i < 256; i++) {
        uint64_t pa = 0x40000000ULL + (uint64_t)i * MMU_BLOCK_SIZE;
        pmd_ram[i] = MMU_BLOCK_NORMAL(pa);
    }

    early_console_puts("  RAM [0x40000000-0x5FFFFFFF] → 256 normal blocks\n");

    /* ------------------------------------------------------------------
     * Map individual kernel sub-regions with fine-grained permissions
     * inside the 2MB blocks already mapped as Normal-WB above.
     *
     * These are NOT separate page table entries — they describe the
     * intended permission split that the full kernel MMU code will
     * enforce later (splitting text read-only+X, data read-write, etc.).
     * For now, the 2MB-block mapping is sufficient to boot.
     *
     *   KERNEL_TEXT  0x40100000  16MB  RW+X  (boot code; kernel refines later)
     *   KERNEL_DATA  0x41100000   8MB  RW
     *   KERNEL_BSS   0x41900000   2MB  RW
     *   PAGE_TABLES  0x44000000   4MB  RW  (already mapped as Normal-WB)
     *   BOOT_STACK   0x44800000 256KB  RW  (already mapped as Normal-WB)
     * ---------------------------------------------------------------- */
    early_console_puts("  Sub-region permissions deferred to kernel MMU\n");

    early_console_puts("MMU: Page tables ready (5 tables, 80KB)\n");
    return 0;
}

/* =========================================================================
 * Post-boot MMU API  (called after MMU is already enabled)
 *
 * These functions use a 4-level page table with 16KB pages for fine-grained
 * mapping.  The mmu_context_t is allocated by the caller.
 *
 * NOTE: These rely on early_console_printf() for debug output.  If the
 * console UART is not identity-mapped after MMU enable, debug output may
 * fault — in that case, disable the printf() calls.
 * ======================================================================= */

/* Dynamic page-table allocator (post-boot, uses heap or reserved region) */
static uint64_t post_pt_buf[MMU_ENTRIES_PER_TABLE * 8] __attribute__((aligned(16384)));
static int post_pt_next = 0;

static uint64_t *post_alloc_table(void)
{
    if (post_pt_next >= 8) {
        early_console_puts("MMU: post-boot PT pool exhausted\n");
        return (uint64_t *)0;
    }
    uint64_t *table = &post_pt_buf[post_pt_next * MMU_ENTRIES_PER_TABLE];
    post_pt_next++;
    kmemset(table, 0, MMU_ENTRIES_PER_TABLE * sizeof(uint64_t));
    return table;
}

/*
 * Walk (and optionally create) the page table entry for a given VA.
 * Returns pointer to the PMD entry.
 */
static uint64_t *mmu_get_pte(mmu_context_t *ctx, uint64_t virt, int alloc)
{
    uint64_t *pgd = ctx->pgd;
    uint64_t *pud, *pmd;
    int pgd_i = PGD_IDX(virt);
    int pud_i = PUD_IDX(virt);
    int pmd_i = PMD_IDX(virt);

    /* PGD → PUD */
    if (!(pgd[pgd_i] & MMU_DESC_BLOCK)) {
        if (!alloc) return (uint64_t *)0;
        pud = post_alloc_table();
        if (!pud) return (uint64_t *)0;
        pgd[pgd_i] = (uint64_t)pud | MMU_DESC_TABLE;
    } else {
        pud = (uint64_t *)(pgd[pgd_i] & ~0x3FFFULL);
    }

    /* PUD → PMD */
    if (!(pud[pud_i] & MMU_DESC_BLOCK)) {
        if (!alloc) return (uint64_t *)0;
        pmd = post_alloc_table();
        if (!pmd) return (uint64_t *)0;
        pud[pud_i] = (uint64_t)pmd | MMU_DESC_TABLE;
    } else {
        pmd = (uint64_t *)(pud[pud_i] & ~0x3FFFULL);
    }

    return &pmd[pmd_i];
}

/*
 * mmu_init() — allocate a fresh PGD and configure MAIR/TCR.
 */
int mmu_init(mmu_context_t *ctx)
{
    if (!ctx)
        return -1;

    early_console_puts("MMU: mmu_init() — allocating PGD\n");

    post_pt_next = 0;   /* reset the post-boot pool */
    ctx->pgd = post_alloc_table();
    if (!ctx->pgd) {
        early_console_puts("MMU: PGD allocation failed\n");
        return -1;
    }

    ctx->mair = MMU_MAIR_VALUE;

    /* T0SZ=T1SZ=25 → 39-bit VA; TG0=16KB; TG1=16KB; WB cacheable; Inner-Shar. */
    ctx->tcr = (25ULL << 0)  |    /* T0SZ  */
               (25ULL << 16) |    /* T1SZ  */
               (2ULL  << 14) |    /* TG0 = 16KB */
               (1ULL  << 30) |    /* TG1 = 16KB */
               (1ULL  << 8)  |    /* IRGN0 = WB */
               (1ULL  << 10) |    /* ORGN0 = WB */
               (3ULL  << 12);     /* SH0 = Inner Shareable */

    ctx->ttbr0  = (uint64_t)ctx->pgd;
    ctx->ttbr1  = 0;
    ctx->enabled = false;

    /* Map the same regions as mmu_build_page_tables() but at page granularity */
    mmu_map_region(ctx, IOMEM_BASE,       IOMEM_BASE,       IOMEM_SIZE,
                   MMU_FLAG_DEVICE | MMU_FLAG_NC);
    mmu_map_region(ctx, KERNEL_TEXT_BASE, KERNEL_TEXT_BASE, KERNEL_TEXT_SIZE,
                   MMU_FLAG_EXEC);
    mmu_map_region(ctx, KERNEL_DATA_BASE, KERNEL_DATA_BASE, KERNEL_DATA_SIZE,
                   MMU_FLAG_RO);
    mmu_map_region(ctx, KERNEL_BSS_BASE,  KERNEL_BSS_BASE,  KERNEL_BSS_SIZE,
                   0);
    mmu_map_region(ctx, PAGE_TABLE_BASE,  PAGE_TABLE_BASE,  PAGE_TABLE_SIZE,
                   0);
    mmu_map_region(ctx, BOOT_STACK_BASE,  BOOT_STACK_BASE,  BOOT_STACK_SIZE,
                   0);

    early_console_puts("MMU: init complete\n");
    return 0;
}

/*
 * mmu_map_region() — map a contiguous VA→PA range at 16KB page granularity.
 */
int mmu_map_region(mmu_context_t *ctx, uint64_t virt, uint64_t phys,
                   uint64_t size, uint32_t flags)
{
    if (!ctx || !ctx->pgd)
        return -1;

    uint64_t vaddr = virt & MMU_PAGE_MASK;
    uint64_t paddr = phys & MMU_PAGE_MASK;
    uint64_t end   = (virt + size + MMU_PAGE_SIZE - 1) & MMU_PAGE_MASK;

    while (vaddr < end) {
        uint64_t *pte = mmu_get_pte(ctx, vaddr, 1);
        if (!pte) {
            early_console_puts("MMU: mmu_get_pte failed for VA ");
            early_console_put_hex(vaddr);
            early_console_putc('\n');
            return -1;
        }

        /* Base entry: page descriptor, AF, inner-shareable */
        uint64_t entry = (paddr & MMU_PAGE_MASK) |
                         MMU_DESC_PAGE |
                         MMU_PTE_ATTR_AF |
                         MMU_PTE_ATTR_SH_INNER;

        /* Memory type via AttrIndx */
        if (flags & MMU_FLAG_DEVICE) {
            entry |= MMU_PTE_ATTR_IDX(0) | MMU_PTE_ATTR_XN;
        } else if (flags & MMU_FLAG_NC) {
            entry |= MMU_PTE_ATTR_IDX(2);
        } else {
            entry |= MMU_PTE_ATTR_IDX(1);
        }

        /* Execute permission */
        if (!(flags & MMU_FLAG_EXEC))
            entry |= MMU_PTE_ATTR_XN;

        /* Read-only */
        if (flags & MMU_FLAG_RO)
            entry |= MMU_PTE_ATTR_RO;

        /* User accessible */
        if (flags & MMU_FLAG_USER)
            entry |= MMU_PTE_ATTR_USER;

        *pte = entry;
        vaddr += MMU_PAGE_SIZE;
        paddr += MMU_PAGE_SIZE;
    }

    early_console_puts("MMU: mapped ");
    early_console_put_hex(virt);
    early_console_puts(" -> ");
    early_console_put_hex(phys);
    early_console_puts(" (size=");
    early_console_put_hex(size);
    early_console_puts(")\n");
    return 0;
}

/*
 * mmu_unmap_region()
 */
int mmu_unmap_region(mmu_context_t *ctx, uint64_t virt, uint64_t size)
{
    if (!ctx || !ctx->pgd)
        return -1;

    uint64_t vaddr = virt & MMU_PAGE_MASK;
    uint64_t end   = (virt + size + MMU_PAGE_SIZE - 1) & MMU_PAGE_MASK;

    while (vaddr < end) {
        uint64_t *pte = mmu_get_pte(ctx, vaddr, 0);
        if (pte)
            *pte = 0;
        vaddr += MMU_PAGE_SIZE;
    }

    mmu_invalidate_tlb();
    return 0;
}

/*
 * mmu_enable() — activate MMU using the given context.
 * (Used after a full context has been set up with mmu_init + mmu_map_region.)
 */
void mmu_enable(mmu_context_t *ctx)
{
    if (!ctx || !ctx->pgd)
        return;

    early_console_puts("MMU: Enabling with full context...\n");

    mmu_invalidate_tlb();

    __asm__ volatile("msr mair_el1, %0"  :: "r"(ctx->mair));
    __asm__ volatile("msr tcr_el1, %0"   :: "r"(ctx->tcr));
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ctx->ttbr0));

    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);   /* M  — MMU enable */
    sctlr |= (1ULL << 2);   /* C  — Data cache enable */
    sctlr |= (1ULL << 12);  /* I  — Instruction cache enable */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");

    ctx->enabled = true;
    early_console_puts("MMU: Enabled (full context)\n");
}

void mmu_disable(void)
{
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr &= ~(1ULL << 0);   /* M */
    sctlr &= ~(1ULL << 2);   /* C */
    sctlr &= ~(1ULL << 12);  /* I */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

void mmu_invalidate_tlb(void)
{
    __asm__ volatile(
        "dsb ish\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
    );
}
