# Water dosing compatibility — controller firmware detection and UI gating (research + design)

Status: **implemented in v6.22 (2026-11-22)** — gate + manual hot water, per the maintainer decisions in
[§10](#10-decisions-resolved-by-the-maintainer); implementation, tests and deviations in [§11](#11-implementation-v622).
Not yet installed on the reference machine (next hardware session, §11.5). §1–§9 are the research as written before
the decisions; where they differ (display-side limits, `T_MAN`, strings), §10/§11 win.
Addresses are in the controller firmware `MEM03_V514_260923.bin` (powerMcu **5.1.4**, flash base `0x0`) and
in the stock display app `demo_v9` (build **1.1.1-69**, symbols kept; VMA = file offset + 0x10000). Neither
binary is in this repository. Register words are as in [registermap.md](registermap.md); frames as in
[protocol.md](protocol.md).

## 0. Summary

* **What "water dosing" is:** hot water in AUTO mode, stopped **by the controller** when the brew scale
  passes the target weight (word 17). Meraki+'s only water start today (`cmd6 sub9 len11`, word 16 = 1,
  word 17 = target) relies on it; Meraki+ sends no stop for a normal dose.
* **Which firmware has it:** the vendor release notes for controller **5.1.4** (and display 1.1.1-69) say
  *"Added hot water weighing."* The stock UI shows the weight-dosed hot water UI only when the controller
  reports a version **greater than 5.1.2**; every older controller gets a manual-only hot water page (§3.3).
* **The controller reports its own version, read-only, in the normal status poll:** words **80..87** hold
  the ASCII firmware string (`"V5.1.4"`), words 88..95 a copy. The stock UI reads it from the `sub56 len56`
  poll — the same poll Meraki+ already sends every 200 ms. Confirmed in the 2026-11-16 capture (§3.1).
  registermap.md called these words "UID block"; corrected in this commit (§3.1).
* **Recommendation: mirror the stock gate exactly.** Dosing = version in words 80..87 parses and is
  `> 5.1.2`. No version yet / empty / unparsable → **manual hot water only** (the stock UI does the same).
  No owner confirmation, no file probes, no OTA traffic. Keep the stock runtime fallback too: a scale fault
  forces manual.
* **Prerequisite (done in v6.22):** Meraki+ had no manual hot water (WATER MODE = MANUAL only showed *"use stock UI"*).
  Manual start/stop must land in the same release as the gate, or owners with older controllers lose hot
  water in Meraki+ entirely (§5).
* **The reference machine doses** — by version (`V5.1.4` in words 80..87) and by behaviour (two doses stopped
  by the controller, §1.3).

## 1. What dosing requires in controller fw 5.1.4

### 1.1 Hot water control `0x1B8C` (called every 10 ms cycle)

Register base `r6 = 0x20000F96` (word 0); per-activity state `r5 = 0x20000028`.

| Step | Address | What it does |
|---|---|---|
| Conflicts | `0x1BA4..0x1BE2` | If work_sta (word 33) bit 2 (brew) or bits 5/6 (steam/grind) are set, or **word 20 ≠ 0**: clear words 16 **and** 19, clear the active flag and the water bits. A water start during a brew/grind/steam is cancelled, not queued. |
| Errors | `0x1C26..0x1C42` | Masks from `0xF138/0xF13C`: word 34 & `0x1200`, word 35 & `0x8001`, word 36 & `0x8001` → same cancel path. |
| Active | `0x1C44..0x1C4E` | `active = (word 19 \| word 16) != 0`, recomputed every cycle. |
| Start edge | `0x1C50..0x1C62` | On inactive → active: **tare the brew scale** (`0xAF90` on scale object `0x200010DC`). Manual and auto both tare. |
| **Weight stop (auto only)** | `0x1C64..0x1C84` | Only if **word 16 ≠ 0**: `weight = (s16)[0x200010DC+6]` (the brew-scale value that is also word 39, 0.1 g). **If `weight > word17 + 15`** (target + 1.5 g) **or word 35 & `0x1400`** (scale fault): write **word 16 = 0**, clear the active flag. |
| Outputs | `0x1C86..0x1CB2` | Active → `work_sta \|= 0x20A` (bits 1, 3, 9). Inactive → clear bit 3; clear bits 1 + 9 only when the boiler refill state `[0x2000002B]` is idle (9). |

