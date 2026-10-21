# MT6357 PMIC Driver

MediaTek MT6357 Power Management IC driver for XNU.

## Overview

The MT6357 is the PMIC (Power Management IC) used in Xiaomi Redmi 9A (dandelion) with the MT6765 SoC. This driver provides basic register access via the PMIC wrapper interface.

## Hardware

- **PMIC**: MediaTek MT6357
- **Interface**: PMIC Wrapper (SPI-like serial interface)
- **Base Address**: `0x1000D000`
- **IRQ**: 161 (MT_SYSIRQ)

## Features

### Implemented
- ✅ PMIC wrapper initialization
- ✅ Register read/write
- ✅ Bit set/clear operations
- ✅ Chip ID detection
- ✅ Status register access
- ✅ Battery ADC readout

### TODO
- [ ] Voltage regulator control (BUCK/LDO)
- [ ] Charger control
- [ ] Interrupt handling
- [ ] Power button handling
- [ ] Thermal management

## Registers

Key registers (see `mtk_pmic.h` for full list):

| Register | Address | Description |
|---|---|---|
| CID | 0x0000 | Chip ID (should be 0x6357) |
| STRUP_CON0 | 0x0004 | Startup control |
| INT_STATUS0 | 0x0110 | Interrupt status |
| BATADC_CON0 | 0x0300 | Battery ADC |
| VPROC_CON0 | 0x0400 | CPU core voltage |
| VDRAM_CON0 | 0x0500 | DRAM voltage |

## Usage

```c
#include "mtk_pmic.h"

mtk_pmic_t pmic;

// Initialize
int ret = mtk_pmic_init(&pmic, 0x1000D000);
if (ret < 0) {
    // Error
}

// Read register
uint16_t val;
mtk_pmic_read(&pmic, MT6357_CID, &val);
printf("Chip ID: 0x%04X\n", val);

// Write register
mtk_pmic_write(&pmic, MT6357_TOP_CON, 0x1234);

// Set bits
mtk_pmic_set_bits(&pmic, MT6357_STRUP_CON0, 0x0001);

// Clear bits
mtk_pmic_clear_bits(&pmic, MT6357_STRUP_CON0, 0x0002);
```

## Testing

```bash
# Compile test program
gcc -o test_pmic test_pmic.c mtk_pmic.c

# Run (on device or emulator)
./test_pmic
```

## PMIC Wrapper Protocol

The MT6765 uses a "PMIC wrapper" to communicate with the MT6357:

1. **Write command to WFIFO** (Write FIFO)
   - Format: `[CMD:1][REG:15][DATA:16]`
   - CMD: 0=read, 1=write
   - REG: 15-bit register address
   - DATA: 16-bit value (for write)

2. **Wait for RFIFO** (Read FIFO)
   - For read: data appears in RFIFO
   - For write: wait for completion flag

3. **Read result**
   - Read from RFIFO for read operations
   - Check status for write operations

## References

- [Linux MT6397 driver](https://github.com/torvalds/linux/tree/master/drivers/mfd/mt6397-core.c)
- [MediaTek PMIC Wrapper](https://github.com/torvalds/linux/tree/master/drivers/soc/mediatek/mtk-pmic-wrap.c)
- MT6357 datasheet (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)

## Next Steps

1. Test on real hardware
2. Implement voltage regulator control
3. Add interrupt handling
4. Integrate with XNU power management
