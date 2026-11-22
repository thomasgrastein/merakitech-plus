# sim/ — E2E test environment for merakiplus (MerakiTech Gen 1 espresso display)

> **Repository layout (public edition):** the notes below were written in the author's working
> directory. In this repository: the UI source is `src/merakiplus.c`; the SIM build is built by
> `./build.sh` into `build/merakiplus_sim` (auto-detected; prebuilt: `bin/merakiplus_sim-*`); the switcher code
> lives in `boot/`; the config baseline (Wi-Fi fields replaced with placeholders) and the parity spec
> live in `sim/fixtures/`; the UART recordings for replay are stored compressed in
> `sim/fixtures/captures/*.csv.gz` (only the columns `ts,direction,raw_hex`). Other `artifacts/…`
> paths are historical.
>
> **Quick:** `./build.sh host && python3 sim/selftest.py && python3 sim/run_e2e.py --all --quick --parallel 4`
>
> Runner/selftest output and scenario descriptions are in English; scenario assertions that match UI
> toasts use the UI's own EN/DA strings.

**Date:** 2026-11-17 · **Owner:** the test work stream · **Status:** COMPLETE AND VERIFIED LOCALLY
**Policy:** local development only — **no contact with the physical device**. The UI work stream's
files (`merakiplus.c`, `UI_v6.md`, `REVERT.md`, `round_v6/`) were **not touched**. Python standard
library only (no Pillow → our own PNG routines in `png.py`, the same method as
`artifacts/write_test/fb2png.py`).

The whole UI can be tested without hardware: a **simulated MCU** (`mcu_sim.py`) speaks the real UART
protocol over a pty, a **scenario engine** drives timelines with fault injection, and a **runner**
(`run_e2e.py`) launches merakiplus's SIM build (or the reference client until it landed), injects touch,
samples UI state + the framebuffer and runs assertions.

---

## 1. Architecture

```
                        sim/reports/run-*/<scenario>/
                        ┌──────────────────────────────────────────┐
                        │ sandbox/            state.jsonl          │
                        │  customer/config.json  fb.bin            │
                        │  customer/new (flag)   snapshots/*.png   │
                        │  proc-mocks, tmp/      sim_assertions.jsonl
                        │  dev/input/event0.fifo report.json       │
                        └──────────────────────────────────────────┘
                              ▲            ▲             ▲
        ┌─────────────────────┘            │             └──────────────────┐
        │                                  │                                │
┌───────┴────────┐   evdev FIFO    ┌───────┴────────┐   JSONL ctrl   ┌───────┴────────┐
│  run_e2e.py    │ ──────────────► │  merakiplus SIM     │ ◄────────────► │  mcu_sim.py    │
│  (runner)      │   touch inject  │  build         │                │  (simulated    │
│                │ ◄────────────── │  OR            │ ◄────────────► │   powerMcu)    │
│  events/samples│   state+fb      │  ref_client.py │   pty (UART    │                │
│  assertions    │                 │  (reference)   │   AA 55+CRC)   │ 129-word model │
│  report JSON/MD│                 └────────────────┘                │ phase machine  │
│  contact sheet │                                                   │ fault injection│
└────────────────┘                                                   │ assertion log  │
        │                                                            │ REPLAY engine  │
        │  gesture stream (file mode)                                └───────┬────────┘
        ▼                                                                    │
┌────────────────┐                                                           │
│ switcher_host  │  (the REAL daemon code from boot/)                        │
└────────────────┘                                                           ▼
                                                              sim/fixtures/captures/
                                                              *.csv.gz
                                                              (REPLAY ground truth)
```

* **`mcu_sim.py`** — a complete MCU model: a 129-word register file initialized from the sandbox
  `config.json` (PARITY §1.3 incl. the byte packing of words 112/113/114), cmd3/cmd6/cmd8/cmd9 +
  0x83/0x86/0x88/0x89, band validation (words 0..31 + 112..114), trigger policy (26/27/30=0, otherwise
  REJECT + log), ACK echo with latency/loss/retransmit profiles, boot push detection, version
  handshake, a phase machine with synthetic sensor curves, fault injection and a JSONL assertion log.
* **`ref_client.py`** — a protocol-level reference client that implements **the same contract** as
  merakiplus's SIM build (`CONTRACT.md`): boot push, keep-awake (word1=0!), poll rotation, frame policy,
  ACK/retransmit (150 ms + 1), hold-to-start touch, settings SAVE, a minimal fb renderer (circle clip
  r234, phase colors, page/toast regions). It proved the sim + runner + assertion pipeline before the
  real UI's SIM build existed.
