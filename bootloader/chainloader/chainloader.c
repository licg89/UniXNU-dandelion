/*
 * Chainloader main implementation
 */

/* Basic type definitions for freestanding environment */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef uint64_t size_t;

/* UART base address (from device tree) */
#define UART_BASE 0x11002000

/* UART registers */
#define UART_THR 0x00
#define UART_LSR 0x14
#define UART_LSR_THRE (1 << 5)

/*
 * MTK MSDC eMMC Controller Registers
 * Base: 0x11230000
 */
#define MSDC_BASE               0x11230000

/* MSDC register offsets */
#define MSDC_CFG                0x00
#define MSDC_IOCON              0x04
#define MSDC_PS                 0x08
#define MSDC_INT                0x0C
#define MSDC_INTEN              0x10
#define MSDC_FIFO_CFG           0x14
#define MSDC_FIFO_ADDR          0x18
#define MSDC_TXDATA             0x1C
#define MSDC_RXDATA             0x20
#define MSDC_CMD                0x30
#define MSDC_CMDARG             0x34
#define MSDC_SDC_CFG            0x38
#define MSDC_SDC_STS            0x3C
#define MSDC_SDC_CMD            0x40
#define MSDC_SDC_ARG            0x44
#define MSDC_SDC_STS2           0x48
#define MSDC_PATCH_BIT0         0xB0
#define MSDC_PATCH_BIT1         0xB4
#define MSDC_PAD_CTRL0          0xE0
#define MSDC_PAD_CTRL1          0xE4
#define MSDC_PAD_CTRL2          0xE8
#define SDC_CFG                 0x38
#define SDC_CMD                 0x40
#define SDC_ARG                 0x44
#define SDC_STS                 0x3C
#define EMMC_IOCON              0xF0

/* MSDC status bits */
#define MSDC_INT_CMDRDY         (1 << 1)
#define MSDC_INT_XFER_COMPL     (1 << 2)
#define SDC_STS_CMDTS           (1 << 6)
#define SDC_STS_CMDDONE         (1 << 7)

/* eMMC commands */
#define MMC_GO_IDLE_STATE       0
#define MMC_SEND_OP_COND        1
#define MMC_ALL_SEND_CID        2
#define MMC_SET_RELATIVE_ADDR   3
#define MMC_SELECT_CARD         7
#define MMC_SEND_CSD            9
#define MMC_READ_SINGLE_BLOCK   17
#define MMC_READ_MULTIPLE_BLOCK 18
#define MMC_SET_BLOCKLEN        16

/* Block size */
#define MMC_BLOCK_SIZE          512
#define XNU_BLOCK_SIZE          4096

/* eMMC state */
static int emmc_initialized = 0;
static uint32_t emmc_rca = 0;

/*
 * Low-level MSDC register access
 */
static inline void msdc_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *base = (volatile uint32_t *)MSDC_BASE;
    base[reg >> 2] = val;
}

static inline uint32_t msdc_read(uint32_t reg)
{
    volatile uint32_t *base = (volatile uint32_t *)MSDC_BASE;
    return base[reg >> 2];
}

/*
 * Low-level UART output
 */
static inline void uart_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    base[reg >> 2] = val;
}

static inline uint32_t uart_read(uint32_t reg)
{
    volatile uint32_t *base = (volatile uint32_t *)UART_BASE;
    return base[reg >> 2];
}

/*
 * Print string via UART
 */
void uart_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            while (!(uart_read(UART_LSR) & UART_LSR_THRE));
            uart_write(UART_THR, '\r');
        }
        while (!(uart_read(UART_LSR) & UART_LSR_THRE));
        uart_write(UART_THR, *str++);
    }
}

/*
 * Print hex value
 */
void uart_put_hex(uint64_t val)
{
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (val >> i) & 0xF;
        while (!(uart_read(UART_LSR) & UART_LSR_THRE));
        uart_write(UART_THR, hex[digit]);
    }
}

/*
 * Load XNU kernel
 *
 * Strategy:
 * 1. Try to read from eMMC first
 * 2. If eMMC fails, assume kernel is already loaded in memory
 */
