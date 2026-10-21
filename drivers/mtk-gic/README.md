# MTK GIC (Generic Interrupt Controller) Driver

ARM GICv2 driver for MT6765 interrupt handling.

## Overview

This driver provides interrupt handling for the Xiaomi Redmi 9A. It implements the ARM Generic Interrupt Controller (GIC) v2, which manages all system interrupts including timers, peripherals, and inter-processor interrupts.

## Hardware

- **Controller**: ARM GICv2
- **GICD Base**: 0x10221000 (Distributor)
- **GICC Base**: 0x10222000 (CPU Interface)
- **CPU Cores**: 4 (Cortex-A53)
- **Max IRQs**: Up to 1020

## Features

### Implemented
- ✅ GIC distributor initialization
- ✅ CPU interface configuration
- ✅ Interrupt enable/disable
- ✅ Priority setting (256 levels)
- ✅ Target CPU assignment
- ✅ Handler registration
- ✅ Software generated interrupts (SGI)
- ✅ Interrupt acknowledgment and EOI

### TODO
- [ ] Affinity routing (GICv2 extension)
- [ ] Secure/non-secure group handling
- [ ] Power management integration
- [ ] Wakeup source configuration

## Interrupt Types

| Type | Range | Description |
|---|---|---|
| SGI | 0-15 | Software Generated (IPI) |
| PPI | 16-31 | Private Peripheral (per-CPU) |
| SPI | 32-1019 | Shared Peripheral |

## Key Interrupts

| IRQ | Peripheral | Description |
|---|---|---|
| 11 | TIMER0 | System timer |
| 12 | TIMER1 | Timer 1 |
| 51 | UART0 | Debug UART |
| 106-109 | I2C0-3 | I2C controllers |
| 110-111 | MSDC0-1 | Storage controllers |
| 161 | PMIC | Power management |
| 17 | TOUCH | Touch controller |

## Register Map

### Distributor (GICD)
| Register | Offset | Description |
|---|---|---|
| CTLR | 0x0000 | Control |
| TYPER | 0x0004 | Type |
| IGROUPR | 0x0080 | Interrupt group |
| ISENABLER | 0x0100 | Enable set |
| ICENABLER | 0x0180 | Enable clear |
| IPRIORITYR | 0x0400 | Priority |
| ITARGETSR | 0x0800 | Target CPU |
| ICFGR | 0x0C00 | Configuration |
| SGIR | 0x0F00 | Software interrupt |

### CPU Interface (GICC)
| Register | Offset | Description |
|---|---|---|
| CTLR | 0x0000 | Control |
| PMR | 0x0004 | Priority mask |
| BPR | 0x0008 | Binary point |
| IAR | 0x000C | Interrupt acknowledge |
| EOIR | 0x0010 | End of interrupt |

## Usage

```c
#include "mtk_gic.h"

mtk_gic_t gic;

// Initialize GIC
int ret = mtk_gic_init(&gic);
if (ret < 0) {
    // Error
}

// Register handler
void my_handler(uint32_t irq, void *data) {
    printf("IRQ %u fired\n", irq);
}
mtk_gic_register_handler(&gic, GIC_IRQ_TOUCH, my_handler, NULL);

// Configure interrupt
mtk_gic_set_priority(&gic, GIC_IRQ_TOUCH, GIC_PRIORITY_HIGH);
mtk_gic_set_target(&gic, GIC_IRQ_TOUCH, 0);
mtk_gic_enable_irq(&gic, GIC_IRQ_TOUCH);

// In exception handler
mtk_gic_handle_irq(&gic);
```

## Interrupt Flow

1. Peripheral asserts IRQ line
2. GIC receives interrupt
3. GIC prioritizes and routes to CPU
4. CPU takes IRQ exception
5. Exception handler calls `mtk_gic_handle_irq()`
6. GIC returns interrupt ID via IAR
7. Registered handler is called
8. End of interrupt signaled via EOIR

## Priority Levels

- 0: Highest priority
- 255: Lowest priority
- Typical usage:
  - 0-31: Critical (timers, watchdog)
  - 32-127: High priority (touch, display)
  - 128-223: Normal (I2C, UART)
  - 224-255: Low (background tasks)

## Integration with XNU

For XNU integration:
1. Map GIC registers to virtual memory
2. Install exception handler for IRQ
3. Implement interrupt dispatch table
4. Support IOKit interrupt registration
5. Handle MP interrupt routing

## References

- [ARM GIC Architecture Specification](https://developer.arm.com/documentation/ihi0048/latest)
- [Linux GIC driver](https://github.com/torvalds/linux/tree/master/drivers/irqchip/irq-gic.c)
- ARM DDI 0471 (GICv2/v3)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)
**Note**: Register addresses need verification

## Next Steps

1. Test on real hardware
2. Verify interrupt routing
3. Implement SMP support
4. Add power management
5. Integrate with XNU interrupt framework
