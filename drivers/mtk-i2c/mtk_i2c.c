/*
 * MTK I2C Controller Driver Implementation
 */

#include "mtk_i2c.h"
#include <stdio.h>
#include <string.h>

/* Delay macro */
#define udelay(x) do { \
    volatile int __i; \
    for (__i = 0; __i < (x) * 10; __i++) \
        __asm__ volatile("" ::: "memory"); \
} while (0)

#define msleep(x) udelay((x) * 1000)

/*
 * Initialize I2C controller
 */
int mtk_i2c_init(mtk_i2c_t *i2c, uint32_t base_addr, uint8_t bus_num)
{
    if (!i2c) {
        return -1;
    }

    i2c->base = (volatile uint32_t *)base_addr;
    i2c->bus_num = bus_num;
    i2c->initialized = false;

    /* Reset FIFO */
    i2c->base[I2C_FIFO_ADDR_CLR >> 2] = 1;
    udelay(100);
    i2c->base[I2C_FIFO_ADDR_CLR >> 2] = 0;

    /* Disable interrupts */
    i2c->base[I2C_INTR_EN_REG >> 2] = 0;

    /* Clear interrupt status */
    i2c->base[I2C_INTR_STAT_REG >> 2] = 0xFFFFFFFF;

    /* Set default speed */
    mtk_i2c_set_speed(i2c, I2C_DEFAULT_SPEED_HZ);

    /* Enable I2C controller */
    i2c->base[I2C_CONTROL_REG >> 2] = I2C_CONTROL_I2CEN | I2C_CONTROL_ACKERR_DET_EN;

    i2c->initialized = true;
    printf("MTK I2C%d: Initialized at 0x%08X, speed %u Hz\n",
           bus_num, base_addr, i2c->speed_hz);

    return 0;
}

/*
 * Set I2C bus speed
 */
int mtk_i2c_set_speed(mtk_i2c_t *i2c, uint32_t speed_hz)
{
    if (!i2c || !i2c->initialized) {
        return -1;
    }

    /* Calculate clock divider */
    /* Formula: SCL = I2C_CLK / (2 * (clk_div + 1)) */
    /* Assuming I2C source clock is 100MHz (typical for MTK) */
    uint32_t i2c_clk = 100000000;  /* 100 MHz */
    uint32_t div = (i2c_clk / (2 * speed_hz)) - 1;

    if (div > 63) {
        div = 63;
    }

    i2c->base[I2C_CLOCK_DIV_REG >> 2] = div;
    i2c->speed_hz = speed_hz;
    i2c->clk_div = div;

    printf("MTK I2C%d: Speed set to %u Hz (div=%u)\n",
           i2c->bus_num, speed_hz, div);

    return 0;
}

/*
 * Wait for transfer completion
 */
static int mtk_i2c_wait_complete(mtk_i2c_t *i2c)
{
    int timeout = 10000;
    uint32_t status;

    do {
        status = i2c->base[I2C_INTR_STAT_REG >> 2];
        if (status & I2C_INT_TRANSAC_COMP) {
            /* Clear interrupt */
            i2c->base[I2C_INTR_STAT_REG >> 2] = I2C_INT_TRANSAC_COMP;
            return 0;
        }
        if (status & (I2C_INT_ACKERR | I2C_INT_DATAERR | I2C_INT_BUSERR)) {
            printf("MTK I2C%d: Transfer error (status=0x%x)\n",
                   i2c->bus_num, status);
            i2c->base[I2C_INTR_STAT_REG >> 2] = status;
            return -2;
        }
        udelay(1);
    } while (--timeout > 0);

    printf("MTK I2C%d: Transfer timeout\n", i2c->bus_num);
    return -1;
}

/*
 * Read from I2C device
 */
int mtk_i2c_read(mtk_i2c_t *i2c, uint8_t slave_addr, uint8_t reg,
                 uint8_t *data, uint32_t len)
{
    if (!i2c || !i2c->initialized || !data || len == 0) {
        return -1;
    }

    /* Set slave address (7-bit, shifted left) */
    i2c->base[I2C_ADDR_REG >> 2] = (slave_addr << 1) | 1;  /* Read bit */

    /* Write register address first */
    i2c->base[I2C_DATA_REG >> 2] = reg;
    i2c->base[I2C_TRANSFER_LEN_REG >> 2] = 1;
    i2c->base[I2C_TRANSAC_LEN_REG >> 2] = 1;

    /* Start transfer */
    i2c->base[I2C_START_REG >> 2] = 1;

    /* Wait for completion */
    int ret = mtk_i2c_wait_complete(i2c);
    if (ret < 0) {
        return ret;
    }

    /* Now read data */
    i2c->base[I2C_TRANSFER_LEN_REG >> 2] = len;
    i2c->base[I2C_TRANSAC_LEN_REG >> 2] = len;

    /* Set read direction */
    uint32_t ctrl = i2c->base[I2C_CONTROL_REG >> 2];
    ctrl |= I2C_CONTROL_DATA_DIR;  /* Read */
    i2c->base[I2C_CONTROL_REG >> 2] = ctrl;

    /* Start read transfer */
    i2c->base[I2C_START_REG >> 2] = 1;

    /* Wait for completion */
    ret = mtk_i2c_wait_complete(i2c);
    if (ret < 0) {
        return ret;
    }

    /* Read data from FIFO */
    for (uint32_t i = 0; i < len; i++) {
        data[i] = i2c->base[I2C_DATA_REG >> 2] & 0xFF;
    }

    return 0;
}

/*
 * Write to I2C device
 */
int mtk_i2c_write(mtk_i2c_t *i2c, uint8_t slave_addr, uint8_t reg,
                  uint8_t *data, uint32_t len)
{
    if (!i2c || !i2c->initialized || !data) {
        return -1;
    }

    /* Set slave address (7-bit, shifted left) */
    i2c->base[I2C_ADDR_REG >> 2] = (slave_addr << 1);  /* Write bit */

    /* Write register address + data */
    i2c->base[I2C_DATA_REG >> 2] = reg;
    for (uint32_t i = 0; i < len; i++) {
        i2c->base[I2C_DATA_REG >> 2] = data[i];
    }

    i2c->base[I2C_TRANSFER_LEN_REG >> 2] = 1 + len;
    i2c->base[I2C_TRANSAC_LEN_REG >> 2] = 1 + len;

    /* Clear read direction */
    uint32_t ctrl = i2c->base[I2C_CONTROL_REG >> 2];
    ctrl &= ~I2C_CONTROL_DATA_DIR;  /* Write */
    i2c->base[I2C_CONTROL_REG >> 2] = ctrl;

    /* Start transfer */
    i2c->base[I2C_START_REG >> 2] = 1;

    /* Wait for completion */
    return mtk_i2c_wait_complete(i2c);
}

/*
 * Scan I2C bus for devices
 */
int mtk_i2c_scan(mtk_i2c_t *i2c, uint8_t *devices, uint32_t *count)
{
    if (!i2c || !i2c->initialized || !devices || !count) {
        return -1;
    }

    *count = 0;

    printf("MTK I2C%d: Scanning bus...\n", i2c->bus_num);

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        int ret = mtk_i2c_read(i2c, addr, 0, &dummy, 1);
        if (ret == 0) {
            printf("  Found device at 0x%02X\n", addr);
            devices[*count] = addr;
            (*count)++;
        }
    }

    printf("MTK I2C%d: Scan complete, found %u devices\n",
           i2c->bus_num, *count);

    return 0;
}
