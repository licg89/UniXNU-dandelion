# Novatek Touch Controller Driver

Novatek touch controller driver for Xiaomi Redmi 9A (dandelion).

## Overview

This driver provides touch input support for the Novatek touch controller used in the Xiaomi Redmi 9A. The controller communicates via I2C3 and supports multi-touch.

## Hardware

- **Controller**: Novatek touch IC
- **I2C Address**: 0x5D
- **I2C Bus**: I2C3 (0x1100F000)
- **IRQ**: 17 (MT_SYSIRQ)
- **Max Touches**: 5 points
- **Resolution**: 720 x 1600 (matches display)

## Features

### Implemented
- ✅ Touch controller initialization
- ✅ IC ID and firmware version readout
- ✅ Multi-touch support (up to 5 points)
- ✅ Touch coordinate reading
- ✅ Pressure and area detection
- ✅ Event type detection (down/move/up)

### TODO
- [ ] Interrupt-driven mode
- [ ] Calibration support
- [ ] Gesture recognition
- [ ] Power management
- [ ] Proximity detection

## Register Map

| Register | Address | Description |
|---|---|---|
| EVENT | 0x00 | Event status |
| TOUCH_NUM | 0x01 | Number of touches |
| X_H | 0x02 | X coordinate high |
| X_L | 0x03 | X coordinate low |
| Y_H | 0x04 | Y coordinate high |
| Y_L | 0x05 | Y coordinate low |
| PRESSURE | 0x06 | Touch pressure |
| AREA | 0x07 | Touch area |
| FW_VER | 0x10 | Firmware version |
| IC_ID | 0x12 | IC identifier |

## Usage

```c
#include "novatek_touch.h"

mtk_i2c_t i2c;
nova_touch_t touch;

// Initialize I2C
mtk_i2c_init(&i2c, MTK_I2C3_BASE, 3);
mtk_i2c_set_speed(&i2c, 400000);

// Initialize touch controller
nova_touch_init(&touch, &i2c, 0x5D);

// Read touch points
nova_touch_point_t points[5];
uint8_t count;
nova_touch_get_points(&touch, points, &count);

for (int i = 0; i < count; i++) {
    printf("Touch %d: (%d, %d)\n", i, points[i].x, points[i].y);
}
```

## Touch Data Structure

```c
typedef struct {
    uint16_t x;           // X coordinate (0-719)
    uint16_t y;           // Y coordinate (0-1599)
    uint8_t pressure;     // Touch pressure
    uint8_t area;         // Touch area
    uint8_t event_type;   // DOWN/MOVE/UP
    bool valid;           // Point is valid
} nova_touch_point_t;
```

## Testing

```bash
# Compile test program
gcc -o test_touch test_touch.c novatek_touch.c ../mtk-i2c/mtk_i2c.c

# Run on device
./test_touch
```

## Interrupt Handling

The touch controller uses IRQ 17. For interrupt-driven mode:
1. Configure GPIO for IRQ 17
2. Set up interrupt handler
3. Read touch data in interrupt context
4. Queue events for processing

## Integration with XNU

For XNU integration:
1. Implement IOKit driver (IOHIDEventDriver subclass)
2. Register interrupt handler
3. Map touch events to HID events
4. Integrate with display coordinates

## References

- [Linux Novatek NVT driver](https://github.com/torvalds/linux/tree/master/drivers/input/touchscreen/novatek)
- Novatek datasheet (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)

## Next Steps

1. Test on real hardware
2. Verify multi-touch functionality
3. Add interrupt support
4. Integrate with XNU IOKit
5. Implement gesture recognition
