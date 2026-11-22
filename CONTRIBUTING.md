# Contributing to Meraki+

Thank you for wanting to help! Issues and pull requests are welcome. Please write them in English
(Danish is fine too if that is easier for you).

## Safety rules (non-negotiable)

1. **No writes to a machine without a backup and a revert path.** Every new device action must take
   an md5-verified backup first, be atomic (temp file on the same file system → md5 → `mv`), be
   idempotent and be undoable by `./revert.sh`. Slot A (`/customer/demo_v9`, libs, `test.sh`) must
   never be written. No `reboot` from any tool.
2. **No manufacturer files in the repository** — no `demo_v9`, MCU firmware (`MEM03_*.bin`), libs,
   `config.bin`, OTA packages or vendor scripts (`demo.sh`, `test.sh`, `upgrade.sh`). No disassembly
   dumps either. Short addresses/excerpts in documentation are fine (interoperability).
3. **No credentials or personal data** — no `config.json` from a real machine (it contains the Wi-Fi
   password), no Wi-Fi names/passwords, IP addresses, MAC addresses, serial numbers, personal paths
   (`/Users/<name>/…`) or md5 sums of files that contain secrets. Use placeholders
   (`<display-ip>`). CI and reviewers check this.
4. **The UI may only send frames that the stock UI also sends**, and only through the frame policy
   (write bands, range checks, CRC, ACK + one retransmit). Calibration, reboot, drain and back-flush
   words are never sent. New commands require documentation in `docs/protocol.md` with evidence.

## Development environment

| Tool | Why |
|---|---|
| [zig](https://ziglang.org/download/) **0.17.1** | the only C toolchain: cross-compiles to `arm-linux-musleabihf` (Cortex-A7, static) + host builds |
| `python3` ≥ 3.8 | simulator, E2E runner, installer and tests (standard library only) |
| `bash`, `git` | scripts |
| (optional) Docker + `arm32v7/debian:bullseye-slim` | run the ARM builds (32-bit `long`!) locally via qemu |

```sh
./build.sh           # everything: device (ARM) + tools + host SIM + self-tests  -> build/
./build.sh host      # only what CI uses: build/merakiplus_sim, switcher_host + self-tests
```

## Testing

Quick loop (run before every PR — about 1 minute):

```sh
./build.sh host
python3 sim/selftest.py                                   # the simulator's own self-test
python3 sim/run_e2e.py --all --quick --parallel 4         # 5 core scenarios against the REAL UI (SIM build)
python3 tests/test_installer.py                           # install/revert/idempotency against a mock display
python3 boot/test_switcher.py                             # the gesture switcher end-to-end (file mode)
```

The **full** E2E suite (all scenarios, both languages, replay of recordings; > 30 min) is not run per
iteration, but as a CI gate (`workflow_dispatch` / weekly) and before a release:

```sh
python3 sim/run_e2e.py --all --parallel 4
python3 sim/run_e2e.py --all --parallel 4 --ui-lang da
```

The UI's built-in self-test (layout, strings, frame policy, mutation probes) — requires the preview build:

```sh
zig cc -DMERAKIPLUS_PREVIEW -O2 -o build/merakiplus_preview src/merakiplus.c -lm && ./build/merakiplus_preview --selftest sim/fixtures/config.json
```

The simulator can also be played interactively in the browser: `python3 sim/play.py` (see `sim/PLAY.md`).

## Code style

- **C (`src/`, `boot/`):** C11, no external dependencies, static musl. New files must build with
  `-Wall -Wextra -Werror` (the UI file has a known, unchanged set of warnings — do not add new ones).
  Keep changes small and local; safety-critical functions (write queue, frame policy, start/stop
  recipes, boot push) are only changed together with a test that proves the behavior.
- **Python:** standard library, no dependencies, Python 3.8 compatible.
- **Strings:** all visible UI text exists in English **and** Danish (the string table in
  `merakiplus.c`). The bilingual UI is a feature — keep both columns in sync.
- **Language:** documentation, user-facing CLI messages and new code comments are in English. Some
  older code comments are still in Danish; translating them is a welcome contribution.

## Pull requests

1. Fork → branch (`feat/…`, `fix/…`) → small, focused commits.
2. Run the quick test loop above; describe in the PR what you ran (and whether it was tested on a
   real machine — which model/firmware).
3. If you change the binaries in `bin/`, describe how they were built and update `bin/MD5SUMS`
   **and** `BIN_MD5` + `BIN_SHA256` in `tools/merakiplus.py` (CI checks that they match). Build `bin/` with the **official**
   zig 0.17.1 tarball from ziglang.org (not a distribution package such as Homebrew's) and its own
   `ZIG_GLOBAL_CACHE_DIR`; only then are the files byte-identical to CI's fresh builds. The sounds in `audio/` are generated:
   change `tools/gen_audio.py`, run it, and update `AUDIO_MD5` + `AUDIO_SHA256` in `tools/merakiplus.py`. Never commit
   audio or video files you did not make (no vendor `.mp3`/`.mp4`; `tests/hygiene.py` refuses them).
4. Update `CHANGELOG.md` and the relevant documentation.
5. Use `merakiplus` for every technical name (paths, env vars `MERAKIPLUS_*`, binaries, logs). Names from
   releases before the 2026-11-19 rebrand exist only in the *legacy-compat* block of
   `tools/merakiplus.py` (install/revert migration); `tests/hygiene.py` fails if they appear elsewhere.

## Releases

**Releases are published only after explicit approval from the maintainer.** Other owners run these
builds on their machines, so a release is a decision, not a side effect: do not bump the version or
trigger the release workflow without prior agreement. CI may build and test any commit; publishing a
release (version tag + assets) is reserved to the maintainer.

## Bug reports

Use the "Bug report" issue template. Include: machine (Gen 1/Gen 2), Meraki+ version
(DIAGNOSTICS → VERSION), what you did, what happened, and if possible the output of
`python3 tools/merakiplus.py status --ip ADDR` and the log from `logs/` — **remove IP addresses and
other personal data first**.

## Security issues

Not as a public issue — see [SECURITY.md](SECURITY.md).
