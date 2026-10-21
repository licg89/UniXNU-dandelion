/*
 * MTK UART Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * MediaTek UART driver for MT6765 debug output
 */

#ifndef _MTK_UART_H_
#define _MTK_UART_H_

#include <stdint.h>
#include <stdbool.h>

/* UART base addresses */
#define MTK_UART0_BASE      0x11002000  /* Debug UART */
#define MTK_UART1_BASE      0x11003000
#define MTK_UART2_BASE      0x11004000
#define MTK_UART3_BASE      0x11005000

/* UART registers (8250 compatible) */
#define UART_RBR            0x00  /* Receive buffer (read) */
#define UART_THR            0x00  /* Transmit holding (write) */
#define UART_DLL            0x00  /* Divisor latch low */
#define UART_DLH            0x04  /* Divisor latch high */
#define UART_IER            0x04  /* Interrupt enable */
#define UART_IIR            0x08  /* Interrupt identification (read) */
#define UART_FCR            0x08  /* FIFO control (write) */
#define UART_LCR            0x0C  /* Line control */
#define UART_MCR            0x10  /* Modem control */
#define UART_LSR            0x14  /* Line status */
#define UART_MSR            0x18  /* Modem status */

/* Line control bits */
#define UART_LCR_DLAB       (1 << 7)  /* Divisor latch access */
#define UART_LCR_8BIT       0x03      /* 8 data bits */
#define UART_LCR_1STOP      0x00      /* 1 stop bit */
#define UART_LCR_NO_PARITY  0x00      /* No parity */

/* FIFO control bits */
#define UART_FCR_ENABLE     (1 << 0)  /* Enable FIFO */
#define UART_FCR_CLEAR_RX   (1 << 1)  /* Clear receive FIFO */
#define UART_FCR_CLEAR_TX   (1 << 2)  /* Clear transmit FIFO */
#define UART_FCR_TRIGGER_14 (3 << 6)  /* 14 byte trigger level */

/* Line status bits */
#define UART_LSR_DR         (1 << 0)  /* Data ready */
#define UART_LSR_OE         (1 << 1)  /* Overrun error */
#define UART_LSR_PE         (1 << 2)  /* Parity error */
#define UART_LSR_FE         (1 << 3)  /* Framing error */
#define UART_LSR_BI         (1 << 4)  /* Break interrupt */
#define UART_LSR_THRE       (1 << 5)  /* Transmitter holding register empty */
#define UART_LSR_TEMT       (1 << 6)  /* Transmitter empty */

/* Interrupt enable bits */
#define UART_IER_RDI        (1 << 0)  /* Receive data available */
#define UART_IER_THRI       (1 << 1)  /* Transmitter holding register empty */

/* Baud rate divisor for 115200 baud (26MHz clock) */
#define UART_BAUD_115200    14
#define UART_BAUD_9600      169
#define UART_BAUD_57600     28

/* UART context */
typedef struct {
    volatile uint32_t *base;
    uint32_t base_addr;
    uint32_t baud;
    bool initialized;
} mtk_uart_t;

/* Function prototypes */
int mtk_uart_init(mtk_uart_t *uart, uint32_t base_addr, uint32_t baud);
void mtk_uart_putc(mtk_uart_t *uart, char c);
char mtk_uart_getc(mtk_uart_t *uart);
void mtk_uart_puts(mtk_uart_t *uart, const char *str);
bool mtk_uart_rx_ready(mtk_uart_t *uart);
bool mtk_uart_tx_ready(mtk_uart_t *uart);
int mtk_uart_printf(mtk_uart_t *uart, const char *fmt, ...);

#endif /* _MTK_UART_H_ */
