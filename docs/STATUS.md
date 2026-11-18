# Status and plan: one robust second-stage loader for the Galaxy S9 (18 November 2026)

This page says what the loader that starts iOS on the S9 does today, what this repository has that
it lacks, and the design that merges them: `loader/` in this repository, a second-stage loader that
Samsung's S-Boot starts from the BOOT partition. **Nothing in `loader/` has run on a phone yet.**
Everything below that says "tested" means tested in QEMU or on the host
([what is tested](#testing-without-a-phone)); [phone-test-plan.md](phone-test-plan.md) is the plan
for the first runs on the phone.

## Where this sits in the boot chain

```
BootROM -> Samsung BL1/BL2/EL3 monitor/TEE -> S-Boot (Samsung's, signed, untouched)
        -> BOOT partition's kernel slot, entered at EL1, MMU off, x0 = S-Boot's FDT
           = the second-stage loader (today xnuloader/sboot, tomorrow optionally loader/ here)
        -> XNU (iOS 27 / iOS 16.5), or another image the loader verified
```

Everything in `loader/` runs after Samsung's signed chain has handed control to the kernel image in
BOOT, at the exception level S-Boot leaves (EL1; Samsung's hypervisor and monitor stay above it), in
RAM only. It does not touch the BootROM, BL1/BL2, the TEE, the bootloader partitions, fuses, keys or
any storage, and it contains no exploit or signature-check bypass ([boundary.md](boundary.md)).

The rest of this repository (`src/`, the EL3 "stage 1" scaffold) is a separate, older idea: a first
stage that would run at EL3 in place of Samsung's early boot. On a retail S9 nothing ever runs that
code at EL3, and the loader does not use its EL3-only parts (below).

## What runs today: `xnuloader/sboot` in scion-kernel ("Starbite")

S-Boot starts `xnuloader/sboot/out/xnu_boot.Image`, which `tools/ios27/s9/make-boot-image.sh` packs
as BOOT's kernel with the iOS payload appended (`tools/ios27/starlte-pack-boot.py pack --append`).
About 30,000 lines, GPL-2.0-only (derived from ios-a5xelte's herolte shim). What it does, with the
places to read (paths in scion-kernel):

| Feature | Where | Notes |
|---|---|---|
| Finds the iOS payload | `xnu_boot.c` `boot_payload()` | the SWEETXNU container (`tools/ios27/herolte/mkpayload.py`) at the Image's 4 KiB-aligned `_image_end`, bounded by the header's `image_size`; falls back to S-Boot's initrd (`/chosen linux,initrd-*`); then bundles `SBUEFI!!`/`SBLINUX!`/`SBRESCUE` after it |
| Checks the payload | `xnu_boot.c` `payload_bad()` | magic, version, segment count, window bounds. **No hash**: a damaged payload is found only when XNU crashes |
| Starts XNU | `xnu_boot.c` `xnu_enter()`, `xnu_boot.S` | segments copied to `phys_base`, ADT after `kernel_end`, `boot_args` (Revision 2, Version 2) on the next page; EL1, MMU off, `x0` = boot_args; DECON trigger left free-running |
| Command line | `menu.c` `opt_load/opt_apply`, `menu7.c` | the payload's 607-byte line, edited in place by the menu's options (cpus, rw, verbose, touch, ...); a catalog entry's line appended |
| Menus | `menu.c`, `menu6.c`, `menu7.c` + `glass*.c` | v5 text, v6, v7 "Liquid Glass"; keys GPA0/GPA2 |
| OS catalog and store | `catalog.c`, `store.c`, `ufs.c`, `sd.c` | iOS, Linux, Android, Windows (UEFI), bare images from a store on UFS (USERDATA + 16 GiB) or microSD; writes only its settings record |
| Recovery | `recovery.c` | hold Bixby: text menu (TWRP via PMU INFORM3 `0x12345674`, Download `0x12345671`, reboot, rescue mode via the `SBNEXT2!` mailbox at `0xfed17fe0`) |
| POST, EngineerMode, battery | `post.c`, `em.c`, `batt.c` | self test, service tool, MAX77705 readout |
| USB fastboot and console | `fastboot*.c`, `usb_*.c` | `docs/ios27/USB-FASTBOOT.md` |
| Failure | `xnu_boot.c` `fail()`, `xnu_boot.S fail:` | a message on the screen, then a request for Download mode (INFORM3 + SWRESET) |
| Boot-loop guard, watchdog, crash screen, re-entry | — | **none** (the woa UEFI keeps its own attempt counter; EngineerMode has vectors only while it runs) |

## What this repository had (`src/`, stage 1)

A clean, MIT-licensed scaffold: a UART and DECON0 framebuffer console, the SCN2 image header
(`include/boot.h`: CRC32 + SHA-256, host tool `tools/mkstage2.py`), CRC32/SHA-256 in plain C
(`src/lib/crypto.c`), an exception-vector crash dump, an interactive shell, a 3-strike boot-loop
counter and a watchdog fail-safe. Built for EL3 and tested in QEMU at EL3.

What the loader takes from it, and what it does not:

| Part | In the loader? | Why |
|---|---|---|
| SCN2 header, CRC32, SHA-256 | yes (`src/lib/verify.c` on top) | the integrity check the current loader lacks. The SHA-256 round-constant table had a typo patched at run time; fixed in the table, and now tested against Python's hashlib for 302 lengths |
| 8x8 font, DECON trigger fact | yes (`src/platform/font8x8.h`) | the screen is the S9's only console |
| `kprintf` (`src/lib/klib.c`) | yes | |
| Crash dump idea | rewritten for EL1 (`loader/crash.c`) | |
| 3-strike guard idea | rewritten (`loader/policy.c`) | stage 1 keeps its count in `PMU_INFORM1`, a register S-Boot itself reads; the loader keeps it in RAM that survives a warm reset |
| Watchdog fail-safe | not on the S9 yet | stage 1 clears the PMU's watchdog reset mask, a write the project has not made from EL1 (open question 2) |
| EL3 entry, EL3->EL2 drop, DRAM init, PSCI, `usbdl`/BootROM IRAM paths | **no** | they belong to a first stage at EL3; after S-Boot none applies, and BootROM/USB-download territory is out of this work's scope |

## The merged design: `loader/`

One loader, written from scratch for this repository (MIT; no code from sboot, whose facts it
re-states with their sources), that S-Boot starts exactly as it starts sboot today.

**BOOT's kernel slot.** `scion-loader.bin` (an arm64 Image, position independent: the build refuses
any absolute relocation) padded to its `_image_end`, then the images it may start, each an *SCN2
record*: one 4 KiB page holding the 112-byte `struct scion_stage2_hdr` (magic `SCN2`, kind, flags,
size, CRC32, SHA-256, name), then the payload, padded to 4 KiB. Kinds: `xnu` (a SWEETXNU payload,
unchanged from today) and `image` (an arm64 Image entered in place with `x0` = S-Boot's FDT; this is
how the current loader, with everything it does, stays one menu entry away). Flags: `default`, `safe`
(the guard's known-good entry). `tools/mkloaderimg.py` makes it; scion-kernel's
`make-boot-image.sh --loader-kind scion` calls it ([scion-kernel side](#scion-kernel-side)).

**Flow** (`loader/main.c`):

1. Identity map (`loader/mmu.c`: the loader and its records write-back cached, the rest of DRAM
   non-cacheable, registers device); without it hashing a 60 MiB payload over Device memory takes
   minutes. Text console on the framebuffer (and the UART where a board has one).
2. List the records by their headers only (cheap); a bad header ends the list there.
3. Read the boot-loop guard's record and make its plan.
4. Show the menu: the entries, *Reboot to recovery (TWRP)*, *Reboot to Download mode*, *Power off*;
   the selected iOS entry's command line below; a countdown to the default entry (5 s; Volume Up
   held at start: no countdown). Volume Up/Down move, Power starts, Bixby edits the line.
5. On start: CRC32 and SHA-256 of the whole payload, then the build's pinned digests if it has any.
   Any mismatch shows **NOT STARTED: CHECK FAILED** with the entry, the reason and both digests, and
   offers *Back to the menu* or *Reboot to recovery*: never a silent hang, never a jump into a
   damaged image. Then the SWEETXNU plan (`loader/xnu.c`: every segment, the ADT and boot_args
   inside the allowed RAM windows, nothing overlapping the payload), the copies, boot_args exactly as
   today, and `ldr_jump` (whole D-cache cleaned by set/way, MMU and caches off, `x1..x3` = 0).

**Command line.** Shown for the selected entry. With the buttons: a list of toggles (build-time
`LDR_TOGGLES`, default `-v serial=3 sweetwdog=60 wdt=-1`) plus *Restore the image's line*. With a
UART: a line editor. The edit applies to this start only; nothing is stored. The loader appends
`scion_ldr=<base>,<size>` (where the kernel slot lies, for a warm restart to put it back) and
`scion_guard=<pa>` (the guard record, for the OS to acknowledge a good boot) when the line has room.

**Trust (our chain, above S-Boot).** SHA-256 in the record proves the payload is the one that was
packed; it is integrity, not authenticity, because record and loader travel together in BOOT. Build
the loader with `PINS=list` (`tools/mkpins.py`, e.g. the `sha256sum` of the payloads the project
released) and it starts only those digests: a trust list of our own, compiled into the loader, so a
BOOT image whose payload was swapped is refused even if its record is self-consistent. Signatures
with a project key (Ed25519 over the record) are the next step (open question 4). None of this
changes, extends or touches the chip's fused root of trust or Samsung's signature checks: those stay
exactly as they are, and anything they refuse to start never reaches this loader.

**Boot-loop guard** (`loader/policy.c`, record in `include/boot.h`). A 32-byte record (`SCNG`,
strikes, last entry, flags, CRC32) in RAM that survives a warm reset; on the S9 the 32 bytes at
`0xfed17fc0` of the exynos-ss page, between the woa UEFI's latches and sboot's boot-next mailbox (to
be confirmed on the phone: phone test 3). Every automatic start adds a strike before the jump; a
start the user chose in the menu forgives earlier strikes; the OS acknowledges a good boot by
writing strikes = 0 (kernel side not written yet: open question 1). A cold power-on reads as no
strikes. At 3 strikes the known-good (`safe`) entry is started; if it was already tried, or there is
none, the menu's default becomes *Reboot to recovery* with a 10 s countdown. Reboot to recovery or
Download clears the record.

**Watchdog.** In QEMU the loader runs its own watchdog (EL1 virtual timer through the GIC, 30 s,
petted by the key loop): a hang inside the loader resets the board and counts a strike. On the S9
the loader has no hardware watchdog of its own yet (open question 2). After a warm restart it pets
the cluster-1 watchdog the OS armed (`SCION_RE_WDT_ARMED`) and stops it right before the next OS.

**Crash dump** (`loader/crash.c`, `loader/entry.S`). EL1 vectors: any exception in the loader shows
**LOADER CRASH** with ESR (decoded), ELR, FAR, SPSR, LR and x0..x7 on the screen, all registers on
the UART, counts a strike, and reboots to recovery on a key or after 60 s.

**Re-entry** (`include/boot.h` `struct scion_reentry`, [handoff-contract.md](handoff-contract.md)).
Base + 0x40 is a second entry point: a running OS that put BOOT's kernel slot back where S-Boot had
it (XNU reuses that RAM) jumps there at EL1 with the MMU off and `x0` = a 728-byte record (reason,
flags, which entry, an optional new line, an FDT, a watchdog it left armed). The loader re-checks the
SHA-256 of whatever it starts, so a bad copy is refused like a damaged BOOT. Agreed with
scion-kernel's warm-restart work (`docs/ios27/WARM-RESTART.md` there); a cold boot whose line has
`sweetwarm=` keeps S-Boot's FDT at `0x91100000` for it (a 4 KiB header, then the FDT).

### Superset, not regression

The new loader is an **option**: `make-boot-image.sh` keeps building today's sboot image unless asked
(`--loader-kind scion`). Against today's loader:

| Today (sboot) | `loader/` |
|---|---|
| iOS payload from BOOT, its line, boot_args, EL1 hand-off | same, plus SHA-256 and a clear error screen; several iOS entries (27 and 16.5) |
| Menu options that edit the line | toggles + full UART editor; options are a build setting |
| Recovery menu (TWRP, Download, reboot) | the same choices in the main menu, plus the guard's automatic recovery |
| Store, Linux/Android/Windows, fastboot, EngineerMode, POST, menu v7 | **through sboot itself**: `--scion-chain-sboot` appends today's loader (with its payload) as an `image` entry, checked by SHA-256 and started as S-Boot would start it |
| Rescue mailbox (`SBNEXT2!`) | handled by sboot when chained; not read by `loader/` |
| — | boot-loop guard, crash screen, re-entry ABI, pinned digests, QEMU tests |

## Testing without a phone

* `make host-test` (`tools/host-test.sh`, `tests/host/codecs-test.c`, built with ASan/UBSan): CRC32
  and SHA-256 against Python for lengths 0..300 and 1,000,000; SCN2 header checks (every field and
  bound), damage (CRC) and change (SHA-256) detection, pinned digests; re-entry and guard record
  sealing; the guard's plan through 3 strikes, the safe entry, recovery, acknowledgement and user
  choice; command-line toggles and bounds; the menu cursor; the SWEETXNU plan against both S9 RAM
  windows and every malformed field.
* `make qemu-test` (`tools/qemu-test.py`, QEMU virt at EL1 booted with `-kernel` like S-Boot, test
  payloads in `tests/qemu/`): 13 scenarios: autoboot of the default iOS entry (boot_args, ADT, line,
  zeroed .bss checked by the fake kernel), menu choice of the chained Image (FDT in x0), line edit,
  a payload changed after packing (SHA-256) and one damaged (CRC) refused with the error screen and
  recovery chosen, pinned digests accepted and refused, the guard counting, tripping to the safe
  entry and then to recovery, the watchdog resetting a hung loader, the crash dump, and a full
  re-entry (fake XNU builds `struct scion_reentry`, jumps to base + 0x40, the loader re-verifies and
  starts entry 2 with the kept FDT and stops the "armed" watchdog).
* `tools/arch-check.py`: 800-line ceiling, plain-C codecs that the host test compiles, no pointer
  tables in the loader; the Makefile refuses absolute relocations.
* CI (`.github/workflows/ci.yml`): stage 1 and the loader for `PLATFORM=qemu` and `starlte`, the host
  tests, the QEMU tests, arch-check.

Not testable without the phone: the S9's keys, framebuffer and DECON trigger as seen from this
loader, S-Boot's actual load address and FDT, the retained page surviving a warm reset, the PMU
reboot paths from this loader, timing of SHA-256 over a real payload.

## scion-kernel side

`tools/ios27/s9/make-boot-image.sh --loader-kind scion --scion-boot DIR` (a built checkout of this
repository): BOOT's kernel slot = `scion-loader.bin` + the iOS 27 payload as the default entry
(+ `--scion-entry NAME:xnu:FILE[:safe]` for 16.5 or a known-good build, + `--scion-chain-sboot` for
today's loader). Default unchanged (`--loader-kind sboot`). `starlte-pack-boot.py` gets
`--image-size-from-file` so the header's `image_size` covers what is appended (default path
unchanged).

## Licences

`loader/`, `src/lib/verify.*`, `tools/mk*.py`, `tests/` and `tools/*test*`: MIT (this repository's
licence), written for this repository. Facts about the S9 (addresses, values, sequences) are taken
from scion-kernel's sboot and Samsung's device tree and re-stated with their source named; no code
from either. Nothing from Apple: the SWEETXNU layout is the project's own format and `boot_args` is
written from its field list.

## Open questions

1. **Guard acknowledgement in the kernel.** XNU must write strikes = 0 (and the CRC) at `scion_guard=`
   once launchd is up (the hang watchdog's "first system call" point is a natural place). Until then
   three automatic starts in a row without touching the menu trip the guard. Owner: kernel side.
2. **A hardware watchdog for the loader on the S9.** Arming `WDT_CLUSTER0`/`1` so it can reset the
   SoC needs the PMU reset-mask write (stage 1's `PMU_MASK_WDT_RESET`); the warm-restart work makes
   that write from the kernel. Should the loader do it too (owner's decision: it changes what the
   phone does when the loader hangs)?
3. **The retained slot** `0xfed17fc0..0xfed17fdf`: free in everything the project knows (woa latches
   end at `0xfed17fb4`, sboot's mailbox starts at `0xfed17fe0`) and in the exynos-ss page that
   survives warm resets; confirm on the phone.
4. **Signatures.** Ed25519 over the SCN2 header with a project key whose public half is compiled into
   the loader; the private key kept by the owner, never in a repository. Pinned digests cover the
   need until then.
5. **BOOT's size** (57.7 MB): two iOS payloads plus a chained sboot with its own copy of the payload
   may not fit; `starlte-pack-boot.py` refuses an image that does not.
6. **S-Boot's load address** is taken as `0x80080000` (template `text_offset` 0x80000) for listings
   only; the loader runs wherever it is put, but everything appended must end below S-Boot's initrd
   at `0x89000000` (mkloaderimg `--max-bytes`).

## Pull requests

1. this page + CI for what exists (stage 1 build and QEMU smoke test);
2. the verified load: SCN2 records, SHA-256 fix, pinned digests, the SWEETXNU plan, the error
   screen, the loader core, host tests and the QEMU tests for it;
3. menu, command-line editing, boot-loop guard, watchdog, crash dump, re-entry, and their QEMU tests;
4. scion-kernel: `make-boot-image.sh --loader-kind scion` (default unchanged) and this design there.