* **`run_e2e.py`** — runner: sandbox → sim → UI client → events → sampling → assertions → report
  (JSON + MD + PNG snapshots + contact sheet).
* **`scenarios/*.json`** — timelines (generated by `gen_scenarios.py`).
* **`selftest.py`** — protocol checks without the runner (CRC, chunking, write policy, auto-stop, boot
  push, packing, keep-awake, faults, ACK profiles, replay verify).
* **`proto.py`** — shared CRC/frame/parse/register name/parity/evdev helpers.
* **`png.py`** — dependency-free PNG (BGRA→PNG, thumbnails, contact sheet with a 5×7 bitmap font).
* **`CONTRACT.md`** — the precise contract for the UI's SIM build.

## 2. Quickstart

```sh
# 1. Self-tests (protocol level, ~3 s):
python3 sim/selftest.py

# 2. One scenario (~12–67 s):
python3 sim/run_e2e.py --scenario brew

# 3. All scenarios:
python3 sim/run_e2e.py --all

# 4. Against a specific UI binary (build/merakiplus_sim is auto-detected):
python3 sim/run_e2e.py --all --ui-bin build/merakiplus_sim

# Reports: sim/reports/run-<timestamp>/report.md (+ report.json, contact_sheet.png per scenario)
python3 sim/run_e2e.py --list          # list of scenarios
```

Playing with the sim on its own:

```sh
python3 sim/mcu_sim.py --pty-file /tmp/pty --ctrl /tmp/simctrl --assert-log /tmp/a.jsonl
# in another terminal: speak the protocol against $(cat /tmp/pty), e.g. with sim/selftest.py's RawClient,
# or inject: echo '{"cmd":"inject","event":{"type":"fault","e1":32,"dur":5},"id":1}' | nc -U /tmp/simctrl
```

For an interactive browser view of the simulator, see [PLAY.md](PLAY.md).

## 3. Scenarios

