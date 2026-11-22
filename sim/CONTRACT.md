# CONTRACT — the SIM build of merakiplus (for the UI work stream)

**Date:** 2026-11-17 · **Owner:** the test work stream (this file + `sim/`) · **Recipient:** the UI work stream (owner of `merakiplus.c`/`UI_v6.md`/`REVERT.md`/`round_v6/`)
**Purpose:** a precise, minimal contract so `run_e2e.py` can run our UI against the simulated MCU (`mcu_sim.py`) without hardware. The contract is **backward compatible**: when `MERAKIPLUS_SIM` is not set, the device build is unchanged (no behavior change, no new files, no new writes).

> **Repository layout (public edition):** paths such as `workspace/…` refer to the author's
> working directory; in this repository the UI source is `src/merakiplus.c` and the SIM build is
> `build/merakiplus_sim` (`./build.sh host`). Toast texts in the examples are quoted exactly as the state
> field contains them (the `toast.text` field holds the Danish string — see Addendum v6.4 A).

Everything below has already been implemented and proven by the **reference client** (`sim/ref_client.py`), which the runner used until the SIM build landed. The reference client follows the contract 1:1 — your build can therefore be verified against the same scenarios without changes.

---

## 1. Build flags

```sh
zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 \
    -DMERAKIPLUS_SIM -o merakiplus_v6_sim merakiplus.c
# host preview for development (optional):
zig cc -DMERAKIPLUS_SIM -DMERAKIPLUS_PREVIEW -O2 -o merakiplus_v6_sim_host merakiplus.c
```

* `-DMERAKIPLUS_SIM` alone (without `MERAKIPLUS_PREVIEW`) = the **real-time build** that talks to `mcu_sim` over a pty, reads touch from a FIFO and writes fb/state to files. This is the build the runner launches.
* The existing `MERAKIPLUS_PREVIEW` mechanics (`sim_reset/sim_run/dump/tap`) may be reused internally, but the SIM build must run in **wall-clock real time** (like the device loop), not the offline `sim_run()` loop.
* Place the binary as `build/merakiplus_sim` (auto-detected by the runner), or pass `--ui-bin <path>` / env `MERAKIPLUS_SIM_BIN`.
* **Without `-DMERAKIPLUS_SIM`: no change.** All hooks compile away; the device behavior (fb/page flip, EVIOCGRAB, ttyS1, sleep logic, writes) is byte-identical to the current v6.x.

## 2. Environment variables (HAL hooks)

The runner sets these (see `run_e2e.py start_ui()`). All paths are absolute.

| Env | Replaces | Requirement |
|---|---|---|
| `MERAKIPLUS_SIM=1` | — | enables the hooks; without it = device build |
| `MERAKIPLUS_SIM_TTY=<pty-slave>` | `/dev/ttyS1` | open `O_RDWR\|O_NOCTTY\|O_NONBLOCK`; raw 8N1 (like `set_opt`); baud 57600 is set but ignored by the pty. **All protocol unchanged** (boot push sub1 len30 + sub112 len3, keep-awake word1=0/30 s, poll rotation, ACK 150 ms + 1 retransmit). |
| `MERAKIPLUS_SIM_ROOT=<sandbox>` | prefix for `/customer`, `/new_customer`, `/proc`, `/tmp`, `/sys` | reuse the existing `MERAKIPLUS_ROOT` mechanism; `MERAKIPLUS_SIM_ROOT` takes precedence. Boot flag = `<root>/customer/new`; slot checks read `<root>/proc/...`. |
| `MERAKIPLUS_CONFIG=<file>` | `/customer/config.json` | existing env, unchanged semantics (atomic tmp+rename, backups). |
| `MERAKIPLUS_BAKDIR=<dir>` | backup directory | existing env. |
| `MERAKIPLUS_SIM_FB=<file>` | `/dev/fb0` | allocate a **480×480×4 BGRA** buffer (uint32, the same pixel format as the device). No page flip in the SIM (one buffer; the `fbpage` equivalent = always 0). Write the buffer to the file after every rendered frame: **tmp+rename**, throttled to ≤ 10 Hz, but ALWAYS on a page/phase/toast change. The file must contain a complete, consistent frame (921600 bytes) at all times. |
| `MERAKIPLUS_SIM_TOUCH=<FIFO/socket>` | `/dev/input/event0` | read **raw 16-byte `input_event` records** (§4) instead of the evdev node. **No `EVIOCGRAB` in the SIM.** No `EVIOCGABS` — use the raw range 0..4095 (default). The same scaling formula as now: `screen = raw*480/4096`. |
| `MERAKIPLUS_SIM_STATE=<file>` | — | **state JSONL** (§3): append one JSON object per rendered frame. |
| `MERAKIPLUS_SIM_CTRL=<unix-socket>` | — | **ctrl socket** (§5): JSONL request/response. |
| `MERAKIPLUS_NOGRAB=1`, `MERAKIPLUS_NOBL=1`, `MERAKIPLUS_SWITCH_DRY=1` | — | existing envs; the runner sets them. The backlight sysfs must not be written in the SIM. |

A missing required env (`MERAKIPLUS_SIM_TTY`/`MERAKIPLUS_SIM_FB`/`MERAKIPLUS_SIM_STATE`) → **exit 2** with a clear error message on stderr.

## 3. The state JSONL channel (per frame)

Append to `$MERAKIPLUS_SIM_STATE`, one JSON object per line, per rendered frame (≥ 10 Hz during animation, otherwise ≥ 2 Hz; always on a page/phase/toast change). The runner samples the file and evaluates assertions against it.

