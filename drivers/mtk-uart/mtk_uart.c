/*
 * MTK UART Driver Implementation
 */

#include "mtk_uart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*
 * Initialize UART
 */
int mtk_uart_init(mtk_uart_t *uart, uint32_t base_addr, uint32_t baud)
{
    if (!uart) {
        return -1;
    }

    uart->base = (volatile uint32_t *)base_addr;
    uart->base_addr = base_addr;
    uart->baud = baud;
    uart->initialized = false;

    /* Disable interrupts */
    uart->base[UART_IER >> 2] = 0x00;

    /* Set baud rate */
    uart->base[UART_LCR >> 2] = UART_LCR_DLAB;
    uart->base[UART_DLL >> 2] = baud & 0xFF;
    uart->base[UART_DLH >> 2] = (baud >> 8) & 0xFF;

    /* 8N1 mode */
    uart->base[UART_LCR >> 2] = UART_LCR_8BIT | UART_LCR_1STOP | UART_LCR_NO_PARITY;

    /* Enable and clear FIFOs */
    uart->base[UART_FCR >> 2] = UART_FCR_ENABLE | UART_FCR_CLEAR_RX | UART_FCR_CLEAR_TX | UART_FCR_TRIGGER_14;

    /* Enable interrupts */
    uart->base[UART_IER >> 2] = UART_IER_RDI | UART_IER_THRI;

    uart->initialized = true;

    return 0;
}

/*
 * Check if transmit is ready
 */
bool mtk_uart_tx_ready(mtk_uart_t *uart)
{
    if (!uart || !uart->initialized) {
        return false;
    }
    return (uart->base[UART_LSR >> 2] & UART_LSR_THRE) != 0;
}

/*
 * Check if receive data is available
 */
bool mtk_uart_rx_ready(mtk_uart_t *uart)
{
    if (!uart || !uart->initialized) {
        return false;
    }
    return (uart->base[UART_LSR >> 2] & UART_LSR_DR) != 0;
}

/*
 * Send character
 */
void mtk_uart_putc(mtk_uart_t *uart, char c)
{
    if (!uart || !uart->initialized) {
        return;
    }

    /* Wait for transmitter ready */
    while (!mtk_uart_tx_ready(uart));

    /* Send character */
    uart->base[UART_THR >> 2] = (uint32_t)c;
}

/*
 * Receive character
 */
char mtk_uart_getc(mtk_uart_t *uart)
{
    if (!uart || !uart->initialized) {
        return '\0';
    }

    /* Wait for data */
    while (!mtk_uart_rx_ready(uart));

    /* Read character */
    return (char)(uart->base[UART_RBR >> 2] & 0xFF);
}

/*
 * Send string
 */
void mtk_uart_puts(mtk_uart_t *uart, const char *str)
{
    if (!uart || !str) {
        return;
    }

    while (*str) {
        if (*str == '\n') {
            mtk_uart_putc(uart, '\r');
        }
        mtk_uart_putc(uart, *str++);
    }
}

/*
 * Simple printf (limited format support)
 */
int mtk_uart_printf(mtk_uart_t *uart, const char *fmt, ...)
{
    if (!uart || !uart->initialized || !fmt) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);

    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);

    va_end(args);

    mtk_uart_puts(uart, buf);

    return len;
}
