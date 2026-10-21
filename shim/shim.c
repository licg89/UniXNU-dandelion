/*
 * Milestone 1: dandelion boot shim - basic framebuffer test
 *
 * This is a minimal shim that:
 * - Sets up framebuffer console
 * - Shows boot information
 * - Tests basic hardware
 * - Then reboots
 *
 * Based on ios-sweet by dipa-studio, adapted for MT6765/Redmi 9A
 */
#include <stdint.h>
#include "console.h"

/* MT6765 specific addresses */
#define MTK_WDT_BASE		0x10007000UL	/* Watchdog timer */
#define WDT_MODE		0x00
#define WDT_MODE_KEY		0x22000000

/* Simple delay */
static void delay_ms(unsigned ms)
{
	volatile uint64_t count = ms * 10000ULL;
	while (count--)
		;
}

/* Reset via watchdog */
static void __attribute__((noreturn)) reset(void)
{
	con_str("RESET", "rebooting...");
	delay_ms(2000);

	/* Trigger watchdog reset */
	*(volatile uint32_t *)(MTK_WDT_BASE + WDT_MODE) = WDT_MODE_KEY;
	*(volatile uint32_t *)(MTK_WDT_BASE + 0x14) = 0x1209;	/* WDT_SWRST */
	*(volatile uint32_t *)(MTK_WDT_BASE + 0x14) = 0x1209;
	*(volatile uint32_t *)(MTK_WDT_BASE + 0x14) = 0x1209;

	for (;;)
		__asm__ volatile("wfe");
}

/* Exception handler */
void exc_handler(uint64_t slot, uint64_t *regs)
{
	con_clear(BLUE);
	con_str("PANIC", "exception");
	con_hex("SLOT", slot);
	con_hex("ESR", ({ uint64_t _v; __asm__ volatile("mrs %0, esr_el1" : "=r"(_v)); _v; }));
	con_hex("ELR", ({ uint64_t _v; __asm__ volatile("mrs %0, elr_el1" : "=r"(_v)); _v; }));
	con_hex("FAR", ({ uint64_t _v; __asm__ volatile("mrs %0, far_el1" : "=r"(_v)); _v; }));

	delay_ms(5000);
	reset();
}

/* Secondary CPU entry (stub) */
void secondary_main(uint64_t idx)
{
	for (;;)
		__asm__ volatile("wfe");
}

/* Main entry point */
void shim_main(uint64_t dtb)
{
	/* Clear screen and show boot message */
	con_clear(BLACK);

	con_str("DANDELION", "boot shim v0.1");
	con_str("DEVICE", "Xiaomi Redmi 9A");
	con_str("SOC", "MediaTek MT6765");
	con_hex("DTB", dtb);

	/* Read CPU info */
	uint64_t mpidr;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
	con_hex("MPIDR", mpidr);

	/* Read CPU features */
	uint64_t id_aa64mmfr0;
	__asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(id_aa64mmfr0));
	con_hex("MMFR0", id_aa64mmfr0);

	/* Check for 16K page support */
	uint64_t tgran16 = (id_aa64mmfr0 >> 20) & 0xf;
	if (tgran16 == 1) {
		con_str("PAGES", "16K supported");
	} else {
		con_str("PAGES", "16K NOT supported");
	}

	/* Show framebuffer info */
	con_hex("FB BASE", FB_BASE);
	con_hex("FB SIZE", FB_W * FB_H * 4);

	/* Test complete */
	con_str("TEST", "complete!");
	con_str("NEXT", "rebooting in 5s...");

	delay_ms(5000);
	reset();
}