So **auto** = word 16 plus a weight stop at word 17. **Manual** = word 19 with **no stop condition** except the
conflict/error cancels: it runs until the display writes word 19 = 0. The controller has no manual timeout.

*Open detail:* the static threshold is target + 1.5 g, but the live capture saw the stop when word 39 read
40.5 / 41.0 g for a 40.0 g target. Poll sampling (~100 ms at ~6.5 g/s) explains part of it; the rest is
unresolved. It does not change the design: the stop is in the controller and is weight-based.

### 1.2 Word 20 and the boiler refill (`0x226E`)

`0x226E` is the **boiler auto-refill** state machine (level probes `0xB336`/`0xB32E`, work_sta `0x210`, state
byte `[0x2000002B]`, 9 = idle). It returns at once while **word 20 ≠ 0** (`0x22AC`), and hot water is cancelled
while word 20 ≠ 0. In 5.1.4 word 20 is a lock-out for refill and hot water, not part of the dosing path
(registermap.md's "water target / add_water" reading is not supported by this code).

### 1.3 Live evidence (reference machine, 2026-11-16)

Two stock hot water presses: `cmd6 sub9 len11` with word 9 = 1, word 16 = 1, word 17 = 400; work_sta
`0x0101 → 0x030B`; scale tared; weight rose to 40.5 / 41.0 g; **the controller cleared bits 1/3/9 and word 16
itself**; no stop frame was sent.

### 1.4 What is plausibly new in 5.1.4

Only the **word 16 weight branch** (`0x1C64..0x1C84`) and probably the **tare on start** need weighing. The
manual path (word 19), the cancels and the output bits are plausibly older. What word 16 does on a
controller ≤ 5.1.2 is **unknown** (no older image is available, §2.3): Meraki+'s current start frame could
do nothing there, or start hot water that only a manual STOP ends. The stock UI never sends word 16 = 1 to
such a controller (§3.3) — Meraki+ must not either.

### 1.5 Side finding: the controller reads the display's build number

The controller's cmd 9 handler (`0x9E12..0x9E9C`) stores the display's version string and derives a number
from characters 6–7 (`"1.1.1-69"` → 69) and from the serial block (words 96/98). It picks a brew-boiler
offset of 2.5 / 7.5 / 12.5 °C (`[0x200000B6]` = 25/75/125; build > 63 and hardware number > 25 → 12.5 °C),
which `boiler_heat` adds to word 11 (`0x3604`). This fits the live data: brew temperature 87.0 °C, e-boiler
~100 °C. Meraki+ answers 0x89 with `"1.1.1-69"` and so gets the same offset as the stock UI — **it must keep
answering exactly the stock build string.**

## 2. Controller firmware versions

### 2.1 Where the version lives

`"V5.1.4"` is in the firmware image and is copied to RAM `0x20000000` at start. Init copies 16 bytes from
there into the register table twice: `0x99B0` → words 80..87, `0x99BC` → words 88..95. The OTA compare
(`0xB51C`) reads the same `0x20000000` as "current version". The debug UART also prints it
(`"Project name :%s , App_version = %s"`).

### 2.2 The OTA version compare (`0xB470`) — not a detection method

The OTA request (`CC DD 01`, size + byte sum + 7-byte version) is answered with a status from `0xB470`:
first character must match (`'V'`); any requested digit greater than the running one sets "newer";
`0x81` = newer (accept), `0x80` = not newer. On `0x81` the controller **erases its staging flash** and enters
the OTA receive state (`0xB7B6..0xB816`). Never use this to probe a version.

### 2.3 Vendor channel and older images

`https://oss.sanbontft.com/ota/62091/powerMcu/release.json` lists only the latest firmware (5.1.4; re-checked
2026-11-22). Bucket listing is denied (`AccessDenied`); file names carry a build date
(`MEM03_V<ver>_<yymmdd>.bin`) and are not guessable, so no guessing was attempted. **No pre-5.1.3 image was
obtained.** The reference machine's slot B still holds an older stock display app (June build, kept by the
installer as `boot/demo_v9.vendor`); pulling it read-only (probe P9d) would show the previous display's hot
water flow.

