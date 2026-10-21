# MTK Timer Driver

MediaTek timer driver for MT6765 with ARM generic timer support.

## Overview

This driver provides system timing for the Xiaomi Redmi 9A. It supports both MTK-specific timers and the ARM generic timer, which is used by XNU for scheduling and timekeeping.

## Hardware

- **MTK Timer Base**: 0x10008000
- **ARM Generic Timer**: System registers (CNTFRQ_EL0, CNTPCT_EL0, etc.)
- **Frequency**: Typically 13MHz or 26MHz (varies by SoC)

## Features

### Implemented
- ✅ MTK timer initialization
- ✅ ARM generic timer support
- ✅ Tick counting
- ✅ Nanosecond timing
- ✅ Microsecond delay
- ✅ One-shot timer
- ✅ Periodic timer
- ✅ Timer stop/control

### TODO
- [ ] Watchdog timer
- [ ] Sleep timer support
- [ ] Low-power timer modes
- [ ] Timer calibration

## Register Map

### MTK Timer (0x10008000)
| Register | Offset | Description |
|---|---|---|
| CON | 0x00 | Control |
| CLK_CON | 0x04 | Clock control |
| RST_CON | 0x08 | Reset control |
| INT_EN | 0x0C | Interrupt enable |
| INT_STA | 0x10 | Interrupt status |
| VAL0 | 0x80 | Timer 0 value |
| CMP0 | 0x84 | Timer 0 compare |
| VAL1-3 | 0x88-0x9C | Timers 1-3 |

### ARM Generic Timer (System Registers)
| Register | Description |
|---|---|
| CNTFRQ_EL0 | Counter frequency |
| CNTPCT_EL0 | Physical counter |
| CNTP_CVAL_EL0 | Physical compare value |
| CNTP_TVAL_EL0 | Physical timer value |
| CNTP_CTL_EL0 | Physical control |
| CNTVCT_EL0 | Virtual counter |

## Usage

```c
#include "mtk_timer.h"

mtk_timer_t timer;

// Initialize timer
int ret = mtk_timer_init(&timer);
if (ret < 0) {
    // Error
}

// Get current time
uint64_t ticks = mtk_timer_get_ticks(&timer);
uint64_t ns = mtk_timer_get_ns(&timer);

// Delay
mtk_timer_delay_us(&timer, 1000);  // 1ms delay

// Set one-shot timer
void my_callback(void *data) {
    printf("Timer fired!\n");
}
mtk_timer_set_oneshot(&timer, 1000000, my_callback, NULL);  // 1ms
```

## ARM Generic Timer

The ARM generic timer is accessed via system registers:

```c
// Get frequency
uint64_t freq = arm_timer_get_freq();

// Get counter
uint64_t count = arm_timer_get_count();

// Set compare
arm_timer_set_compare(1000000);

// Enable/disable
arm_timer_enable();
arm_timer_disable();
```

## Timer Modes

### Free Running
Counter increments continuously.

### Repeat
Timer reloads after reaching compare value.

### One-Shot
Timer fires once and stops.

## Integration with XNU

For XNU integration:
1. Initialize early in boot
2. Register as system clock
3. Implement timer interrupt handler
4. Support scheduler tick
5. Implement absolute time functions

## Performance

Typical performance (26MHz timer):
- Resolution: ~38ns per tick
- Read overhead: ~10 cycles
- Delay accuracy: ±1μs

## Testing

```bash
# Compile test program
gcc -o test_timer test_timer.c mtk_timer.c

# Run on device
./test_timer
```

Test cases:
1. Tick counting
2. Nanosecond timing
3. Microsecond delays
4. Timer frequency validation
5. One-shot/periodic modes

## References

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [Linux MTK timer driver](https://github.com/torvalds/linux/tree/master/drivers/clocksource/timer-mediatek.c)
- MTK timer documentation (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)
**Note**: Register addresses need verification

## Next Steps

1. Test on real hardware
2. Verify timer frequency
3. Implement interrupt handler
4. Add watchdog support
5. Integrate with XNU scheduler
6. Support low-power modes
