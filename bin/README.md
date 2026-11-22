# bin/ — prebuilt Meraki+ binaries (our code only)

| File | What | Built from |
|---|---|---|
| `merakiplus` | **Meraki+ v6.20** device binary (static ARMv7, musl). Installed as `/new_customer/demo_v9`. | `src/merakiplus.c` |
| `merakiplus_snd` | sound helper (ARMv7, dynamic against the display's glibc 2.28; `dlopen`s the display's own `libmi_*.so` at run time). Installed as `/new_customer/merakiplus/merakiplus_snd`, next to `audio/` | `src/merakiplus_snd.c` |
| `switcher` | gesture switcher daemon (center-hold 5 s → stock to Meraki+), only with `--gesture` | `boot/switcher.c` |
| `grab_shim.so` | tiny LD_PRELOAD shim so the switcher can see touches while the stock UI runs (only with `--gesture`) | `boot/grab_shim.c` |
| `tinj_hold` | touch injector used for on-device testing of the gesture | `boot/tinj_hold.c` |
| `keyinj` | key injector used for on-device testing of the power button | `src/keyinj.c` |
| `merakiplus_sim_arm` | SIM build (no hardware) as static ARMv7 | `src/merakiplus.c -DMERAKIPLUS_SIM` |
| `merakiplus_sim-linux-x86_64`, `merakiplus_sim-macos-arm64` | SIM builds for running `sim/` without zig | `src/merakiplus.c -DMERAKIPLUS_SIM` |

`MD5SUMS` lists every file; `tools/merakiplus.py` refuses to install anything whose md5 differs.
**No vendor firmware, libraries or scripts are included here or anywhere in this repository.**

## Provenance of the ARM binaries

**v6.20 (2026-11-22, steam during a brew):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM builds were rebuilt
from the v6.20 source with the official zig 0.17.1. The change is the steam start guard (a latched steam start is allowed
while a brew is the only activity) and the HOLD·STEAM button on the brew screen; the frames are the existing stock ones.
`merakiplus_snd`, `switcher`, `tinj_hold`, `keyinj` and `grab_shim.so` are byte-identical to v6.19; `audio/` is unchanged.
**Verified on a machine** (2026-11-22): `merakiplus` `6c0b4cd1…` installed with `./install.sh`; steam during a brew,
the independent auto-stops and STOP STEAM mid-brew checked live (docs/concurrent-brew-steam.md §12).

**v6.19 (2026-11-21, no ready tones at a power-on with hot boilers + nicer sounds):** `merakiplus`,
`merakiplus_sim_arm` and the two host SIM builds were rebuilt from the v6.19 source with the official zig 0.17.1. The
change is the sound engine's power-on latch (display and audio only, no UART frame changed) and the version string;
the new sounds are data (`audio/`, `AUDIO_MD5`), not code. `merakiplus_snd`, `switcher`, `tinj_hold`, `keyinj` and
`grab_shim.so` are byte-identical to v6.18.1. **Not yet verified on a machine**: covered by the UI self-test (EN + DA,
also as a 32-bit ARM binary), the simulator quick E2E suite and the six `audio_*` scenarios (EN + DA).

**v6.18.1 (2026-11-21, sound-helper fix):** `merakiplus_snd` rebuilt from the fixed source (`./build.sh`: `… -lpthread
-ldl`): it now NEEDs `libc.so.6` + `libpthread.so.0` + `libdl.so.2` and `dlopen`s `libpthread.so.0` RTLD_GLOBAL before
the display's `libmi_common.so`, which uses `sem_post` & co. without NEEDing libpthread (the v6.18 helper failed on a
machine with exit 4 — docs/audio.md §3). Checked against byte-exact copies of the display's glibc 2.28 + MI libraries
in an ARMv7 container. `merakiplus`, `merakiplus_sim_arm` and the host SIM builds changed only in their version string.

**v6.18 (2026-11-21, start jingle + per-boiler ready tones):** `merakiplus`, `merakiplus_sim_arm` and the two host
SIM builds were rebuilt from the v6.18 source; **`merakiplus_snd` is new** (`./build.sh`: `zig cc -target
arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 -O2 -s -fno-PIE -no-pie … -ldl`; bit-reproducible, checked by
`tests/check_bins.py`). It links only `libc.so.6` + `libdl.so.2` and loads the display's own `libmi_common.so`,
`libmi_sys.so`, `libmi_ao.so` (and, for the optional stock sounds, `libhvplayer.so`) at run time — no vendor code is
linked or shipped. The change is display and audio only (no UART frame changed; see CHANGELOG / docs/audio.md).
All reproducible files were built with the **official** zig 0.17.1 release (see below), so `switcher`, `tinj_hold`
and `keyinj` differ from v6.17 in the ELF `.comment` section only (no code change); `grab_shim.so` is byte-identical.
**Not yet verified on a machine**: covered
by the UI self-test (EN + DA, also as a 32-bit ARM binary), the helper's self-test against a mock MI_AO, a symbol
check of the helper against the display's real `libmi_sys.so` / `libmi_ao.so` in an ARMv7 container, and the
simulator scenarios `audio_boiler_ready`, `audio_muted`, `audio_power_cycle`, `audio_sound_off`.

