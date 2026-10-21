/*
 * Novatek Touch Controller Driver Test
 */

#include "novatek_touch.h"
#include <stdio.h>

int main(void)
{
    mtk_i2c_t i2c;
    nova_touch_t touch;
    int ret;

    printf("=== Novatek Touch Controller Test ===\n\n");

    /* Initialize I2C3 (touch controller bus) */
    printf("Initializing I2C3...\n");
    ret = mtk_i2c_init(&i2c, MTK_I2C3_BASE, 3);
    if (ret < 0) {
        printf("FAILED: I2C init error %d\n", ret);
        return ret;
    }
    printf("OK: I2C3 initialized\n\n");

    /* Set speed to 400kHz */
    mtk_i2c_set_speed(&i2c, I2C_FAST_SPEED_HZ);

    /* Initialize touch controller */
    printf("Initializing Novatek touch controller at 0x%02X...\n", NOVA_TOUCH_I2C_ADDR);
    ret = nova_touch_init(&touch, &i2c, NOVA_TOUCH_I2C_ADDR);
    if (ret < 0) {
        printf("FAILED: Touch init error %d\n", ret);
        return ret;
    }
    printf("OK: Touch controller initialized\n");
    printf("  IC ID: 0x%04X\n", touch.ic_id);
    printf("  FW Version: 0x%04X\n\n", touch.fw_ver);

    /* Read touch events in a loop */
    printf("Reading touch events (press Ctrl+C to stop)...\n\n");

    int loop_count = 0;
    while (loop_count < 100) {
        /* Get touch points */
        nova_touch_point_t points[5];
        uint8_t count;

        ret = nova_touch_get_points(&touch, points, &count);
        if (ret < 0) {
            printf("Error reading touch data\n");
            return ret;
        }

        if (count > 0) {
            printf("Touch points: %d\n", count);
            for (uint8_t i = 0; i < count; i++) {
                printf("  Point %d: (%d, %d) pressure=%d area=%d event=%d\n",
                       i, points[i].x, points[i].y,
                       points[i].pressure, points[i].area,
                       points[i].event_type);
            }
            printf("\n");
        }

        loop_count++;

        /* Small delay */
        volatile int delay;
        for (delay = 0; delay < 100000; delay++);
    }

    printf("=== Touch Test Complete ===\n");
    printf("Tested %d iterations\n", loop_count);

    return 0;
}
