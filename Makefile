# scion-boot-exynos9810 — first-stage bootloader
PLATFORM ?= qemu

# Auto-detect GNU cross toolchain or fall back to LLVM/Clang + lld
ifneq ($(shell command -v aarch64-none-elf-gcc 2>/dev/null),)
  CROSS   ?= aarch64-none-elf-
  CC      := $(CROSS)gcc
  OBJCOPY := $(CROSS)objcopy
  READELF := $(CROSS)readelf
  NM      := $(CROSS)nm
  LDFLAGS := -Wl,-T,linker/stage1.ld -Wl,--build-id=none
else ifneq ($(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),)
  CROSS   ?= aarch64-linux-gnu-
  CC      := $(CROSS)gcc
  OBJCOPY := $(CROSS)objcopy
  READELF := $(CROSS)readelf
  NM      := $(CROSS)nm
  LDFLAGS := -Wl,-T,linker/stage1.ld -Wl,--build-id=none
else
  LLVM_BIN := $(shell brew --prefix llvm 2>/dev/null)/bin
  CC       := $(if $(wildcard $(LLVM_BIN)/clang),$(LLVM_BIN)/clang,clang) --target=aarch64-none-elf
  OBJCOPY  := $(if $(wildcard $(LLVM_BIN)/llvm-objcopy),$(LLVM_BIN)/llvm-objcopy,llvm-objcopy)
  READELF  := $(if $(wildcard $(LLVM_BIN)/llvm-readelf),$(LLVM_BIN)/llvm-readelf,llvm-readelf)
  NM       := $(if $(wildcard $(LLVM_BIN)/llvm-nm),$(LLVM_BIN)/llvm-nm,llvm-nm)
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
        src/xmodem.c \
        src/shell.c \
        src/stage2_stub.c \
        src/platform/uart.c \
        src/platform/clock.c \
        src/platform/dram.c \
        src/platform/display.c \
        src/platform/i2c.c \
        src/platform/timer.c \
        src/platform/wdt.c \
        src/platform/safety.c \
        src/psci/psci.c \
        src/lib/klib.c \
        src/lib/crypto.c

BUILD ?= build/$(PLATFORM)
ELF   := $(BUILD)/scion-boot.elf
BIN   := $(BUILD)/scion-boot.bin
SCN2  := $(BUILD)/scion-boot.scn2

all: $(BIN) $(SCN2)

$(ELF): $(SRCS) linker/stage1.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(SCN2): $(BIN) tools/mkstage2.py
	python3 tools/mkstage2.py -i $(BIN) -o $@ -n "scion-boot-$(PLATFORM)"

$(BUILD):
	mkdir -p $(BUILD)

qemu: $(ELF)
	qemu-system-aarch64 -M virt,secure=on,virtualization=on -cpu cortex-a57 -m 2G -nographic \
	  -kernel $(ELF)

clean:
	rm -rf build

# ---- second-stage loader (loader/, docs/STATUS.md) ----------------------------------------
# make PLATFORM=starlte loader     -> build/starlte/scion-loader.bin (+ .elf, .image-end)
# PINS=FILE: SHA-256 list (tools/mkpins.py) the loader will accept; LDR_AUTOBOOT_MS, LDR_TOGGLES.
# LDR_OS_TOKENS=0: pass each XNU entry's line exactly as packed (no scion_ldr=/scion_guard=; loader/main.c).
LDR_PLAT_SRC := loader/plat_$(PLATFORM).c
LDR_SRCS := loader/entry.S loader/main.c loader/ui.c loader/crash.c loader/console.c \
            loader/policy.c loader/xnu.c loader/mmu.c loader/fbcon.c $(LDR_PLAT_SRC) \
            src/lib/klib.c src/lib/crypto.c src/lib/verify.c
LDR_DIR  := $(BUILD)/loader
LDR_OBJS := $(patsubst %,$(LDR_DIR)/%.o,$(subst /,_,$(LDR_SRCS)))
LDR_ELF  := $(BUILD)/scion-loader.elf
LDR_BIN  := $(BUILD)/scion-loader.bin
LDR_LOAD := $(if $(filter starlte,$(PLATFORM)),0x80080000,0x40080000)
LDR_DEFS := $(if $(LDR_AUTOBOOT_MS),-DLDR_AUTOBOOT_MS=$(LDR_AUTOBOOT_MS)u) \
            $(if $(LDR_WDT_MS),-DLDR_WDT_MS=$(LDR_WDT_MS)u) \
            $(if $(LDR_TOGGLES),-DLDR_TOGGLES='"$(LDR_TOGGLES)"') \
            $(if $(LDR_OS_TOKENS),-DLDR_OS_TOKENS=$(LDR_OS_TOKENS))
LDR_CFLAGS := -ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only -mstrict-align \
              -fno-stack-protector -fno-pic -fno-pie -fno-jump-tables \
              -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 \
              -Iinclude -Isrc/lib -Isrc/platform -Iloader -I$(LDR_DIR) $(PLAT_DEF) $(LDR_DEFS)
