/*
 * ARM64 MMU Initialization for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Memory Management Unit setup for XNU on MT6765
 */

#include "mmu_mt6765.h"
#include <stdio.h>
#include <string.h>

/* Page table allocation */
static uint64_t page_tables[MMU_ENTRIES_PER_TABLE * 16] __attribute__((aligned(16384)));
static int next_table = 0;

/*
 * Allocate a page table
 */
static uint64_t *mmu_alloc_table(void)
{
    if (next_table >= 16) {
        printf("MMU: Out of page tables\n");
        return NULL;
    }

    uint64_t *table = &page_tables[next_table * MMU_ENTRIES_PER_TABLE];
    next_table++;

    /* Clear table */
    memset(table, 0, MMU_ENTRIES_PER_TABLE * sizeof(uint64_t));

    return table;
}

/*
 * Get page table entry for virtual address
 */
static uint64_t *mmu_get_pte(mmu_context_t *ctx, uint64_t virt, int alloc)
{
    uint64_t *pgd = ctx->pgd;
    uint64_t *pud, *pmd, *pte;
    int pgd_idx, pud_idx, pmd_idx, pte_idx;

    /* Calculate indices for 16KB pages, 39-bit VA */
    pgd_idx = (virt >> 30) & 0x1FF;    /* 9 bits */
    pud_idx = (virt >> 21) & 0x1FF;    /* 9 bits */
    pmd_idx = (virt >> 12) & 0x1FF;    /* 9 bits */
    pte_idx = 0;                        /* Not used for block mapping */

    /* PGD entry */
    if (!(pgd[pgd_idx] & MMU_PTE_VALID)) {
        if (!alloc) return NULL;
        pud = mmu_alloc_table();
        if (!pud) return NULL;
        pgd[pgd_idx] = (uint64_t)pud | MMU_PTE_TYPE_TABLE;
    }
    pud = (uint64_t *)(pgd[pgd_idx] & ~0xFFF);

    /* PUD entry */
    if (!(pud[pud_idx] & MMU_PTE_VALID)) {
        if (!alloc) return NULL;
        pmd = mmu_alloc_table();
        if (!pmd) return NULL;
        pud[pud_idx] = (uint64_t)pmd | MMU_PTE_TYPE_TABLE;
    }
    pmd = (uint64_t *)(pud[pud_idx] & ~0xFFF);

    /* PMD entry */
    return &pmd[pmd_idx];
}

/*
 * Initialize MMU
 */
int mmu_init(mmu_context_t *ctx)
{
    if (!ctx) {
        return -1;
    }

    printf("MMU: Initializing...\n");

    /* Allocate PGD */
    ctx->pgd = mmu_alloc_table();
    if (!ctx->pgd) {
        printf("MMU: Failed to allocate PGD\n");
        return -1;
    }
    printf("MMU: PGD at %p\n", ctx->pgd);

    /* Configure MAIR (Memory Attribute Indirection Register) */
    ctx->mair = MMU_MAIR_IDX(0, MMU_MAIR_DEVICE_nGnRnE) |  /* Attr0: Device */
                MMU_MAIR_IDX(1, MMU_MAIR_NORMAL_WB) |      /* Attr1: Normal WB */
                MMU_MAIR_IDX(2, MMU_MAIR_NORMAL_NC) |      /* Attr2: Normal NC */
                MMU_MAIR_IDX(3, MMU_MAIR_DEVICE_nGnRwE);   /* Attr3: Device nGnRwE */

    /* Configure TCR (Translation Control Register) */
    /* T0SZ = 25 (39-bit VA), T1SZ = 25 (39-bit VA) */
    /* TG0 = 0b10 (16KB), TG1 = 0b01 (16KB) */
    /* IRGN0 = 0b01 (Write-Back), ORGN0 = 0b01 (Write-Back) */
    /* SH0 = 0b11 (Inner Shareable) */
    ctx->tcr = (25ULL << 0) |      /* T0SZ */
               (25ULL << 16) |     /* T1SZ */
               (2ULL << 14) |      /* TG0 = 16KB */
               (1ULL << 30) |      /* TG1 = 16KB */
               (1ULL << 8) |       /* IRGN0 = Write-Back */
               (1ULL << 10) |      /* ORGN0 = Write-Back */
               (3ULL << 12);       /* SH0 = Inner Shareable */

    /* Set TTBR0/TTBR1 */
    ctx->ttbr0 = (uint64_t)ctx->pgd;
    ctx->ttbr1 = 0;  /* Not used for now */

    printf("MMU: TCR = 0x%llx\n", ctx->tcr);
    printf("MMU: MAIR = 0x%llx\n", ctx->mair);
    printf("MMU: TTBR0 = 0x%llx\n", ctx->ttbr0);

    /* Map kernel regions */
    printf("MMU: Mapping kernel regions...\n");

    /* Kernel text (executable) */
    mmu_map_region(ctx, KERNEL_TEXT_BASE, KERNEL_TEXT_BASE, KERNEL_TEXT_SIZE,
                   MMU_FLAG_EXEC);

    /* Kernel data (RW, non-executable) */
    mmu_map_region(ctx, KERNEL_DATA_BASE, KERNEL_DATA_BASE, KERNEL_DATA_SIZE,
                   MMU_FLAG_RO);

    /* Kernel BSS (RW, non-executable) */
    mmu_map_region(ctx, KERNEL_BSS_BASE, KERNEL_BSS_BASE, KERNEL_BSS_SIZE, 0);

    /* Page tables (RW, non-executable) */
    mmu_map_region(ctx, PAGE_TABLE_BASE, PAGE_TABLE_BASE, PAGE_TABLE_SIZE, 0);

    /* Boot stack (RW, non-executable) */
    mmu_map_region(ctx, BOOT_STACK_BASE, BOOT_STACK_BASE, BOOT_STACK_SIZE, 0);

    /* Device memory (device-nGnRnE) */
    mmu_map_region(ctx, IOMEM_BASE, IOMEM_BASE, IOMEM_SIZE,
                   MMU_FLAG_DEVICE | MMU_FLAG_NC);

    ctx->enabled = false;
    printf("MMU: Initialized\n");

    return 0;
}