| Scenario | Duration | What it proves |
|---|---|---|
| `idle` | 12 s | poll rotation, keep-awake, boot push, version handshake, circle/color/region checks |
| `sleep_wake` | 28 s | word1=1 → drain (ws 0x0001, cooling boilers) → word1=0 → HEAT → READY |
| `grind` | 17 s | **touch hold-to-start (650 ms)** → bit6 → weight ramp → auto-stop ~187/185 (live) → word6 consumed |
| `brew` | 40 s | pre-infusion plateau word51≈358 (6 s) → extraction ≈1346 → predictive stop 352 (live) → after-drip 364 → word22 cleared by the MCU (+0.16 s) |
| `hotwater` | 15 s | sub9 len11 → ws 0x030B → dispensing → auto-stop word39≥word17 → word16 cleared |
| `steam_auto` | 40 s | display steam (sub24 len8, latch) + wand switch → ws 0x4121 → milk ramp → **auto-stop word48≥word25 without a stop write** (word24 MCU clear +0.18 s) |
| `steam_wand_flush` | 34 s | **v6.6:** wand flush DURING a brew → ws 0x5327 (union), word24=0/word46=1, **no auto-stop >650**, release stop, the brew weight continues, no flush writes |
| `steam_auto_overshoot` | 60 s | **v6.6:** cold milk (116) → ramp → auto-stop 650 → **overshoot peak ~689 (+39) ~11 s after the stop**; cur1 ~7340 / word51 ~16650 during steam |
| `concurrent_brew_steam` | 66 s | **v6.20:** UI steam start DURING a brew (pre-infusion) → `0x5327`, steam heater only below target −10 °C (bit 13, `0x7327`), brew stops by weight, steam continues and stops by milk; HOLD·STEAM → STOP STEAM |
| `concurrent_brew_steam_stops` | 30 s | **v6.20:** STOP STEAM mid-brew (brew continues) · steam again via direct word24=1 · STOP BREW while steaming (steam continues) · STOP STEAM |
| `concurrent_steam_blocked` | 30 s | **v6.20:** UI steam refused during grind, hot water and an E2 during a brew — no word24 write |
| `steam_manual_stop` | 12 s | display steam + wand switch → manual stop sub24 word24=0 before the setpoint |
| `stop_water` | 14 s | **v6.13:** WATER (auto) → manual stop sub16 word16=0 in the middle of dispensing → ACK, bits 1+3+9 cleared ≤ 0.5 s, weight held + post/drip −11 like the auto-stop, stop.ok=1 |
| `stop_brew` | 20 s | **v6.13:** BREW → manual stop sub22 word22=0 in the middle of the extraction → ACK, valve pulse 0x0105, after-drip +12 like the auto-stop, stop.ok=1 |
| `stop_grind` | 14 s | **v6.13:** GRIND → manual stop sub6 word6=0 in the middle of the ramp → ACK, bit6 cleared, the weight HOLDS (no ramp towards the target), cur2 decay, stop.ok=1 |
| `stop_control_auto` | 15 s | **v6.13 control:** WATER without a stop word → no stop writes, word16=1 during dispensing, MCU auto-stop at word39≥word17 (unchanged) |
| `power_off_wake` | 55 s | **v6.14:** HOLD 1.6 s → cmd8 (alarm 0/0/0) + sub0 len2 [0,0] ACK → **sleep** (silent UART, registers frozen, keep-awake paused for 34 s) → sim button hold → **reboot to factory defaults** (word11=920/word114=50) → 0x89 → **re-push** restores the config → PowerOn + HOME + READY |
| `power_during_activity` | 42 s | **v6.14:** a power HOLD during a brew is **blocked** (toast, no frames — the stock UI never stops an activity on power-off); the brew auto-stops undisturbed; a new full hold after the brew powers off normally |
| `graph_reset` | 46 s | **v6.15 (merakiplus):** a 250 g cup on the brew scale at rest → BREW (the MCU tares word 39) → wand flush during the extraction → `state.chart`: no reset at rest, ONE reset at the start, ring cleared (`buf` ≤ 35 at 2–3 s), kept during the flush (`buf` ≥ 90), weight axis relative to the brew (`w_top` ≤ 420, not 2700). The REF profile replaces the 6 `chart` checks 1:1 |
| `time_display` | 40 s | **v6.16 (merakiplus-only):** HOME clock = SoC UTC + zone #48 (+ DST when sumtime=1): 16:35 (24 h) → 04:35 P.M. (timemode=0) → a summer clock jump: 12:00 P.M. with DST, 11:00 A.M. without → 11:00 (timemode=1); no cmd8 sent |
| `timemode_stock_parity` | 30 s | **v6.16 (merakiplus-only):** config timemode=1 = 24-HOUR row + 24 h clock (stock `update_time` XORs with 1); SAVE 12-HOUR writes 0 + A.M./P.M. clock; SAVE 24-HOUR writes 1 |
| `schedule_prewarm` | 80 s | **v6.16 (merakiplus-only):** PRE-HEAT daily 06:30 → pre-armed cmd8 alarm [18,6,30] → HOLD → power-off with the same alarm → deep sleep → the sim RTC reaches 06:30 → **alarm wake** (factory reset) → re-push → ON → re-armed [19,6,30]; PRE-HEAT OFF → 0/0/0 |
| `ia_navigation` | 34 s | **v6.17 (merakiplus-only):** the new IA with REAL taps (layout targets): cup icon → BREW page (6 rows, live line) → BREW TEMP editor → CANCEL → header chevron → HOME; gear → SYSTEM (closed: 5 rows) → SCHEDULE opens/closes → PRE-HEAT AT editor; DIAGNOSTICS row → DIAGNOSTICS → back to SYSTEM → HOME. No frame, no config write (45 checks) |
| `feature_page_start` | 43 s | **v6.17 (merakiplus-only):** a brew started from the BREW page (tap the icon, then HOLD TO START 0.8 s): the UI returns to HOME with STOP BREW and the brew runs to its auto-stop — the `brew` scenario's expectations, shifted by 2.15 s |
| `activity_takes_home` | 22 s | **v6.17 (merakiplus-only):** the GRIND page is open when the wand starts steam on the machine → the UI slides to HOME (the live view); SYSTEM opened during the steam stays; afterwards the WATER page opens normally |
| `audio_boiler_ready` | 44 s | **v6.18 (merakiplus-only):** cold display boot + cold boilers (`boilers` init event): START JINGLE once → the EXTRACT boiler's tone when word 62 reaches stage 100 (~11 s) → the STEAM boiler's own tone at ~21 s → cool-down → re-armed → extract tone again; no UART write (27 checks) |
| `audio_muted` | 24 s | **v6.18 (merakiplus-only):** config volume 0: the jingle and both boiler events are detected and logged `muted`, nothing is played |
| `audio_power_cycle` | 46 s | **v6.18 (merakiplus-only):** hot boilers at a cold boot → jingle + tones; button OFF → silent; wake → jingle + tones again; READY TONE = steam only (sound file via `sandbox_files`) |
| `audio_sound_off` | 40 s | **v6.18 (merakiplus-only):** ctrl SAVE `sound` = 0 → both boiler events logged `off`; SOUND on + READY TONE = brew → only the extract tone after the next cool-down |
| `fault_e1/e2/e3` | 12–16 s | error bits words 34/35/36 → error UI + safety stop of the activity |
| `crc_garbage` | 12 s | garbage stream + CRC corruption → resync, the link is kept |
| `no_ack` | 12 s | **SAVE is blocked** (retransmit→FAIL, config unchanged) → recovery with the normal profile |
| `offline` | 14 s | link silent → OFFLINE after 1.5 s → recovery. A real pty close (`link closed` → a new pty on reopen) is proven in selftest T10. |
| `out_of_spec` | 14 s | config out of spec → start blocked, the sim logs boot push violations → fix → start |
| `language` | 10 s | language is config-only (EN index 0..10; **Danish is not among the stock UI's 11 languages** — merakiplus's own UI language is a separate setting, a documented deviation) |
| `boot_flag_toggle` | 10 s | `/customer/new` touch/rm in the sandbox, no MCU writes |
| `runtime_switch` | 10 s | "SWITCH TO ORIGINAL NOW" (dry run): bye view → back, flag unchanged |
| `gesture_switcher` | 12 s | **the REAL `switcher_host`** in file mode: trigger gesture → trigger; incomplete gesture → none; the UI ignores the gesture |
| `settings_save_matrix` | 67 s | **min/max/step/original per parity_spec parameter** (188 checks): word-for-word MCU writes, byte packing 112/113, config persistence, config-only without writes, locked fields rejected. Service triggers (backflush/drain/calibration/reboot/self-test) and fields without limits (timezone/preset/versions/Wi-Fi strings) are deliberately left out — they are not UI settings (PARITY §2.3/§5). |
| `replay_v3` | 42 s | the v3 grind+brew trace served as the truth (8× speed) |
| `replay_v2` | 20 s | the v2 hot water trace (2 dispenses, 8×) |
| `replay_session` | 20 s | the session sleep/drain trace (8×) |
| `replay_steam_live` | 34 s | the **LIVE steam+grind+brew+flush session** (2026-11-17) as the truth (8×): auto-stop, flush 0x5327, concurrent phases |

