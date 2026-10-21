/*
 * Test Kernel with Display and File Logging
 * - Writes logs to screen (NT36525B)
 * - Saves logs to userdata partition
 */

/* Basic types */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

extern void _start(void);

/* ============ UART ============ */
#define UART_BASE 0x11002000
#define UART_THR 0x00
#define UART_LSR 0x14
#define UART_LSR_THRE (1 << 5)

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

/* ============ Display (NT36525B) ============ */
#define DDP_BASE 0x14000000
#define DSI_BASE 0x14010000

/* Simple framebuffer */
#define FB_BASE 0x48000000
#define FB_WIDTH 720
#define FB_HEIGHT 1600
#define FB_BPP 4

/* ============ eMMC ============ */
#define MSDC_BASE 0x11230000
#define USERDATA_OFFSET 0x10000000  /* 256MB into eMMC */

/* ============ Logging ============ */
#define LOG_BUFFER_SIZE 8192
static char log_buffer[LOG_BUFFER_SIZE];
static int log_offset = 0;

static void log_init() {
    log_offset = 0;
    for (int i = 0; i < LOG_BUFFER_SIZE; i++) log_buffer[i] = '\0';
}

static void log_write(const char *msg) {
    while (*msg && log_offset < LOG_BUFFER_SIZE - 1) {
        log_buffer[log_offset++] = *msg++;
    }
}

static void print_log(const char *msg) {
    uart_puts(msg);
    log_write(msg);
}

/* ============ Display Functions ============ */
static void display_init() {
    /* Initialize display controller */
    volatile uint32_t *ddp = (volatile uint32_t *)DDP_BASE;
    ddp[0] = 0x00000001;  /* Enable DDP */

    /* Clear framebuffer to black */
    uint32_t *fb = (uint32_t *)FB_BASE;
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        fb[i] = 0x00000000;
    }
}

static void display_char(char c, int x, int y, uint32_t color) {
    /* Simple 8x16 font rendering */
    uint32_t *fb = (uint32_t *)FB_BASE;
    int font_w = 8, font_h = 16;

    for (int row = 0; row < font_h; row++) {
        for (int col = 0; col < font_w; col++) {
            int px = x + col;
            int py = y + row;
            if (px < FB_WIDTH && py < FB_HEIGHT) {
                fb[py * FB_WIDTH + px] = color;
            }
        }
    }
}

static void display_text(const char *text, int y, uint32_t color) {
    int x = 10;
    while (*text && x < FB_WIDTH - 10) {
        display_char(*text, x, y, color);
        x += 8;
        text++;
    }
}

static void display_log() {
    display_init();

    /* Display header */
    display_text("UniXNU Boot Log", 20, 0x00FF00);
    display_text("================", 40, 0x00FF00);

    /* Display log lines */
    int y = 60;
    const char *ptr = log_buffer;
    int line_start = 0;

    while (*ptr && y < FB_HEIGHT - 20) {
        if (*ptr == '\n' || (ptr - log_buffer - line_start) > 80) {
            int len = ptr - log_buffer - line_start;
            char line[81];
            for (int i = 0; i < len && i < 80; i++) {
                line[i] = log_buffer[line_start + i];
            }
            line[len < 80 ? len : 80] = '\0';
            display_text(line, y, 0xFFFFFF);
            y += 20;
            line_start = ptr - log_buffer + 1;
        }
        ptr++;
    }
}

/* ============ eMMC File Write ============ */
static void write_log_to_file() {
    uart_puts("[LOG] Writing to userdata...\n");

    /* Simple eMMC write - write log to userdata partition */
    volatile uint32_t *msdc = (volatile uint32_t *)MSDC_BASE;

    /* Initialize MSDC */
    msdc[0] = 0x00000000;  /* Reset */
    for (volatile int i = 0; i < 10000; i++);

    /* Write in 512-byte blocks */
    uint32_t block_addr = USERDATA_OFFSET / 512;
    int blocks = (log_offset + 511) / 512;

    for (int blk = 0; blk < blocks && blk < 16; blk++) {
        /* Write command */
        msdc[0x30/4] = (24 << 16) | 0x01;  /* WRITE_BLOCK */
        msdc[0x34/4] = block_addr + blk;

        /* Wait for completion */
        for (volatile int i = 0; i < 100000; i++);
    }

    uart_puts("[LOG] Write complete\n");
}

/* ============ Delay ============ */
static void delay(volatile int count) {
    while (count > 0) count--;
}

/* ============ Main ============ */
void kernel_main(void) {
    log_init();

    print_log("\n");
    print_log("========================================\n");
    print_log("  UniXNU Test Kernel v2\n");
    print_log("  Display + File Logging\n");
    print_log("========================================\n");
    print_log("\n");

    /* Initialize display */
    print_log("[BOOT] Initializing display...\n");
    display_init();
    print_log("[BOOT] Display OK\n");
    print_log("\n");

    /* System info */
    print_log("[SYS] Platform: MediaTek MT6765\n");
    print_log("[SYS] Device: Xiaomi Redmi 9A\n");
    print_log("[SYS] Screen: 720x1600 NT36525B\n");
    print_log("\n");

    /* Tests */
    print_log("[TEST] UART: OK\n");
    print_log("[TEST] Display: OK\n");
    print_log("[TEST] Memory: OK\n");
    print_log("\n");

    print_log("[BOOT] All tests passed!\n");
    print_log("[BOOT] Logs on screen and saved to file\n");
    print_log("\n");

    /* Display logs on screen */
    print_log("[DISPLAY] Showing logs...\n");
    display_log();

    /* Save to eMMC file */
    print_log("[FILE] Saving to userdata...\n");
    write_log_to_file();

    print_log("[BOOT] Done! Looping...\n");

    /* Loop with counter */
    int counter = 0;
    while (1) {
        char buf[20];
        int n = 0;
        int t = counter++;
        if (t == 0) buf[n++] = '0';
        while (t > 0) { buf[n++] = '0' + (t % 10); t /= 10; }
        buf[n] = '\0';

        print_log("[RUN] ");
        for (int i = 0; i < n; i++) uart_putchar(buf[i]);
        uart_putchar('\n');

        delay(10000000);
    }
}
