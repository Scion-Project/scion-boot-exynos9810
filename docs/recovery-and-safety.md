# Brick-Proof Design & Hardware Recovery Guide

`scion-boot-exynos9810` is engineered so that it **cannot permanently brick or damage the phone**, and **always provides a deterministic path back to USB recovery**.

## 1. Hardware Write-Protection (Zero Non-Volatile Writes)

Stage 1 operates purely in volatile SRAM/DRAM and MMIO control registers:
- **No Flash Modification**: It never issues write or erase commands to the UFS Host Controller (`0x11120000`).
- **No OTP / eFuse Modification**: It never writes to the Exynos OTP / eFuse controller (`0x10000000`).
- **BootROM IRAM Preservation**: It protects the Exynos 9810 IROM (`0x00000000..0x00020000`) and BootROM IRAM context/function table (`0x02020000..0x02021400`), ensuring `PTR_USB_REINIT` (`0x020200E0`) and `PTR_USB_RECEIVE` (`0x020200DC`) remain intact.
- **Console Write-Guard**: Interactive commands (`mw32`, `mw64`, `memtest`) validate target addresses via `platform_addr_is_write_protected()` and refuse writes to protected regions.

## 2. Four Independent Recovery Paths

### Path A: Physical Hardware Button Combo at Boot (`GPA0` @ `0x14050024`)
Sampled immediately at startup before clock or DRAM initialization:
- **Hold `Volume Down + Bixby`** (`GPA0_4` + `GPA0_6`): Immediately aborts boot and enters **Exynos BootROM USB Download Mode (`usbdl`, USB ID `04e8:1234`)**.
- **Hold `Volume Up`** (`GPA0_3`): Sets `SCION_BF_RECOVERY | SCION_BF_SAFE_MODE` and drops directly into the Stage-1 UART recovery shell.

### Path B: Automatic Boot-Loop Deadman Counter (`PMU_INFORM1` @ `0x14060804`)
- On every boot attempt, Stage 1 increments a tagged counter (`0x53434200 | count`) in the PMU Alive register `PMU_INFORM1` (which survives warm resets and watchdog resets) and arms a 15-second hardware watchdog (`WDT_CL0` @ `0x10050000`) before jumping to Stage 2.
- Once Stage 2 (or the OS) confirms a healthy handoff, it clears `PMU_INFORM1`.
- If **3 consecutive boot attempts** fail or hang, Stage 1 automatically halts normal boot, clears the counter, and enters Safe Recovery Mode.

### Path C: Exception / Panic / Init-Failure Auto-Fallback to `usbdl`
- Any unhandled CPU exception (`c_exception_handler`), DRAM initialization failure, or Stage-2 hash verification failure prints a full diagnostic dump over UART and automatically invokes `platform_enter_usbdl()`.

### Path D: Interactive Console Recovery Commands
From the `scion-stage1>` UART prompt:
- `safety` — Display write-protected address ranges, `GPA0` button states, and boot-loop counter.
- `usbdl` (or `dnw`) — Immediately enter Exynos BootROM USB Download Mode (`04e8:1234`).
- `poweroff` — Cleanly release `PS_HOLD` (`0x1406330C` <- `0x5200`) to power down PMIC rails safely.

## 3. Recovering via USB (`04e8:1234` DNW Mode)

When the device is in `usbdl` mode (`ID 04e8:1234 Samsung Electronics Co., Ltd`), you can recover or reflash stock firmware using:

- **[halal-beef/hubble](https://github.com/halal-beef/hubble)** — USB recovery tool for Exynos devices (`starlte` / Exynos 9810 supported):
  ```sh
  python3 hubble.py -b <PATH_TO_STOCK_BL_TAR>
  ```
  Hubble streams the split `sboot.bin` sections (`fwbl1.bin`, `bl31.bin`, `bl2.bin`, `u-boot.bin`, `el3_mon.bin`) over DNW (`04e8:1234`) to bring the device directly into Odin Download Mode so stock firmware can be reflashed.
- **[halal-beef/houston-pub](https://github.com/halal-beef/houston-pub)** — Exynos BootROM USB tool for uploading and testing payloads in `usbdl` mode.

## 4. Credits

- **Umer Uddin ([@halal-beef](https://github.com/halal-beef))** (`<umer.uddin@mentallysanemainliners.org>`) — Original creator and author of [Hubble](https://github.com/halal-beef/hubble) and [Houston](https://github.com/halal-beef/houston-pub).
- **[@Robotix22](https://github.com/Robotix22)** — Creator of [open-mini-bl1](https://github.com/Robotix22/open-mini-bl1) and Exynos 9810 (`starlte`) BootROM/IRAM documentation & Hubble/Houston support.
- **[@VDavid003](https://github.com/VDavid003)** — Creator of [exynos-usbdl](https://github.com/VDavid003/exynos-usbdl) and Exynos recovery research contributor.