### Adding a scenario

Scenarios are JSON files in `sim/scenarios/` (they can be written by hand or generated —
`gen_scenarios.py` is the reproducible source):

```json
{
  "name": "my_scenario",
  "description": "a short description",
  "duration_s": 20,
  "config": {"brewTemper": 880},
  "sim": {"profile": "normal", "seed": 1, "require_wand": true},  // require_wand: legacy no-op since v6.6
  "replay": {"csv": "sim/fixtures/captures/uart_live_v3.csv.gz", "mode": "serve", "speed": 8},
  "events": [
    {"t": 2.0, "target": "ui", "cmd": "action", "name": "brew"},
    {"t": 2.0, "target": "ui", "cmd": "touch", "x": 350, "y": 160, "ms": 700},
    {"t": 2.0, "target": "ui", "cmd": "save", "param": "brew_temp", "value": 880},
    {"t": 3.0, "target": "sim", "event": {"type": "fault", "e1": 32, "dur": 5}},
    {"t": 3.0, "target": "sim", "event": {"type": "garbage", "dur": 2}},
    {"t": 3.0, "target": "sim", "event": {"type": "link", "state": "silent"}},
    {"t": 3.0, "target": "sim", "event": {"type": "wand", "on": true}},
    {"t": 3.0, "target": "sim", "event": {"type": "wand_down"}},
    {"t": 6.0, "target": "sim", "event": {"type": "wand_up"}},
    {"t": 3.0, "target": "sim", "cmd": "ack_profile", "profile": "no_ack"},
    {"t": 4.0, "target": "runner", "op": "snapshot", "label": "my_picture"},
    {"t": 5.0, "target": "runner", "op": "config_set", "field": "brewTemper", "value": 870, "reload": true},
    {"t": 6.0, "target": "runner", "op": "gesture", "name": "positiv", "presses": 3, "inject_ui": true}
  ],
  "expect": [
    {"kind": "phase", "t": 4, "window": 1.5, "value": "PRE"},
    {"kind": "phase_hold", "t0": 3, "t1": 8, "value": "EXT", "min_ratio": 0.9},
    {"kind": "phase_seq", "t0": 0, "t1": 40, "values": ["READY","PRE","EXT","READY"]},
    {"kind": "word", "word": 22, "t": 33, "window": 1.5, "value": 0},
    {"kind": "word_hold", "word": 51, "t0": 10, "t1": 30, "min": 1250, "max": 1500},
    {"kind": "word_min_seen", "word": 48, "t0": 30, "t1": 35, "min": 650},
    {"kind": "work_sta_seen", "t0": 9, "t1": 31, "value": "0x1307", "mask": "0x2C00"},
    {"kind": "sim_log", "assert": "write_words", "sub": 11, "words": [880], "t0": 2, "t1": 3},
    {"kind": "sim_log", "assert": "activity_stop", "activity": "brew", "reason_contains": "weight", "t0": 29, "t1": 34},
    {"kind": "config", "field": "brewTemper", "t": 3, "value": 880},
    {"kind": "circle"},
    {"kind": "point", "t": 6, "point": [240, 8], "color": [98, 222, 168], "tol": 30},
    {"kind": "region", "t": 6, "rect": [200, 200, 80, 80], "color": [24, 104, 92], "tol": 30},
    {"kind": "replay_truth", "word": 39, "t0": 17.7, "t1": 18.05, "val_min": 320, "val_max": 370, "signed": true}
  ],
  "universal": {"keepawake": true, "ranges": true, "no_cmd6_fail": true}
}
```

