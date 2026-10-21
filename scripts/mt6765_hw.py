#!/usr/bin/env python3
"""
MT6765 Memory Map and Hardware Definitions for XNU

This module defines the memory map and hardware constants for the
MediaTek MT6765 (Helio G25) SoC used in Xiaomi Redmi 9A (dandelion).
"""

# Memory Map (from /proc/iomem)
MEMORY_MAP = {
    # System RAM
    'ram_base': 0x40000000,
    'ram_size': 0xFCC00000,  # ~4GB

    # Kernel window
    'kernel_base': 0x40080000,
    'kernel_size': 0x02000000,  # 32MB

    # Payload (ramdisk + DTB)
    'payload_base': 0x48000000,
    'payload_size': 0x08000000,  # 128MB

    # Reserved regions
    'reserved_regions': [
        (0x47880000, 0x478C5FFF, 'kernel_reserved'),
        (0x47C90000, 0x47C9FFFF, 'persistent_ram'),
    ]
}

# Peripheral Base Addresses (from /proc/iomem)
PERIPHERALS = {
    # System controllers
    'topckgen': 0x10000000,
    'scp_infra': 0x10001000,
    'toprgu': 0x10007000,
    'apmixed': 0x1000C000,
    'pwrap': 0x1000D000,  # PMIC wrapper

    # Memory controllers
    'emi': 0x10219000,
    'dramc': 0x1022A000,
    'gce_mbox': 0x10238000,

    # I2C controllers
    'i2c0': 0x11007000,
    'i2c1': 0x11008000,
    'i2c2': 0x11009000,
    'i2c3': 0x1100F000,  # Touch controller
    'i2c4': 0x11011000,
    'i2c5': 0x11016000,
    'i2c6': 0x1100D000,

    # SPI controllers
    'spi0': 0x1100A000,
    'spi1': 0x11010000,
    'spi2': 0x11012000,
    'spi3': 0x11013000,
    'spi4': 0x11014000,
    'spi5': 0x11015000,

    # UART
    'uart0': 0x11002000,
    'uart1': 0x11003000,

    # Display subsystem
    'smi_common': 0x14002000,
    'smi_larb0': 0x14003000,
    'smi_larb1': 0x17010000,
    'smi_larb2': 0x15021000,
    'smi_larb3': 0x1A002000,

    # Storage
    'msdc0': 0x11230000,  # eMMC

    # USB
    'usb0': 0x11200000,

    # SCP (System Control Processor)
    'scp': 0x10500000,
}

# Interrupt Map (from /proc/interrupts)
INTERRUPTS = {
    'arch_timer': 30,      # GICv3 PPI
    'mt_timer': 194,       # MTK system timer
    'pmic_irq': 161,       # MT6357 PMIC
    'touch_irq': 17,       # Novatek touch (EINT 0)
    'i2c3_irq': 226,       # I2C3 (touch bus)
    'msdc0_irq': 18,       # eMMC
    'uart0_irq': 219,      # UART0
    'usb0_irq': 64,        # USB0
}

# PMIC (MT6357) Registers
PMIC_MT6357 = {
    'base': 0x1000D000,  # PMIC wrapper base
    'regs': {
        'INT_CON': 0x0000,
        'INT_STATUS': 0x0004,
        'CHR_CON0': 0x0100,
        'CHR_CON1': 0x0104,
        'CHR_CON2': 0x0108,
        'VOLT_MODE': 0x0200,
        'TOP_CON': 0x0300,
    }
}

# Display (NT36525B) Configuration
DISPLAY_NT36525B = {
    'resolution': (720, 1600),
    'interface': 'DSI',
    'color_format': 'RGB888',
    'refresh_rate': 60,
    'dsi_lanes': 4,
    'dsi_clock': 500000000,  # 500 MHz
}

# Touch (Novatek) Configuration
TOUCH_NOVATEK = {
    'i2c_bus': 3,
    'i2c_addr': 0x5D,
    'irq_gpio': 0,  # EINT0
    'reset_gpio': None,
    'max_touches': 10,
}

# eMMC Configuration
EMMC_MTK = {
    'msdc_base': 0x11230000,
    'bus_width': 8,  # 8-bit mode
    'max_freq': 200000000,  # 200 MHz
    'caps': ['MMC_CAP_8_BIT_DATA', 'MMC_CAP_DDR'],
}

def get_memory_regions():
    """Return list of memory regions for XNU boot."""
    return [
        ('ram', MEMORY_MAP['ram_base'], MEMORY_MAP['ram_size']),
        ('kernel', MEMORY_MAP['kernel_base'], MEMORY_MAP['kernel_size']),
        ('payload', MEMORY_MAP['payload_base'], MEMORY_MAP['payload_size']),
    ]

def get_peripheral_bases():
    """Return dictionary of peripheral base addresses."""
    return PERIPHERALS.copy()

def get_interrupt_map():
    """Return dictionary of interrupt mappings."""
    return INTERRUPTS.copy()
