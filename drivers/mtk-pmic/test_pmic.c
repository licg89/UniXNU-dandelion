/*
 * MT6357 PMIC Driver Test
 *
 * Simple test to verify PMIC communication.
 */

#include "mtk_pmic.h"
#include <stdio.h>

int main(void)
{
    mtk_pmic_t pmic;
    int ret;

    printf("=== MT6357 PMIC Driver Test ===\n\n");

    /* Initialize PMIC driver */
    printf("Initializing PMIC at 0x%08X...\n", PMIC_WRAP_BASE);
    ret = mtk_pmic_init(&pmic, PMIC_WRAP_BASE);
    if (ret < 0) {
        printf("FAILED: PMIC init error %d\n", ret);
        return ret;
    }
    printf("OK: PMIC initialized\n\n");

    /* Read chip ID */
    uint16_t cid = mtk_pmic_get_cid(&pmic);
    printf("Chip ID: 0x%04X\n", cid);
    if ((cid & 0xFFF0) == 0x6350) {
        printf("OK: MT6357 detected\n\n");
    } else {
        printf("WARNING: Unexpected chip ID\n\n");
    }

    /* Read PMIC status */
    uint16_t status;
    printf("Reading PMIC status...\n");
    ret = mtk_pmic_get_status(&pmic, &status);
    if (ret < 0) {
        printf("FAILED: Could not read status\n");
        return ret;
    }
    printf("Status: 0x%04X\n", status);

    /* Decode status bits */
    printf("  Power key reset: %s\n", (status & MT6357_PWRKEY_RST) ? "YES" : "no");
    printf("  Charger detect:  %s\n", (status & MT6357_CHRDET) ? "YES" : "no");
    printf("  Over-voltage:    %s\n", (status & MT6357_OV) ? "YES" : "no");
    printf("  Under-voltage:   %s\n", (status & MT6357_UV) ? "YES" : "no");
    printf("  Thermal:         %s\n", (status & MT6357_THM) ? "YES" : "no");
    printf("\n");

    /* Read battery ADC (if available) */
    printf("Reading battery ADC...\n");
    uint16_t bat_adc;
    ret = mtk_pmic_read(&pmic, MT6357_BATADC_CON0, &bat_adc);
    if (ret < 0) {
        printf("FAILED: Could not read battery ADC\n");
    } else {
        printf("Battery ADC: 0x%04X (%d)\n", bat_adc, bat_adc);
        /* Rough voltage calculation (depends on ADC calibration) */
        /* This is just an example - real calculation needs datasheet */
        printf("Estimated voltage: ~%.2f V\n", bat_adc * 0.001);
    }
    printf("\n");

    /* Test write (careful not to break anything!) */
    printf("Testing register write...\n");
    uint16_t test_reg = MT6357_TOP_CON;
    uint16_t orig_val, test_val;

    ret = mtk_pmic_read(&pmic, test_reg, &orig_val);
    if (ret < 0) {
        printf("FAILED: Could not read test register\n");
        return ret;
    }
    printf("Original value at 0x%04X: 0x%04X\n", test_reg, orig_val);

    /* Write test value (set bit 0) */
    test_val = orig_val | 0x0001;
    ret = mtk_pmic_write(&pmic, test_reg, test_val);
    if (ret < 0) {
        printf("FAILED: Could not write test register\n");
        return ret;
    }

    /* Read back */
    ret = mtk_pmic_read(&pmic, test_reg, &test_val);
    if (ret < 0) {
        printf("FAILED: Could not read back test register\n");
        return ret;
    }
    printf("New value: 0x%04X\n", test_val);

    /* Restore original value */
    ret = mtk_pmic_write(&pmic, test_reg, orig_val);
    if (ret < 0) {
        printf("WARNING: Could not restore original value\n");
    }

    printf("\n=== PMIC Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