(`"positiv"` is a literal gesture-result name matched by the runner — "positive"; `"weight"` is matched against the sim's stop reason.)

**Expect kinds:** `phase`, `phase_hold`, `phase_seq`, `word`, `word_hold`, `word_min_seen`,
`work_sta_seen`, `stop_ok` (v6.13: `state.stop.ok/fail`), `page`, `view`, `toast`, `lang`, `config`,
`flag`, `save`, `save_rejected`, `config_oor_min`, `exit_code`, `crc_bad_min`, `offline_seen`,
`link_up_at`, `cmd6`, `circle`, `region`, `point`, `gesture`, `contract`, `replay_truth`, `sim_log`,
`power`, `power_hold`, `power_counter` (v6.14), `chart` (v6.15: `state.chart.<field>` within `[min,max]` over
`[t0,t1]`, every sample or `"mode": "any"`; merakiplus SIM build only — the reference client has no chart).
**sim_log asserts:** `boot_push`, `version`, `keepalive_ok`, `no_reboot`, `no_calib`,
`no_band_rejects`, `range_max`, `range_min`, `write_words`, `ack_words` (v6.13: ACK echo sub+words),
`no_write_word`, `no_writes_between`, `activity_start`, `activity_stop`, `sleep_write_seen`,
`awake_write_seen`, `acks_dropped_min`, `writes_min`.
**Universal checks** (always run unless disabled): circle check on all snapshots, SIM contract (state
fields + fb size), no reboot/calibration trigger writes, no band violations, 0 range violations,
keep-awake ≤ 30 s, boot push + version handshake, exit 0, 0 cmd6 errors.

Then run: `python3 sim/run_e2e.py --scenario my_scenario`.

## 4. List of assertions (what the environment catches)

1. **Protocol:** CRC-16/MODBUS on all frames; chunked reads ≤ 32 B; resync after garbage; cmd9 byte
   LEN; cmd8 fixed 15 B; boot push (sub1 len30 + sub112 len3) on connect; 0x89→cmd9 handshake.
2. **Write policy:** bands 0..31 + 112..114 (everything else is rejected + logged); trigger words
   26/27/30=0 (rejected + logged); parity limits per word (logged as `range_violation` — the MCU has
   no checks, the UI is the only guard); ACK = exact echo.
3. **Keep-awake:** word1=0 is expected every ≤ 30 s → `keepalive_gap` in the sim log (the runner fails
   the scenario).
4. **Phase curves:** work_sta per phase (idle 0x0101, grind 0x0141, brew 0x1107/0x1307, water 0x030B,
   steam 0x4121, sleep 0x0001); pre-infusion plateau ≈358 for ~6 s; extraction ≈1346; predictive
   weight stop + after-drip; auto-stop at the target (water/brew) and the milk setpoint (steam);
   MCU-owned flag clearing (words 16/22/24).
5. **UI state:** phase/page/toasts/language/boot flag/config-oor per frame against expectations; SAVE
   blocked when ACKs are missing (config unchanged!); hold-to-start; locked fields rejected.
6. **Framebuffer:** circle check (nothing outside r240, corners black, max radius ≤ 234.71 — ported
   from `check_circle.py`); color/region checks (phase ring, page region, background, toast).
7. **Settings matrix:** word-for-word writes against `parity_spec.json` (min/max/step/original), byte
   packing 112/113 with a running config state, config-only parameters without MCU writes.
8. **Gesture:** the real switcher daemon is triggered by the trigger gesture and not by an incomplete
   one; the UI starts no activity from the gesture.
9. **Replay:** the sim's replies are matched frame for frame against recorded registers (see the
   tolerances below).