int load_xnu_kernel(void)
{
    uart_puts("[CHAIN] Loading XNU kernel...\n");

    /* Try eMMC first */
    if (!emmc_initialized) {
        int ret = emmc_init();
        if (ret < 0) {
            uart_puts("[CHAIN] WARNING: eMMC init failed, trying memory...\n");
        } else {
            emmc_initialized = 1;
        }
    }

    if (emmc_initialized) {
        int ret = load_xnu_from_emmc();
        if (ret == 0) {
            uart_puts("[CHAIN] Kernel loaded from eMMC successfully\n");
            return 0;
        }
        uart_puts("[CHAIN] WARNING: eMMC read failed, trying memory...\n");
    }

    /* Fallback: check if kernel is already in memory */
    volatile uint32_t *kernel = (volatile uint32_t *)XNU_KERNEL_BASE;
    if (kernel[0] == 0 || kernel[0] == 0xFFFFFFFF) {
        uart_puts("[CHAIN] ERROR: Kernel not found at ");
        uart_put_hex(XNU_KERNEL_BASE);
        uart_puts("\n");
        return -1;
    }

    uart_puts("[CHAIN] Kernel found in memory at ");
    uart_put_hex(XNU_KERNEL_BASE);
    uart_puts("\n");

    return 0;
}

/*
 * Load XNU kernel from eMMC boot partition
 */
int load_xnu_from_emmc(void)
{
    uart_puts("[CHAIN] Reading XNU from eMMC...\n");
    uart_puts("  Offset: ");
    uart_put_hex(XNU_BOOT_PART_OFFSET);
    uart_puts("\n");
    uart_puts("  Destination: ");
    uart_put_hex(XNU_KERNEL_BASE);
    uart_puts("\n");

    /* Read kernel from eMMC */
    int ret = emmc_read(XNU_BOOT_PART_OFFSET, (void *)XNU_KERNEL_BASE, XNU_BOOT_PART_SIZE);
    if (ret < 0) {
        uart_puts("[CHAIN] ERROR: Failed to read kernel from eMMC\n");
        return -1;
    }

    /* Verify kernel presence */
    volatile uint32_t *kernel = (volatile uint32_t *)XNU_KERNEL_BASE;
    if (kernel[0] == 0 || kernel[0] == 0xFFFFFFFF) {
        uart_puts("[CHAIN] ERROR: No valid kernel data at eMMC offset\n");
        return -1;
    }

    uart_puts("[CHAIN] Kernel read complete\n");

    /* Also load DTB if present */
    uart_puts("[CHAIN] Reading DTB from eMMC...\n");
    ret = emmc_read(DTB_PART_OFFSET, (void *)XNU_DTB_BASE, DTB_PART_SIZE);
    if (ret < 0) {
        uart_puts("[CHAIN] WARNING: Failed to read DTB from eMMC\n");
        /* Non-fatal, we can continue without DTB */
    } else {
        uart_puts("[CHAIN] DTB read complete\n");
    }

    return 0;
}

/*
 * Initialize eMMC controller
 */
int emmc_init(void)
{
    uart_puts("[CHAIN] Initializing eMMC controller...\n");

    /* Reset MSDC */
    msdc_write(MSDC_CFG, 0x00000000);

    /* Wait for reset */
    int timeout = 10000;
    while (timeout > 0) {
        if ((msdc_read(MSDC_CFG) & 0x1) == 0)
            break;
        timeout--;
    }

    if (timeout == 0) {
        uart_puts("[CHAIN] ERROR: MSDC reset timeout\n");
        return -1;
    }

    /* Configure MSDC for eMMC */
    msdc_write(MSDC_PAD_CTRL0, 0x00090000);  /* CLK pad control */
    msdc_write(MSDC_PAD_CTRL1, 0x00090000);  /* CMD pad control */
    msdc_write(MSDC_PAD_CTRL2, 0x00090000);  /* DAT pad control */

    /* Set bus width (8-bit for eMMC) */
    msdc_write(MSDC_IOCON, 0x00000007);

    /* Set clock divider */
    msdc_write(MSDC_CFG, 0x10000000);  /* Initial low frequency */

    /* Send CMD0 (GO_IDLE_STATE) */
    msdc_write(MSDC_CMD, (MMC_GO_IDLE_STATE << 16));
    msdc_write(MSDC_CMDARG, 0);

    /* Wait for command completion */
    timeout = 10000;
    while (timeout > 0) {
        uint32_t status = msdc_read(MSDC_INT);
        if (status & MSDC_INT_CMDRDY) {
            msdc_write(MSDC_INT, status);  /* Clear interrupt */
            break;
        }
        timeout--;
    }

    if (timeout == 0) {
        uart_puts("[CHAIN] WARNING: CMD0 timeout\n");
    }

    /* Send CMD1 (SEND_OP_COND) - wait for eMMC ready */
    for (int retry = 0; retry < 100; retry++) {
        msdc_write(MSDC_CMD, (MMC_SEND_OP_COND << 16));
        msdc_write(MSDC_CMDARG, 0x40300000);  /* Voltage 3.0-3.4V */

        timeout = 10000;
        while (timeout > 0) {
            uint32_t status = msdc_read(MSDC_INT);
            if (status & MSDC_INT_CMDRDY) {
                msdc_write(MSDC_INT, status);
                uint32_t resp = msdc_read(MSDC_CMDARG);
                if (resp & 0x80000000) {
                    /* eMMC ready */
                    uart_puts("[CHAIN] eMMC ready\n");
                    return 0;
                }
                break;
            }
            timeout--;
        }
    }

    uart_puts("[CHAIN] WARNING: eMMC init incomplete (will try anyway)\n");
    return 0;  /* Return success to allow fallback */
}

