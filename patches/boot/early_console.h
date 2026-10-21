/*
 * Early Console for MT6765
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Minimal UART driver for early boot messages
 * Works before MMU is enabled
 */

#ifndef _EARLY_CONSOLE_H_
#define _EARLY_CONSOLE_H_

#include "unixnu_types.h"

/* UART0 base address (physical) */
#define EARLY_UART_BASE     0x11002000ULL

/* UART registers (8250 compatible, 32-bit aligned) */
#define UART_THR            0x00    /* Transmit holding register */
#define UART_LSR            0x14    /* Line status register */
#define UART_DLL            0x00    /* Divisor latch low (DLAB=1) */
#define UART_DLH            0x04    /* Divisor latch high (DLAB=1) */
#define UART_LCR            0x0C    /* Line control register */
#define UART_FCR            0x08    /* FIFO control register */

/* UART bits */
#define UART_LCR_DLAB       (1 << 7)    /* Divisor latch access */
#define UART_LCR_8N1        0x03        /* 8 data bits, no parity, 1 stop */
#define UART_FCR_ENABLE     0x07        /* Enable and clear FIFOs */
#define UART_LSR_THRE       (1 << 5)    /* Transmitter holding register empty */

/* Clock and baud */
#define UART_CLOCK          26000000    /* 26 MHz */
#define UART_BAUD_115200    14          /* Divisor for 115200 */

/* Early console state */
typedef struct {
    volatile uint32_t *base;
    int initialized;
} early_console_t;

/* Function prototypes */
int early_console_init(void);
void early_console_putc(char c);
void early_console_puts(const char *str);
void early_console_put_hex(uint64_t val);
void early_console_put_dec(uint64_t val);
int early_console_printf(const char *fmt, ...);

#endif /* _EARLY_CONSOLE_H_ */