## 5. REPLAY mode + tolerances

Two modes (`--replay <frames.csv> --replay-mode serve|verify`):

* **serve** (the scenarios `replay_v2/v3/session`): the recording is played back as **the truth** on
  live polls (the register mirror is driven by recorded RX frames; recorded writes are applied). Live
  client writes (boot push/keep-awake) are layered on top — in the session scenario keep-awake gives
  brief SLEEP→READY flickers (documented; the expectation uses `min_ratio`). The speed is controlled
  with `speed` (the scenarios use 8×). At 8×, brief peaks (e.g. the weight peak 405 for 1–2 frames)
  can fall between the UI's polls → **`replay_truth` assertions** ask the sim for the recording's
  min/max in the window (aliasing-safe) instead of requiring the UI mirror to have seen exactly the
  peak.
* **verify** (selftest T11 + `mcu_sim.py --replay-mode verify`): the sim's **own phase machine** is
  driven by the recorded writes at the recorded times, and the replies are compared **frame for
  frame** with the recording. Raw strace logs (`uart_trace.log`) are first decoded to `frames.csv`
  with the existing decoder (`decode_uart_v3.py <log> --fd 13 --outdir <dir>`, in the author's
  working environment; the recordings in this repo are already decoded). Result:

| Recording | Samples | Match | Exact errors | work_sta sequence | Timing |
|---|---|---|---|---|---|
| v3 (grind+brew, 485 s) | 138,355 | **99.52 %** | 0 | ✓ identical | ✓ ±1.5 s |
| v2 (hot water ×2, 365 s) | 106,778 | **97.89 %** | 0 | ✓ identical | ✓ ±1.5 s |
| session (sleep/drain, 729 s) | 209,538 | **98.93 %** | 0 | ✓ identical | ✓ ±1.5 s |

**Tolerance classes** (documented deviation between the synthetic curve and the recording):

| Class | Words | Tolerance |
|---|---|---|
| `exact` | setup/flags/static (0..32 etc., 34-37, 43-46, 54, 56-57, 59-67, 73-114, 128) | **0** — must match exactly |
| `event` | 3, 6, 9, 16, 22, 24, 33 (work_sta) | time-shifted match ±0.75 s (trigger consumption is asynchronous); work_sta is masked for idle pulses (bits 10/11/13) |
| `sensor` | 40, 41, 42, 47, 48, 49, 68-72 | \|Δ\| ≤ 15 (1.5 °C / ADC units); **physical events are synced** (word48 milk refill/probe: rec<sim−20 or dev>60; word68 water consumption: dev>40) — like the weight physync |
| `weight` | 38, 39, 53, 55 | \|Δ\| ≤ 30 (3 g) or time-shifted ±0.75 s; **physical jumps** (>100 calibrated / >40 raw cells: cup removed, tare glitch) are synced from the truth (+ the raw cell base is adjusted) and counted separately (`physical_sync`) |
| `fast` | 50, 51, 52, 58 | \|Δ\| ≤ max(400, 35 %) or time-shifted ±15 %; **stochastic idle maintenance cycles** (min<200 vs max>200 at idle — small 400–1000 and large >1000) are tolerated on both sides (the phase is not deterministic in the original) |

**PASS criteria:** exact words 100 %, overall match ≥ 92 %, the work_sta sequence (masked) identical
in order, all transitions within ±1.5 s. **v6.6 calibration (STEAMLIVE.md):** grind start +0.39 s,
brew start +0.17 s, water start +0.18 s, steam start +0.30 s; weight rates 19.4/18.6/63.8 units/s
(brew: ramp from pre+2.75 s, stop at target−8, after-drip +12); sleep cooling 0.1125/0.07 units/s;
steam ramp 14 units/s mid rate (cold start 9.8 / final phase 12.6), **auto-stop overshoot +39 units →
peak ~11 s after the stop**, cur1 ~7340 / word51 ~16650 during steam, s boiler drop/recovery + flush
11.6 units/s, wand flush (latch=0) without auto-stop. **Verify steam-live: 99.608 % (PASS)**; short
bit9 pre-pulses are filtered from the ws sequence (stochastic placement: v3 at 1.14 s vs live at
4.97 s). See `reports/v6.6-calibration/REPORT.md`.