/*
 * Read data from eMMC
 */
int emmc_read(uint32_t offset, void *buf, uint32_t size)
{
    /* Convert offset to block number */
    uint32_t block = offset / MMC_BLOCK_SIZE;
    uint32_t block_count = (size + MMC_BLOCK_SIZE - 1) / MMC_BLOCK_SIZE;

    uart_puts("[CHAIN] eMMC read: block=");
    uart_put_hex(block);
    uart_puts(" count=");
    uart_put_hex(block_count);
    uart_puts("\n");

    /* For now, do a simple memory copy simulation */
    /* In a real implementation, this would use DMA */

    /* Use CMD17 (READ_SINGLE_BLOCK) or CMD18 (READ_MULTIPLE_BLOCK) */
    volatile uint8_t *dst = (volatile uint8_t *)buf;

    if (block_count == 1) {
        /* Single block read */
        msdc_write(MSDC_CMD, (MMC_READ_SINGLE_BLOCK << 16));
        msdc_write(MSDC_CMDARG, block);

        int timeout = 100000;
        while (timeout > 0) {
            uint32_t status = msdc_read(MSDC_INT);
            if (status & MSDC_INT_XFER_COMPL) {
                msdc_write(MSDC_INT, status);

                /* Read data from FIFO */
                for (int i = 0; i < MMC_BLOCK_SIZE / 4; i++) {
                    uint32_t data = msdc_read(MSDC_RXDATA);
                    dst[i * 4 + 0] = (data >> 0) & 0xFF;
                    dst[i * 4 + 1] = (data >> 8) & 0xFF;
                    dst[i * 4 + 2] = (data >> 16) & 0xFF;
                    dst[i * 4 + 3] = (data >> 24) & 0xFF;
                }
                return 0;
            }
            timeout--;
        }
    } else {
        /* Multiple block read */
        msdc_write(MSDC_CMD, (MMC_READ_MULTIPLE_BLOCK << 16));
        msdc_write(MSDC_CMDARG, block);

        /* Read blocks */
        for (uint32_t blk = 0; blk < block_count; blk++) {
            int timeout = 100000;
            while (timeout > 0) {
                uint32_t status = msdc_read(MSDC_INT);
                if (status & MSDC_INT_XFER_COMPL) {
                    msdc_write(MSDC_INT, status);

                    /* Read data from FIFO */
                    for (int i = 0; i < MMC_BLOCK_SIZE / 4; i++) {
                        uint32_t data = msdc_read(MSDC_RXDATA);
                        dst[i * 4 + 0] = (data >> 0) & 0xFF;
                        dst[i * 4 + 1] = (data >> 8) & 0xFF;
                        dst[i * 4 + 2] = (data >> 16) & 0xFF;
                        dst[i * 4 + 3] = (data >> 24) & 0xFF;
                    }

                    dst += MMC_BLOCK_SIZE;
                    break;
                }
                timeout--;
            }

            if (timeout == 0) {
                uart_puts("[CHAIN] WARNING: Multi-block read timeout\n");
                return -1;
            }
        }

        return 0;
    }

    uart_puts("[CHAIN] WARNING: eMMC read timeout\n");
    return -1;
}

