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
   either — not even short excerpts or single quoted instructions. Describe stock behaviour in prose or
   pseudocode; flash/RAM addresses are fine (interoperability). `tests/hygiene.py` checks the docs for this.
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

The **full** E2E suite (all scenarios, both languages, replay of recordings; > 30 min) is not needed per
local iteration. CI runs it automatically on every pull request that changes code (see
[How changes reach `main`](#how-changes-reach-main)), weekly, and on request (`workflow_dispatch`). To run it locally:

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

## How changes reach `main`

**Every change goes through a pull request** — from community contributors, the maintainer and the
maintainer's automation (coding agents) alike. `main` is protected:

| Rule on `main` | Setting |
|---|---|
| Pull request required | yes, with **1 approving review** from a maintainer; a new push **dismisses** earlier approvals; changes to trust anchors need the **code owner** ([`.github/CODEOWNERS`](.github/CODEOWNERS)) |
| Required status check | **`merge-gate`** (CI), branch must be **up to date** with `main` before merge |
| Force-push / branch deletion | not allowed |
| Administrators | may bypass (see below) |
| Tags `v*` | created only by the owner, by publishing a signed draft release (tag ruleset) |

### The CI gate: `merge-gate`

`merge-gate` is the only required check. It always runs and passes only when the jobs it aggregates
passed ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)):

| Job | Runs for | What |
|---|---|---|
| `build + quick tests` | every PR and push | hygiene (vendor files, personal data, checksums), device + host builds, `bin/` drift check, UI self-test, simulator self-test, quick E2E, gesture switcher, launcher, signed manifests, installer/revert |
| `full E2E gate (EN + DA)` | PRs that touch **code**: `src/`, `sim/`, `boot/`, `build.sh` or `ci.yml` | all E2E scenarios in English and Danish (> 30 min) |

A docs-only PR therefore needs only the quick gate; a code PR also needs the full E2E suite to be green.
A few timing-window checks are sensitive to load on the shared CI runner: a scenario that fails in the parallel
full run is re-run **once, alone**; the gate still fails if it fails again or if more than three scenarios failed.
Every retry shows up as a warning on the run — if you see the same scenario retried repeatedly, please open an issue.
The full suite also runs weekly and on request (`workflow_dispatch` with `full_e2e`).

### Community contributors

1. Fork → branch (`feat/…`, `fix/…`, `docs/…`) → small, focused commits.
2. Run the quick test loop above; describe in the PR what you ran (and whether it was tested on a
   real machine — which model/firmware).
3. Open the pull request against `main`. It is merged when `merge-gate` is green **and** a maintainer
   has approved it. If `main` moves on, update your branch (merge or rebase `main` into it).
4. If you change the binaries in `bin/`, describe how they were built and update `bin/MD5SUMS`
   **and** `BIN_MD5` + `BIN_SHA256` in `tools/merakiplus.py` (CI checks that they match). Build `bin/` with the **official**
   zig 0.17.1 tarball from ziglang.org (not a distribution package such as Homebrew's) and its own
   `ZIG_GLOBAL_CACHE_DIR`; only then are the files byte-identical to CI's fresh builds. The sounds in `audio/` are generated:
   change `tools/gen_audio.py`, run it, and update `AUDIO_MD5` + `AUDIO_SHA256` in `tools/merakiplus.py`. Never commit
   audio or video files you did not make (no vendor `.mp3`/`.mp4`; `tests/hygiene.py` refuses them).
5. Update `CHANGELOG.md` and the relevant documentation.
6. Use `merakiplus` for every technical name (paths, env vars `MERAKIPLUS_*`, binaries, logs). Names from
   releases before the 2026-11-19 rebrand exist only in the *legacy-compat* block of
   `tools/merakiplus.py` (install/revert migration); `tests/hygiene.py` fails if they appear elsewhere.

Do not bump the version in a community PR unless the maintainer has asked for it (see [Releases](#releases)).

**Trust anchors.** Some files decide what users install and which signatures they trust: `.github/`, `install.sh`,
`revert.sh`, `build.sh`, `tools/merakiplus.py` (pins, keys, verifier), `tools/sign_release.py`, `keys/`,
`src/merakiplus_upd_keys.h`, `tests/hygiene.py`, `tests/check_bins.py` and `bin/`. They are owned by the maintainer in
`.github/CODEOWNERS`; CI marks a PR that touches one with a warning. That is not a rejection — it means the diff is read
line by line. Keep such changes small and separate from feature work where you can.

### Maintainer and maintainer automation

The maintainer's own work — and work done by coding agents on the maintainer's behalf — follows the
same flow: a branch in this repository (`vX.Y-topic`, `feat/…`, `docs/…`) → `gh pr create` → CI
(`merge-gate`) → the maintainer merges (`gh pr merge --admin` once `merge-gate` is green). Agents never merge, never
push to `main`, never create tags and never publish or sign a release; they open the PR and report its CI state.
Recommended: agents use a fine-grained token for this repository with *Contents* and *Pull requests* write access
only — not the owner's login (see [docs/release-process.md](docs/release-process.md#8-credentials-for-automation)).

The repository owner is an administrator, and branch protection is configured **without** enforcing
the rules for administrators. The owner can therefore merge their own (or an agent's) PR without a
second reviewer — there is no other maintainer to review — and can push to `main` directly in an
emergency. This is a deliberate escape hatch, not the normal path: wait for `merge-gate` before
merging, as everyone else does. The owner uses pull requests for their own changes too.

## Releases

**Releases are published only after explicit approval from the maintainer.** Other owners run these
builds on their machines, so a release is a decision, not a side effect.

**Version discipline.** Feature, fix and tooling PRs keep `VERSION` and `MERAKIPLUS_VERSION` at the last released
version and add their `CHANGELOG.md` entry under `## [Unreleased]`; a shipped binary they change is rebuilt with the
unchanged version string. Only a release PR assigns a version.

1. A release PR bumps the version (`VERSION` in `tools/merakiplus.py` **and** `MERAKIPLUS_VERSION`
   in `src/merakiplus.c`), moves the `Unreleased` changelog entries into the new version and, for device changes,
   updates `bin/`.
2. The maintainer merges it once `merge-gate` (including the full E2E suite) is green.
3. On the push to `main`, [`release.yml`](.github/workflows/release.yml) sees a version newer than the
   latest release, re-runs the test gate (cold build) and creates a **draft** release (unsigned assets,
   `manifest.unsigned.json`; not public, no tag).
4. The maintainer reviews, signs and publishes the draft offline (`tools/sign_release.py sign vX.Y --upload`: diff
   since the last signed release, key pin, byte-for-byte rebuild, next `seq`, `manifest.json` + `manifest.sig`, publish)
   — see [docs/signing.md](docs/signing.md). Only then can the release be installed: the installer is **signed by
   default** and refuses unsigned files unless `--allow-unsigned` is given (development builds).

Details: [docs/release-process.md](docs/release-process.md). Do not bump the version or trigger the
release workflow without prior agreement. CI may build and test any commit; publishing a release
(version tag + assets) and signing it are reserved to the maintainer.

## Bug reports

Use the "Bug report" issue template. Include: machine (Gen 1/Gen 2), Meraki+ version
(DIAGNOSTICS → VERSION), what you did, what happened, and if possible the output of
`python3 tools/merakiplus.py status --ip ADDR` and the log from `logs/` — **remove IP addresses and
other personal data first**.

## Security issues

Not as a public issue — see [SECURITY.md](SECURITY.md).
