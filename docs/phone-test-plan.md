# The second-stage loader on the phone: test plan for the owner

For a daytime session with the owner at the phone. Nothing here has run on a phone before; each
step is the smallest one that shows one new thing, and each has the same way back.

**What is written.** Only the BOOT partition, and only with the project's existing tester tool
`tools/ios27/s9/flash.sh SERIAL boot IMG` (scion-kernel), which writes BOOT, reads it back and
compares the SHA-256. RECOVERY (TWRP), EFS, the boot loader partitions and everything else are
never written; the loader itself writes no storage at all.

**The way back, at every step.** Hold Volume Up + Bixby + Power to reach TWRP (or choose *Reboot to
recovery* in the loader's menu), then `flash.sh SERIAL boot <the previous BOOT image>`; the project
already does this after every test image. Before step 1, keep the current BOOT image:
`flash.sh SERIAL backup-boot boot-before-loader.img`.

**What to build.** On the Mac, in a checkout of this repository: `make PLATFORM=starlte loader`
(prints the loader's path and its `image end`). For a first test that should boot exactly as
the image it replaces, add `LDR_OS_TOKENS=0`: the loader then passes the entry's line as packed, without
`scion_ldr=` and `scion_guard=` (54 bytes on the S9; the S9's kernel appends its own built-in arguments only
while its 1024-byte line has room, so those 54 bytes can push the last built-ins out). Nothing in the kernel
reads either token yet. In scion-kernel, once its option is merged:
`tools/ios27/s9/make-boot-image.sh ... --loader-kind scion --scion-boot <this checkout>` (the same
kernel, device tree and template as the image that boots today). Note the SHA-256 it prints.

## Test 1: the loader comes up and starts iOS 27 (the smallest first test)

Image: the loader + the iOS 27 payload you booted last (one entry, `default`), no pins.

Expected, in order:
1. S-Boot's splash as today.
2. A dark screen with a blue bar: **Scion loader v1 starlte / Choose what to start**; below it
   `> 1. iOS 27 [iOS]`, `2. Reboot to recovery (Scion recovery)`, `3. Reboot to Download mode`, `4. Power off`;
   the command line; at the bottom `autoboot: iOS 27 in 5 s (any key stops)` counting down.
3. Without touching anything: **Checking SHA-256 / iOS 27** (a few seconds at most), **Copying the
   kernel**, **Starting EL1 at 0x...**, then iOS 27 boots as it does with today's loader.

Record: whether the screen appeared at all, how long the SHA-256 check took, whether iOS reached
the same point as with today's loader. If the screen stays on S-Boot's splash, or goes black and
stays so for a minute: that is a hang before the console (the identity map or the display); take
the way back and report it. If a red **LOADER CRASH** screen appears: photograph it (ESR, ELR, FAR
are what matter), press any key: it reboots to TWRP.

## Test 2: the menu and the command line

Same image. At the countdown press Volume Down once: the countdown stops.
1. Volume Up/Down move the highlight; *Power off* (Power) turns the phone off (with a cable in, it
   resets instead: expected).
2. Boot again; on `iOS 27` press Bixby: **Edit the command line** with toggles `[ ] -v`,
   `[ ] serial=3`, `[ ] sweetwdog=60`, `[ ] wdt=-1` (or `[x]` where the line has them). Toggle `-v`,
   choose *Done*, the line under the menu changes; Power starts iOS with it (verbose boot).
3. *Reboot to recovery (TWRP)*: TWRP comes up. *Reboot to Download mode*: Download mode comes up
   (leave it with Volume Down + Power held).

## Test 3: the error screen and the boot-loop guard

1. **A changed payload.** The test 1 image with one byte of the payload changed after its SHA-256
   was recorded: `tools/mkloaderimg.py ... --damage 'iOS 27:sha'` (or `:crc`) makes the kernel slot,
   `starlte-pack-boot.py pack` the BOOT image (Claudius can make it). Expected: the countdown ends in a red **NOT STARTED: CHECK
   FAILED** screen naming `iOS 27`, `SHA-256 mismatch` (or `CRC32 mismatch`) and both digests; *Back
   to the menu* returns to the menu, *Reboot to recovery* goes to TWRP. Nothing is started.
2. **The guard's retained RAM.** With the test 1 image: let it autoboot, and as soon as iOS shows
   anything hold Volume Down + Power (a warm reset) before iOS finishes booting. Repeat. The loader's
   UART-less screen does not show the count: after **three** interrupted automatic starts, the
   fourth time the loader comes up the menu shows the amber line *Boot-loop guard: nothing left to try*
   with *Reboot to recovery (TWRP)* counting down (10 s). This shows the record at `0xfed17fc0`
   survives a warm reset. If the banner never appears, the record does not survive (open question 3
   in STATUS.md): report it. Choosing an entry yourself in the menu resets the count.
3. **The known-good entry** (only if BOOT has room for two payloads): an image with iOS 27 as
   `default` and a known-good payload as `safe`; repeat 2: the third time the known-good one starts
   on its own.

## Test 4 (later, with the warm restart): re-entry

When scion-kernel's warm restart is ready (`docs/ios27/WARM-RESTART.md`), its own plan covers the
re-entry into this loader; from this loader's side expected: a second **Scion loader** screen
without S-Boot's splash, then the requested entry after its SHA-256 check.

## What to report back

For each test: pass/fail, photos of any loader screen that was not expected, the time from S-Boot's
splash to the menu and the length of the SHA-256 check. Everything else (UART logs) is not available
on the S9.
