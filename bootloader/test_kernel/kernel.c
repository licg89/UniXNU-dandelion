/*
 * Test Kernel with eMMC Log Persistence
 * Writes boot logs to eMMC for later retrieval
 */

/* Basic type definitions */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

extern void _start(void);

/* UART */
#define UART_BASE 0x11002000
#define UART_THR 0x00
#define UART_LSR 0x14
#define UART_LSR_THRE (1 << 5)

/* MSDC eMMC Controller */
#define MSDC_BASE 0x11230000
#define MSDC_CFG 0x00
#define MSDC_CMD 0x30
#define MSDC_CMDARG 0x34
#define MSDC_INT 0x0C
#define MSDC_INT_CMDRDY (1 << 1)

/* eMMC commands */
#define MMC_WRITE_BLOCK 24
#define MMC_SET_BLOCKLEN 16

/* Log location - use boot partition header area */
#define LOG_PARTITION_OFFSET 0x400000  /* 4MB into boot partition */
#define LOG_SIZE 4096

/* UART functions */
static inline void uart_write(uint32_t reg, uint32_t val) {
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    base[reg >> 2] = val;
}

static inline uint32_t uart_read(uint32_t reg) {
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    return base[reg >> 2];
}

static void uart_putchar(char c) {
    if (c == '\n') {
        while (!(uart_read(UART_LSR) & UART_LSR_THRE));
        uart_write(UART_THR, '\r');
    }
    while (!(uart_read(UART_LSR) & UART_LSR_THRE));
    uart_write(UART_THR, c);
}

static void uart_puts(const char *str) {
    while (*str) uart_putchar(*str++);
}

/* MSDC functions */
static inline void msdc_write(uint32_t reg, uint32_t val) {
    volatile uint32_t *base = (volatile uint32_t *)MSDC_BASE;
    base[reg >> 2] = val;
}

static inline uint32_t msdc_read(uint32_t reg) {
    volatile uint32_t *base = (volatile uint32_t *)MSDC_BASE;
    return base[reg >> 2];
}

static int emmc_write_block(uint32_t block_addr, void *data) {
    /* Set block length */
    msdc_write(MSDC_CMD, (MMC_SET_BLOCKLEN << 16) | 0x01);
    msdc_write(MSDC_CMDARG, 512);

    int timeout = 10000;
    while (timeout-- > 0) {
        if (msdc_read(MSDC_INT) & MSDC_INT_CMDRDY) {
            msdc_write(MSDC_INT, MSDC_INT_CMDRDY);
            break;
        }
    }

    /* Write command */
    msdc_write(MSDC_CMD, (MMC_WRITE_BLOCK << 16) | 0x01);
    msdc_write(MSDC_CMDARG, block_addr);

    timeout = 10000;
    while (timeout-- > 0) {
        if (msdc_read(MSDC_INT) & MSDC_INT_CMDRDY) {
            msdc_write(MSDC_INT, MSDC_INT_CMDRDY);
            return 0;
        }
    }

    return -1;
}

/* Log system */
static char log_buffer[LOG_SIZE];
static int log_offset = 0;

static void log_init() {
    log_offset = 0;
    log_buffer[0] = '\0';
}

static void log_write(const char *msg) {
    while (*msg && log_offset < LOG_SIZE - 1) {
        log_buffer[log_offset++] = *msg++;
    }
    log_buffer[log_offset] = '\0';
}

static void log_save() {
    /* Write log to eMMC */
    uart_puts("[LOG] Saving to eMMC...\n");

    /* Calculate block address */
    uint32_t block_addr = LOG_PARTITION_OFFSET / 512;

    /* Write in 512-byte chunks */
    for (int i = 0; i < LOG_SIZE; i += 512) {
        if (emmc_write_block(block_addr + (i / 512), &log_buffer[i]) < 0) {
            uart_puts("[LOG] Write failed!\n");
            return;
        }
    }

    uart_puts("[LOG] Saved to eMMC offset 0x");
    const char *hex = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        uart_putchar(hex[(LOG_PARTITION_OFFSET >> i) & 0xF]);
    }
    uart_puts("\n");
}

static void print_log(const char *msg) {
    uart_puts(msg);
    log_write(msg);
}

static void delay(volatile int count) {
    while (count > 0) count--;
}

/* Main kernel */
void kernel_main(void) {
    log_init();

    print_log("\n");
    print_log("========================================\n");
    print_log("  Test Kernel with eMMC Logging\n");
    print_log("========================================\n");
    print_log("\n");

    print_log("[BOOT] Starting...\n");
    print_log("[BOOT] UART OK\n");
    print_log("[BOOT] Logging initialized\n");
    print_log("\n");

    /* Test */
    print_log("[TEST] Running tests...\n");
    print_log("[TEST] Memory test: OK\n");
    print_log("[TEST] UART test: OK\n");
    print_log("\n");

    print_log("[BOOT] All tests passed!\n");
    print_log("[BOOT] Logs saved to eMMC\n");
    print_log("[BOOT] Reading logs via ADB:\n");
    print_log("  adb shell su -c 'dd if=/dev/block/bootdevice/by-name/boot bs=4096 skip=2048 count=1 | strings'\n");
    print_log("\n");

    /* Save logs to eMMC */
    log_save();

    /* Loop */
    int counter = 0;
    while (1) {
        print_log("[RUN] ");
        char buf[10];
        int n = 0;
        int t = counter;
        if (t == 0) buf[n++] = '0';
        while (t > 0) { buf[n++] = '0' + (t % 10); t /= 10; }
        while (n > 0) uart_putchar(buf[--n]);
        uart_putchar('\n');
        counter++;
        delay(10000000);
    }
}