**v6.17 (2026-11-20, UX audit + IA consolidation):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM
builds were rebuilt from the v6.17 source. The change is display and navigation only — one page per function
behind its HOME icon, SYSTEM in sections, DIAGNOSTICS behind SYSTEM, shorter texts, a larger STOP button (see
CHANGELOG / docs/ux-audit-v617.md). No UART frame, guard, limit or timing changed: the self-test proves that
HOLD TO START on a page queues byte for byte the same frames as the icon hold, and the frame-vector, policy,
boot-push, STOP, power and scheduling sections of the self-test are unchanged and green. `switcher`,
`tinj_hold`, `keyinj` and `grab_shim.so` are byte-identical to the previous build. **Not yet verified on a
machine** (install deferred); covered by the UI self-test (EN + DA, also as a 32-bit ARM binary), the
simulator quick E2E suite and the new `ia_navigation`, `feature_page_start` and `activity_takes_home` scenarios.

**Rebrand (2026-11-19, still V6.16):** the files were renamed (`merakiplus*`, formerly named after the
internal code name) and `merakiplus`, `merakiplus_sim_arm`, `switcher` and the two host SIM builds were
rebuilt with `./build.sh` from the renamed sources. The only differences are renamed strings (install
directory `/new_customer/merakiplus/`, `MERAKIPLUS_*` variables, the `@(#)merakiplus ` marker, log names
and the displayed `merakiplus/lang` / `merakiplus/schedule` paths); control logic and frames are unchanged.
`grab_shim.so`, `tinj_hold` and `keyinj` are byte-identical (their sources compile to the same output).
The pre-rename V6.16 builds were `2d10173cffb99807d7144619099b49b7` (device UI), `34e64f3b9705e5556e6e09bcbafa3909`
(ARM SIM) and `de0d4920e4e80be32e3a70798886a3c0` (switcher). **Not yet verified on a machine.**

**v6.16 (2026-11-19, time & scheduling):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM builds were
rebuilt from the v6.16 source: the clock on HOME/DIAGNOSTICS, the corrected TIME FORMAT (`timemode` 1 =
24 h, as the stock UI), the planned pre-heat (the stock cmd8 wake alarm, sent at power-off and pre-armed
while on) and the optional auto off (see CHANGELOG / docs/time-scheduling.md). The only new UART frame is
cmd8, which the stock UI and v6.14 already send. `switcher`, `tinj_hold`, `keyinj` and `grab_shim.so` are
byte-identical to the previous build. **Not yet verified on a machine** (install deferred); covered by the
UI self-test (EN + DA, also as a 32-bit ARM binary), the simulator quick E2E suite and the new
`time_display`, `timemode_stock_parity` and `schedule_prewarm` scenarios.

**v6.15 (2026-11-19, graph reset on activity start):** `merakiplus`, `merakiplus_sim_arm` and the two host SIM
builds were rebuilt from the v6.15 source. The only code change since the build below is display-only:
the HOME graph's ring buffer is cleared when an activity starts (see CHANGELOG / docs/calibration.md
§14.7); control logic, frames and thresholds are unchanged. `switcher`, `tinj_hold` and `keyinj` are
byte-identical to the previous build. **Not yet verified on a machine** (install deferred); covered by
the UI self-test (EN + DA, also as a 32-bit ARM binary), the simulator quick E2E suite and the new
`graph_reset` scenario.

**Since 2026-11-19 (English source text)** `merakiplus`, `merakiplus_sim_arm`, `switcher`, `tinj_hold` and
`keyinj` are plain `./build.sh` builds (zig 0.17.1, static, stripped with `-s`, so no build paths are
embedded) of the sources in this repository. The only source change since the on-device-verified v6.14
builds is that comments and developer log/console text were translated from Danish to English (the
EN/DA UI string table is unchanged); the code paths are the same, except that merakiplus's DIAGNOSTICS
switcher-log reader also recognises the switcher's new English log lines. These binaries therefore have
not been re-verified on a machine yet; behaviour is covered by the UI self-test (EN + DA), the switcher
self-test and the simulator E2E suite.