/*
 * Read eMMC blocks (alternative interface)
 */
int emmc_read_blocks(uint32_t start_block, uint32_t count, void *buf)
{
    return emmc_read(start_block * MMC_BLOCK_SIZE, buf, count * MMC_BLOCK_SIZE);
}

/*
 * Setup boot parameters for XNU
 */
int setup_boot_params(void)
{
    uart_puts("[CHAIN] Setting up boot parameters...\n");

    /* Setup boot arguments */
    volatile char *bootargs = (volatile char *)XNU_BOOTARGS_BASE;
    const char *default_cmdline = "debug=0x8 loglevel=15 serial=0x11002000";

    /* Copy command line */
    int i = 0;
    while (default_cmdline[i] && i < 255) {
        bootargs[i] = default_cmdline[i];
        i++;
    }
    bootargs[i] = '\0';

    uart_puts("[CHAIN] Bootargs: ");
    uart_puts((const char *)bootargs);
    uart_puts("\n");

    /* Setup boot parameters structure */
    volatile xnu_boot_params_t *params = (volatile xnu_boot_params_t *)(XNU_BOOTARGS_BASE + 256);
    params->dtb_addr = XNU_DTB_BASE;
    params->kernel_entry = XNU_KERNEL_BASE;
    params->bootargs_addr = XNU_BOOTARGS_BASE;
    params->cpu_count = 8;  /* MT6765 has 8 cores */
    params->memory_size = 512 * 1024 * 1024;  /* 512MB */

    uart_puts("[CHAIN] Boot params:\n");
    uart_puts("  DTB: ");
    uart_put_hex(params->dtb_addr);
    uart_puts("\n");
    uart_puts("  Kernel: ");
    uart_put_hex(params->kernel_entry);
    uart_puts("\n");
    uart_puts("  CPUs: ");
    uart_put_hex(params->cpu_count);
    uart_puts("\n");

    return 0;
}

/*
 * Jump to XNU kernel
 */
void jump_to_xnu(uint64_t entry, uint64_t dtb, uint64_t bootargs)
{
    uart_puts("[CHAIN] Jumping to XNU at ");
    uart_put_hex(entry);
    uart_puts("\n");
    uart_puts("[CHAIN] DTB at ");
    uart_put_hex(dtb);
    uart_puts("\n");
    uart_puts("[CHAIN] Bootargs at ");
    uart_put_hex(bootargs);
    uart_puts("\n");
    uart_puts("[CHAIN] ========================================\n");
    uart_puts("[CHAIN] Starting XNU...\n");
    uart_puts("[CHAIN] ========================================\n");

    /* Disable interrupts */
    __asm__ volatile("msr daifset, #0xf");

    /* Flush caches */
    __asm__ volatile(
        "dsb ish\n"
        "ic iallu\n"
        "dsb ish\n"
        "isb\n"
    );

    /* Jump to kernel */
    /* x0 = DTB address, x1 = bootargs address (ARM64 convention) */
    typedef void (*kernel_entry_t)(uint64_t dtb, uint64_t bootargs);
    kernel_entry_t entry_func = (kernel_entry_t)entry;

    /* This should never return */
    entry_func(dtb, bootargs);

    /* If we get here, something went wrong */
    uart_puts("[CHAIN] ERROR: Kernel returned!\n");
    while (1) {
        __asm__ volatile("wfe");
    }
}

/*
 * Main chainloader entry point
 */
void chainloader_main(void)
{
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  XNU Chainloader for MT6765\n");
    uart_puts("  Loading iOS on Xiaomi Redmi 9A\n");
    uart_puts("========================================\n");
    uart_puts("\n");

    /* Step 1: Load XNU kernel */
    int ret = load_xnu_kernel();
    if (ret < 0) {
        uart_puts("[CHAIN] FATAL: Failed to load kernel\n");
        while (1) __asm__ volatile("wfe");
    }

    /* Step 2: Setup boot parameters */
    ret = setup_boot_params();
    if (ret < 0) {
        uart_puts("[CHAIN] FATAL: Failed to setup boot params\n");
        while (1) __asm__ volatile("wfe");
    }

    /* Step 3: Jump to XNU */
    jump_to_xnu(XNU_KERNEL_BASE, XNU_DTB_BASE, XNU_BOOTARGS_BASE);

    /* Should never reach here */
    while (1) __asm__ volatile("wfe");
}
