# BOOT — boot persistence + on-device UI switching (MerakiTech Gen 1, Sanbon SAT62091M)

> **Note (public edition):** Development notes written during development. Paths such as
> `artifacts/…`, `boot_install.py` steps and md5 tables refer to the author's working environment; as
> an end user, use `./install.sh` and `./revert.sh` (see the [README](../README.md)). The
> manufacturer's files are not included; IP addresses and other personal data have been removed.
> References to `FINDINGS`, `REVERT.md` and `GESTUS-FIX.md` are to the author's working notes; the
> gesture fix is published as [gesture.md](gesture.md).
>
> **Since v6.21** `/new_customer/demo_v9` is no longer the UI itself but Meraki+'s **launcher**
> (`src/merakiplus_boot.c`): the watchdog and the gesture switcher start it, and it `exec`s (same PID) the UI from
> `/new_customer/merakiplus/rel/<version>-<sha8>/demo_v9`, or — when no Meraki+ version works — the stock UI from slot A.
> Where this document says "merakiplus = `/new_customer/demo_v9`", read "the launcher, then a release". The switcher
> treats both paths as B mode. Details: [ota-plan.md phase 1b](ota-plan.md#phase-1b--implemented-2026-11-22).

**Date:** 2026-11-17 · **Status:** PREPARATION — everything built and verified LOCALLY (self-tests +
mock dry run). The device is offline; device runs happen later via `boot_install.py` when the user is
at the machine. No contact with the device took place during preparation.

**Ownership:** this directory (`workspace/boot/`) belongs to the boot work stream. The UI work
stream owns `merakiplus.c`, `UI_v6.md`, `REVERT.md`, `round_v6/` — those files were NOT touched. The
UI changes required from the UI work stream are specified in **§9 (UI requirements)** below.

---

## 1. The mechanism

### 1.1 Vendor A/B boot (unchanged, reused)
`/customer/demo.sh` (the vendor boot script, copy: `artifacts/demo.sh`):
1. PWM backlight + GPIO panel init + `pinit.sh`.
2. Mounts the UBI volume `ubi0:new_customer` → `/new_customer` (**slot B**). `MOUNT_SUCCESS=1` on
   success.
3. OTA handling (§7): `/customer/temp.tar.gz` is unpacked into the *opposite* slot.
4. insmod list (touch `tw31xx.ko`, buttons `k62091.ko`, network, USB…).
5. **Slot branch:** if the flag file `/customer/new` exists AND the mount succeeded →
   **boot B**: `cd /new_customer; export LD_LIBRARY_PATH=…:/new_customer/libs:/config/wifi;
   ./demo_v9 &; /customer/test.sh new_customer &`.
   Otherwise → **boot A**: the same pattern from `/customer` with `/customer/libs`.

**The flag `/customer/new` is the entire persistence mechanism:** `touch` = boot B, `rm` = boot A. No
other persistent state is required.

### 1.2 The watchdog — only the NAME is checked
`/customer/test.sh` (copy: `artifacts/test.sh`) runs a 1 s loop: `ps -A | grep "demo_v9"` → empty? →
`resetdemo <slot>` (cd + LD_LIBRARY_PATH + `./demo_v9 &`). It **only checks that a process name
matches `demo_v9`** — no heartbeat, no exe path, no slot. Consequences (all used/deliberate):
* Our UI installed as `/new_customer/demo_v9` satisfies the watchdog (the name is `demo_v9`).
* A SIGSTOPped process counts as "alive" (the PID exists) — the phase 2 pattern.
* **Watchdog race on a runtime switch:** if the PID disappears completely, the watchdog restarts
  *its own slot's* binary within ~1 s. A naive "kill A → wait → launch B" can therefore end with A on
  top of B (two UIs fighting over fb/UART). The solution is **SIGSTOP-first** (§3.2).
* The watchdog's slot is fixed at boot (`test.sh customer` / `test.sh new_customer`). After a runtime
  switch, the watchdog slot may therefore differ from the running UI — harmless: crash → the watchdog
  restarts the *boot slot's* binary (safe fallback), and the next reboot follows the flag again.

### 1.3 The two switching directions
| Direction | Who | How | Reboot? |
|---|---|---|---|
| A→B (stock→merakiplus) | **the switcher daemon** (this deliverable) | secret gesture (§2) → SIGSTOP-first switch (§3.2) | no |
| B→A (merakiplus→stock) | **merakiplus itself** (UI work stream, §9) | "Switch to original NOW" button: launch A, exit | no |
| Boot choice A/B | the user / `boot_install.py persist` | `touch`/`rm /customer/new` | yes (next boot) |

In B mode merakiplus grabs `/dev/input/event0` exclusively (`EVIOCGRAB`, cf. FINDINGS §5.10) → the
switcher gets no events and idles. When merakiplus exits (the B→A button), the grab is released
automatically (process death, also on `kill -9`), and the switcher observes again → the gesture can
switch back to B. **Both directions work without a reboot, and the switcher never has to handle B→A
itself.**

---

## 2. Gesture specification

**v6.12 ([gesture.md](gesture.md)): the default is now ONE continuous CENTER hold ≥ 5000 ms** within a
radius of 40 px around (240,240) — triggered on release. The corner gesture below is kept as a
fallback (`SWITCHER_GESTURE=corner`). The center is verified background on the stock home screen;
radius 40 (not ~60) because the nearest button edge is ~45 px from the center (measured:
measure_stock_home.py). Env: `SWITCHER_HOLD_MS`, `SWITCHER_CENTER_X/Y/R`. Grab bypass (the stock UI's
EVIOCGRAB): multi-node read-only (a) + sysfs fallback (b) + LD_PRELOAD shim in the guard line (c) —
see [gesture.md](gesture.md) §3.

| Parameter (corner fallback, the legacy default before v6.12) | Value |
|---|---|
| Zone | top-left corner: **x < 90 and y < 90** (480×480 screen pixels) |
| Recommended point | **(75,75)** — inside the round active area (distance to the center ≈ 233 < 240) and background on the stock home screen |
| Count | **3** long presses |
| Hold time per press | **≥ 1500 ms** (the finger must stay in the zone for the whole hold) |
| Window | first press down → third press up **≤ 6000 ms** |
| Example timing | 3×1600 ms hold + 2×300 ms pause = 5400 ms (tinj_hold's `gesture corner`) |
| Cooldown | 10 s after a trigger (no repeated switches) |

Details:
* A press ONLY counts if it started in the zone, stayed in the zone for the whole hold and lasted
  ≥ 1.5 s. Short taps and presses outside the zone are ignored (they do not reset the sequence). If
  the window expires, counting starts over.
* Coordinates are scaled from touch raw with the same formula as the inverse in tinj.c:
  `screen = (raw−min)·480/(max−min+1)`. The switcher reads the axis range via `EVIOCGABS` when opening
  (v6.12: fallback constant 0..480 = the device's actual range, not 4095; the SIM's streams pass
  `SWITCHER_RAW_MAX=4095`).
* **Safety on the stock UI:** the zone is background on the home screen — the Grinder circle
  (center ≈ (155,155), radius ≈ 68) does not reach into x<90, y<90 (the nearest point (89,89) is ≈ 93
  from the circle's center). Verified against `fb_after_revert.png`. On the sleep screen the zone is
  also background. The gesture should be performed from the home/sleep screen; on other stock pages
  the corner may contain navigation (harmless — actuators are only started from the four activity
  buttons with confirmation).
* **Read-only:** the switcher opens the node `O_RDONLY|O_NONBLOCK` and NEVER writes to it. The stock
  UI's input queue is not affected (evdev delivers to all readers; only `EVIOCGRAB` would steal
  events — the switcher never does that).
* Physical reach caveat: whether the touch controller reports all the way out into the square corners
  (outside the round glass) has not been verified live. That is why (75,75) is recommended, which is
  safely inside the round area. Injection via tinj_hold works regardless (an evdev write bypasses the
  controller).

---

## 3. The switcher daemon (`switcher.c`)

### 3.1 Behavior
* Static ARM binary (`zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static`, 0 warnings with
  `-Wall -Wextra -Werror`). No dependencies, no libc requirement beyond musl.
* Started from demo.sh's guard line (§4) — in **both** boots. In B mode it idles (merakiplus's grab starves
  it of events, by design).
* Input discovery (v6.12, grab bypass a): ALL touch nodes are opened read-only at the same time
  (event0..9 with `ABS_MT_POSITION_X`, names "TECHWIN"/"tw31xx" preferred) — a duplicate node that the
  stock UI does not grab delivers events. Override: `SWITCHER_INPUT=<node>`. Sysfs fallback (b):
  `lu_coordinate_x/y` + `point_num` polls if evdev is blind (`SWITCHER_SYSFS_DIR`, `SWITCHER_SOURCE`,
  `SWITCHER_SYSFS_SCREEN`).
* **Grab conflict/open error = idle loop:** if the node cannot be opened (or disappears), it retries
  every 500 ms without crashing. A grabbed node can still be opened — it just delivers no events
  (that is the B mode idle).
* Loop: `poll` with a **50 ms** timeout; read errors (EAGAIN normal, ENODEV/EIO → reopen). No writes
  to UART/fb/config/input. No reboot. No grab.
* Single instance: `flock` on `/tmp/switcher.lock` (an extra instance exits immediately).
* Log: `/tmp/switcher.log` (UTC timestamps, rotation at 64 KB). B's stdout/stderr on a runtime switch
  → `/tmp/merakiplus_boot.log`.
* Signals: SIGTERM/SIGINT → clean exit (used by revert/emergency).
* Gesture timing follows the **event timestamps** (kernel `input_event.time`), not the wall clock —
  robust against scheduling delays and makes file-mode testing possible.
* Test hooks (env): `SWITCHER_INPUT_FILE` (raw evdev stream from a file/FIFO), `SWITCHER_RAW_MAX`
  (file-mode default is now 480 = the device's range), `SWITCHER_LOG`, `SWITCHER_LOCK`,
  `SWITCHER_GESTURE`, `SWITCHER_HOLD_MS`, `SWITCHER_CENTER_X/Y/R`.

### 3.2 Mode check + SIGSTOP-first switch (A→B)
On a trigger:
1. `pidof demo_v9` → for each PID: `readlink /proc/<pid>/exe`.
   * `/customer/demo_v9` → **A**; `/new_customer/demo_v9` → **B**; other/none → ignore.
   * B running (or mixed A+B) → **no action** (logged). No demo_v9 → no action.
2. **SIGSTOP A's PID** — the watchdog still sees the PID → no restart race.
3. `fork` + `setsid` + `chdir /new_customer` + `LD_LIBRARY_PATH=/new_customer/libs:/config/wifi` +
   `execl /new_customer/demo_v9` (argv0 `./demo_v9` so the watchdog grep matches).
4. Verify B: `/proc/<child>/exe == /new_customer/demo_v9` within 2 s.
5. **SIGKILL A** + wait for exit (≤ 2 s). Log "switch A→B DONE" ("skift A→B FÆRDIG" in builds before the English log text).
6. **Fallback if B did not come up:** SIGCONT A + kill the B fork → original UI back, no harm. (A held
   the fb/UART fds while stopped; B could therefore not own them alone until A was gone — but B
   initializes fb/UART while A is stopped, exactly like the phase 2 pattern, and the MCU resyncs on the
   UART header, cf. FINDINGS §5.7.)

Worst case on a power loss in the middle of a switch: the flag is unchanged → the next boot follows the
flag (A if not set). demo.sh's guard line is `[ -x … ] &&` → a missing binary = no switcher, the stock
UI boots normally.

### 3.3 Self-test (locally, without the device)
* `switcher_selftest` (host build of the same C file, `-DSWITCHER_SELFTEST`): **29 deterministic
  checks** — gesture happy path, 2 holds, too short, outside the zone, too slow a window, finger leaving
  the zone, limits (89/90), raw scaling, mode classification, decision logic ([A]→switch, [B]→idle,
  [A,B]→idle, [A,A]→idle, []→idle), repeated trigger, window expiry, taps between holds, MT-only
  stream. All green.
* `test_switcher.py`: end-to-end through **the real daemon code** (`switcher_host`, file mode):
  synthetic 16-byte evdev stream (positive + 4 negative) → proves the trigger + that the mode check
  executes and is safe without demo_v9 ("no demo_v9 → no action"). Evidence: `test-logs/`.
* Known bug found and fixed by the self-test: the tracking ID arrives before the position in the same
  batch → the corner position is now latched at the first `SYN_REPORT` after the down event.

---

## 4. Installation procedure (when the device is online)

**Prerequisites:** the device in **A mode** (stock UI running, cf. FINDINGS §5.8 status),
`/new_customer` mounted, config at baseline (otherwise restore via REVERT.md first), and **OTA blocked
or the risk accepted** (§7 — recommended: block `oss.sanbontft.com` on the firewall BEFORE the
install). Run one step at a time; every step has gates and logs everything to `boot/install.log`.
Error → automatic emergency (§5.3).

```sh
cd workspace/boot

# 0. (optional, already done locally) dry run against a mock root:
python3 boot_install.py mock-setup --mock-root mock_root
python3 boot_install.py backup     --mock-root mock_root
python3 boot_install.py install    --mock-root mock_root
python3 boot_install.py boot-test  --mock-root mock_root
python3 boot_install.py revert-test --mock-root mock_root
python3 boot_install.py persist    --mock-root mock_root --default-boot A

# 1. DEVICE — backup (md5 + copies, flag recorded, config baseline gate):
python3 boot_install.py backup
#    → /new_customer/merakiplus/boot/demo.sh.bak      (md5-verified copy of demo.sh)
#    → /new_customer/merakiplus/boot/demo_v9.vendor   (md5-verified copy of B's vendor binary)
#    → state.json with the flag state + config md5; local snapshots in boot/backups/

# 2. DEVICE — install (upload + ONE line in demo.sh):
python3 boot_install.py install
#    → switcher, tinj_hold, fbpage → /new_customer/merakiplus/boot/
#    → workspace/merakiplus_v6 (md5 READ AT RUN TIME → picks up the UI's v6.1)
#      pushed as /new_customer/demo_v9 (md5 round trip via push_nc)
#    → demo.sh: the guard line is inserted BEFORE the slot branch, AFTER mounts/insmod (v6.12:
#      the shim export is combined into the SAME line — exactly ONE line kept):
#        [ -x /new_customer/merakiplus/boot/switcher ] && { [ -f /new_customer/merakiplus/boot/grab_shim.so ] && export LD_PRELOAD=/new_customer/merakiplus/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; /new_customer/merakiplus/boot/switcher & }
#      (atomic: cp→/customer/.demo_sh.tmp, md5 gate, mv, chmod 755, sync;
#       a legacy guard line from v6.11 is upgraded automatically to the new one)
#    → the revert line is logged (REVERT_LINE in install.log + state.json)
#    Idempotent: run again → the guard line is recognized, nothing is inserted twice.

# 3. DEVICE — boot test (3 reboots + gesture; the machine is left in A, flag gone):
python3 boot_install.py boot-test
#    a) touch flag + reboot → verify B: exe link=/new_customer/demo_v9,
#       pidof switcher, fb capture (test-logs/boot_B_merakiplus.png)
#    b) rm flag + reboot → verify A: exe link=/customer/demo_v9 + fb capture
#    c) gesture via tinj_hold (3×1.6 s at (75,75)) → verify the runtime switch to B
#       WITHOUT a reboot: exe link + fb + /tmp/switcher.log saved
#    d) switch back to A (SIGSTOP-first kill+launch; merakiplus's button can also be used)

# 4. DEVICE — the user's persistence choice (applies at the NEXT boot, no reboot now):
python3 boot_install.py persist --default-boot B   # our UI as the default
python3 boot_install.py persist --default-boot A   # stock as the default (gesture switch still possible)
```

`--default-boot A|B` can also be given at the end of any other step (set/removed at the end, after the
step has succeeded). `--force` overrides the config baseline gate (logged; only use it if the config
drift is understood).

**Manual verification after install (telnet):**
```sh
md5sum /customer/demo.sh /new_customer/demo_v9 /new_customer/merakiplus/boot/switcher
grep -n switcher /customer/demo.sh          # exactly ONE line, before the "starting from B" branch
pidof switcher                               # running (started at boot)
cat /tmp/switcher.log
```

---

## 5. Revert procedures

> End users: `./revert.sh --ip ADDR` performs a full revert without rebooting (see the
> [README](../README.md#remove-meraki-revert)). The procedures below are the underlying manual steps.

### 5.1 Full revert test (one call, verified pattern)
```sh
python3 boot_install.py revert-test
```
Does, in order and with md5 gates: restores `demo.sh` from `.bak` (atomic tmp+mv, md5 == original) →
kills the switcher → `rm /customer/new` + reboot → verifies that stock A is untouched (exe link, no
switcher, demo.sh md5) → restores B's `demo_v9` from `demo_v9.vendor` (md5 == original) → cleans our
own files out of `/tmp` → verifies that the config md5 is unchanged. The backups are left in
`/new_customer/merakiplus/boot/` (harmless; they can be removed with `rm -rf` afterwards if desired — but
then a full revert is no longer possible without a new backup).

### 5.2 Manual revert (telnet, if the script cannot run)
```sh
# 1. demo.sh back (atomic, md5 gate):
cp /new_customer/merakiplus/boot/demo.sh.bak /customer/.demo_sh.tmp
md5sum /customer/.demo_sh.tmp        # must match the original (see §6)
mv /customer/.demo_sh.tmp /customer/demo.sh && sync
# 2. remove the switcher + the flag:
kill $(pidof switcher) 2>/dev/null
rm -f /customer/new && sync
# 3. reboot → stock A:
reboot
# 4. (optional) B's vendor binary back:
cp /new_customer/merakiplus/boot/demo_v9.vendor /new_customer/.demo_v9.tmp
md5sum /new_customer/.demo_v9.tmp    # must match the vendor md5 (§6)
mv /new_customer/.demo_v9.tmp /new_customer/demo_v9 && sync
```
**Only disable the gesture (keep the install):** remove the guard line manually (the same tmp+mv
pattern with an edited copy) or `rm /new_customer/merakiplus/boot/switcher` — the `[ -x … ]` gate then
makes the line harmless.

### 5.3 Emergency (automatic on any error in a device step)
`boot_install.py emergency` (also run automatically when a device step fails): restores `demo.sh` from
`.bak` if it differs → kills the switcher → secures A (SIGCONT if stopped; SIGSTOP-first switch if B
is running; manual launch if there is no demo_v9; kill B if mixed A+B) → `rm` flag → sync. Logs
everything; unresolved errors are reported with a reference to §5.2. **The machine is always left in
A.**

---

## 6. MD5 table (template + locally measured values 2026-11-17)

The device values are verified by the script's gates at run time; deviations = stop. Fill in the
device column during installation.

| File | Local/artifact md5 | Device before install | Device after install | Device after revert |
|---|---|---|---|---|
| `/customer/demo.sh` (original) | `45b8f5d3f79b5eb5c5b405963426d9d4` | ______ | `53cfde05…` (with guard) | `45b8f5d3…` ✓ |
| `/customer/demo.sh` (installed, +1 line) | `53cfde0510e4c2b8ad531516c64bf669` | — | ______ | — |
| `/customer/demo_v9` (vendor A) | `d44c2414b2ac6f0c9370ed6c9f549903` | ______ | unchanged | unchanged |
| `/new_customer/demo_v9` (vendor B) | `d44c2414b2ac6f0c9370ed6c9f549903` | ______ | merakiplus md5 ↓ | `d44c2414…` ✓ |
| merakiplus binary `workspace/merakiplus_v6` (v6.2 at the time of writing; the installer reads the md5 at run time) | `0bb93da0ed0824c424f1e16530f07022` * | — | ______ | — |
| `/new_customer/merakiplus/boot/switcher` | `6222d8c2236d1b27092ed345134ac9bb` | — | ______ | — |
| `/new_customer/merakiplus/boot/tinj_hold` | `32d04919f700194df7e553776c38d385` | — | ______ | — |
| `/new_customer/merakiplus/boot/fbpage` | `a1eefe6934067c280c2b44c4582c899a` | — | ______ | — |
| `/customer/config.json` (baseline) | `<config-md5-redacted>` | ______ | unchanged | unchanged ✓ |

\* The merakiplus md5 is read **at run time** by `boot_install.py install` — the UI's v6.1 rebuild is picked
up automatically (proven during preparation: the md5 changed from `90bcf094…` to `4d927802…` between
builds, and the script used the new one). State + snapshots: `boot/backups/state_snapshot.json`,
`boot/backups/md5_history.json`, device: `/new_customer/merakiplus/boot/state.json`.

---

## 7. OTA interaction (IMPORTANT RISK)

The vendor OTA (`/customer/temp.tar.gz`, an unsigned channel from `oss.sanbontft.com`, cf. FINDINGS
§4.3/§6.6) is handled in demo.sh BEFORE the slot branch:

| Flag at OTA time | The OTA does | Consequence for us |
|---|---|---|
| `/customer/new` exists (B active) | unpacks into **/customer (A)**, **removes the flag** | next boot = stock A (new version). Our `demo.sh` guard line is **overwritten** by the OTA's demo.sh → the switcher no longer starts; the gesture is dead until a re-install. merakiplus in B untouched but inactive. |
| no flag (A active) | unpacks into **/new_customer (B)**, **sets the flag** | next boot = **B = the vendor's new demo_v9 — our merakiplus is overwritten!** (the `demo_v9.vendor` backup and `merakiplus/boot/` survive, as the tar only touches its own paths). The machine boots vendor B without our knowledge. |
| mount failed | only unpacks into A | like row 1 without a flag change. |

In addition, the OTA strips `softversion` from `config.json` → the config md5 changes → our baseline
gate will (correctly) refuse install/revert-test until the config is understood/restored.

**Recommendation for persistence:** block `oss.sanbontft.com` (and optionally `pro.ip-api.com`) on the
LAN firewall for as long as our UI should be persistent. Without blocking: accept that a vendor OTA can
(a) remove the switcher line and (b) replace merakiplus in B + force a B boot. Detection:
`md5sum /customer/demo.sh /new_customer/demo_v9` against the §6 table + `ls /customer/new`. Recovery:
run `backup` (new baseline) + `install` again — the backups in `merakiplus/boot/` survive an OTA, but **the
vendor OTA's new A demo.sh then becomes the new "original"** (the backup takes the current one as the
truth; document the version change in the md5 table).

---

## 8. Risks

1. **`/customer/demo.sh` (slot A's boot script) is touched** — the only change in slot A: ONE guard
   line, installed atomically, with an md5-verified backup (`.bak`) + revert test + emergency. The risk
   of a corrupt demo.sh is minimized by tmp+mv on the same file system + an md5 gate before the mv.
   Power loss between cp and mv: either the old or the new file, never half (rename is atomic).
2. **The switcher only observes input** (read-only, no grab, no writes to UART/fb/config). It cannot
   steal touches or affect the stock UI's queue. Worst case on a crash: no gesture switch (idle); the
   watchdog/stock UI are unaffected (the name `switcher` does not match the watchdog grep).
3. **Accidental gesture triggering:** requires 3 stationary ≥ 1.5 s holds in a 90×90 px corner
   (≈ 3.5 % of the square area, mostly outside the round active area) within 6 s. In normal use (taps
   on the four activity buttons, swipes) the probability is **vanishingly small**; realistic
   unintended sources are cleaning the screen with long presses in the corner or a child holding a
   finger in the corner repeatedly. The consequence is mild: the UI switches to merakiplus (no actuator
   start; merakiplus never starts grind/brew without an explicit button + 650 ms hold), and the user can
   switch back with merakiplus's "Switch to original NOW" button (§9) or the kill+launch pattern (§5.2).
   The gesture only works A→B. **Disable:** remove the guard line via revert (§5.1/§5.2) or
   `rm …/boot/switcher` (the gate makes the line harmless).
4. **Watchdog slot mismatch after a runtime switch** (§1.2): crash after a switch → the watchdog
   restarts the boot slot's binary. A safe fallback, but surprising if you expect "stay in our UI".
   The next reboot normalizes it.
5. **SIGSTOP in the middle of a UART write** (A is stopped before B starts): a partial frame to the MCU
   — the MCU resyncs on the header (FINDINGS §5.7, low risk, accepted in phase 2).
6. **merakiplus's own writes** (v6 SAVE → config.json + MCU registers) are unchanged and documented in
   REVERT.md; the boot deliverable adds no new config writes. Note REVERT.md's OTA caveat on a demo_v9
   restart.
7. **B boot with our UI = no stock sleep control:** merakiplus v6 has keep-awake (word1=0 every 30 s) and
   controls sleep itself; on a B boot the stock UI's sleep logic is not active. Expected behavior, but
   noted.
8. **Root telnet open to the LAN** (FINDINGS §6.6) — unchanged by this deliverable; hardening is still
   recommended.

---

## 9. UI requirements for the UI work stream (merakiplus v6.1+, SYSTEM page)

The boot deliverable is ready; the following two controls had to be added to merakiplus (owner of
`merakiplus.c`/`UI_v6.md`). Specification — implemented and tested by the UI work stream, not by the
boot work stream:

### 9.1 Boot UI toggle (persistence choice)
* Placement: the SYSTEM page, a new row "BOOT UI" with a toggle/button pair.
* Shows the current state: check whether `/customer/new` exists (`access()`/`stat()`): exists →
  "Our UI (B) at boot"; does not exist → "Original (A) at boot".
* On a switch to B: `open("/customer/new", O_CREAT|O_WRONLY, 0644); close(); sync();`
  On a switch to A: `unlink("/customer/new"); sync();`
* Takes effect at the NEXT boot (no reboot from the button). Show a help text: "Applies at the next
  restart. An OTA can overwrite this choice — see BOOT.md §7."
* Must NOT touch demo.sh or other files. Error (read-only fs) → show the error, keep the old state in
  the UI.

### 9.2 "Switch to original NOW" button (runtime B→A, no reboot)
The order is critical because of the watchdog race (§1.2) and fb/UART ownership:
1. **Stop our own activity first:** stop UART polling/timers and fb rendering; **close the
   `/dev/ttyS1` fd** (and the fb fd) so stock A does not share the UART with us.
2. **Launch stock A:** `fork()`; in the child: `setsid()`, `chdir("/customer")`,
   `setenv("LD_LIBRARY_PATH", "/customer/libs:/config/wifi", 1)`, stdio → `/dev/null` (or
   `/tmp/a_relaunch.log`), `execl("/customer/demo_v9", "./demo_v9", NULL)`. (argv0 MUST contain
   `demo_v9` so the watchdog grep matches.)
3. **Wait ~300–500 ms** in the parent (A initializes fb/UART), **then exit** (`_exit(0)`). During the
   overlap merakiplus must NOT read the UART or render the fb (fds closed in step 1). The watchdog now sees
   A's PID → no restart.
4. **Do NOT touch the flag** (`/customer/new`) — a runtime switch ≠ a boot choice. **Do not kill
   demo_v9** — launch + our own exit is enough (the existing guarantee "merakiplus never kills demo_v9" is
   kept).
5. The exit automatically releases `EVIOCGRAB` on event0 → the switcher observes again and the
   gesture (A→B) is available immediately.
6. Log the switch (merakiplus's own log) with time + reason ("user button").
* Known consequence (documented in UI_v6.md): if we booted B, the watchdog is `test.sh new_customer`
  → if A crashes later, B is restarted. The next reboot follows the flag. Acceptable; no action
  required.

### 9.3 General requirements (unchanged guarantees)
* merakiplus must never write to `/customer/demo.sh`, `/customer/demo_v9`, `test.sh`, pinit or libs.
* Config writes only via the established v6 pattern (REVERT.md: tmp+rename, cfgbak, md5 log) — the
  boot deliverable only reads the config md5 as a gate.
* On a B boot (merakiplus as `/new_customer/demo_v9`) merakiplus must run without the `run.sh` wrapper (it is
  phase 2 only; run.sh SIGCONTs a stock process that does not exist on a B boot). Run the binary
  directly; exit codes are logged to `/tmp/merakiplus_boot.log` if started by the switcher.

---

## 10. Local verification (deliverable 4) — evidence

| Check | Result | Evidence |
|---|---|---|
| ARM build, static, 0 warnings (`-Wall -Wextra -Werror`) | ✓ | `build.sh` output; `file switcher tinj_hold` = "statically linked" ARM EABI5 |
| C self-test (gesture/scaling/mode logic) | **29/29** | `test-logs/selftest.log` |
| End-to-end file mode (positive + 4 negative streams) | **5/5** | `test-logs/e2e_*.switcher.log` |
| Mock backup (md5 + copies, flag, config gate) | ✓ | `install.log` + `backups/md5_history.json` |
| Mock install: exactly ONE guard line, correct placement, atomic, idempotent | ✓ | `diff mock_root/customer/demo.sh artifacts/demo.sh` = only line 136 added |
| Mock boot test (B boot/A boot/gesture runtime/back, simulated boot + REAL gesture code) | ✓ | `install.log`, `test-logs/mock_*` |
| Mock revert test: demo.sh + vendor B + config byte-identical to the originals; /tmp clean | ✓ | independent `diff`/`md5` against the artifacts (run 2026-11-17) |
| Mock emergency (restore demo.sh, kill switcher, A secured, flag gone) | ✓ | `install.log` |
| merakiplus md5 read at run time (v6.1 pickup) | ✓ | the md5 changed during preparation; the install used the new one |

**The rest requires the device (online, user at the machine):** the device steps in §4 (real reboot,
fb captures, the gesture on physical touch, the watchdog race in practice, merakiplus v6.1 as the B
binary). The §9 buttons had to be implemented before the B→A button could be tested; the boot test's
switch back, however, uses kill+launch and is independent of the UI work stream.

---

## 11. File inventory (this directory)

| File | Purpose |
|---|---|
| `switcher.c` | daemon source (gesture + mode check + SIGSTOP-first switch) |
| `switcher` | static ARM binary (installed as `/new_customer/merakiplus/boot/switcher`) |
| `switcher_selftest`, `switcher_host` | host builds for local tests |
| `tinj_hold.c`, `tinj_hold` | touch injector with long press + `gesture` (boot test) |
| `boot_install.py` | step script (device + mock backends, gates, emergency) |
| `build.sh` | all builds + self-test |
| `test_switcher.py` | end-to-end file-mode harness |
| `BOOT.md` | this file |
| `backups/` | local snapshots: `demo.sh.orig`, `demo.sh.installed`, state/md5 history |
| `test-logs/` | self-test, e2e, mock and (later) device captures/logs |
| `mock_root/` | local mock root (customer/new_customer/tmp/proc/dev) |
| `install.log` | combined run log (all steps, both backends) |
| `demo.sh.new` | the last installed demo.sh content (md5 evidence) |

*— boot work stream, 2026-11-17. No device contacted; the UI files untouched.*
