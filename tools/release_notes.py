#!/usr/bin/env python3
"""release_notes.py — compose the GitHub release body for a version from CHANGELOG.md.

    python3 tools/release_notes.py 6.17 [--device-name merakiplus]

Prints the CHANGELOG section(s) for the version — e.g. both "[6.16, rebrand]" and
"[6.16]" for 6.16 — followed by the standard install / disclaimer / assets footer.
Used by .github/workflows/release.yml (and the v6.15–v6.17 release backfill).
Standard library only; exits non-zero when the version has no CHANGELOG section.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = "thomasgrastein/merakitech-plus"


def changelog_sections(version):
    """Every '## [<version>…]' section of CHANGELOG.md, in file order."""
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    out, capturing = [], False
    for line in text.splitlines():
        m = re.match(r"^## \[([^\]]+)\]", line)
        if m:
            head = m.group(1)
            # "6.16, rebrand" belongs to 6.16; "6.1" must not match "6.17"
            capturing = head == version or head.startswith(version + ",") or head.startswith(version + " ")
        if capturing:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("version", help="version without the leading v, e.g. 6.17")
    ap.add_argument("--device-name", default="merakiplus",
                    help="asset name of the device binary (pre-rebrand releases used the former name)")
    args = ap.parse_args()
    version = args.version.lstrip("v")

    body = changelog_sections(version)
    if not body:
        sys.exit(f"release_notes.py: no CHANGELOG.md section for version {version}")

    footer = f"""
---
### Installation
```sh
git clone https://github.com/{REPO}.git && cd merakitech-plus
./install.sh --ip <display-ip> --dry-run
./install.sh --ip <display-ip>
# undo everything:
./revert.sh --ip <display-ip>
```
> ⚠️ **Use is entirely at your own risk.** Read the [disclaimer in the README](https://github.com/{REPO}#️-disclaimer--read-this-before-you-do-anything) before installing. Not affiliated with MerakiTech or Cisco Meraki. No manufacturer firmware is included.

### Assets
Our own builds only (zig 0.17.1; provenance in [`bin/README.md`](https://github.com/{REPO}/blob/main/bin/README.md)). `{args.device_name}` is the device binary (static ARMv7, musl); `merakiplus_snd` (from 6.18) is the sound helper (ARMv7, loads the display's own audio libraries at run time; the sounds themselves are in `audio/`); `switcher` + `grab_shim.so` are the optional gesture switcher (`--gesture`); `start.wav`, `ready_extract.wav`, `ready_steam.wav` are our own sounds; the `*_sim*` files are simulator builds for running `sim/` without a device. Checksums: `SHA256SUMS` (from 6.20) and `MD5SUMS`; the installer checks every file against its own pinned SHA-256 and md5 tables. Releases are maintained automatically by CI (`.github/workflows/release.yml`), which verifies the committed `bin/` against fresh builds before publishing.

### Signature
`manifest.json` + `manifest.sig` (from 6.23) list every asset with size and SHA-256 and carry an **Ed25519 signature made offline by the maintainer** after a byte-for-byte local rebuild — CI cannot sign. They are added after publication; until then the release is unsigned. `manifest.unsigned.json` is CI's unsigned copy, for comparison only. Check a download with `python3 tools/merakiplus.py verify-release <dir>`; `install.sh` checks it automatically. Key and process: [`docs/signing.md`](https://github.com/{REPO}/blob/main/docs/signing.md).
"""
    print(body + "\n" + footer)


if __name__ == "__main__":
    main()