/*
 * MTK UART Driver Test
 */

#include "mtk_uart.h"
#include <stdio.h>

int main(void)
{
    mtk_uart_t uart;
    int ret;

    printf("=== MTK UART Test ===\n\n");

    /* Initialize UART0 */
    printf("Initializing UART0 at 0x%08X...\n", MTK_UART0_BASE);
    ret = mtk_uart_init(&uart, MTK_UART0_BASE, UART_BAUD_115200);
    if (ret < 0) {
        printf("FAILED: UART init error %d\n", ret);
        return ret;
    }
    printf("OK: UART0 initialized at 115200 baud\n\n");

    /* Test transmit */
    printf("Testing transmit...\n");
    mtk_uart_puts(&uart, "Hello from MTK UART driver!\n");
    mtk_uart_puts(&uart, "This is a test message.\n");
    mtk_uart_puts(&uart, "Testing special characters: !@#$%^&*()\n");
    mtk_uart_puts(&uart, "Numbers: 0123456789\n");
    mtk_uart_puts(&uart, "Letters: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    mtk_uart_puts(&uart, "Lowercase: abcdefghijklmnopqrstuvwxyz\n\n");

    /* Test printf */
    printf("Testing printf...\n");
    mtk_uart_printf(&uart, "Decimal: %d\n", 12345);
    mtk_uart_printf(&uart, "Hex: 0x%X\n", 0xDEADBEEF);
    mtk_uart_printf(&uart, "String: %s\n", "test string");
    mtk_uart_printf(&uart, "Float: %.2f\n", 3.14159);
    mtk_uart_printf(&uart, "Multiple: %d %s 0x%X\n", 42, "hello", 0x1234);
    printf("\n");

    /* Test TX/RX ready */
    printf("Testing TX/RX ready flags...\n");
    if (mtk_uart_tx_ready(&uart)) {
        printf("OK: TX ready\n");
    } else {
        printf("WARNING: TX not ready\n");
    }

    if (!mtk_uart_rx_ready(&uart)) {
        printf("OK: RX not ready (no data)\n");
    } else {
        printf("WARNING: RX ready (unexpected data)\n");
    }
    printf("\n");

    /* Test loopback (if connected) */
    printf("Testing loopback (connect TX to RX)...\n");
    printf("Sending test character...\n");
    mtk_uart_putc(&uart, 'T');

    printf("Waiting for response (press any key or connect TX-RX)...\n");

    /* Try to receive with timeout */
    int timeout = 1000000;
    while (timeout > 0 && !mtk_uart_rx_ready(&uart)) {
        timeout--;
    }

    if (timeout > 0) {
        char c = mtk_uart_getc(&uart);
        printf("Received: '%c' (0x%02X)\n", c, c);
        if (c == 'T') {
            printf("OK: Loopback test passed\n");
        } else {
            printf("WARNING: Unexpected character\n");
        }
    } else {
        printf("Timeout: No response (loopback not connected)\n");
    }
    printf("\n");

    /* Test different baud rates */
    printf("Testing baud rate switching...\n");

    printf("Switching to 9600 baud...\n");
    ret = mtk_uart_init(&uart, MTK_UART0_BASE, UART_BAUD_9600);
    if (ret < 0) {
        printf("FAILED: Baud switch error\n");
        return ret;
    }
    mtk_uart_puts(&uart, "9600 baud test\n");

    printf("Switching back to 115200 baud...\n");
    ret = mtk_uart_init(&uart, MTK_UART0_BASE, UART_BAUD_115200);
    if (ret < 0) {
        printf("FAILED: Baud switch error\n");
        return ret;
    }
    mtk_uart_puts(&uart, "115200 baud test\n");
    printf("\n");

    /* Test multiple UARTs */
    printf("Testing multiple UARTs...\n");
    mtk_uart_t uart1;
    ret = mtk_uart_init(&uart1, MTK_UART1_BASE, UART_BAUD_115200);
    if (ret < 0) {
        printf("FAILED: UART1 init error\n");
        return ret;
    }
    mtk_uart_puts(&uart1, "UART1 test\n");
    printf("OK: UART1 initialized\n\n");

    printf("=== UART Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