## 3. How the stock UI knows the controller version

### 3.1 Reading it — words 80..95 in the `sub56 len56` poll

* `fuart_thread` polls `(3,0,56)`, `(3,33,7)`, `(3,56,56)`, `(3,33,7)` (`0x146B34..0x146B7C`); `uartRxProcess`
  (`0x144EE0`) copies every reply into the register mirror at `app+0xFE8`. The version string is used from
  `app+0x1088` = mirror byte 160 = **word 80**. The serial number next to it (`app+0x10A8`) = words 96..111.
* Shown as *"MCU Version"* on the About page (`0xE8DB8`) and on the error page (`0xF8D0C`).
* Live capture (2026-11-16): the `0x83 sub56 len56` reply carries `56 35 2E 31 2E 34 00…` (`"V5.1.4"`) at
  word 80 and again at word 88.
* Nothing writes the version to disk: it is no `config.json` key and no file.
* Neither the 0x89/cmd 9 handshake (only the *display's* version) nor the OTA RX parser (`ota_check`
  `0x147AA4`: status, page, progress) carries the controller version.
* **Correction for registermap.md:** words 80..87 / 88..95 are the controller **firmware version string** (and
  its copy), not a chip UID.

### 3.2 Comparing it

`compare_versions(a, b)` (`0x139124`) skips up to 6 leading non-digits (`"V5.1.4"` → `5.1.4`), compares the
dotted numbers part by part and returns −1 / 0 / 1. **An empty string compares equal to anything.**

### 3.3 The hot water gate

`mcu_has_hot_water_weight_ui()` (`0x10885C`) = `compare_versions(word80 string, "5.1.2") > 0` (literal at
`0x178FB0`). It is checked on the HOME hot water tap (`0x10A1B8`, `0x10A580`) and in `uartPageGoto`
(`0x1243A0`):

* **true** → the new hot water panel (AUTO / MANUAL modes, the water weight page).
* **false** — including "not polled yet" and empty → the legacy **manual-only** water page (`page_water_init`
  `0x105950`): `set_manual_hot_water_switch` (`0x105118`) writes word 19 with `usart_send(6,19,1)`.

`getHotWaterModeValue` / `setHotWaterModeValue` / `getAutoHotWaterTarget` contain no version check; the gate is
upstream. `hotWaterMode` and `autoHotWaterTarget` stay in `config.json` on every machine but are simply not
used below 5.1.3.

Stock frames above the gate: AUTO start `hotWaterStartAutoSwitch` (`0x113D10`): word 9 = 1, 19 = 0, 16 = 1,
`usart_send(6,9,11)` (a target below 300 is reset to 3600 first); MANUAL start (`0x113D94`): word 16 = 0,
19 = 1, `usart_send(6,9,11)`; stops `usart_send(6,16,1)` (`0x11468C`) / `usart_send(6,19,1)` (`0x114720`).
**No display-side manual timeout exists in the stock UI.**

**Runtime fallback:** a scale fault (`hotWaterScaleFault` = word 35 bits `0x1000` or `0x0400` — the same bits
the controller's weight stop checks) forces MANUAL, stops a running auto dose (`usart_send(6,16,1)`
`0x115BD8`), blocks switching to AUTO (`0x10A62C`) and opening the weight page (`0x10A678`).

### 3.4 Version floors elsewhere in the stock UI

| Controller version | Display OTA | Controller OTA | Hot water |
|---|---|---|---|
| < 3.8.5 | blocked (`software_update` `0x1390D4`) | not offered | manual only |
| 3.8.5 … < 4.1.6 | allowed | **never offered** (`mcu_check_ver` `0x139AFC` returns before fetching the manifest) | manual only |
| 4.1.6 … 5.1.2 | allowed | offered when the manifest is newer | manual only |
| > 5.1.2 (5.1.4) | allowed | offered when newer | AUTO (weight) + MANUAL |

So machines below 4.1.6 can never reach 5.1.4 through the stock UI: the gate is permanent for them.

