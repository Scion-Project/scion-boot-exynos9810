# scion-boot-starlte — first-stage skeleton
PLATFORM ?= qemu

CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy

ifeq ($(PLATFORM),qemu)
  PLAT_DEF := -DPLATFORM_QEMU
else ifeq ($(PLATFORM),starlte)
  PLAT_DEF := -DPLATFORM_STARLTE
else
  $(error PLATFORM must be qemu or starlte)
endif

CFLAGS := -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only \
          -mstrict-align -fno-stack-protector -Wall -Wextra -O2 \
          -Iinclude -Isrc -Isrc/lib -Isrc/platform $(PLAT_DEF)
LDFLAGS := -Wl,-T,linker/stage1.ld -Wl,--build-id=none

SRCS := src/start.S src/main.c src/stage2_stub.c \
        src/platform/uart.c src/platform/clock.c src/platform/dram.c \
        src/psci/psci.c src/lib/klib.c

BUILD := build/$(PLATFORM)
ELF   := $(BUILD)/scion-boot.elf
BIN   := $(BUILD)/scion-boot.bin

all: $(BIN)

$(ELF): $(SRCS) linker/stage1.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(BUILD):
	mkdir -p $(BUILD)

qemu: $(ELF)
	qemu-system-aarch64 -M virt -cpu cortex-a53 -m 2G -nographic \
	  -kernel $(ELF)

clean:
	rm -rf build

.PHONY: all qemu clean
