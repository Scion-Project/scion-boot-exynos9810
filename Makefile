# scion-boot-exynos9810 — first-stage bootloader
PLATFORM ?= qemu

# Auto-detect GNU cross toolchain or fall back to LLVM/Clang + lld
ifneq ($(shell command -v aarch64-none-elf-gcc 2>/dev/null),)
  CROSS   ?= aarch64-none-elf-
  CC      := $(CROSS)gcc
  OBJCOPY := $(CROSS)objcopy
  LDFLAGS := -Wl,-T,linker/stage1.ld -Wl,--build-id=none
else ifneq ($(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),)
  CROSS   ?= aarch64-linux-gnu-
  CC      := $(CROSS)gcc
  OBJCOPY := $(CROSS)objcopy
  LDFLAGS := -Wl,-T,linker/stage1.ld -Wl,--build-id=none
else
  LLVM_BIN := $(shell brew --prefix llvm 2>/dev/null)/bin
  CC       := $(if $(wildcard $(LLVM_BIN)/clang),$(LLVM_BIN)/clang,clang) --target=aarch64-none-elf
  OBJCOPY  := $(if $(wildcard $(LLVM_BIN)/llvm-objcopy),$(LLVM_BIN)/llvm-objcopy,llvm-objcopy)
  LDFLAGS  := -fuse-ld=lld -Wl,-T,linker/stage1.ld -Wl,--build-id=none
endif

ifeq ($(PLATFORM),qemu)
  PLAT_DEF := -DPLATFORM_QEMU
else ifeq ($(PLATFORM),starlte)
  PLAT_DEF := -DPLATFORM_STARLTE
else
  $(error PLATFORM must be qemu or starlte)
endif

ifneq ($(BOOT_SHELL),)
  PLAT_DEF += -DBOOT_SHELL=1
endif

CFLAGS := -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only \
          -mstrict-align -fno-stack-protector -Wall -Wextra -O2 \
          -Iinclude -Isrc -Isrc/lib -Isrc/platform $(PLAT_DEF)

SRCS := src/start.S \
        src/main.c \
        src/exceptions.c \
        src/loader.c \
        src/shell.c \
        src/stage2_stub.c \
        src/platform/uart.c \
        src/platform/clock.c \
        src/platform/dram.c \
        src/platform/timer.c \
        src/platform/wdt.c \
        src/platform/safety.c \
        src/psci/psci.c \
        src/lib/klib.c \
        src/lib/crypto.c

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
	qemu-system-aarch64 -M virt,secure=on,virtualization=on -cpu cortex-a57 -m 2G -nographic \
	  -kernel $(ELF)

clean:
	rm -rf build

.PHONY: all qemu qemu-el3 clean
