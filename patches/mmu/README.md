# ARM64 MMU Implementation for MT6765

This directory contains the ARM64 Memory Management Unit (MMU) implementation for XNU on MT6765.

## Overview

The MMU is responsible for virtual-to-physical address translation. XNU requires:
- 16KB page size (iOS standard)
- 39-bit virtual address space
- Separate kernel and user space mappings
- Device memory mapping (non-cacheable)

## Files

### mmu_mt6765.h
MMU configuration and definitions:
- Page table entry format
- Memory attributes
- Translation table structure
- Region types and flags

### mmu_init.c
MMU initialization and management:
- Page table allocation
- Region mapping/unmapping
- MMU enable/disable
- TLB invalidation

## Key Concepts

### Page Table Structure

For 16KB pages with 39-bit VA:
```
Level 0 (PGD): 9 bits  → 512 entries
Level 1 (PUD): 9 bits  → 512 entries
Level 2 (PMD): 9 bits  → 512 entries
Page offset:   14 bits → 16KB pages
```

### Memory Attributes

| Attribute | Description |
|---|---|
| Device-nGnRnE | Device memory, non-gathering, non-reordering |
| Normal Write-Back | Cacheable RAM |
| Normal Non-Cacheable | Non-cacheable RAM |

### Memory Map

```
0x40080000 - Kernel text (executable)
0x41080000 - Kernel data (RW)
0x41880000 - Kernel BSS (RW)
0x44000000 - Page tables (RW)
0x44800000 - Boot stack (RW)
0x10000000 - Device memory (device-nGnRnE)
```

## Usage

```c
#include "mmu_mt6765.h"

mmu_context_t mmu;

// Initialize MMU
int ret = mmu_init(&mmu);
if (ret < 0) {
    // Error
}

// Map additional region
mmu_map_region(&mmu, 0x50000000, 0x50000000, 0x10000000, 0);

// Enable MMU
mmu_enable(&mmu);

// MMU is now active, virtual addresses are translated

// Disable MMU (if needed)
mmu_disable();
```

## Implementation Details

### Page Table Allocation

Page tables are allocated from a static pool:
- 16 tables × 2048 entries = 32,768 entries
- Each table: 16KB (aligned)
- Total: 256KB

### Translation Flow

1. Virtual address split into indices
2. Walk page table hierarchy
3. Check permissions
4. Translate to physical address
5. Apply memory attributes

### Cache Coherency

- Data cache: Write-Back, Write-Allocate
- Instruction cache: Enabled for kernel text
- Device memory: Non-cacheable
- TLB: Invalidated on mapping changes

## Testing

```bash
# Compile test
gcc -o test_mmu test_mmu.c mmu_init.c

# Run on device
./test_mmu
```

## Challenges

### 1. 16KB Pages
XNU requires 16KB pages (iOS standard), but most Linux systems use 4KB.
**Solution**: Configure TCR_EL1.TG0 = 0b10 for 16KB granule.

### 2. Kernel/User Split
XNU expects TTBR0 for kernel, TTBR1 for user space.
**Solution**: Use TTBR0 for all mappings initially, add TTBR1 later.

### 3. Device Memory
Device registers must be mapped as non-cacheable.
**Solution**: Use Device-nGnRnE memory attribute.

### 4. Cache Coherency
Need to ensure cache coherency across cores.
**Solution**: Use Inner Shareable attribute for RAM.

## Status

- [x] Page table structure
- [x] Memory attributes
- [x] Region mapping
- [x] MMU enable/disable
- [ ] Multi-core support
- [ ] User space mapping
- [ ] Cache maintenance

## Next Steps

1. Test on QEMU
2. Verify address translation
3. Add user space support
4. Implement cache maintenance
5. Test with XNU

## References

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [ARMv8 MMU documentation](https://developer.arm.com/documentation/101811/latest)
- [Linux ARM64 MMU](https://www.kernel.org/doc/html/latest/arch/arm64/memory.html)