## 6. What the environment does NOT cover

* **Real fb timing on the Cortex-A7** (SSD202): render time, page-flip tearing, mmap latency and
  LVGL-like compositor effects are not modeled. The SIM build renders to a buffer; `frame_ms` is
  measured on the host.
* **Real touch electronics:** the tw31xx controller's noise, ghost touches, multi-finger and raw range
  calibration are not modeled (the evdev format is real, but the events are synthetic).
* **MCU bootloader/OTA:** the `CC DD` OTA protocol, flash staging, the bootloader copy and the V120
  check are **not** simulated (only the `AA 55` link). OTA testing requires a separate deliverable.
* **Physical sensor physics:** the curves are calibrated emulations, not a thermodynamic/weight model.
  Word51's identity (pressure vs. pump drive) and bar scale are **unresolved** (BREW §7) — the sim
  uses the raw plateau values (358/1346), not bar.
* **The shape of the steam curve:** now **calibrated live** (STEAMLIVE.md §5, capture 2026-11-17):
  ramp 1.403 °C/s (mid rate), auto-stop at word48≥word25, overshoot +3.9 °C (peak ~11 s after the
  stop), word24 MCU clear +0.18 s. Remaining deviations in verify (0.39 %): the milk probe's variable
  cooling/flush jet (unobservable physics) and the water level's maintenance purges — see
  `reports/v6.6-calibration/REPORT.md` §8.
* **The MCU's exact stop algorithm** (predictive weight compensation) is approximated (stop at
  target−8 + after-drip +12, calibrated live; brew #1 in replay is seeded with a quadratic ramp fit
  from the recording).
* **Wi-Fi/NFC/preset apply/low-level time sync (cmd8 DST logic)** are out of scope (cmd8 is, however,
  ACKed protocol-correctly).
* **Replay aliasing:** at high playback speed the UI mirror can miss brief peaks (see `replay_truth`
  above).
* **merakiplus's real graphics/layout** with the reference client: the reference client's fb is a simple
  emulation (phase ring + buttons + regions). The circle/color/region checks only become truly
  meaningful with the real UI's SIM build — the contract ensures that the same assertions then apply to
  the real UI.

## 7. Verification status (DELIVERABLE 5)

| Check | Result |
|---|---|
| `sim/selftest.py` (CRC, chunking, write policy, auto-stop, boot push, packing, keep-awake, faults, ACK profiles, offline, cmd8, replay) | **107 PASS / 0 FAIL** |
| Replay verify v3 (BREW trace) | **PASS** (99.52 %, exact 100 %, ws sequence + timing OK) |
| Replay verify v2 + session | **PASS** (97.89 % / 98.93 %) |
| `run_e2e.py --all` with the reference client | **22/22 scenarios PASS, 553/553 checks** (definitive run: `sim/reports/run-20261117-152210/report.md`) |
| Settings save matrix (parity_spec min/max/step) | **188/188 checks** |
| Gesture against the real `switcher_host` | trigger ✓ / incomplete gesture no ✓ |
| No device contact; the UI work stream's files untouched; `sim/fixtures/config.json` md5 = baseline `<config-md5>…` | ✓ |

**What could only be run once the UI's SIM build landed:** the same scenarios against the real UI
(`--ui-bin`), i.e. real merakiplus graphics in the circle/color/region checks, the real stepper/settings UI
in the matrix scenario, the real BOOT UI/SWITCH buttons, the real DIAGNOSTICS page. The contract
(`CONTRACT.md`) is designed so the scenarios run **without changes**; the reference client is the
oracle for field names and timing.

### v6.6 status (2026-11-17, STEAMLIVE calibration)

| Check | Result |
|---|---|
| `sim/selftest.py` | **120 PASS / 0 FAIL** (107 + wand/latch/overshoot/concurrency + steam-live verify) |
| Replay verify **steam-live** (the calibration target) | **PASS 99.608 %** (exact 100 %, ws sequence + timing OK) — was 92.59 % FAIL |
| Replay verify v3 / v2 / session | **PASS** (99.83 % / 99.90 % / 100.00 %) |
| `run_e2e.py --all` against merakiplus V6.6 (raw EN/DA) + REF | **25/25 scenarios · 644/644 checks** |
| steam_auto deterministic 30× | **30/30 PASS** |
| New scenarios | `steam_wand_flush` (31) + `steam_auto_overshoot` (24) |

Details: `sim/reports/v6.6-calibration/REPORT.md` + CONTRACT.md ADDENDUM v6.6-calibration.