### 3.5 `/customer/mcu.bin`

A download cache, not a version record: the stock UI fetches the controller image only when the owner opens
the controller update page (`mcu_check_ver` → `McuGetUpdateLatestVersion` → `mcu_down_load` `0x139B94`), skips
the download when `md5sum /customer/mcu.bin` already matches the manifest, and **moves `mcu_tmp.bin` to
`mcu.bin` even when the download or md5 check failed** (`0x139CFC`; status byte `app+0x1773`). Nothing ever
deletes it. Flashing uses `"V" + mcu_latest_version` from the manifest (`star_mcu_ota` `0x139DE4`). The update
page shows the **manifest's** version (*"MCU Software V%s"*, `0xF2BEC`), not the installed one. → `mcu.bin` can
be missing, stale or corrupt; it says nothing reliable about the running controller.

## 4. Detection options for Meraki+

| # | Option | Read-only | Reliable | Verdict |
|---|---|---|---|---|
| D1 | **Words 80..87 version string, stock rule `> 5.1.2`** | yes (already polled) | yes — it is what the controller itself reports, and what the stock UI uses | **recommended** |
| D2 | md5 of `/customer/mcu.bin` | yes | no (§3.5) | DIAGNOSTICS only, if at all |
| D3 | OTA request as a version oracle | **no** | — | **reject** (§2.2) |
| D4 | behavioural probe (write word 16, watch) | no — dispenses water | — | **reject**: cmd 6 is a memcpy + ACK on every firmware |
| D5 | owner confirmation | yes | depends on the owner | **not needed** with D1; optional override only (§8) |

## 5. Recommended design

### 5.1 The capability

```
mcu_ver   = ASCII of words 80..87 (stop at NUL), valid only if words 80..87 == words 88..95,
            it starts with a non-digit prefix of ≤ 6 chars followed by digits and dots, and the link is up
            (the copy check is stricter than stock; a mismatch only costs convenience)
dosing    = mcu_ver valid && cmp(mcu_ver, "5.1.2") > 0           (stock compare_versions semantics)
            && !(word35 & 0x1400)                                  (stock scale-fault fallback)
```

Until the first `sub56` reply arrives (≤ 200 ms after link-up) `dosing` is false: the UI may show the WATER
page in manual form for one frame. The value can change at runtime (controller restart, scale fault); the UI
re-reads it every frame like any other register-derived state. Words 80..95 are never written by Meraki+.

### 5.2 What changes

