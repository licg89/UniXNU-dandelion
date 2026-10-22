/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 * Diagnostic stage only: no kernel launch, storage access or power changes.
 */
#include <stdint.h>
#include "fdt.h"
#include "handoff.h"

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
    (void)old_sp;  /* Unused parameter */
    uint64_t el = READ_SYSREG(CurrentEL);
    puts_uart("\n=== MT6765 BootShim with FDT ===\n");
    value("CurrentEL", el);
    value("entry_x0", x0);  /* Usually FDT address */
    value("entry_x1", x1);
    value("entry_x2", x2);
    value("entry_x3", x3);

    /* Parse FDT from x0 */
    puts_uart("\n--- FDT Parsing ---\n");
    struct fdt_info fdt_info;
    if (x0 >= 0x40000000 && x0 < 0x80000000) {
        puts_uart("Trying to parse FDT from x0...\n");
        int ret = fdt_inspect((const void *)(uintptr_t)x0, 0x100000, &fdt_info);
        if (ret == 0) {
            puts_uart("FDT parsed successfully!\n");
            value("FDT_bytes", fdt_info.bytes);
            value("memory_count", fdt_info.memory_count);
            for (unsigned i = 0; i < fdt_info.memory_count && i < 4; i++) {
                value("memory_base", fdt_info.memory[i].base);
                value("memory_size", fdt_info.memory[i].size);
            }
            value("reserved_count", fdt_info.reserved_count);
            for (unsigned i = 0; i < fdt_info.reserved_count && i < 8; i++) {
                value("reserved_base", fdt_info.reserved[i].base);
                value("reserved_size", fdt_info.reserved[i].size);
            }
        } else {
            puts_uart("FDT parse failed\n");
        }
    } else {
        puts_uart("x0 not in valid range for FDT\n");
    }

    puts_uart("\n=== Diagnostic Complete ===\n");
    puts_uart("Looping... (watchdog will reset)\n");

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
