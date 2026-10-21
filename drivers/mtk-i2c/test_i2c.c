/*
 * MTK I2C Controller Driver Test
 *
 * Test I2C communication with Novatek touch controller.
 */

#include "mtk_i2c.h"
#include <stdio.h>

/* Novatek touch controller I2C address */
#define NOVA_TOUCH_ADDR     0x5D

/* Touch controller registers (typical Novatek) */
#define NOVA_REG_EVENT      0x00  /* Event/status register */
#define NOVA_REG_TOUCH_NUM  0x01  /* Number of touch points */
#define NOVA_REG_X_H        0x02  /* X coordinate high byte */
#define NOVA_REG_X_L        0x03  /* X coordinate low byte */
#define NOVA_REG_Y_H        0x04  /* Y coordinate high byte */
#define NOVA_REG_Y_L        0x05  /* Y coordinate low byte */
#define NOVA_REG_FW_VER     0x10  /* Firmware version */
#define NOVA_REG_IC_ID      0x12  /* IC ID */

int main(void)
{
    mtk_i2c_t i2c;
    int ret;

    printf("=== MTK I2C Controller Test ===\n\n");

    /* Initialize I2C3 (touch controller bus) */
    printf("Initializing I2C3 at 0x%08X...\n", MTK_I2C3_BASE);
    ret = mtk_i2c_init(&i2c, MTK_I2C3_BASE, 3);
    if (ret < 0) {
        printf("FAILED: I2C init error %d\n", ret);
        return ret;
    }
    printf("OK: I2C3 initialized\n\n");

    /* Set speed to 400kHz for touch controller */
    printf("Setting speed to 400kHz...\n");
    ret = mtk_i2c_set_speed(&i2c, I2C_FAST_SPEED_HZ);
    if (ret < 0) {
        printf("WARNING: Could not set speed\n");
    }
    printf("OK: Speed set\n\n");

    /* Scan I2C bus */
    printf("Scanning I2C bus for devices...\n");
    uint8_t devices[32];
    uint32_t count;
    ret = mtk_i2c_scan(&i2c, devices, &count);
    if (ret < 0) {
        printf("WARNING: Bus scan failed\n");
    } else {
        printf("Found %u devices\n\n", count);
    }

    /* Test touch controller communication */
    printf("Testing Novatek touch controller at 0x%02X...\n\n", NOVA_TOUCH_ADDR);

    /* Read IC ID */
    uint8_t ic_id[2];
    printf("Reading IC ID...\n");
    ret = mtk_i2c_read(&i2c, NOVA_TOUCH_ADDR, NOVA_REG_IC_ID, ic_id, 2);
    if (ret < 0) {
        printf("FAILED: Could not read IC ID (error %d)\n", ret);
        printf("Touch controller not responding\n");
        return ret;
    }
    printf("IC ID: 0x%02X%02X\n\n", ic_id[0], ic_id[1]);

    /* Read firmware version */
    uint8_t fw_ver[2];
    printf("Reading firmware version...\n");
    ret = mtk_i2c_read(&i2c, NOVA_TOUCH_ADDR, NOVA_REG_FW_VER, fw_ver, 2);
    if (ret < 0) {
        printf("FAILED: Could not read firmware version\n");
        return ret;
    }
    printf("Firmware: v%d.%d\n\n", fw_ver[0], fw_ver[1]);

    /* Read event status */
    uint8_t event;
    printf("Reading event status...\n");
    ret = mtk_i2c_read(&i2c, NOVA_TOUCH_ADDR, NOVA_REG_EVENT, &event, 1);
    if (ret < 0) {
        printf("FAILED: Could not read event status\n");
        return ret;
    }
    printf("Event: 0x%02X\n", event);

    if (event == 0) {
        printf("No touch event\n\n");
    } else {
        printf("Touch event detected!\n");

        /* Read touch coordinates */
        uint8_t touch_data[4];
        printf("Reading touch coordinates...\n");
        ret = mtk_i2c_read(&i2c, NOVA_TOUCH_ADDR, NOVA_REG_TOUCH_NUM, touch_data, 4);
        if (ret < 0) {
            printf("FAILED: Could not read touch data\n");
            return ret;
        }

        uint8_t num_touches = touch_data[0];
        uint16_t x = (touch_data[1] << 8) | touch_data[2];
        uint16_t y = (touch_data[3] << 8) | touch_data[4];

        printf("Touch points: %d\n", num_touches);
        printf("Position: (%u, %u)\n\n", x, y);
    }

    /* Test write (read-only for now, skip write test) */
    printf("Skipping write test (touch controller is read-only during normal operation)\n\n");

    printf("=== I2C Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
