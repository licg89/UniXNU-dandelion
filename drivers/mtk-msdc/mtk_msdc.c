/*
 * MTK MSDC eMMC Driver Implementation
 */

#include "mtk_msdc.h"
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
 * Initialize MSDC controller
 */
int mtk_msdc_init(mtk_msdc_t *msdc, uint32_t base_addr, uint8_t host_num)
{
    if (!msdc) {
        return -1;
    }

    msdc->base = (volatile uint32_t *)base_addr;
    msdc->base_addr = base_addr;
    msdc->host_num = host_num;
    msdc->clock = 25000000;  /* 25 MHz initial clock */
    msdc->initialized = false;

    memset(&msdc->card, 0, sizeof(msdc->card));

    printf("MTK MSDC%d: Initializing at 0x%08X\n", host_num, base_addr);

    /* Reset controller */
    msdc->base[MSDC_CFG >> 2] = 0x01;  /* Reset */
    udelay(100);
    msdc->base[MSDC_CFG >> 2] = 0x00;

    /* Configure FIFO */
    msdc->base[MSDC_FIFO_CFG >> 2] = 0x10101010;

    /* Clear interrupts */
    msdc->base[MSDC_INT >> 2] = 0xFFFFFFFF;

    /* Disable interrupts */
    msdc->base[MSDC_INTEN >> 2] = 0x00000000;

    /* Set clock */
    msdc->base[MSDC_CLOCK >> 2] = 0x0001;  /* Enable clock */

    printf("MTK MSDC%d: Controller initialized\n", host_num);

    msdc->initialized = true;
    return 0;
}

/*
 * Send command to eMMC card
 */
static int mtk_msdc_send_cmd(mtk_msdc_t *msdc, uint8_t cmd, uint32_t arg, uint32_t *rsp)
{
    uint32_t status;
    int timeout = 100000;

    /* Wait for command inhibit */
    do {
        status = msdc->base[MSDC_STS >> 2];
        if (!(status & MSDC_STS_CMDINHIBIT)) {
            break;
        }
        udelay(1);
    } while (--timeout > 0);

    if (timeout == 0) {
        printf("MTK MSDC%d: Command inhibit timeout\n", msdc->host_num);
        return -1;
    }

    /* Clear interrupts */
    msdc->base[MSDC_INT >> 2] = 0xFFFFFFFF;

    /* Set argument */
    msdc->base[MSDC_ARG >> 2] = arg;

    /* Send command */
    msdc->base[MSDC_CMD >> 2] = (cmd << 8) | 0x01;  /* Command + start bit */

    /* Wait for command completion */
    timeout = 100000;
    do {
        status = msdc->base[MSDC_INT >> 2];
        if (status & MSDC_INT_CMDRDY) {
            /* Command ready */
            msdc->base[MSDC_INT >> 2] = MSDC_INT_CMDRDY;
            if (rsp) {
                *rsp = msdc->base[MSDC_RSP >> 2];
            }
            return 0;
        }
        if (status & (MSDC_INT_CMDTO | MSDC_INT_RSPCRCERR)) {
            printf("MTK MSDC%d: Command error (status=0x%x)\n", msdc->host_num, status);
            msdc->base[MSDC_INT >> 2] = status;
            return -2;
        }
        udelay(1);
    } while (--timeout > 0);

    printf("MTK MSDC%d: Command timeout\n", msdc->host_num);
    return -1;
}

/*
 * Initialize eMMC card
 */
