/*
 * MTK MSDC eMMC Driver Test
 */

#include "mtk_msdc.h"
#include <stdio.h>

int main(void)
{
    mtk_msdc_t msdc;
    int ret;

    printf("=== MTK MSDC eMMC Test ===\n\n");

    /* Initialize MSDC0 (eMMC) */
    printf("Initializing MSDC0 at 0x%08X...\n", MTK_MSDC0_BASE);
    ret = mtk_msdc_init(&msdc, MTK_MSDC0_BASE, 0);
    if (ret < 0) {
        printf("FAILED: MSDC init error %d\n", ret);
        return ret;
    }
    printf("OK: MSDC0 initialized\n\n");

    /* Initialize eMMC card */
    printf("Initializing eMMC card...\n");
    ret = mtk_msdc_card_init(&msdc);
    if (ret < 0) {
        printf("FAILED: Card init error %d\n", ret);
        return ret;
    }
    printf("OK: eMMC card initialized\n");
    printf("  Capacity: %llu MB (%llu GB)\n",
           msdc.card.capacity / (1024 * 1024),
           msdc.card.capacity / (1024 * 1024 * 1024));
    printf("  Block size: %u bytes\n", msdc.card.block_size);
    printf("  High capacity: %s\n\n", msdc.card.high_capacity ? "YES" : "NO");

    /* Test read */
    printf("Testing read (LBA 0)...\n");
    uint8_t buf[512];
    ret = mtk_msdc_read(&msdc, 0, 1, buf);
    if (ret < 0) {
        printf("FAILED: Read error %d\n", ret);
        return ret;
    }
    printf("OK: Read 512 bytes from LBA 0\n");

    /* Print first 64 bytes */
    printf("First 64 bytes:\n");
    for (int i = 0; i < 64; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    /* Test multi-block read */
    printf("Testing multi-block read (LBA 0, 8 blocks)...\n");
    uint8_t buf2[4096];
    ret = mtk_msdc_read(&msdc, 0, 8, buf2);
    if (ret < 0) {
        printf("FAILED: Multi-block read error %d\n", ret);
        return ret;
    }
    printf("OK: Read 4096 bytes\n\n");

    /* Test write (careful not to overwrite critical data!) */
    printf("Testing write (LBA 1000, 1 block)...\n");
    uint8_t write_buf[512];
    for (int i = 0; i < 512; i++) {
        write_buf[i] = (uint8_t)(i & 0xFF);
    }

    ret = mtk_msdc_write(&msdc, 1000, 1, write_buf);
    if (ret < 0) {
        printf("FAILED: Write error %d\n", ret);
        return ret;
    }
    printf("OK: Written 512 bytes to LBA 1000\n");

    /* Verify write */
    printf("Verifying write...\n");
    uint8_t verify_buf[512];
    ret = mtk_msdc_read(&msdc, 1000, 1, verify_buf);
    if (ret < 0) {
        printf("FAILED: Verification read error %d\n", ret);
        return ret;
    }

    int match = 1;
    for (int i = 0; i < 512; i++) {
        if (write_buf[i] != verify_buf[i]) {
            match = 0;
            break;
        }
    }

    if (match) {
        printf("OK: Write verification passed\n\n");
    } else {
        printf("FAILED: Write verification failed\n\n");
        return -1;
    }

    /* Test random access */
    printf("Testing random access (100 reads)...\n");
    uint32_t test_lbas[] = {0, 100, 500, 1000, 5000, 10000, 50000, 100000};
    int num_tests = sizeof(test_lbas) / sizeof(test_lbas[0]);

    for (int i = 0; i < num_tests; i++) {
        ret = mtk_msdc_read(&msdc, test_lbas[i], 1, buf);
        if (ret < 0) {
            printf("FAILED: Random access read error at LBA %u\n", test_lbas[i]);
            return ret;
        }
    }
    printf("OK: Random access test passed\n\n");

    printf("=== eMMC Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
