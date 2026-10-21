/*
 * Chainloader for XNU on MT6765
 *
 * This is a minimal bootloader that:
 * 1. Is loaded by LK (as Android boot.img)
 * 2. Loads XNU kernel from eMMC
 * 3. Sets up boot parameters
 * 4. Jumps to XNU
 */

#ifndef _CHAINLOADER_H_
#define _CHAINLOADER_H_

/* Basic type definitions for freestanding environment */
#ifndef _STDINT_H
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef uint64_t size_t;
#endif

/* Memory addresses */
#define CHAINLOADER_BASE        0x40000000  /* Where LK loads us */
#define CHAINLOADER_SIZE        0x100000    /* 1MB for chainloader */
#define XNU_KERNEL_BASE         0x40100000  /* Where we load XNU */
#define XNU_DTB_BASE            0x47000000  /* Device tree location */
#define XNU_BOOTARGS_BASE       0x40001000  /* Boot arguments */

/* eMMC boot partition layout */
#define XNU_BOOT_PART_OFFSET    0x4000000   /* 64MB offset in eMMC */
#define XNU_BOOT_PART_SIZE      0x4000000   /* 64MB for XNU kernel */
#define DTB_PART_OFFSET         0x8000000   /* 128MB offset for DTB */
#define DTB_PART_SIZE           0x100000    /* 1MB for DTB */

/* Boot image header (Android format) */
typedef struct {
    char magic[8];              /* "ANDROID!" */
    uint32_t kernel_size;
    uint32_t kernel_addr;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;
    uint32_t second_size;
    uint32_t second_addr;
    uint32_t tags_addr;
    uint32_t page_size;
    uint32_t unused;
    char name[16];
    char cmdline[512];
    uint32_t id[8];
} boot_img_hdr_t;

/* XNU boot parameters */
typedef struct {
    uint64_t dtb_addr;
    uint64_t kernel_entry;
    uint64_t bootargs_addr;
    uint32_t cpu_count;
    uint32_t memory_size;
} xnu_boot_params_t;

/* Function prototypes */
void chainloader_main(void);
int load_xnu_kernel(void);
int load_xnu_from_emmc(void);
int setup_boot_params(void);
void jump_to_xnu(uint64_t entry, uint64_t dtb, uint64_t bootargs);
void uart_puts(const char *str);
void uart_put_hex(uint64_t val);

/* eMMC functions */
int emmc_init(void);
int emmc_read(uint32_t offset, void *buf, uint32_t size);
int emmc_read_blocks(uint32_t start_block, uint32_t count, void *buf);

#endif /* _CHAINLOADER_H_ */
