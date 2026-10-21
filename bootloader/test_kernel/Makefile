# Makefile for Test Kernel

# Toolchain
CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
AS := $(CROSS_COMPILE)as
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# Flags
CFLAGS := -Wall -O2 -nostdlib -nostdinc -ffreestanding
CFLAGS += -mcpu=cortex-a53 -mgeneral-regs-only
CFLAGS += -I.
ASFLAGS := -mcpu=cortex-a53
LDFLAGS := -T link.ld -nostdlib

# Target
TARGET := test_kernel

# Sources
C_SRCS := kernel.c
ASM_SRCS := start.s
OBJS := $(ASM_SRCS:.s=.o) $(C_SRCS:.c=.o)

all: $(TARGET).bin $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "========================================"
	@echo "Test kernel built successfully!"
	@echo "Binary: $(TARGET).bin"
	@echo "========================================"
	@ls -lh $(TARGET).bin

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).bin $(TARGET).elf

disasm: $(TARGET).elf
	$(OBJDUMP) -d $< > $(TARGET).dis

info: $(TARGET).elf
	@echo "========================================"
	@echo "Test Kernel Information:"
	@echo "========================================"
	@$(OBJDUMP) -h $< | grep -E "Idx|text|data|bss"
	@echo "========================================"
	@size $<

# Create bootable image
bootimg: $(TARGET).bin
	@echo "Creating test kernel boot image..."
	@echo "This will be loaded by the chainloader"
	@python3 ../../scripts/mkbootimg.py $(TARGET).bin test_kernel.img

.PHONY: all clean disasm info bootimg
