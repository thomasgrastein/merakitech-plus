# Changelog

All notable changes to Meraki+. Details (with evidence, frames and test figures) are in
[docs/dev/UI_v6.md](docs/dev/UI_v6.md) under *CHANGES-v6.x*. All dates are 2026.

## [6.25] — 2026-11-26
**Grinder: the measured grind size as a ruler, and a bean container message instead of *ASLEEP*.** Both are display-only
and read-only: no UART frame, frame-policy rule or guard changed, and the grinder calibration is never written.
**Hardware-verified on a Gen 1 machine** (ruler tab 1 / 15 / 30 = word 37 at the dial's FINE end, middle and COARSE
end; mirrored orientation confirmed against the physical dial; bean container lifted → *MANGLER*, back → *KLAR*).
Details: [docs/ui-guide.md](docs/ui-guide.md) (GRIND page, §23).
### Added
- **GRIND page: the measured grind size as a ruler** (*GRIND SIZE* / *MALEGRAD*), between GRIND MODE and HOLD TO
  START: the stock dial unrolled into a *COARSE … FINE* (*GROV … FIN*) scale with a marker and a value tab, **mirrored
  like the physical dial on the bean container** (FINE / FIN at the right end, COARSE / GROV at the left, numbers
  ascending right-to-left; hw-confirmed on Gen 1). The value is
  the controller's grind gear, word 37, which Meraki+ already polls. The scale is 1..30 (labels 1 · 5 · 10 … 30). It is
  0..40 (labels every 10) when the machine's gear-40 point is calibrated: word 75 ≠ 0xFFFF and word 75 − word 65 ≥ 100,
  the same rule as the controller. There the controller can report down to −3. Values outside the scale pin to its ends,
  and the tab still shows the number. With no connection, or before word 37 has been read, it shows *--* with no marker,
  and HOLD TO START is dimmed by its usual guard.
- **Bean container message (work_sta word 33 bit 8, docs/ui-guide.md §23):** lifting the bean container showed
  *ASLEEP* / *SOVER* because bit 8 clear was read as sleep. Stock shows `lackBeenTank.png` for that bit. The new BEAN
  phase shows an amber *MISSING* / *MANGLER* chip and *BEAN CONTAINER MISSING · FIT IT* /
  *BØNNEBEHOLDER MANGLER · SÆT DEN I* on HOME; sleep stays word 1 = 1 / word 0 = 0, offline stays no link. The start
  guards block as before with the new text *FIT THE BEAN CONTAINER* / *SÆT BØNNEBEHOLDEREN I*. Display only.
- SIM state JSON: `grind_size` {value, shown, lo, hi, scale40, marker, pos, pinned, labels, drawn, start}; new
  scenarios `grind_size_ruler` and `bean_container_missing` (SIM `bean_tank` switch event); UI self-test sections
  (7 + 4 checks, EN + DA); previews 113–118.
### Changed
- HOME during a grind: the third stat is **SIZE** / **GRAD** (was STEP / TRIN). It shows word 37 as a signed number
  (*-3*, not *65533*).
### Verification
- UI self-test **603 / 0** EN and DA (sections *grinder size ruler* and *v6.25 bean container*). SIM scenarios
  `grind_size_ruler` 46/46 and `bean_container_missing` 19/19 EN + DA; full E2E EN + DA in CI (`merge-gate`), quick
  E2E 5/5, `sim/selftest.py` 204/0, hygiene + bin/ drift OK (official zig 0.17.1).
- On a Gen 1 machine (the same source with the previous version string): ruler follows the dial live (scale 1..30
  on that machine), HOME stat *GRAD* during a grind, bean container message both ways; 0 CRC errors, no error words,
  `config.json` byte-identical.

### Installer and releases (security hardening)
No device binary changed by these. Details:
[docs/release-process.md](docs/release-process.md), [docs/signing.md](docs/signing.md).
#### Changed (behaviour)
- **Installer: signed by default.** `./install.sh` stops when the files are not covered by a valid signed manifest
  of their release (no manifest, unreachable, or files that differ from it). `--allow-unsigned` installs development
  builds on purpose, with an `UNSIGNED install` warning; on a display that already accepted a signed release it also
  needs `--allow-downgrade`. A bad signature, an unknown key or an older `seq` is refused either way.
  `--require-signed` is still accepted (now the default).
- **Installs come from release tags, never from a branch.** The default `--ref` is the installer's own release tag
  (`v<VERSION>`); `--ref` / `MERAKIPLUS_REF` accept only `vX.Y[.Z]` tags and full commit SHAs. The standalone
  `install.sh` (curl) resolves the **latest published release** and fetches the installer and binaries from that tag.
  README: clone, then check out the latest release tag.
- **Releases: draft → sign → publish.** `release.yml` builds a *draft* release (not public, no tag) and never changes
  an existing release; `tools/sign_release.py sign --upload` signs the draft and publishes it. No unsigned release is
  public any more. It runs on `main` only (no tag trigger; a dispatch on another branch is refused).
#### Added
- **Signing review:** before signing, `sign_release.py` shows the commits, diffstat and changed **trust anchors** since
  the last signed release, checks the three pinned key copies at the release commit against a key pin kept **outside
  the repository** (`~/.merakiplus/trusted-key-pin`, `pin-keys`; a difference is refused unless
  `--accept-key-change`), and asks for the reviewed commit SHA. It refuses to run in CI and to sign a draft whose
  commit is not on `origin/main`.
- **`grab_shim.so` is verifiable:** `build.sh` trims the fresh build deterministically (loaded segments only), which
  reproduces the shipped, on-device-verified file byte for byte; the `bin/` drift check and the signing rebuild now
  cover it.
- **`.github/CODEOWNERS`** for the trust anchors, **Dependabot** for the pinned actions, and a CI warning on PRs that
  touch a trust anchor.
#### Security (CI)
- Every action pinned to a full commit SHA; `persist-credentials: false` on every checkout; read-only workflow token,
  `contents: write` only on the draft-release job (no repository code runs there); cold zig cache (`use-cache: false`)
  for the `bin/` drift check and the release build. `tests/hygiene.py` fails if any of this regresses, or if a trust
  anchor is not code-owned.
- Tests: installer (signed by default, `--allow-unsigned`, `max_seq`, `--ref`, the `install.sh` bootstrap with a fake
  `curl`), signing (key pin, review, draft commit, CI refusal, `release.yml` shape).

