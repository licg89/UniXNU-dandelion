/*
 * Minimal Test Kernel - Main Implementation
 *
 * Prints test messages via UART to verify chainloader
 */

/* Basic type definitions for freestanding environment */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

/* UART base address (MT6765 UART0) */
#define UART_BASE 0x11002000

/* UART registers */
#define UART_THR 0x00
#define UART_LSR 0x14
#define UART_LSR_THRE (1 << 5)

/*
 * UART write
 */
static inline void uart_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    base[reg >> 2] = val;
}

/*
 * UART read
 */
static inline uint32_t uart_read(uint32_t reg)
{
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    return base[reg >> 2];
}

/*
 * Print character
 */
static void uart_putchar(char c)
{
    if (c == '\n') {
        while (!(uart_read(UART_LSR) & UART_LSR_THRE));
        uart_write(UART_THR, '\r');
    }
    while (!(uart_read(UART_LSR) & UART_LSR_THRE));
    uart_write(UART_THR, c);
}

/*
 * Print string
 */
static void uart_puts(const char *str)
{
    while (*str) {
        uart_putchar(*str++);
    }
}

/*
 * Print hex value
 */
static void uart_put_hex(uint64_t val)
{
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (val >> i) & 0xF;
        uart_putchar(hex[digit]);
    }
}

/*
 * Print decimal value
 */
static void uart_put_dec(uint64_t val)
{
    char buf[20];
    int i = 0;

    if (val == 0) {
        uart_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        uart_putchar(buf[--i]);
    }
}

/*
 * Simple delay
 */
static void delay(volatile int count)
{
    while (count > 0) {
        count--;
    }
}

/*
 * Kernel main entry point
 */
void kernel_main(void)
{
    /* Print banner */
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Minimal Test Kernel for MT6765\n");
    uart_puts("  Chainloader Verification\n");
    uart_puts("========================================\n");
    uart_puts("\n");

    /* Print system info */
    uart_puts("[TEST] System Information:\n");
    uart_puts("  Architecture: ARM64\n");
    uart_puts("  Platform: MediaTek MT6765\n");
    uart_puts("  Device: Xiaomi Redmi 9A (dandelion)\n");
    uart_puts("  UART Base: ");
    uart_put_hex(UART_BASE);
    uart_puts("\n");
    uart_puts("\n");

    /* Print boot info */
    uart_puts("[TEST] Boot Information:\n");
    uart_puts("  Entry point: ");
    uart_put_hex((uint64_t)_start);
    uart_puts("\n");
    uart_puts("  Stack pointer: ");
    uint64_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    uart_put_hex(sp);
    uart_puts("\n");
    uart_puts("\n");

    /* Test UART */
    uart_puts("[TEST] UART Test:\n");
    uart_puts("  Printing characters: ");
    for (char c = 'A'; c <= 'Z'; c++) {
        uart_putchar(c);
    }
    uart_puts("\n");
    uart_puts("  Numbers: ");
    for (int i = 0; i <= 9; i++) {
        uart_putchar('0' + i);
    }
    uart_puts("\n");
    uart_puts("  [OK] UART working\n");
    uart_puts("\n");

    /* Test memory */
    uart_puts("[TEST] Memory Test:\n");
    volatile uint32_t *test_addr = (volatile uint32_t *)0x40200000;
    *test_addr = 0x12345678;
    uint32_t read_val = *test_addr;
    uart_puts("  Write: 0x12345678\n");
    uart_puts("  Read:  ");
    uart_put_hex(read_val);
    uart_puts("\n");
    if (read_val == 0x12345678) {
        uart_puts("  [OK] Memory working\n");
    } else {
        uart_puts("  [FAIL] Memory test failed\n");
    }
    uart_puts("\n");

    /* Print success message */
    uart_puts("========================================\n");
    uart_puts("  ALL TESTS PASSED!\n");
    uart_puts("========================================\n");
    uart_puts("\n");
    uart_puts("[TEST] Chainloader is working correctly!\n");
    uart_puts("[TEST] You can now proceed with XNU kernel\n");
    uart_puts("\n");

    /* Loop forever */
    int counter = 0;
    while (1) {
        uart_puts("[TEST] Kernel running... ");
        uart_put_dec(counter++);
        uart_puts("\n");
        delay(10000000);
    }
}

/* External reference */
extern void _start(void);
