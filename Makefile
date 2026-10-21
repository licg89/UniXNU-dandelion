# Makefile for XNU on MT6765
#
# Build system for boot code, device tree, and tests

# Toolchain
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
DTC = dtc

# Directories
BOOT_DIR = patches/boot
MMU_DIR = patches/mmu
DTS_DIR = patches/dts
TEST_DIR = tests/qemu

# Output
BUILD_DIR = build
KERNEL = $(BUILD_DIR)/boot_mt6765.bin
DTB = $(BUILD_DIR)/mt6765-dandelion.dtb
KERNEL_ELF = $(BUILD_DIR)/boot_mt6765.elf

# Source files
BOOT_SRC = $(BOOT_DIR)/boot_mt6765.S
MMU_SRC = $(MMU_DIR)/mmu_init.c
EARLY_CON_SRC = $(BOOT_DIR)/early_console.c
DTS_SRC = $(DTS_DIR)/mt6765-dandelion.dts

# Flags
CFLAGS = -Wall -O2 -ffreestanding -nostdinc -nostdlib -nostartfiles
CFLAGS += -mcpu=cortex-a53 -march=armv8-a
CFLAGS += -isystem $(shell $(CC) -print-file-name=include)
CFLAGS += -Ipatches/platform -Ipatches/memory -Ipatches/boot -Ipatches/mmu -Ipatches/include
ASFLAGS = -mcpu=cortex-a53
LDFLAGS = -T patches/boot/linker.ld -nostdlib

# Default target
.PHONY: all
all: $(KERNEL) $(DTB)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile boot assembly (use CC so C preprocessor runs on .S file)
$(BUILD_DIR)/boot.o: $(BOOT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

# Compile MMU
$(BUILD_DIR)/mmu.o: $(MMU_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile early console
$(BUILD_DIR)/early_console.o: $(EARLY_CON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link kernel
$(KERNEL_ELF): $(BUILD_DIR)/boot.o $(BUILD_DIR)/mmu.o $(BUILD_DIR)/early_console.o
	$(LD) $(LDFLAGS) -o $@ $^

# Create binary
$(KERNEL): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# Build device tree
$(DTB): $(DTS_SRC) | $(BUILD_DIR)
	$(DTC) -I dts -O dtb -o $@ $<

# QEMU test
.PHONY: qemu
qemu: all
	$(TEST_DIR)/test_qemu.sh

# Debug with GDB
.PHONY: debug
debug: all
	qemu-system-aarch64 \
		-M virt -cpu cortex-a53 -smp 4 -m 4G \
		-kernel $(KERNEL) -dtb $(DTB) \
		-nographic -serial mon:stdio \
		-S -s

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Install QEMU (Ubuntu/Debian)
.PHONY: install-qemu
install-qemu:
	sudo apt-get install -y qemu-system-arm gcc-aarch64-linux-gnu device-tree-compiler

# Help
.PHONY: help
help:
	@echo "XNU on MT6765 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build kernel and DTB"
	@echo "  qemu      - Run in QEMU"
	@echo "  debug     - Debug with GDB"
	@echo "  clean     - Clean build files"
	@echo "  install-qemu - Install QEMU and tools"
	@echo ""
	@echo "Variables:"
	@echo "  CROSS_COMPILE - Cross-compiler prefix (default: aarch64-linux-gnu-)"
