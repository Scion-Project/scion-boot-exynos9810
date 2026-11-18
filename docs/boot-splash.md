# The boot splash (loader/splash.c)

The loader leaves one Scion frame on the panel just before it starts an XNU entry, and the kernel
keeps it (its framebuffer console stays quiet unless `-v`). The frame has no text.

- **Art**: original, drawn in code (MIT-0 for `loader/splash.*` and `tests/host/splash-test.c`):
  a dark vertical gradient, an outer ring, a thin inner ring and a core disc at 42% of the height.
  Integer maths only. No bitmaps, no Apple artwork.
- **Ignition**: the core grows in four steps (64, 128, 192, 256 of 256), repainting only its square,
  with 10 ms between steps: about 30 ms in total. A full paint of the panel is the other cost.
- **Budget**: one full-panel write plus about 30 ms of pauses. Not measured on a phone yet.
- **No framebuffer**: `splash_handoff()` returns at once. Nothing else in the loader changes.
- **Crossfade**: not here. The loader cannot see iOS's first frame; a fade needs the display
  surface of iOS's own driver.

## Easter eggs

| Egg | How | Effect |
|---|---|---|
| Gold | Volume Up, Up, Volume Down, Down, Bixby, in the menu | gold palette for this boot |
| Ember | Volume Up, Down, Up, Down, Up, in the menu | ember palette for this boot |
| Mono | Volume Up and Volume Down held together for one second, in the menu | greyscale palette |
| Halloween | the loader was built on 31 October | orange palette |
| Winter | the loader was built on 25 December | ice palette |
| Verbose | Bixby held while Power starts an XNU entry | the kernel gets `-v`: its log shows on screen |

Rules: the eggs only observe the menu's keys; the menu does not change. An egg entered in the menu
wins over the build-date variant, and the last egg entered wins. The build date is the build's
`__DATE__`, because the loader reads no clock. Eggs live in RAM only: nothing is written to storage.

Tests: `tests/host/splash-test.c` (pixels, eggs, dates, the handoff on a host buffer) runs in
`make host-test`; `tests/qemu/fake_main.c` and `tools/qemu-test.py` check the splash on the QEMU
framebuffer in the autoboot scenario. The Verbose egg is not in QEMU (no Bixby there).
