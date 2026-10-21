/*
 * MTK GIC Driver Test
 */

#include "mtk_gic.h"
#include <stdio.h>

/* Test interrupt handler */
static int irq_count = 0;

void test_irq_handler(uint32_t irq, void *data)
{
    irq_count++;
    printf("  IRQ %u fired (count=%d)\n", irq, irq_count);
}

int main(void)
{
    mtk_gic_t gic;
    int ret;

    printf("=== MTK GIC Test ===\n\n");

    /* Initialize GIC */
    printf("Initializing GIC...\n");
    ret = mtk_gic_init(&gic);
    if (ret < 0) {
        printf("FAILED: GIC init error %d\n", ret);
        return ret;
    }
    printf("OK: GIC initialized\n\n");

    /* Test enable/disable */
    printf("Testing interrupt enable/disable...\n");
    ret = mtk_gic_enable_irq(&gic, GIC_IRQ_UART0);
    if (ret < 0) {
        printf("FAILED: Enable IRQ error %d\n", ret);
        return ret;
    }
    printf("OK: IRQ %u enabled\n", GIC_IRQ_UART0);

    ret = mtk_gic_disable_irq(&gic, GIC_IRQ_UART0);
    if (ret < 0) {
        printf("FAILED: Disable IRQ error %d\n", ret);
        return ret;
    }
    printf("OK: IRQ %u disabled\n\n", GIC_IRQ_UART0);

    /* Test priority setting */
    printf("Testing priority setting...\n");
    ret = mtk_gic_set_priority(&gic, GIC_IRQ_TOUCH, GIC_PRIORITY_HIGH);
    if (ret < 0) {
        printf("FAILED: Set priority error %d\n", ret);
        return ret;
    }
    printf("OK: IRQ %u priority set to %u\n\n", GIC_IRQ_TOUCH, GIC_PRIORITY_HIGH);

    /* Test target CPU setting */
    printf("Testing target CPU setting...\n");
    ret = mtk_gic_set_target(&gic, GIC_IRQ_TIMER0, 0);
    if (ret < 0) {
        printf("FAILED: Set target error %d\n", ret);
        return ret;
    }
    printf("OK: IRQ %u target set to CPU 0\n\n", GIC_IRQ_TIMER0);

    /* Test handler registration */
    printf("Testing handler registration...\n");
    ret = mtk_gic_register_handler(&gic, GIC_IRQ_TOUCH, test_irq_handler, NULL);
    if (ret < 0) {
        printf("FAILED: Register handler error %d\n", ret);
        return ret;
    }
    printf("OK: Handler registered for IRQ %u\n\n", GIC_IRQ_TOUCH);

    /* Test SGI */
    printf("Testing software generated interrupt...\n");
    ret = mtk_gic_send_sgi(&gic, 0, 0);
    if (ret < 0) {
        printf("FAILED: Send SGI error %d\n", ret);
        return ret;
    }
    printf("OK: SGI 0 sent to CPU 0\n\n");

    /* Test multiple interrupts */
    printf("Testing multiple interrupt configuration...\n");
    uint32_t test_irqs[] = {
        GIC_IRQ_TIMER0, GIC_IRQ_UART0, GIC_IRQ_I2C3,
        GIC_IRQ_MSDC0, GIC_IRQ_PMIC, GIC_IRQ_TOUCH
    };
    int num_irqs = sizeof(test_irqs) / sizeof(test_irqs[0]);

    for (int i = 0; i < num_irqs; i++) {
        ret = mtk_gic_enable_irq(&gic, test_irqs[i]);
        if (ret < 0) {
            printf("FAILED: Enable IRQ %u\n", test_irqs[i]);
            return ret;
        }
        printf("  Enabled IRQ %u\n", test_irqs[i]);
    }
    printf("OK: Multiple interrupts configured\n\n");

    /* Test handler simulation */
    printf("Testing interrupt handling simulation...\n");
    printf("Simulating IRQ %u...\n", GIC_IRQ_TOUCH);
    test_irq_handler(GIC_IRQ_TOUCH, NULL);
    printf("Simulated IRQ count: %d\n\n", irq_count);

    printf("=== GIC Test Complete ===\n");
    printf("All tests passed!\n");

    return 0;
}
