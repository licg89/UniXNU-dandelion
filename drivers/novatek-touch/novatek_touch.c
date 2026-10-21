/*
 * Novatek Touch Controller Driver Implementation
 */

#include "novatek_touch.h"
#include <stdio.h>
#include <string.h>

/*
 * Initialize Novatek touch controller
 */
int nova_touch_init(nova_touch_t *touch, mtk_i2c_t *i2c, uint8_t i2c_addr)
{
    if (!touch || !i2c) {
        return -1;
    }

    touch->i2c = i2c;
    touch->i2c_addr = i2c_addr;
    touch->irq = NOVA_TOUCH_IRQ;
    touch->max_touches = 5;
    touch->max_x = 720;   /* Screen width */
    touch->max_y = 1600;  /* Screen height */
    touch->initialized = false;
    touch->num_points = 0;

    /* Clear touch points */
    memset(touch->points, 0, sizeof(touch->points));

    /* Read IC ID to verify communication */
    int ret = nova_touch_read_id(touch);
    if (ret < 0) {
        printf("Novatek Touch: Failed to read IC ID\n");
        return ret;
    }

    printf("Novatek Touch: IC ID 0x%04X, FW v%d\n", touch->ic_id, touch->fw_ver);
    touch->initialized = true;

    return 0;
}

/*
 * Read IC ID and firmware version
 */
int nova_touch_read_id(nova_touch_t *touch)
{
    if (!touch || !touch->i2c || !touch->i2c->initialized) {
        return -1;
    }

    uint8_t id_data[2];
    int ret = mtk_i2c_read(touch->i2c, touch->i2c_addr, NOVA_REG_IC_ID, id_data, 2);
    if (ret < 0) {
        return ret;
    }
    touch->ic_id = (id_data[0] << 8) | id_data[1];

    uint8_t fw_data[2];
    ret = mtk_i2c_read(touch->i2c, touch->i2c_addr, NOVA_REG_FW_VER, fw_data, 2);
    if (ret < 0) {
        return ret;
    }
    touch->fw_ver = (fw_data[0] << 8) | fw_data[1];

    return 0;
}

/*
 * Read touch events
 */
int nova_touch_read_event(nova_touch_t *touch)
{
    if (!touch || !touch->initialized) {
        return -1;
    }

    /* Read event status */
    uint8_t event;
    int ret = mtk_i2c_read(touch->i2c, touch->i2c_addr, NOVA_REG_EVENT, &event, 1);
    if (ret < 0) {
        return ret;
    }

    if (event == NOVA_EVENT_NONE) {
        touch->num_points = 0;
        return 0;
    }

    /* Read number of touch points */
    uint8_t num_points;
    ret = mtk_i2c_read(touch->i2c, touch->i2c_addr, NOVA_REG_TOUCH_NUM, &num_points, 1);
    if (ret < 0) {
        return ret;
    }

    if (num_points > touch->max_touches) {
        num_points = touch->max_touches;
    }

    touch->num_points = num_points;

    /* Read touch data for each point */
    for (uint8_t i = 0; i < num_points; i++) {
        uint8_t point_data[NOVA_TOUCH_POINT_SIZE];
        uint8_t reg_base = NOVA_TOUCH_DATA_BASE + (i * NOVA_TOUCH_POINT_SIZE);

        ret = mtk_i2c_read(touch->i2c, touch->i2c_addr, reg_base, point_data, NOVA_TOUCH_POINT_SIZE);
        if (ret < 0) {
            return ret;
        }

        /* Parse touch point data */
        touch->points[i].x = (point_data[0] << 8) | point_data[1];
        touch->points[i].y = (point_data[2] << 8) | point_data[3];
        touch->points[i].pressure = point_data[4];
        touch->points[i].area = point_data[5];
        touch->points[i].event_type = point_data[6];
        touch->points[i].valid = true;
    }

    /* Clear valid flag for unused points */
    for (uint8_t i = num_points; i < touch->max_touches; i++) {
        touch->points[i].valid = false;
    }

    return 0;
}

/*
 * Get current touch points
 */
int nova_touch_get_points(nova_touch_t *touch, nova_touch_point_t *points, uint8_t *count)
{
    if (!touch || !touch->initialized || !points || !count) {
        return -1;
    }

    /* Read latest events */
    int ret = nova_touch_read_event(touch);
    if (ret < 0) {
        return ret;
    }

    /* Copy touch points */
    *count = touch->num_points;
    for (uint8_t i = 0; i < touch->num_points; i++) {
        points[i] = touch->points[i];
    }

    return 0;
}

/*
 * Get firmware version
 */
int nova_touch_get_fw_version(nova_touch_t *touch, uint16_t *fw_ver)
{
    if (!touch || !touch->initialized || !fw_ver) {
        return -1;
    }

    *fw_ver = touch->fw_ver;
    return 0;
}
