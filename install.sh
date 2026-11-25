#!/usr/bin/env bash
# install.sh — install or update Meraki+ on a MerakiTech Gen1/Gen2 display.
#
#   ./install.sh --ip 192.168.1.50            install into slot B and start it now (no reboot)
#   ./install.sh --scan                       find the display on your network first
#   ./install.sh --ip ADDR --dry-run          show every step, change nothing
#   ./install.sh --ip ADDR --persist          also start Meraki+ at every boot
#   ./install.sh --ip ADDR --gesture          also install the center-hold gesture (stock -> Meraki+)
#   ./install.sh --help                       all options
#
# Undo everything: ./revert.sh --ip ADDR
# READ THE DISCLAIMER IN README.md FIRST. You use this entirely at your own risk.
#
# Signed by default (docs/signing.md): the installer refuses files that are not covered by the Ed25519-signed manifest
# of their release (--allow-unsigned only for development builds). Run from a clone, it uses that checkout — check out
# the latest release tag first (README). Run standalone (no tools/ next to it), it installs the LATEST PUBLISHED
# RELEASE: it resolves that tag on GitHub and fetches tools/merakiplus.py and the binaries from the tag, never from a
# branch. MERAKIPLUS_REF may name a release tag (vX.Y[.Z]) or a full 40-character commit SHA, nothing else.
set -euo pipefail
REPO="thomasgrastein/merakitech-plus"
REF="${MERAKIPLUS_REF:-}"

valid_ref() { printf '%s\n' "$1" | grep -Eq '^(v[0-9]{1,4}\.[0-9]{1,4}(\.[0-9]{1,4})?|[0-9a-f]{40})$'; }
if [ -n "$REF" ] && ! valid_ref "$REF"; then
  echo "install.sh: MERAKIPLUS_REF must be a release tag like v6.24 or a full 40-character commit SHA, not '$REF'" >&2
  echo "            (branches are refused: they can move; use a git clone for development builds)" >&2
  exit 2
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "install.sh: python3 is required (macOS: xcode-select --install, Debian/Ubuntu: apt install python3)" >&2
  exit 1
fi
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' || {
  echo "install.sh: python3 >= 3.8 is required" >&2; exit 1; }

fetch() {  # fetch URL FILE
  if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
  else python3 -c 'import sys,urllib.request; open(sys.argv[2],"wb").write(urllib.request.urlopen(sys.argv[1],timeout=60).read())' "$1" "$2"; fi
}
latest_tag() {  # the tag of the latest PUBLISHED release (drafts and prereleases are never "latest")
  local url="https://github.com/${REPO}/releases/latest" final
  if command -v curl >/dev/null 2>&1; then final="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "$url")" || return 1
  else final="$(python3 -c 'import sys,urllib.request; print(urllib.request.urlopen(sys.argv[1],timeout=60).geturl())' "$url")" || return 1; fi
  case "$final" in */releases/tag/*) printf '%s\n' "${final##*/}" ;; *) return 1 ;; esac
}

SRC="${BASH_SOURCE[0]:-}"
if [ -n "$SRC" ] && [ -f "$SRC" ] && [ -f "$(dirname "$SRC")/tools/merakiplus.py" ]; then
  TOOL="$(cd "$(dirname "$SRC")" && pwd)/tools/merakiplus.py"
else
  # Standalone mode (e.g. piped from curl). Trust root for this first download: GitHub + this repository (TLS). The
  # fetched installer then checks every binary against its pinned hashes AND the release's signed manifest — but it
  # cannot vouch for itself; a git clone of a release tag is the verifiable route (README "Security and OTA").
  if [ -z "$REF" ]; then
    REF="$(latest_tag)" || { echo "install.sh: could not determine the latest release (offline?) — set MERAKIPLUS_REF=vX.Y or use a git clone" >&2; exit 1; }
    valid_ref "$REF" || { echo "install.sh: unexpected latest release tag '$REF' — refusing" >&2; exit 1; }
  fi
  RAW="https://raw.githubusercontent.com/${REPO}/${REF}"
  TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
  echo "install.sh: standalone mode, release ${REF}: fetching the installer from ${RAW}/tools/merakiplus.py"
  fetch "${RAW}/tools/merakiplus.py" "$TMP/merakiplus.py"
  TOOL="$TMP/merakiplus.py"
  set -- "$@" --ref "$REF"
fi

if [ "$#" -eq 0 ]; then
  echo "usage: ./install.sh --ip ADDR [--persist] [--gesture] [--dry-run]   (or --scan)"
  echo "       ./install.sh --help"
  exit 2
fi
case "${1:-}" in -h|--help) exec python3 "$TOOL" install --help;; esac
python3 "$TOOL" install "$@"
