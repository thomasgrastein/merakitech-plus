#!/usr/bin/env python3
"""release_notes.py — compose the GitHub release body for a version from CHANGELOG.md.

    python3 tools/release_notes.py 6.17 [--device-name merakiplus]
    python3 tools/release_notes.py 6.17 --device          (notes.txt: the section's "### On the machine" bullets)

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


# "On the machine" (OTA phase 3): every release carries an ULTRASHORT summary that the display shows in SYSTEM › UPDATES
# before INSTALL. It is a subsection of the release's CHANGELOG section (so it is in the GitHub release notes too):
#
#     ### On the machine
#     - Updates from GitHub, on the screen
#     - Grind size ruler on the GRIND page
#
# 1..4 bullets, each plain ASCII, at most 36 characters (the display's notes field), no markdown, no links.
# Published as the release asset notes.txt, which the signed manifest covers like every other asset.
DEVICE_HEAD = "### On the machine"
DEVICE_MAX_LINES, DEVICE_MAX_CHARS = 4, 36


class NotesError(ValueError):
    pass


def device_lines(body):
    """The 'On the machine' bullets of a CHANGELOG section (or an Unreleased block). Raises NotesError."""
    lines, inside, found = [], False, False
    for ln in body.splitlines():
        if ln.startswith("### ") or ln.startswith("## "):
            if inside:
                break
            inside = ln.strip() == DEVICE_HEAD
            found = found or inside
            continue
        if inside and ln.strip():
            if not ln.startswith("- "):
                raise NotesError(f"'{DEVICE_HEAD}' holds only '- ' bullets, found: {ln.strip()[:40]!r}")
            lines.append(ln[2:].strip())
    if not found:
        raise NotesError(f"no '{DEVICE_HEAD}' subsection (1..{DEVICE_MAX_LINES} bullets of <= {DEVICE_MAX_CHARS} characters)")
    if not 1 <= len(lines) <= DEVICE_MAX_LINES:
        raise NotesError(f"'{DEVICE_HEAD}' has {len(lines)} bullets, needs 1..{DEVICE_MAX_LINES}")
    for s in lines:
        if len(s) > DEVICE_MAX_CHARS:
            raise NotesError(f"'{s}' is {len(s)} characters, the display shows {DEVICE_MAX_CHARS}")
        if not s or any(not 32 <= ord(c) < 127 for c in s) or any(c in s for c in "*`[]<>|"):
            raise NotesError(f"'{s}': plain ASCII text only (no markdown, links or HTML)")
    return lines


def device_notes(body):
    """notes.txt: the 'On the machine' bullets, one per line."""
    return "".join(s + "\n" for s in device_lines(body))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("version", help="version without the leading v, e.g. 6.17")
    ap.add_argument("--device-name", default="merakiplus",
                    help="asset name of the device binary (pre-rebrand releases used the former name)")
    ap.add_argument("--device", action="store_true",
                    help="print notes.txt (the CHANGELOG section's '### On the machine' bullets, shown in SYSTEM > UPDATES); "
                         "fails when they are missing or too long")
    args = ap.parse_args()
    version = args.version.lstrip("v")

    body = changelog_sections(version)
    if not body:
        sys.exit(f"release_notes.py: no CHANGELOG.md section for version {version}")
    if args.device:
        try:
            sys.stdout.write(device_notes(body))
        except NotesError as e:
            sys.exit(f"release_notes.py: v{version}: {e}")
        return

    footer = f"""
---
### Installation
```sh
git clone https://github.com/{REPO}.git && cd merakitech-plus
git checkout v{version}
./install.sh --ip <display-ip> --dry-run
./install.sh --ip <display-ip>
# undo everything:
./revert.sh --ip <display-ip>
```
> ⚠️ **Use is entirely at your own risk.** Read the [disclaimer in the README](https://github.com/{REPO}#️-disclaimer--read-this-before-you-do-anything) before installing. Not affiliated with MerakiTech or Cisco Meraki. No manufacturer firmware is included.

### Assets
Our own builds only (zig 0.17.1; provenance in [`bin/README.md`](https://github.com/{REPO}/blob/main/bin/README.md)). `{args.device_name}` is the device binary (static ARMv7, musl); `merakiplus_snd` (from 6.18) is the sound helper (ARMv7, loads the display's own audio libraries at run time; the sounds themselves are in `audio/`); `switcher` + `grab_shim.so` are the optional gesture switcher (`--gesture`); `start.wav`, `ready_extract.wav`, `ready_steam.wav` are our own sounds; the `*_sim*` files are simulator builds for running `sim/` without a device. Checksums: `SHA256SUMS` (from 6.20) and `MD5SUMS`; the installer checks every file against its own pinned SHA-256 and md5 tables. CI (`.github/workflows/release.yml`) builds each release as a draft and verifies the committed `bin/` against fresh builds; the maintainer publishes it after signing.

### Signature
`manifest.json` + `manifest.sig` (from 6.23) list every asset with size and SHA-256 and carry an **Ed25519 signature made offline by the maintainer** after a byte-for-byte local rebuild — CI cannot sign. From the first release after the security hardening, a release is published only once signed (draft → sign → publish). Key id `26f673cde8584ba0`. `manifest.unsigned.json` is CI's unsigned copy, for comparison only. Check a download with `python3 tools/merakiplus.py verify-release <dir>`; `install.sh` checks it automatically and refuses unsigned files by default. Key and process: [`docs/signing.md`](https://github.com/{REPO}/blob/main/docs/signing.md).
"""
    print(body + "\n" + footer)


if __name__ == "__main__":
    main()