### Repository process: pull requests
No code or binary changed. Details: [CONTRIBUTING.md](CONTRIBUTING.md#how-changes-reach-main), [docs/release-process.md](docs/release-process.md).
#### Changed
- **`main` is protected; every change arrives as a pull request.** One required CI check, `merge-gate`, plus one
  maintainer approval for community PRs; force-push and branch deletion are blocked.
- **CI:** `merge-gate` aggregates the quick gate and the full E2E suite (EN + DA). The full suite now runs
  automatically for every change to `src/`, `sim/`, `boot/`, `build.sh` or the CI workflow; docs-only changes
  need only the quick gate.

### Documentation
- [Original CoffeeSense NFC reference](docs/stock-coffeesense-nfc.md) covering the tag format, background
  codes, compatible exports and writing a MIFARE Classic 1K tag.

## [6.24] — 2026-11-25
**Water tank: a real message instead of *UNKNOWN FAULT*** (fixes [#1](https://github.com/thomasgrastein/merakitech-plus/issues/1)).
Removing the water tank showed *ERROR · UNKNOWN FAULT*. The controller reports the tank through error bits that
Meraki+ had no text for; the stock UI shows them as water-tank icons. Details: [docs/ui-guide.md §22](docs/ui-guide.md#22-water-tank-messages-v624-issue-1).
### Fixed
- HOME's error line now names the water supply (code shown dimmed, as before):
  - word 36 `0x8000` → *WATER TANK MISSING* / *VANDTANKEN MANGLER* (E3)
  - word 36 `0x0001` → *WATER TANK LOW · PLEASE REFILL* / *LAVT VANDNIVEAU · FYLD TANKEN* (E3; stock: "Water tank is
    low, please refill water")
  - word 35 `0x8000` → *NO WATER FLOW · CHECK THE TANK* / *INTET VANDFLOW · TJEK TANKEN* (E2; pump ran, no flow)
- Boiler / sensor / mains / scale faults keep priority; any other unmapped bit still reads *UNKNOWN FAULT*.
- **Display only:** the phase (ERROR), the start guards (*ERROR ACTIVE · START BLOCKED*) and the frame policy are
  unchanged; no UART frame changed.
- **Evidence:** static analysis of the Gen 1 controller firmware 5.1.4 (tank check, dry-pump check) and the stock
  display UI. The report came from a **Gen 2** machine — the same bits there are not confirmed yet.
### Verification
- UI self-test **592 / 0** EN and DA (new section *v6.24 water tank messages*: 14 bit combinations × 2 languages,
  guards unchanged, clear → READY, text widths); also as a 32-bit ARMv7 binary (EN + DA). Mutants (mapping removed,
  bits swapped, wrong code mask) fail it. New SIM scenario `fault_water_tank` 35/35 EN + DA (22/35 against the v6.23
  SIM). Full E2E **52/52 EN** and **52/52 DA**, quick E2E 5/5,
  `sim/selftest.py` 204/0, installer suite 152/152, launcher tests (96 power-cut states), switcher 55/0, manifest
  tests 115/115, hygiene + bin/ drift OK (official zig 0.17.1).
- **Not yet on a machine.**
### Also in v6.24: signed releases (OTA phase 2, tooling only)
No device binary changed. Details: [docs/signing.md](docs/signing.md), [docs/ota-plan.md](docs/ota-plan.md#phase-2--implemented-2026-11-23).
#### Added
- **Signed release manifests.** `manifest.json` (every asset with size + SHA-256, a monotonic `seq`, tag and commit)
  + `manifest.sig` (Ed25519, signify format), signed **offline** by the maintainer with `tools/sign_release.py` after
  a byte-for-byte rebuild of the tag with the official zig. CI cannot sign; it publishes `manifest.unsigned.json` for
  comparison and refuses to replace assets under an existing signed manifest. **v6.23 is signed** (seq 1; the two
  files were added to the published release).
- **Installer:** verifies the release's signed manifest on the computer before any change on the display, records the
  newest accepted `seq` on the display and refuses older signed releases (`--allow-downgrade`); `--require-signed`;
  `merakiplus.py verify-release DIR` for downloads; `--bin-dir` also takes the WAVs and a signed manifest.
- **Device verifier** `merakiplus_upd` (C + Zig standard library crypto), built and tested on the host and as a static
  ARMv7 binary in an arm32v7 container — not shipped yet (phase 3).
- Tests: `tests/test_upd.py` (118 checks), installer scenario *2: signed release manifest*, hygiene check that the
  pinned keys agree and that no private key is in the tree.

## [6.23] — 2026-11-23
**Auto off: 30 minutes by default, like the stock UI.** Until now Meraki+'s AUTO OFF was *off* unless you turned it
on, so a machine left alone stayed on and hot; the stock UI always switches off after 30 minutes without use. The
default is now 30 minutes. **An explicit choice is kept:** if your schedule file already says *off* (or 15 / 20 / 60),
nothing changes. Details: [docs/time-scheduling.md §5.1](docs/time-scheduling.md#51-default-and-migration-v623),
[docs/safety.md §3](docs/safety.md#3-where-meraki-differs-from-the-stock-ui).
### Changed
- **AUTO OFF default = 30 min** (*SYSTEM → SCHEDULE → AUTO OFF*, still selectable: off / 15 / 20 / 30 / 60). The
  setting is read from Meraki+'s own file `/new_customer/merakiplus/schedule`:
  - no file, no `autooff=` line, or a value that is not exactly 0 / 15 / 20 / 30 / 60 → **30 min** (an empty or
    garbled value is no longer read as 0);
  - `autooff=0` → **off** (an explicit choice), `autooff=15/20/30/60` → unchanged.
- **Updating from v6.16–v6.22:** never saved a SCHEDULE row → you now get 30 min. Saved any SCHEDULE row → the file
  already holds your AUTO OFF value and it is kept — **including *off*, even if you never touched AUTO OFF itself**
  (Meraki+ cannot tell that apart from a deliberate *off*). To get the stock behaviour, set AUTO OFF to *30 MIN*.
  The installer does not rewrite the file.
- No UART frame, frame-policy rule or `config.json` handling changed; the power-off sequence is the existing one.
### Verification
- UI self-test **588 / 0** EN and DA (6 new checks: the 13-case migration matrix, the SYSTEM row showing 30 MIN with
  no file, explicit OFF surviving a save of another row, the default written as `autooff=30` on the first save,
  explicit OFF staying on through 61 min idle, the default powering off at 30:00 with the warning at 29:30). Setting
  the default back to off fails 4 of them; going back to the old lenient parsing fails the migration matrix.
  The 588 / 0 also holds as a 32-bit ARMv7 binary (EN + DA). `schedule_prewarm` asserts `sched.auto_off = 30` before
  and after the first save (41/41; 39/41 against the v6.22 SIM). Full E2E **51/51 EN** and **51/51 DA**, quick E2E 5/5,
  `sim/selftest.py` 204/0, installer suite 140/140, launcher tests (96 power-cut states), switcher 55/0, hygiene +
  bin/ drift OK (official zig 0.17.1). Details: [docs/dev/UI_v6.md CHANGES-v6.23](docs/dev/UI_v6.md#changes-v623).
- **Not yet on a machine.**

## [6.22] — 2026-11-22
**Hot water: the controller-version gate + manual hot water.** Hot water *by weight* (AUTO: the controller stops at
the set weight) exists only on controllers with firmware **> 5.1.2**. Meraki+ now reads the controller's firmware
string from the status poll it already sends and applies **exactly the stock UI's rule**; every other controller gets
**manual hot water**, which Meraki+ now has. Details: [docs/water-dosing-compat.md §10–§11](docs/water-dosing-compat.md#11-implementation-v622).
Designed and tested against controller firmware **V5.1.4** and the stock display build **1.1.1-69** (README).
### Added
- **Controller version gate:** words 80..87 (copy 88..95) are parsed; AUTO only when the version is **> 5.1.2**, the
  copy matches and there is no scale fault. Not polled yet, empty, unparsable, copy mismatch or ≤ 5.1.2 → manual only
  (fail-safe). No new UART traffic; no owner override; `config.json` is never written by the gate.
- **Manual hot water:** the stock legacy water page's frames — start `cmd6 sub19 len1` word 19 = 1, stop word 19 = 0.
  Runs until **STOP WATER**; no time limit and no display-side weight stop (as in the stock UI). Used on controllers
  that cannot dose and for *WATER MODE = MANUAL* on those that can (v6.21: *"WATER = MANUAL · USE STOCK UI"*).
- **Manual-only WATER page:** no HOT WATER / WATER MODE rows, *MANUAL HOT WATER · RUNS UNTIL YOU TAP STOP* and the
  reason (*CONTROLLER < 5.1.3: NO WEIGHT STOP* / *CONTROLLER VERSION UNKNOWN* / *SCALE FAULT: NO WEIGHT STOP*). HOME: *MAN.*
  under the water icon, *MANUAL · RUNS UNTIL STOP* while it runs. EN + DA.
- **DIAGNOSTICS › CONTROLLER:** the controller's firmware string and what hot water can do (*AUTO + MANUAL* /
  *MANUAL ONLY* / *MANUAL (SCALE)*).
- **Scale fault** (word 35 bits `0x1000`/`0x0400`): manual only; a running AUTO dose is stopped once with word 16 = 0
  if the controller has not cleared it itself (stock `hotWaterUpdate`); HOME says *SCALE FAULT* (code E2).
- Simulator: event `mcu_version`, manual water by word 19 alone, the scale-fault model, state `water`; scenarios
  `dosing_version_detect`, `dosing_gated_old_mcu`, `dosing_scale_fault` (sim/CONTRACT.md addendum v6.22).
### Changed
- **Frame policy:** word 19 is a start word (needs the new manual-water intent); **word 16 = 1 is rejected unless the
  controller doses** — the AUTO start can never reach a controller ≤ 5.1.2. On such a controller STOP WATER sends
  word 19 = 0 only (never word 16).
- The 0x89 answer stays the stock build string `1.1.1-69` (the controller derives a brew-boiler offset from it).
### Verification
- UI self-test **582 / 0** EN and DA, also as a 32-bit ARMv7 binary (new section *v6.22 water dosing gate*, 13 checks);
  `sim/selftest.py` **204 / 0** (T16 new); full E2E **51/51 EN** and **51/51 DA** (`dosing_version_detect` 28, `dosing_gated_old_mcu` 44, `dosing_scale_fault` 34 checks), quick E2E 5/5; installer suite 140/140, launcher tests (96 power-cut states), switcher 55/0, hygiene + bin/ drift OK (official zig 0.17.1). The three new scenarios fail against the v6.21 SIM build (20/28, 20/44, 20/34).
- **Not yet on a machine** (install waits for the next hardware session). The manual frame on a ≤ 5.1.2 controller
  is the stock one but not hardware-tested here; manual mode on the 5.1.4 machine is to be verified live.

## [6.21] — 2026-11-22
**Automatic rollback (OTA plan phase 1b) + the installer hardening of phase 1a.** Every Meraki+ version now lives
complete in its own release directory, and a small **launcher** decides at every start which one runs — falling back on
its own, without a network or a computer, when a version does not work. Details:
[docs/ota-plan.md, phase 1b](docs/ota-plan.md#phase-1b--implemented-2026-11-22). Meraki+ still contacts no servers.
### Added
- **Launcher** (`src/merakiplus_boot.c`, static ARMv7, ~60 KB, no UI/network/UART, never sends a signal) installed as
  `/new_customer/demo_v9` — the program the manufacturer's watchdog restarts. It starts the release named by
  `/new_customer/merakiplus/ota/state` and verifies its size + SHA-256 against the release's `MANIFEST` first.
- **Release directories** `/new_customer/merakiplus/rel/<version>-<sha8>/` (UI, sound helper, sounds, `MANIFEST`),
  staged under `.stage-<name>` and renamed into place in one step after a sync; never rewritten afterwards.
- **Automatic rollback:** a newly installed version runs as a **trial**. If it does not become *healthy* within 3
  starts it is rolled back; a crash storm (3 failed starts in a row, or a 7th start within 10 min) falls back for the
  rest of the boot: **new → previous version → the stock UI from slot A** (read-only; the boot flag is never touched).
  The stock UI brings Wi-Fi up, so a build that crashes before its own Wi-Fi start can no longer lock you out (the
  `--persist` soft-brick case of docs/ota-plan.md §2.4).
- **Healthy marker** written by the UI itself: running 60 s with the controller answering for the last 30 s
  (`/tmp/merakiplus_healthy` with boot id + PID; on a trial also on flash so the launcher commits it). The
  installer's health check now waits for it (the 1a tty-atime measurement remains as the explanation on failure).
- **Exec-reload:** an installed update starts when the machine is **idle on HOME** — the running UI execs the
  launcher itself (same PID: no watchdog race, no signal, no reboot); never during an activity. Older UIs are
  activated through the watchdog as before. `--no-activate` leaves the trial for the next power-on.
- **SYSTEM line** under the title: *MERAKI+ V6.21* · *TRIAL* until healthy, · *V6.22 FAILED* after a rollback,
  · *FALLBACK* when an older version runs this boot, or *VENDOR UPDATE · RUN INSTALL* (EN/DA). One-time toasts after
  the splash: *VENDOR UPDATE FOUND · RUN INSTALL* and *V6.22 FAILED · BACK ON V6.21*.
- **Manufacturer-update notice in Meraki+** (deferred from 1a): a downloaded vendor update waiting for the next boot,
  or `demo.sh` that lost the gesture line.
- `./install.sh status` shows the launcher, current / previous / trial release, the last rollback, this boot's
  fallback level and the healthy marker.
### Changed
- **Migration:** the first v6.21 install copies the running v6.17–v6.20 build into a release directory (the rollback
  target), installs the new version as a trial, writes the state file, and only then renames the launcher over
  `/new_customer/demo_v9` (after an on-display launcher selftest). Backups are untouched; `./revert.sh` restores the
  vendor program in slot B and removes `rel/` + `ota/`.
- **Gesture switcher:** recognises a release started by the launcher as Meraki+ (`rel/<name>/demo_v9`) and asks
  the launcher to try Meraki+ again even after this boot's stock fallback. An installed switcher is refreshed and the
  running one restarted by every install (also without `--gesture`).
- The UI finds its sound helper and sounds in its own release directory.
### Installer hardening (phase 1a, first released with this version)
#### Changed
- **Power-cut safe uploads:** every uploaded file is synced to flash **before** it is renamed into place (before, the
  sound helper, the WAVs, the switcher and `demo.sh` were renamed first and synced after).
- **SHA-256:** the installer pins SHA-256 next to md5 for every file it installs and checks both on the computer;
  on the display SHA-256 is checked too when its busybox has `sha256sum`. Releases carry `SHA256SUMS` (and still
  `MD5SUMS`) and now also `switcher`, `grab_shim.so` and the three WAVs.
- **60 s health check** instead of 5 s: the same Meraki+ process must keep running and the coffee machine's
  controller must be talking to it; otherwise the installer says `HEALTH CHECK FAILED` and points to `./revert.sh`.
- Free space is checked for updates too; temp files of an interrupted run are removed; old development builds are
  listed (and offered for removal interactively).
#### Added
- **Manufacturer-update detection** in `status` and `install`: a vendor update waiting for the next boot, slot B or
  `demo.sh` replaced, slot A changed, boot flag cleared. Re-running `install.sh` repairs it.

### Notes
- Tests: launcher watchdog model 32/32 (host + ARMv7 container), launcher power-cut replay 96 cut states, installer
  140/140 (16 scenarios) (incl. a power-cut replay of a whole migration at every sync point), UI self-test 569/569 checks EN + DA (also as a
  32-bit ARM binary), switcher self-test 55/55.
- `bin/`: new `merakiplus_boot`; `merakiplus`, `switcher`, `merakiplus_sim_arm` and the host SIM builds rebuilt (official
  zig 0.17.1); `merakiplus_snd`, `tinj_hold`, `keyinj`, `grab_shim.so` and `audio/` unchanged. Release assets now include
  the launcher, switcher, `grab_shim.so`, the WAVs, `SHA256SUMS` and `MD5SUMS`.
- Not yet verified on a machine: the rollback and power-cut tests on hardware need the boot flag cleared and
  user-performed power cuts (next hardware session, docs/ota-plan.md phase 1b).

## [6.20] — 2026-11-22
**Steam while you brew.** The controller has always been able to run a latched steam session during a brew (verified on
a real machine with the stock UI, [docs/concurrent-brew-steam.md §11](docs/concurrent-brew-steam.md#11-hardware-verification-2026-11-22--pass));
only Meraki+'s own start guard prevented it. No frame, limit or timing changed: the steam start is the stock one.
### Added
- **HOLD·STEAM on the brew screen** (*HOLD·DAMP* in Danish). While a brew runs and no steam does, the action row is
  STOP BREW (left) + HOLD·STEAM (right). Hold it 650 ms to start steam on top of the brew; a tap only explains. Once
  steam runs, the slot is STOP STEAM. Each activity stops by itself (the shot by weight, the steam at the milk
  temperature) or with its own STOP.
### Changed
- **The activity guard allows a steam start while a brew is the only activity** (pre-infusion or extraction; no steam,
  grind or hot water running; word 24 = 0; no error; nothing in transit). Everything else stays blocked: steam during
  grind or hot water, any start during steam, and grind / brew / water during anything. A brew start during steam is
  not offered (the controller would accept it). No extra power or thermal guard in the UI: the controller runs only one
  heater at a time and keeps the steam heater off during a shot until the steam boiler is 10 °C below its target.
- During a brew STOP BREW is the left-hand 152 px pill (it was the single 216 px pill).
### Notes
- Self-test: new section *v6.20 steam during a brew* (10 checks); six older checks tap STOP BREW in its new place.
  555 checks EN + DA (also as a 32-bit ARM binary). 10 / 10 mutants caught (dev workspace).
- Simulator: the sim now accepts a brew start during steam and models the controller's heater rule during brew + steam
  (`sim/selftest.py` 195/0). Scenarios: `concurrent_brew_steam` (rewritten: a real UI start, 37 checks),
  `concurrent_brew_steam_stops` (35) and `concurrent_steam_blocked` (23), new; all fail against v6.19.
- `bin/`: `merakiplus` `6c0b4cd1…`, `merakiplus_sim_arm` `70bf2555…` and the host SIM builds rebuilt (official zig
  0.17.1); `merakiplus_snd`, `switcher`, `tinj_hold`, `keyinj`, `grab_shim.so` and `audio/` unchanged.
- **Verified on a machine** (after the release): HOLD·STEAM during a brew → both ran, the shot stopped by weight and
  the steam at 65 °C; STOP STEAM mid-brew → the shot carried on
  ([docs/concurrent-brew-steam.md §12](docs/concurrent-brew-steam.md#12-implementation-v620)).

## [6.19] — 2026-11-21
Two sound changes from user feedback. Display and audio only — no UART frame, guard, limit, timing, setting or helper
changed (`bin/merakiplus_snd` is byte-identical to v6.18.1).
### Changed
- **No ready tones at a power-on with hot boilers.** Switching on (or waking) a machine whose boilers are still hot
  played the jingle and both ready tones back-to-back. Now each boiler's ready latch is initialized from its state at
  the power-on: a boiler already at heat stage 100 counts as announced, so **only the jingle plays**. A ready tone now
  always means a real cold → hot transition during the session. The decision uses the last sample of a 1.5 s window
  after the power-on (words 62/63 are polled every 200 ms), so stale values right after a power-on or a wake do not
  count. Re-arming after real cooling, the per-boiler READY TONE setting, mute, OFF/sleep silence and skip-during-activity
  are unchanged. Details: [docs/audio.md §2.6](docs/audio.md#26-power-on-with-hot-boilers-v619).
- **Nicer sounds.** `tools/gen_audio.py` now synthesizes struck-instrument timbres instead of sine beeps: modal
  marimba (1 : 3.9 : 9.2) and glockenspiel (1 : 2.76 : 5.40) partials with mallet attack and damped release, a soft FM
  bell layer and a small Schroeder room. Start jingle = warm marimba + bell arpeggio G4-C5-E5-G5 → C6 (1.4 s); brew
  boiler = low marimba double note G4 → D5 (0.8 s); steam boiler = glockenspiel triple pip E6 G#6 B6 (0.75 s). Still
  our own, stdlib-only, 16 kHz mono; ~95 KB together (was ~68 KB). One sound theme (no theme selector).
### Notes
- Self-test: new section *v6.19 boot-already-hot* (8 checks: boot hot → jingle only; cold → hot during the session →
  tone; mixed; stale samples; 3 × sleep → wake hot → no ready tone; long sleep → tones again); the v6.18 sleep/wake and
  OFF → on checks now expect the jingle only. 545 checks EN + DA (also as a 32-bit ARM binary).
- Simulator: new scenarios `audio_boot_hot_no_ready` and `audio_cold_to_hot_ready`; `audio_power_cycle` now expects
  the jingle only (all three fail against the v6.18.1 build, as they should).
- `audio/` md5: `start.wav` `a03cb807d1cdb629b0614fdb740ec71d`, `ready_extract.wav` `4e7e24f18a80ee2f237e2f5ed220afbe`, `ready_steam.wav` `483d57ef122dc9929b004441fe01bc44` (`AUDIO_MD5`).
- `bin/`: `merakiplus` `e654f668…`, `merakiplus_sim_arm` `8624e302…` and the host SIM builds rebuilt (official zig 0.17.1);
  `merakiplus_snd`, `switcher`, `tinj_hold`, `keyinj`, `grab_shim.so` unchanged.
- **Not yet verified on a machine**: the new sounds need a listen on the real speaker and a power-on with hot boilers
  (docs/audio.md §3.5, steps 3 and 5). Update: run the installer again (`./install.sh`).

## [6.18.1] — 2026-11-21
Fix: **every Meraki+ sound was silent on a real machine** (start jingle, both ready tones, the SAVE test tone). Sound
helper only — no UART frame, guard, limit, timing or sound rule changed.
### Fixed
- **`bin/merakiplus_snd` could not load the display's MI libraries**: `merakiplus_snd --check` on the machine printed
  `dlopen: /lib/libmi_common.so: undefined symbol: sem_post` and exited 4. The display's `libmi_common.so` uses
  `sem_*` / `pthread_*` functions that glibc 2.28 has only in `libpthread.so.0`, without listing it as a dependency;
  the stock UI links libpthread, the v6.18 helper did not (`-ldl` only). The helper now puts `libpthread.so.0` into the
  global scope with `dlopen(RTLD_NOW | RTLD_GLOBAL)` before `libmi_common.so` (WAV and MP3 paths), and is also linked
  against it (DT_NEEDED). `--check` reports `MI_SYS + MI_AO ok (sem_post global)`.
  Details and the hardware findings: [docs/audio.md §3](docs/audio.md#3-hardware-check-and-the-v6181-fix).
### Hardware findings (2026-11-21)
- With the v6.18 helper started with `LD_PRELOAD=/lib/libpthread.so.0` the tones **play from the speaker**: the MI_AO path
  works and the speaker amplifier needs **no extra enable pin** beyond what `demo.sh` sets at boot. Checked with Meraki+
  as the only UI process; playback while a paused stock UI holds the MI layer is still untested.
### Notes
- `tests/hygiene.py` now fails if the helper lacks the `libpthread.so.0` dependency or stops loading it first.
- The v6.18 ARM-container check had used a stand-in `libmi_common.so`; it now runs the helper with byte-exact copies of
  the display's own glibc 2.28 and MI libraries (and its own loader), no LD_PRELOAD: v6.18 helper → exit 4 (`sem_post`),
  v6.18.1 helper → exit 0.
- `bin/`: `merakiplus_snd` `a7659c17…` (was `161edd2c…`); `merakiplus` `cc8c5fb9…`, `merakiplus_sim_arm` `ad29bce0…` and
  the host SIM builds changed only in the version string (V6.18.1). `switcher`, `tinj_hold`, `keyinj`, `grab_shim.so` and
  `audio/` are unchanged. All built with the official zig 0.17.1.
- Update: run the installer again (`./install.sh`); it replaces the helper (md5-gated) and the UI.

## [6.18] — 2026-11-21
Audio feedback: a start jingle and **one ready tone per boiler**. The stock audio path was reversed first:
[docs/audio.md](docs/audio.md). **Display and audio only** — no UART frame, guard, limit or timing changed.
### Added
- **Start jingle** once per power-on (button off → on, sleep → wake, or the first start after the display boots).
  Stock plays the audio track of `video/power.mp4` on its logo page.
- **A different tone per boiler**: one when the **brew boiler** is ready, another when the **steam boiler** is — so you
  can hear which one is hot. Stock plays a single *preheatCompele.mp3* once **both** are. The signal is the controller's
  own heat stage (words 62/63 = 100), the same one stock uses.
- Once per power-on; **re-armed only after the boiler has really cooled** (stage ≤ 30 or 15 °C below its ready point),
  so a shot or a steam session never re-triggers it. Silent while off or asleep; a tone that comes due while
  something runs is dropped, as stock drops sounds during its animations. A link glitch is not a power cycle.
- **SYSTEM › DISPLAY · SOUND** (the section was DISPLAY): **SOUND** off / on / stock sounds (the display's own mp3s,
  if present — never shipped), **READY TONE** off / brew boiler / steam boiler / both boilers, and **VOLUME** (config
  `volume`, moved here from STOCK UI because it now sets Meraki+'s sounds too; capped at 70 like stock, 0 = mute).
  SOUND and READY TONE live in Meraki+'s own file `/new_customer/merakiplus/sound`. SAVE plays a short test tone.
- **DIAGNOSTICS › SOUND**: mode · volume and the last sound with its outcome (*STEAM · PLAYED*, *BREW · SKIPPED*, …).
- **Our own sounds** in `audio/` (`start.wav`, `ready_extract.wav`, `ready_steam.wav`; 16 kHz mono, ~68 KB),
  synthesized by `tools/gen_audio.py` (stdlib only). No vendor audio is in the repository; `tests/hygiene.py`
  rejects any other audio/video file.
- **`bin/merakiplus_snd`** (8 KB, `src/merakiplus_snd.c`): plays one sound through the display's own
  `libmi_ao.so` with exactly the MI_AO calls and parameters of the stock player `libhvplayer.so` (48 kHz, 16 bit,
  6 × 2048 frames, the stock volume → dB curve). It is linked against the display's glibc and loads the vendor
  libraries at run time. The installer now also installs it and the three sounds (md5-gated) and revert removes them.
- Simulator: words 62/63 follow the controller's stage rules once the new `boilers` event switches the heat model on;
  state JSON `audio`; scenarios `audio_boiler_ready`, `audio_muted`, `audio_power_cycle`, `audio_sound_off`; the play
  viewer shows the audio log. Runner keys `sim.init_events` and `sandbox_files` (additive).
- Self-test section *v6.18 sound* (27 checks) + a host self-test of the helper against a mock MI_AO (14 checks).
  Meraki+ never signals the helper (it still contains no `kill()`): the helper ends itself after 8 s.
### Changed
- `docs/registermap.md`: words 62/63 are the boilers' **heat stage** (0/30/60/100), not a heating power.
### Notes
- `bin/`: `merakiplus` `3f02369b…`, `merakiplus_sim_arm` `d2266f86…`, the host SIM builds and the new
  `merakiplus_snd` `161edd2c…`. **All reproducible files are now built with the official zig 0.17.1 release**
  (the toolchain CI installs): the v6.17 files had been built with Homebrew's zig, which writes its own name into
  the ELF `.comment` section — same code, different md5 — so CI's new drift check had failed since it was added.
  `switcher` (`f2b95b16…`), `tinj_hold` and `keyinj` therefore changed in `.comment` only; `grab_shim.so` is unchanged.
  `tests/check_bins.py` now names each mismatch, `tests/hygiene.py` rejects Homebrew-built files in `bin/`, and
  `./build.sh` warns when it runs with a Homebrew zig.
- Verified locally: UI self-test 537/0 EN + DA (also as a 32-bit ARM binary), 9/9 mutation probes caught by the new
  section, simulator self-test 192/0, full E2E 43/43 scenarios (1126/1126 checks) in EN and in DA, installer tests.
- **Not yet heard on a machine** (install deferred). The helper resolves every MI symbol against the display's real
  `libmi_sys.so` / `libmi_ao.so` in a 32-bit ARM container; the speaker path itself is pending a hardware check
  ([docs/audio.md](docs/audio.md#3-hardware-check-and-the-v6181-fix)).

## [6.17] — 2026-11-20
A UX audit of v6.16 and the redesign it led to: [docs/ux-audit-v617.md](docs/ux-audit-v617.md).
**Display and navigation only** — no UART frame, guard, limit or timing changed.
### Changed
- **One page per function instead of settings lists.** Tap the grind / brew / water / steam icon on HOME
  to open that function's page: a live line (*BOILER 92.4° · GROUP 70°*, *ON THE SCALE 0.0 G*, …), **its own
  settings** as rows, and a labelled **HOLD TO START**. The three flat lists (BREW · STEAM·WATER · SYSTEM,
  31 rows) and the five-page swipe carousel are gone. Holding an icon still starts directly.
- **The icons say what they will do:** each carries its target underneath (*18.5 G · 36 G · 40 G · 65°*).
  A tap used to answer "HOLD THE BUTTON TO START"; now it opens the page that shows the hold.
- **SYSTEM (the gear) is five rows:** four sections — DISPLAY, CLOCK, SCHEDULE, STOCK UI — that open in
  place (one at a time, with a summary on the closed header: language, time, next pre-heat) and the
  **DIAGNOSTICS** entry with the machine's status. Everything that only matters to the stock UI (BOOT UI,
  SWITCH TO ORIGINAL NOW, VOLUME, STOCK LANG., WATER SHORTCUT, CLEANING REMINDER, AUTO TIME, PAUSE TIME) is
  together under STOCK UI.
- **DIAGNOSTICS is no longer a top-level page.** It opens from SYSTEM — or by tapping HOME's error /
  offline line, which now reads *CODE E2 · TAP FOR DETAILS*.
- **One way back:** the chevron at the top of every page and editor (the whole header is the target), or
  swipe right. No page dots, no invisible edge zones; HOME reacts to no swipe.
- **STOP is the biggest thing under the numbers:** 216 × 54 px (was 160 × 44), bright red with white text
  (4.0 : 1 against the background, was 1.9 : 1); two buttons 152 px wide in the larger font.
- **While something runs, the UI is on HOME.** A function's page is never left on screen during an
  activity (started there, on the machine or by the wand) — HOME is the one live view, where STOP is.
- **Less text.** No footer lines under the editors (*CONTROLLER NOW + CONFIG*, *STOCK UI: AFTER RESTART*,
  file paths); no *SAVED VALUE*; the target during an activity is said once and larger (*STOPS AT 36.0 G*
  instead of *IN THE CUP · TARGET 36.0 G* + *AUTO-STOP AT 36.0 G · OR STOP*); *≈BAR* instead of *≈BAR EST.*;
  one hint line on HOME instead of two; *SAVED · ACTIVE NOW* / *SAVED* instead of the config-file toasts; the
  notes no longer say *CONFIG ONLY* / *CMD8*. 25 string ids removed, 11 added; user-facing text −11 %.
- The read-only WATER SENSOR row is now the WATER page's live line.
### Added
- Editors with a long range show *HOLD - OR + TO GO FAST* while the value is untouched.
- A page's live line turns amber and names the reason when a start is blocked (*NO CONNECTION*,
  *GRIND = TIME · USE STOCK UI*, *CONFIG OUT OF SPEC · FIX FIRST*, …).
- Self-test section *v6.17 IA consolidation* (23 checks): every one of the 29 parameters is on exactly one
  page and its editor opens by real taps from HOME (≤ 3 taps) and closes the same way; HOLD TO START
  queues byte for byte the same frames as the icon hold; an activity started while a page is open brings
  HOME back; STOP size and contrast; no text below an editor's buttons; no *CONFIG ONLY* / *CMD8* /
  *· OR STOP* / *EST.* in any user-facing string.
- E2E scenarios `ia_navigation` (real taps through every level, no frame and no config write),
  `feature_page_start` (a brew started from the BREW page runs exactly like the `brew` scenario) and
  `activity_takes_home`.
- SIM contract (additive, `sim/CONTRACT.md` ADDENDUM v6.17): `tab` = home | grind | brew | water | steam |
  system | diag; `shown.live` / `shown.section`; `page` by those names; `layout` targets `gear`, `back`,
  `start`, `sec_*`, `diag` (the `edge_left` / `edge_right` targets are gone). The `page` keys are unchanged.
### Notes
- `bin/`: `merakiplus` `532e4bd5…`, `merakiplus_sim_arm` `dcb74a94…` and the host SIM builds rebuilt;
  `switcher`, `grab_shim.so`, `tinj_hold` and `keyinj` are byte-identical. **Not yet verified on a
  machine** (install deferred). What that leaves open is listed in §7 of the audit.

## [6.16, rebrand] — 2026-11-19
Same UI version (V6.16); rebuilt binaries. Pure rename + migration: control logic, UART frames,
thresholds and the EN/DA UI texts are unchanged except for the displayed file paths.
### Changed
- **One technical identifier: `merakiplus`.** The former internal code name is gone from the repository.
  Source `src/merakiplus.c`; binaries `bin/merakiplus`, `bin/merakiplus_sim_arm`,
  `bin/merakiplus_sim-linux-x86_64`, `bin/merakiplus_sim-macos-arm64`; install directory
  `/new_customer/merakiplus/` (backups, state, gesture switcher, `lang`, `schedule`, `cfgbak/`); environment
  variables `MERAKIPLUS_*`; build macros `-DMERAKIPLUS_SIM` / `-DMERAKIPLUS_PREVIEW`; `--is-merakiplus`;
  binary marker `@(#)merakiplus `; logs `/tmp/merakiplus_net.log`, `/tmp/merakiplus_boot.log`; console
  prefix `== merakiplus:`. The product name on screen stays **Meraki+**; the process stays `demo_v9`
  (vendor watchdog contract).
- On screen only the displayed paths change (`merakiplus/lang`, `merakiplus/schedule`); the schedule
  footer reads `MERAKI+ · merakiplus/schedule` (without ONLY/KUN, so the longer path fits). A frame-by-frame
  comparison of all 180 previews (EN + DA) against the pre-rename build differs only on those screens.
### Added
- **Migration of earlier installs** in `./install.sh` and `./revert.sh`: the old install directory is
  moved to `/new_customer/merakiplus/` (same filesystem, atomic; merged without overwriting if both
  exist), the old staged binary is dropped, a developer `run.sh` is rewritten, the `demo.sh` guard line
  is rewritten by install (removed by revert, which still restores `demo.sh` byte-identically), and older
  Meraki+ builds are recognised by their old marker. All legacy names live in one block of
  `tools/merakiplus.py` marked *legacy migration only*; `tests/hygiene.py` fails if the old identifier
  appears anywhere else (file names, text or binaries). `tests/test_installer.py` covers upgrade, revert,
  dry-run and purge of a pre-rename install.
- `bin/`: `merakiplus` `737db9b9…`, `merakiplus_sim_arm` `9c3f1fdb…` and `switcher` `a5e0128b…` rebuilt
  (renamed strings only); `grab_shim.so`, `tinj_hold` and `keyinj` are byte-identical to before.
  **Not yet verified on a machine** — an installed device migrates at its next `./install.sh`.

## [6.16] — 2026-11-19
### Fixed
- **TIME FORMAT was inverted relative to the stock UI.** The stock `update_time` (0xD5944) XORs the
  `timemode` flag with 1 before testing it, which the v6.1 analysis missed: **`timemode = 1` is 24-hour,
  `0` is 12-hour (A.M./P.M.)**. The row now shows 24-HOUR for 1, and SAVE writes the value the stock UI
  expects. See [docs/time-scheduling.md](docs/time-scheduling.md).
- The cmd8 frame's date/time is the stock UI's **local** time (UTC + time zone + DST); v6.14 sent the
  display's UTC clock.
### Added
- **Clock**: HOME's top line shows the local time (the display's system clock + the stock UI's time zone
  table + daylight saving when `sumtime` is on) in the stock format (`16:35` / `04:35 P.M.`); DIAGNOSTICS
  has a new *TIME · SCHEDULE* block (local time, zone + DST, auto off, last alarm sent, next pre-heat).
  Meraki+ never sets the clock; an unset clock shows the wordmark and arms no alarm.
- **Planned pre-heat**: *SYSTEM → PRE-HEAT / PRE-HEAT AT / REPEAT* (once, every day, Mon–Fri, Sat–Sun).
  The next occurrence is sent as the controller's wake alarm in the power-off cmd8 frame exactly as the
  stock UI does (`performSleepActions` → `powerOffSend(day, hour, minute)`); the controller's sleep task
  wakes and heats at that time. Additionally the alarm is pre-armed with the same frame while the machine
  is on (so a power-off Meraki+ does not see still wakes it) and re-armed after every wake / MCU restart;
  switching the schedule off sends 0/0/0. A one-time pre-heat switches itself off after use. HOME shows the
  next pre-heat.
- **Optional auto off** (*SYSTEM → AUTO OFF*: off / 15 / 20 / 30 / 60 min, default off): the stock power-off
  sequence after that long without touch, button or activity, with a warning 30 s before.
- The schedule and auto off are stored in Meraki+'s own file `/new_customer/merakiplus/schedule`; `config.json`
  is written only by the existing TIME FORMAT / DAYLIGHT SAVING rows. TIME ZONE (still locked) shows the
  stock label and place names.
- Simulator: scenarios `time_display`, `timemode_stock_parity`, `schedule_prewarm` (alarm wake end to end),
  scenario keys `ui_env` / `ui_only`, expect kind `state`, exact-alarm `time_sync_seen`; the sim's MCU now
  clears the cmd8 alarm on a CPU reset (as the firmware's RAM does).
### Changed
- `bin/`: `merakiplus` is now **V6.16**; `merakiplus_sim_arm` and the host SIM builds rebuilt with it; `switcher`,
  `tinj_hold`, `keyinj` and `grab_shim.so` unchanged. Not yet verified on a machine.

## [6.15] — 2026-11-19
### Fixed
- **The HOME graph resets when an activity starts.** Starting a grind, brew or hot water tares the
  weight to 0, but the graph's 40 s window still held the history from before the start — e.g. a
  200–300 g cup placed on the scale — so the y-autoscale was set by the cup step and the 0 → 36 g curve
  looked flat. The graph (pressure, weight and milk series, and the chart filters) now starts over on
  the idle → active transition and scales to the activity. A concurrent activity (steam/wand flush
  during a brew or grind) does not clear it, and a link gap does not count as idle. Within 1.5 s of the
  start a late tare or late weight-source latch re-anchors the curve once. Display only — regs, control,
  auto-stop and thresholds are unchanged; DIAGNOSTICS → GRAPH = RAW still shows raw values.
  See [docs/calibration.md §14.7](docs/calibration.md#147-the-graph-starts-over-when-an-activity-starts-v615).
- Full E2E gate: the replay scenarios now start the recording relative to the UI's clock instead of
  the simulator's process start, and the runner's event clock and the simulator's log times are
  re-based on the UI's clock origin, so the timing windows no longer drift on a busy CI runner.
### Added
- Simulator: scenario `graph_reset` (a cup at rest, then a brew with a wand flush) and the E2E expect kind
  `chart` (asserts the SIM build's `state.chart`, which now also reports `buf`, `w_max`, `w_top`,
  `resets`, `reanchors`, `act_ms`).
### Changed
- Source comments, developer console/log output, simulator tooling and scenario descriptions are now
  English (the bilingual EN/DA UI string table is unchanged). Switcher log lines are English
  (`GESTURE TRIGGERED`, `switch A→B DONE`); merakiplus's DIAGNOSTICS still recognises the old Danish lines.
- `bin/`: plain `./build.sh` builds of these sources (md5s in `bin/MD5SUMS`, `tools/merakiplus.py` and
  `bin/README.md`): `merakiplus` is now **V6.15**; `merakiplus_sim_arm` and the host SIM builds rebuilt with it;
  `switcher`, `tinj_hold` and `keyinj` rebuilt from the translated sources (unchanged by the graph fix);
  `grab_shim.so` is still the on-device-verified build. The new builds are not yet verified on a machine.

## [6.14] — 2026-11-18 — first public release
### Added
- **The physical power button**: the button node (k62091, `event1`) is read read-only; hold 1.5 s →
  the stock UI's power-off sequence (cmd8 time/alarm + word 0 = 0) byte for byte; TURNING OFF… /
  OFF / TURNING ON… screens; power-on sends PowerOn + awake as at boot. New DIAGNOSTICS block
  *POWER BUTTON*.
- **Public package**: `install.sh` / `revert.sh` (tools/merakiplus.py) with fingerprinting, md5
  gates, one-time backups, atomic install, activation without reboot, dry run and idempotency; mock
  display and installer tests; CI; documentation and disclaimer.
### Known limitation
- On the tested hardware the kernel/PMIC cuts the display's power after a ~0.8 s button hold, and the
  controller's sleep turns the display off ~8 ms after the power-off frame. Power off = power loss for
  the display, power on = cold boot (see [docs/power.md](docs/power.md)).

## [6.13] — 2026-11-18
- **Manual STOP for WATER, BREW and GRIND** (previously steam only). Stop frames reverse-engineered
  from the MCU firmware and cross-checked against the stock UI; feedback toasts (stopping… / stopped /
  no reply / not confirmed).

## [6.12] — 2026-11-18
- **Gesture switcher**: a single center hold ≥ 5 s (radius 40 px) in the stock UI switches to Meraki+
  without a reboot (previously: 3 × corner hold). Grab bypass (`grab_shim.so`, only in the stock
  process), automatic touch range, fallback gesture `SWITCHER_GESTURE=corner`.

## [6.11] — 2026-11-17
- **UX review**: plain language (no word numbers in user-facing text), better contrast, start hint,
  gear icon for settings, close chevron and swipe-down to cancel, swipe guard, DIAGNOSTICS grouped.

## [6.10] — 2026-11-17
- **Pressure as a ≈bar estimate** instead of "uncalibrated": linear anchor (pre-infusion 358 raw =
  config `preInfusionPressure`); pulses > 20 bar are shown as *PULSE*. See
  [docs/calibration.md](docs/calibration.md).

## [6.9] — 2026-11-17
- **Calm graph and stable weight**: median-3 + EMA, pulse gate, p90 scale, stable weight source
  (latch), weight dead band ±0.25 g. Display only — control logic uses raw values.

## [6.8] — 2026-11-17
- REVERT button removed from HOME (developer safeguard); the way back is
  *SYSTEM → SWITCH TO ORIGINAL NOW*.

## [6.7] — 2026-11-17
- **Network guard**: Wi-Fi is brought up at boot from slot B (the stock UI used to do it itself) and
  healed automatically; the password is never logged. Word 113 included in the boot push.

## [6.6] — 2026-11-17
- Phase logic based on a live recording (two steam paths, concurrent phases); **64-bit clock** (the
  32-bit clock wrapped after 24.86 days); config re-push when the controller restarts.

## [6.5] — 2026-11-17
- Meraki+ pushes the config to the controller at boot itself (also on the device); the RX parser can
  no longer stall (resync watchdog); new DIAGNOSTICS fields.

## [6.4] — 2026-11-17
- **SIM build and E2E**: the UI runs without hardware against a simulated controller; E2E suite green
  in English and Danish.

## [6.3] — 2026-11-17
- Product name **Meraki+**, splash and wordmark; all text in **English and Danish** (*UI LANGUAGE*).

## [6.2] — 2026-11-17
- **Boot slot integration**: *BOOT UI* (choose what starts) and *SWITCH TO ORIGINAL NOW* (runtime
  switch without a reboot); DIAGNOSTICS boot block.

## [6.1] — 2026-11-17
- **Settings parity**: limits, steps and labels aligned with the stock UI (address-proven), tiers,
  locked/estimated fields marked.

## [6.0] — 2026-11-16
- First v6: design for the round display (custom anti-aliased font, gauge, toasts, splash), 24
  settings with atomic `config.json` save + backup, GRIND/BREW/WATER/STEAM with hold-to-start, write
  queue with CRC + ACK + retransmit and frame policy, keep-awake fix (word 1 = 0), phases including
  steam/sleeping/error/offline.