| Element | dosing | not dosing |
|---|---|---|
| WATER page row HOT WATER (S_HOTW, weight target) | shown | **hidden** (config value untouched) |
| WATER page row WATER MODE (S_HOTMODE) | shown, AUTO / MANUAL | **hidden**; live line says *MANUAL* |
| Start in AUTO | `sub9 len11`, word 16 = 1, word 17 = target (today) | **never sent** — `act_guard` refuses unless `dosing` |
| Start in MANUAL | new: `sub19 len1`, word 19 = 1 (the stock legacy page's frame) | same — the only start |
| STOP WATER | word 16 = 0 (+ word 19 = 0 when seen) — unchanged | word 19 = 0 (+ word 16 = 0 when seen) |
| Display-side limit (new, both modes) | stop when weight > target + 10 g (never triggers on 5.1.4) | stop after `T_MAN` or when the scale passes 500 g |
| HOME label under the water icon | target weight | *MANUAL* (running time while active) |
| Water temperature live line (word 40) | unchanged | unchanged |
| DIAGNOSTICS | new line *CONTROLLER V5.1.4 · DOSING YES* | *CONTROLLER V4.x · DOSING NO* / *— · UNKNOWN* |

`hotWaterMode = 2` (MANUAL) on a dosing machine: Meraki+ starts manual water instead of the current
*"use stock UI"* toast. `hotWaterMode = 1` on a non-dosing machine: ignored, exactly as the stock UI ignores it.
No migration and no `config.json` write is needed.

Manual time limit `T_MAN`: the stock UI has none and the controller has none (§1.1); a Meraki+ crash during
manual water would leave it running until the watchdog restarts the UI. Proposal: `T_MAN` = 90 s (≈ 580 g at
~6.5 g/s), and on start-up Meraki+ sends word 19 = 0 if it sees word 19 ≠ 0 with water bits active that it did
not start (maintainer decision, §8).

### 5.3 Presets

Water group (`hw` = autoHotWaterTarget, `hm` = hotWaterMode, [presets-spec.md](presets-spec.md) §3): stored
and applied to `config.json` as specified on every machine (they are config values), but on a non-dosing
machine the WATER page does not show them and the apply toast adds *"WATER: MANUAL (CONTROLLER < 5.1.3)"*. A
preset never changes the gate.

### 5.4 Strings (EN / DA drafts)

| Key | EN | DA |
|---|---|---|
| TX_L_WATER_MANUAL | MANUAL | MANUEL |
| TX_SN_WATER_NODOSE | CONTROLLER < 5.1.3: NO WEIGHT STOP | STYRING < 5.1.3: INGEN VÆGT-STOP |
| TX_T_WATER_LIMIT | WATER STOPPED · TIME LIMIT | VAND STOPPET · TIDSGRÆNSE |
| TX_T_WATER_OVER | WATER STOPPED · OVER TARGET | VAND STOPPET · OVER MÅL |
| TX_T_WATER_SCALE | SCALE FAULT · MANUAL WATER | VÆGT-FEJL · MANUELT VAND |
| TX_DV_MCU | CONTROLLER %s · DOSING %s | STYRING %s · DOSERING %s |

Row and toast widths must be checked against the font budget when implemented. `TX_G_WATERMAN` ("use stock
UI") goes away.

### 5.5 Simulator, selftest, E2E

* `sim/proto.py`: a `version_words("V5.1.4")` helper next to `serial_words()` that fills words 80..95; today the
  sim leaves them 0, which the new gate would read as *not dosing*. Default **`V5.1.4`** so every existing
  water scenario (`hotwater`, `stop_water`, `stop_control_auto`, `activity_takes_home`,
  `concurrent_steam_blocked`, `replay_v2`, `settings_save_matrix`) keeps its meaning unchanged.
* `sim/mcu_sim.py`: a scenario parameter `mcu_version` (string or empty) and a `legacy_word16` model for
  ≤ 5.1.2 — `ignore` or `run` (no weight stop) — since the real behaviour is unknown. Additive only (`sim/` is
  shared with the test agent).
* New scenarios: `water_gate_old` (`V5.1.2` → WATER page manual, *no* `sub9 len11` frame with word 16 = 1 on
  the wire, ever), `water_gate_empty` (no version → same), `water_manual` (sub19 start, STOP, `T_MAN` stop),
  `water_scale_fault` (dosing machine, word 35 `0x1000` → manual, auto dose stopped), `water_gate_boundary`
  (`V5.1.3` → dosing, `V5.1.2` → not).
* Selftest: a version-parse table (`"V5.1.4"`, `"5.1.4"`, `"V5.1.2"`, `"V10.0.0"`, `""`, garbage, mismatched copy)
  and a frame-policy check "no word 16 = 1 unless dosing". Previews: WATER page in both states, DIAGNOSTICS line.
* Hardware: none needed for D1 itself (the reference machine already reports `V5.1.4`); a ≤ 5.1.2 machine
  would be needed to verify the manual path on old firmware (§8).

### 5.6 Phasing and effort

| Phase | Content | Effort | Ships? |
|---|---|---|---|
| 0 | Read-only probes P9a–P9d (§9) | 0.5 h on device | no |
| 1 | Version read + gate + manual water + display-side limits + sim/selftest/E2E | 1.5–2 d | only with maintainer approval |
| 2 (optional) | Owner override, issue-template field "controller version (SYSTEM › DIAGNOSTICS)" | 0.5 d | only with approval |

Gate and manual water ship together; hiding auto must never ship before manual works.

## 6. Risks

* **Wrong version reported** (e.g. a future firmware with another format): parse failure → not dosing → manual
  water. Fail-safe, only convenience lost.
* **Old controller treats `sub19` differently than assumed:** the stock 1.1.1-69 UI sends exactly this frame to
  every ≤ 5.1.2 controller, so it is the field-proven path; still unverified on hardware by this project.
* **Gate threshold:** stock uses `> 5.1.2`, so a hypothetical 5.1.3 counts as dosing although the release note
  is on 5.1.4. Keeping stock parity is deliberate: a 5.1.3 controller behaves the same in both UIs.
* **Crash during manual water:** see §5.2 (`T_MAN`, start-up cleanup).

## 7. Unchanged

WATER SHORTCUT (stock UI only), the water temperature line (word 40), grind/brew/steam, and the 0x89 answer.

## 8. Decisions for the maintainer

1. Approve mirroring the stock gate (`> 5.1.2` from words 80..87, unknown → manual), with no owner override in
   phase 1.
2. `T_MAN` (proposal 90 s) and the over-target backstop in AUTO (target + 10 g).
3. Manual water in Meraki+ at all (required for non-dosing owners) — and whether it also replaces the
   *"use stock UI"* toast on dosing machines set to MANUAL (recommended: yes).
4. registermap.md words 80..95 were corrected in the same docs commit as this file ("UID" → firmware version).
5. Whether to ask other owners for their DIAGNOSTICS controller version (issue template) to learn which versions
   exist in the field.

## 9. Hardware probes (extends ota-plan.md P9)

All read-only, only with the machine on and the maintainer's go-ahead. P9 itself is now answered statically:
the controller reports its version in words 80..87 and Meraki+ already polls them.

| # | Probe | Answers |
|---|---|---|
| P9a | Meraki+ UI log / a passive tap of one `0x83 sub56 len56` reply: words 80..95 | confirms `V5.1.4` + identical copy on the running controller (already seen in the 2026-11-16 capture) |
| P9b | `ls -la /customer/ \| grep -i -E 'mcu\|tmp'; md5sum /customer/mcu.bin /customer/mcu_tmp.bin 2>&1` | for the record only (§3.5) |
| P9c | `grep -o -E '"[A-Za-z]*([Vv]ersion\|[Mm]cu)[A-Za-z]*":[^,]*' /customer/config.json` (never print the whole file) | confirms no controller-version key in config (static analysis says none) |
| P9d | pull `/new_customer/merakiplus/boot/demo_v9.vendor` (md5 `3b63e13f…`) to the dev workspace (never into this repo) | the previous stock display's hot water flow and version floors |

## 10. Decisions (resolved by the maintainer)

Binding record (2026-11-22). Where a decision differs from the proposal in §5–§8, the decision wins.

1. **Copy the stock rule, no owner override.** Dosing only when words 80..87 parse as a version **> 5.1.2** AND the
   copy in words 88..95 matches AND there is no scale fault (word 35 & `0x1400`). Everything else — empty string, not
   polled yet, parse failure, copy mismatch, ≤ 5.1.2 — is **manual only** (fail-safe). (§8 item 1.)
2. **No extra safety limits — match stock exactly.** No 90 s manual time limit (`T_MAN` dropped), no display-side AUTO
   stop at target + 10 g, no start-up cleanup of a foreign word 19. The controller's own weight stop governs AUTO;
   MANUAL runs until STOP, as in the stock UI. (§8 item 2.)
3. **Manual water start/stop ships in the same release** and replaces the *"use stock UI"* toast on dosing machines set
   to WATER MODE = MANUAL. (§8 item 3.)
4. (§8 item 4: done in commit `fb55491`.)
5. **No survey of other owners.** (§8 item 5.) The README asks for the *DIAGNOSTICS → CONTROLLER* version in issues
   instead, without a template field.
6. **Release v6.22 is pre-approved for exactly this scope** once all tests are green. **Install on the reference machine
   is not approved in this round** — it waits for the next hardware session (§11.5).

## 11. Implementation (v6.22)

### 11.1 What changed in the UI (`src/merakiplus.c`)

* **Version read** (`mcu_ver_read`, `ver_parse`): words 80..95 as ASCII, lo byte first; at most 6 letters, then
  exactly `N.N.N` (1–4 digits each), then the NUL; the copy must match all 16 bytes. Stricter than stock's
  `compare_versions` (which skips up to 6 *non-digits* and compares an empty string as equal) — a stricter parser can only
  turn a dosing controller into a manual one, never the reverse. **No new UART traffic**: the words are in the existing
  sub56 len56 poll; the UI never writes words 80..95.
* **Gate** (`wd_dosing()` = version > 5.1.2 && no scale fault), evaluated every frame from the register mirror (so a
  controller restart or a scale fault switches it at runtime). Not dosing →
  the WATER page has no rows (HOT WATER and WATER MODE hidden), HOME shows *MAN.* under the water icon, a start is the
  manual frame, and **`frame_policy` rejects any frame with word 16 ≠ 0** (the last line of defence, also against the
  AUTO block with its own intent).
* **Manual water** (`act_push(ACT_WATER)` when `water_manual()` = not dosing or WATER MODE = MANUAL): `cmd6 sub19 len1`
  word 19 = 1 (`aa 55 06 13 01 01 00 c1 db`) — the stock legacy water page's frame (`set_manual_hot_water_switch`
  `0x105118`, `usart_send(6,19,1)`, verified in `demo_v9` 1.1.1-69). New intent `I_WATERMAN` (word 19 is now a start
  word: without the intent it is rejected). **Stop:** on a controller that is not known to be > 5.1.2: word 19 = 0 only
  (`aa 55 06 13 01 00 00 c0 4b`); on a dosing controller unchanged since v6.13 (word 16 = 0, + word 19 = 0 when set).
  No timeout, no display-side weight stop (decision 2). `act_guard`'s *WATER = MANUAL · USE STOCK UI* is gone.
* **Scale fault** (`wd_step`): rising edge of word 35 & `0x1400` → a pending water start is dropped; on a > 5.1.2
  controller whose word 16 is still ≠ 0, word 16 = 0 is sent once + toast *SCALE FAULT · WATER STOPPED* (stock
  `hotWaterUpdate` `0x115B80..0x115BD8`). A manual run is not stopped (stock neither). HOME's error text for these bits is
  now *SCALE FAULT* (code E2; v6.21: *UNKNOWN FAULT*, code `?`).
* **DIAGNOSTICS › CONTROLLER** (a new block above ABOUT): *FIRMWARE* (the string as reported, upper case; *NOT
  REPORTED*; amber *COPY DIFFERS* / unparsable) and *HOT WATER* (*AUTO + MANUAL* / *MANUAL ONLY* / *MANUAL (SCALE)*).
* **config.json is never written by the gate**: `hotWaterMode = 1` on an old controller is simply not used (like stock);
  the selftest checks md5 + write counter. The 0x89 answer stays the stock build string `"1.1.1-69"` (§1.5) — unchanged.
* **Presets** (not implemented yet, [presets-spec.md](presets-spec.md)): the water group stays a config group on every
  machine; applying it never changes the gate (§5.3 stands).

Strings (EN / DA): see [ui-guide.md §21](ui-guide.md#21-hot-water-dosing-gate--manual-water-v622); the drafts of §5.4
were shortened to fit the pixel budgets (`TX_T_WATER_LIMIT` / `TX_T_WATER_OVER` dropped with decision 2; the icon
label is *MAN.* — *MANUAL* is 63 px for a 52 px slot).

### 11.2 Simulator (`sim/`, additive)

* Words 80..95 were already `"V5.1.4"` + copy in `SENSOR_BASELINE` (§5.5's "the sim leaves them 0" was wrong), so every
  existing water scenario keeps its meaning.
* `mcu_sim.py`: event `mcu_version` (`version`, `copy` — mismatch, `null` = leave 88..95; survives a reset); a write of
  word 16 = 1 while the reported version is not > 5.1.2 is logged `legacy_word16`; **word 19 alone starts manual water**
  (as hotwater-ctl `0x1B8C`: `active = word19 | word16`, no word 9), manual has no weight stop and no timeout; a scale
  fault alone stops AUTO (word 16 = 0) but not MANUAL. `sim/selftest.py` T16 (9 checks) → 204/0.
* State JSON `water` (`mcu_ver`, `ver_state`, `ver_dosing`, `dosing`, `scale_fault`, `start`, `run_manual`, `rows`,
  `fault_n`, `fault_stops`, `diag_ver`, `diag_wd`); [sim/CONTRACT.md](../sim/CONTRACT.md) addendum v6.22.
* Scenarios (all `ui_only`): `dosing_version_detect` (28 checks), `dosing_gated_old_mcu` (44), `dosing_scale_fault`
  (34). All three fail against the v6.21 SIM build (20/28, 20/44, 20/34).

### 11.3 Tests

UI selftest **582 checks** EN + DA and on ARMv7 (new section *v6.22 water dosing gate*, 13 checks: parser table, gate
table incl. copy/empty/not-polled, manual frames byte for byte, policy rejects, the HOLD TO START → run → STOP path with
120 s of silence, config untouched, MANUAL mode on V5.1.4, scale-fault stop once, DIAGNOSTICS texts, pixel budgets),
sim selftest 204/0, full E2E 51/51 EN + 51/51 DA, quick E2E 5/5, installer suite 140/140, launcher tests, switcher 55/0,
hygiene + bin/ drift OK; all built with the official zig 0.17.1 ([CHANGELOG](../CHANGELOG.md) entry for 6.22).

### 11.4 Deviations and open points

* **Weight stop threshold unresolved:** the controller code stops at word 39 > target + 1.5 g (`0x1C64..0x1C84`), the
  2026-11-16 capture saw 40.5 / 41.0 g for a 40.0 g target (§1.1). It is controller-side; per decision 2 Meraki+ adds no
  display-side stop, so the difference does not affect the UI.
* **The manual frame on ≤ 5.1.2 is not hardware-tested.** It is the stock 1.1.1-69 legacy page's frame (the field-proven
  path for every older controller), but no ≤ 5.1.2 controller was available. Risk: if an old controller treated a lone
  word 19 differently, manual water would not start (fail-safe: nothing runs) — STOP (word 19 = 0) is the stock stop.
* **Manual on a 5.1.4 controller uses `sub19 len1`, not stock's new-panel frame.** The 1.1.1-69 hot water panel starts
  MANUAL on a > 5.1.2 controller with the whole `sub9 len11` block (word 9 = 1, word 16 = 0, word 19 = 1,
  `0x113D94`). Meraki+ sends the legacy page's `sub19 len1` on every controller (the decision's "same frames stock sends
  to older controllers"). By the controller code (`active = word19 | word16`, recomputed every 10 ms cycle) both start
  the same; **to verify on the 5.1.4 machine** (§11.5).
* **Scale fault blocks every start**, including manual: the error words (34/35/36) block starts since v6.0, and v6.22
  keeps that (the stock legacy page would still offer manual water). Fail-safe; the page says *SCALE FAULT: NO WEIGHT
  STOP* and the live line *ERROR*.
* **Stricter parser than stock** (letters-only prefix, exactly three parts, copy must match): a hypothetical
  `"V5.1.4a"` or a 4-part version would be manual-only in Meraki+ but dosing in the stock UI. Deliberate (decision 1:
  fail-safe).

### 11.5 Next hardware session (not done in this round)

1. **Install v6.22** — needs separate approval (this round's approval covers the release only).
2. **P9a** words 80..95 live (Meraki+ log or a passive tap of one sub56 reply): `V5.1.4` + identical copy; check
   *DIAGNOSTICS → CONTROLLER* shows *V5.1.4 · AUTO + MANUAL*.
3. **P9b** `md5sum /customer/mcu.bin /customer/mcu_tmp.bin` for the record (§3.5).
4. **P9c** the sanitized config grep (never print the whole file) — no controller-version key.
5. **P9d** pull the older stock app from slot B (`boot/demo_v9.vendor`) to the dev workspace, never into this repo.
6. **Manual water live on the 5.1.4 machine:** WATER MODE = MANUAL → HOLD TO START → `sub19 len1 [1]` on the wire,
   work_sta `0x030B`, water runs past the HOT WATER weight, no stop from the display for > 90 s, STOP WATER → word 16 = 0
   + word 19 = 0 → bits cleared. AUTO still stops by weight with no frame from the display.
7. The OTA phase 1b items already pending (ordinary update, flag-cleared rollback / power-cut tests).
