/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 * Diagnostic stage only: no kernel launch, storage access or power changes.
 */
#include <stdint.h>
#include "fdt.h"
#include "handoff.h"

/* Freestanding memcpy implementation */
void *memcpy(void *dest, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

__attribute__((noreturn)) void enter_el1(uint64_t dtb);
static struct fdt_info observed_fdt;

#define READ_SYSREG(reg) ({ uint64_t v; __asm__ volatile("mrs %0, " #reg : "=r"(v)); v; })
#define UART_BASE ((uintptr_t)0x11002000)  /* MT6765 UART0 */
#define UART_LSR (UART_BASE + 0x14)
#define UART_TX_READY (1u << 5)

/* Framebuffer - DISABLED until we find correct address */
#define FB_ENABLED 0
#if FB_ENABLED
#define FB_BASE ((uintptr_t)0x80000000)
#define FB_WIDTH 720
#define FB_HEIGHT 1600
#define FB_STRIDE (720 * 4)
#define FB_SIZE (FB_STRIDE * FB_HEIGHT)

static int fb_y = 20;
#endif

/* Draw a single character to framebuffer */
#if FB_ENABLED
static void fb_putchar(char c, int x, int y, uint32_t color)
{
    if (x < 0 || x >= FB_WIDTH - 8 || y < 0 || y >= FB_HEIGHT - 16) return;

    volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);

    /* Simple 8x16 block rendering */
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            if (c >= ' ' && c <= '~') {
                int px = x + col;
                int py = y + row;
                if (px < FB_WIDTH && py < FB_HEIGHT) {
                    fb[py * FB_WIDTH + px] = color;
                }
            }
        }
    }
}
#endif

/* Draw a string to framebuffer */
#if FB_ENABLED
static void fb_puts(const char *s)
{
    int x = 20;
    while (*s) {
        if (*s == '\n' || x >= FB_WIDTH - 20) {
            fb_y += 20;
            x = 20;
            if (*s == '\n') {
                s++;
                continue;
            }
        }
        if (fb_y >= FB_HEIGHT - 20) {
            fb_y = 20;
            volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);
            for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
                fb[i] = 0x00000000;
            }
        }
        fb_putchar(*s, x, fb_y, 0x00FFFFFF);
        x += 10;
        s++;
    }
}
#endif

/* Display diagnostic header on screen */
#if FB_ENABLED
static int fb_check_valid(void)
{
    /* Try to read from framebuffer address - if it crashes, we'll know */
    volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);
    volatile uint32_t test = fb[0];  /* This might crash if address is invalid */
    (void)test;
    return 1;  /* If we get here, address is valid */
}

static void fb_init(void)
{
    /* Check if framebuffer address is accessible */
    if (!fb_check_valid()) {
        return;  /* Framebuffer not accessible, skip */
    }

    volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        fb[i] = 0x00000000;
    }

    fb_puts("=== MT6765 BootShim v3 ===");
    fb_puts("FB: 0x80000000");
    fb_puts("Device: Redmi 9A");
    fb_puts("Testing framebuffer...");
    fb_y += 10;
}
#endif

/* UART output - ENABLED for FDT debugging */
#define UART_ENABLED 1

#if UART_ENABLED
static void putc_uart(char c)
{
    /* Preserve the UART setup inherited from LK, including its baud rate. */
    for (unsigned int n = 0; n < 1000000; n++) {
        if (*(volatile uint32_t *)UART_LSR & UART_TX_READY) {
            *(volatile uint32_t *)UART_BASE = (uint8_t)c;
            return;
        }
    }
}

static void puts_uart(const char *s)
{
    while (*s) {
        if (*s == '\n') putc_uart('\r');
        putc_uart(*s++);
    }
}
#else
static void putc_uart(char c) { (void)c; }
static void puts_uart(const char *s) { (void)s; }
#endif

