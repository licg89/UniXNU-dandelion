/*
 * MTK I2C Controller Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 * Based on Linux kernel MTK I2C driver
 *
 * Supports MT6765 I2C controllers for touch, sensors, etc.
 */

#ifndef _MTK_I2C_H_
#define _MTK_I2C_H_

#include <stdint.h>
#include <stdbool.h>

/* I2C controller base addresses (from /proc/iomem) */
#define MTK_I2C0_BASE   0x11007000
#define MTK_I2C1_BASE   0x11008000
#define MTK_I2C2_BASE   0x11009000
#define MTK_I2C3_BASE   0x1100F000  /* Touch controller */
#define MTK_I2C4_BASE   0x11011000
#define MTK_I2C5_BASE   0x11016000
#define MTK_I2C6_BASE   0x1100D000

/* I2C registers */
#define I2C_DATA_REG          0x00  /* Data register */
#define I2C_ADDR_REG          0x04  /* Slave address */
#define I2C_CONTROL_REG       0x08  /* Control register */
#define I2C_EXT_CONF_REG      0x0C  /* Extended configuration */
#define I2C_CLOCK_DIV_REG     0x10  /* Clock division */
#define I2C_START_REG         0x14  /* Start transaction */
#define I2C_STOP_REG          0x18  /* Stop condition */
#define I2C_INTR_STAT_REG     0x1C  /* Interrupt status */
#define I2C_INTR_EN_REG       0x20  /* Interrupt enable */
#define I2C_FIFO_ADDR_CLR     0x24  /* FIFO address clear */
#define I2C_TRANSFER_LEN_REG  0x28  /* Transfer length */
#define I2C_TRANSAC_LEN_REG   0x2C  /* Transaction length */

/* Control register bits */
#define I2C_CONTROL_ACKERR_DET_EN    (1 << 0)
#define I2C_CONTROL_CLK_EXT_EN       (1 << 1)
#define I2C_CONTROL_DMAEN_EN         (1 << 2)
#define I2C_CONTROL_DATA_DIR         (1 << 4)
#define I2C_CONTROL_CLK_OUT_EN       (1 << 5)
#define I2C_CONTROL_SMTH_EN          (1 << 6)
#define I2C_CONTROL_PMIC_RD_NSTOP    (1 << 7)
#define I2C_CONTROL_RS                 (1 << 8)
#define I2C_CONTROL_DMAACK_EN        (1 << 9)
#define I2C_CONTROL_STIG                 (1 << 10)
#define I2C_CONTROL_DIR_CHANGE       (1 << 11)
#define I2C_CONTROL_FIFORD_CLR       (1 << 12)
#define I2C_CONTROL_I2CEN            (1 << 13)

/* Interrupt status bits */
#define I2C_INT_TRANSAC_COMP    (1 << 0)
#define I2C_INT_ACKERR          (1 << 1)
#define I2C_INT_DATAERR         (1 << 2)
#define I2C_INT_MASTER_NACK     (1 << 3)
#define I2C_INT_MASTER_ARB      (1 << 4)
#define I2C_INT_BUSERR          (1 << 5)
#define I2C_INT_HS_NACK         (1 << 6)

/* Default clock speed */
#define I2C_DEFAULT_SPEED_HZ    100000  /* 100 kHz */
#define I2C_FAST_SPEED_HZ       400000  /* 400 kHz */

/* Driver context */
typedef struct {
    volatile uint32_t *base;
    uint32_t speed_hz;
    uint32_t clk_div;
    bool initialized;
    uint8_t bus_num;
} mtk_i2c_t;

/* Function prototypes */
int mtk_i2c_init(mtk_i2c_t *i2c, uint32_t base_addr, uint8_t bus_num);
int mtk_i2c_read(mtk_i2c_t *i2c, uint8_t slave_addr, uint8_t reg, uint8_t *data, uint32_t len);
int mtk_i2c_write(mtk_i2c_t *i2c, uint8_t slave_addr, uint8_t reg, uint8_t *data, uint32_t len);
int mtk_i2c_set_speed(mtk_i2c_t *i2c, uint32_t speed_hz);
int mtk_i2c_scan(mtk_i2c_t *i2c, uint8_t *devices, uint32_t *count);

#endif /* _MTK_I2C_H_ */
