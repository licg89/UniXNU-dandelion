/*
 * MTK MSDC eMMC Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * MediaTek Mobile Storage Device Controller driver for MT6765
 * Supports eMMC storage
 */

#ifndef _MTK_MSDC_H_
#define _MTK_MSDC_H_

#include <stdint.h>
#include <stdbool.h>

/* MSDC controller base addresses */
#define MTK_MSDC0_BASE      0x11230000  /* eMMC */
#define MTK_MSDC1_BASE      0x11240000  /* SD card */
#define MTK_MSDC2_BASE      0x11250000  /* SDIO */
#define MTK_MSDC3_BASE      0x11260000  /* SDIO2 */

/* MSDC registers */
#define MSDC_CFG              0x00  /* Configuration */
#define MSDC_STS              0x04  /* Status */
#define MSDC_INT              0x08  /* Interrupt */
#define MSDC_INTEN            0x0C  /* Interrupt enable */
#define MSDC_FIFO_CFG         0x10  /* FIFO configuration */
#define MSDC_FIFO_DATA        0x14  /* FIFO data */
#define MSDC_CMD              0x18  /* Command */
#define MSDC_ARG              0x1C  /* Argument */
#define MSDC_RSP              0x20  /* Response */
#define MSDC_BLK_NUM          0x24  /* Block number */
#define MSDC_IO_CON           0x28  /* I/O control */
#define MSDC_PATCH_BIT        0x30  /* Patch bit */
#define MSDC_PAD_CTRL0        0x40  /* Pad control 0 */
#define MSDC_PAD_CTRL1        0x44  /* Pad control 1 */
#define MSDC_PAD_CTRL2        0x48  /* Pad control 2 */
#define MSDC_PAD_TUNE         0x4C  /* Pad tuning */
#define MSDC_DMA_CTRL         0x60  /* DMA control */
#define MSDC_DMA_CFG          0x64  /* DMA configuration */
#define MSDC_DMA_ADDR         0x68  /* DMA address */
#define MSDC_DMA_LEN          0x6C  /* DMA length */
#define MSDC_CLOCK            0x70  /* Clock control */
#define MSDC_SDC_CFG          0x80  /* SD configuration */
#define MSDC_SDC_CMD          0x84  /* SD command */
#define MSDC_SDC_ARG          0x88  /* SD argument */
#define MSDC_SDC_STS          0x8C  /* SD status */

/* eMMC commands */
#define MMC_GO_IDLE_STATE           0   /* CMD0 */
#define MMC_SEND_OP_COND            1   /* CMD1 */
#define MMC_ALL_SEND_CID            2   /* CMD2 */
#define MMC_SET_RELATIVE_ADDR       3   /* CMD3 */
#define MMC_SELECT_CARD             7   /* CMD7 */
#define MMC_SEND_EXT_CSD            8   /* CMD8 */
#define MMC_SEND_CSD                9   /* CMD9 */
#define MMC_STOP_TRANSMISSION       12  /* CMD12 */
#define MMC_SEND_STATUS             13  /* CMD13 */
#define MMC_SET_BLOCKLEN            16  /* CMD16 */
#define MMC_READ_SINGLE_BLOCK       17  /* CMD17 */
#define MMC_READ_MULTIPLE_BLOCK     18  /* CMD18 */
#define MMC_WRITE_BLOCK             24  /* CMD24 */
#define MMC_WRITE_MULTIPLE_BLOCK    25  /* CMD25 */
#define MMC_APP_CMD                 55  /* CMD55 */

/* Command types */
#define MSDC_CMD_TYPE_CMD       0
#define MSDC_CMD_TYPE_WRITE     1
#define MSDC_CMD_TYPE_READ      2

/* Status bits */
#define MSDC_STS_CMDBUSY        (1 << 0)
#define MSDC_STS_CMDINHIBIT     (1 << 1)
#define MSDC_STS_DATINHIBIT     (1 << 2)

/* Interrupt bits */
#define MSDC_INT_CMDRDY         (1 << 0)
#define MSDC_INT_CMDTO          (1 << 1)
#define MSDC_INT_RSPCRCERR      (1 << 2)
#define MSDC_INT_XFER_COMPL     (1 << 3)
#define MSDC_INT_XFER_INVALID   (1 << 4)
#define MSDC_INT_DATTMO         (1 << 5)
#define MSDC_INT_DATCRCERR      (1 << 6)

/* eMMC card state */
typedef enum {
    EMMC_STATE_IDLE = 0,
    EMMC_STATE_READY = 1,
    EMMC_STATE_IDENT = 2,
    EMMC_STATE_STBY = 3,
    EMMC_STATE_TRAN = 4,
    EMMC_STATE_DATA = 5,
    EMMC_STATE_RCV = 6,
    EMMC_STATE_PRG = 7,
    EMMC_STATE_DIS = 8,
    EMMC_STATE_BTST = 9,
    EMMC_STATE_SLP = 10,
} emmc_state_t;

/* eMMC card info */
typedef struct {
    uint32_t rca;           /* Relative card address */
    uint32_t cid[4];        /* Card ID */
    uint32_t csd[4];        /* Card specific data */
    uint32_t ext_csd[128];  /* Extended CSD */
    uint64_t capacity;      /* Capacity in bytes */
    uint32_t block_size;    /* Block size */
    emmc_state_t state;     /* Current state */
    bool high_capacity;     /* High capacity card */
    bool initialized;
} emmc_card_t;

/* MSDC controller context */
typedef struct {
    volatile uint32_t *base;
    uint32_t base_addr;
    uint8_t host_num;
    uint32_t clock;
    emmc_card_t card;
    bool initialized;
} mtk_msdc_t;

/* Function prototypes */
int mtk_msdc_init(mtk_msdc_t *msdc, uint32_t base_addr, uint8_t host_num);
int mtk_msdc_card_init(mtk_msdc_t *msdc);
int mtk_msdc_read(mtk_msdc_t *msdc, uint64_t lba, uint32_t blocks, void *buf);
int mtk_msdc_write(mtk_msdc_t *msdc, uint64_t lba, uint32_t blocks, const void *buf);
int mtk_msdc_get_card_info(mtk_msdc_t *msdc);

#endif /* _MTK_MSDC_H_ */
