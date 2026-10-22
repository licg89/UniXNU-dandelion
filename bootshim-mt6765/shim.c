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

/* Framebuffer for display output (no UART debugging) */
/* DISABLED for initial testing - framebuffer address needs verification */
#define FB_ENABLED 0
#if FB_ENABLED
#define FB_BASE ((uintptr_t)0x48000000)  /* Needs verification from LK */
#define FB_WIDTH 720
#define FB_HEIGHT 1600
#define FB_STRIDE (720 * 4)
#define FB_SIZE (FB_STRIDE * FB_HEIGHT)

static int fb_y = 20;  /* Current text Y position */
#endif

/* Draw a single character to framebuffer */
#if FB_ENABLED
static void fb_putchar(char c, int x, int y, uint32_t color)
{
    if (x < 0 || x >= FB_WIDTH - 8 || y < 0 || y >= FB_HEIGHT - 16) return;

    volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);

    /* Simple 8x16 block rendering - just fill rectangles for visibility */
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            /* Simple pattern: filled for printable chars */
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
#else
static void fb_putchar(char c, int x, int y, uint32_t color) { (void)c; (void)x; (void)y; (void)color; }
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
            /* Screen full - scroll by clearing */
            fb_y = 20;
            /* Clear screen */
            volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);
            for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
                fb[i] = 0x00000000;  /* Black */
            }
        }
        /* Draw character - white text on black background */
        fb_putchar(*s, x, fb_y, 0x00FFFFFF);
        x += 10;
        s++;
    }
}
#else
static void fb_puts(const char *s) { (void)s; }
#endif

/* Display diagnostic header on screen */
#if FB_ENABLED
static void fb_init(void)
{
    /* Clear screen to black */
    volatile uint32_t *fb = (volatile uint32_t *)(FB_BASE);
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        fb[i] = 0x00000000;
    }

    /* Show header */
    fb_puts("=== MT6765 BootShim Diagnostic ===\n");
    fb_puts("Device: Xiaomi Redmi 9A (dandelion)\n");
    fb_puts("SoC: MediaTek MT6765 (Helio G25)\n");
    fb_puts("CPU: Cortex-A53 x8\n");
    fb_puts("==================================\n\n");
    fb_y += 10;
}
#else
static void fb_init(void) { /* Framebuffer disabled */ }
#endif

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
    /* Initialize framebuffer display first (for no-UART debugging) */
    fb_init();
    fb_puts("BootShim starting...\n");

    uint64_t el = READ_SYSREG(CurrentEL);
    puts_uart("\nXNU_DANDELION_SHIM_DIAGNOSTIC_V1\n");

    fb_puts("UART diagnostic output:\n");
    value("CurrentEL", el);
    value("entry_x0", x0);
    value("entry_x1", x1);
    value("entry_x2", x2);
    value("entry_x3", x3);
    value("entry_sp", old_sp);
    value("MIDR_EL1", READ_SYSREG(MIDR_EL1));
    value("MPIDR_EL1", READ_SYSREG(MPIDR_EL1));
    value("ID_AA64ISAR0_EL1", READ_SYSREG(ID_AA64ISAR0_EL1));
    value("ID_AA64ISAR1_EL1", READ_SYSREG(ID_AA64ISAR1_EL1));
    value("ID_AA64PFR0_EL1", READ_SYSREG(ID_AA64PFR0_EL1));
    value("ID_AA64MMFR0_EL1", READ_SYSREG(ID_AA64MMFR0_EL1));
    value("CNTFRQ_EL0", READ_SYSREG(CNTFRQ_EL0));
    value("CNTVCT_EL0", READ_SYSREG(CNTVCT_EL0));
    if (el == 8) value("SCTLR_EL2", READ_SYSREG(SCTLR_EL2));
    else value("SCTLR_EL1", READ_SYSREG(SCTLR_EL1));
    puts_uart("SHIM_DIAGNOSTIC_DONE\n");
    fb_puts("\n=== Diagnostic Complete ===\n");
    fb_puts("Check UART for full output\n");
    fb_puts("Waiting... (watchdog may reset)\n");
    if (el == 8) {
        /* Do not discard live mappings or dirty cache data inherited from LK. */
        if (READ_SYSREG(SCTLR_EL2) & ((1u << 0) | (1u << 2) | (1u << 12))) {
            puts_uart("EL1_TRANSITION_REFUSED_MMU_OR_CACHE_ON\n");
            stop();
        }
        value("HCR_EL2_before", READ_SYSREG(HCR_EL2));
        value("CNTHCTL_EL2_before", READ_SYSREG(CNTHCTL_EL2));
        puts_uart("ENTER_EL1\n");
        enter_el1(x0);
    }
    if (el == 4) {
        extern void shim_el1(uint64_t dtb);
        shim_el1(x0);
    }
    /* Leave the inherited watchdog untouched. A later reset is possible. */
    stop();
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