int mtk_msdc_card_init(mtk_msdc_t *msdc)
{
    uint32_t rsp;
    int ret;

    printf("MTK MSDC%d: Initializing eMMC card...\n", msdc->host_num);

    /* CMD0: GO_IDLE_STATE */
    ret = mtk_msdc_send_cmd(msdc, MMC_GO_IDLE_STATE, 0, NULL);
    if (ret < 0) {
        printf("MTK MSDC%d: CMD0 failed\n", msdc->host_num);
        return ret;
    }
    printf("MTK MSDC%d: Card in idle state\n", msdc->host_num);
    msleep(10);

    /* CMD1: SEND_OP_COND (with HCS bit) */
    uint32_t retry = 100;
    do {
        ret = mtk_msdc_send_cmd(msdc, MMC_SEND_OP_COND, 0x40000080, &rsp);
        if (ret < 0) {
            printf("MTK MSDC%d: CMD1 failed\n", msdc->host_num);
            return ret;
        }
        if (rsp & 0x80000000) {
            /* Card is ready */
            msdc->card.high_capacity = (rsp & 0x40000000) ? true : false;
            break;
        }
        msleep(10);
    } while (--retry > 0);

    if (retry == 0) {
        printf("MTK MSDC%d: Card init timeout\n", msdc->host_num);
        return -1;
    }

    printf("MTK MSDC%d: Card ready (HCC=%d)\n", msdc->host_num, msdc->card.high_capacity);

    /* CMD2: ALL_SEND_CID */
    ret = mtk_msdc_send_cmd(msdc, MMC_ALL_SEND_CID, 0, msdc->card.cid);
    if (ret < 0) {
        printf("MTK MSDC%d: CMD2 failed\n", msdc->host_num);
        return ret;
    }
    printf("MTK MSDC%d: CID: 0x%08X 0x%08X 0x%08X 0x%08X\n",
           msdc->host_num, msdc->card.cid[0], msdc->card.cid[1],
           msdc->card.cid[2], msdc->card.cid[3]);

    /* CMD3: SET_RELATIVE_ADDR */
    msdc->card.rca = 0x00010000;  /* RCA = 1 */
    ret = mtk_msdc_send_cmd(msdc, MMC_SET_RELATIVE_ADDR, msdc->card.rca, &rsp);
    if (ret < 0) {
        printf("MTK MSDC%d: CMD3 failed\n", msdc->host_num);
        return ret;
    }
    printf("MTK MSDC%d: RCA set to 0x%08X\n", msdc->host_num, msdc->card.rca);

    /* CMD9: SEND_CSD */
    ret = mtk_msdc_send_cmd(msdc, MMC_SEND_CSD, msdc->card.rca, msdc->card.csd);
    if (ret < 0) {
        printf("MTK MSDC%d: CMD9 failed\n", msdc->host_num);
        return ret;
    }

    /* CMD7: SELECT_CARD */
    ret = mtk_msdc_send_cmd(msdc, MMC_SELECT_CARD, msdc->card.rca, &rsp);
    if (ret < 0) {
        printf("MTK MSDC%d: CMD7 failed\n", msdc->host_num);
        return ret;
    }
    printf("MTK MSDC%d: Card selected\n", msdc->host_num);

    msdc->card.initialized = true;

    return mtk_msdc_get_card_info(msdc);
}

/*
 * Get card information
 */
int mtk_msdc_get_card_info(mtk_msdc_t *msdc)
{
    if (!msdc->card.initialized) {
        return -1;
    }

    /* Parse CSD to get capacity */
    uint32_t csd_ver = (msdc->card.csd[0] >> 30) & 0x03;

    if (csd_ver == 1) {
        /* CSD v2.0 (high capacity) */
        uint32_t c_size = ((msdc->card.csd[1] >> 8) & 0x3FFFFF);
        msdc->card.capacity = (uint64_t)(c_size + 1) * 512 * 1024;
        printf("MTK MSDC%d: CSD v2.0, capacity %llu MB\n",
               msdc->host_num, msdc->card.capacity / (1024 * 1024));
    } else {
        /* CSD v1.0 */
        uint32_t c_size = ((msdc->card.csd[1] >> 16) & 0x0FFF) |
                         ((msdc->card.csd[2] & 0x03) << 16);
        uint32_t c_size_mult = (msdc->card.csd[2] >> 15) & 0x07;
        uint32_t read_bl_len = (msdc->card.csd[0] >> 16) & 0x0F;
        uint32_t mult = 1 << (c_size_mult + 2);
        uint32_t blocknr = (c_size + 1) * mult;
        uint32_t block_len = 1 << read_bl_len;
        msdc->card.capacity = (uint64_t)blocknr * block_len;
        printf("MTK MSDC%d: CSD v1.0, capacity %llu MB\n",
               msdc->host_num, msdc->card.capacity / (1024 * 1024));
    }

    msdc->card.block_size = 512;

    return 0;
}

/*
 * Read blocks from eMMC
 */