LDR_CFLAGS += $(shell $(CC) -fno-tree-switch-conversion -E -x c /dev/null >/dev/null 2>&1 && echo -fno-tree-switch-conversion)
LDR_LD   := $(if $(findstring lld,$(LDFLAGS)),-fuse-ld=lld,)

loader: $(LDR_BIN)

$(LDR_DIR)/pins.h: $(PINS) tools/mkpins.py | $(LDR_DIR)
	python3 tools/mkpins.py $(PINS) -o $@

$(LDR_DIR):
	mkdir -p $@

define ldr_obj
$(LDR_DIR)/$(subst /,_,$(1)).o: $(1) $(LDR_DIR)/pins.h $(wildcard loader/*.h) include/boot.h src/lib/verify.h | $(LDR_DIR)
	$$(CC) $$(LDR_CFLAGS) -c $(1) -o $$@
endef
$(foreach s,$(LDR_SRCS),$(eval $(call ldr_obj,$(s))))

# A pointer in static data is an absolute relocation: it would break a loader that S-Boot may
# place anywhere. tools/loader-relocs.sh fails the build on one.
$(LDR_ELF): $(LDR_OBJS) loader/loader.ld tools/loader-relocs.sh
	sh tools/loader-relocs.sh "$(READELF)" $(LDR_OBJS)
	$(CC) -nostdlib -static -Wl,-T,loader/loader.ld -Wl,--build-id=none \
	  -Wl,--defsym=LOAD_ADDR=$(LDR_LOAD) $(LDR_LD) $(LDR_OBJS) -o $@

$(LDR_BIN): $(LDR_ELF) tools/loader-end.sh
	$(OBJCOPY) -O binary $< $@
	sh tools/loader-end.sh "$(NM)" $< > $(BUILD)/scion-loader.image-end
	@echo "loader: $@ ($(PLATFORM)), image end $$(cat $(BUILD)/scion-loader.image-end)"

# ---- tests -------------------------------------------------------------------------------
# make host-test        the codecs (CRC32, SHA-256, SCN2, guard, command line, SWEETXNU plan) on
#                       the host's own compiler: tools/host-test.sh
# make qemu-test        the loader on QEMU virt: tools/qemu-test.py (menu, verified start, a
#                       tampered image refused, pinned digests, guard, watchdog, crash, re-entry)
host-test:
	sh tools/host-test.sh

QT := build/qemu-test
FAKE_CFLAGS := -ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only -mstrict-align \
               -fno-stack-protector -fno-pic -fno-pie -O2 -Wall -Wextra -nostdlib -static \
               -Wl,-T,tests/qemu/fake.ld -Wl,--build-id=none $(LDR_LD)

$(QT)/fakexnu.elf $(QT)/chain.elf: tests/qemu/fake_start.S tests/qemu/fake_main.c tests/qemu/fake.ld
	mkdir -p $(QT)
	$(CC) $(FAKE_CFLAGS) tests/qemu/fake_start.S tests/qemu/fake_main.c -o $(QT)/fakexnu.elf
	$(CC) $(FAKE_CFLAGS) -DCHAIN_IMAGE tests/qemu/fake_start.S tests/qemu/fake_main.c -o $(QT)/chain.elf

$(QT)/fakes.stamp: $(QT)/fakexnu.elf $(QT)/chain.elf tests/qemu/mkfakexnu.py tools/loader-end.sh
	$(OBJCOPY) -O binary $(QT)/fakexnu.elf $(QT)/fakexnu.bin
	$(OBJCOPY) -O binary $(QT)/chain.elf $(QT)/chain.bin
	sh tools/loader-end.sh "$(NM)" $(QT)/fakexnu.elf _mem_end > $(QT)/fakexnu.memsize
	python3 tests/qemu/mkfakexnu.py $(QT)/fakexnu.bin $$(cat $(QT)/fakexnu.memsize) \
	  $(QT)/ios27.payload --cmdline "-v serial=3 test=ios27"
	python3 tests/qemu/mkfakexnu.py $(QT)/fakexnu.bin $$(cat $(QT)/fakexnu.memsize) \
	  $(QT)/ios16.payload --cmdline "-v test=ios16"
	sha256sum $(QT)/ios27.payload > $(QT)/pins.txt
	touch $@

qemu-test: $(QT)/fakes.stamp
	$(MAKE) PLATFORM=qemu BUILD=$(QT)/plain LDR_AUTOBOOT_MS=1500 LDR_WDT_MS=3000 loader
	$(MAKE) PLATFORM=qemu BUILD=$(QT)/pinned LDR_AUTOBOOT_MS=1500 LDR_WDT_MS=3000 PINS=$(QT)/pins.txt loader
	python3 tools/qemu-test.py --dir $(QT)


.PHONY: all qemu qemu-el3 clean loader host-test qemu-test