### v6.13 status (2026-11-18, manual STOP — SIM_HANDOFF)

| Check | Result |
|---|---|
| `sim/selftest.py` | **150 PASS / 0 FAIL** (120 + T12 manual STOP + 4 stop frame vectors) |
| Manual stop in mcu_sim | word16/19=0 (water, **also auto mode**), word22=0 (brew), word6=0 (grind) → the same stop path as the auto-stop (post/drip/decay), ACKed |
| Replay verify v3 / v2 / session / steam-live | **Unchanged** (99.83 / 99.92 / 100.00 / 99.64 %) — the recordings have no stop writes |
| New scenarios (EN + DA + REF) | `stop_water`/`stop_brew`/`stop_grind`/`stop_control_auto`: **4/4 · 100/100 checks in each client** |
| `run_e2e.py --all --quick --parallel 4` | **5/5 · 97/97** |
| Regression (gesture/steam/brew/hotwater/grind/replay-serve) | **8/8 · 178/178** |
| The UI's own stop scenarios (v6.13, DA) rerun | **3/3 · 51/51** — end in READY (before: the activity kept running) |

Details: `sim/reports/v6.13-sim/SUMMARY.md` + CONTRACT.md ADDENDUM v6.13.

### v6.14 status (2026-11-18, MCU sleep/wake — SIM_HANDOFF)

| Check | Result |
|---|---|
| `sim/selftest.py` | **187 PASS / 0 FAIL** (150 + T13 sleep/wake: 37 checks) |
| Sleep in mcu_sim | word0=0 → ACK + ≤ 0.5 s to **silent sleep** (work_sta=0, outputs 0, activities stopped safely, registers frozen, keep-awake paused); cmd3/cmd6/cmd8 are dropped |
| Wake | button hold **≥ 0.8 s** or **RTC alarm** (cmd8) → CPU reset to the **factory start values** + 0x89 round → boot push/re-push expected (config restored) |
| New scenarios (EN + DA, v6.14 SIM + REF) | `power_off_wake` (38 checks) + `power_during_activity` (29 checks): **green in all clients/languages** |
| `run_e2e.py --all --quick --parallel 4` | **5/5 · 97/97** (v6.14 SIM build) |
| Regression (stop×4/gesture×2/steam×4/brew/grind/hotwater/sleep_wake/replay-serve×4 = 18, EN+DA) | **18/18 · 394/394 in each language** — the recordings have no word0=0 writes; sleep never occurs in replay |

Details: `sim/reports/v6.14-sim/SUMMARY.md` + CONTRACT.md ADDENDUM v6.14-sim.

### v6.18 status (2026-11-21, sound — heat stages words 62/63)

| Check | Result |
|---|---|
| `sim/selftest.py` | **192 PASS / 0 FAIL** (+ T14: the MCU's heat-stage rules for words 62/63 and the `boilers` event) |
| New scenarios (EN + DA, v6.18 SIM build) | `audio_boiler_ready` 27/27, `audio_muted` 20/20, `audio_power_cycle` 25/25, `audio_sound_off` 24/24 — green in both languages; reference client: SKIP (`ui_only`) |
| `run_e2e.py --all --parallel 4` (EN and DA) | **43/43 scenarios, 1126/1126 checks** in each language — no existing scenario needed a change (the heat model is off unless a scenario switches it on) |

## 8. File inventory

| File | Purpose |
|---|---|
| `mcu_sim.py` | simulated MCU (pty, register model, phase machine, faults, assertion log, replay) |
| `ref_client.py` | reference client (implements CONTRACT.md) |
| `run_e2e.py` | runner (sandbox, launch, events, sampling, assertions, report) |
| `selftest.py` | 187 protocol/model self-tests (v6.14: + sleep/wake T13) |
| `proto.py` | CRC/frame/parse/parity/evdev shared library |
| `png.py` | dependency-free PNG + contact sheet |
| `gen_scenarios.py` | reproducible generator for scenarios/*.json |
| `scenarios/*.json` | 39 timelines (v6.14: + power_off_wake/power_during_activity; v6.15: + graph_reset; v6.16: + time_display/timemode_stock_parity/schedule_prewarm; v6.17: + ia_navigation/feature_page_start/activity_takes_home) |
| `play.py`, `play_viewer.html`, `PLAY.md` | interactive browser simulator |
| `CONTRACT.md` | the contract for the UI's SIM build |
| `README.md` | this file |
| `reports/run-*/` | run reports (JSON + MD + PNG + logs; not tracked in git) |

*— test work stream, 2026-11-17. Everything run locally; the device was not touched.*
