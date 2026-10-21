# MTK UART Driver

MediaTek UART driver for MT6765 debug output.

## Overview

This driver provides serial console output for the Xiaomi Redmi 9A. It's essential for debugging during boot and runtime.

## Hardware

- **Controller**: MediaTek UART (8250 compatible)
- **Base Address**: 0x11002000 (UART0)
- **Baud Rate**: 115200 (default)
- **Data Format**: 8N1 (8 data bits, no parity, 1 stop bit)

## Features

### Implemented
- ✅ UART initialization
- ✅ Baud rate configuration
- ✅ Character transmit/receive
- ✅ String output
- ✅ Simple printf support
- ✅ TX/RX ready status
- ✅ Multiple UART support

### TODO
- [ ] DMA support
- [ ] Interrupt-driven I/O
- [ ] Hardware flow control (RTS/CTS)
- [ ] Power management

## Register Map

| Register | Offset | Description |
|---|---|---|
| RBR/THR | 0x00 | Receive/Transmit buffer |
| DLL/DLH | 0x00/0x04 | Divisor latch (baud) |
| IER | 0x04 | Interrupt enable |
| IIR/FCR | 0x08 | Interrupt ID / FIFO control |
| LCR | 0x0C | Line control |
| MCR | 0x10 | Modem control |
| LSR | 0x14 | Line status |
| MSR | 0x18 | Modem status |

## Usage

```c
#include "mtk_uart.h"

mtk_uart_t uart;

// Initialize UART
int ret = mtk_uart_init(&uart, MTK_UART0_BASE, UART_BAUD_115200);
if (ret < 0) {
    // Error
}

// Send string
mtk_uart_puts(&uart, "Hello World!\n");

// Send formatted output
mtk_uart_printf(&uart, "Value: %d, Hex: 0x%X\n", 42, 0xDEADBEEF);

// Send/receive characters
mtk_uart_putc(&uart, 'A');
char c = mtk_uart_getc(&uart);
```

## Printf Support

Limited printf with format specifiers:
- `%d` - Decimal integer
- `%u` - Unsigned integer
- `%x` / `%X` - Hexadecimal
- `%s` - String
- `%c` - Character
- `%f` - Float (limited precision)

## Baud Rates

| Baud | Divisor | Notes |
|---|---|---|
| 9600 | 169 | Slow, reliable |
| 57600 | 28 | Medium speed |
| 115200 | 14 | Standard (default) |

Formula: `divisor = clock / (16 * baud)`
Clock = 26MHz (typical MTK)

## Integration with XNU

For XNU integration:
1. Map UART registers to virtual memory
2. Implement early console (before MMU)
3. Register as console device
4. Implement kprintf backend
5. Support panic output

## Debug UART Connection

To use debug UART:
1. Connect USB-to-TTL adapter to UART0 pins
2. Set terminal to 115200 8N1
3. Connect:
   - TX → RX (device RX)
   - RX → TX (device TX)
   - GND → GND
   - **DO NOT connect VCC** (3.3V logic level)

## Testing

```bash
# Compile test program
gcc -o test_uart test_uart.c mtk_uart.c

# Run on device
./test_uart
```

Test cases:
1. Basic string output
2. Printf formatting
3. TX/RX ready flags
4. Loopback test
5. Baud rate switching
6. Multiple UARTs

## References

- [Linux MTK UART driver](https://github.com/torvalds/linux/tree/master/drivers/tty/serial/8250/8250_mtk.c)
- 8250 UART specification
- MTK UART documentation (not publicly available)

## Status

**Phase**: Basic bringup
**Tested**: No (requires hardware)
**Note**: Register addresses need verification

## Next Steps

1. Test on real hardware
2. Verify baud rate calculation
3. Add DMA support
4. Implement interrupt mode
5. Integrate with XNU console
6. Add early console support
