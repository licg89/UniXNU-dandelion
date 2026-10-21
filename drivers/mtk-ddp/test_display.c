/*
 * MTK DDP Display Driver Test
 */

#include "mtk_ddp.h"
#include <stdio.h>

/* Test framebuffer (32MB) */
#define FB_BASE     0x48000000
#define FB_SIZE     (32 * 1024 * 1024)

int main(void)
{
    mtk_ddp_t ddp;
    int ret;

    printf("=== MTK DDP Display Test ===\n\n");

    /* Initialize display controller */
    printf("Initializing display controller...\n");
    ret = mtk_ddp_init(&ddp, FB_BASE, FB_SIZE);
    if (ret < 0) {
        printf("FAILED: Display init error %d\n", ret);
        return ret;
    }
    printf("OK: Display initialized\n\n");

    /* Set video mode */
    printf("Setting video mode...\n");
    ret = mtk_ddp_set_mode(&ddp, DDP_VIDEO_MODE);
    if (ret < 0) {
        printf("FAILED: Set mode error %d\n", ret);
        return ret;
    }
    printf("OK: Video mode set\n\n");

    /* Enable display */
    printf("Enabling display...\n");
    ret = mtk_ddp_enable(&ddp);
    if (ret < 0) {
        printf("FAILED: Enable error %d\n", ret);
        return ret;
    }
    printf("OK: Display enabled\n\n");

    /* Test pattern 1: Fill screen with red */
    printf("Drawing red screen...\n");
    ret = mtk_ddp_fill_rect(&ddp, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x00FF0000);
    if (ret < 0) {
        printf("FAILED: Fill rect error\n");
        return ret;
    }
    printf("OK: Red screen drawn\n\n");

    /* Wait for display */
    volatile int delay;
    for (delay = 0; delay < 10000000; delay++);

    /* Test pattern 2: Fill with green */
    printf("Drawing green screen...\n");
    ret = mtk_ddp_fill_rect(&ddp, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x0000FF00);
    if (ret < 0) {
        printf("FAILED: Fill rect error\n");
        return ret;
    }
    printf("OK: Green screen drawn\n\n");

    for (delay = 0; delay < 10000000; delay++);

    /* Test pattern 3: Fill with blue */
    printf("Drawing blue screen...\n");
    ret = mtk_ddp_fill_rect(&ddp, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x000000FF);
    if (ret < 0) {
        printf("FAILED: Fill rect error\n");
        return ret;
    }
    printf("OK: Blue screen drawn\n\n");

    for (delay = 0; delay < 10000000; delay++);

    /* Test pattern 4: Draw colored rectangles */
    printf("Drawing color bars...\n");
    uint32_t bar_width = DISPLAY_WIDTH / 4;
    uint32_t colors[] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF};

    for (int i = 0; i < 4; i++) {
        ret = mtk_ddp_fill_rect(&ddp, i * bar_width, 0, bar_width, DISPLAY_HEIGHT, colors[i]);
        if (ret < 0) {
            printf("FAILED: Fill rect error\n");
            return ret;
        }
    }
    printf("OK: Color bars drawn\n\n");

    for (delay = 0; delay < 10000000; delay++);

    /* Test pattern 5: Draw gradient */
    printf("Drawing gradient...\n");
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        uint8_t r = (y * 255) / DISPLAY_HEIGHT;
        uint32_t color = (r << 16) | (r << 8) | r;
        mtk_ddp_fill_rect(&ddp, 0, y, DISPLAY_WIDTH, 1, color);
    }
    printf("OK: Gradient drawn\n\n");

    for (delay = 0; delay < 10000000; delay++);

    /* Disable display */
    printf("Disabling display...\n");
    ret = mtk_ddp_disable(&ddp);
    if (ret < 0) {
        printf("FAILED: Disable error %d\n", ret);
        return ret;
    }
    printf("OK: Display disabled\n\n");

    printf("=== Display Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