| File | md5 (this repo) | Built with |
|---|---|---|
| `merakiplus` (V6.20) | `6c0b4cd1091dd330d3248a86a54e017f` | `./build.sh` → `build/merakiplus` |
| `merakiplus_sim_arm` (V6.20) | `70bf25551b568b5cd608560f85060cc0` | `./build.sh` → `build/merakiplus_sim_arm` |
| `merakiplus_snd` (V6.18.1) | `a7659c176b8ef78385b0b612a69e81ca` | `./build.sh` → `build/merakiplus_snd` |
| `switcher` | `f2b95b16b33a9e1bc868a2f5b2414e6b` | `./build.sh` → `build/switcher` |
| `tinj_hold` | `e9806fb92e9b8a7e506a34639173c648` | `./build.sh` → `build/tinj_hold` |
| `keyinj` | `a0cbd4bc2a670abe0e250ec34a193843` | `./build.sh` → `build/keyinj` |
| `merakiplus_sim-linux-x86_64` (V6.20) | `c418d7fb39233d0c0a7693eb4d110765` | `zig cc -target x86_64-linux-musl -DMERAKIPLUS_SIM -O2 -static -s src/merakiplus.c -lm` |
| `merakiplus_sim-macos-arm64` (V6.20) | `58f1684f9440728f9ec92fc231cffa84` | `zig cc -DMERAKIPLUS_SIM -O2 -s src/merakiplus.c -lm` (Mach-O builds are not bit-reproducible) |

The ARM builds above are reproducible: running `./build.sh` on the same sources with the **official**
zig 0.17.1 release (the ziglang.org tarball CI installs) gives the same md5 on macOS and Linux,
independent of the build directory and cache location. A distribution zig such as Homebrew's produces the
same code but writes "Homebrew clang/LLD" into the ELF `.comment` section, so its md5 differs — that is
why CI's drift check failed for the v6.17 binaries (built with Homebrew's zig) until v6.18 rebuilt every
reproducible file with the official toolchain (`switcher`, `tinj_hold`, `keyinj` changed only in `.comment`).
Note that a zig cache shared with a Homebrew zig of the same version can hand its objects to the official
zig: use a separate `ZIG_GLOBAL_CACHE_DIR`. `tests/hygiene.py` rejects Homebrew-built files in `bin/`. The Linux SIM build (`-target x86_64-linux-musl
-static -s`) is reproducible the same way. CI enforces this on every push and PR
(`tests/check_bins.py`): the committed `bin/` must be byte-identical to fresh builds of these seven
reproducible targets, so a source change without a `bin/` rebuild fails CI. `grab_shim.so` (trimmed
verified build, see below) and `merakiplus_sim-macos-arm64` (Mach-O is not bit-reproducible) are
excluded from that check; `tests/hygiene.py` still md5-gates every file in `bin/`.

GitHub releases are maintained automatically by `.github/workflows/release.yml`: a `v*` tag push, a
manual dispatch, or a push to `main` that bumps `VERSION` past the latest release builds, runs the
full quick test gate and publishes the committed `bin/` assets (device binary, sound helper, ARM SIM, Linux SIM,
macOS SIM, `MD5SUMS`) with the CHANGELOG section as notes. Re-runs update the existing release
instead of creating a duplicate. The macOS SIM asset is the committed build from the table above —
CI does not rebuild it (no macOS runner needed), it ships exactly what `bin/MD5SUMS` lists.

`grab_shim.so` (`c42076c807c54d477a5868977404186d`) is still the exact build that was verified on a real
machine: its source change was comments/self-test text only and compiles to the same `.so` as before.
Like the earlier verified builds it has only the non-loaded trailer removed (debug info, symbol table
and section headers, which contained the original build machine's file paths); every loadable segment
is byte-identical to the verified build, which is why its md5 differs from a `./build.sh` build.

Earlier history: the V6.16 builds (after the rename) were `737db9b97dcea27764c682381b70dff2` (`merakiplus`) and
`9c3f1fdb3692283a4c9e1cbaebc05a56` (`merakiplus_sim_arm`). The translated-source v6.14 build of `merakiplus` was `eb4d9360756e391df4cc95f92b875c90`
(`merakiplus_sim_arm` `24c94f930935c7a276e70307c1f48882`). The v6.14 release shipped the on-device-verified builds (on-device md5 of `merakiplus`
`1dd63f42e4b0d649ef851660fc123a10`, shipped trimmed as `8d084f10094305e4be3701bc10fce5de`).
