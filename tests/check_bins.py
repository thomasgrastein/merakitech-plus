#!/usr/bin/env python3
"""check_bins.py — CI drift gate: the committed bin/ must equal fresh builds.

Run after ./build.sh and the Linux SIM cross-build:

    zig cc -target x86_64-linux-musl -DMERAKIPLUS_SIM -O2 -static -s src/merakiplus.c -lm \
        -o build/merakiplus_sim-linux-x86_64

These targets are bit-reproducible with the OFFICIAL zig 0.17.1 release (ziglang.org tarball,
the one CI installs), independent of the build machine and directory (see bin/README.md). A
mismatch means bin/ was not rebuilt after a source change — or it was built with a distribution
zig (e.g. Homebrew's), whose clang/LLD write their own name into the ELF .comment section, so
every byte of code matches but the md5 does not. Rebuild with the official toolchain (and its
own cache dir), then update bin/MD5SUMS and BIN_MD5 in tools/merakiplus.py (CONTRIBUTING.md).

Excluded on purpose:
  * grab_shim.so — the committed file is the on-device-verified build with only the
    non-loaded trailer trimmed; a fresh ./build.sh build differs by design (bin/README.md).
  * merakiplus_sim-macos-arm64 — Mach-O builds are not bit-reproducible; the committed
    build (made on macOS) is shipped as-is. tests/hygiene.py still md5-gates it.
"""
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPRODUCIBLE = (
    "merakiplus",
    "merakiplus_sim_arm",
    "switcher",
    "tinj_hold",
    "keyinj",
    "merakiplus_snd",
    "merakiplus_boot",
    "merakiplus_sim-linux-x86_64",
)


def md5(p):
    return hashlib.md5(p.read_bytes()).hexdigest()


fails = []
for name in REPRODUCIBLE:
    fresh, committed = ROOT / "build" / name, ROOT / "bin" / name
    if not fresh.is_file():
        fails.append(f"build/{name} missing — run ./build.sh and the Linux SIM cross-build first")
        continue
    if not committed.is_file():
        fails.append(f"bin/{name} missing")
        continue
    a, b = md5(fresh), md5(committed)
    if a == b:
        print(f"  [OK]    {name}  {a}")
    else:
        fails.append(f"bin/{name} drifted: committed {b} != fresh build {a} — "
                     "rebuild bin/ and update bin/MD5SUMS + BIN_MD5")

for f in fails:
    print("  [FAIL]  " + f)
print("BIN DRIFT: " + ("OK" if not fails else f"{len(fails)} problem(s)"))
sys.exit(1 if fails else 0)