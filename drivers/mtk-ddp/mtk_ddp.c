/*
 * MTK DDP Display Driver Implementation
 */

#include "mtk_ddp.h"
#include <stdio.h>
#include <string.h>

/* Delay macros */
#define udelay(x) do { \
    volatile int __i; \
    for (__i = 0; __i < (x) * 10; __i++) \
        __asm__ volatile("" ::: "memory"); \
} while (0)

#define msleep(x) udelay((x) * 1000)

/*
 * Write DSI command
 */
static void dsi_write(mtk_ddp_t *ddp, uint32_t reg, uint32_t val)
{
    ddp->dsi_base[reg >> 2] = val;
}

static uint32_t dsi_read(mtk_ddp_t *ddp, uint32_t reg)
{
    return ddp->dsi_base[reg >> 2];
}

/*
 * Initialize display controller
 */
int mtk_ddp_init(mtk_ddp_t *ddp, uint32_t fb_base, uint32_t fb_size)
{
    if (!ddp) {
        return -1;
    }

    ddp->mmsys_base = (volatile uint32_t *)MTK_MMSYS_BASE;
    ddp->rdma_base = (volatile uint32_t *)MTK_DISP_RDMA_BASE;
    ddp->dsi_base = (volatile uint32_t *)MTK_DSI_BASE;
    ddp->mipitx_base = (volatile uint32_t *)MTK_MIPI_TX_BASE;

    ddp->width = DISPLAY_WIDTH;
    ddp->height = DISPLAY_HEIGHT;
    ddp->refresh = DISPLAY_REFRESH;
    ddp->mode = DDP_VIDEO_MODE;
    ddp->initialized = false;

    /* Setup framebuffer */
    ddp->fb.base = fb_base;
    ddp->fb.size = fb_size;
    ddp->fb.width = DISPLAY_WIDTH;
    ddp->fb.height = DISPLAY_HEIGHT;
    ddp->fb.pitch = DISPLAY_WIDTH * (DISPLAY_BPP / 8);
    ddp->fb.format = DDP_RGB888;

    printf("MTK DDP: Initializing display %ux%u @ %uHz\n",
           ddp->width, ddp->height, ddp->refresh);
    printf("MTK DDP: Framebuffer at 0x%08X, size %u bytes\n",
           fb_base, fb_size);

    /* Clear framebuffer */
    printf("MTK DDP: Clearing framebuffer...\n");
    memset((void *)fb_base, 0, fb_size);

    /* Initialize MIPI DSI */
    printf("MTK DDP: Initializing MIPI DSI...\n");
    int ret = nt36525b_init(ddp);
    if (ret < 0) {
        printf("MTK DDP: Failed to initialize panel\n");
        return ret;
    }

    ddp->initialized = true;
    printf("MTK DDP: Display initialized\n");

    return 0;
}

/*
 * Set display mode
 */
int mtk_ddp_set_mode(mtk_ddp_t *ddp, ddp_mode_t mode)
{
    if (!ddp || !ddp->initialized) {
        return -1;
    }

    ddp->mode = mode;

    /* Configure DSI mode */
    if (mode == DDP_VIDEO_MODE) {
        /* Video mode timing */
        dsi_write(ddp, DSI_MODE_CTRL, 0x01);  /* Video mode */

        /* Horizontal timing (pixels) */
        dsi_write(ddp, DSI_HSA_WC, 10);    /* HSA */
        dsi_write(ddp, DSI_HBP_WC, 20);    /* HBP */
        dsi_write(ddp, DSI_HFP_WC, 20);    /* HFP */

        /* Vertical timing (lines) */
        dsi_write(ddp, DSI_TIMING0, (2 << 16) | 2);   /* VSA */
        dsi_write(ddp, DSI_TIMING1, (10 << 16) | 10); /* VBP, VFP */

        /* Frame dimensions */
        dsi_write(ddp, DSI_FB_WIDTH, ddp->width);
        dsi_write(ddp, DSI_FB_HEIGHT, ddp->height);

        printf("MTK DDP: Video mode configured\n");
    } else {
        /* Command mode */
        dsi_write(ddp, DSI_MODE_CTRL, 0x00);  /* Command mode */
        printf("MTK DDP: Command mode configured\n");
    }

    return 0;
}

/*
 * Set framebuffer
 */