static void value(const char *label, uint64_t v)
{
    /* Output to UART */
    puts_uart(label);
    puts_uart("=0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned int digit = (unsigned int)(v >> shift) & 15;
        putc_uart((char)(digit < 10 ? '0' + digit : 'a' + digit - 10));
    }
    puts_uart("\n");

#if FB_ENABLED
    /* Output to framebuffer */
    char buf[64];
    int pos = 0;
    for (int i = 0; label[i] && pos < 40; i++) {
        buf[pos++] = label[i];
    }
    buf[pos++] = '=';
    buf[pos++] = '0';
    buf[pos++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned int digit = (unsigned int)(v >> shift) & 15;
        buf[pos++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
    buf[pos++] = '\n';
    buf[pos] = '\0';
    fb_puts(buf);
#endif
}

void handoff_trace(const char *label, uint64_t v) { value(label, v); }
void handoff_message(const char *message) { puts_uart(message); }

__attribute__((noreturn)) static void stop(void)
{
    for (;;) __asm__ volatile("wfe");
}

__attribute__((noreturn)) void exception_report(uint64_t slot)
{
    uint64_t el = READ_SYSREG(CurrentEL);
    puts_uart("XNU_SHIM_EXCEPTION\n");
    value("vector", slot);
    value("CurrentEL", el);
    if (el == 8) {
        value("ESR_EL2", READ_SYSREG(ESR_EL2));
        value("ELR_EL2", READ_SYSREG(ELR_EL2));
        value("FAR_EL2", READ_SYSREG(FAR_EL2));
    } else {
        value("ESR_EL1", READ_SYSREG(ESR_EL1));
        value("ELR_EL1", READ_SYSREG(ELR_EL1));
        value("FAR_EL1", READ_SYSREG(FAR_EL1));
    }
    stop();
}

__attribute__((noreturn)) void shim_main(uint64_t x0, uint64_t x1,
                                        uint64_t x2, uint64_t x3, uint64_t old_sp)
{
    (void)old_sp;
    (void)x0; (void)x1; (void)x2; (void)x3;

    puts_uart("\n=== MT6765 Memory Scanner ===\n");
    puts_uart("Testing memory addresses (read-only)...\n\n");

    /* Test different memory addresses - READ ONLY */
    /* If address is invalid, will crash and reboot */
    uint64_t test_addrs[] = {
        0x40000000,  /* DRAM start */
        0x41000000,
        0x42000000,
        0x43000000,
        0x44000000,
        0x45000000,
        0x46000000,
        0x47000000,
        0x48000000,
        0x49000000,
        0x4A000000,
        0x4B000000,
        0x4C000000,
        0x4D000000,
        0x4E000000,
        0x4F000000,
        0x50000000,
        0x60000000,
        0x70000000,
        0x80000000,
    };

    for (int i = 0; i < 20; i++) {
        uint64_t addr = test_addrs[i];
        volatile uint32_t *ptr = (volatile uint32_t *)addr;

        puts_uart("Testing 0x");
        for (int shift = 60; shift >= 0; shift -= 4) {
            unsigned int digit = (unsigned int)(addr >> shift) & 15;
            putc_uart((char)(digit < 10 ? '0' + digit : 'a' + digit - 10));
        }
        puts_uart("... ");

        /* Try to read - if crashes, address is invalid */
        volatile uint32_t val = *ptr;

        puts_uart("OK value=0x");
        for (int shift = 28; shift >= 0; shift -= 4) {
            unsigned int digit = (unsigned int)(val >> shift) & 15;
            putc_uart((char)(digit < 10 ? '0' + digit : 'a' + digit - 10));
        }
        puts_uart("\n");

        /* Small delay */
        for (volatile int k = 0; k < 1000000; k++);
    }

    puts_uart("\n=== All addresses readable ===\n");
    puts_uart("Now entering infinite loop...\n");

    /* Loop forever */
    for (;;) __asm__ volatile("wfe");
}

__attribute__((noreturn)) void shim_el1(uint64_t dtb)
{
    puts_uart("XNU_DANDELION_EL1_DIAGNOSTIC_V2\n");
    value("CurrentEL", READ_SYSREG(CurrentEL));
    if (READ_SYSREG(CurrentEL) != 4) {
        puts_uart("EL1_DIAGNOSTIC_WRONG_EXCEPTION_LEVEL\n");
        stop();
    }
    value("SCTLR_EL1", READ_SYSREG(SCTLR_EL1));
    value("CNTFRQ_EL0", READ_SYSREG(CNTFRQ_EL0));
    value("CNTVCT_EL0", READ_SYSREG(CNTVCT_EL0));
    value("DTB", dtb);
    /* Restrict all speculative input parsing to the known low DRAM window. */
    if (dtb < 0x40000000 || dtb > 0x7ff00000 || (dtb & 7) ||
        fdt_inspect((const void *)(uintptr_t)dtb, 0x100000, &observed_fdt)) {
        puts_uart("FDT_INVALID\n");
        stop();
    }
    value("FDT_bytes", observed_fdt.bytes);
    value("FDT_memory_count", observed_fdt.memory_count);
    for (unsigned i = 0; i < observed_fdt.memory_count; i++) {
        value("FDT_memory_base", observed_fdt.memory[i].base);
        value("FDT_memory_size", observed_fdt.memory[i].size);
    }
    value("FDT_reserved_count", observed_fdt.reserved_count);
    for (unsigned i = 0; i < observed_fdt.reserved_count; i++) {
        value("FDT_reserved_base", observed_fdt.reserved[i].base);
        value("FDT_reserved_size", observed_fdt.reserved[i].size);
    }
    value("FDT_initrd_start", observed_fdt.initrd_start);
    value("FDT_initrd_end", observed_fdt.initrd_end);
    puts_uart("SHIM_EL1_DIAGNOSTIC_DONE\n");
    const char *result = handoff_boot(&observed_fdt, dtb);
    puts_uart(result);
    puts_uart("\n");
    stop();
}
