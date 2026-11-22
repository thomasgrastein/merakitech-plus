# Secure updates (OTA) for Meraki+ — inventory and plan

Status: **phases 1a and 1b implemented** (v6.21: hardened companion installer, launcher, release directories and
automatic rollback — see [Phase 1a — implemented](#phase-1a--implemented-2026-11-22) and
[Phase 1b — implemented](#phase-1b--implemented-2026-11-22)); phases 2–4 are a plan, not implemented. It builds on the vendor A/B mechanism described in
[boot.md](boot.md) and refines its §7 (vendor OTA interaction).
Confidence: **H** = proven from code or an on-device observation; **M** = strong inference; **L** = guess.

The machine was **offline** during this round, so the inventory in §2 is **static** (vendor scripts, the stock binary,
earlier read-only on-device snapshots). The read-only probes that close the remaining gaps are listed in §10.

Hard rules this plan keeps everywhere:

- **Slot A (`/customer/demo_v9`, `/customer/libs`, …) is never written** by Meraki+ or by its updater.
- **The boot flag `/customer/new` keeps its meaning** (present = boot slot B). Updates never set or clear it; only the
  user does (installer `--persist`, `revert.sh`, the BOOT UI row).
- **`revert.sh` keeps working** from every intermediate state, and the backups in `merakiplus/boot/` are never touched.
- **No new listening port.** Updates are outbound-only, user-triggered or explicitly opted in.
- **No reboots by tools.** A new version is activated by an in-place exec (same PID, see §5.5) or at the next power-on.

---

## 1. Summary

| Topic | Recommendation |
|---|---|
| Source | GitHub releases of this repo (as today), plus a **signed release manifest** (`manifest.json` + `manifest.sig`, Ed25519) that lists every asset with size and SHA-256. |
| Trust | **Ed25519 signature** (signify-compatible, pure Ed25519) made **offline** by the maintainer — CI cannot sign. Public keys (primary + cold backup) are embedded in the device helper and the installer. MD5 stays only as a legacy display value. |
| Transport | **Two transports, one verifier.** (a) LAN push from a companion computer (today's `install.sh`, hardened) — always available, no device TLS needed. (b) Later, an opt-in **on-device fetch over HTTPS** by a small static helper (BearSSL, pinned roots). Both end in the same on-device `merakiplus_upd verify+stage` code path. |
| Atomicity | Versioned **release directories** in `/new_customer/merakiplus/rel/`, a **state file** as the single commit point (tmp + fsync + rename), fsync **before** every rename. All assets of a version switch together. |
| Rollback | A tiny static **boot launcher** becomes `/new_customer/demo_v9` (the name the vendor watchdog restarts). It starts the selected release, counts starts that never became *healthy*, and falls back **current → previous → stock UI from slot A** without touching the flag. This also closes today's worst failure mode: a build that crashes before Wi-Fi is up (§3.3). |
| Consent | Off by default. SYSTEM › UPDATES: CHECK NOW → shows version + notes → INSTALL (now, when idle) or AT NEXT START. Optional later: notify-only automatic check, and auto-install at the next power-on. **Never during an activity**; never while asleep (the display has no power then). |
| Vendor OTA | Layered: (0) while Meraki+ runs, no vendor update code runs at all; (1) block the vendor OTA domain in the router (recommended for `--persist` users); (2) optional runtime hosts pin after a probe; (3) **detect and repair**: Meraki+ and the installer recognise vendor OTA traces and offer a re-install. |
| Effort | Phase 0 probes 0.5 d · 1a hardening 1–1.5 d · 1b launcher + rollback 3–4 d · 2 signed manifests 2 d · 3 on-device fetch 5–7 d · 4 opt-in automation 1.5–2 d. About **14–18 agent-days**, plus user-attended hardware sessions. |

Phase order deliberately differs from "fetch first, sign later": **no device-side download ships before signature
verification exists** (§9).

---

## 2. Inventory (task A)

### 2.1 Storage

| Item | Finding | Conf. |
|---|---|---|
| Flash | Raw NAND (`/proc/mtd`): IPL/UBOOT (×2), ENV, KERNEL, RECOVERY, rootfs (+ `_bak`), KEY_CUST, miservice, LOGO and one **UBI** device holding the volumes `customer`, `appconfigs`, `new_customer`. | H |
| `/customer` (slot A) | `ubi0:customer`, UBIFS, ~76 MB, **~43 MB free**. Holds the stock UI, its libs, `demo.sh`, `test.sh`, `upgrade.sh`, `config.json`, the boot flag. | H |
| `/new_customer` (slot B) | `ubi0:new_customer`, UBIFS, ~109 MB, **~60 MB free** with Meraki+ installed. Created by `demo.sh` with `ubimkvol` if missing; mounted rw at every boot. | H |
| `/tmp` | tmpfs, ~47 MB nominal but backed by RAM; the display has only **~22 MB free RAM**. Never stage downloads or long logs there. | H |
| rootfs, `/etc` | rootfs read-only vs. writable and whether `/etc` is tmpfs is **unknown** (the vendor `upgrade.sh` writes `/etc/fw_env.config`, so `/etc` is writable at least at runtime). Probe P4. | M |
| Wear | UBI does wear levelling across the whole UBI device; UBIFS is designed to survive power cuts (journal, atomic rename). A release is ~0.5 MB and changes a few times a month → negligible wear. The only frequent writer the plan adds is the launcher's start counter, and it writes **only during a trial** (§5.4), never in steady state. Bad-block / erase counters: probe P1. | M |
| Where backups live | `/new_customer/merakiplus/boot/` (`demo_v9.vendor` = slot B's original program, `demo.sh.bak`, `merakiplus.state`). Settings backups in `merakiplus/cfgbak/`. A staged copy of the UI in `merakiplus/merakiplus`. Survive vendor OTAs in practice (the vendor tar only writes its own paths). | H |
| Room for A/B copies | **Yes, with a wide margin.** One Meraki+ set (UI ~300 KB, sound helper ~9 KB, three WAVs ~95 KB, switcher ~80 KB) is about **0.5 MB**; keeping current + previous + one staged version costs ~1.5 MB of ~60 MB free. | H |

Housekeeping note: development installs accumulate old binaries in `merakiplus/` (a few MB on the author's machine). The
installer should only ever keep what the state file references (§5.3), and offer to remove stray files.

### 2.2 On-device tooling

| Tool | Finding | Conf. |
|---|---|---|
| Shell | busybox **1.20.2** (old). `ash`, `md5sum`, `tar`, `gzip`, `nc` (with `-l`, used by the installer), `base64`, `awk`, `sed`, `sync`, `mv`. `sha256sum` likely (busybox ≥ 1.14) — probe P2. | H / M |
| wget | busybox 1.20.2 `wget` has **no HTTPS** (TLS support arrived in later busybox versions). Useless for GitHub, which is HTTPS-only. | M |
| curl / TLS | No `curl` binary seen. The stock UI links **libcurl 7.76.1 + OpenSSL 1.1** from `/customer/libs`. Their CA bundle location is unknown (probe P3). Using them would tie our updater to vendor libraries that a vendor OTA can replace, and both are end-of-life. | H / M |
| Crypto for us | Nothing usable on the device for signatures. **Our own static helper** can carry Ed25519 + SHA-256 (Monocypher, ~15 KB, CC0/BSD-2) and, for on-device HTTPS, a TLS client (BearSSL, MIT, ~150–250 KB static). Verification cost on the Cortex-A7: milliseconds. | H |
| Clock | The SoC clock is UTC; the stock UI sets it via an HTTP time service + `hwclock -w`; Meraki+ does not sync time. Whether the RTC keeps time across a power cut is **unknown** (probe P6). TLS certificate checks therefore must not depend on a correct clock (§6.2). | H / M |
| cron / boot hooks | No cron seen. Boot hooks: `/customer/demo.sh` (vendor; Meraki+ adds one guard line only with `--gesture`) and `/customer/test.sh new_customer`, which restarts **whatever `/new_customer/demo_v9` is**. That file is the one hook we fully own in every slot-B boot. | H |
| Firewall / hosts | `iptables` not seen and netfilter modules are not in the vendor `insmod` list → probably no on-device firewall. `/etc/hosts` + glibc resolver: probe P4. | M |
| Boot ID | `/proc/sys/kernel/random/boot_id` is standard on Linux 4.9 — used to tell "same boot" from "new boot" without a reliable clock. Probe P2. | M |

### 2.3 The vendor OTA mechanism (what could clobber Meraki+)

Static analysis of the stock UI (`demo_v9` 1.1.1-69, function names are its own symbols) plus the published manifest
and the boot scripts:

| Step | What happens | Conf. |
|---|---|---|
| Check | `app_wifi_thread` calls `Image_check_ver` and `mcu_check_ver` after Wi-Fi comes up. They fetch `https://oss.sanbontft.com/ota/62091/<language>/release.json` (and the MCU manifest) to `/tmp/getlatestversion` / `/tmp/getMculatestversion` and compare the version strings. **Check is automatic; it downloads only the manifest.** | H |
| Apply | `deal_ota_update` is called only from the `onHandle` event handlers of `page_software_update` / the MCU update page → **download + apply is user-initiated** from the stock Software Update page. | M (probe P8 confirms) |
| Download | Into `/customer`: the new UI as `demo_temp` (MD5-checked), `libs.tar.gz`, `fonts.tar.gz`, `audio.tar.gz`, `video.tar.gz` (MD5-checked, extracted **into `/customer`**), `image.tar.gz` (MD5-checked, renamed to `temp.tar.gz`). The manifest's `*_check` / `commond_before` / `commond_after` fields are **shell commands run as root** — the channel is unsigned (MD5 only). | H |
| Immediately | If `libs.tar.gz` contains `demo.sh` / `test.sh` / `upgrade.sh`, they are moved into `/customer` (**replaces `demo.sh` → our guard line disappears**) and `test.sh` + `upgrade.sh` are copied into `/new_customer/` (harmless to us: we do not use those copies). Slot A's `demo_v9` is replaced. | H |
| Next boot | `demo.sh` sees `/customer/temp.tar.gz`: **flag absent** → extracts into `/new_customer` (very likely including a `demo_v9`, because a fresh slot B must be bootable from the tarball alone) and **sets the flag**; **flag present** (our `--persist` state) → extracts into `/customer` and **removes the flag**. | H (script) / M (tar content) |
| Full system | `upgrade.sh` runs `otaunpack` + `reboot` if a `SStarOta.bin.gz` appears in the new slot's `imgs/` — a full firmware flash (kernel/rootfs). Not triggered so far, but possible. | H |
| MCU | `mcu_tmp.bin` → flashed to the controller over the UART. A new controller firmware may change the protocol Meraki+ relies on (developed against controller fw 5.1.4). | H |

**What can clobber Meraki+:** (a) `/new_customer/demo_v9` (today the UI itself; in this plan the launcher) when a tar is
extracted into slot B; (b) our `demo.sh` guard line; (c) the boot flag (set or cleared). **What survives:**
`/new_customer/merakiplus/` (the tar writes its own paths only — M), slot-A-independent backups, settings in
`config.json` (stock format; the OTA strips only `softversion`).

**Key structural fact (H):** the vendor check/download code lives inside the stock UI. While Meraki+ is the running UI
(slot-B boot), **no vendor OTA code runs at all**, and Meraki+ never contacts the vendor servers. Exposure exists only
while the stock UI runs: a boot without the flag, after "SWITCH TO ORIGINAL NOW", or after a launcher fallback to stock
(§5.4).

### 2.4 Today's install / revert flow and its failure modes

Flow (`tools/merakiplus.py`, called by `install.sh`): read-only inspect → fetch binaries (local `bin/` or raw GitHub at a
ref) and **MD5-check them on the computer** → confirm "machine idle" → backups once → for each file: `nc -l` upload to
`<dest>.part` on the same filesystem, MD5 re-read, `mv` over the destination, `sync` → copy the staged UI over
`/new_customer/demo_v9` via `.tmp` (cp, sync, MD5, mv, sync) → activate by **watchdog exec-reload** (`kill -TERM`; the
vendor watchdog restarts slot B within ~1–2 s) or a stop-first runtime switch → verify the live `/proc/<pid>/exe` MD5 and
that it still runs after 5 s. `revert.sh` mirrors it (stop-first switch back to slot A, restore `demo.sh` and slot B from
the backups, restore the original flag).

| Failure | Today | Severity |
|---|---|---|
| Power cut during an upload | `.part` file left over; destination untouched. | low |
| Power cut right after `mv` in `push()` | `push()` renamed **before** any `sync` of the new data (the MD5 check reads the page cache, not flash). On UBIFS a rename can reach flash before the data → a truncated/zeroed file at the final name was possible. `atomic_copy()` already synced before its rename, so `/new_customer/demo_v9` was safe; the sound helper and WAVs (pushed directly) were not. | medium — **fixed in 1a** (sync before every rename) |
| Interrupted multi-file update | Files are replaced one by one → mixed versions possible (e.g. new WAVs with the old UI). Switcher and shim are only refreshed with `--gesture`, so installed copies can lag behind `bin/` (observed on the author's machine; harmless today). | low–medium |
| New build crashes at start | The vendor watchdog restarts it every ~1–2.5 s forever. The installer notices (5 s check) and tells the user to revert — **if the computer is still connected**. | high |
| **Crash before Wi-Fi is up, with `--persist`** | In a slot-B boot **Meraki+ itself brings up Wi-Fi** (the stock UI normally does it). A build that crashes before its network guard runs (~4–5 s after start) leaves the display **without Wi-Fi → no telnet → no `revert.sh`**, at every power-on (the flag persists). The gesture switcher only switches A→B. Recovery would need the serial console. | **critical (soft brick)** — closed by the launcher in 1b |
| ETXTBSY | Avoided: the running file is never opened for writing; a rename gives the new inode a name while the old one keeps running (`/proc/<pid>/exe` then reads "… (deleted)", which the installer handles). | none |
| Full flash | Fresh installs check ~1.2 MB free; updates do not. A full volume makes the `.part` write fail → MD5 mismatch → abort, old file intact, `.part` left over. | low |
| Watchdog race | Handled: exec-reload via the watchdog, or SIGSTOP-first switching. | none |
| Wrong download | Prevented on the computer by the pinned MD5 table in the installer (which itself comes from the same Git ref — no independent authenticity). | medium (closed in 2) |

---

## 3. Threat and failure model

### 3.1 What we defend against

1. **Corruption**: truncated/garbled downloads, flash write errors, interrupted copies.
2. **Power loss at any instant** (the PMIC cuts the display after a ~0.8 s power-button hold, the controller's sleep cuts
   it too, and users pull plugs).
3. **A bad Meraki+ release** that crashes, hangs or never reaches a working state on some machine.
4. **Supply chain**: a compromised GitHub account, a leaked CI token, a tampered mirror/CDN, a malicious pull request
   that slips into a release. → Releases must be **signed offline**; CI and GitHub alone cannot produce an update
   devices accept.
5. **Rollback/freeze tricks**: serving an older, validly signed release with known bugs.
6. **Vendor OTA** overwriting or bypassing Meraki+ (§7).

### 3.2 What we cannot defend against (and say so)

- **The LAN.** The display ships an unauthenticated root telnet server. Anyone on the same network already has full
  control; signatures do not change that. Meraki+ keeps telnet because it is also the recovery path. Users must isolate
  the machine (README *Security and OTA*).
- A compromised maintainer signing key. Mitigation: offline key, a cold backup key for rotation (§6.1).

### 3.3 Why rollback must not depend on the network

In a slot-B boot, Meraki+ is the process that starts Wi-Fi. Any rollback that needs a computer, telnet or a network
fetch fails exactly in the worst case (an early crash). Hence the launcher (§5.4) decides **locally, at start time**,
and its last resort is the stock UI from slot A — which brings up Wi-Fi by itself and makes the device reachable again.

---

## 4. Release source and manifest (task B1)

GitHub releases stay the only source. Changes to what a release carries:

| Asset | Today | Plan |
|---|---|---|
| `merakiplus` (UI), `merakiplus_snd` | release assets | unchanged |
| `audio/*.wav` | raw repo files | **also release assets** (the manifest must cover every installed file) |
| `merakiplus_boot` (launcher), `merakiplus_upd` (updater) | — | new release assets (phases 1b/2) |
| `switcher`, `grab_shim.so` | raw repo files | release assets, listed in the manifest's `platform` section |
| `MD5SUMS` | release asset | kept (legacy, display only) |
| `SHA256SUMS` | — | new (phase 1a, CI) |
| `manifest.json` + `manifest.sig` | — | new (phase 2, **signed offline**) |

Manifest (illustrative; canonical JSON, UTF-8, sorted keys, no floats; the signature covers the exact bytes):

```json
{
  "format": 1,
  "product": "merakiplus",
  "board": "sat62091m",
  "version": "6.21",
  "seq": 27,
  "channel": "stable",
  "released": "2026-12-01",
  "min_from_seq": 20,
  "requires": { "launcher": 1, "switcher": 2, "data_schema": 3, "mcu_fw_tested": ["5.1.4"] },
  "release": [
    { "name": "demo_v9", "src": "merakiplus", "size": 306024, "sha256": "…", "mode": "755" },
    { "name": "merakiplus_snd", "src": "merakiplus_snd", "size": 8912, "sha256": "…", "mode": "755" },
    { "name": "audio/start.wav", "src": "start.wav", "size": 45484, "sha256": "…", "mode": "644" },
    { "name": "merakiplus_upd", "src": "merakiplus_upd", "size": …, "sha256": "…", "mode": "755" }
  ],
  "platform": [
    { "name": "launcher", "src": "merakiplus_boot", "version": 1, "size": …, "sha256": "…" },
    { "name": "switcher", "src": "switcher", "version": 2, "size": …, "sha256": "…" }
  ],
  "base_url": "https://github.com/<owner>/merakitech-plus/releases/download/v6.21/",
  "notes_url": "https://github.com/<owner>/merakitech-plus/releases/tag/v6.21"
}
```

- `seq` is a strictly increasing integer (versions like `6.18.1` make string comparison fragile). Devices store the
  highest accepted `seq` and refuse lower ones from the network (§6.3).
- `release` = files that live in a versioned release directory and switch atomically together.
  `platform` = fixed-path components that change rarely (launcher, switcher/shim) and are updated separately (§5.6).
- `base_url` pins the tag, so a moving "latest" link cannot mix assets from two releases.
- Discovery uses the fixed URL `…/releases/latest/download/manifest.json` (+ `.sig`): no GitHub API, no JSON paging, no
  rate-limit token. A release without a manifest returns 404 → "no update" (safe default).

---

## 5. On-device design (tasks B3–B5)

### 5.1 Components

| Component | What | Where | Lifetime |
|---|---|---|---|
| `merakiplus_boot` ("launcher") | Static musl ARM binary, target size < 40 KB, no network, no UI. Chooses which release to exec. | `/new_customer/demo_v9` (the only path the vendor watchdog starts in a slot-B boot) | Updated rarely, only through the companion (phases 1–3). |
| `merakiplus_upd` ("updater") | Static musl helper: manifest + signature verify, SHA-256, staging, state transitions, later the HTTPS fetch. Runs to completion and exits; **never a daemon, never listens**. | inside each release directory | Ships with every release. |
| Meraki+ UI | Gains: find its assets relative to its own executable; *healthy* marker; SYSTEM › UPDATES; exec-reload. | `rel/<seq>/demo_v9` | per release |

### 5.2 Layout

```
/new_customer/demo_v9                 launcher (was: the UI itself)
/new_customer/merakiplus/
  rel/27-6.21/                        one directory per installed release (complete set)
    demo_v9  merakiplus_snd  merakiplus_upd  audio/*.wav  manifest.json  manifest.sig
  rel/26-6.20/                        previous release (kept for rollback)
  ota/state                           the commit point (see 5.3)
  ota/stage/                          downloads in progress (*.part), removed after use
  ota/lock                            flock for every writer (launcher, UI, updater)
  ota/status                          small progress/result file read by the UI (bytes, not logs)
  boot/  cfgbak/  lang  schedule  …   unchanged: backups, gesture parts and shared settings (not versioned)
```

The UI binary keeps the file name `demo_v9` inside its release directory, so the vendor watchdog's `ps | grep demo_v9`
and the installer's `*/demo_v9` process scan keep matching. `/proc/<pid>/exe` becomes the release path, which needs
small compatibility changes (§5.6).

### 5.3 The state file — the single commit point

`ota/state` is a small `key=value` text file, always replaced by **write `state.tmp` → fsync → rename → fsync the
directory** (busybox `sync` in scripts). Fields:

```
format=1
current=27-6.21          release that is known good (committed)
previous=26-6.20         last known good before it (rollback target)
pending=                 release on trial ("" = none)
tries=0                  starts of `pending` that did not become healthy
starts=…                 boot_id + count of starts in this boot (crash-storm detection)
max_seq=27               highest seq ever accepted (anti-rollback for network installs)
fallback=                "stock" while the launcher is falling back to slot A
last=ok|rolled_back:<release>:<reason>|…   shown once by the UI
```

Every transition is one state rewrite: **stage → verify → rename the staged directory into `rel/` → write
`pending=<new>` (commit point) → activate → healthy → write `current=<new>, previous=<old>, pending=`**. Garbage
collection deletes only `rel/` entries that `current`, `previous` and `pending` do not name, and partial leftovers in
`stage/`.

### 5.4 Launcher algorithm (boot-count + success-marker rollback)

On every start (boot, watchdog restart, exec-reload):

1. Take `ota/lock`. Read `ota/state`; if missing or unparseable, use `current`'s last good copy (`state.bak`, kept by
   every writer) or else the newest complete release directory.
2. **Trial:** if `pending` is set: `tries += 1`, write the state (fsync) **before** exec. If `tries > 3`, or more than
   6 starts happened in this boot within 10 minutes (boot_id + uptime, no wall clock needed): mark
   `last=rolled_back:<pending>:crashloop`, clear `pending`, continue with `current`.
3. **Steady state:** no write at all (no wear). Before exec, check that `rel/<current>/demo_v9` exists and matches the
   SHA-256 recorded in that directory's manifest (~300 KB, a few ms). On mismatch or a crash storm on `current`: switch to
   `previous`.
4. **Last resort:** if neither release is usable, set `fallback=stock` and exec the **stock UI from slot A** exactly like
   the installer's revert path does: `chdir /customer`, `LD_LIBRARY_PATH=/customer/libs:/config/wifi`,
   `execv("/customer/demo_v9")` with argv0 `./demo_v9`. Slot A is executed, never written; the flag is untouched. The
   stock UI brings Wi-Fi up, so the device becomes reachable for `install.sh`/`revert.sh`.
5. Exec the chosen `rel/<x>/demo_v9` with `chdir` to its directory, argv0 `./demo_v9`, and
   `MERAKIPLUS_REL=<dir>`, `MERAKIPLUS_TRIAL=1` when on trial. `execv` keeps the PID, so the watchdog never sees a gap.

**Healthy** (written by the UI, under the lock): the UI has drawn its first frame, has exchanged valid frames with the
controller for ≥ 30 s, and has run ≥ 60 s. Then it commits (`current=<pending>`, `previous=<old current>`,
`pending=`, `tries=0`). Before healthy, the UI shows a discreet "trial" note in SYSTEM › UPDATES.

Trade-offs: three quick power cycles during the first minute of a trial roll back a good release (safe direction; the
UI then offers "TRY AGAIN"). Crashes that start only after the healthy point are not auto-rolled back; the user has
ROLL BACK in SYSTEM and `revert.sh` (the UI and Wi-Fi are up by then).

### 5.5 Activation without reboot

- **Exec-reload (preferred):** the UI, when idle and confirmed, stops its own activities, closes its descriptors
  (frame buffer, UART, the input grab is released on close) and calls `execv("/new_customer/demo_v9")`. Same PID → no
  watchdog race, works in both watchdog slots (a slot-A boot where Meraki+ was started by the gesture), and needs no
  `kill()` (Meraki+'s "no kill() calls" invariant holds). The controller sees a 1–3 s UART gap, the same as today's
  installer reloads.
- **At next start:** only `pending` is written; the launcher picks it up at the next power-on. Zero disruption and the
  natural window for automatic installs (§8).
- Fallback: exit and let the vendor watchdog restart `/new_customer/demo_v9` (only valid when the watchdog slot is B).

### 5.6 Scope of an update and version compatibility (task B5)

| Group | Members | Updated how | Compatibility rule |
|---|---|---|---|
| Release set | UI, sound helper, WAVs, updater, manifest | Always together, one atomic switch (5.3). Partial updates of single files are not offered. | Inside one release everything matches by construction. |
| Platform: launcher | `/new_customer/demo_v9` | Companion only (phases 1–3); atomic rename after the new launcher passes `--selftest` **on the device** against a scratch state directory. | `requires.launcher` in the manifest; the updater refuses a release that needs a newer launcher and tells the user to run `install.sh`. |
| Platform: switcher + shim | `merakiplus/boot/` (fixed paths, referenced by the `demo.sh` guard line) | Companion, only when `--gesture` is installed; atomic rename; the running switcher is restarted after the swap. | The switcher must classify "any exe under `/new_customer/`" as Meraki+ (today it compares with exactly `/new_customer/demo_v9`, which breaks once the UI runs from `rel/`). → `requires.switcher`. |
| Shared data | `config.json` (stock format), `lang`, `schedule`, `cfgbak/`, presets later | Never versioned; every release must read older **and newer** formats (ignore unknown keys) so a rollback is safe. `requires.data_schema` names the newest format a release writes. | Rollback to a release with a lower `data_schema` is allowed only if the files on disk are within its range; otherwise the UI warns before rolling back. |
| Controller firmware | powerMcu (vendor) | Never by Meraki+ | `mcu_fw_tested`; the UI warns once if the controller reports an untested version (probe P9: confirm the UI can read it). |

Required code changes for the launcher layout (all phase 1b): the UI reads assets from `MERAKIPLUS_REL` (fallback: its
own exe directory, then the legacy fixed paths); the installer's `verify_running`, `stop_first_switch` and the
switcher's mode check accept `/new_customer/…/demo_v9`; `revert.sh` restores `/new_customer/demo_v9` from the vendor
backup exactly as today and removes `rel/` + `ota/` (backups stay).

### 5.7 Power loss at every step

| Phase | What is on flash after a cut | Next start |
|---|---|---|
| Download / upload into `stage/*.part` | partial files, unreferenced | current release runs; stage is cleaned on the next updater run |
| Hash check, fsync | same | same |
| Rename `stage/<x>` → `rel/<x>` | either the stage or the complete release directory (UBIFS rename is atomic) | current runs; an unreferenced `rel/<x>` is resumed (re-verified) or collected |
| State rewrite `pending=<x>` | old or new state, never a mix (tmp + fsync + rename) | old: current runs. new: trial starts |
| Trial start | `tries` was persisted before exec | trial continues; counts toward the rollback limit |
| Healthy commit | old or new state | old: the trial continues and commits again after 60 s |
| Garbage collection | a half-deleted unreferenced directory | collected again |
| Launcher swap (companion) | old or new launcher (rename after fsync and on-device selftest) | whichever is there runs |

The only single point of failure left is a **broken launcher** — the same exposure as today's UI binary, but the
launcher is ~1/10 of the code, has no UI/network/UART, changes rarely, is tested by fault injection (§9) and is
selftested on the device before every swap.

---

## 6. Integrity, authenticity, transport (tasks B2–B3)

### 6.1 Signatures

- **Algorithm:** Ed25519 over the exact `manifest.json` bytes, stored in **signify format** (pure Ed25519, key ID in the
  header). Tooling: `signify` on Linux/macOS, or a small `tools/sign_manifest.py`. Verification: Monocypher in
  `merakiplus_upd` (C, compiled with `zig cc` like everything else), and a vendored pure-Python RFC 8032 verifier in
  `tools/merakiplus.py` (the Python standard library has no Ed25519).
- **Keys:** generated offline. The primary key lives on the maintainer's machine (passphrase-protected) and never in
  CI or GitHub secrets; a **cold backup key** is stored offline. Both public keys are embedded in the updater and the
  installer. Rotation: a release signed by the old key ships binaries that trust the new key.
- **Signing flow:** CI builds and publishes the release as today (unsigned). The maintainer runs
  `tools/sign_release.py v6.21`, which downloads the release assets, **rebuilds `bin/` locally with the official
  toolchain and compares byte for byte** (the builds are reproducible), writes the manifest with `seq = previous + 1`,
  signs it and uploads `manifest.json` + `manifest.sig`. Until that upload, devices see "no update".
- **Hashes:** SHA-256 for every file in the manifest. MD5 is kept only as a familiar display value and is never trusted.

### 6.2 Transport options compared

| | A. LAN push from a companion (hardened `install.sh`) | B. On-device HTTPS fetch |
|---|---|---|
| Needs on the device | nothing new (busybox `nc`), plus the verifier | a TLS client + trust anchors (≈ 200 KB), correct redirects |
| Works for | anyone with a computer on the same network | anyone, from the machine's own screen |
| New exposure | none (outbound from the computer; telnet already exists) | outbound HTTPS to GitHub from the display (privacy: GitHub sees the public IP; no identifiers sent, User-Agent `merakiplus/<version>`) |
| Failure modes | computer disconnects mid-way (handled by staging) | TLS failure, clock, GitHub down, captive networks |
| Trust | signature verified on the computer **and** on the device | signature verified on the device |

**Recommendation:** ship A first (phases 1–2), then B as an opt-in feature (phase 3). Both feed the same
`merakiplus_upd stage <dir>` → `verify` → `commit-pending` code on the device, so there is exactly one verifier.

On-device TLS details (phase 3):

- **Static BearSSL** inside `merakiplus_upd`; no dependency on the vendor's libcurl/OpenSSL (which a vendor OTA can
  replace and which are end-of-life).
- **Pinned trust anchors** for the hosts involved (`github.com` and the release-asset CDN host it redirects to),
  limited to a small set of public roots; refreshed with each release. Hostname verification on.
- **Clock independence:** certificate validity is checked against `max(system clock, the helper's build date)`, so a
  device whose RTC reset to 1970 still validates; a far-future clock is treated as "unknown" → use the build date.
- Redirects: follow at most 5, HTTPS only, only to the pinned host list.
- Limits: manifest ≤ 64 KB, each asset ≤ the size in the manifest (streamed to flash while hashing; nothing in RAM or
  `/tmp`), total timeouts.
- TLS gives confidentiality and an early failure; **authenticity still comes only from the signature**, so a
  TLS-interception middlebox can at worst block updates, never install one.

### 6.3 Anti-rollback

- A network install (on-device fetch or companion default) requires `seq > max_seq`.
- **Rolling back to `previous`** (already installed, verified when it was installed) is always allowed — it is a local
  choice, not a downgrade attack.
- An explicit downgrade to an older release is possible only from the companion with `--allow-downgrade` (LAN presence
  is required anyway); it resets `max_seq` and is logged in the state file.
- `min_from_seq` lets a release refuse to be installed over very old layouts (forces `install.sh` for big migrations).

---

## 7. Vendor OTA defense (task B7)

| Layer | What | Covers | Cost / risk | Recommendation |
|---|---|---|---|---|
| 0. Structural | While Meraki+ runs, stock code (and its OTA check) does not run. Meraki+ never contacts vendor servers. | all slot-B time | none | keep, document |
| 1. Network | Block `oss.sanbontft.com` (DNS/firewall) in the router, e.g. a UniFi DNS block / traffic rule. | stock sessions | user effort; also blocks vendor fixes incl. controller firmware | **recommended for `--persist` users** (README already says so) |
| 2. On-device pin | At runtime, point the vendor OTA host to `0.0.0.0` in `/etc/hosts` — only if probe P4 shows `/etc` is volatile (tmpfs) **and** the stock UI resolves through `/etc/hosts`. Applied by the launcher (slot-B boots) or the guard line (when `--gesture` is installed). Never written into the rootfs. | stock sessions started from a slot-B boot | small; resets at power-off, so no lasting change to the vendor system | **optional, opt-in, after P4** |
| 3. Detect + repair | Meraki+ (at start) and `install.sh status` look for vendor OTA traces: `/customer/temp.tar.gz` or `demo_temp`, slot A changed, guard line missing, `/new_customer/demo_v9` not ours, flag flipped. The UI shows a one-time notice; `install.sh` repairs (re-installs launcher/guard line, keeps backups, takes the new vendor `demo.sh` as the new baseline). | everything | small | **always** |
| Accept + re-apply | Let the vendor update, then run `install.sh` again. | — | the window between vendor update and re-install runs stock | documented path for users who want vendor/controller fixes |

**Recommended stance:** layers 0 + 1 + 3, layer 2 after the probe. Users who want a vendor update: `revert.sh` → update
in the stock UI → `install.sh` (Meraki+ then checks the controller firmware version against `mcu_fw_tested` and warns
if it is new).

The no-flag row in [boot.md](boot.md) §7 changes slightly with the launcher: a vendor tar extracted into slot B most
likely replaces `/new_customer/demo_v9` (the launcher) — the `rel/` directories survive, so a repair is a re-install of
the launcher only.

---

## 8. Consent, scheduling and UX (tasks B6, B8)

### 8.1 Rules

- **Default: off.** Without opt-in, Meraki+ makes no network request beyond what it does today (Wi-Fi + DHCP).
- **Never during an activity:** grind, brew, steam, hot water, flush/cleaning, a confirmation dialog, an error state, or
  a pending controller frame. INSTALL NOW additionally requires that the machine is idle and confirmed by the user.
- **Never while asleep:** the controller's sleep removes the display's power. Automatic installs therefore use "at next
  start" (the launcher switches at power-on, before the UI starts heating-related work).
- Downloads may run while idle; an activity started during a download pauses the download (it continues later or is
  discarded — staging makes both safe).
- Settings (Meraki+'s own settings file, not `config.json`): `UPDATES` = OFF / CHECK (notify only) / AUTO (download and
  install at next start). `AUTO` is introduced only in phase 4.

### 8.2 SYSTEM › UPDATES (new section)

| Row | Content |
|---|---|
| VERSION | `V6.21` (+ `TRIAL` until healthy) |
| UPDATES | OFF / CHECK / AUTO |
| CHECK NOW | runs the check; result toast |
| LAST CHECK | relative time or `NEVER`; error code on failure |
| INSTALL V6.22 | visible when an update is staged or available: opens notes (first lines) → INSTALL NOW / AT NEXT START |
| ROLL BACK TO V6.20 | visible when `previous` exists; confirmation |

Progress: a thin bar on the row (`DOWNLOADING 42 %`, `VERIFYING`, `READY`). Failures are toasts with a short reason;
details go to `ota/status` (small, overwritten, not a growing log).

Draft strings (EN / DA), to be fitted into the 264 px row budget by the string pipeline:

| Key | EN | DA |
|---|---|---|
| TX_PG_UPDATES | UPDATES | OPDATERINGER |
| TX_SL_UPD_MODE | UPDATES | OPDATERINGER |
| OPT OFF / CHECK / AUTO | OFF · CHECK · AUTO | FRA · TJEK · AUTO |
| TX_UPD_CHECK | CHECK NOW | TJEK NU |
| TX_UPD_LAST | LAST CHECK | SENESTE TJEK |
| TX_UPD_NEVER | NEVER | ALDRIG |
| TX_UPD_INSTALL | INSTALL %s | INSTALLÉR %s |
| TX_UPD_NOW | INSTALL NOW | INSTALLÉR NU |
| TX_UPD_NEXT | AT NEXT START | VED NÆSTE START |
| TX_UPD_ROLLBACK | ROLL BACK TO %s | RUL TILBAGE TIL %s |
| TX_UPD_TRIAL | TRIAL | PRØVE |
| TX_T_UPD_NONE | UP TO DATE | OPDATERET |
| TX_T_UPD_AVAIL | %s AVAILABLE | %s KLAR |
| TX_T_UPD_BUSY | NOT WHILE THE MACHINE IS BUSY | IKKE MENS MASKINEN ARBEJDER |
| TX_T_UPD_NET | NO CONNECTION TO GITHUB | INGEN FORBINDELSE TIL GITHUB |
| TX_T_UPD_SIG | UPDATE REJECTED: SIGNATURE | OPDATERING AFVIST: SIGNATUR |
| TX_T_UPD_HASH | DOWNLOAD DAMAGED · TRY AGAIN | DOWNLOAD BESKADIGET · PRØV IGEN |
| TX_T_UPD_SPACE | NOT ENOUGH SPACE | IKKE NOK PLADS |
| TX_T_UPD_OLDER | OLDER VERSION REFUSED | ÆLDRE VERSION AFVIST |
| TX_T_UPD_LAUNCHER | NEEDS INSTALL FROM A COMPUTER | KRÆVER INSTALLATION FRA COMPUTER |
| TX_T_UPD_DONE | UPDATED TO %s | OPDATERET TIL %s |
| TX_T_UPD_ROLLED | %s FAILED · BACK ON %s | %s FEJLEDE · TILBAGE PÅ %s |
| TX_T_UPD_VENDOR | VENDOR UPDATE DETECTED · RUN INSTALL | PRODUCENT-OPDATERING FUNDET · KØR INSTALL |

---

## 9. Failure modes and handling (task B9)

| Failure | Detection | Result | User sees |
|---|---|---|---|
| Corrupt / truncated download | size + SHA-256 per file before the rename into `rel/` | stage discarded; current unchanged | DOWNLOAD DAMAGED |
| Tampered asset or manifest | signature, then per-file SHA-256 | rejected before anything is staged | UPDATE REJECTED: SIGNATURE |
| Flash full | `statfs` before staging (need 2 × set + 1 MB) and write errors | abort, stage cleaned | NOT ENOUGH SPACE |
| Flash write error / bit rot | re-hash from flash after fsync (drop caches first); launcher hash check at start | stage discarded / launcher falls back to previous | toast after rollback |
| Power loss in any phase | §5.7 | always a complete, verified release or stock | nothing, or a rollback toast |
| New release crash-loops | launcher `tries` / crash storm | rollback to previous within seconds, no network needed | %s FAILED · BACK ON %s |
| Both releases unusable | launcher | stock UI from slot A, Wi-Fi up, `fallback=stock` recorded | stock UI; `install.sh status` explains |
| New release runs but misbehaves | user | ROLL BACK in SYSTEM, or `revert.sh` | — |
| TLS failure / wrong clock | BearSSL error; clock floor = build date | no update | NO CONNECTION TO GITHUB (+ code) |
| GitHub unreachable / rate limited / captive portal | HTTP status, timeouts, non-JSON body | no update; next check later | NO CONNECTION TO GITHUB |
| Downgrade attempt (older signed release) | `seq ≤ max_seq` | refused | OLDER VERSION REFUSED |
| Mixed-version assets | impossible by construction (release directory + one state switch); platform components gated by `requires` | — | NEEDS INSTALL FROM A COMPUTER when a platform part is too old |
| Launcher too old for a release | `requires.launcher` | refused | NEEDS INSTALL FROM A COMPUTER |
| Vendor OTA ran | §7 layer 3 | repair via `install.sh` | VENDOR UPDATE DETECTED |
| Two writers at once (UI commit vs. updater) | `ota/lock` (flock) | serialised | — |
| Update requested during an activity | idle gate | deferred | NOT WHILE THE MACHINE IS BUSY |

---

## 10. Phasing, effort, tests (task C)

Effort is in focused agent-days and excludes waiting for hardware sessions.

### Phase 0 — read-only probes (0.5 d, needs the machine on and the user's go-ahead)

All read-only; no writes, no reboots. Stream long output off the device, never into `/tmp`.

| # | Probe | Answers |
|---|---|---|
| P1 | `cat /proc/mtd /proc/mounts; df -k; ls /sys/class/ubi/; cat /sys/class/ubi/ubi0/{bad_peb_count,max_ec,reserved_for_bad,total_eraseblocks}; cat /sys/class/ubi/ubi0_*/name /sys/class/ubi/ubi0_*/data_bytes` | volume sizes, wear and spare blocks |
| P2 | `busybox` (applet list); `ls -l /bin /sbin /usr/bin /usr/sbin`; `which sha256sum sha1sum xz flock`; `cat /proc/sys/kernel/random/boot_id` | tooling, boot ID |
| P3 | `ls -l /customer/libs/ | grep -i -E 'ssl|crypto|curl'`; `ls -la /etc/ssl /etc/ssl/certs /usr/share/ca-certificates 2>&1`; `strings /customer/libs/libcurl.so* | grep -i -E 'ca-bundle|cacert|/etc/ssl'` | whether the vendor TLS stack verifies, CA location (for the record; the plan does not depend on it) |
| P4 | `ls -l /etc/hosts /etc/resolv.conf /etc/nsswitch.conf; grep -E ' / | /etc ' /proc/mounts`; `ls /proc/net/ip_tables_names 2>&1; ls /config/modules/4.9.84/ | grep -i -E 'netfilter|ip_tables|xt_'` | hosts pin feasibility, firewall feasibility |
| P5 | `free; cat /proc/meminfo | head -5; cat /proc/loadavg` (idle and during a brew if the user is brewing anyway) | RAM headroom for TLS |
| P6 | `date -u; hwclock -r` right after a natural power-on (user powers on; no tool reboot) | whether the RTC survives power-off |
| P7 | `cat /proc/<ui pid>/status | grep -i vm` and the start-up timeline from `/tmp/merakiplus_net.log` | UI memory, when Wi-Fi is up after start (defines "crash before Wi-Fi") |
| P8 | Only if the user happens to run the stock UI: `ls -l /customer/demo_temp /customer/temp.tar.gz /tmp/getlatestversion 2>&1; cat /tmp/getlatestversion` | confirms "check automatic, apply manual" |
| P9 | DIAGNOSTICS screen / UI log: does Meraki+ see the controller firmware version? | basis for `mcu_fw_tested` |
| P10 | `stat -c %X /dev/ttyS1; date +%s` twice, a few seconds apart, while Meraki+ runs; `ls -lue /dev/ttyS1` | confirms the 1a health check's controller-link signal (tty atime) and which applet measures it |

### Phase 1a — hardened companion install (1–1.5 d)

- `push()`: `sync` before the rename; free-space check for updates too; remove stale `.part` files.
- Releases gain `SHA256SUMS` and the WAVs, switcher and shim as assets (CI); the installer pins SHA-256 alongside MD5.
- Post-install health check extended from 5 s to 60 s with a clear "run `revert.sh` now" path.
- `install.sh status`: vendor OTA trace detection (§7 layer 3); offer to remove stray old binaries.
- Tests: `tests/test_installer.py` (mock device) for sync ordering, space errors and OTA-trace detection.
- Hardware: one ordinary update on the user's machine, observed, idle.
- Risk: low.

### Phase 1a — implemented (2026-11-22)

Installer, CI and docs only — **no shipped binary changed** (no version bump; `bin/` and the device UI are
the v6.20 builds). What `tools/merakiplus.py` does now:

| Item | Implementation |
|---|---|
| Sync before rename | `push()`: upload to `<dest>.part` → md5 (+ SHA-256) → `chmod` + `sync` → `mv` + `sync` → re-verify. `atomic_copy()` already did cp + `sync` → md5 → `mv` + `sync`; the state file is now written tmp → `sync` → `mv` → `sync` as well. Power cut before the rename: destination untouched, a stale temp file; after it: the complete new file. |
| Stale temp files | `status` counts `*.part` / `.*.tmp` left by interrupted runs (only our own temp names); `install` removes them. |
| Stray old builds | `status` lists files in `/new_customer/merakiplus/` that carry the Meraki+ marker and are neither the staged copy nor running. `install` offers to remove them interactively; with `--yes` they are kept (never deleted unasked). |
| Free space | checked for updates too: all uploads + the slot-B copy + 512 KB. |
| SHA-256 | `BIN_SHA256` / `AUDIO_SHA256` pinned next to `BIN_MD5` / `AUDIO_MD5`; every file is checked against both on the computer before upload. On the display SHA-256 is checked in addition to md5 **only when its busybox has `sha256sum`** (detected once per run; not yet proven on this firmware, probe P2), otherwise md5 stays the gate. `tests/hygiene.py` keeps both tables in sync with `bin/` and `audio/`. |
| Release assets | CI publishes `SHA256SUMS` next to `MD5SUMS` (kept for compatibility) and adds `switcher`, `grab_shim.so` and the three WAVs as assets. |
| 60 s health check | after activation: the same PID must stay alive for 60 s (no crash, no watchdog restart), must hold the controller link `/dev/ttyS1` open, and the controller must be talking — measured read-only through the tty layer, which updates the device node's access time on every successful read (8 s granularity): last data ≤ 20 s old. Failure → `HEALTH CHECK FAILED: …` + `./revert.sh --ip …`, exit 1, no automatic rollback (1b). If neither `stat -c %X` nor `ls -e` is available, the link activity is reported as not measurable (a warning, not a failure). |
| Vendor OTA detection (§7 layer 3) | `status` and `install` report: `/customer/temp.tar.gz` or `/customer/demo_temp` (an update waiting for the next boot), slot B no longer Meraki+, gesture installed but the guard line missing, `demo.sh` (without our line) or slot A changed since install (baselines `sh_md5` / `a_md5` in `merakiplus.state`, refreshed by every install), boot flag cleared together with a changed slot A. The advice is to re-run `install.sh` with the previous options; a re-install repairs everything and keeps a newly found vendor slot-B program as an extra backup (`demo_v9.vendor.<md5>`; the first backup is never overwritten). The vendor tarball itself is never touched. No router or hosts blocking (§13 decision 6). |
| Tests | `tests/test_installer.py` against the mock display: sync-before-rename for every staged rename, SHA-256 rejection of a tampered binary, display without `stat`/`sha256sum`, health check success and its three failure paths (crash, silent link, link not open) followed by a working revert, free space on update, temp-file and stray-build handling, every vendor trace and the repair. |

Deviations from the plan text above, all deliberate:

- **No UI-side indicator yet.** §7 layer 3 also mentions a one-time notice in Meraki+; that needs a UI change
  and lands with phase 1b (together with the UI's own *healthy* marker). The installer is the detector in 1a.
- **"Controller talking" is measured from outside** (tty access time) instead of a UI-written marker, so no
  binary had to change. Phase 1b replaces it with the UI's own healthy commit (frames ≥ 30 s, run ≥ 60 s).
- **Probe P10 (new, read-only)** for the next hardware session: `stat -c %X /dev/ttyS1; date +%s` twice a few
  seconds apart while Meraki+ runs (confirms the atime signal and the `stat` applet), and `printf abc | sha256sum`
  (P2). Until then the installer degrades to warnings, never to a false success.
- Hardware: the "one ordinary update on the user's machine" is pending (the machine was offline).

### Phase 1b — implemented (2026-11-22)

Shipped as **v6.21** (the first release with the phase 1a installer too). Not yet verified on hardware (see the end of
this section).

| Item | Implementation |
|---|---|
| Layout | `/new_customer/demo_v9` = the launcher; `/new_customer/merakiplus/rel/<version>-<sha8>/` = `demo_v9` (UI), `merakiplus_snd`, `audio/*.wav`, `MANIFEST` (`file <path> <size> <sha256>` per file); `ota/state`, `ota/healthy`, `ota/launch.log`, `ota/lock`. Settings, backups and the gesture parts stay where they were. |
| Launcher | `src/merakiplus_boot.c`, static musl ARMv7, ~60 KB (stdio; the plan's 40 KB target was not pursued), no UI/network/UART, no signals. Algorithm as §5.4 with these concrete rules: a trial (`pending`) gets `tries+1` written (tmp → fsync → rename → fsync dir) **before** the exec; at the 4th start without a healthy marker (3 failed starts) or when its files do not match the `MANIFEST` it is rolled back (`last=rolled_back:<name>:crashloop|damaged`). A trial that wrote `ota/healthy` is committed at the next launcher start (or by the installer right away). Steady state writes **nothing** to flash: per-boot counters live in `/tmp/merakiplus_launch` (boot id, release, PID of the last start, starts in a row without a healthy marker, start times). A level (current → previous → stock) is left for the rest of the boot after 3 starts in a row without healthy, a 7th start within 10 min, or a `MANIFEST` mismatch. An explicit start (`MERAKIPLUS_LAUNCH=gesture|reload|install`) resets the level. Stock = `chdir /customer`, slot A's `LD_LIBRARY_PATH`, `execv /customer/demo_v9`, argv0 `./demo_v9`, Meraki+ variables removed. The UI gets `MERAKIPLUS_REL`, `MERAKIPLUS_RELNAME`, `MERAKIPLUS_TRIAL`, `MERAKIPLUS_FALLBACK`, `MERAKIPLUS_NOTE`. `--dry`, `--selftest <dir>`, `--version`. |
| Healthy | The UI writes `/tmp/merakiplus_healthy` (`boot=<boot_id> pid=<pid> rel=<name> state=healthy`) once it has run ≥ 60 s **and** the controller has answered without a gap (link up = a frame within 1.5 s) for ≥ 30 s; on a trial also `ota/healthy` on flash. A deliberate exit (SWITCH TO ORIGINAL, exec-reload, SIGTERM) writes `state=exit`, so it does not count as a failed start. boot_id + PID make a marker from an earlier boot or process worthless. |
| Activation (§13 decision 3) | The installer writes `pending=<new>` and, when the running UI is v6.21+, `/tmp/merakiplus_reload`. The UI checks it every 2 s and, only when idle (HOME or the OFF screen, no activity, nothing in the write queue, no save, no dialog, no touch, no sound playing), marks every descriptor close-on-exec and `execv`s the launcher — same PID, no signal. A busy machine defers it (the installer says so; the trial starts when idle or at the next power-on). Older UIs: the 1a paths (watchdog exec-reload / stop-first switch). `--no-activate`: next power-on. |
| Installer | Stages `rel/.stage-<name>` (every file uploaded or copied on the display, md5 + SHA-256 when available, synced), writes the `MANIFEST` last, `sync`, one rename. First install with this layout: the running v6.17–v6.20 UI is copied into its own release directory (rollback target, `current`), the new one becomes `pending`. Then the state file, then the launcher: uploaded next to slot B, **selftested on the display**, synced, renamed over `/new_customer/demo_v9`. After healthy: commit (`current`/`previous`) and removal of release directories nobody references. Health failure: exit 1, the trial stays (the launcher rolls it back after 3 failed starts), `./revert.sh` advice. An installed switcher is refreshed (it must classify `rel/<name>/demo_v9` as Meraki+) and the running one restarted with the guard line's `LD_PRELOAD`. `status`: launcher, releases, trial, last rollback, this boot's level, healthy marker. `revert.sh`: as before, plus `rel/`, `ota/` and the flat sound files are removed; backups stay. |
| UI | v6.21: release-relative sound helper/WAVs (`MERAKIPLUS_REL`, else its own exe directory, else the v6.18–v6.20 paths); a release exe counts as slot B; the SYSTEM line shows `· TRIAL` / `· V6.22 FAILED` / `· FALLBACK` / the vendor notice; one-time toasts for a rollback and a vendor update. The plan's SYSTEM › UPDATES section (CHECK NOW, INSTALL, ROLL BACK) belongs to phase 3. |
| Vendor OTA notice (§7 layer 3, deferred from 1a) | The UI checks at start for `/customer/temp.tar.gz`, `/customer/demo_temp`, and an installed gesture without its `demo.sh` line. |

Power cut at each step (all verified by replay, see Tests):

| Step | On flash after a cut | Next start |
|---|---|---|
| Upload/copy into `rel/.stage-<name>` | partial stage, unreferenced | old program or launcher as before; the next install removes the stage |
| Rename stage → `rel/<name>` | no `rel/<name>` or the complete one | as before |
| State write (installer or launcher) | the old or the new state file (tmp + sync + rename + sync) | the release the old or the new state names |
| Launcher upload + selftest | `.demo_v9.launcher` (removed by the next install), old `/new_customer/demo_v9` | the old program (vendor or the previous Meraki+) |
| Launcher rename | old program or the launcher; releases + state were synced before | old program, or the launcher starts the trial / current |
| Launcher `tries+1` before the exec | old or new `tries` | the trial again (counts towards the limit) |
| Healthy marker / commit | old or new | uncommitted: the next start commits it |
| Revert | slot B is restored before `rel/` is removed | vendor program; leftovers are removed by the next revert |

Tests (all without hardware):

- `tests/launcher_watchdog.sh` — the vendor watchdog (restart after every exit) with fake UIs: crash before Wi-Fi on a
  first install (3 trial starts → stock), crash in every release (new ×3 → current ×3 → previous ×3 → stock; after a
  power cycle again, with no state write), healthy trial → commit, healthy-on-flash survives a power cut, healthy then
  crashing (7th start in 10 min → previous), hang, damaged current / trial, clean exits, no state file, stage names;
  the environment the UI gets. Runs with the host build in CI and with the static ARM build in an arm32v7 container
  (`python3 tests/test_launcher.py --arm`, local).
- `tests/test_launcher.py` — selftest + the model + **power-cut replay**: every launcher file operation is logged
  (`MERAKIPLUS_BOOT_OPLOG`); for every prefix the flash contents are rebuilt keeping only synced data (renames after
  the last directory sync landed or not, unsynced data truncated or junk), and the launcher must parse the old or the
  new state and start the same release as without the cut. A build without the fsync before the rename fails it.
- `tests/test_installer.py` — every 1a scenario adapted, plus: migration of a v6.20 `--persist --gesture` install
  (release copy, trial, watchdog activation, commit, switcher restart, no-op re-install, revert), **installer power-cut
  replay** (the mock snapshots `/new_customer` at every `sync`; each snapshot, with temp data synced or junk, must boot a
  complete release — the launcher's host build decides — or the untouched old program), update by exec-reload (same
  PID), deferral while busy and the reload once idle, collection of old releases, `--no-activate`.
- UI self-test section *v6.21 OTA phase 1b* (release paths, healthy timing incl. a link gap, markers, the SYSTEM line
  and toasts in EN + DA within their pixel budget, vendor traces, the idle gate).

Deviations from the plan text above, all deliberate:

- **No `merakiplus_upd`, no signed manifest yet** (phase 2): release names are `<version>-<sha8>` instead of
  `<seq>-<version>`; the `MANIFEST` is written by the installer from its pinned SHA-256 table. No `max_seq`.
- **Steady-state demotion is per boot**, not persistent: a release that crash-loops is tried again after the next power
  cycle (a few seconds) — self-healing for transient causes, no flash wear. Only a failed trial is rolled back for good.
- **No ROLL BACK button** in the UI yet (phase 3 UPDATES section); `./revert.sh` remains the manual way back.
- **The installer commits** after its health check instead of waiting for the next launcher start; the launcher commits
  a healthy trial itself when no installer is around.
- **`ota/lock`** is taken by the launcher only (non-blocking, ≤ 2 s); the installer writes the state only before it
  activates and after the health check, and uses its own temp name.

Next hardware session (user present; §13 decision 5; together with phase 0 probes P1–P10):

1. Ordinary update to v6.21 with `./install.sh` (idle machine): migration, launcher selftest on the display, watchdog
   activation, healthy marker, commit; `./install.sh status`.
2. **Clear the boot flag first** (user-approved write), so every power cycle returns to stock.
3. Canary releases built for the test (not published): one that exits at once → 3 starts → rollback; one that crashes
   before Wi-Fi with the flag set → stock fallback brings Wi-Fi back (telnet reachable).
4. Power cuts **performed by the user** at announced points (during a trial start, right after a commit); any
   injected long power-button press needs explicit approval per session.
5. Restore the flag; revisit the vendor OTA block decision (§13 decision 6) before stock runs during these tests.

### Phase 1b — launcher, release directories, auto-rollback (3–4 d) — the original plan

- `src/merakiplus_boot.c` (launcher), state file library shared with the updater, UI changes (asset path,
  healthy commit, exec-reload, SYSTEM shows version/trial/ROLL BACK), switcher and installer compatibility (§5.6),
  migration (current UI → `rel/<seq>`; launcher installed last, by atomic rename) and `revert.sh` support.
- **Container tests (arm32v7):** a model of the vendor watchdog loop + fake UIs (crash at once / crash after 10 s /
  hang / healthy / exits) → assert rollback after 3 tries, crash-storm detection, stock fallback (a stub
  `/customer/demo_v9`), no state writes in steady state.
- **Crash-consistency tests (host):** all file operations go through a small layer (write, fsync, rename, unlink). A
  test build records the operation log; the harness replays every prefix, keeping only data that was synced before the
  cut, then runs the launcher on the result and asserts: it starts a complete, hash-correct release or stock — never a
  partial file. Covers every power-cut point of §5.7 exhaustively.
- **Sim:** SYSTEM › UPDATES previews in EN/DA; selftest checks for the idle gate and trial/rollback toasts.
- **Hardware (user present):** first with the **boot flag cleared** so a power cycle always returns to stock (a write the
  user approves beforehand): install launcher + current release, verify commit after 60 s; install a deliberately
  crashing canary release built with a **test key** → verify automatic rollback; then a canary that crashes before Wi-Fi
  → verify the stock fallback brings Wi-Fi back; then power-cut tests at announced phases, performed **by the user**
  (unplug, or a power-button hold ≥ 0.8 s). Injecting a long power-button press would also cut power, so it counts as
  an agent-triggered reboot and is never done without explicit approval. Finally the user restores the flag.
- Risk: medium (new critical binary); mitigated by size, the exhaustive tests and the on-device selftest before swap.

### Phase 2 — signed manifests (2 d)

- Offline key generation (user decision on custody), `tools/sign_release.py` (download, reproducible rebuild compare,
  manifest, sign, upload), Monocypher in `merakiplus_upd`, pure-Python verifier in the installer, `seq`/`max_seq`.
- Companion installs verify on the computer **and** on the device (`merakiplus_upd stage/verify/commit-pending`).
- Tests: RFC 8032 vectors; tampered manifest/asset/signature; wrong key; older `seq`; `requires` mismatches; manifest
  canonicalisation.
- Hardware: one signed update via the companion.
- Risk: low; key handling is the main human risk.

### Phase 3 — on-device check + fetch with consent (5–7 d)

- BearSSL in `merakiplus_upd`, pinned anchors, redirect handling, clock floor, streaming to `stage/`, status file.
- UI: SYSTEM › UPDATES (OFF/CHECK), CHECK NOW, INSTALL NOW / AT NEXT START, progress, toasts, idle gate.
- **Container tests:** a local HTTPS server with a test CA and the test signing key: 200/302/404/500, truncated body,
  slow body, wrong host in a redirect, expired/not-yet-valid certificate with the clock at 1970, signature/hash/seq
  failures, disk full (small loop-mounted volume).
- **Sim:** UPDATES rows, toasts, idle gate (an update offered during a brew is deferred), EN/DA previews.
- Hardware: CHECK NOW against the real GitHub release; install a real signed release; observe RAM during TLS.
- Risk: medium (TLS code size, anchor maintenance). Effort is dominated by TLS integration and tests.

### Phase 4 — opt-in automation (1.5–2 d)

- UPDATES = CHECK: check after Wi-Fi comes up and then at most once a day while on; notify only.
- UPDATES = AUTO: download while idle, install **at next start** only; never INSTALL NOW automatically.
- Stable channel only; pre-releases only by companion.
- Tests: sim time-warp scenarios (check cadence, deferral, no install during activity).
- Risk: low once phases 1–3 are done.

### Vendor OTA layer 2 (0.5–1 d, after P4)

Runtime hosts pin in the launcher/guard line, opt-in, never in the rootfs; tests in the container; one hardware check
that the stock UI's check fails cleanly while pinned.

### What runs where

| Test | Host / container | Sim | Hardware |
|---|---|---|---|
| Launcher state machine, crash loops, stock fallback | ✔ (arm32v7 container, watchdog model) | | confirm once with a canary |
| Power-cut consistency | ✔ (operation-log replay, exhaustive) | | a few user-performed cuts |
| Signatures, hashes, anti-rollback | ✔ | | one real signed update |
| HTTPS fetch | ✔ (local TLS server) | | one real fetch |
| UI rows, strings, idle gate | | ✔ | visual check |
| Vendor OTA detection | ✔ (mock device) | | only if a real vendor OTA happens |
| UBIFS behaviour itself | | | not tested by us (relied upon; P1 records its health) |

---

## 11. Security posture (task B10)

- No new listening sockets, no daemon: the updater runs on demand and exits.
- New outbound traffic only with opt-in: HTTPS to GitHub, no identifiers, no telemetry.
- No secrets on the device beyond what is already there (Wi-Fi credentials in `config.json`). The updater never logs
  `config.json`, never sends it anywhere, and only holds public keys.
- Telnet stays open (vendor; also our recovery path). The plan does not make it worse; it remains the main local risk,
  and the README keeps telling users to isolate the machine.
- Signing keys never in CI; CI can build and publish, but cannot make devices accept anything.
- Supported versions: only the latest release (SECURITY.md); devices on old releases can always update through the
  companion path.

---

## 12. Open questions for the user

1. **On-device network access:** today the README promises that Meraki+ contacts no servers. Is an opt-in, default-off
   HTTPS check to GitHub acceptable? (If not: phases 1–2 only; the companion stays the transport.)
2. **Key custody:** where the primary signing key lives (passphrase-protected file vs. hardware token) and who keeps the
   cold backup key.
3. **AUTO mode:** is "download automatically, install at next power-on" acceptable, or should every install need a tap?
4. **Stock fallback:** when both Meraki+ releases fail, may the launcher start the stock UI automatically (recommended:
   it restores Wi-Fi and thus the recovery path), or should it keep retrying Meraki+?
5. **Hardware sessions:** OK to clear the boot flag for the phase 1b tests (and restore it afterwards), and who performs
   the power cuts?
6. **Vendor OTA:** activate the router block now (recommended before phase 1b), and is the on-device hosts pin wanted?
7. **Pre-releases:** should owners be able to opt into a beta channel, or stable only?
8. **Gen 2:** untested hardware; should the updater refuse to run when the board fingerprint differs (recommended), as
   the installer already does?

---

## 13. Decisions (resolved by the maintainer)

All eight open questions above are answered. This section is the binding record; where a decision differs from the
recommendation in the plan, the decision wins.

1. **On-device network access: YES, opt-in.** The display may contact GitHub over HTTPS to check and fetch updates,
   default-off. The README's "contacts no servers" promise must be updated to describe the opt-in updater (phase 3).
2. **Key custody: passphrase-protected file.** The primary Ed25519 signing key lives as a passphrase-protected file on
   the maintainer's own computer; a cold backup key is kept offline by the maintainer. No hardware token required.
3. **Activation: automatic at next power-on / when idle.** Downloading requires an explicit consent tap; once a version
   is downloaded and verified, activation happens automatically at the next power-on or when the machine is idle and
   confirmed — no extra tap per install. Never during an activity.
4. **Stock fallback: YES.** When both Meraki+ releases fail their health checks, the launcher automatically starts the
   stock UI from slot A (read-only; boot flag untouched). This restores Wi-Fi and the recovery path.
5. **Hardware sessions: YES.** The boot flag may be cleared for phase 1b power-cut/rollback tests and restored
   afterwards, so any power cycle returns to stock. The user performs all physical power cuts; long-press injection
   needs explicit approval per session.
6. **Vendor OTA block: NOT YET.** No router (UniFi) block and no on-device hosts pin for now. Risk accepted: while
   Meraki+ runs, the vendor update code does not run. Revisit before phase 1b hardware tests, since a stock boot during
   testing is exposed to vendor OTA; the detect-and-repair layer still applies.
7. **Channel: stable only.** No beta channel.
8. **Board guard in the updater: NO.** The updater does not refuse non-Gen1 board fingerprints (maintainer's choice,
   deviating from the recommendation). The installer's existing board check is unchanged. Consequence: Gen2 or other
   boards are not blocked by the updater itself; hardware verification on non-Gen1 remains out of scope and at the
   owner's risk.
