/*
 * MT6357 PMIC Driver Implementation
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Implements basic PMIC register access via PMIC wrapper for MT6765.
 */

#include "mtk_pmic.h"
#include <stdio.h>

/* Delay macro (simple busy-wait) */
#define udelay(x) do { \
    volatile int __i; \
    for (__i = 0; __i < (x) * 10; __i++) \
        __asm__ volatile("" ::: "memory"); \
} while (0)

/*
 * Initialize PMIC driver
 *
 * @param pmic     Pointer to PMIC context
 * @param base_addr PMIC wrapper base address
 * @return 0 on success, negative on error
 */
int mtk_pmic_init(mtk_pmic_t *pmic, uint32_t base_addr)
{
    if (!pmic) {
        return -1;
    }

    pmic->wrap_base = (volatile uint32_t *)base_addr;
    pmic->initialized = false;

    /* Wait for PMIC wrapper initialization */
    udelay(100);

    /* Check initialization done */
    uint32_t init_done = pmic->wrap_base[PWRAP_INIT_DONE0 >> 2];
    if (!(init_done & 0x1)) {
        printf("MTK PMIC: Wrapper not initialized (0x%x)\n", init_done);
        return -2;
    }

    /* Read chip ID */
    uint16_t cid;
    int ret = mtk_pmic_read(pmic, MT6357_CID, &cid);
    if (ret < 0) {
        printf("MTK PMIC: Failed to read CID\n");
        return ret;
    }

    pmic->cid = cid;
    printf("MTK PMIC: Chip ID = 0x%04X\n", cid);

    /* Verify chip ID (MT6357 should be 0x6357 or similar) */
    if ((cid & 0xFFF0) != 0x6350) {
        printf("MTK PMIC: Warning: Unexpected chip ID 0x%04X\n", cid);
        /* Continue anyway - might be a variant */
    }

    pmic->initialized = true;
    printf("MTK PMIC: Initialized successfully\n");

    return 0;
}

/*
 * Read PMIC register via PMIC wrapper
 *
 * PMIC wrapper protocol:
 * 1. Write command to WFIFO (write FIFO)
 * 2. Wait for RFIFO (read FIFO) to have data
 * 3. Read result from RFIFO
 *
 * @param pmic  Pointer to PMIC context
 * @param reg   Register address (15-bit)
 * @param val   Pointer to store read value
 * @return 0 on success, negative on error
 */
int mtk_pmic_read(mtk_pmic_t *pmic, uint16_t reg, uint16_t *val)
{
    if (!pmic || !pmic->initialized || !val) {
        return -1;
    }

    /* Construct read command */
    uint32_t cmd = PMIC_CMD_READ | (reg & PMIC_REG_MASK);

    /* Write to WFIFO */
    pmic->wrap_base[PWRAP_WRAPPER_LO_WFIFO >> 2] = cmd;

    /* Wait for response in RFIFO (with timeout) */
    int timeout = 1000;
    uint32_t rdata;
    do {
        rdata = pmic->wrap_base[PWRAP_WRAPPER_LO_RFIFO >> 2];
        if (rdata & 0xFFFF0000) {
            /* Data ready */
            *val = rdata & PMIC_DATA_MASK;
            return 0;
        }
        udelay(1);
    } while (--timeout > 0);

    printf("MTK PMIC: Read timeout for reg 0x%04X\n", reg);
    return -2;
}

/*
 * Write PMIC register via PMIC wrapper
 *
 * @param pmic  Pointer to PMIC context
 * @param reg   Register address (15-bit)
 * @param val   Value to write (16-bit)
 * @return 0 on success, negative on error
 */
int mtk_pmic_write(mtk_pmic_t *pmic, uint16_t reg, uint16_t val)
{
    if (!pmic || !pmic->initialized) {
        return -1;
    }

    /* Construct write command */
    uint32_t cmd = PMIC_CMD_WRITE | (reg & PMIC_REG_MASK) | ((uint32_t)val << 16);

    /* Write to WFIFO */
    pmic->wrap_base[PWRAP_WRAPPER_LO_WFIFO >> 2] = cmd;

    /* Wait for completion (no data returned for writes) */
    int timeout = 1000;
    do {
        uint32_t status = pmic->wrap_base[PWRAP_WRAPPER_LO_WCLR >> 2];
        if (status & 0x1) {
            /* Write complete */
            return 0;
        }
        udelay(1);
    } while (--timeout > 0);

    printf("MTK PMIC: Write timeout for reg 0x%04X\n", reg);
    return -2;
}

/*
 * Set bits in PMIC register (read-modify-write)
 *
 * @param pmic  Pointer to PMIC context
 * @param reg   Register address
 * @param bits  Bits to set
 * @return 0 on success, negative on error
 */
int mtk_pmic_set_bits(mtk_pmic_t *pmic, uint16_t reg, uint16_t bits)
{
    uint16_t val;
    int ret = mtk_pmic_read(pmic, reg, &val);
    if (ret < 0) {
        return ret;
    }

    val |= bits;
    return mtk_pmic_write(pmic, reg, val);
}

/*
 * Clear bits in PMIC register (read-modify-write)
 *
 * @param pmic  Pointer to PMIC context
 * @param reg   Register address
 * @param bits  Bits to clear
 * @return 0 on success, negative on error
 */
int mtk_pmic_clear_bits(mtk_pmic_t *pmic, uint16_t reg, uint16_t bits)
{
    uint16_t val;
    int ret = mtk_pmic_read(pmic, reg, &val);
    if (ret < 0) {
        return ret;
    }

    val &= ~bits;
    return mtk_pmic_write(pmic, reg, val);
}

/*
 * Get PMIC chip ID
 *
 * @param pmic  Pointer to PMIC context
 * @return Chip ID
 */
uint16_t mtk_pmic_get_cid(mtk_pmic_t *pmic)
{
    if (!pmic || !pmic->initialized) {
        return 0;
    }
    return pmic->cid;
}

/*
 * Get PMIC status
 *
 * @param pmic    Pointer to PMIC context
 * @param status  Pointer to store status value
 * @return 0 on success, negative on error
 */
int mtk_pmic_get_status(mtk_pmic_t *pmic, uint16_t *status)
{
    if (!pmic || !status) {
        return -1;
    }

    /* Read interrupt status register */
    return mtk_pmic_read(pmic, MT6357_INT_STATUS0, status);
}
