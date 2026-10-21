# MTK I2C Controller Driver

MediaTek I2C controller driver for MT6765 (Helio G25).

## Overview

The MT6765 has 7 I2C controllers. This driver provides basic I2C master functionality for communicating with touch controllers, sensors, and other I2C devices.

## Hardware

| Controller | Base Address | Typical Use |
|---|---|---|
| I2C0 | 0x11007000 | General purpose |
| I2C1 | 0x11008000 | General purpose |
| I2C2 | 0x11009000 | General purpose |
| **I2C3** | **0x1100F000** | **Touch controller (Novatek @ 0x5D)** |
| I2C4 | 0x11011000 | General purpose |
| I2C5 | 0x11016000 | General purpose |
| I2C6 | 0x1100D000 | General purpose |

## Features

### Implemented
- ✅ I2C controller initialization
- ✅ Read/write operations
- ✅ Speed configuration (100kHz, 400kHz)
- ✅ Bus scanning
- ✅ Error handling

### TODO
- [ ] DMA support
- [ ] High-speed mode (3.4MHz)
- [ ] Multi-master support
- [ ] Interrupt-driven transfers

## Usage

```c
#include "mtk_i2c.h"

mtk_i2c_t i2c;

// Initialize I2C3 (touch controller bus)
int ret = mtk_i2c_init(&i2c, MTK_I2C3_BASE, 3);
if (ret < 0) {
    // Error
}

// Set speed to 400kHz
mtk_i2c_set_speed(&i2c, 400000);

// Read from touch controller
uint8_t data[10];
ret = mtk_i2c_read(&i2c, 0x5D, 0x00, data, sizeof(data));

// Write to device
uint8_t cmd[] = {0x01, 0x02};
ret = mtk_i2c_write(&i2c, 0x5D, 0x10, cmd, sizeof(cmd));

// Scan bus for devices
uint8_t devices[32];
uint32_t count;
mtk_i2c_scan(&i2c, devices, &count);
```

## Testing

```bash
# Compile test program
gcc -o test_i2c test_i2c.c mtk_i2c.c

# Run on device
./test_i2c
```

## I2C Protocol

Standard I2C master mode:
1. Send START condition
2. Send slave address + R/W bit
3. Send register address
4. Send/receive data
5. Send STOP condition

## References

- [Linux MTK I2C driver](https://github.com/torvalds/linux/tree/master/drivers/i2c/busses/i2c-mt65xx.c)
- MT6765 datasheet (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)

## Next Steps

1. Test on real hardware
2. Verify touch controller communication
3. Add DMA support for large transfers
4. Integrate with XNU IOKit
