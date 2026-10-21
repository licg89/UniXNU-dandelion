# MTK DDP Display Driver

MediaTek Display Data Path driver for MT6765 with NT36525B MIPI DSI panel.

## Overview

This driver provides display output for the Xiaomi Redmi 9A. It handles the MTK display pipeline (DDP), MIPI DSI controller, and NT36525B panel initialization.

## Hardware

- **SoC**: MediaTek MT6765 (Helio G25)
- **Display**: 720 x 1600, 60Hz
- **Panel**: Novatek NT36525B
- **Interface**: MIPI DSI (2 lanes)
- **Color Depth**: 24-bit RGB888

## Display Pipeline

MTK display pipeline consists of:
1. **OVL** (Overlay) - Layer composition
2. **RDMA** (Read DMA) - Framebuffer readout
3. **COLOR** - Color processing
4. **AAL** - Adaptive ambient light
5. **GAMMA** - Gamma correction
6. **DITHER** - Dithering
7. **DSI** - MIPI DSI output
8. **MIPI TX** - PHY layer

## Features

### Implemented
- ✅ Display controller initialization
- ✅ MIPI DSI configuration
- ✅ NT36525B panel initialization
- ✅ Video mode support
- ✅ Framebuffer setup
- ✅ Color fill operations
- ✅ Display enable/disable

### TODO
- [ ] Command mode support
- [ ] Multi-layer composition (OVL)
- [ ] Gamma correction
- [ ] Brightness control
- [ ] Panel calibration
- [ ] VSync support
- [ ] Power management

## Register Map

### MIPI DSI Controller (0x14014000)
| Register | Offset | Description |
|---|---|---|
| INTEN | 0x0000 | Interrupt enable |
| INTSTA | 0x0004 | Interrupt status |
| CON_CTRL | 0x0010 | Control |
| MODE_CTRL | 0x0014 | Mode control |
| TXQ_CTRL | 0x0018 | TX queue |
| PS_CTRL | 0x001C | Packet size |
| HSA_WC | 0x0028 | HSA word count |
| HBP_WC | 0x002C | HBP word count |
| HFP_WC | 0x0030 | HFP word count |
| VM_CTRL | 0x0040 | Video mode |
| FB_WIDTH | 0x0044 | Framebuffer width |
| FB_HEIGHT | 0x0048 | Framebuffer height |

### RDMA (0x1400A000)
| Register | Offset | Description |
|---|---|---|
| CON | 0x0020 | Control |
| SRC_BASE | 0x00F0 | Source base |
| SRC_W_IN_BYTE | 0x00F4 | Source width |
| PITCH | 0x00F8 | Pitch |
| SRC_HEIGHT | 0x0100 | Source height |

### MIPI TX PHY (0x10012000)
| Register | Offset | Description |
|---|---|---|
| LANE_CON | 0x0000 | Lane control |
| PLL_CON | 0x0010 | PLL control |
| PLL_DIV | 0x0014 | PLL divider |

## Usage

```c
#include "mtk_ddp.h"

mtk_ddp_t ddp;

// Initialize display
int ret = mtk_ddp_init(&ddp, 0x48000000, 32*1024*1024);
if (ret < 0) {
    // Error
}

// Set video mode
mtk_ddp_set_mode(&ddp, DDP_VIDEO_MODE);

// Enable display
mtk_ddp_enable(&ddp);

// Draw red screen
mtk_ddp_fill_rect(&ddp, 0, 0, 720, 1600, 0x00FF0000);
```

## NT36525B Panel Init

The panel initialization sequence needs to be extracted from:
- LK (Little Kernel) bootloader
- Android kernel driver
- Panel vendor documentation

Current implementation uses a simplified sequence. Real sequence requires:
1. Power on sequence (VCC, VDD, VDDIO)
2. Reset sequence (GPIO toggle)
3. Sleep out command (0x11)
4. Display on command (0x29)
5. Panel-specific configuration commands

## Framebuffer Format

```c
typedef struct {
    uint32_t base;        // Physical address
    uint32_t size;        // Size in bytes
    uint32_t width;       // Width in pixels
    uint32_t height;      // Height in pixels
    uint32_t pitch;       // Bytes per line
    ddp_format_t format;  // RGB565/RGB666/RGB888
} ddp_fb_t;
```

For 720x1600 @ RGB888:
- Width: 720 pixels
- Height: 1600 pixels
- Pitch: 720 * 3 = 2160 bytes
- Size: 2160 * 1600 = 3,456,000 bytes (~3.3 MB)

## Testing

```bash
# Compile test program
gcc -o test_display test_display.c mtk_ddp.c

# Run on device
./test_display
```

Test patterns:
1. Red screen
2. Green screen
3. Blue screen
4. Color bars (RGBW)
5. Gradient

## Integration with XNU

For XNU integration:
1. Implement IOKit driver (IOFramebuffer subclass)
2. Map framebuffer to I/O memory
3. Implement mode setting
4. Handle VSync interrupts
5. Support multiple displays (future)

## References

- [Linux MTK DRM driver](https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/mediatek)
- [MTK DSI documentation](https://www.kernel.org/doc/html/latest/gpu/mediatek-dsi.html)
- NT36525B datasheet (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)
**Note**: Register addresses need verification on real hardware

## Next Steps

1. Extract real panel init sequence from LK
2. Verify register addresses
3. Test on real hardware
4. Add gamma and color correction
5. Implement IOKit integration
6. Add power management