int mtk_ddp_set_fb(mtk_ddp_t *ddp, uint32_t base, uint32_t width, uint32_t height, ddp_format_t fmt)
{
    if (!ddp || !ddp->initialized) {
        return -1;
    }

    ddp->fb.base = base;
    ddp->fb.width = width;
    ddp->fb.height = height;
    ddp->fb.format = fmt;

    /* Calculate pitch based on format */
    uint32_t bpp;
    switch (fmt) {
        case DDP_RGB565: bpp = 16; break;
        case DDP_RGB666: bpp = 24; break;
        case DDP_RGB888: bpp = 24; break;
        default: bpp = 32; break;
    }
    ddp->fb.pitch = width * (bpp / 8);

    /* Configure RDMA */
    ddp->rdma_base[RDMA_RDMA_SRC_BASE >> 2] = base;
    ddp->rdma_base[RDMA_RDMA_SRC_W_IN_BYTE >> 2] = ddp->fb.pitch;
    ddp->rdma_base[RDMA_RDMA_PITCH >> 2] = ddp->fb.pitch;
    ddp->rdma_base[RDMA_RDMA_SRC_HEIGHT >> 2] = height;

    printf("MTK DDP: Framebuffer configured: %ux%u, pitch=%u\n",
           width, height, ddp->fb.pitch);

    return 0;
}

/*
 * Enable display
 */
int mtk_ddp_enable(mtk_ddp_t *ddp)
{
    if (!ddp || !ddp->initialized) {
        return -1;
    }

    /* Enable RDMA */
    ddp->rdma_base[RDMA_RDMA_CON >> 2] = 0x01;  /* Enable */

    /* Enable DSI */
    dsi_write(ddp, DSI_CON_CTRL, 0x01);  /* Enable */

    /* Enable MIPI TX */
    ddp->mipitx_base[MIPITX_LANE_CON >> 2] = 0x0F;  /* Enable all lanes */

    printf("MTK DDP: Display enabled\n");

    return 0;
}

/*
 * Disable display
 */
int mtk_ddp_disable(mtk_ddp_t *ddp)
{
    if (!ddp || !ddp->initialized) {
        return -1;
    }

    /* Disable DSI */
    dsi_write(ddp, DSI_CON_CTRL, 0x00);

    /* Disable RDMA */
    ddp->rdma_base[RDMA_RDMA_CON >> 2] = 0x00;

    /* Disable MIPI TX */
    ddp->mipitx_base[MIPITX_LANE_CON >> 2] = 0x00;

    printf("MTK DDP: Display disabled\n");

    return 0;
}

/*
 * Fill rectangle with color
 */
int mtk_ddp_fill_rect(mtk_ddp_t *ddp, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!ddp || !ddp->initialized) {
        return -1;
    }

    /* Bounds check */
    if (x >= ddp->fb.width || y >= ddp->fb.height) {
        return -1;
    }

    /* Clamp dimensions */
    if (x + w > ddp->fb.width) w = ddp->fb.width - x;
    if (y + h > ddp->fb.height) h = ddp->fb.height - y;

    /* Fill rectangle */
    uint32_t *fb = (uint32_t *)ddp->fb.base;
    uint32_t pitch_pixels = ddp->fb.pitch / 4;

    for (uint32_t row = y; row < y + h; row++) {
        for (uint32_t col = x; col < x + w; col++) {
            fb[row * pitch_pixels + col] = color;
        }
    }

    return 0;
}

/*
 * NT36525B panel initialization
 *
 * This is a simplified init sequence. Real sequence needs to be extracted
 * from the device's LK or kernel driver.
 */
int nt36525b_init(mtk_ddp_t *ddp)
{
    printf("NT36525B: Initializing panel...\n");

    /* Reset sequence */
    printf("NT36525B: Resetting panel...\n");
    /* TODO: Implement reset via GPIO */
    msleep(20);

    /* Configure MIPI DSI */
    printf("NT36525B: Configuring MIPI DSI...\n");

    /* Set DSI clock and timing */
    dsi_write(ddp, DSI_TXQ_CTRL, 0x01);
    dsi_write(ddp, DSI_PS_CTRL, 0x02);  /* Pixel format: RGB888 */
    dsi_write(ddp, DSI_PS_DATA, ddp->width);

    /* Configure MIPI TX PHY */
    ddp->mipitx_base[MIPITX_PLL_CON >> 2] = 0x01;  /* Enable PLL */
    ddp->mipitx_base[MIPITX_PLL_DIV >> 2] = 0x10;  /* PLL divider */

    /* Wait for PLL lock */
    msleep(10);

    /* Send init commands to panel */
    printf("NT36525B: Sending init commands...\n");

    /* Sleep out */
    dsi_write(ddp, DSI_TXQ_CTRL, 0x11);
    msleep(120);

    /* Display on */
    dsi_write(ddp, DSI_TXQ_CTRL, 0x29);
    msleep(20);

    printf("NT36525B: Panel initialized\n");

    return 0;
}