**Required fields** (the runner's `contract` check):

```json
{
  "t_ms": 12345,                 // monotonic ms since process start
  "view": "main",                // main | bye  (V_SPLASH/V_MAIN/V_DETAIL/V_SYS/V_BYE → map)
  "page": "home",                // stable strings: home | settings | detail | sys | diag | bye
  "phase": "READY",              // ENUM KEY, not a (Danish) label! (see below)
  "work_sta": 257,               // word33 as the UI sees it (mirror)
  "regs": {"0":1,"1":0,"4":185,"11":870,"14":3,"17":400,"23":360,"25":650,
            "33":257,"34":0,"35":0,"36":0,"38":0,"39":0,"40":998,"42":696,
            "47":1302,"48":232,"50":5,"51":0,"52":0,"58":5,
            "112":260,"113":1541,"114":70},   // displayed/mirrored words (string keys)
  "lang": 0,                     // config language index
  "boot_flag": false,            // /customer/new exists (sandbox)
  "crc_ok": 1234, "crc_bad": 0,  // RX counters
  "cmd6": {"ok":3,"retx":0,"fail":0,"block":0},
  "frame_ms": 4.2                // render time of the last frame
}
```

**Recommended fields** (used by the scenarios; without them the corresponding checks fail):

```json
{
  "toast": {"text":"GEMT · CONTROLLER + CONFIG","kind":"ok"},  // or null; kind: ok|err|warn ("SAVED · CONTROLLER + CONFIG")
  "config_oor": [{"param":"brew_temp","field":"brewTemper","value":1200,"min":850,"max":960}],
  "config_missing": false,
  "link_up": true,               // now-last_rx < 1500 ms
  "save": {"state":"ok","param":"brew_temp","value":880},      // pending|ok|fail|null
  "activity_pending": "brew",    // or null
  "regs_s": {"38":-1,"39":-3},   // signed weights (optional; the runner uses s16 itself)
  "boot_pushed": true,
  "client": "merakiplus-sim",         // identification (the reference client writes "reference")
  "version": "V6.2"
}
```

**Phase keys** (the same order as `PH_*` in merakiplus.c — the runner assumes these keys):
`OFFLINE, SLEEP, ERROR, HEAT, READY, GRIND, PRE, EXT, WATER, STEAM, DONE`.
The `phase_raw()` logic is already correct in v6 (bits 5+14 steam, bit6 grind, bits 1+2(+9) brew, bits 1+3 water, word1=1/bit8 cleared sleep, word40<word11−20 heat) — **just export the enum key** instead of `PH_NAME[]`.

**Page mapping** (proposal, so the scenarios work): `PG_HOME→"home"`, `PG_T0/T1/T2→"settings"` (list) / `"detail"` (open stepper), `V_SYS→"sys"`, `PG_DIAG→"diag"`, `V_BYE→"bye"`.

## 4. Touch inject format (the same as tinj_hold)

The runner writes **raw evdev bytes** to `$MERAKIPLUS_SIM_TOUCH` (FIFO). Format = exactly what `tinj_hold.c` writes to `/dev/input/event0`:

* 16-byte record: `struct input_event { int32 sec; int32 usec; uint16 type; uint16 code; int32 value; }` → Python `struct.Struct("<iiHhi")`.
* **Down:** `ABS_MT_TRACKING_ID(0x39)=1`, `ABS_MT_POSITION_X(0x35)=raw`, `ABS_MT_POSITION_Y(0x36)=raw`, `ABS_X(0)=raw`, `ABS_Y(1)=raw`, `BTN_TOUCH(0x14a)=1`, `SYN_REPORT(0)=0`.
* **Up:** `ABS_MT_TRACKING_ID=-1`, `BTN_TOUCH=0`, `SYN_REPORT`.
* **Raw scaling:** `raw = px*4096/480` (inverse: `px = raw*480/4096`).
* Timestamps = kernel timeval; gesture timing (switcher) follows the **event timestamps**, not the wall clock.
* The same `inp_poll()` logic as on the device (TRACKING_ID/BTN_TOUCH → down/up, SYN_REPORT → latch). Hold-to-start (650 ms) is measured the same way.

The FIFO is opened by the runner with `open(path,"wb")` — the SIM build must keep the read end open (also open a write end internally, so reads do not hit EOF when the runner closes it between injections).

## 5. Ctrl socket (JSONL request/response)

A Unix socket at `$MERAKIPLUS_SIM_CTRL`. Each line = one JSON object; the response has the same `"id"`.

**Required:**

| cmd | Action | Response |
|---|---|---|
| `ping` | — | `{ok, t, page, phase}` |
| `fb_dump {path}` | write an immediate fb dump (raw BGRA, 921600 B) to `path` (tmp+rename) | `{ok:true}` |
| `exit {code}` | clean exit: close UART/fb/touch, release any grab, exit(code) | `{ok:true}` followed by the process exit |

**Required for full scenario coverage** (test hooks; compile away without `MERAKIPLUS_SIM`):

| cmd | Action |
|---|---|
| `action {name}` | start an activity as via a UI button: `grind\|brew\|water\|steam\|steam_stop` (the same `act_push()` paths, with the hold-to-start logic bypassed — the hook = explicit intent) |
| `save {param, value}` | settings SAVE as via stepper+SAVE: parity write (word for word) + config persistence; `param` = parity_spec id |
| `page {name}` | navigate to `home\|settings\|detail\|sys\|diag` |
| `touch {x, y, ms}` | convenience: internal down/up (ms ≥ 650 = hold) — the same path as FIFO touch |
| `boot_flag {value}` | the SYSTEM row's BOOT UI toggle: touch/rm `<root>/customer/new` |
| `runtime_switch` | "SWITCH TO ORIGINAL NOW": with `MERAKIPLUS_SWITCH_DRY=1` → bye view + log, keep running; without → launch the helper + exit 0 (device semantics) |
| `raw_write {sub, words}` | test hook: send a cmd6 frame through the frame policy (emulates stock writes, e.g. sleep word1=1) |
| `config_reload` | reread the config from `$MERAKIPLUS_CONFIG` (as at boot) |
| `state_now` | emit a state line immediately |

## 6. Exit codes

| Code | Meaning |
|---|---|
| 0 | normal exit (REVERT/exit button, ctrl `exit`, runtime switch B→A) |
| 2 | usage/env error (missing SIM env, invalid config path) |
| 3 | UART open error (the pty does not exist / cannot be opened) |
| 4 | fb/touch init error |
| 130 | SIGINT |

The runner asserts `exit_code=0` in all scenarios (except explicit error scenarios).

## 7. Timing expectations (unchanged from v6)

* Poll: sub0/sub33 @ 50 ms alternating + sub56 @ 200 ms + sub112,3 @ 1 s (or the stock rotation @ 100 ms — both are accepted by the sim).
* Keep-awake: word0=1 + **word1=0** at start + a refresh every ≤ 30 s (the sim logs `keepalive_gap` at > 30.5 s).
* Write ACK: 150 ms timeout, exactly 1 retransmit, then FAIL → SAVE blocks, config unchanged.
* Link timeout: 1500 ms without RX → OFFLINE.
* Render: 33 ms (animation) / 100 ms (idle).
* Boot push: `sub1 len30` + `sub112 len3` within ~6 s after connect (the sim logs `boot_push_missing` otherwise).
* Version handshake: reply with cmd9 (`softversion`) to the MCU's 0x89 (arrives every 20 ms until a reply).

## 8. Sandbox layout (the runner creates this)

```
sandbox/
  customer/config.json        # copy of sim/fixtures/config.json + scenario overrides
  customer/new                # boot flag (only if the scenario says so)
  customer/bak/               # config backups
  new_customer/merakiplus/boot/    # slot B structure
  proc/{uptime,meminfo,version}
  sys/class/pwm/pwmchip0/pwm2/duty_cycle
  tmp/
  dev/input/event0.fifo       # touch inject (MERAKIPLUS_SIM_TOUCH)
  fb.bin                      # fb buffer (MERAKIPLUS_SIM_FB)
```

## 9. Verifying the contract

The runner's `contract` check verifies per scenario: all required state fields present, an fb buffer ≥ 921600 bytes, exit code 0. In addition, the **reference client** is a running implementation of the contract — use it as the oracle:

```sh
python3 sim/run_e2e.py --all                 # reference client (proves sim + runner)
python3 sim/run_e2e.py --all --ui-bin build/merakiplus_sim   # your build
```

When your build lands, the same scenarios must run green **without changes** to the scenarios. Deviations in page names/phase keys are the only permitted adjustments — document them in `UI_v6.md`, so the runner's mapping can be updated in one place.

## 10. What the SIM build must NOT do

* No contact with the physical device (no network, no telnet, no `/dev/ttyS1` on the device).
* No change to the device build when `MERAKIPLUS_SIM` is not set (guarantee: byte-identical behavior).
* No new config fields, no writes to wifi/ssid/password, no OTA, no word26/27/30≠0 (the frame policy in v6 is already correct — the sim rejects and logs).
* No touching the `sim/` files (the test work stream's property) — feedback is given via `UI_v6.md`/a message, and the contract is adjusted here.

*— test work stream, 2026-11-17. The contract is minimal, backward compatible and already proven by the reference client (22 scenarios, 107 self-test checks).*

---

## ADDENDUM v6.4 — from the UI work stream: how merakiplus's SIM build fulfills the contract

**Date:** 2026-11-17 · **Written by:** the design/UI work stream (owner of `merakiplus.c`) · **Applies to:** `workspace/merakiplus.c` v6.4, `build/merakiplus_sim`.
The contract above is unchanged. This addendum documents (A) where the SIM build deviates or clarifies, (B) the fields and hooks that were added, (C) where the reference client and the real UI behave differently — and how the runner handles it — (D) the additive changes in `run_e2e.py` and (E) findings for the test work stream. Details and rationale: `workspace/UI_v6.md` §13 + CHANGES-v6.4.

### A. Deviations and clarifications

| § | The contract says | The SIM build does | Why |
|---|---|---|---|
| 1 | `merakiplus_v6_sim` is built statically for ARM and auto-detected by the runner | **`merakiplus_v6_sim` is the HOST build** (`zig cc -DMERAKIPLUS_SIM -O2`), which is what the runner starts. The static ARM build (the contract's command, 0 warnings) is called **`merakiplus_v6_sim_arm`** | the runner runs on a macOS/arm64 host and cannot start an ARM ELF. macOS has no static libc, so the host build is dynamic against the system libc; the ARM build is static and smoke-tested in a local ARMv7 container (`build_sim.sh`) |
| 1 | `-DMERAKIPLUS_SIM -DMERAKIPLUS_PREVIEW` = host preview | gives the ordinary preview/self-test binary (`MERAKIPLUS_PREVIEW` takes precedence) | the real-time build is `-DMERAKIPLUS_SIM` alone and can be built for the host directly |
| 2 | `MERAKIPLUS_SIM_ROOT` is a prefix | is **required** (otherwise `MERAKIPLUS_ROOT`); if it is missing, or is `/`: exit 2 | the SIM build must never read or write the host's `/customer`, `/new_customer`, `/proc`, `/tmp` |
| 2 | — | **the language file** follows the root in the SIM: `<root>/new_customer/merakiplus/lang`, unless `MERAKIPLUS_LANGFILE` is set. The runner now sets `MERAKIPLUS_LANGFILE` and has `--ui-lang en\|da` | in the device build the language file does not follow `MERAKIPLUS_ROOT` (v6.3, unchanged) |
| 2 | "All protocol unchanged (boot push sub1 len30 + sub112 len3 …)" | the boot push is sent **only by the SIM build** (and can be disabled with `MERAKIPLUS_SIM_NOBOOTPUSH=1`) | **merakiplus v6.3 does not boot-push** — the stock UI does. The device build is deliberately unchanged in v6.4; whether the device should boot-push on a slot B boot is an open point (UI_v6.md CHANGES-v6.4 item 6). The block goes through the frame policy: start/trigger words = 0, words 5/13/15 = 60, and a config value out of spec gets the whole `sub1 len30` rejected (BLOCK +1, nothing is sent) |
| 3 | a state line "per rendered frame" | one line per rendered frame **if the line's contents have changed** since the previous line (the RX counter `crc_ok` and the frame time fields do not count as a change); otherwise at least every 90 ms. No displayed register value can therefore be missing from the stream | unchanged frames give no information, and the runner rereads the whole file on every sample. `frames`, `frame_ms_avg`, `frame_ms_peak` cover all rendered frames |
| 3 | `phase` = enum key | `phase` = `phase_raw()` (never `DONE`); **`phase_ui`** = what the chip shows (incl. `DONE` for 8 s after an activity) | the scenarios expect READY right after a stop |
| 3 | `lang` = config language index | unchanged: `lang` = `config.language` (the stock UI's language). merakiplus's own UI language is **`ui_lang`** (`"en"`/`"da"`) + `lang_src` | two different settings since v6.3 |
| 3 | `toast.text` | `toast.text` = the string table's **Danish** text (the scenarios' assertion language), **`toast.shown`** = the text on the screen (UI language, possibly truncated), `toast.id` = string id, `kind` = ok/err/warn/info | the same asserts pass in both UI languages. NB: `TX_T_LANG_SAVED` is self-referential ("SPROG GEMT · DANSK" / "LANGUAGE SAVED · ENGLISH") — use `shown`/`ui_lang` there |
| 3 | `save.state`: pending\|ok\|fail | + `rejected` (a guard or the hook rejected it; toast shown) and `unchanged` (SAVE of the saved value just closes the editor) | that is what the UI does |
| 4 | "gesture timing follows event timestamps" | FIFO records are queued and delivered when their timestamp distance to the gesture's first record has elapsed on the wall clock | the runner writes a whole gesture at once; otherwise a hold would last 0 ms |
| 5 `touch` | px → raw | `raw = ceil(px·4096/480)`, so the UI gets exactly `px` back | `floor` (proto.touch_raw) gives 1 px too little for most coordinates |
| 5 `action` | "as via a UI button, hold bypassed" | calls `act_request()`: **all guards apply** (error active, asleep, activity in progress, config out of spec, queue busy). If the write queue is busy for a moment (keep-awake), the hook is deferred for up to 600 ms | that is what the button does |
| 5 `save` | "as via stepper+SAVE: parity write (word for word)" | opens the row's editor, sets the value and calls `save_begin()` — **merakiplus's own SAVE path**. Unknown/not exposed/locked parameter: toast *LOCKED*. Value outside the range or between the stepper's steps: toast *OUT OF LIMITS*. Where merakiplus's SAVE is deliberately not parity_spec word for word, see C | the hook must not be able to do anything the user cannot |
| 5 `boot_flag` | touch/rm the flag | `sys_open(BOOT UI)` → choose → `sys_confirm()` → `boot_flag_set()` **with the guard** (slot B mounted + merakiplus binary) | the same path as the row |
| 5 `runtime_switch` | dry: bye view + log | the dry run shows the goodbye screen for 1 s and returns with the toast *DRY-RUN · SEE cfgbak/boot_ui.log* — also when SWITCH NOW is tapped on the screen. (The device's dry run: log + toast only.) The guards apply: blocked during an activity | the contract requires the bye view |
| 5 `raw_write` | "through the frame policy (emulates stock writes, e.g. sleep word1=1)" | the band, trigger and range guards apply; **the only relaxation is word1 ∈ {0,1}**. `word1=1` also **pauses** merakiplus's keep-awake until a `raw_write` with `word1=0` | merakiplus's frame policy rejects word1=1, and merakiplus wakes a sleeping controller within 3 s (v6 behavior; proven in `ui_autowake`). Without the pause `sleep_wake` cannot keep the machine asleep |
| 5 `config_reload` | reread the config | rereads the language file + config and boot-pushes again (as at startup) | |
| 8 | sandbox layout | the SIM build **adds** what is missing (overwrites nothing; `MERAKIPLUS_SIM_NOPROVISION=1` disables it): `proc/mounts` (with `/new_customer`), `proc/sys/kernel/random/boot_id`, `proc/self/exe` → `/new_customer/demo_v9`, `new_customer/demo_v9` (a mock with the merakiplus tag), `customer/demo_v9` (mock stock) | the BOOT UI guard and SWITCH NOW would otherwise be LOCKED: they read the mounts, slot B's binary tag and whether slot A has a binary |

Exit codes as in §6. `SIGTERM` gives exit 0 (a clean shutdown, like the device); `SIGINT` 130.

### B. Additions: fields and hooks

* **State fields beyond §3:** `view_raw` (splash/main/detail/sys/bye), `tab` (home/brew/steam/system/diag), `phase_ui`, `shown` (what the screen shows: hero/unit/label/sub-line/stat trio on HOME; label/value/range in an editor), `ui_lang`, `lang_src`, `next_boot` (original/ours/slot_b), `boot_lock`, `exe_slot`, `self_exe`, `switch{mode,locked,dry,bye,stock_n,switcher_pid,child,last_evt}`, `restart_needed{file,pending,stamp}`, `blok` (= `cmd6.block`), `wq_n`, `boot_push{enabled,sub1_len30,sub112_len3,runs}`, `ka_hold`, `backlight_preview`, `config_writes`, `config_md5`, `frame_ms_max`/`frame_ms_avg`/`frame_ms_peak`/`frames`, `fb_seq`.
* **`regs`** also contains words 2, 3, 6, 9, 16, 19, 22, 24, 37, 41 — the scenarios' `word` checks on 3/6/16/22/24 therefore measure the UI's real mirror (the reference client leaves them out, and the runner reads a missing word as 0).
* **New hook `layout`:** returns the real touch targets in the current view (`{"name","x","y","r"}` or `{"name","x","y","w","h"}`, x/y = center). HOME: `grind` (150,372), `brew` (210,372), `water` (270,372), `steam` (330,372), all r 30; `revert`; the edge zones. Editor: `minus` (84,184), `plus` (396,184), `cancel`, `save`. Lists: one row per visible setting. Use it instead of `ref_client.BUTTONS` (`play.py` already does).
* **`page`** takes `tab` (0–2) for `settings`, `param` for `detail` and `x` (0 = BOOT UI, 1 = SWITCH) for `sys`.
* **`save`** also accepts `ui_language` (0 = en, 1 = da; merakiplus's own language file) and the config key as `param`.

### C. Reference client ≠ the real UI: the UI profile

Five scenarios have expectations that are tied to the reference client's synthetic layout, or to it having neither merakiplus's guards nor merakiplus's deliberate deviations from `parity_spec.json`. The scenario files are **not** changed. Instead the runner reads a UI profile when the client is the SIM build, and replaces the affected expectations **1:1** (the number of checks is unchanged) with what merakiplus actually does. Each override must hit exactly one expectation, otherwise the scenario fails; all applied overrides are listed in the report and marked `[ui-profile]`. `--no-ui-profile` runs the scenarios raw.

| Scenario | Tied to the reference client | In merakiplus | Override |
|---|---|---|---|
| `idle` | color point (240,20) = page band; the middle region = phase background | gauge arc in the phase gradient; dark background with white hero text | 2 colors (the point (240,8) = phase ring passes unchanged) |
| `grind` | touch (130,160) = GRIND in the reference layout | GRIND is (150,372) | 1 event (coordinate) |
| `fault_e1` | a brew is started during an active error, the MCU safety-stops it | the start is blocked: *ERROR ACTIVE · START BLOCKED*, no frame is sent | 1 check |
| `out_of_spec` | the boot push sends out-of-spec values, the sim logs them | the frame policy rejects the block (BLOCK ≥ 1); the sim never sees the value | 1 check |
| `settings_save_matrix` | parity_spec word for word for all parameters | PRE-INF. TIME = `sub113 len1` (not `sub112 len2`); GRIND MODE = `sub2 len1` (not `len2` with start word 3); SAVE of an unchanged value writes nothing; PAUSE TIME is locked; BRIGHTNESS 0 is left out; `init`/`patch` are not exposed | 16 checks |
| `replay_v3` (**timing, not behavior**) | a 10 Hz state stream: no line falls in the window's last milliseconds | one line per changed frame (up to 30/s): the window `word51 ∈ [300,420]` in [14.5 ; 15.0] ends 10–20 ms before the pressure jump (measured 15.01 / 15.02 / 14.99 s) and is therefore sometimes hit | 1 check: the window end 15.0 → 14.9 |

A raw run against the real UI: 17/22 scenarios (matrix 172/188; the 26 checks that fail are exactly those in the first five rows). With the profile: 22/22 (matrix 188/188). The five rows in the matrix have also been run through the **real stepper + SAVE** with touch in `workspace/sim_scenarios/ui_rows_real_frames.json`.

**Proposal for the test work stream:** move the profile's contents into `gen_scenarios.py`/`ref_client.py` (as you have already done for `hotwater_mode`), so the reference client follows merakiplus for these rows — then the profile can be emptied.

### D. Changes in `sim/run_e2e.py` (all additive; reference runs are unchanged)

* `MERAKIPLUS_LANGFILE` is set to `<sandbox>/new_customer/merakiplus/lang`; `--ui-lang en|da` writes the file (the v6.3 note).
* `--ui-profile <file>` / `--no-ui-profile` + `apply_ui_profile()`; the report shows the applied overrides and marks the affected checks.
* `--run-name`, `--scen-dir` (used by `workspace/sim_e2e.py` for `sim/reports/v6.4/` and merakiplus's own touch scenarios).
* The report: UI language, total check count, frame time per scenario (from `frame_ms_avg`/`frame_ms_peak`/`frames`).
* `ScenarioRun(...)` has two new optional arguments (`ui_lang`, `ui_profile`); existing calls (e.g. `play.py`) work unchanged.

### E. Findings for the test work stream

* **`steam_auto` is flaky regardless of the client (about 1 run in 9).** On auto-stop the sim sets `milk_peak = r[48] + randint(0, 8)` (`mcu_sim.py`, `_stop_activity`), and the after-phase writes `int(milk_peak − 0.5·el)`. If it draws 0, word 48 = 650 for a single 10 ms tick and is then 649 — a client that polls words 0..55 every 100 ms (merakiplus) or every 400 ms (the reference client) normally does not see it, and `word_min_seen 48 ≥ 650` fails with `max=649`. Seen once in six full runs against the SIM build (`sim/reports/v6.4/flake_steam_auto/`); everything else in the scenario passed. Proposal: `randint(1, 8)`, or let the check ask the sim (like `replay_truth`).
* **`replay_v3`: the window `word51 ∈ [300,420]` in [14.5 ; 15.0] has a 10–20 ms margin** to the pressure jump at 8× playback (all other `word_hold`/`phase_hold` windows have ≥ 170 ms). It failed once against the SIM build when three suites were running at the same time; the UI profile shortens it to 14.9. Proposal: do the same in `gen_scenarios.py`.
* **`regs` in the reference client** lacks words 3, 6, 16, 22 and 24, so `word` checks on them pass on a missing key (the runner reads it as 0). The SIM build exports them; all also pass with real values.
* **`sim/play.py`** already uses the `layout` hook against the SIM build (PLAY.md). The UI language can also be changed without restarting the UI: write the language file and send `config_reload` (it rereads the language file), or `save {"param":"ui_language","value":0|1}`.

*— design/UI work stream, 2026-11-17 (v6.4). The device was not contacted; `boot/` was not touched; in `sim/` only `run_e2e.py` (additive) and this addendum were changed.*

## ADDENDUM v6.5 — from the UI work stream: boot push on the device, activity guard, RX resync

**Date:** 2026-11-17 · **Written by:** the design/UI work stream · **Applies to:** `workspace/merakiplus.c` v6.5, `build/merakiplus_sim`.
The contract and ADDENDUM v6.4 are unchanged. Details: `workspace/UI_v6.md` CHANGES-v6.5.

### A. The SIM build's behavior is unchanged

* The boot push (`sub1 len30` + `sub112 len3`) is sent as in v6.4: at startup and on `config_reload`, and a running activity **defers** it. `MERAKIPLUS_SIM_NOBOOTPUSH=1` disables it.
* ADDENDUM v6.4 A, row §2 ("the boot push is sent **only** by the SIM build") is outdated: from v6.5 the **device build** sends it too — the same `boot_push_step()`, the same word layout, the same frame policy. The only difference is the guard below.

### B. New

| What | Contents |
|---|---|
| `MERAKIPLUS_SIM_BOOTPUSH_GUARD=1` | gives the SIM build **the device's activity guard**: if grind/brew/water/steam is running the first time the link is up and word 33 has been read, the push is **skipped** (not deferred, not repeated by a later SAVE). Without the variable: the contract behavior (defer). The runner passes its environment on to the UI, so the variable can be set in the calling process |
| state `boot_push.sub1_len30` / `.sub112_len3` | new value `"skipped"` (only with the guard). `boot_pushed` is `false` for a skipped push |
| state `rx_resync` | the number of discarded header candidates in the RX parser: impossible length (LEN > 129 words), watchdog (an incomplete candidate older than `RX_RESYNC_MS` = 400 ms; one per parser call) and buffer overflow. `crc_bad` is, as before, CRC errors on whole frames |
| `crc_bad` | now also counts a `cmd 3` or `cmd 8/0x88` frame with a wrong CRC (v6.4 skipped them without a CRC — and could thereby swallow the start of a valid frame right after noise). Valid frames are handled as before |
| DIAGNOSTICS | the cell is called *RX CRC / RESYNC* (`crc_bad / rx_resync`); new row *BOOT PUSH 1-30* / *BOOT PUSH 112-114* |

### C. Findings for the test work stream

* **`sim/rx_stall_repro.py` was run against both binaries** (`workspace/sim_e2e.py --only rxstall` → `sim/reports/v6.5/rx_stall.txt`): v6.5 = *verified* (never OFFLINE, `rx_resync` = 1); a SIM binary built from `merakiplus_v6_4.c` = about 4 s without RX (3.98 / 4.01 s), OFFLINE after about 1.3 s. `stall_s` for v6.5 is the measurement's floor (a 0.6 s plateau window); the recovery itself is 0 ms for an impossible length (self-test).
* **The stall can be triggered without bit errors in the length byte:** a stray `AA 55` immediately in front of a valid frame (`AA 55 AA 55 83 …`) is read by v6.4 as command `AA` with length `0x83` = 131 words. This is covered in merakiplus's self-test (9 "worst" prefixes), but not by a sim injection. Proposal: an inject type that sends a given prefix (`"prefix": "aa55"`, `"aa5503"`, `"aa5508"`) right before the next reply.
* **The sim starts with the config preloaded** (`config_to_words` in `__init__` and `do_reboot`), i.e. as if the stock UI had already boot-pushed. For a "slot B boot", `sim_e2e.py` sets the controller's start values (registermap §2) with `set_reg` before the UI is started. Proposal: a sim option `--mcu-defaults` (and let `reboot` give the start values), so scenarios can express it.
* **The default profile:** `workspace/sim_e2e.py` now runs without `--ui-profile`, i.e. with the runner's own default (`sim/sim_ui_profile-ui.json`). The v6.4 profile file is no longer used and can be dropped.

*— design/UI work stream, 2026-11-17 (v6.5). The device was not contacted; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` and `sim/run_e2e.py` were not touched; in `sim/` only this addendum and `reports/v6.5/` are new.*

## ADDENDUM v6.6 — from the UI work stream: phase fields (two steam paths, concurrent phases), clock offset, re-push after an MCU restart

**Date:** 2026-11-17 · **Written by:** the design/UI work stream · **Applies to:** `workspace/merakiplus.c` v6.6, `build/merakiplus_sim`.
The contract and ADDENDUM v6.4 / v6.5 are unchanged. Details: `workspace/UI_v6.md` CHANGES-v6.6.

### A. Unchanged

* **`phase` is unchanged:** `phase_raw()` — steam (bits 5+14) first. During work_sta `0x5327` `phase` is therefore still `STEAM`, as `replay_steam_live` asserts. `work_sta`, `regs`, toasts, writes, exit codes and all hooks are as in v6.5.
* The hook `action steam_stop` sends the STOP block as before (`sub24 len8`, word 24 = 0), regardless of the steam path.

### B. New

| What | Contents |
|---|---|
| state `phase_main` | what HOME shows as the main activity: grind / brew / water take precedence over steam. During `0x5327`: `EXT` (while `phase` = `STEAM`) |
| state `phases` | all active phases, the main activity first: `["EXT","STEAM"]`, `["STEAM"]`, `["READY"]` |
| state `steam` | `on` (bits 5+14 set) · `kind`: `none` / `unknown` (word 24 not read) / `auto` (word 24 ≠ 0 while the bits are set — sticky until they are cleared) / `wand` (word 24 = 0) · `last_kind` · `concurrent` · `wand_sw` (word 44 bit0 or word 46 ≠ 0) · `wand_seen` · `ord24` · `target` (word 25) · `milk` (word 48) · `stop_btn` |
| state `shown.chip` / `shown.badge` | the chip is *STEAM·AUTO* / *STEAM·WAND* (*DAMP·AUTO* / *DAMP·WAND*) during steam; `badge` is the steam badge during concurrent phases, otherwise `null`. During concurrent phases the 3rd stat field is `MILK` / `MÆLK` |
| state `regs` | also words 44 and 46 |
| state `repush` | `count`, `ver` (triggered by `0x89` arriving again), `defaults` (triggered by word 112 = 0 and word 113 = 0), `last`, `armed`, `ver_reqs`, `defaults_sig` |
| the hook `layout` | `steam_stop` only exists for latched steam (`steam.stop_btn`); during wand/flush the four start buttons are listed (they cannot be pressed while an activity is running) |
| `MERAKIPLUS_SIM_UPTIME_MS=<n>` | the clock starts at n ms (SIM build only). `t_ms` is still ms since UI start. Used by `workspace/sim_e2e.py --only wrap` to run the main loop across 2^31 and 2^32 ms |
| **New behavior on the line** | if the UI sees `0x89` again more than 200 ms after its boot push — i.e. after the sim's `reboot`, or after `link` `closed` → `up` — it sends `sub1 len30` + `sub112 len3` again (through the frame policy; in contract mode deferred during an activity, with `MERAKIPLUS_SIM_BOOTPUSH_GUARD=1` skipped). The same happens if words 112 and 113 are both read as 0 three times in a row on an idle machine. None of the 25 scenarios does either; `sim/reports/v6.6/repush.txt` shows the sequence |

### C. Findings for the test work stream

*(The first edition of this section had as C.1 and C.2: "`sim/selftest.py` gave 105 / 2 during the verification" and "the sim cannot do a wand flush next to a brew". Both are answered by the test work stream's ADDENDUM v6.6-calibration below, which refers to them, and are replaced here by the status after the rerun.)*

* **The new steam scenarios pass against v6.6 without changes:** `steam_wand_flush` 31/31 and `steam_auto_overshoot` 24/24 in both UI languages (the suite: 25/25 · 644/644; `sim/selftest.py` 120/0). `workspace/sim_e2e.py --only phases` also reads their state lines against the new fields (`steam.kind`, `steam.concurrent`, `phase_main`, `shown.badge`, the 3rd stat field): 20/20.
* **The race is noted, not hidden:** the first full run coincided with the rework of the steam model (20/23 in both languages; `sim/selftest.py` 105/2 at that time). `sim_e2e.py` now takes a fingerprint of `sim/` before and after each part — see `sim/reports/v6.6/SUMMARY.md`.
* **`reboot` still preloads the config** (as if the stock UI had boot-pushed). For the re-push test `sim_e2e.py` sets the start values (registermap §2) with `set_reg` right after `reboot`. The proposal from ADDENDUM v6.5 C about `--mcu-defaults` stands.
* **The runner has no generic state field assert** (e.g. `{"kind":"state","path":"steam.kind","value":"wand","t0":…,"t1":…}`). With it the new fields could be asserted in the scenario files instead of in `sim_e2e.py`.

*— design/UI work stream, 2026-11-17 (v6.6). The device was not contacted; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` and `sim/run_e2e.py` were not touched; in `sim/` only this addendum and `reports/v6.6/` are new.*

## ADDENDUM v6.6-calibration — from the test work stream: mcu_sim calibrated against STEAMLIVE (two steam paths, flush, concurrent phases)

**Date:** 2026-11-17 · **Written by:** the test work stream (owner of `sim/`) · **Applies to:** `sim/mcu_sim.py`, `sim/scenarios/`, `sim/selftest.py`, `sim/ref_client.py`.
The contract and ADDENDUM v6.4/v6.5/v6.6 (UI work stream) are unchanged. Full documentation: `sim/reports/v6.6-calibration/REPORT.md`.

### A. The sim now supports the two steam paths + concurrent phases (answer to the v6.6 addendum C.2)

* **Display steam (latch=1):** `cmd6 sub24 len8 word24=1` → bits 5+14 after ~0.30 s; **auto-stop at word48≥word25**; the MCU clears the bits itself and resets **word24 ~0.18 s after the bits**. Does **not** require the wand switch (STEAMLIVE §5 — `require_wand` is now a legacy no-op; flag/ctrl/CLI kept).
* **Wand/flush (latch=0):** inject/ctrl **`wand_down`/`wand_up`** (new aliases for the `wand` type) → word44 bit0 + **word46 = switch 0/1** (the STEAMLIVE §6.3 correction: not a flow meter counter). A wand edge starts a flush **on top of** a running activity: `work_sta` = union (brew+wand = **0x5327**). **No milk auto-stop** (even > 650); stops on wand release. **No UART writes.**
* **The steam channel is independent** of the primary activity (grind/brew/water) and of their after-phases; the milk overshoot (+39 units, peak ~11 s after the auto-stop) and the boiler recovery continue through other activities.
* **The UI fields can now be asserted directly against the sim** (not only replay): `steam.kind == "wand"` during a flush (word24=0), `"auto"` during display steam (word24=1, sticky until the MCU clear), `steam.concurrent` during 0x5327, `phase_main` = EXT during brew+flush, `shown.badge` = the steam badge. New scenarios: `steam_wand_flush.json` (31 checks) + `steam_auto_overshoot.json` (24 checks).
* **Additive ctrl extensions:** `ping` now also returns `wand` (bool) and `steam` ({latch, src}|null). All existing fields unchanged.

### B. Scenario reconciling (check count unchanged except for the 2 new ones)

* `steam_auto`/`steam_manual_stop`: **display start BEFORE wand down** (wand first would start a flush, and the guards would then block the steam start). The expectations are unchanged.
* `brew`: 3 windows/values adjusted 1:1 to the live calibration (stop ~29.9 s: word51 ext to 29.5; word39 max 365; bit2 pulse from 29.5).
* `ref_client`: the state `regs` now also mirrors 3/6/9/16/19/22/24/44/46 (additive, like merakiplus).
* selftest: **120 PASS / 0 FAIL** (107 + 10 new wand/latch/overshoot/concurrency checks + 3 steam-live verify checks) — the 2 FAILs the UI work stream saw during the v6.6 verification (C.1) were the sim rework and are now closed.

### C. Verification

* **replay verify steam-live: 92.59 % → 99.608 % (PASS)**, exact words 100 %, ws sequence identical, timing ±1.5 s. v3/v2/session: 99.83/99.90/100.00 % (all PASS).
* **The suite: 25/25 scenarios · 644/644 checks** in raw EN + raw DA (V6.6 binary, md5 5658cfc3…) + REF profile (20 overrides). `steam_auto` 30/30 deterministic.
* `sim/run_e2e.py` was **not** changed; `workspace/*` and `boot/` were not touched.

*— test work stream, 2026-11-17 (v6.6-calibration). The device was not contacted; everything is local against the STEAMLIVE measurements.*

---

## ADDENDUM v6.7/v6.8 — from the UI work stream: network guard (Wi-Fi model), word 113 in the boot push, no REVERT button

### A. Unchanged
* The contract fields, the hooks and the scenarios. No file in `sim/` was changed apart from this addendum.

### B. New / changed in the SIM build
* **State:** new object `net{enabled, state (off|down|starting|connecting|up), iface, ifup, wpa_pid, dhcp_pid, carrier, ip, gw, heals, boots, heal_ms, init_runs, wpa_starts, dhcp_starts, conf_writes, fails}`. The default = a healthy stack (like runtime B); `MERAKIPLUS_SIM_NET=cold` = a slot B cold start, `=off` = the guard disabled.
* **New hook `net {name}`:** `kill | ip_drop | if_down | cold | ap_down | ap_up | status` — only affects the SIM build's MODEL of wlan0/wpa_supplicant/udhcpc (no network on the host). The network guard writes `<sandbox>/appconfigs/wpa_supplicant.conf` and `<sandbox>/tmp/merakiplus_net.log` on bring-up.
* **`layout`:** `revert` is gone (v6.8: the REVERT button on HOME has been removed; no scenario used it).
* **Boot push:** word 113 is sent as the stock UI's recorded boot push: `(preInfusionTime<<8) | 0` (GROUND-TRUTH.md). The SAVE path (sub113) is unchanged `time<<8 | breakTime` — `settings_save_matrix`'s 1541 expectations still apply.

*— UI work stream, 2026-11-17 (v6.7/v6.8). Reports: `sim/reports/v6.7-v6.8/`.*

## ADDENDUM v6.9 — from the UI work stream: display conditioning, stable weight source, weight dead band

### A. Unchanged
* The contract fields (`regs` and `regs_s` are still **raw**), the hooks, frames and the scenarios. No file in `sim/` was changed apart from this addendum.
* The sim's demo `calm` (median-3 + EMA + dead band in `mcu_sim.py`, default OFF) is not used by the UI. The conditioning lives in merakiplus and therefore also applies on the device. Run the E2E with `calm` OFF, so `regs` are the raw sensor values.

### B. New in the SIM build (all addendum fields; display only)
* **State:** `cond{raw, a_chart, a_hero, med, scale (pct|hold|max), pct, hold, head, hero_snaps, wdb, gate}`, `chart{mode, active, full, top, raw_max, disp_max, ticks, base_ticks, clipped, n, n_active}` and `weight{src (38|39), switches, shown}`. `shown` is the displayed weight in 0.1 g, signed, conditioned and with the dead band.
* **New hook `cond {…}`:** `a_chart`, `a_hero`, `med` (1|3), `scale`, `pct`, `hold`, `head`, `gate`, `raw` (0|1), `reset`. All fields are optional; without fields the state is read. `raw:1` = DIAGNOSTICS → GRAPH = RAW.
* **The weight source** for the graph and the hero is latched. Word 39 is the default. Word 38 is only used during a grind (work_sta bit 6) and its DONE. A switch requires 300 ms; a UI-started switch happens immediately. Maintenance bits (0x0400/0x0800/0x2000/0x0C00/0x2800) never switch it. `shown.hero`/`shown.stats` show the conditioned value, and \|weight\| < 0.25 g is shown as `0.0`.
* **UI-side scenario** (additive, in `workspace/sim_scenarios/`): `ui_calm_graph.json` (152 s). It covers idle with pulses, grind, brew, idle after a brew, auto steam and cup noise/tare via `set_reg 39`. Only existing event types are used (`action`, `set_reg`, `wand`).

### C. For the test work stream
* Scenario asserts on `shown.hero` during a ramp must take into account that the hero lags about 0.3 s behind the raw value (median-3 + EMA 0.25). At DONE and other phase changes it snaps to raw. No existing scenario is affected (E2E results in `sim/reports/v6.9/`).

*— UI work stream, 2026-11-17 (v6.9). Reports: `sim/reports/v6.9/`.*

## ADDENDUM v6.13 — from the test work stream: manual STOP in mcu_sim (words 16/19/22/6) + stop scenarios

**Background:** `sim/reports/v6.13/SIM_HANDOFF.md` — merakiplus v6.13 sends manual stop frames (UI_v6 §18), but mcu_sim kept running to the target. Now implemented. The contract above is **unchanged**; the SIM build (v6.13) had already implemented the stop side.

### A. New in `sim/mcu_sim.py` (the MCU model)
* **Manual stop = the MCU's own auto-stop path.** The control tasks recompute the run flags every 10 ms round (like the firmware):
  | Activity | Stop write | Sim effect |
  |---|---|---|
  | WATER | `cmd6 sub16 len1 [0]` (+ `sub19 len1 [0]` if word19≠0) | stops as soon as `word16==0 and word19==0` — **also in auto mode** (hot water ctl `0x1B8C`: active=(word19\|word16)≠0). Bits 1+3+9 are cleared, the weight stops at the **current** weight (no snap to the target), post/drip −11 + current/flow decay exactly like the weight stop. |
  | BREW | `cmd6 sub22 len1 [0]` | stops on `word22` 1→0 (brew ctl `0x1E9C`): the same `_tick_post("brew")` as the weight stop — bits 1+2+9+12 are cleared, valve pulse `0x0105` for ~0.21 s, after-drip +12, word22=0. |
  | GRIND | `cmd6 sub6 len1 [0]` | stops on `word6==0` during a grind (grind ctl `0x19C6`: runs while word6≠0; word3 is the tare trigger). Bit6 is cleared, **the weight holds** (no ramp towards target+2), cur2 decay like the auto-stop. |
* All stop writes are ACKed (exact echo, the same latency/loss profile). New assertion event **`ack`** (`sub`, `len`, `words`) on a scheduled ACK echo — used by the new sim_log assert `ack_words`.
* **Unchanged:** auto-stop (weight/milk/timeout), error stop, replay serve/verify (the recordings have no stop writes; verify rates unchanged), idle pulses, all other curves. The `activity_stop` log now has an additive `manual` field; the reason for a manual stop is `"manual stop word…=0"`.

### B. New in `sim/ref_client.py` (oracle parity, additive)
* **Ctrl actions:** `action grind_stop | brew_stop | water_stop` (the same names as merakiplus v6.13's `STOP_KEY`). A stop is not blocked by the active phase; frames = zeros only, no intent. The water stop sends `sub19=[0]` after the `sub16` ACK if word19≠0 (never half a water stop).
* **State:** new field `stop{pending, last, req, ok, fail}` — the same semantics as merakiplus's `stop` object (minus `btns`, which the reference layout does not render). `ok` is only counted once the activity's bits have been cleared (`stop_running`: grind bit6 / brew bits 1+2 / water bits 1+3) — an ACK alone does not count. 5 s without a clear or a missing ACK → `fail`.
* Toasts: `STOP: GRIND/BREW/WATER` → `GRIND/BREW/WATER STOPPED` (language-neutral assert: `contains "STOP"` / `"STOPP"` also matches merakiplus's EN/DA toasts).

### C. New in `sim/run_e2e.py` + scenarios (additive)
* **sim_log assert `ack_words`** (sub + words + window): proves the stop write's ACK byte for byte.
* **Expect kind `stop_ok`** (`min`, `max_fail`, `t0`, `t1`): reads `state.stop.ok/fail` — client-neutral (merakiplus + reference).
* **New scenarios** (auto-discovered in `sim/scenarios/`): `stop_water` (14 s), `stop_brew` (20 s, stop in the middle of the extraction), `stop_grind` (14 s), `stop_control_auto` (15 s, no stop write → auto-stop unchanged). All assert: the stop frame byte for byte, ACK, bits cleared ≤ 0.5 s, weight/flow ramp stopped, post/drip/pulse run, `stop.ok≥1`, toast STOP/STOPP, READY at the end.
* The `--quick` subset is unchanged (the stop scenarios are run specifically: `--scenario stop_water` or `--all`).

*— test work stream, 2026-11-18 (v6.13-sim). Local only; the device was not touched; `workspace/*` and `boot/` were not touched. Reports: `sim/reports/v6.13-sim/`.*

---

## ADDENDUM v6.14-sim — from the test work stream: MCU sleep/wake in mcu_sim (word0=0 → silent; button/alarm → reboot + re-push)

**Date:** 2026-11-18 · **Written by:** the test work stream (owner of `sim/`) · **Applies to:** `sim/mcu_sim.py`, `sim/ref_client.py`, `sim/run_e2e.py`, `sim/selftest.py`, `sim/scenarios/`.
**Background:** `sim/reports/v6.14/SIM_HANDOFF.md` — on a HOLD of the power button merakiplus v6.14 sends cmd8 (alarm 0/0/0) + cmd6 sub0 len2 [0,0] and expects an MCU that goes to sleep (UI_v6 §19). Now modeled. The contract above is **unchanged**; the v6.14 SIM build had already implemented the UI side (the `key` hook + `power` in the state JSON).

### A. New in `sim/mcu_sim.py` (the MCU model, MEM03 sleep task 0x2F52 / reset 0x326)

* **Sleep:** word0=0 (cmd6 sub0 len2 `[0,0]` — or any write that sets word0=0) → ACKed normally, then **≤ 0.5 s** to sleep (the status task checks word0 every 500 ms, `0x29D4`): `work_sta=0`, outputs 0 (words 62/63), running activities (primary + steam) are stopped **safely** (no after-phase — the tasks have been deleted), **the register file is kept/frozen**, and the UART is **silent**: cmd3/cmd6/cmd8 are dropped (`dvale_rx_dropped`), no replies, no 0x89. The keep-awake monitoring **pauses** during sleep (no `keepalive_gap` while switched off).
* **Wake — only two paths:** (1) a simulated button hold **≥ 0.8 s** (PC.8, 8×100 ms after a release) or (2) the **RTC alarm** (cmd8's alarm day/hour/minute ≠ 0/0/0; the RTC runs on sim time, a match during sleep → wake). Both → **CPU reset to the FACTORY start values** (registermap §2: word0=1, word11=920, word114=50, word4=180, word25=550 … — the config is **not** preloaded) + the 0x89 round every 20 ms → the host's **boot push/re-push is expected again** (`boot_push_missing` otherwise, the same window as on connect).
* **New ctrl/inject events** (JSONL via `inject`): `{"type":"power_key","value":1|0}` (down/release; a hold ≥ 0.8 s during sleep → wake), `{"type":"power_key","action":"hold","dur":1.6}` (one complete hold with automatic release), `{"type":"power_off"}` (sleep without a UART write), `{"type":"power_on"}` (wake/reset without a hold), `{"type":"alarm","day":D,"hour":H,"minute":M}` (set the alarm like cmd8's field). A button hold while the MCU is **on** does nothing in the MCU (the display owns the power-off semantics).
* **`ping` now also returns `power`**: `{state: on|off, key_down, hold_s, dvale_pending, alarm, wakes, rx_dropped}` (additive; all existing fields unchanged).
* **New assertion events:** `dvale_enter`, `power_wake` (cause: power_key/alarm/ctrl), `power_key` (value/held_s), `alarm_set`; `reboot` now has `factory`/`cause`/`defaults` (a snapshot of the start values); `time_sync` now has `alarm` ([day,hour,minute]). `stats` += `dvale_entries`, `power_wakes`, `dvale_rx_dropped`. (*dvale* = sleep.)
* **Compatibility:** ctrl `reboot` / word30=1 **still preloads the config** (`factory=False`, as before — ADDENDUM v6.5 C's proposal is thereby partly closed: a power wake gives the start values, ctrl reboot is unchanged). Replay serve/verify unchanged (the recordings have no word0=0 writes; sleep never occurs). All existing scenarios/self-tests are still green.

### B. New in `sim/ref_client.py` (oracle parity, additive)

* **Ctrl action `key`** (`{"cmd":"key","value":1|0[,"code":116]}`) — the same hook as merakiplus v6.14's SIM build. Full stock semantics (UI_v6 §19.1): HOLD **1.5 s** → cmd8 (alarm 0/0/0) → **≥ 80 ms** → cmd6 sub0 len2 `[0,0]` through the write queue → ACK → OFF. **Blocked** during brew/steam/grind (work_sta bit12/14, words 22/24/6 + its own pending; hot water does not block) → toast *STOP THE ACTIVITY FIRST · THEN OFF*, no frames, a new full hold after the activity. Without a connection/word0=0 → *NO CONNECTION · CANNOT TURN OFF*. Keep-awake **pauses** while switched off; polls continue (the MCU is silent → OFFLINE). An MCU reply word0=1 for 0.6 s → PowerOn (`sub0=[1]` + `sub1=[0]`) + HOME + toast *MACHINE IS ON*; 10 s without a reply → back to OFF. The re-push after an MCU reset happens via the existing 0x89 detection (`boot_pushed=False` → `boot_push()` again).
* **State:** new field `power{state: on|turning_off|off|turning_on, backlight_off, presses, short, offs, ons, blocked, fails, ack8, auto_on, wake_timeouts}` — the same shape as merakiplus's; `page="off"` while the off page is shown.

### C. New in `sim/run_e2e.py` + scenarios (additive)

* **Expect kinds:** `power` (state in a window), `power_hold` (state over an interval, min_ratio), `power_counter` (counter ≥ min).
* **sim_log asserts:** `dvale_seen`, `no_acks_between` (silent UART), `time_sync_seen` (optional `alarm0`), `power_wake_seen` (`cause_contains`), `reboot_seen` (optional `factory`), `factory_defaults` (word0=1/word11=920/word114=50 in the reboot snapshot); `boot_push` now accepts `t0` (only a push after that time = re-push).
* **New scenarios** (auto-discovered): `power_off_wake` (55 s: power off → silent sleep for 34 s → sim button hold → reboot to factory defaults → re-push restores the config → PowerOn/HOME/READY) + `power_during_activity` (42 s: a hold during a brew is blocked with a toast and no frames, the brew auto-stops undisturbed, a new hold after the brew powers off normally). Both green against the **v6.14 SIM build** (EN+DA) and the **reference client** (REF profile).
* The `--quick` subset is unchanged (the power scenarios are run specifically: `--scenario power_off_wake` or `--all`).

*— test work stream, 2026-11-18 (v6.14-sim). Local only; the device was not touched; `workspace/*` and `boot/` were not touched. Reports: `sim/reports/v6.14-sim/`.*

---

## ADDENDUM v6.15 — the HOME graph resets on activity start (state.chart fields, expect kind `chart`)

**Date:** 2026-11-19 · **Applies to:** `sim/run_e2e.py`, `sim/scenarios/graph_reset.json`, `sim/sim_ui_profile-ref.json`. The contract above is **unchanged**; everything here is additive.

* **merakiplus SIM build, state JSON:** `chart` keeps its v6.9 fields and adds `buf` (samples in the graph ring), `w_max` / `w_top` (the weight line's maximum and axis top in brew/water mode, 0.1 g; 0 in other modes), `resets` (idle → active clears since start), `reanchors` (re-clears within 1.5 s of a start: late tare / late weight-source latch) and `act_ms` (ms since the current curve started, −1 = none). Display only: `regs`/`regs_s` stay raw.
* **Expect kind `chart`** (`run_e2e.py`): `{"kind":"chart","t0":…,"t1":…,"field":"buf","min":…,"max":…,"mode":"any"?}` — every sample in the window that has `chart.<field>` must be in `[min,max]` (`"mode":"any"`: at least one). Extra keys (e.g. `"label"`) are ignored by the evaluator and let profiles match one expectation exactly.
* **Scenario `graph_reset`** (46 s): `set_reg` word 39 = 2500 (a 250 g cup) at rest → UI brew at 8 s (the sim tares word 39) → `wand_down`/`wand_up` at 18/22 s (flush during the extraction). Green against the merakiplus v6.15 SIM build (EN + DA, 21/21).
* **REF profile:** the reference client has no HOME graph, so `sim_ui_profile-ref.json` replaces the 6 `chart` expectations 1:1 with phase/word checks for the same windows (`phase` READY/PRE/STEAM, `phase_hold` EXT, `word_hold`/`word_min_seen` 39). Green against the reference client (21/21).
* `--quick` is unchanged (run `--scenario graph_reset` or `--all`).


---

## ADDENDUM v6.16 — clock, planned pre-heat and auto off (state.clock / state.sched, `ui_env`, expect kind `state`)

**Date:** 2026-11-19 · **Applies to:** `sim/run_e2e.py`, `sim/mcu_sim.py`, `sim/scenarios/{time_display,timemode_stock_parity,schedule_prewarm}.json`. The contract above is **unchanged**; everything here is additive.

* **merakiplus SIM build, state JSON:** `clock` = `{utc, valid, local "YYYY-MM-DDTHH:MM:SS", wday, timemode, tz, dst, shown}` (`shown` = HOME's top line, stock `update_time` format) and `sched` = `{preheat, at "HH:MM", repeat once|daily|mon-fri|sat-sun, auto_off (min), once_at, file, alarm [d,h,m] (what a power-off would send now), next_utc, armed [d,h,m]|null (last pre-arm sent), arm_sent, arm_ack, idle_s, auto_offs, home (HOME's pre-heat line)|null}`.
* **merakiplus SIM build, environment:** `MERAKIPLUS_SIM_CLOCK=YYYY-MM-DDTHH:MM:SSZ` sets the UI's UTC wall clock at start (it then runs in real time); `MERAKIPLUS_SCHEDFILE` overrides the schedule file (default `<sandbox>/new_customer/merakiplus/schedule`). **ctrl:** `{"cmd":"clock","utc":"…Z"}` jumps the clock, `{"cmd":"clock","advance_s":N}` moves it; `save` accepts `preheat`, `preheat_at` (minutes after midnight, step 5), `preheat_repeat` (0 once, 1 daily, 2 Mon–Fri, 3 Sat–Sun), `auto_off` (0..4 = off/15/20/30/60 min).
* **Scenario key `ui_env`** (`run_e2e.py`): extra UI environment variables (e.g. `MERAKIPLUS_SIM_CLOCK`). **Scenario key `ui_only`:** the scenario has no reference-client oracle; with the reference client it is reported as SKIP (one informational check), never as a fake pass of its checks.
* **Expect kind `state`:** `{"kind":"state","path":"clock.shown","t":…,"window":…|"t0":…,"t1":…,"equals"|"contains"|"not_contains"|"min"|"max":…,"mode":"any"|"all"}` — a dotted path into the state JSON. **`sim_log` `time_sync_seen`** additionally accepts `"alarm":[d,h,m]` (exact), `"max_count":N` and `"count":0` (none in the window).
* **`mcu_sim.py`:** a CPU reset (`do_reboot`, incl. the alarm/button wake) clears the cmd8 alarm (it lives in MCU RAM 0x20000E18; the RTC keeps counting) and logs `alarm_cleared` — the host has to re-arm it. All existing scenarios are unaffected (`sim/selftest.py` 187/0).
* **Scenarios** (all `ui_only`): `time_display` (40 s; 24 h → 12 h → summer clock with/without DST → back), `timemode_stock_parity` (30 s; the TIME FORMAT row and the clock vs. config `timemode` 1/0/1), `schedule_prewarm` (80 s; pre-arm [18,6,30] → power-off with the same alarm → deep sleep → RTC 06:30 → **alarm wake** → factory reset → re-push → ON → re-arm [19,6,30] → PRE-HEAT OFF disarms with 0/0/0).
* `--quick` is unchanged.
* **Timing margin (v6.16):** in `power_during_activity` (second hold) and `schedule_prewarm` the button release moved from 1.6 s to 2.1 s after the press. The power-off fires at 1.5 s of hold regardless of the release; with 1.6 s a ctrl call delivered ~270 ms late on a busy CI runner turned the hold into a short press (full-E2E DA run on 697961b). Checks and windows are unchanged.


---

## ADDENDUM v6.17 — IA consolidation: one page per feature, SYSTEM in sections (state.tab / state.shown, `page`, `layout`)

**Date:** 2026-11-20 · **Applies to:** `src/merakiplus.c` (SIM build), `sim/scenarios/{ia_navigation,feature_page_start,activity_takes_home}.json`, `sim/play_viewer.html`. The §3 `page` keys are **unchanged** (`home | settings | detail | sys | diag | bye | off`): the four feature pages and SYSTEM all report `"settings"`. Background: `docs/ux-audit-v617.md`.

* **State `tab`** is now `home | grind | brew | water | steam | system | diag` (v6.4–v6.16: `home | brew | steam | system | diag`). Nothing in the existing scenarios read it.
* **State `shown` on a list page** (`page: "settings"`): `title` (the page's title in the UI language), `rows` (rows in the list right now), `scroll`, **`live`** (the line under the title: live values, or the reason a start is blocked) and **`section`** (`display | clock | schedule | stock`, `null` when SYSTEM is closed or on a feature page). SYSTEM closed = 5 rows (4 section headers + DIAGNOSTICS).
* **ctrl `page`:** `name` = `home | grind | brew | water | steam | system | diag | detail | sys`. `settings` is still accepted with `tab` 0–4 (grind, brew, water, steam, system; default 1 = brew — v6.4–v6.16: 0–2 = brew, steam·water, system). `system` takes an optional `"section"` to open. `detail` (with `param`) opens the editor on the page the row lives on (SYSTEM: with its section opened); `sys` opens BOOT UI / SWITCH from SYSTEM › STOCK UI.
* **ctrl `layout`:** HOME = `grind`, `brew`, `water`, `steam` (while idle; a **tap** opens that feature's page, a **hold ≥ 650 ms** starts it), `gear` (→ SYSTEM), `diag` (only while HOME shows the error/offline hint) and the STOP buttons while something runs. **The `edge_left` / `edge_right` targets are gone** (no carousel, no invisible edge zones). Every page but HOME: `back` (the header chevron); list pages: one target per visible row — the parameter id, `sec_display | sec_clock | sec_schedule | sec_stock` (section headers), `boot_ui`, `switch_now`, `diag` — and `start` (HOLD TO START) on a feature page; DIAGNOSTICS: `graph` while the pill is in view. `swipe` in the reply now reads "right = back".
* **Behaviour the scenarios rely on:** `save`/`action`/`boot_flag`/`runtime_switch` are unchanged (same frames, same guards). A feature page never stays on screen while an activity runs: the UI returns to HOME (`tab: "home"`), where the STOP button is; SYSTEM and DIAGNOSTICS stay.
* **Scenarios** (all `ui_only`): `ia_navigation` (34 s, 45 checks; real taps through HOME → BREW page → editor → back, SYSTEM sections, DIAGNOSTICS and back — no frame, no config write), `feature_page_start` (43 s; a brew started with HOLD TO START on the BREW page = the `brew` scenario's expectations shifted by 2.15 s), `activity_takes_home` (22 s; the GRIND page is open when the wand starts steam → HOME; SYSTEM stays).
* `--quick` is unchanged (idle, brew, steam_auto, no_ack, out_of_spec).


---

## ADDENDUM v6.18 — sound: start jingle + per-boiler ready tones (state.audio, `boilers` event, `sim.init_events`, `sandbox_files`)

**Date:** 2026-11-21 · **Applies to:** `src/merakiplus.c` (SIM build), `sim/mcu_sim.py`, `sim/run_e2e.py`, `sim/play.py`, `sim/play_viewer.html`, `sim/scenarios/audio_{boiler_ready,muted,power_cycle,sound_off}.json`. The contract above is **unchanged**; everything here is additive. Background: [docs/audio.md](../docs/audio.md).

* **merakiplus SIM build, state JSON `audio`:** `mode` (`off | on | stock`), `ready_mask` (0 off, 1 brew, 2 steam, 3 both), `vol` (config `volume` capped at 70), `machine` (0 = no link, decision held · 1 = off/asleep/turning on · 2 = on), `armed` / `ready` (`[extract, steam]`), `cold_boot`, per event `n_<event>` (detected) and `played_<event>` for `start_jingle`, `boiler_ready_extract`, `boiler_ready_steam`, `preview`; `n_suppressed` (dropped during an activity), `n_muted` (volume 0), `n_off` (SOUND / READY TONE off); `log` = the last 8 events oldest first as `event:outcome,…` (outcomes `played | off | muted | suppressed | noplayer | dropped`); `file` (what the speaker would get); `last`. The SIM plays nothing. `regs` now also lists words **62** and **63**.
* **merakiplus SIM build:** `MERAKIPLUS_SOUNDFILE` overrides the sound file (default `<sandbox>/new_customer/merakiplus/sound`); a cold display boot is read from `<sandbox>/proc/uptime` (< 120 s). **ctrl `save`** accepts `sound` (0..2) and `ready_tone` (0..3); `layout` has the row targets `sound` / `ready_tone` (and `volume`, now in `sec_display`).
* **`mcu_sim.py` — event `boilers`:** `{"type":"boilers","e":<0.1 °C>,"s":<0.1 °C>,"e_rate":<0.1 °C/s>,"s_rate":<0.1 °C/s>}` (all keys optional) sets the boiler temperatures and **switches on the heat model**: words 62/63 are then the MCU's heat stages (`McuSim.heat_stage`: extract 40/60 °C/word 11, steam 50/80 °C/gear target − 10 °C; 100 while work_sta bit 12 / bit 5; 0 while asleep). `"model": false` switches it off again. **Without the event nothing changes** (62 = 63 = 100 while awake, as before), so no existing scenario is affected (`sim/selftest.py` 192/0, T14 new).
* **Scenario keys (runner):** `sim.init_events` = sim events injected after the sim starts and **before the UI starts** (e.g. cold boilers); `sandbox_files` = `{relative path: text}` written into the sandbox before the UI starts (e.g. `proc/uptime`, the sound file); paths must stay inside the sandbox.
* **play.py:** `POST /api/boilers` (same keys as the event); the viewer has an *Audio (v6.18)* card with the log and *Cold boilers / Cool brew boiler / Hot* buttons.
* **Scenarios** (all `ui_only`): `audio_boiler_ready` (44 s, 27 checks: cold boot + cold boilers → jingle once, extract tone at ~11 s, the steam boiler's own tone at ~21 s, cool-down → re-armed → extract tone again, no UART write), `audio_muted` (24 s, config volume 0 → events logged `muted`, nothing played), `audio_power_cycle` (46 s: hot boilers at a cold boot → jingle + tones; button OFF → silent; wake → jingle + tones again; READY TONE = steam only), `audio_sound_off` (40 s: ctrl SAVE `sound` = 0 → events logged `off`; SOUND on + READY TONE brew → only the extract tone). Green EN + DA.
* `--quick` is unchanged.

## ADDENDUM v6.20 — steam during a brew (relaxed steam guard, HOLD·STEAM, sim: brew during steam + heater rule)

**Date:** 2026-11-22 · **Applies to:** `src/merakiplus.c` (SIM build), `sim/mcu_sim.py`, `sim/selftest.py`, `sim/scenarios/concurrent_brew_steam.json` (rewritten), `sim/scenarios/concurrent_brew_steam_stops.json` + `concurrent_steam_blocked.json` (new). Everything is additive. Background: [docs/concurrent-brew-steam.md §12](../docs/concurrent-brew-steam.md#12-implementation-v620).

* **merakiplus SIM build:** ctrl `action` `steam` now **starts** steam while a brew (PRE/EXT) is the only activity (before: guard `TX_G_BUSY`); steam during grind / hot water / steam and every other start during an activity still answer `ok:false` with the guard id. **State JSON `stop.steam_hold`:** `"on"` (HOLD·STEAM shown and a hold would start steam), `"dim"` (shown, the guard says no), `null` (not shown). `stop.btns` is unchanged (`["brew"]` while HOLD·STEAM is in the right-hand slot). **`layout`** lists `steam_hold` (the right-hand 160 × 60 slot) next to `brew_stop` (left-hand slot) while it is shown.
* **`mcu_sim.py`:** (1) a brew trigger (word 9 + word 22 = 1) is accepted while steam runs (mode `steam`), as the MCU's brew-ctl does; the steam bits stay set. Grind / water triggers during steam are still not consumed. (2) **Heater rule for latched steam during a brew** (MCU heater task 0x3502): steam boiler word 47 falls 6.5 units/s with no steam-heater slot until it is below target − 100 (target by gear word 14: 1200/1250/1300, else 1350; none during phase-controlled pre-infusion), then work_sta **bit 13** (`0x7327`) and cur1 ≈ 7950 until the target; the brew keeps words 51/52. Steam alone and the wand flush are unchanged. `sim/selftest.py` T15 (3 checks) → 195/0.
* **Scenarios** (all `ui_only`): `concurrent_brew_steam` (66 s: UI brew, UI steam at 4 s during pre-infusion → `sub24 [1,650,…]`, `0x5327`, `stop.steam_hold` `on` → `null`, `stop.btns` `["brew","steam"]`, bit 13 in the overlap, brew stops by weight, steam continues `0x4121` and stops by milk; no word 22 write), `concurrent_brew_steam_stops` (30 s: STOP STEAM mid-brew → `sub24 [0,650,…]` only, brew continues, HOLD·STEAM back; steam again via a direct `set_reg` word 24 = 1; STOP BREW while steaming → `sub22 [0]` only, steam continues; STOP STEAM; `stop.ok` 1/2/3), `concurrent_steam_blocked` (30 s: UI steam refused with `TX_G_BUSY` during grind and hot water, `TX_G_ERROR` after an E2 during a brew; no word 24 write ever). All three fail against the v6.19 build.

## ADDENDUM v6.19 — sound: no ready tone at a power-on with hot boilers (scenarios only)

**Date:** 2026-11-21 · **Applies to:** `sim/scenarios/audio_boot_hot_no_ready.json`, `sim/scenarios/audio_cold_to_hot_ready.json` (new), `sim/scenarios/audio_power_cycle.json` (expectations). The contract above, `mcu_sim.py`, `run_e2e.py`, `play.py` and the state JSON are **unchanged**. Background: [docs/audio.md §2.6](../docs/audio.md#26-power-on-with-hot-boilers-v619).

* **Rule under test:** at every "on" (cold boot of the display, OFF → on, sleep → wake) a boiler already at heat stage 100 counts as announced — only the start jingle plays; a ready tone needs a cold → hot transition during the session.
* **Scenarios** (all `ui_only`): `audio_boot_hot_no_ready` (42 s, 26 checks: cold boot with both boilers hot → jingle only, latches `[0,0]`; sleep → wake while hot → second jingle, still no ready event; the extract boiler cools to 65 °C → `armed [1,0]` → heats up → its tone once; no UART write), `audio_cold_to_hot_ready` (38 s, 22 checks: boot hot → jingle only; both boilers go cold at 6 s → armed → extract tone, then the steam tone, each once). `audio_power_cycle` now expects the jingle only at both power-ons and no ready event (25 checks). Green EN + DA; the three fail against the v6.18.1 SIM build.


## ADDENDUM v6.22 — water dosing gate + manual hot water (state.water, `mcu_version` event, word 19 alone starts water)

**Date:** 2026-11-22 · **Applies to:** `src/merakiplus.c` (SIM build), `sim/mcu_sim.py`, `sim/selftest.py`, `sim/scenarios/dosing_{version_detect,gated_old_mcu,scale_fault}.json` (new). Everything is additive; the contract above is unchanged. Background: [docs/water-dosing-compat.md §10–§11](../docs/water-dosing-compat.md#11-implementation-v622).

* **Baseline:** words 80..87 = `"V5.1.4"` (ASCII, lo byte first) and 88..95 = the same copy — already in `SENSOR_BASELINE`, so every existing water scenario (`hotwater`, `stop_water`, `stop_control_auto`, `activity_takes_home`, `concurrent_steam_blocked`, `replay_v2`, `settings_save_matrix`) keeps its meaning.
* **`mcu_sim.py` — event `mcu_version`:** `{"type":"mcu_version","version":"V5.1.2"}` (copy = the same), `"copy":"V5.1.3"` (mismatch), `"copy":null` (88..95 untouched), `"version":""` (not reported). Survives a controller reset. Logged `mcu_version` (`dosing` = the stock rule `> 5.1.2`). A write of word 16 ≠ 0 while the reported version is not > 5.1.2 is logged **`legacy_word16`**.
* **`mcu_sim.py` — hot water:** a write of **word 19 = 1 alone** (sub19 len1, word 9 = 0, word 16 = 0) starts MANUAL hot water (+0.18 s, as hotwater-ctl `0x1B8C`: `active = word19 | word16`). MANUAL has no weight stop and no timeout; word 19 = 0 stops it. A **scale fault alone** (word 35 & `0x1400`, nothing else in 34/35/36) stops AUTO (word 16 = 0, reason `scale fault …`) but not MANUAL; any other error bit still safety-stops. `sim/selftest.py` T16 (9 checks) → 204/0.
* **merakiplus SIM build, state JSON `water`:** `mcu_ver` (the string as reported, upper case, `""` when empty/not polled), `ver_state` (`none | ok | bad | copy`), `ver_dosing` (version > 5.1.2), `dosing` (+ no scale fault), `scale_fault`, `start` (`auto | manual` — what the next start sends), `run_manual` (the running/last water run is MANUAL), `rows` (rows on the WATER page: 2 or 0), `fault_n`, `fault_stops` (word 16 = 0 frames sent on a scale fault), `diag_ver` (DIAGNOSTICS › CONTROLLER › FIRMWARE as drawn), `diag_wd` (`TX_DV_WD_AUTO | TX_DV_WD_MAN | TX_DV_WD_FAULT`). ctrl `action water` sends the manual frame when `start` is `manual`; `water_stop` on a controller not > 5.1.2 sends word 19 = 0 only.
* **Scenarios** (all `ui_only`): `dosing_version_detect` (18 s, 28 checks: V5.1.4 → dosing, DIAGNOSTICS values, WATER page 2 rows, the unchanged AUTO frame, weight stop, no word 19 write), `dosing_gated_old_mcu` (36 s, 44 checks: V5.1.2 → no rows, HOLD TO START → `sub19 [1]`, runs past the target with no stop, STOP → `sub19 [0]`, no word 16 / sub9 write ever; at runtime empty → manual, copy mismatch → manual, V5.1.4 → 2 rows; config `hotWaterMode` stays 1, no config write), `dosing_scale_fault` (34 s, 34 checks: AUTO dose stopped by a scale fault, manual only + `TX_DV_WD_FAULT`, start refused with `TX_G_ERROR`, dosing again after the fault; MANUAL on V5.1.2 keeps running through a 0x0400 fault, STOP = `sub19 [0]`). All three fail against the v6.21 SIM build.
* `--quick` is unchanged.
