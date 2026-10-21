/*
 * MT6357 PMIC Driver for XNU
 *
 * Copyright (c) 2026 UniXNU Project
 * Based on Linux kernel MT6397 driver
 *
 * This driver provides basic PMIC access for the MediaTek MT6357
 * power management IC used in Xiaomi Redmi 9A (dandelion).
 */

#ifndef _MTK_PMIC_H_
#define _MTK_PMIC_H_

#include <stdint.h>
#include <stdbool.h>

/* PMIC Wrapper base address */
#define PMIC_WRAP_BASE          0x1000D000

/* PMIC Wrapper registers */
#define PWRAP_WRAPPER_LO_WCLR   0x0010
#define PWRAP_WRAPPER_LO_WFIFO  0x0014
#define PWRAP_WRAPPER_LO_RFIFO  0x0018
#define PWRAP_INIT_DONE0        0x009C
#define PWRAP_STAUPD_CTRL       0x00C4
#define PWRAP_INT_EN            0x00A4
#define PWRAP_INT_FLG           0x00A8
#define PWRAP_INT_CLR           0x00AC

/* PMIC read/write commands via PMIC wrapper */
#define PMIC_CMD_READ           0x0000
#define PMIC_CMD_WRITE          0x8000
#define PMIC_REG_MASK           0x7FFF
#define PMIC_DATA_MASK          0xFFFF

/* MT6357 key registers */
#define MT6357_CID               0x0000  /* Chip ID */
#define MT6357_SWCHRSTATUS      0x0314  /* Charger status */
#define MT6357_STRUP_CON0       0x0004  /* Startup control */
#define MT6357_STRUP_CON1       0x0006
#define MT6357_TOP_CON          0x0008
#define MT6357_INT_CON0         0x0100  /* Interrupt control */
#define MT6357_INT_STATUS0      0x0110  /* Interrupt status */
#define MT6357_OCSTATUS0        0x0150  /* Over-current status */
#define MT6357_BATADC_CON0      0x0300  /* Battery ADC */
#define MT6357_BATADC_CON1      0x0302
#define MT6357_BATADC_CON2      0x0304
#define MT6357_BATADC_CON3      0x0306
#define MT6357_BATADC_CON4      0x0308
#define MT6357_BATADC_CON5      0x030A
#define MT6357_BATADC_CON6      0x030C
#define MT6357_BATADC_CON7      0x030E
#define MT6357_BATADC_CON8      0x0310
#define MT6357_BATADC_CON9      0x0312

/* BUCK converters */
#define MT6357_VPROC_CON0       0x0400  /* VPROC (CPU core) */
#define MT6357_VPROC_CON1       0x0402
#define MT6357_VPROC_CON2       0x0404
#define MT6357_VPROC_ON_CON0    0x0406
#define MT6357_VPROC_OFF_CON0   0x0408

#define MT6357_VCORE_CON0       0x0420  /* VCORE */
#define MT6357_VCORE_CON1       0x0422
#define MT6357_VCORE_CON2       0x0424

#define MT6357_VMODEM_CON0      0x0440  /* VMODEM */
#define MT6357_VMODEM_CON1      0x0442

#define MT6357_VS1_CON0         0x0460  /* VS1 */
#define MT6357_VS1_CON1         0x0462

#define MT6357_VS2_CON0         0x0480  /* VS2 */
#define MT6357_VS2_CON1         0x0482

/* LDO regulators */
#define MT6357_VDRAM_CON0       0x0500  /* VDRAM */
#define MT6357_VDRAM_CON1       0x0502

#define MT6357_VDIG18_CON0      0x0520  /* VDIG18 */
#define MT6357_VDIG18_CON1      0x0522

#define MT6357_VUSB_CON0        0x0540  /* VUSB */
#define MT6357_VUSB_CON1        0x0542

#define MT6357_VIO28_CON0       0x0560  /* VIO28 */
#define MT6357_VIO28_CON1       0x0562

#define MT6357_VIO18_CON0       0x0580  /* VIO18 */
#define MT6357_VIO18_CON1       0x0582

#define MT6357_VCAMD_CON0       0x05A0  /* VCAMD */
#define MT6357_VCAMD_CON1       0x05A2

/* PMIC status bits */
#define MT6357_PWRKEY_RST       (1 << 0)
#define MT6357_CHRDET           (1 << 1)
#define MT6357_OV               (1 << 2)
#define MT6357_UV               (1 << 3)
#define MT6357_THM              (1 << 4)
#define MT6357_TRIMBUF_UPD      (1 << 5)
#define MT6357_RTC              (1 << 6)

/* Driver context */
typedef struct {
    volatile uint32_t *wrap_base;  /* PMIC wrapper base */
    uint16_t cid;                   /* Chip ID */
    bool initialized;
} mtk_pmic_t;

/* Function prototypes */
int mtk_pmic_init(mtk_pmic_t *pmic, uint32_t base_addr);
int mtk_pmic_read(mtk_pmic_t *pmic, uint16_t reg, uint16_t *val);
int mtk_pmic_write(mtk_pmic_t *pmic, uint16_t reg, uint16_t val);
int mtk_pmic_set_bits(mtk_pmic_t *pmic, uint16_t reg, uint16_t bits);
int mtk_pmic_clear_bits(mtk_pmic_t *pmic, uint16_t reg, uint16_t bits);
uint16_t mtk_pmic_get_cid(mtk_pmic_t *pmic);
int mtk_pmic_get_status(mtk_pmic_t *pmic, uint16_t *status);

#endif /* _MTK_PMIC_H_ */
