# Meraki+ — UI guide

This guide is extracted from the full UI specification ([dev/UI_v6.md](dev/UI_v6.md)) and describes the
pages, touch zones and settings in **Meraki+ v6.18.1**. For a short user guide, see
[README → Usage](../README.md#usage). Addresses in square brackets (e.g. `[0x12bcfc]`) refer to the
place in the original UI program where the limit was read (see [dev/PARITY.md](dev/PARITY.md)).

The UI is available in **English and Danish** (*gear → SYSTEM → DISPLAY · SOUND → UI LANGUAGE*). This guide uses the English
labels; where a Danish label is useful for cross-reference with the developer notes, it is given in
parentheses.

## 3. Page map and touch

**v6.17** replaced the five-page carousel (HOME · BREW · STEAM·WATER · SYSTEM · DIAGNOSTICS, changed by
swiping) with one root and sheets — the reasoning is in [ux-audit-v617.md](ux-audit-v617.md):

```
HOME ─ icon, tap ─► GRIND / BREW / WATER / STEAM   (live line · that feature's settings · HOLD TO START)
     ─ icon, hold ► starts the activity (650 ms)
     ─ gear ──────► SYSTEM ─ DISPLAY · SOUND · CLOCK · SCHEDULE · STOCK UI (sections, one open at a time)
     │                     └ DIAGNOSTICS ─► DIAGNOSTICS
     └ error / offline hint, tap ─► DIAGNOSTICS
```

**Open** by tapping the thing; **go back** with the chevron at the top of every page and editor (the
whole header is the target), or swipe right. There are no page dots, no edge zones and no page swipes.

| Page | Opened by | Contents |
|---|---|---|
| **HOME** | — (root) | clock (or the wordmark while the clock is not set) · phase chip · gauge · hero number · sub-line · chart (40 s) / one hint line · 3 stats · the 4 activity icons **with their targets underneath** (grind dose, brew weight, water amount, milk temperature) — or the STOP button(s) while something runs · the gear |
| **GRIND** | tap the bean icon | live line *ON THE SCALE 0.0 G* · GRIND DOSE, GRIND MODE · **GRIND SIZE** ruler (v6.25): the grinder's measured grind size (word 37, read-only) on a *COARSE … FINE* scale 1..30 (mirrored like the physical dial on the bean container: FINE / FIN right, COARSE / GROV left), or 0..40 when the machine's gear-40 point is calibrated (word 75 ≠ 0xFFFF and word 75 − word 65 ≥ 100); values outside the scale pin to its ends, the tab still shows the number; *--* and no marker without a connection · HOLD TO START |
| **BREW** | tap the cup icon | live line *BOILER 92.4° · GROUP 70°* · BREW WEIGHT, BREW TEMP, PRE-INFUSION, PRE-INF. TIME, PRE-INF. PRESSURE, GROUP HEAD (4 in view, the list scrolls) · HOLD TO START |
| **WATER** | tap the drop icon | live line *WATER 99.9°* (word 40 — the "water sensor") · HOT WATER, WATER MODE · HOLD TO START. **v6.22:** on a controller that cannot dose (≤ 5.1.2, version unknown, scale fault) no rows — *MANUAL HOT WATER · RUNS UNTIL YOU TAP STOP* + the reason (§21) |
| **STEAM** | tap the steam icon | live line *BOILER 130° · MILK 22.6°* · MILK TEMP, STEAM LEVEL · HOLD TO START |
| **SYSTEM** | the gear | *MERAKI+ V6.18.1* · four section headers with a summary (**DISPLAY · SOUND** *ENGLISH* · **CLOCK** *16:35* · **SCHEDULE** *06:30 / OFF* · **STOCK UI**) + the **DIAGNOSTICS** row (*OK* / the error code / *DOWN*). Closed: 5 rows, no scrolling. A tap on a header opens it (and closes the other) |
| **DIAGNOSTICS** | SYSTEM's last row, or a tap on HOME's error / offline hint | STATUS, BOOT · SLOT · SWITCHER, NET, GRAPH (smooth / raw), PRESSURE · ESTIMATE, RAW COUNTERS, POWER BUTTON, TIME · SCHEDULE, SOUND (v6.18), ABOUT — scrolls vertically. Its chevron returns to where it was opened from |

A feature page's live line turns **amber and names the reason** when a start is not possible (*NO
CONNECTION*, *ERROR ACTIVE · START BLOCKED*, *GRIND = TIME · USE STOCK UI*, *CONFIG OUT OF SPEC · FIX
FIRST*, …); HOLD TO START is then dimmed. **While an activity runs, no feature page is shown**: started
from the page, on the machine or by the wand, the UI is on HOME, where the STOP button is. SYSTEM,
DIAGNOSTICS and an open editor stay.

Modal views: **splash** (boot), **stepper detail** (from a row) and — from v6.2 — the **system
action** (BOOT UI, switch confirmation, exit reminder; same geometry and button zones as the stepper
detail) as well as the last frame before a runtime switch. From v6.17 the stepper detail has no
footer lines; its status line shows *CHANGED · WAS …*, *SAVING…*, the zone's place names, or — on an
untouched editor with ≥ 20 steps — *HOLD - OR + TO GO FAST*.

**HOME per phase** — the hero number and the gauge change meaning, so the most important value is
always the largest:

| Phase | Hero | Gauge | Stats |
|---|---|---|---|
| READY / HEATING UP / ASLEEP / ERROR | brew boiler temp (word 40), label *BREW BOILER*, sub-line *TARGET 87.0°* | 20–120 °C, marker = brew setpoint | group (42), steam (47), weight (39) |
| GRINDING | dose weight (word 38) | weight / target, marker = target | time, current (58), grinder step (37) |
| PRE-INFUSION / EXTRACTION | weight in the cup (word 39), label *IN THE CUP*, sub-line *STOPS AT 36.0 G* (v6.17: the target is stated once) | **raw pressure** (word 51, 0–1600) + inner ring = weight/target | time, ≈bar, flow (52) |
| HOT WATER | weight (39) | weight / target | time, flow, boiler |
| STEAM | milk temp (word 48) | 20–80 °C, marker = target | time, boiler (word 47), level |
| DONE (8 s) | final value | as for the activity | "DONE IN x S" |

**Pressure labelling (v6.10; v6.11: shorter texts; v6.17: `≈BAR`):** the gauge still shows the raw scale. The bar
value is labelled `≈BAR` (v6.11–v6.16: `≈BAR EST.`; v6.10: `≈BAR · EST.`) and the chart `PRESSURE` (v6.10:
`PRESS. ≈BAR · EST.`; the estimate is in the field right below). The conversion is a single linear
anchor: the pre-infusion plateau in the recording (358 raw) = config `preInfusionPressure` (4 bar),
i.e. k = 4/358 = 0.01117 bar per raw unit. Above 20 bar no number is shown (*PULSE*). Basis and
caveats: [calibration.md §16](calibration.md#16-pressure-estimate-v610). (Up to and including v6.9:
`≈BAR*` / `PRESSURE* UNCALIBRATED` with the hypothesis "1346 raw ≈ 9 bar".)

