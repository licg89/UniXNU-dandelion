# MTK MSDC eMMC Driver

MediaTek Mobile Storage Device Controller driver for MT6765 eMMC storage.

## Overview

This driver provides eMMC storage access for the Xiaomi Redmi 9A. It handles the MTK MSDC controller and eMMC protocol, enabling read/write operations needed for loading iOS.

## Hardware

- **Controller**: MediaTek MSDC (4 controllers)
- **eMMC**: Connected to MSDC0
- **Base Address**: 0x11230000 (MSDC0)
- **Capacity**: ~32GB (varies by model)
- **Interface**: 8-bit eMMC 5.1

## Features

### Implemented
- ✅ MSDC controller initialization
- ✅ eMMC card initialization sequence
- ✅ Single block read/write (CMD17/CMD24)
- ✅ Multi-block read/write (CMD18/CMD25)
- ✅ Card info extraction (capacity, CID, CSD)
- ✅ High capacity card support

### TODO
- [ ] DMA support for large transfers
- [ ] DDR52 mode
- [ ] HS200/HS400 modes
- [ ] Partition table parsing
- [ ] Filesystem support
- [ ] Power management
- [ ] Error recovery

## Register Map

### MSDC Controller (0x11230000)
| Register | Offset | Description |
|---|---|---|
| CFG | 0x00 | Configuration |
| STS | 0x04 | Status |
| INT | 0x08 | Interrupt |
| INTEN | 0x0C | Interrupt enable |
| FIFO_CFG | 0x10 | FIFO config |
| FIFO_DATA | 0x14 | FIFO data |
| CMD | 0x18 | Command |
| ARG | 0x1C | Argument |
| RSP | 0x20 | Response |
| BLK_NUM | 0x24 | Block number |
| DMA_CTRL | 0x60 | DMA control |
| DMA_ADDR | 0x68 | DMA address |
| DMA_LEN | 0x6C | DMA length |

## eMMC Initialization Sequence

1. **CMD0** (GO_IDLE_STATE): Reset card
2. **CMD1** (SEND_OP_COND): Initialize card, check capacity
3. **CMD2** (ALL_SEND_CID): Get card ID
4. **CMD3** (SET_RELATIVE_ADDR): Set relative address
5. **CMD9** (SEND_CSD): Get card specific data
6. **CMD7** (SELECT_CARD): Select card for data transfer

## Usage

```c
#include "mtk_msdc.h"

mtk_msdc_t msdc;

// Initialize controller
int ret = mtk_msdc_init(&msdc, MTK_MSDC0_BASE, 0);
if (ret < 0) {
    // Error
}

// Initialize card
ret = mtk_msdc_card_init(&msdc);
if (ret < 0) {
    // Error
}

// Read block
uint8_t buf[512];
ret = mtk_msdc_read(&msdc, 0, 1, buf);

// Write block
uint8_t data[512] = {0};
ret = mtk_msdc_write(&msdc, 1000, 1, data);
```

## eMMC Commands

| CMD | Name | Description |
|---|---|---|
| 0 | GO_IDLE_STATE | Reset card |
| 1 | SEND_OP_COND | Initialize |
| 2 | ALL_SEND_CID | Get CID |
| 3 | SET_RELATIVE_ADDR | Set RCA |
| 7 | SELECT_CARD | Select card |
| 8 | SEND_EXT_CSD | Extended CSD |
| 9 | SEND_CSD | Get CSD |
| 12 | STOP_TRANSMISSION | Stop multi-block |
| 13 | SEND_STATUS | Get status |
| 17 | READ_SINGLE_BLOCK | Read 1 block |
| 18 | READ_MULTIPLE_BLOCK | Read N blocks |
| 24 | WRITE_BLOCK | Write 1 block |
| 25 | WRITE_MULTIPLE_BLOCK | Write N blocks |

## Card Info Structure

```c
typedef struct {
    uint32_t rca;           // Relative card address
    uint32_t cid[4];        // Card ID
    uint32_t csd[4];        // Card specific data
    uint32_t ext_csd[128];  // Extended CSD
    uint64_t capacity;      // Capacity in bytes
    uint32_t block_size;    // Block size (512)
    emmc_state_t state;     // Current state
    bool high_capacity;     // High capacity card
} emmc_card_t;
```

## Testing

```bash
# Compile test program
gcc -o test_emmc test_emmc.c mtk_msdc.c

# Run on device
./test_emmc
```

Test cases:
1. Controller initialization
2. Card initialization sequence
3. Single block read/write
4. Multi-block read/write
5. Write verification
6. Random access reads

## Integration with XNU

For XNU integration:
1. Implement IOKit driver (IOBlockStorageDevice subclass)
2. Parse GPT/MBR partition table
3. Implement HFS+/APFS filesystem support
4. Register as boot device
5. Support partition-based loading

## Performance

Expected performance (theoretical):
- Single block read: ~1ms
- Multi-block read: ~10MB/s (PIO mode)
- Multi-block read: ~100MB/s (DMA + HS200)
- Write: Similar to read

## References

- [Linux MTK MSDC driver](https://github.com/torvalds/linux/tree/master/drivers/mmc/host/mtk-sd.c)
- [eMMC specification](https://www.jedec.org/sites/default/files/JESD84-B51.pdf)
- MTK MSDC documentation (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)
**Note**: Register addresses need verification

## Next Steps

1. Test on real hardware
2. Verify eMMC initialization sequence
3. Add DMA support
4. Implement partition table parsing
5. Add filesystem support (HFS+/APFS)
6. Integrate with XNU storage stack
