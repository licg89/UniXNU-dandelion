/*
 * Novatek Touch Controller Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Supports Novatek touch controllers (NT series)
 * Used in Xiaomi Redmi 9A (dandelion)
 */

#ifndef _NOVATEK_TOUCH_H_
#define _NOVATEK_TOUCH_H_

#include <stdint.h>
#include <stdbool.h>
#include "../mtk-i2c/mtk_i2c.h"

/* Touch controller configuration */
#define NOVA_TOUCH_I2C_ADDR     0x5D
#define NOVA_TOUCH_IRQ          17

/* Touch controller registers */
#define NOVA_REG_EVENT          0x00  /* Event/status register */
#define NOVA_REG_TOUCH_NUM      0x01  /* Number of touch points */
#define NOVA_REG_X_H            0x02  /* X coordinate high byte */
#define NOVA_REG_X_L            0x03  /* X coordinate low byte */
#define NOVA_REG_Y_H            0x04  /* Y coordinate high byte */
#define NOVA_REG_Y_L            0x05  /* Y coordinate low byte */
#define NOVA_REG_PRESSURE       0x06  /* Touch pressure */
#define NOVA_REG_AREA           0x07  /* Touch area */
#define NOVA_REG_FW_VER         0x10  /* Firmware version */
#define NOVA_REG_IC_ID          0x12  /* IC ID */
#define NOVA_REG_FW_BUILD       0x14  /* Firmware build info */

/* Multi-touch point structure */
#define NOVA_TOUCH_DATA_BASE    0x20  /* Multi-touch data base */
#define NOVA_TOUCH_POINT_SIZE   0x08  /* Size per touch point */

/* Event types */
#define NOVA_EVENT_NONE         0x00
#define NOVA_EVENT_TOUCH_DOWN   0x01
#define NOVA_EVENT_TOUCH_MOVE   0x02
#define NOVA_EVENT_TOUCH_UP     0x03

/* Touch point structure */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t pressure;
    uint8_t area;
    uint8_t event_type;
    bool valid;
} nova_touch_point_t;

/* Touch controller state */
typedef struct {
    mtk_i2c_t *i2c;
    uint8_t i2c_addr;
    uint8_t irq;
    uint16_t ic_id;
    uint16_t fw_ver;
    uint8_t max_touches;
    uint16_t max_x;
    uint16_t max_y;
    bool initialized;
    nova_touch_point_t points[5];  /* Support up to 5 touch points */
    uint8_t num_points;
} nova_touch_t;

/* Function prototypes */
int nova_touch_init(nova_touch_t *touch, mtk_i2c_t *i2c, uint8_t i2c_addr);
int nova_touch_read_id(nova_touch_t *touch);
int nova_touch_read_event(nova_touch_t *touch);
int nova_touch_get_points(nova_touch_t *touch, nova_touch_point_t *points, uint8_t *count);
int nova_touch_get_fw_version(nova_touch_t *touch, uint16_t *fw_ver);

#endif /* _NOVATEK_TOUCH_H_ */
