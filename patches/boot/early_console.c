/*
 * Early Console Implementation
 *
 * Minimal UART driver for early boot messages
 * Works before MMU, before BSS, before heap
 */

#include "early_console.h"
#include <stdarg.h>

/* Global console state (use BSS) */
static early_console_t early_con;

/*
 * Low-level UART write (no buffering)
 */
static inline void uart_write(uint32_t reg, uint32_t val)
{
    early_con.base[reg >> 2] = val;
}

static inline uint32_t uart_read(uint32_t reg)
{
    return early_con.base[reg >> 2];
}

/*
 * Initialize early console
 */
int early_console_init(void)
{
    /* Map UART base (identity mapped before MMU) */
    early_con.base = (volatile uint32_t *)EARLY_UART_BASE;
    early_con.initialized = 0;

    /* Disable interrupts */
    uart_write(0x04, 0x00);  /* IER = 0 */

    /* Set baud rate */
    uart_write(UART_LCR, UART_LCR_DLAB);
    uart_write(UART_DLL, UART_BAUD_115200 & 0xFF);
    uart_write(UART_DLH, (UART_BAUD_115200 >> 8) & 0xFF);

    /* 8N1 mode */
    uart_write(UART_LCR, UART_LCR_8N1);

    /* Enable and clear FIFOs */
    uart_write(UART_FCR, UART_FCR_ENABLE);

    early_con.initialized = 1;

    return 0;
}

/*
 * Output a single character
 */
void early_console_putc(char c)
{
    if (!early_con.initialized) {
        return;
    }

    /* Wait for transmitter ready */
    while (!(uart_read(UART_LSR) & UART_LSR_THRE)) {
        __asm__ volatile("" ::: "memory");
    }

    /* Send character */
    uart_write(UART_THR, (uint32_t)c);
}

/*
 * Output a string
 */
void early_console_puts(const char *str)
{
    if (!str) {
        return;
    }

    while (*str) {
        if (*str == '\n') {
            early_console_putc('\r');
        }
        early_console_putc(*str);
        str++;
    }
}

/*
 * Output hex value
 */
void early_console_put_hex(uint64_t val)
{
    const char *hex_chars = "0123456789ABCDEF";
    int i;
    int started = 0;

    early_console_puts("0x");

    for (i = 60; i >= 0; i -= 4) {
        uint8_t digit = (val >> i) & 0xF;
        if (digit != 0 || started || i == 0) {
            early_console_putc(hex_chars[digit]);
            started = 1;
        }
    }
}

/*
 * Output decimal value
 */
void early_console_put_dec(uint64_t val)
{
    char buf[20];
    int i = 0;

    if (val == 0) {
        early_console_putc('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (--i >= 0) {
        early_console_putc(buf[i]);
    }
}

/*
 * Simple printf (limited format support)
 */
int early_console_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int val = va_arg(args, int);
                    if (val < 0) {
                        early_console_putc('-');
                        val = -val;
                    }
                    early_console_put_dec((uint64_t)val);
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    early_console_put_dec((uint64_t)val);
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned int val = va_arg(args, unsigned int);
                    early_console_put_hex((uint64_t)val);
                    break;
                }
                case 'p': {
                    void *ptr = va_arg(args, void *);
                    early_console_put_hex((uint64_t)ptr);
                    break;
                }
                case 's': {
                    const char *str = va_arg(args, const char *);
                    early_console_puts(str);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    early_console_putc(c);
                    break;
                }
                case '%':
                    early_console_putc('%');
                    break;
                default:
                    early_console_putc('%');
                    early_console_putc(*fmt);
                    break;
            }
        } else {
            if (*fmt == '\n') {
                early_console_putc('\r');
            }
            early_console_putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
    return 0;
}
