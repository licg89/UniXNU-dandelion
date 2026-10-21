/*
 * MTK DDP Display Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * MediaTek Display Data Path driver for MT6765
 * Supports NT36525B MIPI DSI panel (720x1600)
 */

#ifndef _MTK_DDP_H_
#define _MTK_DDP_H_

#include <stdint.h>
#include <stdbool.h>

/* Display configuration */
#define DISPLAY_WIDTH           720
#define DISPLAY_HEIGHT          1600
#define DISPLAY_BPP             32      /* bits per pixel */
#define DISPLAY_REFRESH         60      /* Hz */

/* Base addresses (typical MTK layout, needs verification) */
#define MTK_MMSYS_BASE          0x14000000  /* Multimedia system */
#define MTK_DISP_OVL_BASE       0x14008000  /* Overlay */
#define MTK_DISP_RDMA_BASE      0x1400A000  /* Read DMA */
#define MTK_DISP_WDMA_BASE      0x1400B000  /* Write DMA */
#define MTK_DISP_COLOR_BASE     0x1400C000  /* Color */
#define MTK_DISP_AAL_BASE       0x1400D000  /* Adaptive ambient light */
#define MTK_DISP_GAMMA_BASE     0x1400E000  /* Gamma */
#define MTK_DISP_DITHER_BASE    0x1400F000  /* Dither */
#define MTK_DSI_BASE            0x14014000  /* DSI controller */
#define MTK_DPI_BASE            0x14015000  /* DPI controller */
#define MTK_MIPI_TX_BASE        0x10012000  /* MIPI TX PHY */

/* DSI registers */
#define DSI_INTEN               0x0000  /* Interrupt enable */
#define DSI_INTSTA              0x0004  /* Interrupt status */
#define DSI_CON_CTRL            0x0010  /* Control */
#define DSI_MODE_CTRL           0x0014  /* Mode control */
#define DSI_TXQ_CTRL            0x0018  /* TX queue control */
#define DSI_PS_CTRL             0x001C  /* Packet size control */
#define DSI_PS_DATA             0x0020  /* Packet size data */
#define DSI_HSA_WC              0x0028  /* HSA word count */
#define DSI_HBP_WC              0x002C  /* HBP word count */
#define DSI_HFP_WC              0x0030  /* HFP word count */
#define DSI_BLLP_WC             0x0034  /* BLLP word count */
#define DSI_TIMING0             0x0038  /* Timing 0 */
#define DSI_TIMING1             0x003C  /* Timing 1 */
#define DSI_VM_CTRL             0x0040  /* Video mode control */
#define DSI_FB_WIDTH            0x0044  /* Frame buffer width */
#define DSI_FB_HEIGHT           0x0048  /* Frame buffer height */

/* RDMA registers */
#define RDMA_GLOBAL_INT         0x0000  /* Global interrupt */
#define RDMA_RDMA_INT_ENABLE    0x0004  /* Interrupt enable */
#define RDMA_RDMA_INT_STATUS    0x0008  /* Interrupt status */
#define RDMA_RDMA_CON           0x0020  /* Control */
#define RDMA_RDMA_SRC_BASE      0x00F0  /* Source base address */
#define RDMA_RDMA_SRC_W_IN_BYTE 0x00F4  /* Source width */
#define RDMA_RDMA_PITCH         0x00F8  /* Pitch */
#define RDMA_RDMA_SRC_CON       0x00FC  /* Source control */
#define RDMA_RDMA_SRC_HEIGHT    0x0100  /* Source height */
#define RDMA_RDMA_ADDR          0x0104  /* Current address */

/* MIPI TX PHY registers */
#define MIPITX_LANE_CON         0x0000  /* Lane control */
#define MIPITX_VMNL_CON         0x0004  /* Virtual channel control */
#define MIPITX_PLL_CON          0x0010  /* PLL control */
#define MIPITX_PLL_DIV          0x0014  /* PLL divider */

/* Display modes */
typedef enum {
    DDP_CMD_MODE = 0,     /* Command mode */
    DDP_VIDEO_MODE = 1,   /* Video mode */
} ddp_mode_t;

/* Display format */
typedef enum {
    DDP_RGB565 = 0,
    DDP_RGB666 = 1,
    DDP_RGB888 = 2,
} ddp_format_t;

/* Framebuffer info */
typedef struct {
    uint32_t base;        /* Physical base address */
    uint32_t size;        /* Framebuffer size in bytes */
    uint32_t width;       /* Width in pixels */
    uint32_t height;      /* Height in pixels */
    uint32_t pitch;       /* Bytes per line */
    ddp_format_t format;  /* Pixel format */
} ddp_fb_t;

/* Display controller context */
typedef struct {
    volatile uint32_t *mmsys_base;
    volatile uint32_t *rdma_base;
    volatile uint32_t *dsi_base;
    volatile uint32_t *mipitx_base;
    ddp_mode_t mode;
    ddp_fb_t fb;
    uint32_t width;
    uint32_t height;
    uint32_t refresh;
    bool initialized;
} mtk_ddp_t;

/* Function prototypes */
int mtk_ddp_init(mtk_ddp_t *ddp, uint32_t fb_base, uint32_t fb_size);
int mtk_ddp_set_mode(mtk_ddp_t *ddp, ddp_mode_t mode);
int mtk_ddp_set_fb(mtk_ddp_t *ddp, uint32_t base, uint32_t width, uint32_t height, ddp_format_t fmt);
int mtk_ddp_enable(mtk_ddp_t *ddp);
int mtk_ddp_disable(mtk_ddp_t *ddp);
int mtk_ddp_fill_rect(mtk_ddp_t *ddp, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* NT36525B panel initialization */
int nt36525b_init(mtk_ddp_t *ddp);

#endif /* _MTK_DDP_H_ */
