/*
 * Console definitions for dandelion (Redmi 9A)
 * Framebuffer parameters for 720x1600 display
 */
#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <stdint.h>

/* Framebuffer parameters for Redmi 9A (720x1600) */
/* Note: These may need adjustment based on actual LK setup */
#define FB_BASE		0x48000000UL	/* Framebuffer base (from LK) */
#define FB_W		720
#define FB_H		1600
#define FB_STRIDE	(FB_W * 4)	/* 4 bytes per pixel (ARGB) */

/* Colors (ARGB) */
#define WHITE		0x00FFFFFF
#define YELLOW		0x00FFFF00
#define BLUE		0x000000FF
#define RED		0x00FF0000
#define GREEN		0x0000FF00
#define BLACK		0x00000000

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);

#endif /* _CONSOLE_H_ */