/*
 * Map a memory region
 */
int mmu_map_region(mmu_context_t *ctx, uint64_t virt, uint64_t phys, uint64_t size, uint32_t flags)
{
    if (!ctx || !ctx->pgd) {
        return -1;
    }

    uint64_t end = virt + size;
    uint64_t vaddr = virt & MMU_PAGE_MASK;
    uint64_t paddr = phys & MMU_PAGE_MASK;

    while (vaddr < end) {
        uint64_t *pte = mmu_get_pte(ctx, vaddr, 1);
        if (!pte) {
            printf("MMU: Failed to get PTE for 0x%llx\n", vaddr);
            return -1;
        }

        /* Build page table entry */
        uint64_t entry = paddr | MMU_PTE_TYPE_PAGE | MMU_PTE_ATTR_AF | MMU_PTE_ATTR_SH_INNER;

        /* Set attributes based on flags */
        if (flags & MMU_FLAG_DEVICE) {
            /* Device memory: use Attr0 */
            entry |= (0ULL << 2);  /* AttrIndx = 0 */
            entry |= MMU_PTE_ATTR_XN;  /* Execute Never */
        } else if (flags & MMU_FLAG_NC) {
            /* Non-cacheable: use Attr2 */
            entry |= (2ULL << 2);  /* AttrIndx = 2 */
        } else {
            /* Normal memory: use Attr1 */
            entry |= (1ULL << 2);  /* AttrIndx = 1 */
        }

        if (!(flags & MMU_FLAG_EXEC)) {
            entry |= MMU_PTE_ATTR_XN;  /* Execute Never */
        }

        if (flags & MMU_FLAG_RO) {
            entry |= MMU_PTE_ATTR_RO;
        }

        if (flags & MMU_FLAG_USER) {
            entry |= MMU_PTE_ATTR_USER;
        }

        *pte = entry;

        vaddr += MMU_PAGE_SIZE;
        paddr += MMU_PAGE_SIZE;
    }

    printf("MMU: Mapped 0x%llx -> 0x%llx (size=0x%llx, flags=0x%x)\n",
           virt, phys, size, flags);

    return 0;
}

/*
 * Unmap a memory region
 */
int mmu_unmap_region(mmu_context_t *ctx, uint64_t virt, uint64_t size)
{
    if (!ctx || !ctx->pgd) {
        return -1;
    }

    uint64_t end = virt + size;
    uint64_t vaddr = virt & MMU_PAGE_MASK;

    while (vaddr < end) {
        uint64_t *pte = mmu_get_pte(ctx, vaddr, 0);
        if (pte) {
            *pte = 0;  /* Clear entry */
        }
        vaddr += MMU_PAGE_SIZE;
    }

    mmu_invalidate_tlb();

    return 0;
}

/*
 * Enable MMU
 */
void mmu_enable(mmu_context_t *ctx)
{
    if (!ctx || !ctx->pgd) {
        return;
    }

    printf("MMU: Enabling...\n");

    /* Invalidate TLB */
    mmu_invalidate_tlb();

    /* Set MAIR */
    __asm__ volatile("msr mair_el1, %0" :: "r"(ctx->mair));

    /* Set TCR */
    __asm__ volatile("msr tcr_el1, %0" :: "r"(ctx->tcr));

    /* Set TTBR0 */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ctx->ttbr0));

    /* Enable MMU */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);   /* M: MMU enable */
    sctlr |= (1ULL << 2);   /* C: Data cache enable */
    sctlr |= (1ULL << 12);  /* I: Instruction cache enable */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");

    ctx->enabled = true;
    printf("MMU: Enabled\n");
}

/*
 * Disable MMU
 */
void mmu_disable(void)
{
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr &= ~(1ULL << 0);  /* M: MMU disable */
    sctlr &= ~(1ULL << 2);  /* C: Data cache disable */
    sctlr &= ~(1ULL << 12); /* I: Instruction cache disable */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

/*
 * Invalidate TLB
 */
void mmu_invalidate_tlb(void)
{
    __asm__ volatile(
        "dsb ish\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
    );
}
