#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The second-stage loader on QEMU virt (EL1, booted with -kernel as S-Boot would start it):
one QEMU run per scenario, driven over the serial console. `make qemu-test` builds what it needs
(two loaders: plain and pinned, and the test payloads of tests/qemu/) and runs this.

  qemu-test.py --dir build/qemu-test [--only NAME] [-v]

Scenarios (each: what is appended to the loader, keys typed, lines that must appear):
  autoboot        the countdown starts the default iOS entry; boot_args, line and tokens checked
  menu-chain      '2' starts the chained arm64 Image in place with the FDT in x0
  edit-line       'e' edits the command line over the UART; the OS sees the edit
  tamper-sha      a payload changed after packing (CRC fixed up): SHA-256 mismatch, error screen,
                  back to the menu, then recovery chosen
  tamper-crc      a payload byte flipped: CRC32 mismatch, error screen, recovery chosen
  pinned-ok       a loader built with pinned digests starts the listed payload
  pinned-refused  the same loader refuses an unlisted one
  guard-count     an automatic start records one unacknowledged start
  guard-safe      3 strikes: the known-good entry is started
  guard-recovery  4 strikes, known-good already tried: reboot to recovery
  watchdog        a hung loader (test key H) is reset by its watchdog, strike counted
  cntfrq0         CNTFRQ_EL0 read as 0 (as the S9's S-Boot leaves it): the countdown ends, iOS 27 starts
  cntfrq0-keys    the same build: keys move the highlight and choose (the first phone run froze here)
  crash           a fault in the loader (test key X) prints the crash dump, then recovery
  reentry         the OS re-enters the loader at base+0x40 (struct scion_reentry) and asks for
                  entry 2: the loader re-verifies it, stops the watchdog the OS says it left
                  armed, and starts it with the FDT it kept at the cold boot (sweetwarm=)
"""
import argparse
import os
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GUARD_PA = 0x5FFFF000


def mkimg(loader_dir, out, entries, damage=()):
    end = open(os.path.join(loader_dir, "scion-loader.image-end")).read().strip()
    cmd = [sys.executable, os.path.join(HERE, "mkloaderimg.py"), "--loader",
           os.path.join(loader_dir, "scion-loader.bin"), "--image-end", end, "-o", out]
    for e in entries:
        cmd += ["--entry", e]
    for d in damage:
        cmd += ["--damage", d]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return out


def guard_rec(strikes, flags=0, path=None):
    rec = bytearray(struct.pack("<IHHIIIIQ", 0x474E4353, 1, 32, strikes, 0, flags, 0, 0))
    struct.pack_into("<I", rec, 20, zlib.crc32(bytes(rec)) & 0xFFFFFFFF)
    open(path, "wb").write(rec)
    return path


class Run:
    def __init__(self, img, extra, verbose, timeout):
        self.cmd = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a57", "-m", "2G",
                    "-display", "none", "-monitor", "none", "-nic", "none", "-serial", "stdio", "-no-reboot",
                    "-kernel", img] + extra
        self.p = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        self.q, self.out, self.verbose = queue.Queue(), "", verbose
        self.deadline = time.time() + timeout
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for chunk in iter(lambda: self.p.stdout.read1(4096), b""):
            self.q.put(chunk.decode("latin-1"))
        self.q.put(None)

    def _drain(self, until):
        """One chunk of output: "data", or "eof" when QEMU's output ended, or "idle" at until."""
        while time.time() < until:
            try:
                c = self.q.get(timeout=0.05)
            except queue.Empty:
                continue
            if c is None:
                self.q.put(None)
                return "eof"
            self.out += c
            if self.verbose:
                sys.stdout.write(c)
            return "data"
        return "idle"

    def expect(self, pattern, start=0):
        rx = re.compile(pattern)
        while True:
            m = rx.search(self.out, start)
            if m:
                return m.end()
            if time.time() > self.deadline:
                raise AssertionError(f"timed out waiting for /{pattern}/")
            if self._drain(self.deadline) == "eof" and not rx.search(self.out, start):
                raise AssertionError(f"QEMU ended before /{pattern}/")

    def send(self, s):
        time.sleep(0.2)
        self.p.stdin.write(s.encode())
        self.p.stdin.flush()

    def finish(self):
        end = time.time() + 5
        while self.p.poll() is None and time.time() < end:
            self._drain(time.time() + 0.2)
        if self.p.poll() is None:
            self.p.kill()
            raise AssertionError("QEMU did not end (no reset/power off)")
        while self._drain(time.time() + 0.5) == "data":
            pass


def scenarios(d):
    plain, pinned = os.path.join(d, "plain"), os.path.join(d, "pinned")
    ios27, ios16 = os.path.join(d, "ios27.payload"), os.path.join(d, "ios16.payload")
    chain = os.path.join(d, "chain.bin")
    two = [f"iOS 27:xnu:{ios27}:default", f"Starbite:image:{chain}"]
    with_safe = [f"iOS 27:xnu:{ios27}:default", f"iOS 16.5:xnu:{ios16}:safe"]
    s = {}

    def autoboot(r):
        r.expect(r"autoboot: iOS 27 in")
        r.expect(r"verify iOS 27: verified")
        r.expect(r"guard: 1 unacknowledged start")
        r.expect(r"FAKEXNU: line=-v serial=3 test=ios27 scion_ldr=0x40080000,0x[0-9a-f]+ scion_guard=0x5ffff000")
        r.expect(r"FAKEXNU: adt=FAKEADT!")
        r.expect(r"FAKEXNU: PASS boot_args")
    s["autoboot"] = (plain, two, [], autoboot)

    # The S9's S-Boot leaves CNTFRQ_EL0 at 0: the countdown must still end, and a key press must
    # still be read (the first phone run froze on both).
    def cntfrq0(r):
        r.expect(r"timer: CNTFRQ_EL0 is 0, board rate used")
        r.expect(r"autoboot: iOS 27 in 1 s")
        r.expect(r"verify iOS 27: verified")
        r.expect(r"FAKEXNU: line=-v serial=3 test=ios27 scion_ldr=")
        r.expect(r"FAKEXNU: PASS boot_args")
    s["cntfrq0"] = (os.path.join(d, "cntfrq0"), two, [], cntfrq0)

    def cntfrq0_keys(r):
        r.expect(r"autoboot: iOS 27 in")
        r.send("j")                           # down: the countdown stops, the menu waits
        r.expect(r"> 2\. Starbite")
        r.send("j")
        r.expect(r"> 3\. Reboot to recovery")
        r.send("\r")
        r.expect(r"reboot: recovery")
    s["cntfrq0-keys"] = (os.path.join(d, "cntfrq0"), two, [], cntfrq0_keys)

    def menu_chain(r):
        r.expect(r"autoboot: iOS 27 in")
        r.send("2")
        r.expect(r"verify Starbite: verified")
        r.expect(r"CHAIN: PASS fdt")
    s["menu-chain"] = (plain, two, [], menu_chain)

    def edit_line(r):
        r.expect(r"autoboot: iOS 27 in")
        r.send("e")
        r.expect(r"edit> ")
        r.send(" extra=1\r")
        r.expect(r"line now: -v serial=3 test=ios27 extra=1")
        r.send("\r")
        r.expect(r"FAKEXNU: line=-v serial=3 test=ios27 extra=1 scion_ldr=")
        r.expect(r"FAKEXNU: PASS boot_args")
    s["edit-line"] = (plain, two, [], edit_line)

    def tampered(reason):
        def f(r):
            r.expect(r"autoboot: iOS 27 in")
            r.expect(r"==== NOT STARTED: CHECK FAILED ====")
            r.expect(reason)
            r.expect(r"computed [0-9a-f]{64}")
            r.send("1")                       # back to the menu
            r.expect(r"---- menu ----")
            r.expect(r"1\. iOS 27  \[FAILED\]")
            r.send("3")                       # Reboot to recovery
            r.expect(r"reboot: recovery")
        return f
    s["tamper-sha"] = (plain, two, ["tamper-sha"], tampered(r"reason: SHA-256 mismatch"))
    s["tamper-crc"] = (plain, two, ["tamper-crc"], tampered(r"reason: CRC32 mismatch"))

    def pinned_ok(r):
        r.expect(r"verify iOS 27: verified")
        r.expect(r"FAKEXNU: PASS boot_args")
    s["pinned-ok"] = (pinned, two, [], pinned_ok)

    def pinned_refused(r):
        r.expect(r"autoboot: iOS 27 in")
        r.send("2")
        r.expect(r"reason: digest not in this loader's trusted list")
        r.send("2")
        r.expect(r"reboot: recovery")
    s["pinned-refused"] = (pinned, two, [], pinned_refused)

    def guard_count(r):
        r.expect(r"guard: 0 unacknowledged start\(s\), plan 0")
        r.expect(r"guard: 1 unacknowledged start\(s\) recorded")
        r.expect(r"FAKEXNU: PASS")
    s["guard-count"] = (plain, with_safe, [], guard_count)

    def guard_safe(r):
        r.expect(r"guard: 3 unacknowledged start\(s\), plan 1")
        r.expect(r"!! Boot-loop guard: the last starts did not finish booting")
        r.expect(r"autoboot: iOS 16\.5 in")
        r.expect(r"verify iOS 16\.5: verified")
        r.expect(r"guard: 4 unacknowledged start\(s\) recorded")
        r.expect(r"FAKEXNU: line=-v test=ios16 ")
    s["guard-safe"] = (plain, with_safe, [("guard", 3, 0)], guard_safe)

    def guard_recovery(r):
        r.expect(r"guard: 4 unacknowledged start\(s\), plan 2")
        r.expect(r"!! Boot-loop guard: nothing left to try")
        r.expect(r"autoboot: Reboot to recovery \(Scion recovery\) in")
        r.expect(r"reboot: recovery")
    s["guard-recovery"] = (plain, with_safe, [("guard", 4, 1)], guard_recovery)

    def watchdog(r):
        r.expect(r"watchdog: 3000 ms while the loader runs")
        r.expect(r"autoboot: iOS 27 in")
        r.send("H")
        r.expect(r"test: hanging the loader")
        r.expect(r"WATCHDOG: no progress for 3000 ms; guard strikes now 1; resetting")
        r.expect(r"reboot: normal")
    s["watchdog"] = (plain, two, [], watchdog)

    def crash(r):
        r.expect(r"autoboot: iOS 27 in")
        r.send("X")
        r.expect(r"==== LOADER CRASH: Sync exception \(vector 4\), data abort ====")
        r.expect(r"FAR 0000001000000000")
        r.expect(r"x30 [0-9a-f]{16}")
        r.expect(r"any key, or 60 s: reboot to recovery")
        r.send("k")
        r.expect(r"reboot: recovery")
    s["crash"] = (plain, two, [], crash)

    def reentry(r):
        r.expect(r"autoboot: iOS 27 in")
        r.send("e")
        r.expect(r"edit> ")
        r.send(" fake_reenter=1 sweetwarm=1\r")
        r.send("\r")
        r.expect(r"fdt: \d+ bytes kept at 0x5f801000 for a warm restart")
        r.expect(r"FAKEXNU: PASS boot_args")
        r.expect(r"FAKEXNU: re-entering the loader at 0x0*40080040")
        r.expect(r"scion-loader v\S+ \(qemu-virt\) at EL1, re- entry")
        r.expect(r"re-entry: reason 3 flags 0x53 entry 1 method 0 fdt 0x5f801000")
        r.expect(r"re-entry: entry 2 line: re=1")
        r.expect(r"verify Starbite: verified")
        r.expect(r"os watchdog: stopped before the next OS")
        r.expect(r"CHAIN: started, x0=0x000000005f801000")
        r.expect(r"CHAIN: PASS fdt")
    s["reentry"] = (plain, two, [], reentry)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", required=True)
    ap.add_argument("--only")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--timeout", type=float, default=30)
    a = ap.parse_args()
    work = os.path.join(a.dir, "runs")
    os.makedirs(work, exist_ok=True)
    failed = []
    for name, (loader, entries, mods, check) in scenarios(a.dir).items():
        if a.only and name != a.only:
            continue
        damage = [f"iOS 27:{m[7:]}" for m in mods if isinstance(m, str) and m.startswith("tamper-")]
        img = mkimg(loader, os.path.join(work, name + ".img"), entries, damage)
        extra = []
        for m in mods:
            if isinstance(m, tuple) and m[0] == "guard":
                g = guard_rec(m[1], m[2], os.path.join(work, name + ".guard"))
                extra += ["-device", f"loader,file={g},addr={GUARD_PA:#x},force-raw=on"]
        r = Run(img, extra, a.verbose, a.timeout)
        try:
            check(r)
            r.finish()
            print(f"PASS {name}")
        except AssertionError as e:
            r.p.kill()
            failed.append(name)
            print(f"FAIL {name}: {e}")
            log = os.path.join(work, name + ".log")
            open(log, "w").write(r.out)
            print(f"     console: {log}")
    print(f"qemu-test: {len(failed)} failed" if failed else "qemu-test: all passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
