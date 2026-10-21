# Makefile for XNU Chainloader on MT6765

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
TARGET := chainloader

# Sources
C_SRCS := chainloader.c
ASM_SRCS := chainloader_asm.s
OBJS := $(ASM_SRCS:.s=.o) $(C_SRCS:.c=.o)

all: $(TARGET).bin $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "========================================"
	@echo "Chainloader built successfully!"
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
	@echo "Chainloader Information:"
	@echo "========================================"
	@$(OBJDUMP) -h $< | grep -E "Idx|text|data|bss"
	@echo "========================================"
	@size $<

# Create Android boot.img
bootimg: $(TARGET).bin
	@echo "Creating Android boot.img..."
	@echo "Note: You need mkbootimg tool installed"
	@echo "Usage:"
	@echo "  mkbootimg --kernel $(TARGET).bin --base 0x40000000 \\"
	@echo "    --cmdline \"xnu_chainloader\" --output xnu_chainloader.img"

# Flash to device
flash: $(TARGET).bin
	@echo "Flashing chainloader to device..."
	@echo "Make sure device is in fastboot mode"
	@echo "Run: fastboot flash boot xnu_chainloader.img"

.PHONY: all clean disasm info bootimg flash