int mtk_msdc_read(mtk_msdc_t *msdc, uint64_t lba, uint32_t blocks, void *buf)
{
    if (!msdc->card.initialized || !buf) {
        return -1;
    }

    uint32_t status;
    int timeout;

    /* Wait for data inhibit */
    timeout = 100000;
    do {
        status = msdc->base[MSDC_STS >> 2];
        if (!(status & MSDC_STS_DATINHIBIT)) {
            break;
        }
        udelay(1);
    } while (--timeout > 0);

    if (timeout == 0) {
        printf("MTK MSDC%d: Data inhibit timeout\n", msdc->host_num);
        return -1;
    }

    /* Set block size and count */
    msdc->base[MSDC_BLK_NUM >> 2] = blocks;

    /* Send read command */
    uint32_t arg = msdc->card.high_capacity ? (uint32_t)lba : (uint32_t)(lba * 512);
    uint32_t cmd = (blocks > 1) ? MMC_READ_MULTIPLE_BLOCK : MMC_READ_SINGLE_BLOCK;

    int ret = mtk_msdc_send_cmd(msdc, cmd, arg, NULL);
    if (ret < 0) {
        printf("MTK MSDC%d: Read command failed\n", msdc->host_num);
        return ret;
    }

    /* Read data from FIFO */
    uint32_t *data = (uint32_t *)buf;
    uint32_t words = blocks * 128;  /* 512 bytes = 128 words per block */

    for (uint32_t i = 0; i < words; i++) {
        timeout = 100000;
        do {
            status = msdc->base[MSDC_INT >> 2];
            if (status & MSDC_INT_XFER_COMPL) {
                data[i] = msdc->base[MSDC_FIFO_DATA >> 2];
                msdc->base[MSDC_INT >> 2] = MSDC_INT_XFER_COMPL;
                break;
            }
            if (status & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
                printf("MTK MSDC%d: Data transfer error\n", msdc->host_num);
                return -3;
            }
            udelay(1);
        } while (--timeout > 0);

        if (timeout == 0) {
            printf("MTK MSDC%d: Data read timeout\n", msdc->host_num);
            return -1;
        }
    }

    /* Send stop command for multi-block read */
    if (blocks > 1) {
        mtk_msdc_send_cmd(msdc, MMC_STOP_TRANSMISSION, 0, NULL);
    }

    return 0;
}

/*
 * Write blocks to eMMC
 */
int mtk_msdc_write(mtk_msdc_t *msdc, uint64_t lba, uint32_t blocks, const void *buf)
{
    if (!msdc->card.initialized || !buf) {
        return -1;
    }

    uint32_t status;
    int timeout;

    /* Wait for data inhibit */
    timeout = 100000;
    do {
        status = msdc->base[MSDC_STS >> 2];
        if (!(status & MSDC_STS_DATINHIBIT)) {
            break;
        }
        udelay(1);
    } while (--timeout > 0);

    if (timeout == 0) {
        printf("MTK MSDC%d: Data inhibit timeout\n", msdc->host_num);
        return -1;
    }

    /* Set block size and count */
    msdc->base[MSDC_BLK_NUM >> 2] = blocks;

    /* Send write command */
    uint32_t arg = msdc->card.high_capacity ? (uint32_t)lba : (uint32_t)(lba * 512);
    uint32_t cmd = (blocks > 1) ? MMC_WRITE_MULTIPLE_BLOCK : MMC_WRITE_BLOCK;

    int ret = mtk_msdc_send_cmd(msdc, cmd, arg, NULL);
    if (ret < 0) {
        printf("MTK MSDC%d: Write command failed\n", msdc->host_num);
        return ret;
    }

    /* Write data to FIFO */
    const uint32_t *data = (const uint32_t *)buf;
    uint32_t words = blocks * 128;

    for (uint32_t i = 0; i < words; i++) {
        timeout = 100000;
        do {
            status = msdc->base[MSDC_STS >> 2];
            if (!(status & MSDC_STS_DATINHIBIT)) {
                break;
            }
            udelay(1);
        } while (--timeout > 0);

        if (timeout == 0) {
            printf("MTK MSDC%d: Write timeout\n", msdc->host_num);
            return -1;
        }

        msdc->base[MSDC_FIFO_DATA >> 2] = data[i];
    }

    /* Wait for transfer complete */
    timeout = 100000;
    do {
        status = msdc->base[MSDC_INT >> 2];
        if (status & MSDC_INT_XFER_COMPL) {
            msdc->base[MSDC_INT >> 2] = MSDC_INT_XFER_COMPL;
            break;
        }
        if (status & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
            printf("MTK MSDC%d: Write error\n", msdc->host_num);
            return -3;
        }
        udelay(1);
    } while (--timeout > 0);

    /* Send stop command for multi-block write */
    if (blocks > 1) {
        mtk_msdc_send_cmd(msdc, MMC_STOP_TRANSMISSION, 0, NULL);
    }

    return 0;
}