**Touch zones** (for testing; pixel coordinates):

| Element | Zone | Gesture |
|---|---|---|
| GRIND / BREW / WATER / STEAM (HOME, idle) | circles r=30 around (150 / 210 / 270 / 330, 372); the target text is at y 405 | **tap** = open the feature's page · **hold ≥ 650 ms** = start (a ring fills around the icon) |
| STOP (HOME, while something runs) | one button: 124..356 × 342..402 (232 × 60 px; pill 216 × 54) · two buttons (main activity + latched steam): 80..240 and 240..400 × 342..402 (pills 152 × 54) | tap |
| Gear (HOME) | 208..271 × 426..471 (64 × 46 px) | tap = SYSTEM (no frame) |
| Error / offline hint (HOME, only in those two states) | 88..392 × 236..294 | tap = DIAGNOSTICS |
| The chart / the BAR field (v6.10) | the chart 118..362 × 240..294 when it shows pressure; the BAR field 193..287 × 298..338 during brewing | tap = toast with the basis (*PRE-INF. 4 BAR = RAW 358 · LINEAR*) — no frames |
| **Page header** (every page but HOME) | 160..319 × 14..79 (160 × 66 px: chevron + title) | tap = back · pull down ≥ 70 px = back |
| Swipe right (every page and editor but HOME) | anywhere, not started on −/+; ≥ 60 px, started horizontally (axis lock in the first 14 px), ≤ ~30° skew | back. A swipe left does nothing; HOME reacts to no swipe |
| List row k | x 64..416, y center 121 + 58·k − scroll (feature pages: the list ends at y 342, SYSTEM at 394) | tap = the editor / open-close the section / the system action / DIAGNOSTICS; vertical drag scrolls |
| HOLD TO START (feature pages) | 116..363 × 344..403 (248 × 60 px; pill 232 × 48) | **hold ≥ 650 ms** = start (the pill fills, the UI returns to HOME) · a short tap = toast *KEEP HOLDING TO START* or the reason it is blocked |
| BOOT UI / SWITCH TO ORIGINAL NOW | the first two rows of SYSTEM › STOCK UI | tap opens the system action |
| DIAGNOSTICS | the whole field y 86..394; the GRAPH pill 90..390 × 44 px high | vertical drag scrolls; tap on the pill = smooth / raw |
| Close chevron (detail + system view) | 196..283 × 34..79 (88 × 46 px) | tap = CANCEL / BACK |
| Swipe down (detail + system view) | anywhere, not started on −/+ | ≥ 70 px vertical, ≤ ~30° skew = CANCEL / BACK |
| − / + | circles r=42 around (84,184) / (396,184) | tap = one step; hold = repeat (170 → 90 → 45 ms, ×5 steps after ~28 repeats) |
| CANCEL / SAVE | 100..236 / 244..380 × 284..348 | tap |
| BACK (locked row / sensor) | 170..310 × 284..348 | tap; −/+ do not exist |

---

## 4. Settings table (what is actually implemented)

One table (`SET[]`) is the single source of truth for label, config field, word, limits, step and
status. All numbers are **raw config units** and inclusive. The **Status** column: **PROVEN** =
range and step are address-proven in PARITY.md §2 · **ESTIMATE** = min and step are proven, max is an
estimate · **LOCKED** = shown without an editor · **SENSOR** = read-only measured value.

