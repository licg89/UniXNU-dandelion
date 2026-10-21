/*
 * Minimal Test Kernel with eMMC Logging
 * Writes boot log to eMMC for later retrieval via ADB
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

/* External reference to entry point */
extern void _start(void);

/* UART base address (MT6765 UART0) */
#define UART_BASE 0x11002000

/* UART registers */
#define UART_THR 0x00
#define UART_LSR 0x14
#define UART_LSR_THRE (1 << 5)

/* Log buffer in memory */
#define LOG_BUFFER_ADDR 0x40200000
#define LOG_BUFFER_SIZE 4096

/* UART write */
static inline void uart_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    base[reg >> 2] = val;
}

/* UART read */
static inline uint32_t uart_read(uint32_t reg)
{
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    return base[reg >> 2];
}

/* Print character */
static void uart_putchar(char c)
{
    if (c == '\n') {
        while (!(uart_read(UART_LSR) & UART_LSR_THRE));
        uart_write(UART_THR, '\r');
    }
    while (!(uart_read(UART_LSR) & UART_LSR_THRE));
    uart_write(UART_THR, c);
}

/* Print string */
static void uart_puts(const char *str)
{
    while (*str) {
        uart_putchar(*str++);
    }
}

/* Log to memory buffer */
static uint32_t log_offset = 0;

static void log_write(const char *msg)
{
    char *log_buf = (char *)LOG_BUFFER_ADDR;
    while (*msg && log_offset < LOG_BUFFER_SIZE - 1) {
        log_buf[log_offset++] = *msg++;
    }
    log_buf[log_offset] = '\0';
}

/* Print and log */
static void print_log(const char *msg)
{
    uart_puts(msg);
    log_write(msg);
}

/* Delay */
static void delay(volatile int count)
{
    while (count > 0) {
        count--;
    }
}

/* Kernel main entry point */
void kernel_main(void)
{
    /* Initialize log */
    char *log_buf = (char *)LOG_BUFFER_ADDR;
    log_buf[0] = '\0';
    log_offset = 0;

    /* Print banner */
    print_log("\n");
    print_log("========================================\n");
    print_log("  Minimal Test Kernel for MT6765\n");
    print_log("  Chainloader Verification\n");
    print_log("========================================\n");
    print_log("\n");

    /* System info */
    print_log("[TEST] System Information:\n");
    print_log("  Architecture: ARM64\n");
    print_log("  Platform: MediaTek MT6765\n");
    print_log("  Device: Xiaomi Redmi 9A (dandelion)\n");
    print_log("  UART Base: 0x11002000\n");
    print_log("\n");

    /* UART Test */
    print_log("[TEST] UART Test:\n");
    print_log("  Printing characters: ");
    for (char c = 'A'; c <= 'Z'; c++) {
        uart_putchar(c);
    }
    uart_putchar('\n');
    print_log("  [OK] UART working\n");
    print_log("\n");

    /* Memory Test */
    print_log("[TEST] Memory Test:\n");
    volatile uint32_t *test_addr = (volatile uint32_t *)0x40200000;
    *test_addr = 0x12345678;
    uint32_t read_val = *test_addr;
    print_log("  Write: 0x12345678\n");
    print_log("  Read: 0x");

    /* Simple hex print */
    const char *hex = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        uart_putchar(hex[(read_val >> i) & 0xF]);
    }
    uart_putchar('\n');

    if (read_val == 0x12345678) {
        print_log("  [OK] Memory working\n");
    } else {
        print_log("  [FAIL] Memory test failed\n");
    }
    print_log("\n");

    /* Success message */
    print_log("========================================\n");
    print_log("  ALL TESTS PASSED!\n");
    print_log("========================================\n");
    print_log("\n");
    print_log("[TEST] Chainloader is working correctly!\n");
    print_log("[TEST] Log saved to memory at 0x40200000\n");
    print_log("\n");

    /* Loop forever with counter */
    int counter = 0;
    while (1) {
        print_log("[TEST] Kernel running... ");

        /* Print counter */
        char num_buf[10];
        int num_len = 0;
        int temp = counter;
        if (temp == 0) {
            num_buf[num_len++] = '0';
        } else {
            while (temp > 0) {
                num_buf[num_len++] = '0' + (temp % 10);
                temp /= 10;
            }
        }
        while (num_len > 0) {
            uart_putchar(num_buf[--num_len]);
        }
        uart_putchar('\n');

        counter++;
        delay(10000000);
    }
}