The tables below keep the grouping of the spec (tier 1 espresso, tier 1 steam/water, config-only). **Where
a row is shown since v6.17:** GRIND DOSE, GRIND MODE → **GRIND** · BREW WEIGHT, BREW TEMP, PRE-INFUSION,
PRE-INF. TIME, PRE-INF. PRESSURE, GROUP HEAD → **BREW** · HOT WATER, WATER MODE → **WATER** (the WATER SENSOR
is that page's live line, not a row) · MILK TEMP, STEAM LEVEL → **STEAM** · UI LANGUAGE, BRIGHTNESS, TEMP UNIT,
SOUND, READY TONE, VOLUME → **SYSTEM › DISPLAY · SOUND** (v6.18: + the two sound rows; VOLUME moved here from STOCK
UI because it now sets Meraki+'s sounds too) · TIME FORMAT, DAYLIGHT SAVING, TIME ZONE → **SYSTEM › CLOCK** ·
PRE-HEAT, PRE-HEAT AT, REPEAT, AUTO OFF → **SYSTEM › SCHEDULE** · BOOT UI, SWITCH TO ORIGINAL NOW, STOCK LANG.,
WATER SHORTCUT, CLEANING REMINDER, AUTO TIME, PAUSE TIME → **SYSTEM › STOCK UI** (what only the stock UI reads).

### 4.1 Espresso (spec tier 1) — the GRIND and BREW pages
| Label | config field | Word | Write on SAVE | Min..max / step | Shown as | Status |
|---|---|---|---|---|---|---|
| GRIND DOSE | `autoGrindWeight` | 4 | sub4 len1 | 10..309 / 1 | 1.0–30.9 g | **PROVEN** [0xf4984] |
| GRIND MODE | `grindMode` | 2 | sub2 len1 | 1..2 | 1 WEIGHT / 2 TIME | **PROVEN** [0x122180] |
| BREW WEIGHT | `autobrewWeight` | 23 | sub23 len1 | 100..990 / 10 | 10–99 g | **PROVEN** [0x12be5c] |
| BREW TEMP | `brewTemper` | 11 | sub11 len1 | 850..960 / 10 | 85.0–96.0 °C | **PROVEN** [0x12bcfc] |
| PRE-INFUSION | `preInfusionSwitch` | 112 high byte | sub112 len2 | 0..1 | OFF / ON | **PROVEN** [0x103858] |
| PRE-INF. TIME | `preInfusionTime` | 113 high byte | sub113 len1 | 5..15 / 1 (ceiling 60) | 5–15 s | **ESTIMATE** (max) |
| PRE-INF. PRESSURE | `preInfusionPressure` | 112 low byte | sub112 len2 | 3..4 / 1 | 3–4 bar | **PROVEN** (MCU 0x1E10 only knows 3 and 4) |
| PAUSE TIME | `breakTime` | 113 low byte | — (no editor) | — | 5 s | **LOCKED** (no effect, §4.5) |
| GROUP HEAD | `headTemper` | 114 | sub114 len1 | 30..80 / 10 (ceiling 100) | 30–80 °C | **ESTIMATE** (max) |

### 4.2 Steam and hot water (spec tier 1) — the STEAM and WATER pages
| Label | config field | Word | Write on SAVE | Min..max / step | Shown as | Status |
|---|---|---|---|---|---|---|
| MILK TEMP | `AtuoSteamTarge` | 25 | sub25 len1 | 450..700 / 10 | 45.0–70.0 °C | **PROVEN** [0x12bf80] |
| STEAM LEVEL | `steamGears` | 14 | sub14 len1 | 1..4 / 1 | GENTLE / MILD / MEDIUM / STRONG | **PROVEN** [0xdd028] |
| HOT WATER | `autoHotWaterTarget` | 17 | sub17 len1 | 300..5000 / 100 | 30–500 g in steps of 10 g | **PROVEN** [0xfbf0c] |
| WATER MODE | `hotWaterMode` | (16/19 in the start block) | — config only | 1..2 | 1 AUTO / 2 MANUAL (v6.22: MANUAL starts manual water; before: "use stock UI") | **PROVEN** [0x11344c] |
| WATER SENSOR | *(none)* | 40 (read-only) | — no write exists | — | live, e.g. 99.9 °C | **SENSOR** [0x112c9c] |

### 4.3 SYSTEM (config only — no MCU write)
| Label | config field | Min..max | Shown as | Spec tier | Status |
|---|---|---|---|---|---|
| **UI LANGUAGE** (v6.3) | *(none — its own file `/new_customer/merakiplus/lang`)* | 0..1 | ENGLISH / DANSK (endonyms) | — | merakiplus's own setting; **not** a spec parameter and not in `--dump-table` (CHANGES-v6.3 item 2) |
| BRIGHTNESS | `backlight` | 1..10 | live preview (PWM `duty = 100 − 7·v`) | 2 | **PROVEN** 0..10; **0 deliberately left out** (duty 100 = a dark screen the user cannot find their way back from) |
| VOLUME | `volume` | 0..100 | 0–100 | 2 | **PROVEN** for the slider; UI hint about the discrete selector (§4.4). v6.18: also Meraki+'s sound volume, capped at 70 like stock (0 = mute) — [audio.md](audio.md) |
| **SOUND** (v6.18) | *(none — its own file `/new_customer/merakiplus/sound`, `sound=`)* | 0..2 | OFF / ON / STOCK SOUNDS | — | merakiplus's own setting: the start jingle + ready tones; STOCK SOUNDS = the display's own `/customer/audio/*.mp3` if present |
| **READY TONE** (v6.18) | *(none — the same file, `ready=`)* | 0..3 | OFF / BREW BOILER / STEAM BOILER / BOTH BOILERS | — | merakiplus's own setting: which boiler announces itself (its own tone each) |
| TEMP UNIT | `Temperature` | 0..1 | 0 °F / 1 °C — also controls merakiplus's display | 2 | **PROVEN** |
| STOCK LANG. (before v6.3: LANGUAGE) | `language` | 0..10 | 11 languages, Danish not among them (the names are shown in the UI language) | 2 | **PROVEN** — unlocked in v6.1; only controls the stock UI |
| WATER SHORTCUT | `hotWaterShortcut` | 0..1 | OFF / ON | 2 | **PROVEN** |
| DAYLIGHT SAVING | `sumtime` | 0..1 | OFF / ON | 2 | **PROVEN** |
| CLEANING REMINDER | `clearTip` | 0..1 | OFF / ON | 2 | **PROVEN** |
| TIME FORMAT | `timemode` | 0..1 | **0 = 12-HOUR / 1 = 24-HOUR** (corrected in v6.16) | 3 | **PROVEN** [`update_time` 0xD5944: `model[0x59]` is XORed with 1 at 0xD5970] |
| AUTO TIME | `Automatically` | 0..1 | OFF / ON | 3 | **PROVEN** |
| TIME ZONE | `timeZone` | — | `UTC+01:00` + place names (v6.16) | 3 | **LOCKED** (set in the stock UI) |
| PRE-HEAT · PRE-HEAT AT · REPEAT (v6.16) | *(none — `/new_customer/merakiplus/schedule`)* | off/on · 00:00–23:55 · once / every day / Mon–Fri / Sat–Sun | the next occurrence is sent as the controller's wake alarm | — | Meraki+'s own ([time-scheduling.md](time-scheduling.md)) |
| AUTO OFF (v6.16) | *(same file)* | off / 15 / 20 / 30 / 60 min (default 30 since v6.23) | power-off after that long without touch or activity | — | Meraki+'s own (stock: fixed 30 min); no file / no valid value → 30, explicit `autooff=0` stays off ([time-scheduling.md §5.1](time-scheduling.md#51-default-and-migration-v623)) |

### 4.4 What is still estimated or locked
| Row | What is uncertain | How the UI shows it | Why it is justifiable |
|---|---|---|---|
| PRE-INF. TIME | the picker's number of options has not been mapped (PARITY §6.6); max 15 = the stock UI's own default | **`MAX = ESTIMATE`** in amber next to the range + `≈` in front of the value in the list | 5–15 s is harmless; the field is one byte |
| GROUP HEAD | the same picker anomaly; the spec's max of 80 is itself a cautious estimate | **`MAX = ESTIMATE`** + `≈` | E1 only triggers at > 110 °C; 80 is far below |
| VOLUME | the stock UI has two controls: a 0–100 slider (what merakiplus edits) and a discrete High/Medium/Low/Mute selector whose mapping has not been mapped | **`STEPS = ESTIMATE`** + `≈` (the note says *"MERAKI+ + STOCK UI SOUNDS"*) | config only, no MCU write |
| PAUSE TIME | max unknown — and the value has no effect | locked, amber note *"NO EFFECT · PRE-INF. TIME IS USED"* | §4.5 |
| TIME ZONE | the size and meaning of the index table | locked, *"SET IT IN THE STOCK UI"* | an invalid index could break the stock UI's time/DST logic |

**The estimate rule:** if the stock UI itself has saved a value above my estimated max (e.g.
`headTemper=90`), that is evidence that the picker reaches it, and the range is extended to that value
— but never above the ceiling (60 s / 100 °C). **Proven** limits are never extended.

**Verify online** (§11 M): count the picker's options for pre-inf. time/pressure/pause and group temp
in the stock UI, and correct `max` in the two SF_EST rows.

### 4.5 Deliberately removed, locked or not exposed
| Field | Decision | Rationale (spec) |
|---|---|---|
| hot water **temperature** | **live line on the WATER page** (v6.1–v6.16: a read-only sensor row), not a setting | The stock UI has no setpoint: the temperature shown is the e-boiler's measured value (word 40, `hotWaterTemperUpdate` 0x112c9c). It is shown where you would look for it — at the top of the WATER page |
| `breakTime` | **locked**, the value is shown | The MCU recipe 0x1E10 copies `preInfusionTime` into the low byte of word113 before the timing `word13 + word113_lo` is computed — the separate pause value is always replaced. An editor would be a button with no effect. The value is still packed into word113 lo, so the frames are identical to the stock UI's |
| `brewMode` | **removed** | vestigial: `get/setExtractModeValue` has 0 callers |
| `sleepMode`, `sleepModeSetting` | **removed** | no editor in the stock UI (the setters have 0 callers); sleep timing is hard-coded (~600 s screensaver / ~1800 s power-off). merakiplus keeps the machine awake itself (§7.1) |
| `theme` | not exposed | the field does not exist in `config.json`, and `getTheme` has **0 callers** in `demo_v9` (`grep -c '<getTheme>' demo_v9.dis` = 1, only the definition) — a saved value would never be read. In addition, merakiplus never adds keys |
| `presetStore` / NFC | deferred | §9 |
| Wi-Fi (`ssid`, `password`, `wifionoff`), `init`, `patch`, version fields | not exposed | must not be touched / not user settings |
| calibration (words 26/27), reboot (word 30), drain (28/29), back-flush (20), self-test (8) | not exposed **and** impossible to send | §7a |

### 4.6 Range guard (three layers)
1. **The stepper** clamps to the range and snaps to the step grid.
2. **SAVE** rejects anything outside the range; an activity start is blocked if a config value used in
   a cmd6 frame is out of spec or missing (*"CONFIG OUT OF SPEC · FIX FIRST"*).
3. **The frame policy** (§7a) rejects any frame with a word outside its range — even if layers 1–2
   should have a bug.

A config value outside a proven range can only occur if the file was changed outside the stock UI. It
is shown as it is, in red (a list row with a red tint, the detail with *"OUT OF SPEC · PICK A NEW
VALUE"*), and the first tap on −/+ brings it into range.

---

## 4a. Quirks from the spec — and what merakiplus does about them

| # | Quirk (PARITY) | Consequence in merakiplus |
|---|---|---|
| 1 | Hot water has no temperature setting; the display is word 40 | sensor row (§4.5) |
| 2 | `brewMode` vestigial | removed |
| 3 | `sleepMode`/`sleepModeSetting` without an editor, timing hard-coded | removed |
| 4 | word112 = `(switch<<8)\|pressure`, word113 = `(time<<8)\|pause`, word114 = `headTemper` in whole °C (MCU ×10) | `pack112`/`pack113` match exactly — tested for all combinations + 5 byte vectors (§10) |
| 5 | MCU 0x1E10 only handles pressures 3 and 4; other values are ignored | range 3..4; the frame policy rejects word112 with any other pressure |
| 6 | word13 = 60 at boot, overwritten by 0x1E10 (340/440 at 120 V, word15 = 300/350 at 220 V) | merakiplus sends word13/15 = 60 as observed and does not otherwise touch them; the policy rejects other values |
| 7 | `breakTime` is overwritten by the MCU | locked (§4.5). Before BREW, word113 is only compared on the **high byte**, otherwise the mirror (e.g. `0606` after a brew) would trigger an unnecessary re-sync every time |
| 8 | A boot push exists: `fuart_thread` sends `sub1 len30` + `sub112 len3` | the reload semantics in §5 have been corrected |
| 9 | Trigger words 3/6/9/26/27/30 must be 0 in normal blocks; word30 = 1 reboots the MCU | §7a |
| 10 | ~~Steam requires the physical wand switch~~ — **disproven live** (STEAMLIVE §6.3): display steam starts 0.22 s after the write with the wand switch = 0. The wand switch alone gives a *flush* without auto-stop | **v6.6:** *"STARTING STEAM…"*, then *STEAM·AUTO*; the wand alone = *STEAM·WAND* without a STOP button ([CHANGES-v6.6](dev/UI_v6.md#changes-v66) item 2) |
| 11 | Picker navigation is anomalous → number of options unknown | SF_EST + a visible hint (§4.4) |
| 12 | Volume: slider + discrete selector | hint (§4.4) |

**Two findings beyond the spec** (both verifiable in `demo_v9.dis`):
* **`timemode`: 1 = 24 h, 0 = 12 h** (corrected in v6.16 — the v6.1 reading "0 = 24 h" missed an `eor`).
  `model_data_init` stores `timemode != 0` in model+0x59, and `update_time` [0xD5944] XORs that byte with 1
  (0xD5970) before the test: `timemode = 1` → `"%02d:%02d"`, `0` → `A.M./P.M.`. The stock switch is labelled
  "24-Hour Format" and is ON for 1. The factory config has 1 = a 24-hour clock. See [time-scheduling.md](time-scheduling.md).
* **`sumtime` is daylight saving time**, not "timer display" as the spec's label guesses: the accessor
  is called `getSumerTimeSwitchValue` [0xd2804]. v6's label DAYLIGHT SAVING (SOMMERTID) is kept.

---

## 18. Manual STOP (v6.13)

**The bug (user report):** hot water could not be stopped from the Meraki+ screen. Only steam had a
STOP button; grind, brew and water only stopped at the target weight. v6.13 gives all four activities
a STOP button. Auto-stop is unchanged — STOP is an addition.

### 18.1 Stop frames — byte for byte, with evidence

All three stop words were found in the MCU firmware (`MEM03_V514_260923.bin`, md5 `e96155da…`,
disassembly in `mcu_analysis/`) and cross-checked against the stock UI's own stop calls (`demo_v9`,
symbols preserved, `demo_v9.dis`). The pattern is the same for all three: **the MCU's own auto-stop
writes exactly that word to 0**, and a manual write of the same word therefore hits exactly the same
code path.

| Activity | Stop frame (cmd6, CRC-16/MODBUS LE) | MCU firmware: what controls the activity | The MCU's own auto-stop | The stock UI's stop call | Bits cleared |
|---|---|---|---|---|---|
| **WATER** (auto, merakiplus's start) | `aa 55 06 10 01 00 00 c0 0f` — sub16 len1, word 16 = 0 | hot water ctl `0x1B8C`: `active = (word19 \| word16) != 0`, recomputed every round (`0x1C44..0x1C4E`); active → `work_sta \|= 0x20A` (`0x1C8A`) | weight stop `0x1C80`: writes word 16 = 0 | `hotWaterStartStop` `0x11468C`: mirror word 16 = 0, `usart_send(6,16,1)` | bit 3 always (`0x1C9A`); bits 1+9 when the boiler refill is idle (`[0x2000002B] == 9`, `0x1CA2`) |
| WATER (manual mode) | `aa 55 06 13 01 00 00 c0 4b` — sub19 len1, word 19 = 0 | the same (`word19 \| word16`) | — | `hotWaterStartStop` `0x114720`: `usart_send(6,19,1)` | as above |
| **BREW** | `aa 55 06 16 01 00 00 c0 87` — sub22 len1, word 22 = 0 | brew ctl `0x1E9C`: on every **change** of word 22, `running = (word22 != 0)` (`0x1F94..0x1FC6`) | weight stop `0x1F92`: writes word 22 = 0 | home `onClick` `0x109694`: mirror word 22 flipped to 0, `usart_send(6,5,18)` (the whole block) | bits 2+12 in the 1st round (`0x20C4`, `& 0xEFFB`), valve pulse on bit 2 after 10 rounds (`0x20D2`); bits 1+9 when the refill is idle (`0x20F0`) |
| **GRIND** | `aa 55 06 06 01 00 00 c4 47` — sub6 len1, word 6 = 0 | grind ctl `0x19C6`: the grinder runs while word 6 ≠ 0 (`0x1AA2`: `work_sta \|= 0x40`, otherwise `&= ~0x40`) | weight `0x1A68`, timeout `0x1A72`, error `0x1A3E`/`0x1A8C`: all `word 6 = 0` | home `onClick` `0x10928C`: mirror word 6 = 0, `usart_send(6,6,1)` (the start branch `0x109278` sends `sub3 len4`) | bit 6 |
| STEAM (unchanged since v6) | `aa 55 06 18 08 00 00 8a 02 00 … 00 0c f7` — sub24 len8, word 24 = 0 | steam ctl `0x210E` | word 48 ≥ word 25 | (SESSION.md §10.3) | bits 5+14 |

**Brew — why only word 22 and not the stock UI's whole block?** The stock UI sends `sub5 len18` with
the mirror's words 5..22, where only word 22 has changed; the other 17 words are the MCU's own polled
values. cmd6 is `memcpy` + ACK (`0x99C6`, no sub-specific logic), so for the MCU the only thing that
happens is that word 22 goes 1 → 0 — exactly what merakiplus's `sub22 len1` does. The narrow frame cannot
overwrite a recipe value with a stale mirror.

**Words 9 and 3 are not stop words.** Word 9 (brew/water) and word 3 (grind) are tare triggers that
the MCU consumes and resets within 10 ms of the start (`0x1ED2`, `0x19F2`). Writing them to 0 during
an activity does nothing. *(REGISTERMAP §2.2 calls word 6 "tare" and word 7 "run". The byte offsets
`6` = word 3 and `0xC` = word 6 (relative to word 0) show that word 3 is the tare trigger and word 6 the run
flag.)*

**Safe in the middle of a run?** Yes, for all three. The stock UI offers the same stop (the water,
brew and grind buttons are toggles), and the MCU runs the same stop path as at the target weight. For
brewing this also means the valve pulse afterwards (pressure relief, BREW.md §4).

**Known exception (not observed):** a brew started by the physical "e" switch (PA.15, the `0x1F52`
branch) runs with word 22 = 0. There, word 22 = 0 cannot stop the brew. merakiplus reports this honestly
as *"STOP NOT CONFIRMED · TRY AGAIN"* after 5 s. This path has never been seen on the machine.

### 18.2 The UI

* **The button** is in the same place and has the same style as STOP STEAM: the red pill in the action
  row. **v6.17:** touch area 232 × 60 px, pill 216 × 54 px in F_M, bright red with white text (≥ 4.5 : 1;
  the red is 4.0 : 1 against the background). v6.13–v6.16: 180 × 56 / 160 × 44 px in F_S and a dark red
  (1.9 : 1 against the background). The texts
  are *STOP WATER / STOP BREW / STOP GRIND* and *STOP VAND / STOP BRYG / STOP KVÆRN*. The button is
  shown while the activity's work_sta bits are set (`phase_main`). It is gone in the DONE view, and the
  valve pulse after a brew (`0105`) gives no button.
* **Two buttons:** if a main activity runs at the same time as latched steam (e.g. `5327` + word 24 =
  1), there are two buttons side by side: left = the main activity, right = STOP STEAM. Each has a
  touch area of 160 × 60 px and a pill of 152 × 54 px in F_S (v6.13–v6.16: 148 × 56 / 136 × 44 px in F_XS). The wand/flush still has no STOP button.
* **v6.20 — HOLD·STEAM during a brew:** while a brew runs and no steam does, the right-hand slot is **HOLD·STEAM**
  (*HOLD·DAMP*; same size as the two STOP slots, outlined in the steam colour, filled while held). Holding it 650 ms starts
  latched steam on top of the brew (the same frame as the steam icon); a tap only explains. Once the controller latches,
  the slot is STOP STEAM. Dimmed when steam cannot start (error, a write in transit). See
  [concurrent-brew-steam.md §12](concurrent-brew-steam.md#12-implementation-v620).
* **Feedback:** a tap (activated on release, like all buttons) immediately shows the toast *"STOPPING
  WATER…"*. When the MCU has cleared the bits, *"WATER STOPPED"* (green) follows. If there is no ACK,
  not even after a retransmit, *"STOP: NO REPLY · TRY AGAIN"* (red) is shown. If the bits have not
  been cleared after 5 s, *"STOP NOT CONFIRMED · TRY AGAIN"* (yellow) is shown. An ACK alone does not
  count as confirmation. A new tap while the stop frame is waiting for an ACK does not queue another
  frame.
* **The sub-line** is the auto-stop expectation: *"STOPS AT 40.0 G"* (v6.13–v6.16: *"AUTO-STOP AT 40.0 G ·
  OR STOP"* — the button is right below it).
* **Frame policy:** stop frames only contain zeros and go through the same gate and write queue as
  everything else (CRC + ACK wait + one retransmit). They require no intent and cannot start anything.
  `MERAKIPLUS_READONLY=1` sends nothing, and the toast says why.
* **SIM:** the state JSON has `stop: {btns, pending, last, req, ok, fail}`. The ctrl actions
  `grind_stop` / `brew_stop` / `water_stop` (+ `steam_stop`, as before) and the layout names
  `grind_stop` / `brew_stop` / `water_stop` / `steam_stop` are new.

### 18.3 Verification

Locally (all green, figures in [CHANGES-v6.13](dev/UI_v6.md#changes-v613) item 6): 26 new self-test
checks, previews and spec alignment, mutation probes, SIM and E2E (quick).

**On-device:** not run. The machine was offline for the whole round: `<display-ip>` did not answer
ping, the ARP lookup was incomplete, and the telnet port was closed (2026-11-18 around 15:45).
`verify_stop_v613.py` is ready to run when the machine is switched on (§11, test plan S).

## 19. The power button (v6.14)

> On the tested hardware the power button behaves as a hardware power switch: see
> [power.md](power.md). The section below describes the software implementation (which mirrors the
> stock UI byte for byte).

**The bug:** the physical power button did nothing while Meraki+ was running. merakiplus only opened the
touch node (`/dev/input/event0`, TECHWIN_TS). The button sits on a different input node:
`/dev/input/event1` = **k62091** (the GPIO key driver `k62091.ko`, "Handlers=kbd event1", EV=3 = SYN +
KEY, one key). merakiplus never read that node.

### 19.1 The stock semantics — reverse-engineered, with evidence

| What | The stock UI does | Evidence (address) | merakiplus v6.14 |
|---|---|---|---|
| Which node | `open("/dev/input/event1", O_RDWR)` in its own thread — **no EVIOCGRAB**. For every `EV_KEY` (the code is not checked) `value` is stored (1 = down, 0 = released) | `app_powerKey_thread` 0x140AB4 (string 0x17BCEC); stored in app+0x15A8 | the k62091 node, read-only, **without a grab**. Selection: `MERAKIPLUS_KBD` → a node whose name contains `k62091` → the first node with EV_KEY and without EV_ABS → `/dev/input/event1`. All EV_KEY codes count as the button |
| When it powers off | `SleepRefresh` (lv_timer 100 ms): only when the second changes, e60 is incremented while the button is down. `dealPowerState(1)` powers off when e60 > 1 — i.e. the button held across **two second boundaries = 1.0–2.0 s**. A short press only wakes the stock UI's screensaver | `SleepRefresh` 0x149268 (timer 100 ms at 0x1354A8), e60 = 0x1E0E60, `dealPowerState` 0x14911C | **HOLD 1.5 s** (in the middle of the stock UI's window). Short press: nothing (counted on DIAGNOSTICS) |
| Blocking | While `isExtractionOrSteamSwitchOn()` is true, it does not count and does not power off: work_sta bit 12 (brew) or bit 14 (steam), mirror word 22 (brew active), 24 (steam) or 6 (grind). **Hot water does not block.** The stock UI NEVER stops an activity on power-off | `isExtractionOrSteamSwitchOn` 0x1481E0 (`GetMaichinState(0x1000/0x4000)`, words 22/24/6) | The same predicate (+ merakiplus's own start in progress for the three). Toast *STOP THE ACTIVITY FIRST · THEN OFF*. When the activity has ended, a **new full hold** powers off (the counter stands still, like e60). Hot water: power-off is sent, no stop frame |
| No connection | Only powers off if mirror word 0 ≠ 0 | 0x149210 | Only if the link is up and the controller has replied with word 0 ≠ 0. Otherwise toast *NO CONNECTION · CANNOT TURN OFF* — the button may be what just woke a sleeping controller. READ-ONLY: nothing |
| Power-off frames | `powerOff`: mirror word 0 = 0 → `performSleepActions` → `powerOffSend(0,0,0)` = **cmd8** (15 bytes) → `usleep(80000)` → `usart_send(6,0,2)` = **cmd6 sub0 len2 [word 0 = 0, word 1 = the mirror]** | `powerOff` 0x149074, `performSleepActions` 0x148324 (0x148F50, 0x14905C), `powerOffSend` 0x14700C, buffer `powerSendBuf` 0x1A6030 | Exactly that: `aa 55 08 <year lo> <year hi> <month 1-12> <day> <h> <m> <s> 00 00 00 <crc>` (example 2026-11-18 16:35:28: `aa 55 08 ea 07 0b 12 10 23 1c 00 00 00 cc df`), **≥ 80 ms** later `aa 55 06 00 02 00 00 00 00 56 94` (word 1 = 0 = awake, as merakiplus's keep-awake keeps it). The cmd6 frame goes through the write queue (CRC, ACK, one retransmit) and the frame policy with the new intent `I_POWEROFF` |
| cmd8's contents | year (u16), month (+1), day, hour, minute, second, alarm day/hour/minute = the next scheduled pre-heat (`performSleepActions`); no schedule = alarm 0/0/0 | 0x14703C–0x147148 (CRC_GetModbus16 over 13 bytes) | v6.16: stock's local time (UTC + zone + DST; v6.14 sent UTC) + the next pre-heat occurrence, or 0/0/0 without a schedule ([time-scheduling.md](time-scheduling.md)). The MCU's 0x88 acknowledgement is only counted for DIAGNOSTICS — the stock UI does not wait for it either |
| The MCU | The status task sees word 0 = 0 (every 500 ms) and starts `sleep`. It deletes `boiler_heat`, `head_heat`, **`status`** (RX parser + replies) and `run`, sets work_sta = 0 and resets the outputs. It only wakes on a **hold of the button** (its own GPIO PC.8, 8 × 100 ms low after a release) or the alarm → `reset muc` (CPU reset) | MEM03 0x29D4/0x29D8, sleep task 0x2F52 (0x2E1E deletes tasks; 0x2F0C outputs 0), alarm comparison 0x2FE2–0x3036, reset 0x326 (SYS IPRST) | merakiplus sends nothing more: keep-awake stands still, and polls are not answered (the link goes down — expected). The cmd8 parser 0x9C88 stores the 10 bytes in 0x20000E18 |
| Screen | Backlight off (`blpwm_set_enable(2,0)` + duty 0), off page. 1.5 s later: fb closed, `killall dispinit`, GPIOs released, **`echo mem > /sys/power/state`** (SoC suspend). After resume: mirror word 0 = 1, `killall demo_v9` → the watchdog restarts the stock UI → `PowerOn` | 0x1490C0–0x1490FC, 0x149494–0x14952C, `uikit_os_back_light_off` 0xC2278 | TURNING OFF… immediately, OFF after the ACK. The backlight is switched off 1.5 s after the press: duty 99 (dimmest) and then `pwm2/enable = 0` (merakiplus does not write the stock UI's duty 0 — with "polarity inversed" that is the brightest light if the enable did not take effect). **No SoC suspend and no killall** (a deliberate deviation: the network guard and telnet stay alive; merakiplus is not a restartable app behind a watchdog in run.sh mode) |
| Power on | The SoC wakes from the same button press; the stock UI restarts and sends `PowerOn` (word 0 = 1) at startup. The MCU wakes by itself (reset; word 0 starts as 1) | `app_init` 0xD9170 → `PowerOn` 0x146EF8 | A button press in OFF → backlight on, *TURNING ON… · KEEP HOLDING THE BUTTON A MOMENT*. When the controller has replied with word 0 = 1 for 0.6 s → PowerOn `aa 55 06 00 01 01 00 c5 5f` + awake `aa 55 06 01 01 00 00 c5 33` (the same two frames as at startup) → HOME, toast *MACHINE IS ON*, keep-awake runs again. Without a reply within 10 s: back to OFF. If the controller replies by itself with word 0 = 1 while merakiplus is in OFF (alarm, or the button was held without merakiplus seeing it), it powers on the same way. After the MCU reset the controller is on its startup values — **merakiplus's re-push** (v6.6, the 0x89 round) puts the config's values back |
| Touch while off | (the SoC sleeps — touch does nothing) | — | Touches are swallowed (no navigation, no frames). The OFF page is shown for 8 s; then the backlight goes off again. Touch cannot switch the machine on: the controller cannot receive anything while it sleeps |

### 19.2 The UI
* **TURNING OFF… / OFF / TURNING ON…** is a black page with a power symbol (ring + line; crema during
  TURNING OFF and TURNING ON, dimmed during OFF). During OFF: *HOLD THE POWER BUTTON TO TURN ON* and
  *THE SCREEN TURNS OFF · TOUCH TO SHOW*. An open detail is closed without SAVE (brightness and
  language previews are reverted); after POWER ON Meraki+ is on HOME, like the stock UI
  (`navigator_popToHome`).
* **DIAGNOSTICS → POWER BUTTON** (a new block between RAW COUNTERS and ABOUT): state, *POWER · WORD 0*
  (the controller's reply or *NO REPLY*), *PRESS · OFF · ON*, *LAST KEY* (`code = value · age`), node
  (`/dev/input/event1 · k62091` or *NO BUTTON NODE FOUND*).
* Sheets: `power_states_en.png` · `power_states_da.png`.

### 19.3 Limits
* Only the backlight is switched off; the SoC keeps running (power consumption: the display still
  draws 10 frames/s in black). That is the only deliberate deviation from the stock UI.
* A button press while the stock UI is running (slot A) is handled by the stock UI as before. In
  run.sh mode the stock UI is SIGSTOPped; its own queue on event1 receives the same presses and reads
  them after SIGCONT — the last value is "released", so the stock UI does not power off because of it.
* `mcu_sim.py` does not yet model the MCU's sleep (silence on word 0 = 0, reset on the button). The SIM
  has a ctrl action `{"cmd":"key","value":1|0}` and `power` in the state JSON; see `SIM_HANDOFF`.

### 19.4 Verification
Locally all green (figures in [CHANGES-v6.14](dev/UI_v6.md#changes-v614) item 5). On-device: **not
run** in this round (the installation of v6.13 in slot B was in progress). `verify_power_v614.py` is
ready (§11, test plan T). The later on-device run and its hardware findings are documented in
[power.md](power.md).

## 20. Sound (v6.18)

The full description — the reversed stock audio path with addresses, the rules and the helper — is in
[audio.md](audio.md). In short:

* **What plays:** a **start jingle** once per power-on (a marimba + bell arpeggio), then **one tone per boiler** as its
  controller heat stage (word 62 brew/extract, word 63 steam) goes from below 100 to 100 — a low marimba double note
  for the brew boiler, a glockenspiel triple pip for the steam boiler. A boiler that is **already** at stage 100 at the
  power-on / wake plays nothing (v6.19: only the jingle). A boiler is re-armed only after real cooling (stage ≤ 30 or
  15 °C below its ready point).
  Silent while OFF / asleep; a tone that comes due during an activity is skipped (logged *suppressed*).
* **Where to set it:** *SYSTEM › DISPLAY · SOUND* — SOUND (OFF / ON / STOCK SOUNDS), READY TONE (OFF / BREW BOILER /
  STEAM BOILER / BOTH BOILERS), VOLUME (config `volume`, 0 = mute, capped at 70). SAVE of any of them plays a short
  test tone (when the machine is on).
* **Where to see it:** *DIAGNOSTICS › SOUND* — `SOUND · VOLUME` (*ON · 31*, *OFF*, amber *VOLUME 0* / *NO PLAYER*) and
  `LAST SOUND` (*JINGLE · PLAYED*, *STEAM · PLAYED*, *BREW · SKIPPED*, *BREW · MUTED*, …). On the device every event is
  also a line in `/tmp/merakiplus_snd.log`.
* Previews: `100_system_display_sound`, `101_detail_sound`, `102_detail_ready_tone`, `103_detail_volume`,
  `104_sound_saved_toast`, `105_diag_sound` (EN + DA).

## 21. Hot water: dosing gate + manual water (v6.22)

Background, evidence and decisions: [water-dosing-compat.md](water-dosing-compat.md) §10–§11.

* **The rule (stock, copied):** hot water by weight (AUTO) only when the controller's firmware string in words 80..87
  (copy in 88..95, both in the sub56 len56 poll) parses as a version **> 5.1.2** and the copy matches and there is no
  scale fault (word 35 & 0x1400). Everything else — not polled yet, empty, garbage, copy mismatch, ≤ 5.1.2, scale
  fault — is **MANUAL ONLY**. No owner override. `config.json` is never written by the gate.
* **WATER page, manual only:** no rows (HOT WATER and WATER MODE are hidden, their config values untouched); three
  centred lines — *MANUAL HOT WATER* (F_S), *RUNS UNTIL YOU TAP STOP*, and why: *CONTROLLER < 5.1.3: NO WEIGHT STOP* /
  *CONTROLLER VERSION UNKNOWN* / *SCALE FAULT: NO WEIGHT STOP* (amber). HOLD TO START as on every feature page.
* **Frames:** manual start `aa 55 06 13 01 01 00 c1 db` (sub19 len1, word 19 = 1), manual stop `aa 55 06 13 01 00 00 c0 4b`
  (word 19 = 0) — the stock legacy water page (`set_manual_hot_water_switch` 0x105118, `usart_send(6,19,1)`). On a
  controller not known to be > 5.1.2 the stop is word 19 = 0 **only**; word 16 is never written to it.
  `frame_policy` rejects word 16 = 1 unless dosing, and word 19 = 1 without the manual-water intent.
* **WATER MODE = MANUAL on a dosing controller** starts manual water too (v6.17–v6.21: toast *WATER = MANUAL · USE
  STOCK UI*, removed).
* **HOME:** the water icon's target reads *MAN.* when the next start is manual; the hold sub-line *HOLD · HOT WATER ·
  MANUAL*; while running *MANUAL · RUNS UNTIL STOP*, no target mark, the ring fills over 300 g; STOP WATER as always.
  **No time limit and no display-side weight stop** (maintainer decision 2; the stock UI has none either).
* **Scale fault while AUTO runs:** on the rising edge, a pending water start is dropped and — if the controller still
  reports word 16 ≠ 0 — word 16 = 0 is sent once with the toast *SCALE FAULT · WATER STOPPED* (stock `hotWaterUpdate`
  0x115B80..0x115BD8). The controller clears word 16 itself on the same bits, so usually nothing has to be sent.
  A manual run is not stopped. HOME's error text: *SCALE FAULT* (code E2). Starts stay blocked while the error is
  active (as for every error word).
* **DIAGNOSTICS › CONTROLLER** (above ABOUT): *FIRMWARE* = the string as reported (upper case; *NOT REPORTED*,
  amber *COPY DIFFERS*, amber when unparsable) · *HOT WATER* = *AUTO + MANUAL* (green) / *MANUAL ONLY* /
  *MANUAL (SCALE)* (amber).
* **Strings (EN / DA):** `TX_W_MAN_T` MANUAL HOT WATER / MANUELT VARMT VAND, `TX_W_MAN_RUN` RUNS UNTIL YOU TAP STOP / LØBER
  TIL DU TRYKKER STOP, `TX_W_WHY_*`, `TX_H_HOLD_WATER_MAN`, `TX_H_WATER_MAN`, `TX_A_WATER_MAN` MAN., `TX_T_WATER_SCALE`,
  `TX_ER_E2_SCALE`, `TX_DG_MCUCAP` CONTROLLER / STYRING, `TX_DL_MCU_*`, `TX_DV_MCU_*`, `TX_DV_WD_*` (one block; full
  table in [dev/UI_v6.md Appendix A](dev/UI_v6.md#appendix-a--string-table-id--en--da)). All fit their pixel budgets
  in both languages (selftest).
* Previews: `106_page_water_manual_old`, `107_page_water_manual_unknown`, `108_page_water_mode_manual`,
  `109_home_water_manual_label`, `110_home_water_manual_running`, `111_diag_controller`, `112_diag_controller_scale_fault`.

## 22. Water tank messages (v6.24, issue #1)

Until v6.24, removing the water tank showed *ERROR · UNKNOWN FAULT*: the controller reports the tank through error
bits that Meraki+ had no text for. v6.24 names them. Evidence: static analysis of the Gen 1 controller firmware
(5.1.4) and the stock display UI. Meraki+ runs on Gen 2 (the issue was reported on a Gen 2 machine running v6.23),
but **these specific bits are not yet confirmed on Gen 2** — issue #1 awaits the Gen 2 values.

| Bit | What the controller means (Gen 1) | Stock UI | Meraki+ HOME sub-line (EN / DA) | Code |
|---|---|---|---|---|
| word 36 `0x8000` | tank switches read "none" — **tank removed** | icon `lackWaterTank` | *WATER TANK MISSING* / *VANDTANKEN MANGLER* | E3 |
| word 36 `0x0001` | tank switches read "low" | icon `lack_water` + *Water tank is low, please refill water* | *WATER TANK LOW · PLEASE REFILL* / *LAVT VANDNIVEAU · FYLD TANKEN* | E3 |
| word 35 `0x8000` | the pump ran but the flow meter stood still (latched; cleared when the tank is taken out) | icon `lackWater` | *NO WATER FLOW · CHECK THE TANK* / *INTET VANDFLOW · TJEK TANKEN* | E2 |

* **Where it comes from:** the controller's tank check (MCU `0xB268`, every 10th status tick) adds two debounced
  tank-switch inputs: 0 → word 36 bit 15 set (bit 0 cleared, word 35 bit 15 cleared), 1 → both word-36 bits cleared
  (normal), 2 → word 36 bit 0 set (bit 15 cleared). The dry-pump check (MCU `0xB218`) sets word 35 bit 15 when the
  pump has run with no flow-meter pulses for more than 60 checks. The controller's own hot-water control also cancels
  on word 35 / word 36 `& 0x8001` ([water-dosing-compat.md §1.1](water-dosing-compat.md#11-hot-water-control-0x1b8c-called-every-10-ms-cycle)). The stock display (`dealLackWaterAndBar`
  `0x10F6EC`) shows these as water-tank icons and is not part of its error-code pop-up list (`error_tip_deale`
  `0x10EB14`); the `LackWater.mp3` sound is only played by the factory test page.
* **Priority:** boiler / sensor / mains / scale faults keep their place in front; then tank missing, tank low, no
  flow. Every other unmapped bit still reads *UNKNOWN FAULT* (code `?`) — DIAGNOSTICS shows the raw E1 / E2 / E3
  values in any case.
* **Display only:** the phase is still ERROR while any of words 34/35/36 ≠ 0, so starts stay blocked (*ERROR ACTIVE ·
  START BLOCKED*) exactly as before; no frame, frame-policy rule or guard changed. Unlike the stock UI, which only
  shows an icon, Meraki+ therefore also blocks starts while the tank is low — unchanged from v6.23.
* **Tests:** UI self-test section *v6.24 water tank messages* (14 bit combinations × EN/DA, guards unchanged, clear →
  READY, text widths); SIM scenario `fault_water_tank` (SIM state `err.id` / `err.code`).

## 23. Bean container message (v6.25, work_sta bit 8)

Until v6.25, lifting the bean container (hopper) off the machine showed *ASLEEP* / *SOVER*: `phase_raw()` read
work_sta (word 33) bit 8 clear as sleep. Bit 8 is the bean-container switch — the stock UI shows
`lackBeenTank.png` for exactly that bit (see the evidence comment at §22). The same work_sta value `0x0001`
also occurs in the drain/sleep task (word 1 = 1), which is why the bit alone is ambiguous: word 1 and word 0
disambiguate it. v6.25 therefore checks sleep first (word 1 = 1, or word 0 = 0 = the MCU sleep task), no link
stays *OFFLINE*, and only then reads bit 8 clear as the new **BEAN** phase.

| State (link up) | Phase | HOME chip (EN / DA) | HOME sub-line (EN / DA) |
|---|---|---|---|
| word 33 bit 8 clear, word 0 = 1, word 1 = 0 | BEAN | *MISSING* / *MANGLER* (amber) | *BEAN CONTAINER MISSING · FIT IT* / *BØNNEBEHOLDER MANGLER · SÆT DEN I* |
| word 1 = 1 (drain/sleep) | SLEEP | *ASLEEP* / *SOVER* | *WAKING UP…* / *VÅGNER…* |
| word 0 = 0 (power off, MCU sleep task) | SLEEP | *ASLEEP* / *SOVER* | *WAKING UP…* / *VÅGNER…* |
| no link | OFFLINE | *OFFLINE* | *NO RESPONSE…* |

* **Display only.** The four start guards keep blocking exactly as they did when the state was mislabelled sleep;
  the guard text is now *FIT THE BEAN CONTAINER* / *SÆT BØNNEBEHOLDEREN I* (also HOME's hint line). No frame-policy
  rule, no control write and no calibration change. A running activity (grind/brew/water/steam) and real error words
  keep priority over the container state, as before.
* **Evidence:** Gen 1 hardware session 2026-11-26 (controller 5.1.4): lifting the container cleared bit 8 with
  word 0 = 1 / word 1 = 0 and E1/E2/E3 = 0; the old build showed SOVER, the fixed build shows the bean message;
  putting it back returns READY. SIM: `bean_tank` switch event in `sim/mcu_sim.py` (idle work_sta recomputed from
  it), scenario `bean_container_missing`, UI self-test section `st_bean` (EN + DA). Gen 2: not verified yet.
