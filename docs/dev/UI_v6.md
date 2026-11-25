# UI_v6 — Meraki+ (identifier merakiplus) v6.18: start jingle + one ready tone per boiler (sound) + one page per feature + SYSTEM in sections + DIAGNOSTICS behind SYSTEM (UX audit) + clock + correct 24-hour flag + planned pre-heat and auto off + graph reset on activity start + the physical power button + manual STOP for water / brew / grind + settings parity (spec-aligned) + boot slot integration + EN/DA + design for the round screen + SIM build/E2E + boot push and RX resync on the device + live-proven phase semantics, 64-bit clock and re-push after an MCU restart + Wi-Fi at startup and self-healing (network guard) + HOME without REVERT + calm graph and stable weight (display conditioning, weight latch, dead band) + pressure as a ≈bar estimate (anchor: pre-infusion = config pressure) + UX review (plain language, contrast, start hint, gear, close chevron, swipe guard, DIAGNOSTICS groups)

> **Note (public edition):** Development notes written during development. Paths such as
> `artifacts/…`, `boot_install.py` steps and md5 tables refer to the author's working environment; as
> an end user, use `./install.sh` and `./revert.sh` (see the [README](../../README.md)). The
> manufacturer's files are not included; IP addresses and other personal data have been removed.
> In this repository the UI source is `src/merakiplus.c`; `REVERT.md` is published as
> [REVERT-v6-config.md](REVERT-v6-config.md), `boot/BOOT.md` as [docs/boot.md](../boot.md) and
> `boot/GESTUS-FIX.md` as [docs/gesture.md](../gesture.md). References to `BREW.md`, `STEAMLIVE.md`,
> `SESSION.md`, `HOTWATER.md`, `WRITETEST.md`, `FINDINGS` and `GROUND-TRUTH.md` are to the author's
> capture/analysis notes.

**Date:** 2026-11-22 · **Version:** v6.21 (v6.20 + **launcher + automatic rollback** (OTA plan phase 1b): the UI runs from a release directory, writes its own healthy marker, exec-reloads into an installed update when idle, and shows TRIAL / FAILED / FALLBACK / a vendor-update notice on SYSTEM — [ota-plan.md phase 1b](../ota-plan.md#phase-1b--implemented-2026-11-22)) · v6.20 (v6.19 + **steam during a brew**: HOLD·STEAM next to STOP BREW, a latched steam start allowed while a brew is the only activity — [concurrent-brew-steam.md §12](../concurrent-brew-steam.md#12-implementation-v620)) · v6.19 (v6.18.1 + **no ready tones at a power-on with hot boilers** — only the jingle; a ready tone needs a cold → hot transition during the session — and **nicer synthesized sounds** (marimba / glockenspiel / bell instead of sine beeps); display and audio only, see [CHANGES-v6.19](#changes-v619) and [audio.md §2.6](../audio.md#26-power-on-with-hot-boilers-v619)). v6.18.1 = (v6.18 + a **sound-helper fix**: on a real machine the helper could not load the display's `libmi_common.so` — `undefined symbol: sem_post` — so every sound was silent; see [CHANGES-v6.18.1](#changes-v6181) and [audio.md §3](../audio.md#3-hardware-check-and-the-v6181-fix)). v6.18 = (v6.17 + **sound**: a start jingle once per power-on and a different ready tone per boiler when the controller's heat stage (words 62/63) reaches 100 — the stock UI plays one sound once both are; display and audio only, no frame changed; see [CHANGES-v6.18](#changes-v618) and [audio.md](../audio.md)). v6.17 = (v6.16 + **UX audit / IA consolidation** — display and navigation only: HOME is the root, each feature has its own page with its settings and HOLD TO START, SYSTEM has four sections, DIAGNOSTICS sits behind SYSTEM, the STOP button is larger, the texts are shorter; see [CHANGES-v6.17](#changes-v617) and [ux-audit-v617.md](../ux-audit-v617.md). §3 below describes v6.17; older sections and CHANGES-v6.x describe the UI as it was at the time). v6.16 = v6.15 + **time & scheduling** — user bugs: the 24-hour flag was inverted relative to the stock UI, merakiplus showed no clock, and there was no planned pre-heat; the clock, the fixed TIME FORMAT, a pre-heat schedule sent as the stock cmd8 alarm and an optional auto off, see [CHANGES-v6.16](#changes-v616) + [time-scheduling.md](../time-scheduling.md)). v6.15 = (v6.14 + **the graph resets when an activity starts** — user bug: the cup placed before a brew dominated the graph scale, so the 0 → 36 g curve looked flat; display only, §14.7 + [CHANGES-v6.15](#changes-v615)). v6.14 = (v6.13 + **the physical power button** — user bug: the button did not work in merakiplus; the k62091 node (event1) is now read, and the stock UI's power-off/power-on semantics have been reverse-engineered and mirrored, §19 + [CHANGES-v6.14](#changes-v614)). v6.13 = (v6.12 + **manual STOP for WATER, BREW and GRIND** — user bug: hot water could not be stopped from the screen; the stop words were reverse-engineered from the MCU firmware and cross-checked against the stock UI's own stop calls; auto-stop is unchanged — see [§18](#18-manual-stop-v613) and [CHANGES-v6.13](#changes-v613); v6.12 = the v6.11 UI + the boot work stream's gesture fix ([gesture.md](../gesture.md)), source kept as `merakiplus_v6_12.c`; v6.11 = v6.10 + **UX review**: a critical design pass over all pages and states — plain language instead of word numbers and raw codes, raised contrast, a start hint in the empty chart field, a visible gear for the settings, a close chevron + swipe-down in the details, a swipe guard with axis lock, short stat labels and DIAGNOSTICS in sections; control logic and protocol are unchanged — see [§17](#17-ux-review-v611) and [CHANGES-v6.11](#changes-v611); v6.10 = v6.9 + **pressure estimate**: "PRESSURE\* UNCALIBRATED" has been replaced by **≈BAR · EST.** with a single linear anchor — the pre-infusion plateau of 358 raw in the recording = config `preInfusionPressure` (4 bar) — and a pulse limit of 20 bar; display only — see [§16](#16-pressure-estimate-v610) and [CHANGES-v6.10](#changes-v610); v6.9 = v6.8 + **display conditioning** of the graph and hero — median-3 + EMA, a robust scale, pulse gate, pressure curve only during brew/water —, a **stable weight source** (a latch instead of the phase per sample: the user's periodic weight spikes) and a **weight dead band** < 0.25 g; display only, all control logic reads raw values — see [§14](#14-display-conditioning-and-stable-weight-v69) and [CHANGES-v6.9](#changes-v69); v6.8 = v6.7 without the REVERT button on HOME — see [CHANGES-v6.8](#changes-v68); v6.7 = v6.6 + **network guard**: Wi-Fi at startup (a slot B boot had no network) and self-healing of the Wi-Fi stack, plus word 113 in the boot push as the stock UI's recorded value — see [CHANGES-v6.7](#changes-v67); v6.6 = v6.5 + phase semantics after the live session — two steam paths and concurrent phases —, a **64-bit clock** (v6.5's clock wrapped after 24.86 days on the device) and a **re-push of the boot push after an MCU restart** — see [CHANGES-v6.6](#changes-v66); v6.5 = v6.4 + a boot push from the config at startup **also on the device**, with an activity guard, and a resync watchdog in the RX parser — see [CHANGES-v6.5](#changes-v65); v6.4 = v6.3 + a SIM build against the simulated test environment — see [§13](#13-sim-build-and-e2e-v64) and [CHANGES-v6.4](#changes-v64); v6.3 = v6.2 + the product name **Meraki+** and two UI languages, English default / Danish — see [CHANGES-v6.3](#changes-v63); v6.2 = v6.1 + BOOT UI, runtime switch to the stock UI, exit reminder — see [CHANGES-v6.2](#changes-v62)) · **Status:** implemented, cross-compiled and **only verified locally** (offline previews + self-test + automated spec alignment). **Nothing has been run on the machine, and no network device has been contacted.** The on-device work belongs to the integration engineer (§11).
**Source:** `merakiplus.c` (one file, plain C, no new dependencies) · **Binary:** `merakiplus_v6` (static ARMv7 EABI5 musl) · **Previews:** `round_v6/` (English; contact sheet) + `round_v6/da/` (Danish; contact sheet) · `EN | DA side by side` · **Revert:** `REVERT.md` · **Boot integration:** `boot/BOOT.md` (the boot work stream's; §9 = the UI requirements for v6.2) · **Spec:** [PARITY.md](PARITY.md) / `parity_spec.json` · **Predecessor:** `ROUNDUI.md` (v4)

> **Status of the caveat in v6:** the spec was missing when v6 was written, and all limits were assumptions. v6.1 has replaced them with the spec's proven ranges. The alignment is automated: `parity_check.py` compares `SET[]` in the binary with `parity_spec.json` and fails on any unjustified deviation (`round_v6/parity_check.txt`: **41 spec parameters → 24 rows, 17 left out with a rationale, ALL ALIGNED**). What is still estimated or locked is in §4.4. All changes are listed in [CHANGES-v6.1](#changes-v61).

> **Names and languages (v6.3):** the product is called **Meraki+** in the UI. `merakiplus` is the technical identifier — source file, binary, the directory `/new_customer/merakiplus/`, environment variables and logs — and is therefore also used in this document when the program is meant. The UI exists in **English (default) and Danish**. This English edition refers to screen elements by their **English** text; the Danish wording is in [Appendix A](#appendix-a--string-table-id--en--da), and a few Danish labels are given in parentheses where the history depends on them. Some texts were renamed in v6.3 (CHANGES-v6.3 item 5): sections 1–12 use the new names, CHANGES-v6.1 and -v6.2 quote the names of the time.

---

## 1. What's new compared to v4

| Area | v4 | v6 |
|---|---|---|
| Keep-awake | wrote `word1=1` (= sleep/drain, **bug**) | writes **`word1=0`** at start + every 30 s; every 3 s if the mirror shows `word1≠0` |
| Writes | power/awake only | a write queue with CRC + ACK wait + **one** retransmit (150 ms → retransmit → 300 ms → error); a **frame policy** on every cmd6 frame: write bands, trigger words and per-word spec range (§7a) |
| Activities | inert pills | GRIND / BREW / WATER / STEAM with observed recipes, **hold-to-start** (650 ms), STOP for steam |
| Settings | none | 24 config rows on 3 pages (21 editable, 2 locked, 1 sensor) + from v6.3 UI LANGUAGE (its own file), stepper detail, SAVE/CANCEL, atomic `config.json` persistence with backup + md5 log |
| Phases | idle/grind/brew/hot water | + **STEAM** (`work_sta & 0x4020`), ASLEEP (`word1==1` or bit 8 cleared), ERROR (E1/E2/E3), OFFLINE, HEATING UP/READY, DONE |
| Graphics | 5×7 block font, full-page redraw | a custom anti-aliased stroke font, AA primitives, a gauge with glow, a dirty-rect compositor, slide/crossfade, ripple, toasts, splash |
| Input | events also go to demo_v9's queue | `EVIOCGRAB` (exclusive) — see §7.4 |
| Poll | words 0..111 | + `cmd3 sub112 len3` every second (words 112..114 for pre-infusion/group temp) |

Kept unchanged: the global circle clip (chord mask in `pset`/`hspan`, R=234), the 10 Hz poll pattern (sub0/sub33 @ 50 ms alternating + sub56 @ 200 ms), CRC counters, the cmd9 version reply, a graceful exit on SIGINT/SIGTERM, **the run.sh contract** (SIGSTOP/SIGCONT; run.sh is not changed).

---

## 2. Design system

**Colors** (near-black with a radial vignette, ordered dither against banding; pure black outside R=234):

| Token | RGB | Use |
|---|---|---|
| text 1 / 2 / 3 | 242,244,247 / **176,183,196** / **140,147,162** (v6.11; v6.10: 160,167,180 / 104,111,126) | values / sub-lines / micro labels — contrast measured in the self-test: text 3 ≥ 4.5:1 on the background, surfaces 1 and 2; text 2 ≥ 7:1 |
| disabled (v6.11) | 98,105,120 | inactive icons, stepper signs at the limit, offline hero, disabled SAVE — ≥ 3:1 (large glyphs), deliberately weaker than text 3 |
| surface 1 / 2 / 3 | 25,28,35 / 37,41,51 / 52,57,70 | chips, buttons, pressed state |
| accent "crema" | 244,192,122 | **the only UI accent**: settings values, SAVE, stepper ring, splash |
| ok / warning / error | 98,222,168 / 255,190,84 / 255,96,96 | toasts, CRC chip, errors |

Semantic phase colors (used consistently on the gauge fill, lit ticks, phase chip, sub-line, chart and active button):

| Phase | Color | | Phase | Color |
|---|---|---|---|---|
| OFFLINE | gray 124,130,144 | | GRINDING | lime 174,228,112 |
| ASLEEP | indigo 136,144,222 | | PRE-INFUSION | blue 92,180,255 |
| ERROR | red 255,96,96 | | EXTRACTION | copper 255,142,68 |
| HEATING UP | amber 255,186,84 | | HOT WATER | cyan 70,216,230 |
| READY / DONE | mint 98,222,168 | | STEAM | violet 198,152,255 |

**Typography:** a custom stroke font (stroke-drawn glyphs on a 10-unit grid, rasterized with a distance field → real anti-aliasing, cached per size). Deliberately **upper-case typography with letter spacing**; numbers are **tabular** (all digits the same width, so values do not jump). Full ÆØÅ. Scale (cap height px): XS 11 · S 14 · M 18 · L 26 · XL 44 · XXL 66 (hero number). Icons (bean, cup, drop, steam) are glyphs in the same engine.

> Deviation from the task text: it mentioned a scaled bitmap font (1x–4x). A 5×7 font scaled ×8 gives staircase edges that cannot be called polished, so I wrote a stroke font instead. Still plain C without dependencies, and the font data is ~70 lines in the source.

**Grid:** a centered column; everything lies inside the gauge's inner radius (~193). List rows: 336×50 px chips, 58 px pitch, corner radius 16. Pills: fully rounded, a 2 px soft shadow.

**Gauge:** a 270° arc (135°→405°), center line r=214, thickness 14, round ends, a dark→light gradient along the fill, a white "head" dot, a soft glow ±5 px, 50 ticks (every 5th larger) r 225..232 that light up in the phase color up to the value, a white **target marker** across the ring. During a brew a thin white inner ring (r=198) = weight progress towards the auto-stop.

**Motion:** exponentially eased gauge fill (τ≈110 ms) · a sweep from zero when the gauge changes scale (temperature ↔ weight ↔ pressure) · a 300 ms color crossfade on a phase change · touch ripple (360 ms) · a pressed state on all buttons · a hold ring around the activity button · a pulsing ring on the active activity · page slide 260 ms (ease-in-out) · crossfade 200 ms into/out of the detail · toast fade/slide · splash with an arc sweep (1.5 s).

---

## 3. Page map and touch

**v6.17** replaced the five-page carousel (HOME · BREW · STEAM·WATER · SYSTEM · DIAGNOSTICS, changed by
swiping) with one root and sheets — the reasoning is in [ux-audit-v617.md](../ux-audit-v617.md) and CHANGES-v6.17 below:

```
HOME ─ icon, tap ─► GRIND / BREW / WATER / STEAM   (live line · that feature's settings · HOLD TO START)
     ─ icon, hold ► starts the activity (650 ms)
     ─ gear ──────► SYSTEM ─ DISPLAY · CLOCK · SCHEDULE · STOCK UI (sections, one open at a time)
     │                     └ DIAGNOSTICS ─► DIAGNOSTICS
     └ error / offline hint, tap ─► DIAGNOSTICS
```

**Open** by tapping the thing; **go back** with the chevron at the top of every page and editor (the
whole header is the target), or swipe right. There are no page dots, no edge zones and no page swipes.

| Page | Opened by | Contents |
|---|---|---|
| **HOME** | — (root) | clock (or the wordmark while the clock is not set) · phase chip · gauge · hero number · sub-line · chart (40 s) / one hint line · 3 stats · the 4 activity icons **with their targets underneath** (grind dose, brew weight, water amount, milk temperature) — or the STOP button(s) while something runs · the gear |
| **GRIND** | tap the bean icon | live line *ON THE SCALE 0.0 G* · GRIND DOSE, GRIND MODE · HOLD TO START |
| **BREW** | tap the cup icon | live line *BOILER 92.4° · GROUP 70°* · BREW WEIGHT, BREW TEMP, PRE-INFUSION, PRE-INF. TIME, PRE-INF. PRESSURE, GROUP HEAD (4 in view, the list scrolls) · HOLD TO START |
| **WATER** | tap the drop icon | live line *WATER 99.9°* (word 40 — the "water sensor") · HOT WATER, WATER MODE · HOLD TO START |
| **STEAM** | tap the steam icon | live line *BOILER 130° · MILK 22.6°* · MILK TEMP, STEAM LEVEL · HOLD TO START |
| **SYSTEM** | the gear | *MERAKI+ V6.21* (v6.21: + *· TRIAL* until healthy, *· V6.22 FAILED* / *· FALLBACK* in amber, or *VENDOR UPDATE · RUN INSTALL*) · four section headers with a summary (**DISPLAY · SOUND** *ENGLISH* · **CLOCK** *16:35* · **SCHEDULE** *06:30 / OFF* · **STOCK UI**) + the **DIAGNOSTICS** row (*OK* / the error code / *DOWN*). Closed: 5 rows, no scrolling. A tap on a header opens it (and closes the other) |
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
caveats: [§16](#16-pressure-estimate-v610). (Up to and including v6.9:
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
| WATER MODE | `hotWaterMode` | (16/19 in the start block) | — config only | 1..2 | 1 AUTO / 2 MANUAL | **PROVEN** [0x11344c] |
| WATER SENSOR | *(none)* | 40 (read-only) | — no write exists | — | live, e.g. 99.9 °C | **SENSOR** [0x112c9c] |

### 4.3 SYSTEM (config only — no MCU write)
| Label | config field | Min..max | Shown as | Spec tier | Status |
|---|---|---|---|---|---|
| **UI LANGUAGE** (v6.3) | *(none — its own file `/new_customer/merakiplus/lang`)* | 0..1 | ENGLISH / DANSK (endonyms) | — | merakiplus's own setting; **not** a spec parameter and not in `--dump-table` (CHANGES-v6.3 item 2) |
| BRIGHTNESS | `backlight` | 1..10 | live preview (PWM `duty = 100 − 7·v`) | 2 | **PROVEN** 0..10; **0 deliberately left out** (duty 100 = a dark screen the user cannot find their way back from) |
| VOLUME | `volume` | 0..100 | 0–100 | 2 | **PROVEN** for the slider; UI hint about the discrete selector (§4.4) |
| TEMP UNIT | `Temperature` | 0..1 | 0 °F / 1 °C — also controls merakiplus's display | 2 | **PROVEN** |
| STOCK LANG. (before v6.3: LANGUAGE) | `language` | 0..10 | 11 languages, Danish not among them (the names are shown in the UI language) | 2 | **PROVEN** — unlocked in v6.1; only controls the stock UI |
| WATER SHORTCUT | `hotWaterShortcut` | 0..1 | OFF / ON | 2 | **PROVEN** |
| DAYLIGHT SAVING | `sumtime` | 0..1 | OFF / ON | 2 | **PROVEN** |
| CLEANING REMINDER | `clearTip` | 0..1 | OFF / ON | 2 | **PROVEN** |
| TIME FORMAT | `timemode` | 0..1 | **0 = 12-HOUR / 1 = 24-HOUR** (corrected in v6.16) | 3 | **PROVEN** [`update_time` 0xD5944: `model[0x59]` is XORed with 1 at 0xD5970] |
| AUTO TIME | `Automatically` | 0..1 | OFF / ON | 3 | **PROVEN** |
| TIME ZONE | `timeZone` | — | `UTC+01:00` (v6.16: the stock label; the detail shows the place names) | 3 | **LOCKED** (set in the stock UI; table 0x1A5890 copied in v6.16) |
| **PRE-HEAT** (v6.16) | *(none — `/new_customer/merakiplus/schedule`)* | 0..1 | OFF / ON | — | merakiplus's own; the alarm goes out as the stock cmd8 alarm ([time-scheduling.md](../time-scheduling.md)) |
| **PRE-HEAT AT** (v6.16) | *(same file)* | 00:00..23:55, step 5 min | per TIME FORMAT | — | merakiplus's own |
| **REPEAT** (v6.16) | *(same file)* | 0..3 | ONCE / EVERY DAY / MON-FRI / SAT-SUN | — | merakiplus's own (stock: Never / Everyday / Workday / Weekend) |
| **AUTO OFF** (v6.16) | *(same file)* | 0..4 | OFF / 15 / 20 / 30 / 60 MIN (default 30 MIN since v6.23) | — | merakiplus's own (stock: fixed 30 min; `sleepModeSetting`'s table is 15/20/30/60) |

### 4.4 What is still estimated or locked
| Row | What is uncertain | How the UI shows it | Why it is justifiable |
|---|---|---|---|
| PRE-INF. TIME | the picker's number of options has not been mapped (PARITY §6.6); max 15 = the stock UI's own default | **`MAX = ESTIMATE`** in amber next to the range + `≈` in front of the value in the list | 5–15 s is harmless; the field is one byte |
| GROUP HEAD | the same picker anomaly; the spec's max of 80 is itself a cautious estimate | **`MAX = ESTIMATE`** + `≈` | E1 only triggers at > 110 °C; 80 is far below |
| VOLUME | the stock UI has two controls: a 0–100 slider (what merakiplus edits) and a discrete High/Medium/Low/Mute selector whose mapping has not been mapped | **`STEPS = ESTIMATE`** + `≈` (the note says *"… · 0-100 SLIDER"*) | config only, no MCU write |
| PAUSE TIME | max unknown — and the value has no effect | locked, amber note *"NO EFFECT · PRE-INF. TIME IS USED"* | §4.5 |
| TIME ZONE | the size and meaning of the index table | locked, *"THE SPEC HAS NO INDEX TABLE"* | an invalid index could break the stock UI's time/DST logic |

**The estimate rule:** if the stock UI itself has saved a value above my estimated max (e.g.
`headTemper=90`), that is evidence that the picker reaches it, and the range is extended to that value
— but never above the ceiling (60 s / 100 °C). **Proven** limits are never extended.

**Verify online** (§11 M): count the picker's options for pre-inf. time/pressure/pause and group temp
in the stock UI, and correct `max` in the two SF_EST rows.

### 4.5 Deliberately removed, locked or not exposed
| Field | Decision | Rationale (spec) |
|---|---|---|
| hot water **temperature** | **live line on the WATER page** (v6.1–v6.16: a read-only sensor row), not a setting | The stock UI has no setpoint: the temperature shown is the e-boiler's measured value (word 40, `hotWaterTemperUpdate` 0x112c9c). The row sits on STEAM · WATER, where you would look for the setting, and says *"NO SETPOINT EXISTS"* |
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
| 10 | ~~Steam requires the physical wand switch~~ — **disproven live** (STEAMLIVE §6.3): display steam starts 0.22 s after the write with the wand switch = 0. The wand switch alone gives a *flush* without auto-stop | **v6.6:** *"STARTING STEAM…"*, then *STEAM·AUTO*; the wand alone = *STEAM·WAND* without a STOP button ([CHANGES-v6.6](#changes-v66) item 2) |
| 11 | Picker navigation is anomalous → number of options unknown | SF_EST + a visible hint (§4.4) |
| 12 | Volume: slider + discrete selector | hint (§4.4) |

**Two findings beyond the spec** (both verifiable in `demo_v9.dis`):
* **`timemode`: 1 = 24 h, 0 = 12 h** (corrected in v6.16 — the v6.1 finding "0 = 24 h" was wrong). `model_data_init`
  stores `timemode != 0` in model+0x59, and `update_time` [0xD5944] XORs that byte with 1 (0xD5970) before the
  test: `timemode = 1` → `"%02d:%02d"`, `timemode = 0` → `A.M./P.M.`. The stock switch is labelled "24-Hour
  Format" and is ON for 1. The factory config has 1 = a 24-hour clock. Details in [time-scheduling.md](../time-scheduling.md).
* **`sumtime` is daylight saving time**, not "timer display" as the spec's label guesses: the accessor
  is called `getSumerTimeSwitchValue` [0xd2804]. v6's label DAYLIGHT SAVING (SOMMERTID) is kept.

---

## 5. The save path: order and reload semantics

SAVE is **all-or-nothing** and runs in this order (the same as the stock UI's onClick handlers: MCU first, then config — PARITY §5 item 8):

1. **Guards:** value within the range, config loaded, no activity in progress, link up (for MCU parameters), write queue empty.
2. **MCU write** (if the parameter has a word): a cmd6 block through the frame policy → wait for the `0x86` ACK with echo. No ACK after one retransmit → toast *"SAVE FAILED · NO ACK · NOT SAVED"* (v6.4; before *"NO ACK · NOT SAVED"*), the config is not touched.
3. **config.json persistence** (`cfg_set_int`): fresh read → md5 → `cfgbak/config.json.orig` is created if it is missing (**pristine, never overwritten**) → `cfgbak/config.json.bak` (rolling) → new contents where **only the number token** of the one top-level field has been replaced → `/customer/.merakiplus_cfg.tmp` → `fsync` → reread and compared → `rename()` → `fsync` of the directory → a line in `cfgbak/config_writes.log` → the flag file `cfgbak/RESTART_NEEDED`.
4. If step 3 fails, the **old** value is sent to the MCU again (rollback), and the UI shows *"CONFIG ERROR · NOT SAVED"*.

**Format parity with the stock UI.** The stock UI's `setNumber` immediately writes the whole tree with `cJSON_Print` (`fseek/ftruncate/fwrite/fflush/fsync`, PARITY §1.2). merakiplus keeps the safer pattern (tmp + rename + backup + md5), but the result is **byte-identical** to what the stock UI would have written itself: `cjson_canon.py` recreates the `cJSON_Print` format independently and shows that (a) the baseline file is in exactly that format, (b) merakiplus's file after a SAVE equals `cJSON_Print(baseline with the field changed)`, (c) the field order is unchanged and no field has been added (`round_v6/config_format_check.txt`). merakiplus never adds keys and never touches strings or `presetStore`.

**Reload semantics (corrected in v6.1 after PARITY §1.4):**

| Who | When a SAVE takes effect |
|---|---|
| **The controller (MCU)** | **immediately** — the cmd6 write is ACKed before the config is touched |
| **merakiplus** | immediately |
| **The stock UI (demo_v9)** | **only after a restart** (`kill $(pidof demo_v9)`; the watchdog starts it again in < 4 s). At startup it reads `config.json` and **boot-pushes** the values to the controller itself: `cmd6 sub1 len30` (words 1..30) + `cmd6 sub112 len3` (words 112..114) |

This means:
* After a restart the config, the stock UI and the controller are **automatically in sync** — also for a field whose MCU write might have been lost.
* **Without** a restart (only exit → SIGCONT; v6.8: *SWITCH TO ORIGINAL NOW* in run.sh mode) the stock UI keeps running with its **old RAM mirror**. It shows old values, and its next block write (e.g. a brew start `sub5 len18`, which sends words 11/14/17 along) sends the **old** values to the controller again. Its own next config write also writes out its old tree. Therefore: **restart demo_v9 after an merakiplus SAVE, before the stock UI is used.** `RESTART_NEEDED` marks that this is pending, and merakiplus prints a reminder on stdout on exit.
* **merakiplus never kills demo_v9 itself** — that would break the run.sh contract (run.sh SIGCONTs the PID it started with). The restart is a manual step.
* The UI text in the detail view therefore says *"CONTROLLER NOW + CONFIG"* / *"STOCK UI: AFTER RESTART"* (v6 just wrote *"TAKES EFFECT NOW"*, which was an exaggeration as far as the stock UI is concerned).

Paths can be redirected: `MERAKIPLUS_CONFIG` (default `/customer/config.json`), `MERAKIPLUS_BAKDIR` (default `/new_customer/merakiplus/cfgbak`), and from v6.3 `MERAKIPLUS_LANGFILE` (default `/new_customer/merakiplus/lang`; the UI language, which does **not** go through the save path here — see CHANGES-v6.3 item 2).

---

## 6. Activity control

Frames are built by `act_push()`; only the config-derived fields are substituted, the rest is as observed. The self-test compares them **byte for byte incl. CRC** with the recorded frames:

| Activity | Block | Contents | Source | Stop |
|---|---|---|---|---|
| GRIND | sub3 len4 | `1, dose, 60, 1` | [L] BREW.md W1 | auto at weight · **STOP GRIND** = `sub6 len1` word 6 = 0 (v6.13, §18) |
| BREW | sub5 len18 | `60,0,0,0,1,0,temp,0,60,level,60,0,water,0,0,0,0,1` | [L] BREW.md W3 | auto at weight (predictive) · **STOP BREW** = `sub22 len1` word 22 = 0 (v6.13, §18) |
| WATER | sub9 len11 | `1,0,temp,0,60,level,60,1,water,0,0` | [L] HOTWATER.md W1 | auto at weight · **STOP WATER** = `sub16 len1` word 16 = 0 (+ `sub19 len1` word 19 = 0 if word 19 ≠ 0) (v6.13, §18) |
| STEAM | sub24 len8 | `1, target, 0,0,0,0,0,0` | [L] STEAMLIVE.md §4.1 (byte-identical) | auto at milk (word 48) ≥ target (word 25) · **STOP STEAM** = the same block with word24=0 — only for latched steam (v6.6) |

* **Before BREW** the MCU mirror is compared with the config; only on a difference are `sub23 len1` (brew weight) and/or `sub112 len3` (pre-infusion + group temp) sent first. word113 is only compared on the high byte (§4a no. 7). If a frame in the sequence cannot be queued, none of them are queued.
* **Guards** (otherwise a toast with the reason): link up, not ASLEEP, no error code, no activity in progress, write queue empty, all frame-relevant config values loaded and within spec, and the mode is the observed one (`grindMode=1` weight, `hotWaterMode=1` auto) — grinding by time and manual water have no observed recipe.
* **Intent:** every start frame declares which start words it may set (GRIND 3+6, BREW 9+22, WATER 9+16, STEAM 24). Everything else is rejected (§7a).
* **The auto-stop expectation** is shown in the sub-line (*"AUTO-STOP AT 36.0 G · OR STOP"* — v6.13; *"AUTO-STOP AT 65.0° · OR STOP"*) and as a marker/inner ring.
* **Manual STOP (v6.13):** while grind, brew or water is running, **STOP GRIND / STOP BREW / STOP WATER** is shown where the start icons are (touch area 180 × 56 px). Tap → *"STOPPING WATER…"* → stop frame → *"WATER STOPPED"* once the MCU has cleared the bits. Details: [§18](#18-manual-stop-v613).
* **Steam (v6.6, proven live in STEAMLIVE.md):** after the start, *"STARTING STEAM…"* is shown; the steam starts by itself about 0.2 s after the write, without the wand being opened. The chip is then **STEAM·AUTO** with the target (word 25), the auto-stop expectation and a STOP button. If the wand is opened without a start from the screen (flush), the chip is **STEAM·WAND**: no auto-stop expectation, no STOP button — it stops when the wand is released, and merakiplus sends nothing. Steam on top of a brew (work_sta `5327`) is shown as the brew's page with a steam badge and the milk temperature in the stat row. Details: [CHANGES-v6.6](#changes-v66) item 2.
* **v6.8:** HOME has no REVERT button. The way back to the stock UI is SYSTEM → *SWITCH TO ORIGINAL NOW* (blocked during an activity; the MCU auto-stops by itself), the switcher gesture and run.sh's trap.

---

## 7. Protocol and system details

1. **Keep-awake:** `AA 55 06 01 01 00 00 C5 33` (word1=0). Start sequence: `sub0=[1]`, `sub1=[0]`.
2. **Phase detection** (`phase_raw`): `0x4020` → STEAM · bit 6 → GRINDING · bits 1+2 → brew (bit 9 = extraction) · bits 1+3 → HOT WATER · E1/E2/E3 ≠ 0 → ERROR · `word1==1` or bit 8 cleared → ASLEEP · word40 < word11 − 2.0 °C → HEATING UP · otherwise READY. (v4's brew mask `0x206` could never match pre-infusion `0x1107`; fixed.) **v6.6:** `phase_raw` is unchanged and is what the guards and the state contract use. HOME shows `phase_main`: grind / brew / water take precedence over steam, and the steam path is decided by word 24 while `0x4020` is set (≠ 0 → AUTO, sticky; = 0 → WAND; word 44 bit0 / word 46 confirm).
3. **Error/empty states:** OFFLINE after 1.5 s without a valid frame · CRC chip for 10 s after a CRC error · error codes decoded into plain language (registermap §2.7; v6.11: the code E1/E2/E3 is shown dimmed in the chart field with a reference to DIAGNOSTICS) · v6.11: the empty chart says what you can do (*HOLD AN ICON TO START* / *SWIPE FOR SETTINGS*, or the reason why the start is blocked) instead of "NO ACTIVITY".
4. **`EVIOCGRAB`:** while demo_v9 is SIGSTOPped, the kernel still puts every touch into its evdev queue, and it replays them after SIGCONT. In v4 that was harmless (few touches, on the background). v6 has many touches in the middle of the screen — exactly where the stock UI's Grinder/Brew/Steam/Hot Water buttons are — so merakiplus takes an exclusive grab of the input device and releases it on exit. **Side effect:** `tinj` (another process) cannot inject while the grab is held. `MERAKIPLUS_NOGRAB=1` disables the grab for tinj runs.
5. **Read-only:** `MERAKIPLUS_READONLY=1` blocks all cmd6 writes (also keep-awake and the boot push) — for the first screen/touch round.
6. **Boot push (v6.5):** after the start sequence, when the link is up and work_sta has been read: `cmd6 sub1 len30` + `cmd6 sub112 len3` from config.json — the stock UI's own two blocks — through the frame policy and the write queue. If an activity is already running at startup, it is skipped (activity guard). `MERAKIPLUS_NOBOOTPUSH=1` disables it. Details: [CHANGES-v6.5](#changes-v65) item 2.
7. **RX parser (v6.5):** a frame is only accepted with its full length and a valid CRC; a candidate with an impossible length is discarded immediately, and an incomplete candidate is discarded after 400 ms (`RX_RESYNC_MS`), so the parser finds the next `AA 55`. Counted on DIAGNOSTICS (*RX CRC / RESYNC*). Details: [CHANGES-v6.5](#changes-v65) item 3.
8. **Clock (v6.6):** all times are `ms_t` (`int64_t`), derived from `CLOCK_MONOTONIC`. Up to and including v6.5 the clock was a `long` (32 bits on the device) and wrapped after 24.86 days. The main loop's timers are in `lt`. Details: [CHANGES-v6.6](#changes-v66) item 3.
9. **Re-push after an MCU restart (v6.6):** if the version request (`0x89`) arrives again more than 200 ms after the boot push, or words 112 and 113 are both 0 in three reads in a row on an idle machine, the boot push is sent again — the same blocks, the same frame policy, the same activity guard; at least 5 s between two, at most 3 signature-triggered ones per 10 min. DIAGNOSTICS: *RE-PUSH n · 1-30*. `MERAKIPLUS_NOBOOTPUSH=1` also disables it. Details: [CHANGES-v6.6](#changes-v66) item 4.

---

## 7a. Safety assertions: the frame policy

The MCU has **no range checks** (cmd6 = `memcpy` + ACK), and some words are triggers that the MCU consumes: 3/6/9 = start/tare, **26/27 = weight calibration (writes MCU flash)**, **30 = MCU reboot**. Therefore **every** cmd6 frame — settings SAVE, activity start, sync before a brew, keep-awake, rollback — goes through one gate, `wq_push_t()` → `frame_policy()`, before it can reach the UART:

| Layer | Rule | Enforced |
|---|---|---|
| Write bands | only words 0..31 and 112..114 | runtime |
| **TRIG_NEVER** = words 26, 27, 30 | must **never** be ≠ 0. No caller can allow them | runtime **+ compile time** (`_Static_assert`: no intent mask overlaps TRIG_NEVER) |
| **TRIG_START** = words 3, 6, 9, 16, 22, 24 | may only be ≠ 0 in a frame if the caller has explicitly named the word (`I_GRIND`, `I_BREW`, `I_WATER`, `I_STEAM`) | runtime + compile time (intent ⊆ TRIG_START) |
| Per-word range (`word_ok`) | words 4/11/14/17/23/25/2/112/113/114 must lie within the `SET[]` range · words 5/13/15 = 60 · word 0 = 1 · word 1 = 0 · **all other words = 0** (7, 8, 10, 12, 18–21, 26–31) | runtime |

Consequences: merakiplus cannot send calibration, reboot, drain (28/29), back-flush (20), self-test (8), sleep/drain (word1 = 1), power-off (word0 = 0) or the manual water switch (19) — not even through a future code bug. A settings SAVE can never start an activity. The one exception that is built in: **STOP STEAM** clamps the target word into the range, so a stop can never be rejected. *(v6.14 adds power-off via the new intent `I_POWEROFF`, only from the physical power button — §19.)*

A rejected frame is not queued, is counted in `wq.n_block` (DIAGNOSTICS: the fourth number in **CMD6 OK·RTX·ERR·BLOCK**, amber if ≠ 0) and gives the toast *"REJECTED BY FRAME POLICY"* / *"WRITE REJECTED"*. On exit merakiplus prints the count on stdout. **Expected value in all normal use: 0.**

**Audit of v6's frames against the write atlas (PARITY §3)** — no deviations found; v6 already sent 0 in words 26/27/30:

| Frame | Atlas | Trigger words in the frame | Result |
|---|---|---|---|
| GRIND `sub3 len4` | §3.1 | 3 = 1, 6 = 1 (intended) | byte-identical with BREW W1 |
| BREW `sub5 len18` | §3.2 | 9 = 1, 22 = 1 (intended); 6 = 0, 16 = 0 | byte-identical with BREW W3 |
| WATER `sub9 len11` | §3.3 | 9 = 1, 16 = 1 (intended); 19 = 0 | byte-identical with HOTWATER W1 |
| STEAM start/stop `sub24 len8` | §3.4 | 24 = 1/0; **26 = 27 = 30 = 0**, 28/29/31 = 0 | byte-identical with SESSION §10.2/10.3 |
| sync `sub23 len1`, `sub112 len3` | §3.7–3.8 | none | ok |
| settings SAVE (10 word types) | §3.7–3.8 | **never covers** words 3/6/9/16/22/24/26/27/30 | ok — `grindMode` is sent as `sub2 len1`; the stock UI's `sub2 len2` would also send start word 3 |
| keep-awake `sub0=[1]`, `sub1=[0]` | §3.8 | none | ok |

`sub18 len14` (steam mirror sync, §3.5) and `sub4 len21/22` (NFC/preset, §3.6) are not used by merakiplus.

---

## 8. Rendering and performance

* **Layers:** `bgpure` (vignette, built once) → `bgbase` (+ the page's static text) → `slay` (+ gauge) → `canvas` (+ widgets + overlays).
* **Gauge LUT:** angle/coverage/glow per pixel are computed once; per frame only the pixels in the changed angle interval are recomputed.
* **Widgets** have a signature (a hash of the contents) and a fixed rect; they are only drawn when the signature changes or their rect is hit by damage.
* **Present:** only dirty rects are copied to the fb; because of the page flip the union of this and the previous frame's rects is copied. No change → no flip. 30 fps during animation/touch, 10 fps otherwise.

Measured on the Mac (host), not on the device:

| Scenario | host ms/frame |
|---|---|
| brew, animated gauge + chart + numbers | 0.03 (max 0.07) |
| full rebuild of a page | 0.19 |
| page slide | 0.17 |
| idle without changes | 0.001 |

Even with a pessimistic factor of 100 for the Cortex-A7 this is below the 30 ms budget, but it is an extrapolation. **The DIAGNOSTICS page shows the real frame time on the device** (`FRAME MS NOW/MAX`, max over 5 s) — it must be read in the test (§11 C).

---

## 9. Remaining assumptions and deferred items

**Assumptions that remain after the alignment:**
* The two ESTIMATE maxima and the volume selector's mapping (§4.4).
* `grindMode` is written as `sub2 len1`. The stock UI uses `sub2 len2`, which also writes word 3 (the grind start trigger); merakiplus deliberately keeps the trigger word out of the frame. The MCU is a plain `memcpy`, so word 2 gets the same value.
* The meaning of `Temperature` (0 = °F, 1 = °C) is the spec's; `timemode` is now proven from the code (§4a).
* The language names were shown in Danish (ENGELSK, TYSK, FRANSK, ITALIENSK, SPANSK, POLSK, ARABISK, KINESISK FORENKL., KINESISK TRAD., KOREANSK, JAPANSK) in the spec's index order 0..10. merakiplus itself stayed Danish at the time — the field only controls the stock UI after its restart. *(From v6.3 the names are shown in the UI language.)*

**Item 8 of the alignment — language, time zone, presets:**
| Topic | Decision | Why |
|---|---|---|
| **STOCK LANG.** (v6.1: LANGUAGE) | **unlocked**, 0..10 | The spec proves the table: 11 language strings @ 0x166d84..0x166df4, index 0..10, confidence H. The handler [0xe1c78] is only `setLangType(idx)`. I have also checked that the language choice does not affect OTA: `GetUpdateLatestVersion` [0x1375b4] uses a **hard-coded** `"English"` in the manifest URL |
| **TIME ZONE** | still **locked** | The spec gives `min`/`max` = null: "UTC offset index" without a table size. The field drives `time_sync_thread` + the DST calculation; an index outside the table has not been proven harmless |
| **Presets** (`presetStore`) | still **deferred** | The spec gives the block frame (`sub4 len22`, words 4..25) and the field names (`name, iconId, grind, dose, brewTemp, yield, steamLevel, milkTemp`, `currentIndex`, `presetAppliedIndex`), but **not** a complete proven format: `template_words` is null, there are 6 numeric fields vs. 5 words (4/11/23/14/25) without a field→word mapping or units, and the current `presetStore` is `{}`, so there is no example of the serialization. Saving a preset also requires writing a nested object and adding the key `presetAppliedIndex` — merakiplus's writer deliberately only replaces one number token and never adds keys. Guessing here would write a structure the stock UI might not be able to parse |
| **NFC** | deferred | requires the MF522 driver + card format; the spec itself recommends leaving it out |

**Other deferred items:**
| Topic | Why |
|---|---|
| Manual stop of grind / brew / water | The spec mentions candidates for water (`sub16 len1` + `sub19 len1` = 0), but only from static analysis. Should be captured from the stock UI's stop before it gets a button. *(Done in v6.13 — §18.)* |
| Grinding by time (`grindMode=2`), manual water (`hotWaterMode=2`) | no observed start recipe; the start is blocked with an explanation. The setting can still be saved — it then applies to the stock UI |
| Volume preview, merakiplus's own auto-sleep | sound is owned by demo_v9; merakiplus keeps the machine awake by design |
| Calibrated pressure | requires a physical reference (BREW.md §7) |
| Kinetic scrolling | direct dragging is enough for 5–10 rows |

---

## 10. Build and preview

```sh
cd workspace
# device (static ARMv7 musl) — zig 0.17.1 used; 0 warnings
zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o merakiplus_v6 merakiplus.c
# device binary + SIM build (host + static ARM) + a list of what changed in the device build vs. v6.5
# + (if the local ARMv7 container exists) the whole self-test as a real 32-bit ARM binary
sh build_sim.sh
# everything local in one go: preview build + self-test + bench + spec alignment + all previews
# + comparison with v6.5 + mutation probes + config format check + circle check + contact sheet
sh round_v6/build_previews.sh
# per iteration (§15 DEV-ITERATION-POLICY): sim self-test + quick core subset, one language, parallel (about 45 s)
python3 ../../sim/selftest.py && (cd ../.. && python3 sim/run_e2e.py --all --quick --parallel 4 --ui-bin workspace/merakiplus_v6_sim)
# the FULL E2E verification (> 35 min) — ONLY in CI/CD or at a release milestone (§15)
python3 sim_e2e.py                   # -> sim/reports/v6.9/ (individual parts: --only calm | net | ref | …)
```
In this repository: `./build.sh` / `./build.sh host` (see [CONTRIBUTING.md](../../CONTRIBUTING.md)).
Individually: `/tmp/merakiplus_v6_preview [--lang en|da] <outdir> <config.json>` · `[--lang en|da] --selftest <config.json>` · `--bench <config.json>` · `--dump-table` (SET[] as TSV) · `--dump-strings` (the string table as TSV: id, class, EN, DA) · `--textw "TEXT"…` (pixel widths) · `--is-merakiplus <file>…` (the BOOT UI guard's binary recognition; exit 0 = all are merakiplus). PNG: `python3 round_v6/fb2png.py <file.bin>…`.

**Local results v6.9** (the whole table in [CHANGES-v6.9](#changes-v69) item 6): self-test **354 checks, 0 errors** in English and Danish — and **353 / 0** as a real 32-bit ARM binary; previews 67 × 2, circle check 0 violations, 90 of 126 byte-identical with v6.8 (the rest intended); mutation probes 10/10; spec alignment ALL ALIGNED; 0 warnings.

**Local results v6.6** (historical; everything has been rerun; the whole table is in [CHANGES-v6.6](#changes-v66) item 1): self-test **304 checks, 0 errors** in English and Danish — and **303 / 0** as a real 32-bit ARM binary; previews 62 × 2, circle check 0 violations, 102 of 114 byte-identical with v6.5; spec alignment ALL ALIGNED; 0 warnings.

**Local results v6.5** (historical; details in [CHANGES-v6.5](#changes-v65) item 1):
* **Self-test (the device code):** **241 checks, 0 errors** in English and Danish (v6.4: 199; the 42 new ones cover the boot push on the device and the RX resync).
* **Previews:** 57 × 2 languages; **circle check 57/57 + 57/57, 0 violations**; 102 of 112 byte-identical with v6.4, the other 10 are DIAGNOSTICS + the version string (`v64_compare.txt`); 1 new scenario.
* **Device build vs. v6.4:** 14 changed and 14 new fragments, all intended (`sim_build_check.txt`). **Spec alignment: ALL ALIGNED.** Config format 5/5, boot check 11/11, i18n check 6/6. **0 warnings** (device, SIM ARM, SIM host).
* **E2E against the simulated MCU, real UI:** English **23/23 scenarios / 589/589 checks**, Danish **23/23 / 589/589**; settings matrix 188/188 in both; boot push 23/23; `sim/selftest.py` 107/107 (`sim/reports/v6.5/SUMMARY.md`).

**Local results v6.4** (history; details in [CHANGES-v6.4](#changes-v64) items 1–2):
* **E2E against the simulated MCU, real UI:** English **22/22 scenarios / 553/553 checks**, Danish **22/22 / 553/553**; settings matrix 188/188 in both; `sim/selftest.py` 107/107 (`sim/reports/v6.4/SUMMARY.md`).
* **Self-test (the device code):** **199 checks, 0 errors** in English and Danish (v6.3: 193; the 6 new ones cover the SIM boot push).
* **Previews:** 56 × 2 languages; **circle check 56/56 + 56/56, 0 violations**; 100 of 112 byte-identical with v6.3, the other 12 intended (`v63_compare.txt`).
* **Device build = v6.3 + two texts:** token comparison after the preprocessor, 10 differences (`sim_build_check.txt`). **Spec alignment: ALL ALIGNED.** Config format 5/5, boot check 10/10, i18n check 6/6. **0 warnings** (device, SIM ARM, SIM host).

**Local results v6.3** (history; everything was rerun in v6.4 and is still green — details in [CHANGES-v6.3](#changes-v63) item 8):
* **Self-test:** **193 checks, 0 errors** — run twice, with English (`selftest.txt`) and Danish (`selftest_da.txt`) as the UI language (v6.2: 152). The 41 new ones are the i18n table, the language file, the UI LANGUAGE row and the layout in both languages.
* **Previews:** **56 scenarios × 2 languages = 112** (`round_v6/` English, `round_v6/da/` Danish). **Circle check 56/56 + 56/56, 0 violations**, max non-black radius 234.67 (`check_circle.txt`, `check_circle_da.txt`).
* **Spec alignment:** unchanged, **ALL ALIGNED** (41 → 19 proven, 2 estimated, 2 locked, 1 sensor, 17 left out). **Config format:** 5/5 PASS. **Boot check:** 8/8 PASS. **i18n check** (`i18n_check.txt`): 6/6 PASS.
* **Static build:** 0 warnings, 0 errors.

**Local results v6.2 (history — the parts below were rerun in v6.3 and are still green; the figures are v6.2's):**
* **Self-test** (`round_v6/selftest.txt`): **152 checks, 0 errors** (v6.1: 87). The 65 new ones cover the flag logic, the exit reminder, the runtime switch and the layout.
  * *Frame vectors:* the 6 observed frames (keep-awake, GRIND, BREW, WATER, STEAM start/stop) are still byte-identical incl. CRC; write bands, retransmit, timeout, ACK.
  * *Packing:* word112/113 for all combinations (switch 0–1, pressure 3–4, time 5–60, pause 0–255) + 5 new byte vectors with an independently computed CRC (SAVE of pre-infusion OFF, pressure 3, time 7, group temp 80, grind mode).
  * *Trigger word audit:* 36 settings frames (min/max/current for all 12 MCU rows) never cover a trigger or start word; every activity frame has exactly its intended start words; the steam block's words 26–31 are 0; 13 negative tests (word30 = 1, word26/27 ≠ 0, start words without or with the wrong intent, service words, out-of-spec values, packed words) are all rejected and counted.
  * *Boot push equivalence:* an independent model of `model_data_init` (config → mirror) shows that every SAVE writes exactly the words and values the stock UI's boot push would set from the same config.
  * *Table/editor:* removed fields are gone, sensor/locked/unlocked status, stepper clamp and step grid, layout check (all texts in the detail view fit within the circle's chord for all 24 rows).
  * *Config persistence:* as v6 + **all 21 editable fields saved as min → max → original**, md5 back at the baseline `<config-md5-redacted>`, pristine backup untouched, one log line per write (69), unknown keys (`theme`) are rejected.
  * *Out of spec:* proven limits are not extended, estimates are only extended up to the ceiling, an enum outside the table gives no lookup outside the name array, the start is blocked, and the frame policy rejects the brew block even if the UI guard is bypassed.
* **Spec alignment** (`round_v6/parity_check.txt`): 41 spec parameters → 19 proven, 2 estimated, 2 locked, 1 sensor, 17 left out with a rationale. **ALL ALIGNED.**
* **Config format** (`round_v6/config_format_check.txt`): 4/4 PASS — merakiplus's file is byte-identical to `cJSON_Print` of the same change.
* **Circle check** (`round_v6/check_circle.txt`): **51/51 previews OK, 0 violations**, max non-black radius 234.67.
* **Static build:** 0 warnings, 0 errors.
* The save scenarios in the preview run the real persistence code against **copies** (`round_v6/_preview_config*.json`); `artifacts/config.json` is untouched (md5 unchanged).

Previews (51, `.bin` + `.png`; `39`–`50` are new in v6.2, see CHANGES-v6.2 item 8): `00_splash` · `01–13_home_*` · `14–17_list_*` (BREW, scrolled, STEAM · WATER with the sensor row, SYSTEM) · `18–25_detail_*` (unchanged, changed, enum, **locked time zone**, hold acceleration, saved toast, saving, no ACK) · `26_diag` (with the BLOCK counter) · `27–29` · **new in v6.1:** `30_detail_estimate_hint` (MAX = ESTIMATE) · `31_detail_sensor_vandtemp` · `32_detail_breaktime_locked` · `33_detail_language` · `34_detail_steam_gear` · `35_list_system_scrolled` · `36_list_bryg_out_of_spec` · `37_detail_out_of_spec` · `38_home_start_blocked_out_of_spec`. **New in v6.3:** `51_detail_ui_language` · `52_ui_language_live_preview` · `53_ui_language_saved_toast` · `54_home_other_language` · `55_diag_about` — and the whole set now exists in two languages (`round_v6/` = English, `round_v6/da/` = Danish).

---

## 11. ON-DEVICE TEST PLAN (for the integration engineer)

> **Language (v6.3):** a fresh installation starts in **English**. This English edition quotes the English texts. Texts from versions before v6.11 that no longer exist in the string table (e.g. *BREW TEMP SET TO …*, *STEAM ARMED · OPEN THE STEAM VALVE*, *STOP SENT · WORD 24 = 0*) are given in English translation.

None of this has been carried out. The order goes from harmless to running actuators; stop at the first deviation.

**A. Preparation**
- [ ] Baseline: `pidof demo_v9`, `md5sum /customer/config.json` (expected `<config-md5-redacted>`), `cat /sys/class/pwm/pwmchip0/pwm2/duty_cycle` (expected 37), fb capture.
- [ ] Fetch a local copy of `/customer/config.json` (independent of merakiplus's own backups).
- [ ] Upload `merakiplus_v6` to `/tmp`, md5 round trip. Install only after B–F: v4 → `merakiplus_v4.bin`, v6 → `merakiplus` (run.sh unchanged).

**B. Read-only round** — `MERAKIPLUS_READONLY=1 MERAKIPLUS_NOGRAB=1`
- [ ] Splash → HOME is shown; fb capture of all 5 pages; `check_circle.py` = 0 violations.
- [ ] DIAGNOSTICS: LINK OK, RX OK increases, RX CRC / RESYNC = 0 / 0, CMD6 = 0/0/0/0, **FRAME MS MAX < 30**; scroll one row: BOOT PUSH = OFF / OFF (read-only sends nothing).
- [ ] The values in the three lists match `config.json`; no row is red (out of spec); `MIRROR 112-114` shows `0104 0605 70` (or `0104 0606 70` if there has been a brew since the stock UI's boot — see L).
- [ ] WATER SENSOR (STEAM · WATER) follows the BREW BOILER number on HOME; the row has no −/+.
- [ ] TIME FORMAT shows **24-HOUR** (config `timemode=1`), HOME's top line shows a 24-hour clock, and the stock UI's clock is 24-hour (v6.16). If not, the finding in §4a is wrong: report back.
- [ ] Swipe + edge tap change the page; SYSTEM → *SWITCH TO ORIGINAL NOW* → SWITCH NOW (v6.8; before: REVERT) → demo_v9 SIGCONT, the stock UI redraws itself.

**C. Write round without actuators**
- [ ] Start without `MERAKIPLUS_READONLY`. DIAGNOSTICS: AWAKE = YES, WORK_STA = `0101`, CMD6 OK ≥ 4 (PowerOn, awake and from v6.5 the two boot push blocks — see Q), ERR = 0, **BLOCK = 0**. The boilers hold their temperature for 5 min (the v4 bug would let them cool down).
- [ ] FRAME MS during slide, scroll and hold repeat: read MAX.

**D. EVIOCGRAB** (a physical finger, without `MERAKIPLUS_NOGRAB`)
- [ ] Tap around on HOME and in the lists, then *SWITCH TO ORIGINAL NOW* (v6.8; before: REVERT): the stock UI must **not** react to any of the touches.
- [ ] `kill -9` on merakiplus: the grab is released, the stock UI reacts normally to touch after SIGCONT.

**E. Settings matrix** — for each row: open → change → SAVE → verify → set back → SAVE → verify

| Parameter | Test change | Verify after SAVE |
|---|---|---|
| BRIGHTNESS | 9 → 5 → 9 | duty 37 → 65 while editing (live), 65 after SAVE, 37 after revert; CANCEL restores the duty |
| BREW TEMP | 87.0 → 88.0 → 87.0 | DIAGNOSTICS CMD6 OK +1; `word 11` = 880 (the HOME sub-line shows 88.0°) |
| GROUP HEAD | 70 → 80 → 70 | `MIRROR …/114` = 80; cannot go above 80 (estimate) |
| PRE-INF. PRESSURE | 4 → 3 → 4 | `MIRROR 112` = `0103` → `0104`; cannot choose anything but 3/4 |
| STEAM LEVEL | MEDIUM → STRONG → MEDIUM | word 14 = 4 → 3 |
| HOT WATER | 40 → 50 → 40 g | word 17 = 500 → 400; steps of 10 g |
| STOCK LANG. (config only) | ENGLISH → GERMAN → ENGLISH | config only (`language` 0 → 1 → 0); see F for the stock UI |
| PRE-INF. TIME | 6 → 7 → 6 | `MIRROR 113` = `0705` |
| PRE-INFUSION | ON → OFF → ON | `MIRROR 112` = `0004` → `0104` |
| GRIND DOSE | 18.5 → 18.0 → 18.5 | word 4 |
| MILK TEMP | 65.0 → 60.0 → 65.0 | word 25 |
| VOLUME (config only) | 31 → 30 → 31 | only the config changed, the CMD6 counter unchanged |

For **every** SAVE:
- [ ] `md5sum /customer/config.json` = `md5_after` in the last line of `cfgbak/config_writes.log`.
- [ ] A `diff` against the previous version shows **only** the one number changed.
- [ ] `cfgbak/config.json.orig` still has the baseline md5; `config.json.bak` = the version before.
- [ ] No `/customer/.merakiplus_cfg.tmp` left behind.
- [ ] After the whole matrix: `md5sum /customer/config.json` = baseline.
- [ ] Error path: a stopped MCU reply cannot be provoked safely — skip; covered by the self-test.

**F. Reload semantics**
- [ ] Save one change, *SWITCH TO ORIGINAL NOW* (v6.8; before: REVERT): the stock UI still shows the **old** value (expected), and merakiplus has printed the restart reminder on stdout.
- [ ] `kill $(pidof demo_v9)` → the watchdog restarts it → the stock UI shows the **new** value → set it back via merakiplus or the stock UI, verify the baseline md5.
- [ ] Optional: STOCK LANG. → GERMAN, restart demo_v9, the stock UI is in German; set back to ENGLISH + restart.

**G. Activities** (supervised, water in the tank, hand on the switch)
- [ ] GRIND (portafilter in): hold the button → phase GRINDING within 3 s → auto-stop near the dose → DONE. Compare the TX frame with `aa 55 06 03 04 01 00 b9 00 3c 00 01 00 5d 6e`.
- [ ] WATER (cup underneath): auto-stop near 40 g.
- [ ] BREW: PRE-INFUSION (blue) ≈ 6 s → EXTRACTION (copper) → auto-stop ≈ 34–37 g; the inner ring reaches 100 %.
- [ ] A short tap on an activity button does **not** start anything.

**H. Steam session with milk** (the recipe has since been proven live with **the stock UI** — STEAMLIVE.md; with merakiplus it has still not been run. Use **R2–R6** for what v6.6 shows; the items here are still valid for the frames and the stop)
- [ ] Cold milk in the jug, probe/nozzle down. Hold STEAM → *"STARTING STEAM…"* (v6.5 and older: *"STEAM ARMED · OPEN THE STEAM VALVE"*).
- [ ] ~~Note: does the steam start on the write alone, or only when the valve is opened?~~ Answered live: on the write alone, after about 0.2 s. If the wand is opened without a write, it is a flush (R4).
- [ ] During steam: WORK_STA has bits 5+14 (`4121`), phase STEAM, hero = milk temp rising, marker at 65 °C.
- [ ] **Auto-stop:** does the MCU stop by itself at ≥ 65.0 °C? Note the final temperature (overshoot) and whether word 24 goes to 0.
- [ ] **Manual stop** in a new session: tap STOP STEAM at ~45 °C → does the steam stop? TX = `aa 55 06 18 08 00 00 8a 02 … 0c f7`.
- [ ] Timeout behavior if the steam does not start (the UI resets after 15 s; what does the MCU do with word 24 = 1? — still open, CHANGES-v6.6 item 8). **If word 24 stays at 1, do not just carry on — write 0 via STOP or the stock UI.**

**J. Trigger word audit on the device** (PARITY §6.2 — words 26/27/30 must never be ≠ 0)
- [ ] Record merakiplus's TX during all of E + G + H (UART sniff or `strace -e write` on merakiplus's PID). Decode every `AA 55 06 SUB LEN …` frame and confirm: no frame has word 26, 27 or 30 ≠ 0; words 3/6/9 ≠ 0 **only** occur in the three start blocks (`sub3 len4`, `sub5 len18`, `sub9 len11`); no settings SAVE covers a start word.
- [ ] DIAGNOSTICS after the whole session: **BLOCK = 0** (the policy has had nothing to reject) and ERR = 0.
- [ ] No MCU reboot: RX OK increases continuously, LINK has not been DOWN, the boiler temperatures have not had a dip.
- [ ] No unintended calibration: put a known weight on before and after the session — WEIGHT on HOME shows the same.

**K. Boot push sync** (PARITY §1.4 — the first live confirmation of the statically found boot push)
- [ ] Save BREW TEMP 87.0 → 88.0 and GROUP HEAD 70 → 80 in merakiplus (the controller + config now have 880 / 80). *SWITCH TO ORIGINAL NOW* (v6.8; before: REVERT).
- [ ] While merakiplus is **not** running: restore the config from `cfgbak/config.json.orig` with tmp+mv (REVERT.md), but do **not** restart demo_v9 yet. Now the config = 870/70, the controller = 880/80.
- [ ] `kill $(pidof demo_v9)` and record the UART during startup: expect `AA 55 06 01 1E …` (sub1 len30) and shortly after `AA 55 06 70 03 04 01 05 06 46 00 C7 02` (sub112 len3).
- [ ] Start merakiplus with `MERAKIPLUS_READONLY=1` (no writes from merakiplus): the HOME sub-line shows *"BREW TEMP SET TO 87.0°"* (word 11 = 870) and `MIRROR …/114` = 70. The controller has therefore been synced from the config by the stock UI's boot, not by merakiplus.
- [ ] If word 11 is still 880: the boot push does not exist in the form the spec describes — report back; §5 must then be corrected.

**L. The breakTime quirk** (PARITY §4 — MCU 0x1E10 overwrites word113 lo)
- [ ] After a fresh demo_v9 startup: `MIRROR 113` = `0605` (time 6, pause 5).
- [ ] Brew once with PRE-INFUSION = ON. During or after the brew: `MIRROR 113` = **`0606`** — the low byte has become the pre-inf. time. Note the duration of the PRE-INFUSION phase (the TIME field).
- [ ] Set the pause time to e.g. 9 s in **the stock UI** (merakiplus deliberately has no editor), restart merakiplus: PAUSE TIME shows 9, `MIRROR 113` = `0609`. Brew again: `MIRROR 113` = `0606`, and PRE-INFUSION lasts the same as before. That is the quirk demonstrated.
- [ ] Counter-test: set PRE-INF. TIME 6 → 8 in merakiplus and brew: `MIRROR 113` = `0808`, and PRE-INFUSION lasts about 2 s longer.
- [ ] PRE-INFUSION = OFF and brew: `MIRROR 113` low byte = `00`, no blue phase. Set everything back.

**M. Close the estimates** (PARITY §6.6)
- [ ] In the stock UI: count the options in the pickers for pre-inf. time, pre-inf. pressure, pause and group temp, and note the highest value. Also note which config values the discrete sound selector (High/Medium/Low/Mute) writes. The result closes the two SF_EST rows and the volume hint.

**N. Boot slot and runtime switch (v6.2)** — after `boot_install.py install` (BOOT.md §4); none of it has been run
- [ ] **N1 · Guard in an A boot via run.sh:** the bottom of SYSTEM shows BOOT UI = ORIGINAL (editable if merakiplus is installed as `/new_customer/demo_v9`). The bottom of DIAGNOSTICS: FLAG = NOT PRESENT, STARTED FROM = RUN.SH, SLOT B = MERAKI+ FOUND, SWITCHER DAEMON = RUNNING, STOCK PROCESS = PAUSED.
- [ ] **N2 · Flag:** choose MERAKI+ → SAVE; `ls -l /customer/new` (empty file), `cat /new_customer/merakiplus/cfgbak/boot_ui.log`. Choose ORIGINAL → SAVE; the file is gone. `md5sum /customer/demo.sh /customer/config.json` unchanged. Restart with the flag set → merakiplus boots (DIAGNOSTICS: STARTED FROM = SLOT B, STOCK PROCESS = NONE).
- [ ] **N3 · `LD_LIBRARY_PATH` (closes the deviation in CHANGES-v6.2 item 2):** in a slot B boot: `tr '\0' '\n' </proc/$(pidof demo_v9)/environ | grep LD_LIBRARY`. If there is a prefix in front of `/new_customer/libs`, that is what merakiplus preserves. Then run with `MERAKIPLUS_SWITCH_DRY=1`, tap SWITCH NOW and read the path in `boot_ui.log`.
- [ ] **N4 · Runtime switch B→A:** in a slot B boot, HOME = READY: SYSTEM → SWITCH TO ORIGINAL NOW → SWITCH NOW. Expect *"SWITCHING TO ORIGINAL"*, then the stock UI without a restart. `for p in $(pidof demo_v9); do readlink /proc/$p/exe; done` = only `/customer/demo_v9`; `ls /proc/$(pidof demo_v9)/fd` contains no fd inherited from merakiplus; **touch works in the stock UI** (that is the grab test); `cat /new_customer/merakiplus/cfgbak/boot_ui.log`. Record the UART during the switch: expect the stock UI's boot push (`AA 55 06 01 1E …`), no half frames from merakiplus.
- [ ] **N5 · The watchdog race:** repeat N4 ten times (back with the gesture). Two `demo_v9` must never be running afterwards, and merakiplus must not come back by itself.
- [ ] **N6 · Back with the gesture:** 3 × 1.6 s at (75,75) (from v6.12: a 5 s center hold) → merakiplus; DIAGNOSTICS shows the event from `/tmp/switcher.log`.
- [ ] **N7 · Known consequence:** after N4, `kill $(pidof demo_v9)` → the watchdog starts **merakiplus** (slot B), not the stock UI. Expected.
- [ ] **N8 · (v6.8: dropped — the REVERT button has been removed.)** Replaced by: HOME has no touch zone where the pill was (taps on 150..330 × 404..480 do nothing); *SWITCH TO ORIGINAL NOW* starts the stock UI in a slot B boot and is a clean exit in run.sh mode.
- [ ] **N9 · Exit reminder:** run.sh mode, save one setting, *SWITCH TO ORIGINAL NOW* → SWITCH NOW (v6.8; before: REVERT) → the reminder is shown; EXIT → the stock UI shows the old value; `kill $(pidof demo_v9)` → the new one. Repeat without SAVE: no reminder. Repeat after a machine restart (the file is still there): no reminder.
- [ ] **N10 · Blocking:** start steam, try SWITCH NOW → *"WAIT UNTIL THE ACTIVITY ENDS"*, merakiplus keeps running.
- [ ] **N11 · Error path (optional, if you dare):** `chmod -x /customer/demo_v9` makes the row locked; restore the x bit right afterwards.

**O. Language and product name (v6.3)** — none of it has been run on the device
- [ ] **O1 · Default:** the first start without `/new_customer/merakiplus/lang` (`ls` shows that the file does not exist): the splash shows the wordmark MERAKI with a "+" badge, *"ADVANCED BREWING INTERFACE"* and *"V6.4 · CONFIG LOADED"*; HOME is in English with the title line MERAKI+ above the phase chip; the bottom of DIAGNOSTICS (ABOUT): *"ENGLISH · DEFAULT (NO FILE)"*.
- [ ] **O2 · Live switch:** SYSTEM → UI LANGUAGE (top row) → **+**: the whole screen switches to Danish immediately (title UI-SPROG, buttons ANNULLER / GEM) → GEM → toast *"SPROG GEMT · DANSK"*. No restart. `cat /new_customer/merakiplus/lang` = `da`, `wc -c` = 2. Go through all 5 pages in Danish; fb captures of them + `check_circle.py` = 0 violations.
- [ ] **O3 · Independent of the stock UI:** after O2 `md5sum /customer/config.json` is unchanged, `grep language /customer/config.json` unchanged, `cfgbak/config_writes.log` has no new line, there is no new `RESTART_NEEDED`, and the DIAGNOSTICS CMD6 counters are the same (no frame sent).
- [ ] **O4 · Remembered:** exit (v6.8: *SWITCH TO ORIGINAL NOW*) and start again (or restart in a slot B boot): Danish from the first frame (splash: *"AVANCERET BRYGGE-INTERFACE"*); DIAGNOSTICS / ABOUT (OM): *"DANSK · FRA FIL"*.
- [ ] **O5 · Fallback:** `echo de > /new_customer/merakiplus/lang` → start: English, ABOUT shows *"ENGLISH · UNKNOWN FILE VALUE"* in amber. `rm` the file → *"DEFAULT (NO FILE)"*. `echo da > …` (with a newline) → Danish.
- [ ] **O6 · CANCEL:** UI LANGUAGE → change → CANCEL: the language goes back, the file is unchanged.
- [ ] **O7 · STOCK LANG. is something else:** STOCK LANG. → GERMAN → SAVE only changes `language` in the config (as in E); Meraki+ stays in its own language. Set it back.
- [ ] **O8 · Product name:** go through fb captures of all pages in both languages: the name is Meraki+ everywhere, and there is no generation designation anywhere.
- [ ] **O9 · °F layout (tightened in v6.3):** TEMP UNIT = °F → in the BREW and STEAM · WATER lists the label and value do not touch each other; the HOME sub-line during steam is not clipped. Set back to °C.
- [ ] **O10 · Frame time:** read FRAME MS MAX right after a language switch (full redraw + new glyphs in the cache).

**P. v6.4** — the device binary does the same as v6.3; only two texts are new
- [ ] **P1 · Version:** the splash and DIAGNOSTICS show *V6.5* (O1 mentions v6.4's string; from v6.5 it says *V6.5 · CONFIG LOADED*). `strings /new_customer/demo_v9 | grep -c MERAKIPLUS_SIM` = 0 (it is the device binary, not the SIM build).
- [ ] **P2 · Toast:** provoke a SAVE without an ACK if it can be done safely (otherwise skip): the toast is *"SAVE FAILED · NO ACK · NOT SAVED"*, and the config is unchanged.
- [ ] **P3 · (v6.4; the decision was taken in v6.5 — use it as a counter-test, Q6) Measure before the decision on the boot push (CHANGES-v6.4 item 6):** a cold start in a slot B boot; before anything has been saved or brewed: read words 4, 11, 14, 17, 23, 25 and 112–114 (`sdump.py`, or HOME: *BREW TEMP SET TO …*) and compare with the config. Expected without a boot push: the MCU's start values (word 11 = 920, word 114 = 50), not the config's (870 / 70).

**Q. v6.5: boot push and RX resync** — none of it has been run on the device
- [ ] **Q1 · Version and new cells:** the splash and DIAGNOSTICS show *V6.5*. DIAGNOSTICS: *RX CRC / RESYNC* = `0 / 0`; scroll down one row: *BOOT PUSH 1-30* = OK and *BOOT PUSH 112-114* = OK (green).
- [ ] **Q2 · run.sh mode, idle machine — record merakiplus's TX** (UART sniff or `strace -e write` on merakiplus's PID) from the start: after `aa 55 06 00 01 01 00 c5 5f` and `aa 55 06 01 01 00 00 c5 33` come **`aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 … 62 3e`** (67 bytes, sub1 len30) and **`aa 55 06 70 03 04 01 05 06 46 00 c7 02`** (sub112 len3) — with the baseline config. Each has one ACK (`aa 55 86 …` with the same payload). DIAGNOSTICS: CMD6 = `4 / 0 / 0 / 0`. Note the ACK latency for the 30-word block, and whether RTX is 0.
- [ ] **Q3 · The same block as the stock UI:** compare Q2's two frames byte for byte with the stock UI's own from test plan K (same config). If a word differs, PARITY §1.3–1.4 is incomplete: report back, and run with `MERAKIPLUS_NOBOOTPUSH=1` for now.
- [ ] **Q4 · No side effect in run.sh mode:** after Q2 WORK_STA is still `0101`, the boiler temperatures hold, RX OK increases continuously (no MCU reboot), WEIGHT on HOME is unchanged (no calibration), `MIRROR 112-114` = `0104 0605 70`.
- [ ] **Q5 · Slot B boot, cold start (what the fix is for):** set the flag, switch off and on. When HOME is shown: the sub-line does **not** say *BREW TEMP SET TO 92.0°*; `sdump.py` (or DIAGNOSTICS) shows word 11 = 870, word 4 = 185, word 14 = 3, word 17 = 400, word 23 = 360, word 25 = 650 and `MIRROR 112-114` = `0104 0605 70`; BOOT PUSH = OK / OK. The brew boiler heats towards 87.0 °C. Note work_sta during the heating — are there bits beyond `0101`?
- [ ] **Q6 · Counter-test (optional):** the same cold start with the v6.4 binary (`merakiplus_v6_4.bin`) — expected word 11 = 920 and word 114 = 50 (test plan P3). That is the difference v6.5 closes.
- [ ] **Q7 · The activity guard:** start a brew in **the stock UI**, and start `run.sh` while it runs. The brew continues to its target weight without interruption; DIAGNOSTICS: BOOT PUSH = SKIPPED / SKIPPED, CMD6 OK = 2. Repeat with steam (nozzle in milk): the steam does not stop when merakiplus starts.
- [ ] **Q8 · Config out of spec:** with a copy of the config where `brewTemper` = 1200 (`MERAKIPLUS_CONFIG=<copy>`): BOOT PUSH 1-30 = REJECTED, 112-114 = OK, BLOCK = 1, and word 11 on the controller is unchanged. Fix the value with a SAVE in merakiplus: BOOT PUSH 1-30 changes to OK.
- [ ] **Q9 · Disable:** `MERAKIPLUS_NOBOOTPUSH=1` → BOOT PUSH = OFF / OFF, CMD6 OK = 2 (the v6.4 behavior).
- [ ] **Q10 · RESYNC over a long session:** let merakiplus run through E + G + H and read *RX CRC / RESYNC* at the end. Expected `0 / 0`. If RESYNC is above 0, note both numbers, and whether LINK has been DOWN — after v6.5 a single count there must not cause OFFLINE.

**R. v6.6: phase semantics, clock and re-push** — none of it has been run on the device
- [ ] **R1 · Version and unchanged DIAGNOSTICS:** the splash and DIAGNOSTICS show *V6.6*. The boot push row is called *BOOT PUSH 1-30* / *BOOT PUSH 112-114* (OK / OK) — **not** *RE-PUSH …*. `strings /new_customer/demo_v9 | grep -c MERAKIPLUS_SIM` = 0.
- [ ] **R2 · Latched steam** (nozzle in cold milk; start with the STEAM button, **do not touch the wand**): the sub-line briefly shows *STARTING STEAM…*; within about 0.3 s the chip is **STEAM·AUTO**, the label *MILK · TARGET 65.0°*, the sub-line *AUTO-STOP AT 65.0° · OR STOP*, and STOP STEAM is visible. The chip must never show *STEAM·WAND*. The steam stops by itself at about 65 °C; CMD6 OK has only increased by 1 (the start write) — merakiplus has not sent any stop. Then DONE with *DONE IN … S · RISES ≈4° MORE*. Note how high the hero gets over the next ~12 s (STEAMLIVE: 68.9 °C).
- [ ] **R3 · Manual STOP during latched steam:** start again, tap STOP STEAM at about 40 °C: toast *STOP SENT · WORD 24 = 0*; the chip stays *STEAM·AUTO* until the steam has stopped (no *STEAM·WAND* flash); then DONE.
- [ ] **R4 · Wand / flush alone** (do not touch the screen; open the wand for 3–4 s): the chip is **STEAM·WAND**, the label *MILK PROBE · NO TARGET*, the sub-line *NO AUTO-STOP · RELEASE WAND*; **no STOP button** — the four buttons are there, STEAM highlighted; the CMD6 counters stand still. Release: DONE with *DONE IN … S* (without a note). On DIAGNOSTICS during the flush: PHASE = *STEAM·WAND*, WORK_STA = `4121` (possibly with pulses).
- [ ] **R5 · Wand above the target:** hold the wand open towards the probe until the hero shows above 65 °C: the steam continues (as in STEAMLIVE §6.2); merakiplus still shows *STEAM·WAND*, no DONE, no frame. The start buttons answer *ACTIVITY IN PROGRESS*.
- [ ] **R6 · Concurrent phases:** start a brew; open the wand for about 3 s in the middle of the extraction. Chip **EXTRACTION**, right below it the badge **STEAM·WAND**; the hero is the weight and keeps rising; the stat row is *TIME · ≈BAR · EST. · MILK*; BREW **and** STEAM are highlighted; no STOP button. Release: the badge disappears, FLOW is back, the brew runs to its target, and *DONE IN …* is the whole brew time. Note WORK_STA if DIAGNOSTICS can be reached (`5327`).
- [ ] **R7 · No false re-push:** after the whole session (E + G + H + R2–R6): the DIAGNOSTICS row is still called *BOOT PUSH 1-30*. If it says *RE-PUSH n · 1-30*: note the number, the cells' outcome, WORK_STA, and what happened right before — it means an MCU restart, or that the controller also sends `0x89` outside its startup (not seen in any capture).
- [ ] **R8 · Slot B boot with the wand open (optional):** a cold start in a slot B boot, hold the wand open while merakiplus starts: BOOT PUSH = SKIPPED (the guard). Release the wand: within about 5 s the row is called *RE-PUSH 1 · 1-30* with OK / OK, word 11 = 870, and `MIRROR 112-114` = `0104 0605 70`. The sub-line on HOME no longer says *BREW TEMP SET TO 92.0°*.
- [ ] **R9 · Long running (the clock):** cannot be forced on the device. Let merakiplus run as long as possible in a slot B boot (preferably more than 25 days); note `cat /proc/uptime`, and that HOME still updates, RX OK increases, FRAME MS is as before, and keep-awake keeps the machine awake. **v6.5 and older must not be used for a permanent slot B boot.**
- [ ] **R10 · Counter-test (optional):** R2 with `merakiplus_v6_5.bin` shows the chip *STEAM* and the sub-line *STEAM ARMED · OPEN THE STEAM VALVE*; R4 shows *STEAM* with *AUTO-STOP AT 65.0° · OR STOP* and a STOP button that does nothing.

**I. Wrap-up**
- [ ] Exit (v6.8: *SWITCH TO ORIGINAL NOW*), `pidof demo_v9` unchanged (unless F/K/L restarted it), config md5 = baseline, PWM duty = 37, `/tmp` cleaned up, `cfgbak/` reviewed, `RESTART_NEEDED` removed after the last demo_v9 restart.


### S · Manual STOP (v6.13) — run once the machine is on and idle

Run `python3 verify_stop_v613.py --plan` (no contact with the machine) and then `python3 verify_stop_v613.py`.

What the script does:
* puts the v6.13 binary and a copy of run.sh in `/tmp/merakiplus_s/` and starts merakiplus with `MERAKIPLUS_NOBOOTPUSH=1` and `MERAKIPLUS_NONET=1`; the stock UI is SIGSTOPped
* attaches strace (passive) to merakiplus's `read`/`write` of `/dev/ttyS1`
* asks a person at the machine to hold the WATER icon and tap **STOP WATER** after about 2 s (no touch injection; merakiplus grabs touch as always)
* stops merakiplus, so run.sh's trap SIGCONTs the stock UI
* fetches the log and deletes `/tmp/merakiplus_s/`
* afterwards decides the checks in [§18.1](#181-stop-frames--byte-for-byte-with-evidence): the stop frame byte for byte, ACK, work_sta bits cleared, flow dropped, no other writes, stock PID unchanged, config and flag unchanged

There is no reboot and no write to the config, flag or slots. A safety net stops merakiplus after 240 s. The result ends up in `stop_v613_ondevice/<activity>_<time>/PROOF.md`.

- [ ] **S1 · water** (cup under the spout): `verify_stop_v613.py`. Expected: `aa 55 06 10 01 00 00 c0 0f` → ACK ~20–280 ms → work_sta `030B` → `0101` (bit 3 cleared) within ~0.5 s, flow (word 52) → 0, toast *WATER STOPPED*.
- [ ] **S2 · grind** (optional; beans + cup): `--activity grind`. Expected: `aa 55 06 06 01 00 00 c4 47` → bit 6 cleared, grinder current (word 58) drops.
- [ ] **S3 · brew** (optional; portafilter with coffee): `--activity brew`. Expected: `aa 55 06 16 01 00 00 c0 87` → bits 1+2+9+12 cleared, valve pulse `0105` for ~0.2 s and the pump stops.

### T · The power button (v6.14) — run once the machine is on and idle

Run `python3 verify_power_v614.py --plan` (no contact) and then `python3 verify_power_v614.py`. Requires `merakiplus_v6_14.bin` (built by `build_v614.sh`).

What the script does:
* puts `merakiplus_v6_14.bin` and a copy of run.sh in `/tmp/merakiplus_p/` and starts merakiplus with `MERAKIPLUS_NONET=1`; the stock UI is SIGSTOPped. **Boot/re-push is ON** (the MCU wakes with a reset and must get the config back)
* notes the open input nodes (expected `/dev/input/event0` + `/dev/input/event1`) and attaches strace (passive) to `/dev/ttyS1`
* asks a person at the machine to **hold the power button for ~2 s** (TURNING OFF… → OFF), reads `pwm2/enable`, and after ≥ 10 s to **hold it again** (TURNING ON… → HOME)
* stops merakiplus (run.sh SIGCONTs the stock UI; merakiplus turns the backlight on at exit), fetches the log and deletes `/tmp/merakiplus_p/`

No reboot, no config/flag/slot writes. Safety net: 300 s. Result: `power_v614_ondevice/<time>/PROOF.md`. *(The actual on-device run found that the hardware cuts the display's power — see [power.md](../power.md).)*

- [ ] **T1 · power off:** cmd8 (alarm 0/0/0) → ≥ 80 ms → `aa 55 06 00 02 00 00 00 00 56 94` → ACK; word 0 = 0; the controller silent ≥ 3 s; `pwm2/enable = 0`; no keep-awake in the meantime.
- [ ] **T2 · power on:** the controller wakes (the 0x89 round); `aa 55 06 00 01 01 00 c5 5f` + `aa 55 06 01 01 00 00 c5 33` → ACK; word 0 = 1; re-push → words 11/14/25/114 = config.
- [ ] **T3 · afterwards:** stock PID unchanged and running, backlight as before, config md5 + flag unchanged, display uptime growing.
- [ ] **T4 (optional, manual):** hold the button during a brew → toast *STOP THE ACTIVITY FIRST · THEN OFF*, nothing is sent; after the brew a new hold powers off.

---

## 12. Files

*(Files in the author's working directory; in this repository the source is `src/merakiplus.c` and the shipped binaries are in `bin/` — see [bin/README.md](../../bin/README.md).)*

| File | Contents |
|---|---|
| `merakiplus.c` / `merakiplus_v6` | **v6.13** (manual STOP) — the source and binary md5s are in [CHANGES-v6.13](#changes-v613) item 7 |
| `merakiplus_v6_12.c` / `merakiplus_v6_12.bin` | **new (v6.13):** v6.12 kept unchanged for comparison/rollback — source `e9eea8caebfa5d832884d96996dbd2c7`, binary `725386ccadade594275f20cc998885eb` (= the deployed v6.11/v6.12 binary). SIM: `merakiplus_v6_12_sim` / `merakiplus_v6_12_sim_arm` |
| `verify_stop_v613.py` | **new (v6.13):** on-device proof of STOP (water; `--activity grind|brew`): run.sh mode in `/tmp`, passive strace, a physical tap; `--plan`, `--selftest`, `--decode` |
| `round_v6/v612_compare.py` | **new (v6.13):** previews against v6.12 — only HOME's action row, sub-line and toast may differ |
| `merakiplus.c` (history) | all of **v6.9** (7095 lines), md5 `440be5124c0d986ec23e4c3b08f84149` |
| `merakiplus_v6` | static ARM binary **v6.9** (the device), md5 **`2dce4eba0b4ce68dcd0369e955cf3b77`** (reproducible with zig 0.17.1 in the same directory and with the default cache) |
| `merakiplus_v6_9wip.c` / `merakiplus_v6_9wip.bin` | **new (v6.9):** the state at the start of the v6.9 round (v6.8 + the predecessor's partial conditioning) — source `048a40ea7894dab5b57a58f6d9af8ead`, binary `bcb1b50c913700c64a6cfc39bb5a5776`. Must not be installed |
| `merakiplus_v6_8wip.c` / `merakiplus_v6_8wip.bin` | v6.8 (network guard, without the REVERT button) — source `5cdcc14b0503c67b939e27b6579655ff`, binary `75887fb154ffef20cc72696064e16c39`; reference for the build and preview comparison |
| `merakiplus_v6_6.c` / `merakiplus_v6_6.bin` | v6.6 — source `3369e228fa36c42988b7f0421ff6eef4`, binary `c22153af54069d9d1fcbe43fcb118c99` |
| `round_v6/fbcrop.py` | **new (v6.9):** crops of fb dumps/PNGs in a grid (chart before/after sheets) |
| `sim_scenarios/ui_calm_graph.json` | **new (v6.9):** graph + weight through idle with pulses, grind, brew, idle after a brew, steam and tare — run against v6.8 and v6.9 (`sim_e2e.py --only calm`) |
| `merakiplus_v6_sim` / `merakiplus_v6_sim_arm` | the SIM build for the host (the one `sim/run_e2e.py` starts; the md5 changes on every link) / static ARMv7 (the contract's build line; v6.9: `cbebc8b128f413c1d40a83de2bd6343a`; previous md5 `61d69f8489634a16739da14ada213bf3`). **Must not go on the device** |
| `merakiplus_v6_5.c` / `merakiplus_v6_5.bin` | **new:** v6.5 kept unchanged for comparison/rollback — source md5 `f6e502b3d4cb7d212d1a1786520b2de4`, binary md5 `e84785d514c11ef88705ac97240b98c1`. **Not for a permanent slot B boot** (the 32-bit clock, CHANGES-v6.6 item 3) |
| `merakiplus_v6_4.c` / `merakiplus_v6_4.bin` | **new:** v6.4 kept unchanged for comparison/rollback — source md5 `5bdaf341f77dcec3554707cf6cd9df70`, binary md5 `1fe0b5b8c3b24abf0cadcde7b099076a` |
| `merakiplus_v6_3.c` / `merakiplus_v6_3.bin` | **new:** v6.3 kept unchanged for comparison/rollback — source md5 `4ac62cd08de36f7fea026eec5a3494ab`, binary md5 `5815176d0e766c7a6532b88014e932fb` |
| `build_sim.sh` | builds the device and SIM binaries; warnings with a fresh cache; **v6.6:** comparison of the device build with v6.5 per function/data object against a list of intended changes (time type differences separately); a file access review; exit codes; a smoke test of the ARM SIM binary and **the whole self-test as a 32-bit ARM binary** in a local ARMv7 container → `round_v6/sim_build_check.txt`, `round_v6/selftest_arm32.txt` |
| `sim_e2e.py` | runs `sim/selftest.py`, all scenarios (EN + DA), merakiplus's own touch scenarios, the runtime switch without dry run, the boot push against the simulated MCU, the RX stall repro and the targeted scenarios and — **v6.6** — the phase semantics line by line against the LIVE recording (`phases`), the clock wraparound in real time (`wrap`) and MCU restart → re-push (`repush`); notes for each part whether the test work stream's files changed along the way; collects everything in `sim/reports/v6.6/` |
| `sim_ui_profile.json` | v6.4's 22 overrides. **Out of use from v6.5:** the test work stream's scenarios now assert merakiplus's behavior directly, and the runner has its own (empty) default profile |
| `sim_scenarios/` | 4 scenarios with real FIFO touch on merakiplus's own coordinates (v6.4: navigation + stepper + SAVE + hold-to-start; the deviating rows' real frames; auto-wake; blocked/allowed switch) + **new (v6.6):** `ui_replay_steam_live_x4.json` — the LIVE recording (steam with auto-stop, grind, brew, wand flush during a brew) played back at 4× |
| `merakiplus_v6_2.c` / `merakiplus_v6_2.bin` | **new:** v6.2 kept unchanged for comparison/rollback — source md5 `c2dc02caf271097e6b4e65e598db0af1`, binary md5 `0bb93da0ed0824c424f1e16530f07022` |
| `merakiplus_v6_1.c` / `merakiplus_v6_1.bin` | v6.1 kept unchanged — source md5 `7ff7d9d497e3c74cc2a4ac477ac01728`, binary md5 `4d927802cbeb69cc949f463021b29b99` (v6.0 was `90bcf094…`) |
| `boot/` | the boot work stream's deliverable (switcher daemon, `boot_install.py`, `BOOT.md`). **Not touched by v6.2, v6.3, v6.4, v6.5 or v6.6.** |
| `parity_check.py` | **new:** SET[] ↔ `parity_spec.json` (limits, steps, tiers, words); exit 1 on an unjustified deviation |
| `cjson_canon.py` | proves that merakiplus's config file is byte-identical to `cJSON_Print` |
| `i18n_appendix.py` | **new (v6.3):** generates Appendix A from the binary's string table (`--write`) and fails if the appendix is not in sync (`--check`, run by `build_previews.sh`) |
| `round_v6/` | **v6.6:** 62 scenarios per language; new files `v65_compare.txt`, `mutation_check.txt`, `selftest_arm32.txt` · **English:** `.bin` + `.png`, `contact_sheet.png` · **Danish:** `da/` with the same + `contact_sheet_da.png` · `contact_sheet_en_da.png` (EN \| DA side by side) · `selftest.txt` + **`selftest_da.txt`**, `parity_check.txt`, `config_format_check.txt`, `check_circle.txt` + **`check_circle_da.txt`**, `bench.txt`, `boot_check.txt`, **`i18n_check.txt`**, **`strings.tsv`** (the string table: id, class, EN, DA), `build_previews.sh`, `fb2png.py`; `_preview_root/` is the mock root, `_preview_lang` the language file in the preview run |
| `REVERT.md` | updated: boot push semantics, restart before stock settings, frame policy, flag, runtime switch, `boot_ui.log` + **v6.3:** the language file `lang` + **v6.4:** nothing new on the device; back to v6.3 + **v6.5:** boot push at startup, `MERAKIPLUS_NOBOOTPUSH`, back to v6.4 + **v6.6:** re-push after an MCU restart, back to v6.5 |
| run.sh | **unchanged** (md5 `3d799859549d2b8ba32245453022c20d`) |

---

## 13. SIM build and E2E (v6.4)

From v6.4 the UI can run **without hardware** against the test work stream's simulated MCU ([sim/](../../sim/), contract: [sim/CONTRACT.md](../../sim/CONTRACT.md)). It is the same source file; `-DMERAKIPLUS_SIM` only swaps out the three file nodes and adds a test surface on top. The device build contains none of it (CHANGES-v6.4 item 2).

### 13.1 Build and run

```sh
cd workspace
sh build_sim.sh                      # device binary + SIM (host + static ARM) + all build checks -> round_v6/sim_build_check.txt
python3 sim_e2e.py                   # the whole E2E verification -> sim/reports/v6.5/SUMMARY.md (about 30 min)
python3 sim_e2e.py --only bootpush   # v6.5: only the boot push against the simulated MCU (also: rxstall, targeted)
python3 ../../sim/run_e2e.py --all   # only the scenarios (finds merakiplus_v6_sim itself); --ui-lang da for Danish
```

| Binary | Built with | Used for |
|---|---|---|
| `merakiplus_v6` | `zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2` | the device (unchanged contract) |
| `merakiplus_v6_sim` | `zig cc -DMERAKIPLUS_SIM -O2 … -lm` (the host; macOS has no static libc) | **the one `run_e2e.py` starts** (auto-detected) |
| `merakiplus_v6_sim_arm` | as the device + `-DMERAKIPLUS_SIM` (static ARMv7) | the contract's build line; smoke-tested in a local ARMv7 container |
| `/tmp/merakiplus_v6_preview` | `-DMERAKIPLUS_PREVIEW` (also together with `-DMERAKIPLUS_SIM`) | previews + self-test, as before |

### 13.2 HAL: what the SIM build swaps out

| On the device | In the SIM | Environment |
|---|---|---|
| `/dev/ttyS1` | a pty slave from `mcu_sim.py`; the same `uart_open()`/`uart_flush()` (raw 8N1, non-blocking) | `MERAKIPLUS_SIM_TTY` (required) |
| `/dev/fb0` + page flip | one 480×480 BGRA buffer in memory. `fb_present()` copies **only the rects the compositor reports as changed** — like the device — and the buffer is written to a file (tmp + rename; at most 10 Hz, always on a page/phase/toast change) | `MERAKIPLUS_SIM_FB` (required) |
| `/dev/input/event0` + `EVIOCGRAB` | a FIFO with raw 16-byte evdev records (the same as `tinj_hold`). No grab, no `EVIOCGABS`. Records are processed according to their **timestamps**, and the decoding is the device's own `inp_poll()` | `MERAKIPLUS_SIM_TOUCH` |
| `/customer`, `/new_customer`, `/proc`, `/tmp` | under the sandbox root (`rp()`); the config, backup directory and language file follow the root unless their own variable is set | `MERAKIPLUS_SIM_ROOT` (required; `/` is rejected), `MERAKIPLUS_CONFIG`, `MERAKIPLUS_BAKDIR`, `MERAKIPLUS_LANGFILE` |
| backlight sysfs | never written; the preview value is in the state JSON | — |
| `sync()` after a flag change | left out (no global sync on the host) | — |
| — | state JSONL: one line per rendered frame if the contents have changed; otherwise at least every 90 ms | `MERAKIPLUS_SIM_STATE` (required) |
| — | a ctrl socket with test hooks (§13.4) | `MERAKIPLUS_SIM_CTRL` |

Exit codes: 0 normal · 2 environment error · 3 the pty cannot be opened · 4 fb/touch cannot be opened · 130 SIGINT. `MERAKIPLUS_SIM_NOBOOTPUSH=1` disables the boot push; `MERAKIPLUS_SIM_BOOTPUSH_GUARD=1` (v6.5) gives the SIM build the device's activity guard (skip instead of defer); `MERAKIPLUS_SIM_NOPROVISION=1` leaves the sandbox as the runner made it (then BOOT UI and SWITCH NOW are locked).

**File access:** `build_sim.sh` reviews all file calls in the SIM build after the preprocessor: the only fixed path without the sandbox root is `/dev/null`, and there is no reference to `/dev/fb0`, `/dev/input`, `/dev/ttyS1`, `/sys/class`, `EVIOCGRAB` or `FBIO*`.

### 13.3 State JSON

One line per rendered frame when something in the line has changed since the previous one (the RX counter `crc_ok` and the frame time fields do not count as a change), otherwise at least every 90 ms. A register value the UI has had on a frame is therefore always in the stream.

The contract's fields (`t_ms`, `view`, `page`, `phase`, `work_sta`, `regs`, `lang`, `boot_flag`, `crc_ok`, `crc_bad`, `cmd6`, `frame_ms`, `toast`, `config_oor`, `config_missing`, `link_up`, `save`, `activity_pending`, `regs_s`, `boot_pushed`, `client`, `version`) plus:

| Field | Contents |
|---|---|
| `phase` / `phase_ui` | `phase_raw()` (never `DONE`) / what the chip shows (`DONE` for 8 s after an activity) |
| `page` / `view_raw` / `tab` | `home` · `settings` · `detail` · `sys` · `diag` · `bye` / `splash` · `main` · `detail` · `sys` · `bye` / `home` · `brew` · `steam` · `system` · `diag` |
| `shown` | what the screen shows: HOME = chip, hero, unit, label, sub-line and the three stat pairs; editor = label, value, saved value, range, locked/saving |
| `toast` | `text` = the table's **Danish** text (the assertion language), `shown` = the text on the screen, `id` = string id, `kind` = ok/err/warn/info |
| `lang` / `ui_lang` / `lang_src` | `config.language` (the stock UI's language) / Meraki+'s own language (`en`/`da`) / where it comes from |
| `boot_flag`, `next_boot`, `boot_lock`, `exe_slot`, `self_exe` | the flag (read fresh), what the next restart gives, the BOOT UI lock, which slot merakiplus runs from |
| `switch` | `mode` (launch/cont), `locked`, `dry`, `bye`, `stock_n`, `switcher_pid`, `child`, `last_evt` |
| `restart_needed` | `file`, `pending` (is a stock UI with an old mirror waiting?), `stamp` (`boot_id=… mono_s=…`) |
| `cmd6` + `blok`, `wq_n` | ok / retx / fail / block; `blok` = frames rejected by the frame policy (DIAGNOSTICS "BLOCK") |
| `boot_push` | `sub1_len30` and `sub112_len3`: ok / noack / blocked / nocfg / skipped (v6.5: the activity guard); `runs` |
| `rx_resync` | **v6.5:** discarded header candidates (impossible length, watchdog, overflow) — DIAGNOSTICS "RESYNC". `crc_bad` is, as before, CRC errors on whole frames |
| `phase_main` / `phases` | **v6.6:** HOME's main activity (grind / brew / water take precedence over steam) / all active ones, the main activity first — e.g. `["EXT","STEAM"]` at work_sta `5327`. `phase` is unchanged (`phase_raw()`: steam first) |
| `steam` | **v6.6:** `on`, `kind` (none / unknown / auto / wand), `last_kind`, `concurrent`, `wand_sw` (word 44 bit0 or word 46), `wand_seen`, `ord24`, `target` (word 25), `milk` (word 48), `stop_btn` (is the STOP button shown?) |
| `shown.badge` | **v6.6:** the steam badge during concurrent phases (otherwise `null`); `shown.chip` is *STEAM·AUTO* / *STEAM·WAND* during steam |
| `repush` | **v6.6:** `count`, `ver` (triggered by `0x89`), `defaults` (triggered by the start value signature), `last`, `armed`, `ver_reqs` (number of `0x89` received), `defaults_sig` |
| `save` | `state` = pending / ok / fail / rejected / unchanged, `param`, `value` — also for a SAVE started by touch |
| `frame_ms`, `frame_ms_max`, `frame_ms_avg`, `frame_ms_peak`, `frames` | the last frame, max since the previous line, average and max over the whole run, number of rendered frames |

### 13.4 Test hooks (ctrl socket)

The hooks call the **same functions as a tap** and bypass neither the guards nor the frame policy:

| Hook | What it does in the UI |
|---|---|
| `action {name}` | `act_request()` — the button without the hold time. All guards apply. `steam_stop` = STOP STEAM |
| `save {param,value}` | opens the row's editor, sets the value, `save_begin()`. Not exposed/locked: *LOCKED*. Outside the range or step: *OUT OF LIMITS* |
| `touch {x,y,ms}` | the same record sequence as `tinj_hold`, put in the touch queue |
| `page {name,…}` | navigation; `detail` takes `param`, `settings` takes `tab`, `sys` takes `x` |
| `boot_flag {value}` | BOOT UI → choose → SAVE (`sys_confirm` → `boot_flag_set` with the guard) |
| `runtime_switch` | SWITCH TO ORIGINAL NOW → SWITCH NOW (`sys_confirm` → `do_switch`). The dry run shows the goodbye screen for 1 s |
| `raw_write {sub,words}` | emulates a **stock UI write**. The only relaxation of the frame policy: word1 ∈ {0,1}. word1=1 pauses merakiplus's keep-awake until a `raw_write` with word1=0 |
| `config_reload` | the language file + config are read again, and the boot push is repeated |
| `layout` | the real touch targets in the current view (for e.g. `sim/play.py`). **v6.8:** `revert` is gone (the button has been removed) |
| `net {name}` | **v6.7:** the Wi-Fi MODEL (the SIM build has no real stack): `kill` (wpa_supplicant + udhcpc die, the IP/route remain), `ip_drop`, `if_down` (the IP remains, the routes are gone), `cold` (no wlan0), `ap_down` / `ap_up`, `status`. The network guard reacts as on the device; `MERAKIPLUS_SIM_NET=cold\|off` at start. State JSON: `net{state, iface, ifup, wpa_pid, dhcp_pid, carrier, ip, gw, heals, boots, heal_ms, init_runs, wpa_starts, dhcp_starts, conf_writes, fails}` |
| `ping`, `state_now`, `fb_dump {path}`, `exit {code}` | as in the contract |

If the write queue is busy at the moment an `action`/`save` hook arrives (e.g. with a keep-awake), the hook waits up to 600 ms — like a tap a moment later.

### 13.5 What the SIM build does that the device does not

| | SIM build | Device build |
|---|---|---|
| **Boot push** `sub1 len30` + `sub112 len3` | at startup **and** on `config_reload`; an activity **defers** it (contract behavior, unchanged from v6.4). `MERAKIPLUS_SIM_BOOTPUSH_GUARD=1` gives the device's guard | **v6.5:** at startup; if an activity is already running, **it is skipped** (activity guard, CHANGES-v6.5 item 2). (v6.4: no boot push) |
| Dry run of SWITCH NOW | goodbye screen for 1 s + toast | log + toast only |
| `sync()`, backlight, `EVIOCGRAB` | no | yes |

Everything else — the main loop, poll rotation, RX parser with the resync watchdog, keep-awake, write queue (block limit 30 words in both from v6.5), ACK/retransmit, frame policy, the boot push's word layout and state machine, the save path, activity guards, boot/switch logic, drawing code — is the same code.

### 13.6 What the E2E environment says nothing about

* **The device's frame time.** The SIM figures were measured on the host and cannot be scaled up to the device (CHANGES-v6.4 item 7). The device's must be measured on the device (DIAGNOSTICS shows it).
* **Pixel exactness.** An ARMv7 compilation of the same code does not give bit-identical images to the host's (CHANGES-v6.4 item 6).
* fb mmap/page flip, touch electronics, OTA and the real MCU's timing — see [sim/README.md](../../sim/README.md) §6.

---

## 14. Display conditioning and stable weight (v6.9)

**The problem:** the graph and the numbers on HOME jumped around. There were three causes:
* **Word 51 (pressure/pump drive)** has ADC noise of ±9..±40. Outside an activity it has
  **maintenance pulses**: small heating cycles of 400–900 for 0.5–1.5 s and deadhead pulses of
  3000–16670 for 0.1–0.25 s (BREW.md §5–7, STEAMLIVE.md §5.3). v6.8 switched the chart to "active" on
  a single sample ≥ 40 and scaled to the maximum. Every pulse at idle therefore became a spike that
  flattened the rest of the curve.
* **The weight** (user complaint: *periodic weight spikes, even when the machine is not touched*).
  The graph chose the weight word per sample based on the phase
  (`phase_main()==GRIND ? word 38 : word 39`), and the hero number based on the displayed phase. One
  wrong work_sta sample, a link gap or a single bit-6 sample therefore put a different weight into
  the curve, e.g. the grinder weight 18.6 g in the middle of the cup's 36.7 g. On top of that there
  is cup noise of ±0.1–0.2 g at idle.
* **Words 38/39 are signed** (tare `0xFFFD` = −0.3 g). A filter on the unsigned words smears the tare
  towards 6553 g; the sim's demo "calm" had exactly that bug. The UI filters on int16.

**The principle:** only what is **drawn** changes. `reg[]`, the ring buffer (`g_p/g_w/g_m`), the
state JSON's `regs`/`regs_s`, all writes, the frame policy, auto-stop expectations, guards,
thresholds, the phase logic and the DIAGNOSTICS counters all still read the raw values. The build
check proves it: 29 control/protocol fragments are token-identical to v6.8 (`sim_build_check.txt`).

### 14.1 What is drawn

| Element | Conditioning | Constant (`-D…` at build time · ctrl `cond` in the SIM) |
|---|---|---|
| Chart series (pressure, weight, milk) | per sample (10 Hz) median of the last 3 → EMA | `CD_A_CHART` 0.18 (time constant 0.5 s) · `CD_MED` 3 |
| Chart pressure: pulse gate | if median-3 jumps to > 2 × displayed + 400, the displayed value is held for up to 3 samples; if the jump lasts longer, it is followed | `CD_GATE` 3 (0 = off) |
| Chart on activity change | the next 2 samples follow median-3 directly (no EMA tail; a pulse sample right at the change does not get through) | `CD_FAST` 2 |
| Chart weight on source change and cup off/on | source change 38 ↔ 39: the weight filter restarts on the new word (no history from the other word — otherwise a flash of the old weight). A median jump > 2.0 g in one sample = cup off/on → snap (noise ±0.2 g; a brew rises about 0.2 g/sample) | `CD_W_JUMP` 20 |
| Chart scale | top = max(target/floor × 1.08, **p90 of the window's ACTIVE raw samples** × 1.15). Raw samples above the top are drawn as small ticks just below the ceiling | `CD_PCT` 90 · `CD_HEAD` 1.15 · `scale` = `pct` / `hold` / `max` (v6.8) |
| Pressure at idle | only drawn for samples during brew/pre-infusion/water (`g_a`). Outside the activity word 51 is maintenance, and the pulses are shown as small ticks on the baseline. The chart stays "active" at idle as long as the 40 s window contains a brew/water run | — |
| Hero (weight / milk / boiler) + gauge + BAR field | median-3 + EMA at 10 Hz; **snap to raw** on a phase change (incl. DONE), source change, setpoint/target change, a median jump > 15.0 and after > 0.5 s without an update | `CD_A_HERO` 0.25 |
| Weight (hero, chart, stat field) | **dead band**: \|weight\| < 0.25 g (\|word\| < 2.5) is shown as 0 | `CD_WDB` 2.5 |

**Why p90 only over the active samples:** a p90 over the whole 40 s window is 0 during the first
~4 s of an extraction, because the window is mostly idle. The curve would be squashed against the
ceiling. p90 tolerates up to 10 % of the active samples being pulses. If there are more, they are
part of the signal and are included in the scale.

### 14.2 Stable weight source (latch)

* The default is the **brew weight (word 39)**.
* The **grinder weight (word 38)** is only used while a grind *is* in progress (work_sta bit 6), and
  while its DONE screen is shown for 8 s. The latter only applies if the source was already latched
  to 38 by a real grind.
* A switch requires the wanted source to have been stable for **≥ 300 ms** (`WSRC_DEB_MS`).
  Maintenance bits (0x0400/0x0800/0x2000/0x0C00/0x2800), a link gap or a single deviating work_sta
  sample never switch it.
* **Exception:** if the UI itself started the activity (`act_request`, < 4 s ago) and the activity's
  bits are set, it switches immediately. The activity is real, and the hero then shows the tare of
  0.0 g instead of the cup's weight. If a grind starts *without* the UI (physically), the hero shows
  the latched source for the first 300–400 ms.
* The graph and the hero use the same source. State JSON: `weight:{src, switches, shown}`.

### 14.3 DIAGNOSTICS → GRAPH (smooth / raw)

DIAGNOSTICS has the row **GRAPH · TAP TO TOGGLE**. The default is **SMOOTH · MEDIAN 3 + EMA**; a
tap gives **RAW · UNFILTERED**. RAW is v6.8's series and max scale, without dead band and gate, and
the chart is labelled **RAW** in amber in the right corner. The choice only applies to the current
session: no config, no frames, and it is forgotten on restart.

### 14.4 Known trade-off

The hero lags behind a steady ramp: median-3 adds 1 sample, EMA 0.25 about 3 samples. During a
grind at about 2 g/s the hero shows about 0.8 g below the raw value (preview 03: 11.1 g vs. raw
12.0 g). At DONE it snaps to the raw final weight. The auto-stop belongs to the MCU and is not
affected. If the hero should follow faster, raise `CD_A_HERO` (e.g. 0.4 → about 0.35 g lag).

### 14.5 Tuning in the SIM

```sh
# ctrl socket (SIM build): all fields optional; no fields = read
{"cmd":"cond"}                                    # -> {"ok":true,"raw":false,"a_chart":0.180,"a_hero":0.250,"med":3,"scale":"pct","hold":6,"pct":90,"head":1.15,"gate":3}
{"cmd":"cond","a_chart":0.3,"gate":0}             # live — the filters restart
{"cmd":"cond","raw":1}                            # = DIAGNOSTICS → GRAPH = RAW
```
State JSON (addition, display only): `cond{…, wdb, gate, hero_snaps}`,
`chart{mode, active, full, top, raw_max, disp_max, ticks, base_ticks, clipped, n, n_active}` (v6.15: + `buf, w_max, w_top, resets, reanchors, act_ms`, §14.7),
`weight{src, switches, shown}`.

### 14.6 Before/after figures (graph + weight)

`round_v6/calm_stats.py` reads the state lines from `ui_calm_graph` (the v6.8 and v6.9 SIM builds,
same sim and seed) and computes per segment what was on the screen. The result is in
`sim/reports/v6.9/calm_stats.txt`, and snapshots are side by side in `calm_chart_v68_v69.png`
(chart detail) and `calm_full_v68_v69.png` (full screen). The figures are in
[CHANGES-v6.9](#changes-v69) item 6.

### 14.7 The graph starts over when an activity starts (v6.15)

**The bug (user report):** starting a grind, brew or hot water tares the weight to 0 (the MCU does
this), but the graph's 40 s window still held the samples from before the start, for example a
200–300 g cup that had just been placed on the scale. The weight axis was scaled to that cup step,
so the 0 → 36 g curve of the actual brew was a nearly flat line at the bottom of the chart.

**What v6.15 does (display only):**

* **Reset on idle → active.** `graph_push()` clears the ring buffer (`g_n = g_head = 0`) and restarts
  the three chart filters when the main phase (`phase_main()`: grind, pre-infusion/extraction, hot
  water, or steam on its own) goes from idle to active. The first sample of the activity is the first
  sample in the ring. The y-autoscale is computed from the ring on every draw, so it follows at once:
  1 s into a brew the weight axis top is the 10 g floor × 1.08 (108) instead of 250 g × 1.08 (2700).
* **No reset for a concurrent activity.** A wand flush or steam that starts *during* a brew or grind
  is not an idle → active transition (the main phase stays BREW/GRIND), so the curve is kept. The same
  applies to a brew started while steam is running: the main phase changes from STEAM to BREW without
  passing through idle, so nothing is cleared.
* **Re-anchor right after the start.** Within 1.5 s of the start the ring is cleared once more if the
  weight source latches late (a grind started at the machine: the source switches to word 38 after
  the 300 ms debounce, §14.2) or the tare lands late (the weight drops by more than 2 g in one sample).
  The cup's pre-tare samples therefore never stay in the curve.
* **Link gaps are not idle.** OFFLINE samples do not count as idle, so a short link loss during a
  brew does not wipe the brew curve when the link comes back.
* **All series.** There is one ring buffer for pressure (word 51), weight (38/39) and milk temperature
  (48), so all three are relative to the activity. Flow (word 52) is a stat number without history.
* **Unchanged:** `reg[]`, the state JSON's `regs`/`regs_s`, writes, the frame policy, auto-stop,
  thresholds, guards and the phase logic. DIAGNOSTICS → GRAPH = RAW still shows the raw
  (unconditioned) values; after a start that is the raw series of the fresh ring.
* **State JSON (SIM build):** `chart` now also has `buf` (samples in the ring), `w_max`/`w_top`
  (the weight line's maximum and axis top in brew/water mode, 0.1 g), `resets`, `reanchors` and
  `act_ms` (ms since the current curve started, −1 = none yet).

The x-axis stays a 40 s window, so early in an activity the curve starts part-way across the chart
and grows to the right, as before.

---

## 15. Development iteration (DEV-ITERATION-POLICY)

**The user's directive (2026-11-17):** the FULL E2E suite takes over 35 min and is **not** run per iteration.

| When | What is run | Time |
|---|---|---|
| **Every iteration** | `sh round_v6/build_previews.sh` (self-test EN + DA, previews in both languages + circle check, spec alignment, mutation probes) · `sh build_sim.sh` (0 warnings, static ARM, SIM builds) · `python3 sim/selftest.py` · `python3 sim/run_e2e.py --all --quick --parallel 4 --ui-bin workspace/merakiplus_v6_sim` (5 core scenarios, English only) | about 3–4 min in total; the quick run about 45 s |
| **Targeted, when the change requires it** | one single part of `sim_e2e.py`, e.g. `--only calm` (graph/weight before/after, about 6 min) or `--only net`, or one scenario with `run_e2e.py --scenario <name>` | minutes |
| **Only in CI/CD or at a release milestone** | `run_e2e.py --all` in EN **and** DA, the REF client, steam30, the replay scenarios, the UI scenarios in both languages — i.e. `python3 sim_e2e.py` without `--only` | > 35 min |

The report always states which level was run. A green quick run is not the same as a green full suite. Results from an earlier full run may be quoted, but they must be labelled with the binary (md5) they were run against.


---

## 16. Pressure estimate (v6.10)

**The request:** pressure was shown as *UNCALIBRATED*. A reasonable anchor exists, so the UI now
shows a defensible **estimate** in bar — clearly labelled as an estimate, with the raw value always
available.

### 16.1 The anchor and the scale

| | Value | Source |
|---|---|---|
| **P0** (`P_ANCHOR_RAW`) | **358 raw** (word 51) | The pre-infusion plateau in the UART recording (`BREW.md §6`): brew 20:54:05–06.7, duration 6.06 s (= config `preInfusionTime` 6), median 356, range 332..366. `STEAMLIVE.md` confirms ~360 / 6.84 s in two independent brews |
| **Anchor pressure** | config **`preInfusionPressure`** (default 4 bar) | `/customer/config.json` (`preInfusionPressure: 4` during the recording). Read via the settings table (`SET[S_PIPRESS]`): if the config changes, the scale changes. Outside 1..15 bar or without a config: `P_ANCHOR_BAR_DEF` = 4 |
| **k** | anchor pressure / P0 = 4 / 358 = **0.011173 bar per raw unit** | `p_k()` |
| **Displayed bar** | word 51 (conditioned, §14) × k | `p_bar()` |
| **Pulse limit** (`P_EST_MAX`) | **20 bar** (= 1790 raw at anchor 4) | Above the limit no number is shown |

| Plateau (recording) | Raw | ≈bar (anchor 4) |
|---|---|---|
| Pre-infusion | 358 | **4.0** |
| Extraction (median) | 1346 | **15.0** |
| Extraction (start peak / end) | 1379 / 1325 | 15.4 / 14.8 |
| Transition overshoot | 1481 | 16.5 |
| Hot water (v2) | ~1300 | 14.5 |
| Deadhead / maintenance / steam | 15 716..16 670 | (176..186 linear) → **PULSE**, no number |

The constants can be changed at build time: `-DP_ANCHOR_RAW=… -DP_ANCHOR_BAR_DEF=… -DP_EST_MAX=…`.

### 16.2 What the screen shows

| Where | Text (DA / EN) |
|---|---|
| HOME, the stat field during brewing | label **≈BAR · EST.**, value e.g. **15.0**; above 20 bar **PULS** / PULSE |
| HOME, the chart caption (brew, water, idle after a brew) | **TRYK ≈BAR · EST.** / PRESS. ≈BAR · EST. (replaces *TRYK\* UKALIBRERET*) |
| HOME, tap on the chart or the BAR field | toast (the footnote): **PRÆ-INF. 4 BAR = RÅ 358 · LINEÆR** / PRE-INF. 4 BAR = RAW 358 · LINEAR |
| DIAGNOSTICS, new block **PRESSURE · ESTIMATE** (between GRAPH and ABOUT) | *ANCHOR · PRE-INF. IN RECORDING* → **4 BAR = RAW 358 · k 0.01117** · *NOW · WORD 51 · PUMP SIDE, LINEAR* → **RAW 1346 = ≈15.0 BAR**, or in amber **RAW 16670 · MAINTENANCE PULSE** (idle) / **RAW … · PULSE, NO PRESSURE** (during an activity) · notes: *ESTIMATE · PUMP SIDE · NOT AT THE PUCK* and *> 20 BAR = PULSE · RAW: GRAPH PILL* |
| DIAGNOSTICS → GRAPH = **RAW** | the BAR field shows word 51 as a **raw number** under the label **PRESSURE RAW** (pulses too, unclamped); the chart is v6.9's raw series. Gauge and ring buffer are raw as before |
| SIM state JSON (addition) | `press{raw, k, anchor_bar, p0, max_bar, bar, pulse}` — `bar` is `null` and `pulse` is `true` above the limit |

Display only: control logic, writes, auto-stop, guards and thresholds never use bar (build-checked:
37 control, protocol and conditioning fragments are token-identical to v6.9).

### 16.3 Caveats — why *EST.* and not a calibration

* **(a) Pump side, not the puck.** The linear anchor gives extraction ≈ 15 bar. That is plausible
  for a vibration pump **without an OPV** (budget machine; 12–15 bar is common), but it is the
  pressure on the pump side — not the pressure at the puck, which is typically lower.
* **(b) Pulses are not physical on a linear scale.** The deadhead/maintenance pulses at idle (raw up
  to ~16 670) and the steam level (~16 650, STEAMLIVE §5.3) would be 175–186 bar. The UI therefore
  never shows them as numbers: above 20 bar it shows *PULSE* (HOME) and *MAINTENANCE PULSE* /
  *PULSE, NO PRESSURE* (DIAGNOSTICS); the chart at idle shows them as ticks on the baseline (v6.9).
* **(c) A two-point affine fit does not work.** Idle ≈ 0, pre = 4 bar and ext = 9 bar (the common
  espresso norm) cannot lie on one straight line through 0 (1346/358 = 3.76, not 2.25), and the
  pulses fit neither (BREW.md §7, anchors A/B). Word 51 may be non-linear, or a pump drive level
  rather than a pressure (REGISTERMAP: SPI2 ADC channel 0xA, pressure *or* drive candidate). That is
  why the number is an **estimate** and not a certified calibration.
* **(d) The config anchor is an assumption.** Having the scale follow `preInfusionPressure` assumes
  that the plateau *is* regulated to that pressure. P0 = 358 was measured at 4 bar. If the config is
  set to 3, the UI uses k = 3/358, but it has not been measured whether the plateau then still sits
  at 358 (or moves). Only 4 bar has been recorded.
* **Physical verification** (a pressure gauge on a blind basket, or the sensor ID of the transducer)
  is still the only path to a real calibration (BREW.md §7, recommendation). The raw value is always
  available: DIAGNOSTICS → GRAPH = RAW and the "now" line in PRESSURE · ESTIMATE.


---

## 17. UX review (v6.11)

A critical pass over **all** pages and states (73 previews × 2 languages + the code). The criteria were: information hierarchy, whether the screen can be read at arm's length, touch targets, feedback and affordances, whether the navigation can be found, label text and capitals, color semantics, error, empty, offline and sleep states, the use of the round screen, and contrast. The findings from the orchestrator (O1–O10) and my own (E1–E9) were merged and prioritized by **effect for the user ÷ risk** (P1 = do it now, P2 = do it if it is cheap, P3 = deferred).

*(The "before" column quotes the texts of the time; the Danish text is given where the finding was about the Danish wording.)*

### 17.1 Findings, priority and outcome

| # | Finding (before) | Prio | Outcome in v6.11 |
|---|---|---|---|
| E1 | **Stat labels ran together:** *GROUP STEAM BOILER WEIGHT* (a 115 px label in a 94 px column, 6 px of air) read as one line. The same applied to *KVÆRN-TRIN* (grinder step, 100 px) and *TRYK RÅ* (pressure raw) | P1 | **Done.** *STEAM* (*DAMP*) at idle, *BOILER* (*KEDEL*) in the steam trio (*TIME · STEAM · LEVEL* would read as "steam level"), *STEP* and *RAW*. The self-test now requires ≥ 16 px of air between two labels (`STAT_GAP`) |
| O3 + E2 | **HOME at idle:** *NO ACTIVITY* repeated the phase chip and did not say what you could do. *BREW TEMP SET TO 87.0°* was a long F_XS line | P1 | **Done.** The empty chart field now shows **HOLD AN ICON TO START** and **SWIPE FOR SETTINGS**. If nothing can be started, it shows the reason (the guard's text, amber on a config error). The sub-line is **TARGET 87.0°** in F_S, and the label is **BREW BOILER · NOW**, so hero = now and sub-line = target |
| O1 | **Could not be found:** settings only via swipe or invisible edge zones. "Hold to start" was only shown in a toast after a wrong tap | P1 | **Done.** A **gear** below the page dots (touch area 64 × 46 px) opens BREW, and the start hint is in the chart field (see O3). Edge taps now also work on the lists. The page name is already the title on all pages except HOME, where *MERAKI+* is the title. The dots have therefore not been given labels |
| O4 | **Technical language for the user:** *WAKING CONTROLLER · WORD 1 = 0*, *NO REPLY FROM CONTROLLER*, *E2 · BREW BOILER SENSOR*, notes such as *WORD 11 · BREW BOILER SETPOINT* / *WORD 112 HI BYTE*, toasts with *ACK*, *FRAME POLICY*, *WRITE QUEUE*, *STOP SENT · WORD 24 = 0* | P1 | **Done.** The texts are now *WAKING UP…*, *NO CONNECTION TO THE MACHINE* and *BREW BOILER SENSOR FAULT*. The E code is shown dimmed in the chart field (*CODE E2 · SEE DIAGNOSTICS*). The notes describe the effect (*TARGET FOR THE BREW BOILER*, *SOAKS THE PUCK BEFORE FULL PRESSURE*, *THE MACHINE ONLY KNOWS 3 AND 4* …). Toasts: *SAVE FAILED · NO REPLY*, *THE MACHINE DID NOT ANSWER*, *BLOCKED BY A SAFETY RULE*, *BUSY · TRY AGAIN*, *STOPPING STEAM…*. Word numbers only appear on DIAGNOSTICS and in §4. A self-test checks that no user-facing string (HOME, toast, note, footer) contains word numbers, ACK, frame policy or write queue |
| O9 | **Contrast:** micro labels (text 3 = 104,111,126) had 3.5:1 on the background and 2.9:1 on list rows, at 11 px capitals with thin strokes | P1 | **Done.** Text 3 is now 140,147,162: 5.7 / 5.5 / 4.7:1 on background / surface 1 / surface 2. Text 2 is now 176,183,196: 8.7 / 8.5 / 7.2:1. Disabled elements have been given their own color, 98,105,120 (3.2:1, large glyphs), so "off" still looks off. The progress ring during a brew has kept its old color |
| O5 | **Swipe vs. scroll:** a slanted gesture (e.g. dx 70, dy 45) that started slightly horizontally changed the page in the middle of a list | P1 | **Done.** The axis is locked during the first 14 px. A page change requires a horizontal start, ≥ 60 px and ≤ ~30° (\|dx\| ≥ 1.7·\|dy\|). A gesture that starts vertically never changes the page, not even on HOME |
| O6 | **No back affordance** in the detail and system views apart from CANCEL | P1 | **Done.** A **close chevron** at the top (touch area 88 × 46 px) and **swipe down** (≥ 70 px, vertical, not started on −/+). Both have the same semantics as CANCEL, so nothing is saved |
| O2 | **Touch targets** | P1 (measurement) | **Were already fine.** They are now measured through `hit_test()` in the self-test: icons 60 × 61, −/+ 85 × 85, rows 352 × 58, CANCEL/SAVE 136 × 64, gear 64 × 46, chevron 88 × 46. The only exception was DIAGNOSTICS' GRAPH pill at 300 × 40, which is now **300 × 44** |
| O8 | **HOME during a brew:** the chart text *PRESS. ≈BAR · EST.* repeated the stat field's *≈BAR · EST.* | P2 | **Done.** The chart is called **PRESSURE**, and the stat field **≈BAR EST.**. The basis (anchor 4 bar = raw 358) is still in the toast on a tap on the chart and on DIAGNOSTICS |
| O7 | **DIAGNOSTICS:** 16 cells in one block grid, raw counters before the link status. Boot, network and pressure only came after 8 rows | P2 | **Partly.** DIAGNOSTICS is now divided into sections: **STATUS** at the top (link, phase, E1/E2/E3, awake, network, version, boot push), then BOOT · SLOT · SWITCHER, NETWORK, GRAPH and PRESSURE · ESTIMATE, then **RAW COUNTERS · SERVICE** (rx, crc, work_sta, cmd6, current, frame, mirror, md5) and ABOUT at the bottom. The cells' contents are unchanged. *Deferred:* folding the raw counters behind a toggle (see 17.2) |
| O10 | **Tap feedback** | P2 | **Was already consistent:** a ripple on all targets, a pressed color on rows, −/+, pills, icons (+ hold ring) and the GRAPH pill. The new targets (gear, chevron) have the same pressed state. Previews `70_home_gear_pressed` and `71_detail_back_pressed` |
| E3 | The error and offline states had no way forward | P2 | **Done** via the hint: *CODE E2 · SEE DIAGNOSTICS* and *DETAILS IN DIAGNOSTICS* |
| E4 | The sub-line at idle (the most important message: target, offline, error) was in the smallest font | P2 | **Done.** F_S when the text fits in the field. During activities it is still F_XS |
| E5 | The four activity icons have no text below them | P3 | **Deferred.** There are 20 px between the icon row and the dots. Four labels in F_XS would be squeezed against the gear. The hold hint in the chart field and *HOLD · BREW 36.0 G* in the sub-line while holding cover the need |
| E6 | The detail footers *CONTROLLER NOW + CONFIG / STOCK UI: AFTER RESTART* are technical language | P3 | **Deferred.** They describe a real two-UI semantics (PARITY §1.4), and the E2E scenarios quote toasts with the same wording (*GEMT · CONTROLLER + CONFIG*). It should be reworded together with the test work stream |
| E7 | *HOLD AN ICON …* is also shown while you are holding an icon (the sub-line then shows *HOLD · BREW …*) | P3 | **Accepted.** It is redundant, but not wrong. Hiding the hint would cause a 650 ms flash |
| E8 | The hero *--.-* offline reads as dashes | P3 | **Unchanged.** It is now in the "disabled" color, and the sub-line clearly says *NO CONNECTION TO THE MACHINE* |
| E9 | The page dots do not show names | P3 | **Unchanged** (see O1: the title is on the page) |

### 17.2 Deferred — and why

* **The raw counters folded behind a toggle (O7, second half).** It changes DIAGNOSTICS' scroll geometry dynamically and moves the GRAPH pill's touch area, which the self-tests, previews (`DG_SC_*`) and the E2E profile rely on. DIAGNOSTICS is an expert page, so a separate section further down gives most of the gain without that risk.
* **Labels below the icons (E5)** and **rewording the detail footers (E6):** see the table.
* **Labels on the page dots (O1):** the title is already on the pages. An extra layer of text at y 430 would compete with the gear.

### 17.3 Deliberately unchanged

The phase colors and the gauge are byte-identical in all 140 compared previews (the ring r ≥ 200 above y 380). The same applies to the display conditioning (v6.9), the pressure estimate (v6.10), the weight latch and dead band, EN/DA, the circle clip, the frame policy, the RAW toggle, the boot and switch flows and the removed REVERT. 47 control and protocol fragments are **token-identical** to v6.10 (the build check). That covers the frame policy, write queue, activity recipes and guard, phase logic, the SAVE, close and switch paths, boot push, network guard, RX, CRC, flag, conditioning and the pressure estimate.

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
  (1.9 : 1 against the background); the pill in v6.6–v6.12 was 150 px. The texts
  are *STOP WATER / STOP BREW / STOP GRIND* and *STOP VAND / STOP BRYG / STOP KVÆRN*. The button is
  shown while the activity's work_sta bits are set (`phase_main`). It is gone in the DONE view, and the
  valve pulse after a brew (`0105`) gives no button.
* **Two buttons:** if a main activity runs at the same time as latched steam (e.g. `5327` + word 24 =
  1), there are two buttons side by side: left = the main activity, right = STOP STEAM. Each has a
  touch area of 160 × 60 px and a pill of 152 × 54 px in F_S (v6.13–v6.16: 148 × 56 / 136 × 44 px in F_XS). The wand/flush still has no STOP button.
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

Locally (all green, figures in [CHANGES-v6.13](#changes-v613) item 6): 26 new self-test
checks, previews and spec alignment, mutation probes, SIM and E2E (quick).

**On-device:** not run. The machine was offline for the whole round: `<display-ip>` did not answer
ping, the ARP lookup was incomplete, and the telnet port was closed (2026-11-18 around 15:45).
`verify_stop_v613.py` is ready to run when the machine is switched on (§11, test plan S).

## 19. The power button (v6.14)

> On the tested hardware the power button behaves as a hardware power switch: see
> [power.md](../power.md). The section below describes the software implementation (which mirrors the
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
| cmd8's contents | year (u16), month (+1), day, hour, minute, second, alarm day/hour/minute = the next scheduled pre-heat (`performSleepActions`); no schedule = alarm 0/0/0 | 0x14703C–0x147148 (CRC_GetModbus16 over 13 bytes) | v6.16: stock's local time (UTC + zone + DST; v6.14 sent UTC) + the next pre-heat occurrence, or 0/0/0 without a schedule ([time-scheduling.md](../time-scheduling.md)). The MCU's 0x88 acknowledgement is only counted for DIAGNOSTICS — the stock UI does not wait for it either |
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
Locally all green (figures in [CHANGES-v6.14](#changes-v614) item 5). On-device: **not
run** in this round (the installation of v6.13 in slot B was in progress). `verify_power_v614.py` is
ready (§11, test plan T). The later on-device run and its hardware findings are documented in
[power.md](../power.md).

---

## CHANGES-v6.1

Every alignment delta, numbered after the task's items. *(UI texts of the time are given in English translation; the UI was Danish-only before v6.3.)*

**1 · Limits, labels, tiers**
| Row | v6.0 (assumed) | v6.1 (spec) |
|---|---|---|
| GRIND DOSE | 70..250 / 1 | **10..309 / 1** (1.0–30.9 g) |
| BREW WEIGHT | 150..800 / 5 | **100..990 / 10** (10–99 g) |
| BREW TEMPERATURE | 800..980 / 10 | **850..960 / 10** (85–96 °C) |
| MILK TEMPERATURE | 400..750 / 10 | **450..700 / 10** (45–70 °C) |
| HOT WATER | 200..4000 / 50 | **300..5000 / 100** (30–500 g, step 10 g) |
| STEAM LEVEL | 1..4, numbers | 1..4, **names** GENTLE/MILD/MEDIUM/STRONG |
| PRE-INF. PRESSURE | 3..6 | **3..4** (the MCU only knows 3 and 4) |
| PRE-INF. TIME | 5..15 (assumed) | 5..15, now **marked ESTIMATE**, ceiling 60 |
| GROUP TEMP | 30..90 / 10 | **30..80 / 10**, marked ESTIMATE, ceiling 100 |
| GRIND MODE | 0 manual / 1 auto | **1 WEIGHT / 2 TIME** |
| WATER MODE | 0 manual / 1 auto | **1 AUTO / 2 MANUAL** |
| TIME FORMAT | 0 = 12 h / 1 = 24 h | **0 = 24 h / 1 = 12 h** (from `update_time`) — *wrong: the `eor` at 0xD5970 was missed; corrected back to 0 = 12 h / 1 = 24 h in v6.16* |
| BRIGHTNESS | 1..10 (assumed) | 1..10 — the spec says 0..10; 0 deliberately left out |
| the `def` column | current config values | the stock UI's `getNumber` defaults |

* Pages renamed/redistributed: BREW (9) · **STEAM · WATER** (5, previously COMFORT) · SYSTEM (10). 26 → 24 rows. Every row now carries the spec's tier (`ptier`).
* **The always-extend rule has been removed.** v6 extended any range to accommodate a loaded value. v6.1 only extends the two estimated maxima (up to the ceiling); everything else out of spec is shown in red and blocks activity starts.
* A new visible hint: *"MAX = ESTIMATE"* / *"SLIDER SCALE · SELECTOR UNKNOWN"* in the detail and `≈` in the list.
* The stepper now snaps to the step grid (a value saved in °F such as 856 goes to 850/860).

**2 · Removed / locked**
* `brewMode`, `sleepMode`, `sleepModeSetting` removed from the table (and the brew guard on `brewMode` with them).
* A new row **WATER TEMP · SENSOR** (word 40, live, no editor) — the hot water temperature is a measurement, not a setting.
* `hotWaterShortcut` moved to SYSTEM (spec tier 2).

**3 · The breakTime quirk** — PAUSE TIME is **locked** with an amber note *"NO EFFECT · THE MCU USES PRE-INF. TIME"*. Chosen over an editor with a warning, because a button without an effect promises something the machine does not do, and because the max is unknown. The value is still packed into word113 lo. Demo in §11 L.

**4 · Packing** — v6's `pack112`/`pack113` were correct; no change in the bytes. New: the pressure is limited to 3/4, the re-sync before a brew only compares word113's high byte, and half a sync sequence is never queued.

**5 · Safety** — none of v6's frames had ≠ 0 in words 26/27/30 (audit in §7a). New: a central `frame_policy()` on every cmd6 frame (trigger words, start intent, per-word range), 3 `_Static_assert`s, a `BLOCK` counter on DIAGNOSTICS, STOP STEAM cannot be rejected, a rollback cannot hang the save flow. 24 new self-test cases for this.

**6 · Boot push** — UI text *"TAKES EFFECT NOW"* → *"CONTROLLER NOW + CONFIG"* / *"STOCK UI: AFTER RESTART"*; the config-only toast *"SAVED · STOCK UI: AFTER RESTART"*; the `RESTART_NEEDED` text and a stdout reminder on exit explain the restart + boot push; §5 and REVERT.md rewritten.

**7 · Persistence** — the write pattern is unchanged (it was already token replacement in the stock UI's own format). What is new is the **proof**: `cjson_canon.py` + a full table round trip (21 fields, min → max → original) with the md5 back at the baseline.

**8 · Language / time zone / presets** — LANGUAGE unlocked (0..10, proven). TIME ZONE still locked (no table in the spec). Presets/NFC still deferred (§9).

**9 · Runs** — self-test 33 → **87 checks**; previews 30 → **39**; new `parity_check.py` and `cjson_canon.py` in `build_previews.sh`; static build 0 warnings.

**Fixes found along the way (not in the task):**
* **Out-of-array lookup:** v6's extend rule could give an enum range larger than the name table (`grindMode=2` → `OPT_GRINDM[2]`). Gone together with the rule; an enum outside the table is now shown as `(number)`.
* The `timemode` mapping was inverted (see item 1). *(v6.16: this "fix" was itself wrong — see CHANGES-v6.16.)*
* A tap on −/+ in a locked row gave a ripple without effect; locked rows now only have BACK.

**Unchanged:** the run.sh contract, all 6 observed frame vectors, keep-awake, phase detection, design system, rendering, `EVIOCGRAB`, environment variables.

---

## CHANGES-v6.2

Boot slot integration: merakiplus can now choose the boot UI itself, switch to the stock UI without a restart and say honestly when the stock UI must be restarted. The requirements come from the task and from the boot work stream's `boot/BOOT.md` **§9 (UI requirements)**, which has been followed point by point. Protocol, frame policy, keep-awake, `EVIOCGRAB`, circle clip and `run.sh` are unchanged (35 of the 39 v6.1 previews are byte-identical; the four that differ are splash/DIAGNOSTICS (version text) and the two SYSTEM lists).

> **About the order:** BOOT.md did not exist when the work began, and was written along the way. The runtime switch was first built as the task described it (a `/tmp` shell helper that waits for merakiplus's exit and then starts the stock UI). When BOOT.md §9.2 arrived, it was reworked to its sequence, because the §UI requirements take precedence. **There is therefore no helper process and no script in `/tmp` in v6.2.**

**1 · SYSTEM row BOOT UI** (BOOT.md §9.1)
* At the bottom of SYSTEM, under the heading *STARTUP · UI CHOICE*: the row **BOOT UI** with the value **MERAKIPLUS** / **ORIGINAL** and the sub-line *"APPLIES AT NEXT RESTART"*.
* A tap opens a view with −/+ (− = ORIGINAL, + = MERAKIPLUS), CANCEL and SAVE. At the top is the current state, read live from the disk every second: *"NOW: OUR UI (B) AT BOOT"* or *"NOW: ORIGINAL (A) AT BOOT"*. The help text is §9.1's, verbatim: *"APPLIES AT NEXT RESTART"* · *"OTA CAN OVERWRITE THIS CHOICE — SEE BOOT.md §7"*.
* SAVE does exactly one thing: MERAKIPLUS = `open("/customer/new", O_CREAT|O_WRONLY, 0644)` + `sync()`; ORIGINAL = `unlink("/customer/new")` + `sync()`. Nothing else in `/customer` is touched. The calls are idempotent. No restart from the button.
* **Error** (e.g. a read-only file system): toast *"FLAG ERROR · NOTHING CHANGED"*, the view stays with the old state.
* **Guard** (task requirement, stricter than BOOT.md): the row is only editable when `/new_customer` is in `/proc/mounts`, and `/new_customer/demo_v9` is executable and **is merakiplus**. Otherwise it is locked with the reason: *"SLOT B NOT MOUNTED"*, *"SLOT B HAS NO demo_v9"* or *"SLOT B IS NOT MERAKIPLUS"*. The guard applies in both directions: if there is a vendor binary in slot B, the flag belongs to the vendor's OTA mechanism, and merakiplus does not touch it.
* The recognition of merakiplus is a `what(1)` tag in the binary (`@(#)merakiplus …`); a v6.1 binary is recognized by a string from its exit message. `build_previews.sh` tests it against the real files: `merakiplus_v6` and `merakiplus_v6_1.bin` = merakiplus; the vendor `demo_v9` (both copies) and `boot/switcher` = not merakiplus.
* The value shown is what `demo.sh` will actually start: *MERAKIPLUS* only if the flag exists **and** slot B is mounted **and** contains merakiplus; *SLOT B* if the flag exists but the binary is a different one; otherwise *ORIGINAL*.

**2 · SYSTEM action SWITCH TO ORIGINAL NOW** (BOOT.md §9.2 — runtime B→A, no restart)
The row opens a confirmation (CANCEL / SWITCH NOW). SWITCH NOW runs §9.2's six steps in the prescribed order:

| §9.2 | What merakiplus does | Where in the source |
|---|---|---|
| — | draws the last frame *"SWITCHING TO ORIGINAL"* (stays until the stock UI draws) | `do_switch()` |
| 1 | stops its own activity: the UART is flushed and **closed**, the touch grab is released and the input closed, the fb is unmapped and **closed** | `dev_release_impl()` |
| 2 | `fork()`; in the child `setsid()`, `chdir("/customer")`, `setenv("LD_LIBRARY_PATH", …, 1)`, stdio → `/dev/null`, all other fds closed, `execl("/customer/demo_v9", "./demo_v9", NULL)` | `switch_launch()` |
| 3 | waits **400 ms** (clock-driven), then exits. Meanwhile the UART is not read, and the fb is not drawn — the fds are closed | `switch_launch()` + the main loop |
| 4 | the flag `/customer/new` is not touched. **No process gets a signal** — the source contains 0 `kill()` calls (checked by `build_previews.sh`) | — |
| 5 | exit; anything that might be left is released by the kernel | `main()` |
| 6 | a log line in `cfgbak/boot_ui.log` with the time, `reason=user-button`, the stock pid and the path used | `boot_log()` |

* **One addition to step 2:** the child closes *all* inherited fds, not just stdio. An inherited touch fd would otherwise keep `EVIOCGRAB` alive inside the stock process, and the stock UI would never get a touch. The self-test measures it from the inside: the mock "stock" reports `extra_fds=0`.
* **One deviation from the letter of §9.2 — `LD_LIBRARY_PATH`:** §9.2 says `"/customer/libs:/config/wifi"`. `demo.sh`'s A branch sets `"$LD_LIBRARY_PATH:/customer/libs:/config/wifi"`, i.e. the boot environment's prefix is kept. merakiplus does the same: takes its own inherited path, removes the slot components and appends A's. Without a prefix (e.g. when the switcher has started merakiplus) the value is **exactly** §9.2's; with a prefix it is what a real A boot would give the stock UI. Rationale: if one of the stock binary's libraries were in a prefix directory, the pure §9.2 value would make the stock UI die at startup. Whether the prefix exists on the device has not been measured — see on-device item N3. If the pure value is wanted, it is one line in `ld_path_for_a()`.
* **The stock UI does not come up** (the child dies within the 400 ms): merakiplus does **not** exit, reopens UART/fb/touch, shows *"SWITCH FAILED · …"* and logs it.
* **Blocked during an activity:** if grind/brew/water/steam is running, or a write is waiting for an ACK, SWITCH NOW is rejected with a toast. The stock UI's boot push (`cmd6 sub1 len30`) writes, among other things, the start words to 0, and that must not happen in the middle of a brew. This is an addition to §9.2.
* **Locked** if `/customer/demo_v9` is missing or not executable (BACK only).
* The stock UI starts fresh and reads `config.json` itself, so `RESTART_NEEDED` is removed on a successful switch.
* The screen shows the way back: *"BACK HERE: 3 LONG PRESSES · TOP LEFT (1.5 S)"* (the switcher gesture, BOOT.md §2), or — if `pidof switcher` is empty — *"SWITCHER DAEMON NOT RUNNING"* and *"BACK HERE: RESTART"* / *"TELNET ONLY"*, depending on whether the flag is set.
* `MERAKIPLUS_SWITCH_DRY=1`: SWITCH NOW releases nothing and starts nothing; the log says what would have happened.

**Known consequence (BOOT.md §9.2, §1.2):** the watchdog's slot is fixed at boot. If the machine was booted in B, `test.sh new_customer` is running; if the stock UI dies after a runtime switch — also if you run `kill $(pidof demo_v9)` yourself — the watchdog starts **merakiplus** again, not the stock UI. The next restart follows the flag. It is a safe fallback and requires no action.

**3 · The reverse path (only documented — not part of merakiplus)**
Stock → merakiplus without a restart is done by the switcher daemon on the gesture (BOOT.md §3.2): SIGSTOP the stock UI → start `/new_customer/demo_v9` with `LD_LIBRARY_PATH=/new_customer/libs:/config/wifi` → verify the exe link → SIGKILL the stock UI; if merakiplus does not come up, the stock UI is SIGCONTed. For testing without the gesture the same pattern exists in `boot_install.py` (`launch_a_stopfirst` is the B→A direction; `boot-test` step c is A→B via `tinj_hold`). While merakiplus is running, it holds touch exclusively, so the switcher sees no touches and does nothing.

**4 · Exit reminder** (the v6.1 integration note)
* REVERT now shows a full-screen reminder **before** exit when it is true: *"CONFIG HAS CHANGED"* · *"STOCK UI NEEDS A RESTART OF demo_v9 FOR NEW VALUES"* · the command `kill $(pidof demo_v9)` in a field of its own · *"RUN VIA TELNET · WATCHDOG RESTARTS IT"* · *"OTHERWISE THE STOCK UI SENDS OLD VALUES WHEN BREWING"*. Buttons: BACK / EXIT. It must be a screen and not a toast: after the exit the stock UI owns the screen.
* **True** means: `RESTART_NEEDED` exists, **and** a `/customer/demo_v9` is running that was started before the latest SAVE in the same boot. `RESTART_NEEDED` therefore gets a stamp on every SAVE (`boot_id=… mono_s=…`), which is compared with the process's start time in `/proc/<pid>/stat`. So the reminder is **not** shown after a machine restart, **not** when the stock UI has been restarted since the SAVE, and **not** in a slot B boot, where there is no paused stock UI. A v6.1 flag without a stamp counts as valid.
* The stdout message on exit follows the same rule and uses the same wording.
* Paths and commands are shown in lower case (the shell is case-sensitive). The stroke font has been given a–z, `$`, `'` and `§`; they are only used in marked code spans, the rest of the UI is upper case as before.

**5 · The REVERT button now depends on how merakiplus was started**
* **run.sh mode** (there is a paused stock UI): unchanged — exit, `run.sh` SIGCONTs. The only difference is the reminder in item 4, if it is true.
* **Slot B boot** (merakiplus *is* `/new_customer/demo_v9`, no stock UI): a plain exit would just make the watchdog start merakiplus again. REVERT therefore opens the confirmation from item 2. This is not in BOOT.md; it was chosen because the button would otherwise have no effect.
* SWITCH TO ORIGINAL NOW in run.sh mode does the same as REVERT (nothing is started — two `demo_v9` must never run).

**6 · DIAGNOSTICS**
* The page can now **scroll vertically** (the same gesture and indicator as the lists); the 14 existing fields are unchanged at the top.
* A new block *BOOT · SLOT · SWITCHER*: `FLAG /customer/new` (EXISTS / NOT PRESENT) · NEXT BOOT (MERAKIPLUS / ORIGINAL / SLOT B) · MERAKIPLUS STARTED FROM (SLOT B / RUN.SH / SLOT A, from `/proc/self/exe`) · SLOT B (MERAKIPLUS FOUND / NOT MOUNTED / NO DEMO_V9 / NOT MERAKIPLUS) · SWITCHER DAEMON (RUNNING / NOT RUNNING, `comm == switcher` in `/proc`) · STOCK PROCESS (NONE / PAUSED / **ALSO RUNNING!** in red) · a PID line (merakiplus / stock UI / switcher) · the full exe path · the latest switch event from `/tmp/switcher.log` (the last line with `skift` or `GESTUS`, with the time; *"LOG NOT READABLE"* / *"NO SWITCH EVENT IN THE LOG"* otherwise). Long paths and events are truncated with `…`.
* The boot state is only read while it is shown (SYSTEM, DIAGNOSTICS, the system views), once per second.

**7 · New persistent writes** — see `REVERT.md`: `/customer/new` (only on SAVE of BOOT UI) and `cfgbak/boot_ui.log` (one line per flag change and per runtime switch). `RESTART_NEEDED` has gained the stamp line. Nothing in `/tmp`.

**8 · Local verification**
* **Self-test: 87 → 152 checks, 0 errors** (`round_v6/selftest.txt`). New, all against a mock root (`/tmp/merakiplus_v6_selftest_*/root` with `/customer`, `/new_customer`, `/proc`, `/tmp`):
  * *Flag logic (25):* all four lock reasons, the vendor flag untouched in both directions, touch/rm/idempotency, nothing else in `/customer` changed, a read-only directory, live reading, slot B disappearing after the flag has been set, v6.1 recognition, a tag across a 64 KiB read boundary, the real vendor binary, exe slot, process scan (paused / running / `(deleted)` / slot B `demo_v9` / own pid), `switcher.log` (missing / without a switch / a large file), `LD_LIBRARY_PATH` derivation.
  * *The reminder's truth (7):* the six combinations of flag, stamp, boot id and start time + that SAVE stamps the flag.
  * *Runtime switch (14):* the whole §9.2 sequence run for real, with a **copy of the test binary** as the mock "stock" in `<root>/customer/demo_v9`. It reports back itself: `argv0=./demo_v9`, its own session, cwd `/customer`, stdio = `/dev/null`, **0 inherited fds**, started **after** step 1, the path it got. In addition: the measured wait (about 400 ms; 403–412 ms in the runs), the flag untouched, the log line, `RESTART_NEEDED` removed, a stock UI that dies immediately (merakiplus stays and takes the screen back), dry run, run.sh mode (nothing is started), a missing binary. No real UI process is started, and no signal is sent.
  * *UI routes (15):* REVERT in all three situations, SWITCH NOW during steam, reminder → EXIT/BACK, locked views in the hit test, BOOT UI through real touch events (−, +, SAVE, CANCEL, write error).
  * *Layout (4):* all texts in all states measured against the gauge ring's inner edge, the field widths and the distance to the gauge ends; rows and DIAGNOSTICS cells against their columns (also with 5-digit PIDs and 80-character paths).
* **Previews: 39 → 51**, all re-rendered; **circle check 51/51, 0 violations**. New: `39_list_system_boot_locked` · `40_bootui_detail` · `41_bootui_changed` · `42_bootui_saved_toast` · `43_bootui_locked` · `44_switch_confirm_boot` · `45_switch_confirm_runsh` · `46_switch_blocked_activity` · `47_switching_bye` · `48_exit_banner` · `49_diag_boot` · `50_diag_boot_runsh`. `35_list_system_scrolled` now shows the bottom of SYSTEM with the two new rows.
* **Spec alignment:** unchanged, **ALL ALIGNED** (the new rows are system actions and are outside `SET[]`; the table is still 24 rows).
* **Static build:** 0 warnings (also 0 new ones with `-Wall -Wextra`).
* `round_v6/boot_check.txt`: the binary recognition against the real files, 0 `kill()` calls, `run.sh` unchanged.

**Fixes found along the way**
* A redraw of the hero field could erase the edge of the + button on the BOOT UI screen (the fields overlapped by 30 px). Found when reviewing the preview at full size; the hero field has been narrowed.
* The last frame before the switch froze with half a touch ripple; the ripple and toast are now cleared before it is drawn.
* With 5-digit PIDs three DIAGNOSTICS cells became wider than the column; the PIDs have been given their own line.

**Not verified (requires the device)** — see §11 N.

---

## CHANGES-v6.3

Product name and language. The UI is now called **Meraki+**, and every visible text exists in English (default) and Danish. The protocol, frame policy, keep-awake, `EVIOCGRAB`, circle clip, boot logic and `run.sh` have not been touched — this has been proven, see item 9.

**1 · Product name: Meraki+**
* **Splash:** the wordmark *MERAKI* with a "+" badge (a crema disc with a dark plus, raised like a superscript), the tagline *"ADVANCED BREWING INTERFACE"* / *"AVANCERET BRYGGE-INTERFACE"* and the line *"V6.3 · CONFIG LOADED"* / *"… INDLÆST"*.
* **HOME:** a discreet title line *MERAKI+* at the top inside the gauge ring (the plus in the accent color). It lives in the static layer and costs nothing per frame.
* **DIAGNOSTICS:** the VERSION cell shows *MERAKI+ V6.3*; at the bottom there is a new *ABOUT* block with the UI language, wordmark, version and tagline.
* **All texts that used to say MERAKIPLUS** now say MERAKI+: the BOOT UI choice (MERAKI+ / ORIGINAL), *"NOW: MERAKI+ (B) AT BOOT"*, *"MERAKI+ CLOSES · STOCK UI STARTS"*, *"MERAKI+ HAS EXITED"*, the lock reasons, the DIAGNOSTICS cells and the notes in the settings.
* **No generation name** in any UI string — the same software runs on several machine generations. The binary's `what(1)` tag has changed from `… merakitech-gen1 display-ui` to `@(#)merakiplus V6.3 meraki-plus display-ui`; the BOOT UI guard only recognizes the first 10 characters (`@(#)merakiplus `), so v6.1, v6.2 and v6.3 binaries are all recognized (`boot_check.txt`).
* **The technical identifier is `merakiplus`:** source file, binary, `/new_customer/merakiplus/`, the `MERAKIPLUS_*` variables, the `cfgbak/` logs and the stdout prefix `== merakiplus:`. `run.sh` and `boot_install.py` depend on them. In the UI `merakiplus` only appears inside displayed paths (e.g. `merakiplus/lang`).
* **Deviation from the wording of BOOT.md §9.1:** the help text *"Our UI (B) at boot"* has become *"MERAKI+ (B) AT BOOT"*. The meaning is the same; the product name takes precedence.

**2 · UI language: a new SYSTEM row UI LANGUAGE / UI-SPROG**
* **At the top of SYSTEM**, so it can be found without being able to read the rest. The options are always shown as *ENGLISH* and *DANSK* (endonyms, the same in both languages).
* **Live:** a tap on −/+ switches the whole UI immediately (like the brightness preview). CANCEL goes back to the saved language; SAVE makes it permanent. The confirmation comes in the new language: *"SPROG GEMT · DANSK"* / *"LANGUAGE SAVED · ENGLISH"*. No restart.
* **Stored in merakiplus's own file `/new_customer/merakiplus/lang`:** plain text, exactly `en` or `da` (2 bytes). Written atomically: a temp file in the same directory → `fsync` → reread and compare → `rename()` → `fsync` of the directory.
* **Read once at startup**, before the first frame. If the file is missing, cannot be read, or contains anything other than `en`/`da`: **English**. The reading tolerates whitespace, newlines and capitals, so `echo da > /new_customer/merakiplus/lang` also works. Everything else (`de`, `dansk`, an empty file, binary, too long) gives English.
* **If the file cannot be written** (e.g. a read-only file system), the language still applies for the rest of the session, and the toast says so: *"LANGUAGE ACTIVE · FILE NOT SAVED"*. This is a deliberate exception to the all-or-nothing rule for settings: a user who cannot read English must not be locked out of their language by a write error.
* **Does not touch the stock UI:** the row has no config key and no MCU word. SAVE does not write to `config.json`, sends no cmd6 frame, makes no backup and no `RESTART_NEEDED`. The self-test measures all four parts.
* **The stock UI's language is a different row:** *STOCK LANG.* (`config.language`, 11 languages without Danish) works as in v6.1. It has been renamed from *LANGUAGE*, so the two cannot be confused, and its note points to UI LANGUAGE.
* **DIAGNOSTICS / ABOUT** shows the active language and where it comes from: *FROM FILE* · *DEFAULT (NO FILE)* · *UNKNOWN FILE VALUE* (amber) · *NOT SAVED* (amber).
* `MERAKIPLUS_LANGFILE=<file>` moves the file (for testing and for v6.4's SIM run).
* In the code the row is an ordinary `SET[]` row with the flag `SF_LOCAL`. `--dump-table` leaves it out, so the spec alignment still sees 24 rows.

**3 · The string table**
* **One place:** `STR_TABLE` in `merakiplus.c` — 324 ids, each with an English and a Danish text and a class. No drawing code contains a text any more; `SET[]` refers to ids for the label, unit, note, hint and enum names.
* **Lookup per render:** `TR(id)` looks up in the active language every time a text is drawn. Nothing is cached across a language switch; a switch simply marks the static layer for redrawing. An unknown language gives English.
* **Class = the field the text sits in** (toast, note, footer, button, title, DIAGNOSTICS cell …). The self-test measures every classified string in both languages against the field's pixel budget; texts with placeholders are measured in their context by the layout tests.
* **Rules the self-test enforces for the whole table:** both languages filled in · the same printf conversions in the same order · the same numbers and decimal separator · units identical · every character exists in the stroke font · no generation name · the code name `merakiplus` only in displayed paths and on the console.
* **Appendix A is generated** from the binary's own table (`--dump-strings` → `strings.tsv` → `i18n_appendix.py`); `build_previews.sh` fails if the two do not match.
* **Not translated (deliberately):** the log files `cfgbak/config_writes.log` and `cfgbak/boot_ui.log`, the contents of `RESTART_NEEDED` and the stderr diagnostics — they are read by tests and tools and are unchanged. *LAST SWITCH* on DIAGNOSTICS shows the switcher daemon's log line verbatim (the boot work stream's text, Danish). The stdout lines on exit **are** translated (ASCII, the same `== merakiplus:` prefix).

**4 · Numbers and units are the same in both languages**
* The decimal point, `°C`/`°F`, `G`, `S`, `BAR`, `V`, `HZ` and all numbers are identical; only words are translated. The units are in the table with the same text in both columns, and the self-test compares them.
* One Danish text had a decimal comma: *"ØVERST TIL VENSTRE (1,5 S)"* is now *"(1.5 S)"*, like all other numbers in the UI.
* TIME FORMAT showed *24 T / 12 T*; now *24-TIMERS / 12-TIMERS* (English *24-HOUR / 12-HOUR*), so it is a word and not a unit that changes.

**5 · Danish texts changed in v6.3** *(this table is about the Danish wording and is therefore kept in Danish)*

| Before (v6.2) | Now | Why |
|---|---|---|
| MERAKIPLUS (everywhere) | MERAKI+ | product name |
| NU: VORES UI (B) VED BOOT | NU: MERAKI+ (B) VED BOOT | product name |
| SPROG | STOCK-SPROG | distinguished from UI-SPROG |
| GRUPPE-TEMP · E1 · GRUPPE OVER 110° · E2 · GRUPPE-SENSOR · ORD 114 · BRYGHOVED-SETPOINT | GRUPPEHOVED · E1 · GRUPPEHOVED OVER 110° · E2 · GRUPPEHOVED-SENSOR · ORD 114 · GRUPPEHOVED-SETPOINT | the correct term (group head = gruppehoved); the unit °C shows that it is a temperature. The stat label on HOME is still the short *GRUPPE* — *GRUPPEHOVED* cannot fit in a 94 px column |
| BRYG-TEMPERATUR | BRYG-TEMP | label + value collided in °F (item 7) |
| MÆLK-TEMPERATUR · MÆLK-TEMP · E2 · MÆLK-SENSOR | MÆLKETEMP · MÆLKETEMP · E2 · MÆLKESENSOR | the correct compound + room in °F |
| VAND-TEMP · SENSOR | VAND-SENSOR | label + value collided at ≥ 100.0° |
| SLIDER-SKALA · VÆLGER UKENDT (hint) | TRIN = SKØN (hint) + the note *"… · SLIDER 0-100"* | the hint reached into the gauge ring (item 7) |
| 24 T / 12 T | 24-TIMERS / 12-TIMERS | item 4 |
| ØVERST TIL VENSTRE (1,5 S) | ØVERST TIL VENSTRE (1.5 S) | item 4 |
| MERAKIPLUS STARTET FRA · SPEJL 112/113/114 (DIAGNOSE) | STARTET FRA · SPEJL 112-114 | room in the column in both languages |
| SKIFT FEJLEDE · *(free error text)* | SKIFT FEJLEDE · INGEN STOCK-BINÆR / FORK-FEJL / STOCK-UI STOPPEDE | fixed, translated reasons; the full error text is still in `boot_ui.log` |
| KUN STOCK-UI · MERAKIPLUS ER DANSK (note) | KUN STOCK-UI · MERAKI+: SE UI-SPROG | no longer true |
| splash: MERAKIPLUS · MERAKITECH … · V6.2 | wordmark MERAKI+ · tagline · V6.3 | item 1 |

All other Danish texts are verbatim as in v6.2.

**6 · Layout changes**
* SYSTEM has one more row; BOOT UI and SWITCH TO ORIGINAL NOW are rows 12 and 13 (the screen position at full scroll is unchanged).
* DIAGNOSTICS is 151 px longer (the ABOUT block); max scroll 431.
* The HOME sub-line's field has been widened from 296 to 336 px (room for °F values and the English texts); the ring allows it.
* List rows with long enum names choose the font in three steps (large → medium → small) and only then truncate with `…`. This happens in one place: *STOCK LANG. = KINESISK FORENKL.* (Chinese simplified) is shown in Danish as *KINESISK FOR…* in the list; the detail view shows the whole name.
* Toasts are now truncated with `…` if a text is wider than the pill (before it was clipped by the field's edge).
* The range line for rows without a unit had a double space before the hint; fixed.

**7 · Bugs found by the new layout checks (all also existed in v6.2)**
* **°F:** with TEMP UNIT = °F, *BRYG-TEMPERATUR* collided with the value in the list (11 px), and the HOME sub-line during steam was 6 px wider than its field. The layout test now measures temperature rows in both units.
* **The VOLUME detail:** range + hint were 336 px wide, where there are only 296 px inside the gauge ring; the text lay over the ring on both sides. There was no preview of exactly that screen, and the old test measured against the circle's chord instead of the ring's inner edge.
* **WATER SENSOR:** at a boiler temperature ≥ 100.0° the value collided with the label.
* **The SWITCH FAILED toast** could become about 460 px wide in a 356 px pill.

**8 · Local verification**
* **Self-test: 152 → 193 checks, 0 errors**, run twice: UI language English (`selftest.txt`) and Danish (`selftest_da.txt`). New:
  * *The table (9):* all 324 ids have EN + DA · the same printf conversions · the same numbers/units · no generation name (not even in the binary tag) · `merakiplus` only in paths/console · every character has a glyph · 196 classified strings × 2 languages within the pixel budget · endonyms · `TR()` per call with fallback.
  * *The language file (12):* missing → English · `da`/`en` · tolerant reading · 13 invalid values → English · empty file · a path that is a directory · no read permission · `lang_save` writes exactly 2 bytes with no leftover tmp file · an invalid language is rejected · a missing directory · a read-only directory (the old file intact).
  * *The UI LANGUAGE row (13), through real touch events:* the first row on SYSTEM · opens with the active language · **+** switches live without writing · CANCEL undoes · SAVE writes `da` and confirms in Danish · **config.json md5, `config.language`, write counter, `RESTART_NEEDED` and backup directory unchanged** · **0 cmd6 frames, 0 bytes to the UART** · remembered after a "restart" · back to English · STOCK LANG. does not change the UI language · write error → the language applies for the rest of the session, a warning shown · the phase name, error text and HOME texts follow the language without a restart.
  * *Layout (all existing checks now run in both languages, + new ones):* the detail view for all 25 rows (label and range against **the ring's inner edge**, temperature rows in °C and °F, a list row with the label + widest value + unit) · the system views in all states · DIAGNOSTICS (label pairs and cell pairs must not touch, also with worst-case values; the ABOUT block) · HOME (stat label trios against the columns, chart texts, buttons, the title line against the progress ring) · splash.
* **Previews: 51 → 56 scenarios, in two languages (112 images)**; **circle check 56/56 + 56/56, 0 violations**. New: `51_detail_ui_language` · `52_ui_language_live_preview` (one tap later: the whole screen in the other language) · `53_ui_language_saved_toast` · `54_home_other_language` · `55_diag_about`. `49`/`50` show the boot block as before (scroll 280). Contact sheets: English, Danish, `EN | DA side by side`.
* **Spec alignment:** unchanged, **ALL ALIGNED** — 24 rows (UI LANGUAGE is not a spec parameter).
* **Static build:** `zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2` → **0 warnings**. With `-Wall -Wextra`: the same two categories as v6.2 and no new ones (the one extra line is the new table row).
* `round_v6/i18n_check.txt`: the source has 0 lines with a generation name outside the self-test's ban list; the device binary has 0 such strings (apart from the build directory's path in the debug info, which cannot be displayed) and contains both taglines; the appendix is in sync; the same 56 scenarios in both languages.
* `artifacts/config.json` is untouched (md5 `<config-md5-redacted>`); the config copies after the English and the Danish preview runs are byte-identical.

**9 · Unchanged — and how it has been proven**
* **53 functions are byte-identical to v6.2** (compared textually against `merakiplus_v6_2.c`), e.g. `crc16`, `build_write`, the whole write queue, `rx_feed`, `word_ok`, `frame_policy`, `setting_mcu_push`, `act_push`, `act_push_steam_stop`, `proto_keepawake`, `phase_raw`, the whole `cfg_*` persistence, `boot_flag_set`, `boot_refresh`, `restart_pending`, `ld_path_for_a`, `bin_is_merakiplus`, `inp_open`/`inp_poll`, `dev_release_impl`, `fb_open`/`fb_present`, `pset`/`hspan`, `hit_test`, `ui_touch`, `revert_request`. `TRIG_NEVER`, `TRIG_START`, the intent asserts and the timing constants are the same lines.
* The six affected logic functions (`do_switch`, `sys_confirm`, `save_step`, `edit_step`, `settings_load`, `switch_launch`) have only changed in string lookups, a reason code for the translated toast and the two language hooks.
* The self-test's frame vectors (the 6 observed frames byte for byte), the trigger word audit (words 26/27/30 always 0), the boot push equivalence and the config persistence are the same tests as in v6.2 and are green in both languages.
* `run.sh`: md5 `3d799859549d2b8ba32245453022c20d` (unchanged). `boot/` and `sim/` were not touched.

**10 · For v6.4 (SIM HAL — not done in this round)**
* The SIM HAL against `sim/CONTRACT.md` has deliberately not been started. What v6.3 makes available for it: `MERAKIPLUS_LANGFILE` (a language file outside `/new_customer`), `--lang en|da` on the preview binary (previews, self-test, `--textw`), and that every text can be looked up by id in `strings.tsv`.

**Not verified (requires the device)** — see §11 O.

---

## CHANGES-v6.4

SIM build and E2E. The UI can now run without hardware against the test work stream's simulated MCU, and the whole E2E suite is green against the **real** UI in English and Danish. The device binary does the same as v6.3: only the version string and one toast text have changed (item 2). Usage, HAL, state fields and hooks are in [§13](#13-sim-build-and-e2e-v64); deviations from the contract are also in [sim/CONTRACT.md](../../sim/CONTRACT.md) (ADDENDUM v6.4).

**1 · Result**

| What | Result |
|---|---|
| `sim/selftest.py` (protocol level, unchanged) | **107 PASS / 0 FAIL** |
| 22 scenarios, real UI, **English**, with the UI profile | **22/22 scenarios · 553/553 checks** · settings_save_matrix **188/188** |
| 22 scenarios, real UI, **Danish**, with the UI profile | **22/22 scenarios · 553/553 checks** · settings_save_matrix **188/188** |
| The replay scenarios (v2 / v3 / session) | green in both languages — and also in the raw run |
| The same 22 scenarios **raw** (without the UI profile) | 17/22 scenarios · 527/553 checks · matrix 172/188 — the 26 checks that fail are exactly those in item 4 |
| merakiplus's own touch scenarios (4, real FIFO touch) | English **4/4 · 87/87 checks**, Danish **4/4 · 87/87 checks** |
| SWITCH TO ORIGINAL NOW without dry run (BOOT.md §9.2 in the sandbox) | **9/9** |
| Device build: self-test | **199 checks, 0 errors** — English and Danish (v6.3: 193; the 6 new ones cover the SIM boot push) |
| Device build: previews | 56 × 2 languages re-rendered; **circle check 56/56 + 56/56, 0 violations**; **100 of 112 byte-identical with v6.3**, the other 12 are the intended ones (item 2) |
| Spec alignment · config format · boot check · i18n check | **ALL ALIGNED** · 5/5 · 10/10 · 6/6 |
| Static build | device, SIM ARM and SIM host: **0 warnings** (counted with a fresh compiler cache) |

Reports, PNG snapshots and contact sheets: `sim/reports/v6.4/` — start with `SUMMARY.md`; one image per scenario in `overview_en.html` / `overview_da.html`.

**2 · The device build is v6.3 plus two texts — this is how it has been proven**
* **Source text after the preprocessor** (`build_sim.sh`): the device builds of v6.3 and v6.4 have been compared token for token. 65,838 tokens in both; **10 differences**: 8 × `"V6.3"` → `"V6.4"` and the toast in English and Danish. Nothing else — no new branch, no new call, no new constant.
* **How it is kept out:** everything SIM-specific is in `#ifdef SIM_RT` or in a handful of macros (`UART_DEV`, `INP_READ`, `SIM_TICK` …), which in the device build are empty or equal to the v6.3 expression.
* **The binary:** `strings merakiplus_v6` contains no `MERAKIPLUS_SIM`, `merakiplus-sim` or `boot_push`. `-Wall -Wextra` gives exactly the same warnings as v6.3 (25 × SET[] initializer, 1 × unused `fill_rect`).
* **Previews:** v6.3 was built from `merakiplus_v6_3.c` and rendered in a tmp directory; all 112 images were compared byte for byte (`v63_compare.txt`). The 12 that differ: `00_splash`, `26_diag`, `49`/`50_diag_boot*` and `55_diag_about` (the version string; in 49/50 the bottom edge of the VERSION cell can be seen at the top of the scrolled field) and `25_save_fail_no_ack` (the toast) — each in both languages.
* **The toast:** SAVE without an ACK now shows *SAVE FAILED · NO ACK · NOT SAVED* / *GEM FEJLET · INGEN ACK · INTET GEMT* (before: *NO ACK · NOT SAVED* / *INGEN ACK · INTET GEMT*). The text now says what went wrong before it says why; it takes up 304 px of the pill's 312 in Danish. The scenario `no_ack` asserts *GEM FEJLET*.
* **Unchanged:** protocol, frame policy (words 26/27/30 = 0, spec ranges, explicit start intent), keep-awake word1 = 0, `EVIOCGRAB`, circle clip, BOOT UI/switch logic (BOOT.md §9.2), the activity-blocked switch, the exit reminder, the EN/DA tables (323 of 324 ids unchanged), `run.sh` (md5 `3d799859…`), `boot/`.

**3 · The SIM HAL (only in `-DMERAKIPLUS_SIM`)**
* **The same main loop.** The SIM build uses the device's `main()`: poll rotation, `wq_step`, keep-awake, `inp_poll`, render cadence 33/100 ms. Three hooks (`SIM_TICK`, `SIM_FRAME`, `SIM_EXIT`) have been inserted and are empty on the device.
* **Framebuffer:** one 480×480 BGRA buffer in memory. `fb_present()` only copies the rects the compositor reports as changed — exactly like the device — so a bug in the dirty-rect logic would also be visible in the snapshots.
* **Touch:** raw 16-byte evdev records from a FIFO, decoded by the device's own `inp_poll()`. Records are processed according to their timestamps, so a 700 ms hold written all at once also lasts 700 ms.
* **UART:** `uart_open()`/`uart_flush()` are the device's; only the path has been switched to the pty.
* **Paths:** everything goes through the sandbox root. `MERAKIPLUS_SIM_ROOT` is required, and `/` is rejected.
* **State JSON and ctrl socket:** §13.3–13.4. The hooks call the same functions as a tap.
* **Boot push:** only in the SIM (item 6).

**4 · Where the reference client and the real UI differ (the UI profile)**

The scenarios were written against `sim/ref_client.py`. Five of them have expectations that can only be met by the reference client: its synthetic layout, or that it has neither merakiplus's guards nor merakiplus's deliberate deviations from `parity_spec.json`. Bending the UI to them would mean sending out-of-spec values, starting a brew during an active error, unlocking PAUSE TIME or allowing a dark screen. The scenario files have not been touched. Instead the runner reads `sim_ui_profile.json` when the client is the SIM build, and replaces the affected expectations 1:1 with what merakiplus does — the number of checks is unchanged, each override must hit exactly one expectation, and the report lists them all.

| Scenario | The reference client | merakiplus (deliberate, documented) | Overrides |
|---|---|---|---|
| `idle` | a page-colored band at the top; a phase-colored background | a gauge arc in the phase gradient; a dark background with white hero text (§2) | 2 color samples |
| `grind` | GRIND button at (130,160) | GRIND at (150,372) (§3) | 1 touch coordinate |
| `fault_e1` | starts a brew during an active error; the MCU stops it | the start is blocked: *ERROR ACTIVE · START BLOCKED* (§6) | 1 |
| `out_of_spec` | sends out-of-spec values; the sim logs them | the frame policy rejects the block; BLOCK ≥ 1 (§7a) | 1 |
| `settings_save_matrix` | parity_spec word for word | PRE-INF. TIME = `sub113 len1` · GRIND MODE = `sub2 len1` · SAVE of an unchanged value writes nothing · PAUSE TIME locked · BRIGHTNESS 0 left out · `init`/`patch` not exposed (§4.1–4.5) | 16 |

On top of that comes **one pure timing tolerance** (`replay_v3`): the window for the pre-infusion plateau ends 10–20 ms before the pressure jump at 8× playback and only passes when the client's state stream is coarse (the reference client: 10 Hz). merakiplus writes one line per changed frame; the window has been shortened from 15.0 to 14.9 s. All other time windows in the suite have at least 170 ms of margin (measured over three runs).

The five matrix rows have also been run through the real stepper + SAVE with touch (`sim_scenarios/ui_rows_real_frames.json`). `run_e2e.py --no-ui-profile` runs the scenarios raw.

**5 · Scenarios that required something of the UI — and what**

| Scenario | What was missing | Fix | Where |
|---|---|---|---|
| all (the universal check *boot push*) | merakiplus does not boot-push | boot push `sub1 len30` + `sub112 len3` through the frame policy | **SIM build only** |
| `no_ack` | the toast did not contain *GEM FEJLET* | a new toast text (item 2) | **device + SIM** |
| `no_ack`, `settings_save_matrix` | a `save` hook that hit a keep-awake in flight was rejected with *WAITING FOR CONTROLLER* | the hook waits up to 600 ms for an empty write queue | SIM hook only |
| `boot_flag_toggle`, `runtime_switch` | the runner's sandbox lacks what the BOOT UI guard reads → LOCKED | the SIM build adds to the sandbox (mounts, a slot B mock with the merakiplus tag, mock stock) | SIM build only |
| `runtime_switch` | the dry run only showed a toast; the scenario requires the goodbye screen | the dry run shows the goodbye screen for 1 s | SIM build only |
| `sleep_wake` | the frame policy rejects word1 = 1, and merakiplus wakes a sleeping controller within 3 s | `raw_write` allows word1 ∈ {0,1} and pauses keep-awake | SIM hook only |
| `grind`, `gesture_switcher` | a gesture written all at once to the FIFO lasted 0 ms | a touch queue driven by the records' timestamps | SIM HAL only |
| `brew`, `hotwater`, `steam_*` | the chip shows DONE for 8 s; the scenarios wait for READY | `phase` = the raw phase, `phase_ui` = the chip | state JSON only |
| `language` | `lang` must be `config.language`, not the UI language | `lang` + a new field `ui_lang` | state JSON only |
| `no_ack`, `out_of_spec`, `fault_e1`, matrix (text asserts) | the asserts are Danish; the UI may be in English | `toast.text` = the Danish table text, `toast.shown` = the screen's | state JSON only |
| `idle`, `grind`, `fault_e1`, `out_of_spec`, matrix, `replay_v3` | item 4 | the UI profile (runner), no UI change | — |

`crc_garbage`, `fault_e2`, `fault_e3`, `offline`, `replay_v2` and `replay_session` passed with the HAL alone.

**6 · Findings and open points**
* **Boot push on the device — requires a decision.** merakiplus has never boot-pushed; the contract assumed it. In run.sh mode it does not matter: the paused stock UI has done it. On a **slot B boot** (v6.2) the stock UI never starts. The MCU's register file is in RAM and is initialized to fixed start values (registermap §2: word 11 = 920, word 114 = 50, word 14 = 4, word 25 = 550 …), so after a cold start the controller has **a brew setpoint of 92.0 °C and a group head of 50 °C where the config says 87.0 and 70** — until merakiplus writes the words. That happens on SAVE, in the start blocks (brew/water send words 11, 14 and 17 along; steam word 25; grind word 4) and, for the brew weight and pre-infusion/group head, right before BREW. According to the static analysis the boiler therefore heats towards the wrong temperature until the first brew; this has **not been measured on the machine** (test plan P3). HOME would show it honestly (*BREW TEMP SET TO 92.0°*), but that is hardly desired. The code exists (`boot_push_step()`, only compiled in SIM/preview) and is tested in the SIM and the self-test: through the frame policy, deferred while an activity is running. Using it on the device requires two calls in `main()` (start + step) and `MERAKIPLUS_BOOTPUSH` defined in the device build. This has **not** been done in v6.4, because it is new write behavior on the real MCU — 30 words in one block — that has not been tried on the machine, and because the device build was to stay unchanged in this round. Test plan P3 first measures whether the problem exists. *(Done in v6.5.)*
* **`rx_feed` can stall for about 3.5 s (code reading, not reproduced).** If the parser sees `AA 55` followed by a length field > 129 in a non-special frame, it breaks out without advancing and stays there until the 8 kB buffer overflows. The UI would show OFFLINE and then recover by itself. Requires a bit error in exactly the length byte; `crc_garbage` does not hit it. The fix is one line (`if(L>REGN){ i++; continue; }`), but it has been kept out of v6.4 because the device build was to stay unchanged. *(Fixed in v6.5.)*
* **The host's images are not bit-identical with ARM code.** The same source compiled for ARMv7 and run emulated in a local container gives 56/56 previews that differ from the host's by on average 72 pixels (0.03 %) with ±1 in one color channel — and in one place a 3×8 px glyph fragment. The self-test is green on ARM in both languages (198 of 199 checks; the last one requires the vendor binary, which is not in the container). Invisible, but previews and SIM snapshots are therefore not pixel-exact predictions of the device.
* **The UI profile should become superfluous.** Its 22 overrides belong in `gen_scenarios.py`/`ref_client.py` (as `hotwater_mode` already is). The proposal is in the contract addendum.
* **`steam_auto` is flaky regardless of the client** (about 1 run in 9): the sim's after-steam peak can be at 650 for a single 10 ms tick, and then no client sees it (`word48 ≥ 650` fails with 649). Seen once in the full runs; the cause and the proposal are in the contract addendum (E), the evidence in `sim/reports/v6.4/flake_steam_auto/`.
* **`sim/play.py`** (the test work stream's play mode, which arrived along the way) already runs against the SIM build and uses the new `layout` hook for the real touch targets.

**7 · Frame time**

Measured as on the device — only `ui_frame()` — over all rendered frames in the E2E runs:

| Run | Frames | Average | Worst frame |
|---|---|---|---|
| 22 scenarios, English | 10505 | 0.219 ms | 4.16 ms (`settings_save_matrix`) |
| 22 scenarios, Danish | 10495 | 0.221 ms | 5.19 ms (`settings_save_matrix`) |
| own touch scenarios, English | 1904 | 0.194 ms | 5.93 ms (`ui_rows_real_frames`) |
| own touch scenarios, Danish | 1892 | 0.227 ms | 7.96 ms (`ui_rows_real_frames`) |

**Host figures (Apple silicon), not the device's — and they cannot be scaled up.** In real time the process sleeps 2 ms between rounds, so every frame starts with cold caches; that is why the average here is about 10× the bench figure in §8 (0.03 ms, a warm loop), which is what §8's extrapolation is based on. The worst frame in `settings_save_matrix` is a SAVE frame: `cfg_set_int()` with backup, `fsync` and rename runs inside `ui_update()` — it does so on the device too, where the flash write will cost more. The caveat from §8 stands unchanged: the device's frame time must be read on the device (DIAGNOSTICS, `FRAME MS NOW/MAX`), especially right after a SAVE. The emulated ARM run (bench under qemu: 1.0 ms per brew frame, 4.2 ms for a full rebuild) is not a device figure either.

**8 · Changes outside `merakiplus.c`**
* `sim/run_e2e.py` — additive: `MERAKIPLUS_LANGFILE` + `--ui-lang`, the UI profile (`--ui-profile` / `--no-ui-profile`), `--run-name`, `--scen-dir`, language/check count/frame time in the report. Reference runs are unchanged.
* `sim/CONTRACT.md` — ADDENDUM v6.4 added at the bottom; the contract text is untouched. No other files in `sim/` have changed; the scenarios are unchanged.
* `round_v6/build_previews.sh` — now compares all previews with v6.3 and fails on an unintended difference.
* New: `build_sim.sh`, `sim_e2e.py`, `sim_ui_profile.json`, `sim_scenarios/`.
* `boot/` was not touched. `artifacts/config.json`: md5 `<config-md5-redacted>` (unchanged).

**Not verified (requires the device):** everything. v6.4 changes two texts on the device; O1 (the splash now shows *V6.4*) and E (SAVE without an ACK shows the new toast) cover them.

---

## CHANGES-v6.5

Two fixes in the **device build**, both from the findings in CHANGES-v6.4 item 6: (1) merakiplus now boot-pushes the config to the controller itself — also on the device — and (2) the RX parser can no longer stall. In addition two new DIAGNOSTICS fields. Protocol, frame policy, keep-awake, the save path, activity recipes, boot/switch logic, texts and drawing code are unchanged; the SIM build's behavior is unchanged. The toast *SAVE FAILED · NO ACK · NOT SAVED* / *GEM FEJLET · INGEN ACK · INTET GEMT* from v6.4 stays as it is.

**1 · Result**

| What | Result |
|---|---|
| The device code: self-test | **241 checks, 0 errors** — English and Danish (v6.4: 199; the 42 new ones: 23 boot push on the device, 19 RX resync — item 5) |
| Mutation probe: the new RX tests against **v6.4's** parser | **11 of 19 fail** — the finding is thereby reproduced, not just read (item 3) |
| Previews | 57 × 2 languages re-rendered; **circle check 57/57 + 57/57, 0 violations**; **102 of 112 byte-identical with v6.4**, the other 10 are DIAGNOSTICS + the version string (`v64_compare.txt`); 1 new scenario (`56_diag_bootpush`) |
| Spec alignment · config format · boot check · i18n check | **ALL ALIGNED** · 5/5 · 11/11 · 6/6 |
| Static build | device, SIM ARM and SIM host: **0 warnings** (fresh compiler cache); the device binary reproducible; `-Wall -Wextra` = exactly v6.4's warnings |
| Device build vs. v6.4, per function/data object after the preprocessor | 1326 fragments unchanged · **14 changed · 14 new · 0 removed** — all on the list in item 7 (`sim_build_check.txt`) |
| `sim/selftest.py` (protocol level) | **107 PASS / 0 FAIL** |
| Boot push in real time against the simulated MCU (`bootpush.txt`) | **23/23** — "slot B boot" (MCU at 920/50 → 870/70 in 138 ms), restart in the middle of a brew with and without the guard |
| RX stall repro (the test work stream's `sim/rx_stall_repro.py`) | **v6.5: never OFFLINE, RESYNC = 1** · the v6.4 binary as baseline: about 4 s without RX (3.98 and 4.01 s in two runs), OFFLINE shown |
| Targeted scenarios (`idle`, `no_ack`, `crc_garbage`, `out_of_spec`, `replay_v3`, `brew`) | **6/6** |
| The whole suite, real UI, **English** | **23/23 scenarios · 589/589 checks** · settings_save_matrix 188/188 |
| The whole suite, real UI, **Danish** | **23/23 scenarios · 589/589 checks** · settings_save_matrix 188/188 |
| merakiplus's own touch scenarios · SWITCH NOW without dry run | English **4/4 · 87/87**, Danish **4/4 · 87/87** · **9/9** |

Reports: `sim/reports/v6.5/` — start with `SUMMARY.md`.

The suite now has 23 scenarios (v6.4: 22; `replay_steam_live` is the test work stream's new one). **It was run twice:** during the first run the test work stream corrected a time window in `replay_steam_live`, and the English run, which had already run the scenario with the old window, got 22/23 (the window ended 9 ms after a pressure jump — a timing margin in the scenario, not UI behavior). The figures above are from the rerun on the final scenario set, during which no file in `sim/` changed. Details: SUMMARY.md, *Run notes*.

**2 · Boot push on the device**

*What.* At startup — both in a slot B boot and in run.sh — merakiplus sends the two blocks the stock UI's `fuart_thread` sends when it starts (PARITY §1.3–1.4):

```
cmd6 sub1   len30   words 1..30      67 bytes
cmd6 sub112 len3    words 112..114   13 bytes
```

The code is v6.4's SIM boot push (`boot_push_step()`), now compiled in all build types; the `MERAKIPLUS_BOOTPUSH` flag is gone, and the write queue's block limit is 30 words everywhere (the v6.4 device: 24).

*Why.* On a slot B boot the stock UI never starts. The controller's register file is in RAM and gets fixed start values on every startup (registermap §2), so without a boot push it has **a brew setpoint of 92.0 °C and a group head of 50 °C where the config says 87.0 and 70** — until the first SAVE or the first start block. That was the open point in v6.4.

*The word layout is the stock UI's*, word for word:

| Word | Value | Source |
|---|---|---|
| 2 · 4 · 11 · 14 · 17 · 23 · 25 | `grindMode` · `autoGrindWeight` · `brewTemper` · `steamGears` · `autoHotWaterTarget` · `autobrewWeight` · `AtuoSteamTarge` | config.json, raw values (`model_data_init` 0xcef40) |
| 5 · 13 · 15 | 60 | the stock UI's fixed recipe words (`fuart_thread` 0x146810) |
| 1 | 0 | awake |
| all others in 1..30 — also the start words 3/6/9/16/22/24 and 26/27/30 | 0 | the stock UI's mirror is zero here at startup |
| 112 | `(preInfusionSwitch ? 0x100 : 0) \| preInfusionPressure` | packed |
| 113 | `(preInfusionTime << 8) \| breakTime` | packed |
| 114 | `headTemper` | whole °C |

With the current config the two frames on the line are `aa 55 06 01 1e 00 00 01 00 00 00 b9 00 3c 00 … 66 03 … 8a 02 … 62 3e` and `aa 55 06 70 03 04 01 05 06 46 00 c7 02`. The self-test compares both — and the corresponding ones for a fixture config with completely different values — with vectors computed outside the program with an independent CRC.

*Order.* Unchanged first: `sub0 = [1]` (PowerOn) and `sub1 = [0]` (awake). When the link is up and work_sta has been read: `sub1 len30` → ACK → `sub112 len3` → ACK. In the SIM both are acknowledged 138 ms after the UI start, i.e. during the splash.

*Through the frame policy, like everything else.* The blocks are queued with `wq_push()` without an intent: the start words and words 26/27/30 cannot be anything but 0, every word must be within its spec range, and every block gets the write queue's ACK wait with **one** retransmit (150 ms + 300 ms). A config value out of spec gets the **whole** `sub1 len30` rejected and counted (BLOCK +1) — it is never sent; `sub112 len3` is still sent.

*The activity guard.* `sub1 len30` writes the start words to 0. If an activity is already running when merakiplus starts — run.sh started in the middle of a brew, or merakiplus restarted during one — the block could interrupt it. Therefore:

* The decision is made **once**: the first time the link is up **and** word 33 (work_sta) has been read. Before that nothing is sent — not even if the link is only "up" because the controller has asked for the version.
* If grind, brew, hot water or steam is running at that moment (`phase_active()`: work_sta bit 6 · bits 1+2 · bits 1+3 · bits 5+14 — the same patterns that block activity starts and SWITCH NOW), **the push is skipped**. It is not deferred and not repeated by a later SAVE. The controller then has the values the activity was started with; every SAVE and every start block still writes its own words.
* If the machine is idle — `0101` (awake), `0001` (sleep/drain) or `0000` — the push is sent.
* An activity that starts **after** the decision (between the two blocks) **defers** the missing block until the machine is idle.
* The guard is deliberately tied to the four known activity patterns and not to "any bit beyond `0101`". An unknown bit during e.g. cold heating or water refill would otherwise get the push skipped precisely at the cold slot B start it is meant for — and the stock UI boot-pushes by itself without looking at work_sta.
* The SIM build does **not** have the guard by default: it defers, as in v6.4 (the contract behavior is unchanged). `MERAKIPLUS_SIM_BOOTPUSH_GUARD=1` gives the SIM build the device's guard, so it can be run against the simulated MCU (item 6).

*Outcome* — shown on DIAGNOSTICS (item 4):

| DIAGNOSTICS | Means | Repeated after a SAVE? |
|---|---|---|
| OK (green) | the block has been sent and acknowledged | no |
| NO ACK (amber) | sent + one retransmit, no acknowledgement; the next block was sent anyway | yes |
| REJECTED (amber) | the frame policy rejected the block: a config value is out of spec | yes — once the config is fixed |
| NO CONFIG (amber) | a boot push key is missing from config.json; nothing is sent (no guessed values) | yes |
| SKIPPED | the activity guard | no |
| WAITING / SENDING | not finished yet (no link, queue busy, activity in progress) | — |
| OFF | `MERAKIPLUS_NOBOOTPUSH=1` or `MERAKIPLUS_READONLY=1` | — |

*In run.sh mode* the paused stock UI has already sent the blocks; merakiplus sends the same values again. Two things are worth knowing: the low byte of word 113 gets `breakTime` again (the controller overwrites it itself at the next brew, §4a), and recipe words the stock UI may have set from an NFC card or a preset (13, 15, 18) go back to the boot values — exactly as a restart of the stock UI would.

*Disable:* `MERAKIPLUS_NOBOOTPUSH=1` gives the v6.4 behavior. `MERAKIPLUS_READONLY=1` sends no cmd6 at all, as before.

**3 · RX resync watchdog**

*The finding.* v6.4's `rx_feed()` stalled when a header candidate had a length field above 129 words, until the 8 kB buffer overflowed. During the work it turned out to be easier to hit than "a bit error in the length byte":

* A stray `AA 55` in front of a valid frame is enough: `AA 55 AA 55 83 …` is read as command `AA` with length `83` = 131 words. The same stall.
* Commands 3 and 8/0x88 were skipped without a CRC (7 and 15 bytes respectively). `AA 55 03` in noise right before a valid frame swallowed its first 4 bytes, and the frame was lost.

Both have been reproduced: the new self-tests fail against v6.4's parser (11 of 19), and in E2E a v6.4 SIM binary stands about 4 s without RX and shows OFFLINE after the test work stream's injection of one frame with a corrupted length byte — v6.5 never shows OFFLINE.

*The parser now.* A candidate (`AA 55 …`) only becomes a frame when the whole frame has been received and the CRC matches. Otherwise it advances one byte and searches for the next `AA 55`. No bytes are consumed without a CRC verdict. The only thing that can hold the parser is a candidate whose length field promises more bytes than have arrived — and that state has two guards:

1. **Impossible length** (above 129 words): discarded immediately.
2. **Watchdog:** an incomplete candidate that has waited `RX_RESYNC_MS` is discarded. The rest of the buffer is searched, so valid frames that have arrived *behind* it are not lost. Bytes whose latest arrival is equally old cannot be a frame on its way and are discarded the same way. `main()` calls the parser on every round (every 2 ms), so the guard also works when nothing arrives.

*The window: `RX_RESYNC_MS` = 400 ms.* Chosen from what has been measured:

| Measurement | Value | Source | Relation to 400 ms |
|---|---|---|---|
| The longest valid frame on the line (265 bytes, 57600 8N1) | 46 ms | PROTOCOL §2 | 8.7× below |
| Poll cadence, stock (merakiplus polls every 50 ms) | ~100–130 ms | UARTLIVE / BREW / HOTWATER / SESSION §2 | pauses *between* frames — do not affect the watchdog |
| The longest poll gap | 456 ms (HOTWATER), 451 (UARTLIVE), 318 (BREW), 145 (SESSION) | same | ditto |
| ACK latency, normal | 12–25 ms | HOTWATER, BREW §3.2 | — |
| **The longest MCU silence** (the first ACK after a start write) | **249–279 ms** | BREW §3.2 | 400 ms = 1.4× |
| The write queue's ACK budget (150 + 300 ms) | 450 ms | §7 | the window is **below**: a stuck candidate in front of an ACK does not in itself become *SAVE FAILED* |
| The OFFLINE limit | 1500 ms | §7 | a resync can never show OFFLINE |

The watchdog only measures a candidate that is waiting for the rest of itself. Between frames the buffer is empty, so poll gaps and ACK waits — regardless of length — do not count. The controller sends a frame in one go; a pause *in the middle of* a frame has not been seen in any capture, but a pause of up to 398 ms is tolerated (tested), so also the 279 ms.

*How quickly does it recover?*

| Case | The next valid frame is received |
|---|---|
| Impossible length (the v6.4 finding), a stray `AA 55` | in the same call — 0 ms |
| Too long, but a possible length; traffic continues | when the candidate's byte count has arrived and the CRC rejects it: 100 ms in the test (two poll replies later) |
| The same, and then silence | exactly 400 ms after the candidate |
| A truncated frame followed by traffic | immediately (CRC), or on arrival if the fragment is more than 400 ms old |
| An ACK caught behind a stuck candidate, no other traffic | 400 ms — before the write queue gives up at 450 ms; the SAVE succeeds |
| 4107 bytes of random noise with 20 embedded "worst" prefixes | 0 false frames, the buffer at most 262 bytes, the five frames right after all received, the first one immediately |

*Counters.* `rx_resync` counts discarded candidates: impossible length, watchdog (one per call) and buffer overflow (which can now no longer be reached). A CRC error on a whole frame is counted as before in `rx_bad`. Commands 3 and 8/0x88 with a wrong CRC now count as CRC errors; before they were skipped silently. Valid frames are handled exactly as before.

**4 · DIAGNOSTICS**

* The cell *RX CRC ERRORS* is now called **RX CRC / RESYNC** and shows `CRC errors / resync` (amber when either is above 0). On a healthy machine it says `0 / 0`.
* A new row at the bottom of the top block: **BOOT PUSH 1-30** and **BOOT PUSH 112-114** with the outcome from item 2. The row sits right below the fold; the boot block has moved down one row. Preview: `56_diag_bootpush`.
* Nothing else on the screen has changed: no toast, nothing on HOME. The boot push and a resync are invisible unless you open DIAGNOSTICS.
* 9 new string ids (Appendix A); the self-test's layout check measures the new row in all outcomes in both languages.

**5 · Self-test: 42 new checks**

*Boot push on the device (23)* — the guard enabled, ACKs delivered as real `0x86` frames through `rx_feed()`:
* frames on the line = independent vectors, for the real config and for a fixture config where all 12 fields differ from both the real config and the controller's start values;
* an independent audit of the block: words 26/27/30 = 0, start words = 0, words 5/13/15 = 60, all other unknown words = 0;
* nothing is sent without a link or before work_sta has been read; only the version reply has gone out;
* ACK → next block → done; the controller mirror = config afterwards; the push is sent once (10 s later: nothing new);
* no retransmit before 150 ms, exactly one at 150 ms with the same bytes; no ACK → NO ACK after 450 ms, and the next block is sent anyway;
* **the guard:** work_sta `0141` / `1107` / `1307` / `030B` / `4121` at startup → 0 bytes sent, not even 25 s after the activity has ended or after a SAVE; `0101` / `0001` / `0000` → the push is sent; an activity between the two blocks → block 2 is deferred 30 s and then sent;
* config out of spec → `sub1 len30` rejected (BLOCK +1, 0 bytes), `sub112 len3` sent; after a SAVE that fixes the value, the push is repeated; a completed push is not repeated;
* a missing config key → nothing is sent; read-only and `MERAKIPLUS_NOBOOTPUSH` → nothing is sent; the DIAGNOSTICS texts for each outcome.

*RX resync (19):* a corrupt length byte (impossible; possible but too long, with and without subsequent traffic) · the v6.4 scenario over 3 s of poll traffic (all 60 replies, the largest gap 50 ms) · a truncated frame in five variants · a burst of noise · each of 9 "worst" prefixes in front of valid frames · 40 valid replies + 1 ACK interleaved with noise, CRC errors, impossible and too long lengths, truncated frames and `AA 55 03` (all 41 received once, in order) · pauses *between* frames of 100–1400 ms and *in the middle of* a frame of 20–398 ms (no resync) · 450 ms in the middle of a frame (that frame is given up, the next one is received) · the version handshake and a valid command 3 · an ACK behind a stuck candidate · the window's three limits.

**6 · SIM build and E2E**

* **The SIM behavior is unchanged:** the boot push is deferred during an activity (no guard), and `MERAKIPLUS_SIM_NOBOOTPUSH=1` disables it. The repeat after a SAVE now lives in `save_step()` for all build types instead of in the SIM hook — the same condition.
* **New in the state JSON:** `rx_resync`, and `boot_push.sub1_len30` / `.sub112_len3` can be `skipped`. **New environment variable:** `MERAKIPLUS_SIM_BOOTPUSH_GUARD=1` (the device's guard). Addendum in [sim/CONTRACT.md](../../sim/CONTRACT.md).
* **Boot push against the simulated MCU** (`sim_e2e.py --only bootpush`, 23/23) — the same main loop and the same `boot_push_step()` as the device:
  * *"slot B boot":* the controller is set to its start values (word 11 = 920, word 114 = 50 …); the UI starts with the guard and brings words 1..30 + 112..114 to the config (870 / 70 …). The write order is `sub0 len1`, `sub1 len1`, `sub1 len30`, `sub112 len3` — and nothing else.
  * *Restart in the middle of a brew, with the guard:* UI no. 2 only sends PowerOn/keep-awake; the brew stops by itself at the target weight; the push is not sent afterwards either.
  * *The same without the guard (the contract mode):* the push is sent when the brew has ended.
* **RX stall:** the test work stream's `sim/rx_stall_repro.py` is run against v6.5 and against a SIM binary built from `merakiplus_v6_4.c` (`rx_stall.txt`).
* **The UI profile is out of use:** the test work stream's scenarios now assert merakiplus's guarded behavior directly, and the runner runs the SIM build with its own, empty default profile. `sim_ui_profile.json` from v6.4 is no longer read.

**7 · What has changed in the device build — the full list**

`build_sim.sh` compares the device builds of v6.4 and v6.5 after the preprocessor, fragment by fragment (function, data object, type), and fails on anything not listed here:

| | Fragments |
|---|---|
| **Changed (14)** | `rx_feed()` · `main()` (start + step of the boot push, the watchdog call, `MERAKIPLUS_NOBOOTPUSH`) · `save_step()` (repeat the push after a SAVE) · `diag_top_text()`, `draw_diag()`, `scroll_max()`, `DG_LAB` (DIAGNOSTICS) · `WFrame`, `wq_push_t()`, `wq_tx()` (block limit 24 → 30 words) · the string table and its ids · `splash_status()`, `MERAKIPLUS_MARK` (the version string) |
| **New (14)** | `boot_push_words()`, `boot_push_cfg_ok()`, `boot_push_start()`, `boot_push_step()`, `boot_push_resave()` (+ prototype), `bp_failed()`, `bp_text()` · the state `bp`, `bp_on` and the two enums · `rx_resync`, `rx_pend_ms` / `rx_last_ms` |
| **Unchanged** | the other 1326 — including the frame policy, the write queue's ACK/retransmit logic, keep-awake, the save path, the activity recipes, phase detection, the boot/switch logic and all drawing code |

**8 · Open points and caveats**

* **The word layout has never been seen on the line.** The stock UI's boot push was found by static analysis (PARITY §1.3–1.4) and has not been caught in a capture. merakiplus sends what the analysis says; test plan K + Q2 compare it with the stock UI's own. *(Later confirmed on the device — see [power.md](../power.md) §3.)*
* **A 30-word block is new on the real controller as seen from merakiplus** (the largest so far: 18 words). The stock UI sends it at every startup, and the controller's write handler is a `memcpy` with a band check (`sub + len < 33`), but the ACK latency for 67 bytes has not been measured. If it is above 450 ms, DIAGNOSTICS shows *NO ACK* — the values have then very likely been written anyway, and `MIRROR 112-114` + HOME show it.
* **NO ACK gives no new attempt by itself** — only at the next SAVE or the next start of merakiplus. This follows the task's "ACK wait + one retransmit". A limited number of automatic repeats, or a reconciliation of the mirror against the config, is a possible next round; it has not been done because it is new write behavior that cannot be tried without the machine.
* **If the controller restarts while merakiplus is running,** it is back on its start values, and merakiplus does not push again. The stock UI has the same gap. Not in this round. *(Done in v6.6: re-push.)*
* **The guard only knows the four activities.** Drain, back-flush and self-test, started from the stock UI right before run.sh, are not `phase_active()`; the push would then write words 8/20/28/29 = 0. merakiplus's keep-awake (word 1 = 0) already interrupts a drain at startup, so the order "service routine in the stock UI → run.sh" was not safe before either.
* **Build scripts could hide an error.** Both `build_sim.sh` and `build_previews.sh` sent their checks through `tee`, and an error in an embedded Python check just aborted the block with exit 0. Both now fail loudly. (Discovered when the i18n appendix was out of sync and the script still reported 0.)
* **The host's SIM binary has no stable md5** (Mach-O; every link gives a new one). The device binary and the ARM SIM binary are reproducible.
* **Outside this round, but relevant for a permanent slot B boot: the clock is 32-bit.** `now_ms()` returns `long`, which is 32 bits on the device (measured in the local ARMv7 container: `sizeof(long)` = 4, `time_t` = 8). The millisecond clock therefore wraps around **24.86 days after startup**. Time differences (`now − x`) survive the wraparound, but the main loop's timers are compared absolutely (`now >= next_poll`, `next_render`, `next_slow`, `next_ext`, keep-awake): a timer that has been set to one of the last milliseconds before the wraparound, and which the loop's 2 ms steps skip over, only becomes true again 49.7 days later — the poll or rendering then stands still until merakiplus is restarted, and the watchdog only looks at the PID. Estimated risk: a few percent per timer per wraparound. The bug is older than v6.5 (inherited from v4) and has not been fixed here; v6.5's new code only uses differences. The fix is a 64-bit time base throughout the file — a proposal for the next round. *(Done in v6.6.)*

**9 · Changes outside `merakiplus.c`**
* `build_sim.sh` — now compares against v6.4 per fragment (item 7); fails loudly.
* `round_v6/build_previews.sh` — compares all previews with v6.4 (`v64_compare.txt`); collects all checks at the end and fails if any of them does.
* `sim_e2e.py` — `sim/reports/v6.5/`; new parts `bootpush`, `rxstall`, `targeted`.
* [sim/CONTRACT.md](../../sim/CONTRACT.md) — ADDENDUM v6.5 added at the bottom. `sim/scenarios/`, `sim/mcu_sim.py`, `sim/run_e2e.py` and `boot/` were **not** touched.
* `merakiplus_v6_4.c` / `merakiplus_v6_4.bin` — v6.4 kept unchanged. `run.sh` (md5 `3d799859…`) and `artifacts/config.json` (md5 `<config-md5-redacted>`) are unchanged.

**Not verified (requires the device):** everything — test plan Q.

## CHANGES-v6.6

Three things: (1) the phase semantics now follow what the live session in `STEAMLIVE.md` has proven — two steam paths and concurrent phases; (2) the clock is 64-bit — up to and including v6.5 it wrapped after 24.86 days on the device; (3) if the controller restarts while merakiplus is running, the boot push is sent again. Protocol, frame policy, write queue, keep-awake cadence, the save path, activity recipes, boot/switch logic and the config format are unchanged. The state contract's field `phase` is unchanged.

**1 · Result**

| What | Result |
|---|---|
| The device code: self-test | **304 checks, 0 errors** — English and Danish (v6.5: 241; the 63 new ones: 29 phase semantics, 18 clock wraparound, 16 re-push — item 5) |
| The same self-test as a **real 32-bit ARM binary** (local ARMv7 container, `long` = 32 bits as on the device) | **303 checks, 0 errors** in English and Danish (one check requires the stock binary next to the config and is skipped in the container). The same source with v6.5's time type (`long`): **10 of the 18 wraparound checks fail** (`selftest_arm32.txt`) |
| Mutation probes: the new tests against v6.5's behavior | 32-bit clock: **10 of 18** fail · steam first on HOME: **6 of 29** · all steam as latched: **13 of 29** · no re-push: **11 of 16** — and 0 errors in other sections (`mutation_check.txt`) |
| Previews | **62 × 2 languages** re-rendered (57 from v6.5 + 5 new); **circle check 62/62 + 62/62, 0 violations**; **102 of 114 byte-identical with v6.5** — the other 12 are the version string (4 images per language) and the two steam texts (2 per language) (`v65_compare.txt`) |
| Spec alignment · config format · boot check · i18n check | **ALL ALIGNED** · 5/5 · 12/12 · 6/6 |
| Static build | device, SIM ARM and SIM host: **0 warnings** (fresh compiler cache); the device binary reproducible; `-Wall -Wextra` = exactly v6.5's warnings |
| Device build vs. v6.5, per function/data object after the preprocessor | 1354 fragments in v6.5: **1274 unchanged · 52 where the only difference is the time type (`long` → `ms_t`) · 28 changed · 31 new · 0 removed** — all on the list in item 7. Frame policy, the write queue's push/ACK, activity recipes, the boot push words and CRC are token-identical (`sim_build_check.txt`, 21 PASS) |
| `sim/selftest.py` (protocol level; the test work stream's) | **120 PASS / 0 FAIL** (v6.5: 107 — the test work stream has extended it along the way) |
| The whole suite, real UI, **English** | **25/25 scenarios · 644/644 checks** · settings_save_matrix 188/188 (v6.5: 23 scenarios; new from the test work stream: `steam_wand_flush`, `steam_auto_overshoot`) |
| The whole suite, real UI, **Danish** | **25/25 scenarios · 644/644 checks** · settings_save_matrix 188/188 |
| Phase semantics, line by line (`phases.txt`) | **20/20** — the LIVE recording in replay (4×) + the sim's own flush during a brew and overshoot, in both languages |
| Clock wraparound in real time (`wrap.txt`) | **15/15** — 2^31 and 2^32 ms; 32-bit baseline: 227 keep-awake writes in 6 s, a DONE chip without an activity, no state lines after the wraparound |
| MCU restart → re-push (`repush.txt`) | **14/14** — the controller back on the config 0.40 s after the restart; v6.5 baseline: stays at 920 / 50 |
| Boot push (the v6.5 test) · RX stall repro · SWITCH NOW without dry run | **23/23** · v6.6 never OFFLINE, RESYNC = 1 (v6.4 baseline: 4.0 s without RX) · **9/9** |
| merakiplus's own scenarios (4 touch + the new replay scenario) | English **5/5 · 110/110**, Danish **5/5 · 110/110** |
| Targeted scenarios (`idle`, `brew`, `grind`, `hotwater`, `steam_auto`, `steam_manual_stop`, `replay_steam_live`, `no_ack`, `out_of_spec`, `offline`, `sleep_wake`) | **11/11** |

Reports: `sim/reports/v6.6/` — start with `SUMMARY.md`.

**The suite was run twice.** The test work stream reworked the sim's steam model to STEAMLIVE while this round was being verified. The first full run (19:05–19:36) was hit by it: `sim/` changed during 7 of 12 parts, the sim's own self-test was at 105 / 2, and English and Danish gave 20/23 (581/589) — `brew`, `steam_auto` and `steam_manual_stop`, all on the sim's timing or steam model. The same three passed individually a few minutes later on a `sim/` that was standing still. The figures above are from the rerun (19:38–20:12) on the final set; during it `sim/` changed once, during the English suite, which still gave 25/25. `sim_e2e.py` now records a fingerprint of `sim/` before and after each part itself. Details: SUMMARY.md, *Run notes*; the first run is kept in `race_run1/`.

**2 · Phase semantics after STEAMLIVE**

*What the live session showed.* Steam is work_sta bits 5+14 (`0x4020`) — but there are **two paths** there, each with its own stop rule, and steam can run **at the same time** as a brew:

| | LATCHED auto steam | MOMENTARY wand / flush |
|---|---|---|
| Start | UI button: `cmd6 sub24 len8`, word 24 = 1 | the physical wand switch; **no UART frame** |
| Signature | word 24 = 1 while bits 5+14 are set; word 44 bit0 = 0, word 46 = 0 | bits 5+14 **without** word 24 = 1; word 44 bit0 = 1 and word 46 = 1 follow about 0.4 s later |
| Stop | **the MCU itself**, when the milk (word 48) reaches the target (word 25): clears the bits and then word 24. No stop write | when the wand is released. **No auto-stop** — not even above the target (the probe reached 69.5 °C with a target of 65.0 °C) |
| After the stop | the probe keeps rising about 3.9 °C over ~11 s (thermal inertia) | — |

*What merakiplus shows now.*

| State | Chip | Label / sub-line | Gauge | Button row |
|---|---|---|---|---|
| Latched steam | **STEAM·AUTO** | *MILK · TARGET 65.0°* / *AUTO-STOP AT 65.0° · OR STOP* — the target is **word 25** (the controller's own), not the config | milk with a target mark | **STOP STEAM** |
| Wand / flush | **STEAM·WAND** | *MILK PROBE · NO TARGET* / *NO AUTO-STOP · RELEASE WAND* | milk, **without** a target mark; no target line in the graph | **no STOP button** — the four buttons, STEAM highlighted as active |
| Brew + steam at the same time (`5327`) | **EXTRACTION** + the badge **STEAM·WAND** on the label line | *AUTO-STOP AT 36.0 G* (the brew's) | the brew's pressure + progress ring; **the weight** in the hero | BREW **and** STEAM highlighted as active |
| … the stat row | | *TIME · ≈BAR\* · **MILK*** — the milk temperature is where FLOW otherwise is | | |
| After an auto-stop | DONE | *DONE IN 41.3 S · RISES ≈4° MORE* (≈7° in °F) — the hero keeps following the probe | | |
| After the wand | DONE | *DONE IN 3.1 S* | | |
| Steam ordered, not yet running | READY | *STARTING STEAM…* (before: *STEAM ARMED · OPEN THE STEAM VALVE*) | | |

Previews: `07` (latched) · `57` (wand) · `58` (wand above the target) · `59` (brew + steam) · `60` (after an auto-stop).

*How it is decided* (`steam_track()`, called once per frame):

1. If bits 5+14 are set and word 24 ≠ 0: **AUTO**. The class is **sticky** until the bits are cleared. The reason: after a manual STOP word 24 = 0 is in the mirror immediately (the ACK echo), while the MCU only clears the bits a moment later — without the stickiness the chip would flash *STEAM·WAND*.
2. If bits 5+14 are set and word 24 = 0 (and not already AUTO): **WAND**. This applies from the first reply; word 44 bit0 / word 46 arrive about 0.4 s later and are noted as confirmation (`steam.wand_seen` in the state JSON), but the class does not wait for them — the MCU only has the two start paths.
3. If word 24 has not been read yet (only a `sub33` reply has arrived): **unknown** — the chip is the neutral *STEAM*, the sub-line is empty, and the STOP button is shown. STOP is always a safe frame; a missing STOP button during latched steam is not.

*Main activity and guards.* `phase_raw()` is unchanged (steam first) and is still what the state contract's `phase` and **all guards** use: any activity — also a wand flush — blocks starts, SAVE of MCU values, SWITCH NOW and the boot push. New is `phase_main()`: grind / brew / water take precedence over steam. It controls the chip, gauge, hero and time field on HOME. The brew clock runs undisturbed while the steam comes and goes; if the brew ends while the wand is still open, HOME switches to *STEAM·WAND* without a DONE view in between, and TIME then shows the steam's time.

*What has not changed:* the STOP write (`sub24 len8`, word 24 = 0), the start block, the 15 s limit for *NO START*, and the ctrl hook `action steam_stop` (it sends as before).

*Overridden text.* *STEAM ARMED · OPEN THE STEAM VALVE* was based on the static reading that display steam requires the wand switch. Live, the steam started 0.22 s after the write with word 44 bit0 = 0. The text is therefore *STARTING STEAM…*, as for the other three activities; the string id `TX_H_STEAM_ARMED` has been removed.

**3 · 64-bit clock**

*The bug.* `now_ms()` returned `long`. On the device `long` is 32 bits, so the millisecond clock wrapped 2^31 ms = **24.86 days** after startup. CHANGES-v6.5 item 8 described the consequence for the main loop's timers. The review for this round showed that it was bigger than that:

| What | With the 32-bit clock | Seen in |
|---|---|---|
| The poll, slow, ext and render timers (`now >= next_…`) | a deadline in the clock's last milliseconds that a long loop step skipped over only became true again 49.7 days later: **no poll / a frozen screen**, and the watchdog only looks at the PID | self-test (mutant): 60 + 15 + 3 queries in 8 s instead of 160 + 40 + 8 — nothing after the wraparound |
| Keep-awake (`now >= ka_next`) | during the last 30 s before the wraparound the deadline was "in the past": **a keep-awake write in every idle loop round** | E2E (32-bit baseline): 227 writes in 6 s, where there should be 1 |
| The stepper repeat (`now >= rep_next`) | a hold on − / + across the wraparound stalled | self-test: 400 instead of 3200 |
| All differences above 24.86 days (`now − x < limit`) became **negative** | a link that had been silent that long looked "up"; the DONE chip came back 24.86 days after the latest activity and stayed; the CRC warning and the latest toast likewise | self-test + E2E (the chip was on DONE from the start) |
| `/proc/<pid>/stat` field 22 read with `atol` | saturated at 2^31 ticks = **248 days of uptime**: the exit reminder ("the stock UI has old values") would always — or never — be true | self-test on 32-bit ARM |
| The RX counters (`long`, `++`) | signed overflow after about 2.6 years of continuous operation | self-test |
| The pulse animation `sinf(now/260.f)` | a `float` has 24 bits: after 4.7 hours the resolution was 2 ms, after 24 days 256 ms — the ring around the active button stuttered | review |

*The fix.* One type, `ms_t` = `int64_t`, for **all** time values — points in time, deadlines and differences — in all build types. The clock does not wrap (292 million years). `now_ms()` forms the sum in 64 bits before it is assigned (`ts_ms()`).

| Area | Now `ms_t` |
|---|---|
| Clock | `now_ms()`, `ts_ms()` |
| Main loop | the six timers have been moved from local variables in `main()` to `lt` and are driven by `loop_poll()`, `loop_graph()`, `loop_render_due()`, `loop_rendered()` — so the self-test runs **the same code** as the device |
| Protocol | the keep-awake deadline (`ka_next`), the write queue's ACK clock (`wq.sent_ms`: 150 + 300 ms), the RX resync window (`rx_pend_ms`, `rx_last_ms`: 400 ms), `last_rx_ms` (OFFLINE after 1.5 s), `last_bad_ms` |
| Touch | hold-to-start (`tch.t0`: 650 ms), the stepper repeat with acceleration (`tch.rep_next`) |
| UI | toast, ripple, page transition (`tr.t0`), splash, phase clocks (`act.t_act0`, `t_done`, `done_ms`, `t_req`, `t_col`), boot poll (`bs.t`), graph timestamps (`g_t[]`), the frame time statistics |
| Boot | seconds since boot (`mono_s()`, stamped in `RESTART_NEEDED`), the process start time (`proc_stat()`, now `atoll`), the wait in the runtime switch |
| SIM | `t_ms`, the touch queue's due times, fb/state cadence, hook deadlines |

In addition: the RX counters are `uint32_t` (wrap around in a well-defined way), the poll rotator is `unsigned`, and the pulse phase is taken from the clock's lowest 24 bits (the image is bit-identical to v6.5 for the first 4.7 hours — and just as smooth afterwards). There is no `long` left as a time type in the source; `build_sim.sh` checks it.

*What has not been touched:* the time **values** (all limits and cadences are the same numbers), and the 52 functions where the only difference is the parameter's type.

*Tested in three ways.*
1. **Self-test (18 checks)** with a mock clock across 2^31 ms and 2^32 ms: the main loop with one 40 ms step that skips all three poll deadlines; the render timer; keep-awake (exactly 2 writes in 65 s, at B + 10 000 and B + 40 000 ms); the ACK budget (retransmit at exactly +150 ms, given up +300 ms later); the resync window (exactly 400 ms); hold-to-start (nothing at 649 ms, start at 650 ms); the stepper repeat (the same value as far from the limit); toast; page transition; frame time statistics; differences of 24.86 days; 2^45 ms; a `/proc` start time above 2^31 ticks; the counters' wraparound.
2. **As a real 32-bit binary.** The self-test has been cross-compiled statically for ARMv7 and run in the local ARMv7 container (`getconf LONG_BIT` = 32): green in both languages. The same source with `typedef long ms_t` fails 10 of the wraparound checks on the same platform — and 0 on the host, where `long` is 64 bits. That is exactly why the bug could not be seen in any earlier local run.
3. **The real main loop in real time** (`sim_e2e.py --only wrap`): the SIM build is started with the clock 6 s before 2^31 or 2^32 ms (`MERAKIPLUS_SIM_UPTIME_MS`, only exists in the SIM build) and runs for 44 s against the simulated MCU — results in item 6.

**4 · Re-push after an MCU restart**

*The gap.* The controller's register file is in RAM. If the MCU restarts while merakiplus is running, it is back on its start values (brew 92.0 °C, group 50 °C, pre-infusion reset), and v6.5 did not push again. The stock UI has the same gap.

*Two signs of a restart:*

**(a) The version request arrives again.** The MCU sends `cmd 0x89` every 20 ms from its startup until it has got a reply — and only then (0 in 12 minutes of live traffic). merakiplus replies to each one as before. An `0x89` arriving more than 200 ms after the boot push left WAITING is an MCU startup *after* our push. The first handshake is before the push and does not count; a straggler from it is within the 200 ms and does not count either. When the round has been quiet for 300 ms, and any push in progress is finished, the boot push is started again.

**(b) The start value signature: word 112 = 0 and word 113 = 0.** These are the MCU's start values, and they can never be config values within spec (word 112's low byte is the pre-inf. pressure 3..4; word 113's high byte is the pre-inf. time ≥ 5). The signature therefore says unambiguously that words 112..114 have not been written since the MCU's startup. It must be present in **3 fresh reads in a row** (polled every 1 s), while the machine is idle, the link is up, the write queue is empty, and the config is loaded and in spec. It catches a restart where the `0x89` round was lost — and a boot push that the guard skipped while the controller was actually on the start values (a slot B boot with the wand open).

*The re-push is the boot push.* The same two blocks (`sub1 len30` + `sub112 len3`), the same word layout, the same gate (`wq_push()` without an intent → the frame policy), the same ACK wait with one retransmit — and **the same activity guard**, decided on a work_sta read *after* the restart. If an activity is running at that moment, it is skipped; if the signature is present afterwards, (b) takes it when the machine is idle. In the SIM build without `MERAKIPLUS_SIM_BOOTPUSH_GUARD` it is deferred, like the boot push.

*Limits* — so a controller that does not accept it does not cause a stream of writes:

| Rule | Value |
|---|---|
| Interval between two re-pushes | at least 5 s |
| Signature-triggered re-pushes | at most 3 per 10 min (sliding) |
| Config out of spec / missing | no signature-triggered re-push (the block would be rejected again) |
| `MERAKIPLUS_NOBOOTPUSH=1`, `MERAKIPLUS_READONLY=1` | no re-push |

*DIAGNOSTICS.* After a re-push the boot push row is called **RE-PUSH 2 · 1-30** (accent color) and **112-114**, where the number is the number of re-pushes since merakiplus started; the cells show the outcome of the latest push (OK, NO ACK, REJECTED, SKIPPED …). Without a re-push the row is unchanged *BOOT PUSH 1-30* / *BOOT PUSH 112-114*. Preview: `61_diag_repush`. Nothing else on the screen: no toast, nothing on HOME.

*Consequence for an open point in v6.5:* a boot push that ended in NO ACK, and where words 112..113 were actually not written, now gets up to 3 new attempts by itself through (b). If the blocks were written and only the ACK was missing, nothing happens — as before.

**5 · Self-test: 63 new checks**

*Phase semantics (29)* — all through `rx_feed()` with real `0x83` frames:
* latched: chip, target from **word 25** (fixture: 60.0° vs. the config's 65.0°), auto-stop text, gauge mark, the STOP button is shown and can be hit; maintenance pulses (`6121` / `4521`) change nothing; stickiness after the STOP ACK;
* auto-stop **row by row from STEAMLIVE §5.1** (`6121` → `2101` with word 24 = 1 → `0501` with word 24 = 0): DONE, the STOP button gone, **nothing sent**; the after-rise note; the hero follows the probe 65.2 → 68.9;
* wand alone: *STEAM·WAND* from the first reply, confirmed by words 44/46 later; no target text, no target mark, no STOP button, start blocked;
* **wand above the target:** the probe 56.0 → 69.5° with a target of 65.0° over 4 s — *STEAM·WAND* all the way, never DONE, and a tap where the STOP button would be sends no frame;
* concurrent **row by row from STEAMLIVE §6.2** (`1307` → `5327` → wand on → 66.9° → 69.5° → `1307`): `phase_raw()` = STEAM, the main phase = EXTRACTION, chip + badge, weight in the hero, milk in the stat row, the brew clock undisturbed; the brew ends with DONE for the brew and the whole brew time;
* variants: brew + latched steam (STOP shown), word 24 unread, grind + wand (the graph still takes the grind weight), the brew ends with the wand open, the link drops during steam, *STARTING STEAM…*, the badge's width in both languages.

*Clock wraparound (18)* — item 3.

*Re-push (16)* — the guard enabled, an "MCU" that replies with status every 50 ms, words 112..114 every 1 s and ACKs after 20 ms:
* the first handshake (3 × `0x89` before the push + a straggler) gives no re-push; all 4 are answered;
* a restart in the middle of the session (5 × `0x89`): nothing is sent before the round has been quiet for 300 ms and a fresh work_sta has been read; then `sub1 len30` = the independent vector from v6.5 and `sub112 len3` = `… 04 01 05 06 46 00 c7 02`; the controller back on the config; the DIAGNOSTICS row;
* two restarts close together: exactly 5000 ms between the two re-pushes;
* **the guard:** a restart with work_sta `4121` → skipped, 0 bytes; the signature triggers nothing as long as the activity is running; when the machine is idle, the push is sent (reason: start values);
* a restart **without** `0x89`: the signature triggers after 3 fresh reads (about 4 s); 2 + 1 (config) + 2 reads trigger nothing;
* budget: a controller that ACKs but does not keep the values: 3 re-pushes, then quiet; 10 min after the first one, one new one is given;
* a restart in the middle of a push (block A ACKed, block B without an ACK, `0x89`): the push is completed, and both blocks are sent again;
* without the guard (the SIM contract): deferred and sent when the machine is idle;
* no re-push: config out of spec, read-only, `MERAKIPLUS_NOBOOTPUSH` — `0x89` is still answered.

*The mutation probes* are run by `build_previews.sh` on every run and fail if a mutant gets through.

**6 · SIM build and E2E**

* **The SIM behavior is unchanged** for everything the test work stream's scenarios measure: `phase`, `work_sta`, `regs`, toasts, writes.
* **New in the state JSON:** `phase_main`, `phases` (all active ones, the main activity first), `steam{on, kind, last_kind, concurrent, wand_sw, wand_seen, ord24, target, milk, stop_btn}`, `shown.badge`, `repush{count, ver, defaults, last, armed, ver_reqs, defaults_sig}`; `regs` also has words 44 and 46. `shown.chip` is *STEAM·AUTO* / *STEAM·WAND* during steam.
* **New environment variable, SIM only:** `MERAKIPLUS_SIM_UPTIME_MS=<n>` — the clock starts at n ms.
* **New behavior, visible to the sim:** after `reboot` (and after `link closed` → `up`, which also resets the version handshake) merakiplus sends the boot push again. None of the 25 scenarios does either.
* **A new scenario at merakiplus:** `sim_scenarios/ui_replay_steam_live_x4.json` — the LIVE recording played back at 4×. `sim_e2e.py --only phases` then goes through **every state line** against the new fields — also in the test work stream's `steam_wand_flush` and `steam_auto_overshoot`, when they exist in the suite. Addendum in [sim/CONTRACT.md](../../sim/CONTRACT.md).
* **Phase semantics against the LIVE recording** (`--only phases`, 20/20): in the replay each of the 182 (EN) / 191 (DA) lines with latched steam has `steam.kind` = auto, the chip *STEAM·AUTO* / *DAMP·AUTO* and a STOP button; after the auto-stop the note *RISES ≈4° MORE* is shown, and the probe goes from 64.6° to 68.9°. Every line with work_sta `5327` has `kind` = wand, `phase` = STEAM (the contract), `phase_main` = EXT, chip + badge, the weight in the hero (21.6 → 27.9 g) and the milk in the 3rd stat field (51.4° → 66.7°); the probe reaches 69.5° without a STOP button and without DONE. No line is misclassified, and merakiplus sent 0 stop writes.
* **The same fields against the sim's own model** (the test work stream's two new scenarios, run in the suite): `steam_wand_flush` — 141 lines with `5327`, of which 48 with the probe above the target, all as above; after the release EXTRACTION continues without DONE. `steam_auto_overshoot` — 663 lines latched all the way, DONE with the note, the probe 64.9° at the stop and 68.8° afterwards.
* **Clock wraparound** (`--only wrap`, 15/15): with the clock started 6 s before the limit and 44 s in real time: state lines all the way (the largest gap 171 ms), RX 25.6 replies/s before and after, exactly 2 keep-awake writes (0.06 s and 30.03 s), a hold-to-start that begins before and completes after the wraparound starts the brew, which stops by itself at its target weight; the toast from before the wraparound disappears on time. **Baseline** (the same source with `typedef int32_t ms_t`): the chip is on DONE from the start, 227 keep-awake writes in the first 6 s, RX 130/s (a poll stream), and no state lines after the wraparound.
* **Re-push** (`--only repush`, 14/14): after the sim's `reboot` + start values merakiplus answers `0x89`, and 0.40 s after the restart words 11 / 114 / 112 are back at 870 / 70 / `0104`; the writes are exactly `sub1 len30` + `sub112 len3`; the sim does not report `boot_push_missing`. If the register file is reset without `0x89`, the signature triggers the re-push after 4.1 s. **Baseline** (a SIM binary built from `merakiplus_v6_5.c`): no writes, the controller stays at 920 / 50, and the sim reports `boot_push_missing`.
* **A/B step** (`--only ab`): scenarios that fail in the suite are run again against v6.6 and against the unchanged v6.5 binary on the same sim. In the rerun there was nothing to compare.

**7 · What has changed in the device build — the full list**

`build_sim.sh` compares the device builds of v6.5 and v6.6 after the preprocessor, fragment by fragment, and fails on anything not listed here. Fragments where the only difference is `long` → `ms_t` are counted separately.

| | Fragments |
|---|---|
| **Changed (28)** | *Clock:* `now_ms()`, `mono_s()`, `mono_fake`, `proc_stat()`, `restart_pending()`, `cfg_set_int()` (the stamp's format), `act` (field types), `rx_ok`, `rx_resync`, `main()` (the timers via `loop_*()`, the call to `repush_step()`) · *Phases:* `ui_update()`, `home_model()`, `HomeM`, `STAT_SET` + its enum, `draw_home()`, `draw_chart()`, `hit_test()` · *Re-push:* `rx_feed()` (counts `0x89` and reads of words 112..113), `bp`, `boot_push_start()`, `boot_push_step()` (the time the push left WAITING), `diag_top_text()`, `draw_diag()` · the string table and its ids · `splash_status()`, `MERAKIPLUS_MARK` (the version string) |
| **New (31)** | *Clock:* `ms_t`, `ts_ms()`, `lt`, `loop_poll()`, `loop_graph()`, `loop_render_due()`, `loop_rendered()` + a `_Static_assert` · *Phases:* `stm` + enum, `steam_bits()`, `wand_sw()`, `steam_track()`, `steam_stop_visible()`, `steam_target()` (+ prototype), `phase_main()`, `steam_label()` + a `_Static_assert` · *Re-push:* `rps` + enum, `bp_guard_cfg`, `ver_n`, `ver_ms`, `repush_count()` (+ prototype), `repush_cfg_sane()`, `mcu_defaults_sig()`, `repush_begin()`, `repush_step()`, `diag_label()` |
| **Time type only (52)** | e.g. `wq_step()`, `wq_tx()`, `proto_keepawake()`, `link_up()`, `phase_raw()`, `act_guard()`, `act_request()`, `save_begin()`, `save_step()`, `ui_touch()`, `ui_frame()`, `do_switch()`, `switch_launch()` — the full list is in `sim_build_check.txt` |
| **Unchanged (1274)** | including `frame_policy()`, `word_ok()`, `wq_push_t()`, `wq_push()`, `wq_on_ack()`, `wq_pop()`, `act_push()`, `act_push_steam_stop()`, `setting_mcu_push()`, `boot_push_words()`, `boot_push_resave()`, `build_write()`, `frame_query()`, `frame_version()`, `crc16()`, `boot_flag_set()`, `settings_load()` and all other drawing code |

**8 · Open points and caveats**

* **None of it has been run on the device** — test plan R.
* **The clock wraparound cannot be tested on the device in a reasonable time** (24.86 days). The proof is the three local tests in item 3, one of them on 32-bit ARM. `MERAKIPLUS_SIM_UPTIME_MS` deliberately does not exist in the device binary.
* **`0x89` is only known as a startup request.** If it also arrives in other situations (not seen in any capture), every round gives one re-push: the same values written again, with the guard. The counter on DIAGNOSTICS shows it — R7 asks for it to be read after a long session.
* **An MCU restart cannot be provoked safely on the machine.** merakiplus never sends word 30 (reboot), and the MCU shares power with the display. (a) and (b) have therefore only been tested against the simulated MCU and in the self-test.
* **The signature only looks at words 112..113.** A restart where (a) was lost **and** where block B was nevertheless written afterwards (e.g. by a brew start, which syncs words 112..114), but block A was not, is not detected: word 11 could be at 92.0 °C. The brew start itself sends word 11 in its block, so a brew gets the right temperature; the boiler would just hold 92.0 °C in between. A full reconciliation of the mirror against the config has not been done.
* **A wand flush blocks starts from merakiplus.** While bits 5+14 are set, the start buttons answer *ACTIVITY IN PROGRESS*. In the live session the brew was started first and the wand opened afterwards; the reverse order has not been tried on the machine, and merakiplus does not allow it.
* **Word 24 = 1 without the steam starting.** If the controller does not give bits 5+14 within 15 s, merakiplus shows *NO START · CHECK THE MACHINE* — but leaves word 24 as it is. Whether the MCU can then start the steam later is unknown. A STOP write on the timeout would close it; it has not been done, because it is new write behavior outside this round.
* **The after-rise (≈4°) is one measured number** from one session (cold milk 12 → 65 °C). It is information in the sub-line, not a calculation.
* **The host's SIM binary still has no stable md5** (Mach-O). The device binary and the ARM SIM binary are reproducible.
* **New ground truth from the device, arriving during this round** (the boot work stream's `boot/ondevice/GROUND-TRUTH.md`, read — not touched): the stock UI's own boot push has now been captured on the line. `sub1 len30` is **byte-identical** to merakiplus's (67 bytes, `… 62 3e`) — the open point from CHANGES-v6.5 item 8 is thereby closed for that block. `sub112 len3` differs in **one byte**: the stock UI sends word 113 = `0600` (low byte 0), merakiplus sends `0605` (low byte = `breakTime` 5): `… 04 01 00 06 46 00 c7 ce` vs. merakiplus's `… 04 01 05 06 46 00 c7 02`. The controller overwrites the low byte itself at the next brew (§4a), and it has no known effect. **Not changed in v6.6:** the finding is outside the round's three items and arrived after the verification had been run; the fix is one line in `boot_push_words()` (`pack113(time, 0)`) plus the two test vectors, and it also applies to the re-push. Test plan Q2/Q3's expected frame is merakiplus's, not the stock UI's. The same capture shows that the stock UI does not send a separate PowerOn / keep-awake before the blocks; merakiplus does (unchanged since v6). *(Fixed in v6.7.)*

**9 · Changes outside `merakiplus.c`**
* `build_sim.sh` — compares against v6.5; time type differences separately; a check that the safety-critical fragments are token-identical; the self-test as a 32-bit ARM binary.
* `round_v6/build_previews.sh` — compares all previews with v6.5 (`v65_compare.txt`); mutation probes (`mutation_check.txt`).
* `sim_e2e.py` — `sim/reports/v6.6/`; new parts `phases`, `wrap`, `repush`; notes for each part whether the test work stream's files changed along the way.
* `sim_scenarios/ui_replay_steam_live_x4.json` — new (additive).
* [sim/CONTRACT.md](../../sim/CONTRACT.md) — ADDENDUM v6.6 added at the bottom. `sim/scenarios/`, `sim/mcu_sim.py`, `sim/run_e2e.py` and `boot/` were **not** touched.
* `merakiplus_v6_5.c` / `merakiplus_v6_5.bin` — v6.5 kept unchanged. `run.sh` (md5 `3d799859…`) and `artifacts/config.json` (md5 `<config-md5-redacted>`) are unchanged.

**Not verified (requires the device):** everything — test plan R.

---

*— design sub-agent, 2026-11-17 (v6.6: phase semantics after STEAMLIVE + 64-bit clock + re-push after an MCU restart). Local work only: source, builds, simulated MCU, previews and self-test; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` and `sim/run_e2e.py` were not touched (in `sim/` only the contract addendum and `reports/v6.6/` are new), and the device was not contacted.*

*— design sub-agent, 2026-11-17 (v6.5: boot push + RX resync in the device build). Local work only: source, builds, simulated MCU, previews and self-test; `boot/`, `sim/scenarios/`, `sim/mcu_sim.py` and `sim/run_e2e.py` were not touched (in `sim/` only the contract addendum and `reports/v6.5/` are new), and the device was not contacted.*

*— design sub-agent, 2026-11-17 (v6.4: SIM build + E2E). Local work only: source, builds, simulated MCU, previews and self-test; `boot/` was not touched, in `sim/` only `run_e2e.py` (additive) and the contract addendum were changed, and the device was not contacted.*

*— design sub-agent, 2026-11-17 (v6.3: product name + i18n). Local work only: source, cross-build, offline previews in two languages and self-test; `boot/` and `sim/` were not touched, and the device was not contacted.*

*— design sub-agent, 2026-11-17 (v6.2: boot slot integration). Local work only as below; `boot/` was not touched, and the device was not contacted.*

*— design sub-agent, 2026-11-17 (v6.1 alignment). Local work only: source, cross-build, offline previews, self-test and static lookups in `demo_v9.dis`. The machine was not touched; `artifacts/config.json` is unchanged (md5 `<config-md5-redacted>`).*

---

## CHANGES-v6.7

**Network guard (Wi-Fi at startup + self-healing) and word 113 in the boot push.** On a slot B boot no Wi-Fi was started (INSTALL-REPORT §3.1). The stock UI brings Wi-Fi up itself via `libmi_wlan.so`; `demo.sh` does not. The mechanism has been proven by reverse engineering (`net_re/`, the comment "NET" in the source).

1. **What merakiplus does — only when the stack is DOWN:**
   * The init script `/config/wifi/ssw01bInit.sh` is only run if `wlan0` is missing.
   * `/appconfigs/wpa_supplicant.conf` is written from `config.json` in exactly the `save_config` format (md5-proven) and only if the contents differ.
   * Then `wpa_supplicant -i wlan0 -Dnl80211 -c …` is started and 1 s later `udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1` — both with the stock UI's arguments. If `wlan0` still has an IP and a default route, they are kept (the same IP).
   * If the supplicant is alive but `wlan0` is down: `ifconfig wlan0 up`. No IP (8 s) or no default route despite carrier (3 s): `udhcpc` with backoff.
   * **A living wpa_supplicant is never touched** (0 `kill()` calls in the source). An init script that hangs is logged after 25 s and not killed.
   * Failing starts get a backoff of 5 → 10 → 20 → 40 → 60 s.
   * `wifionoff = 0` or an invalid config (empty ssid, password < 8): the guard is OFF. `MERAKIPLUS_NONET=1` disables it completely.
2. **Visible:** DIAGNOSTICS → **NETWORK · WPA PID / IP / STATE** (e.g. `841 / <display-ip> / UP`, green). Log in `/tmp/merakiplus_net.log` (transitions only; the password is never written). `merakiplus --net-status` (read-only) and `--net-watch [s]`.
3. **Word 113 in the boot push** is now sent as the stock UI's recorded boot push, `0x0600`: the low byte (breakTime) is 0 (GROUND-TRUTH.md). The SAVE path still packs `time<<8 | breakTime` (e.g. `0605`). Self-test vector: sub112 len3 = `aa 55 06 70 03 04 01 00 06 46 00 c7 ce`. With breakTime 0 / 9 / 255 the boot push's word 113 is still `0600`. `0x0600` is within the frame policy and is not the MCU's start value signature.
4. **Proof (local):**
   * The self-test section *v6.7 network guard*: 20/20 checks against a model of wlan0/wpa_supplicant/udhcpc. It covers a cold start, a healthy stack (0 interventions), a kill of the supplicant, the IP gone, wlan0 down, the AP gone for 2 min, no default route, an init script that hangs, backoff and the config guard.
   * E2E in real time against the SIM build's Wi-Fi model (`sim_e2e.py --only net`): 10/10.
   * The mutation probes `net0` and `dhcpall` are caught.
5. **Proof on the device:** `net_heal_proof.py` is ready. It is read-only telnet plus one controlled kill of the stock UI's wpa_supplicant while demo_v9 is SIGSTOPped. There are no reboots and no writes to the flag, slots or config (`/tmp/merakiplus_t/` on tmpfs), and a fallback kicks in after 150 s. **Status 2026-11-17 23:01:** not run. The device was not on the network (see [CHANGES-v6.9](#changes-v69) item 6). No reboots and no writes to the flag, slots or config in this round.

## CHANGES-v6.8

**The REVERT button on HOME has been removed.** It was a phase 2 safeguard.
* HOME = gauge + stats + activity icons + page dots (y 420). There is **no touch zone** left where the pill was. Self-test: the whole field 150..329 × 404..479 gives `T_NONE`, both idle and during a brew. No UI string (EN/DA) mentions REVERT. The mutant `pille` is caught.
* The escape routes are unchanged:
  * SYSTEM → **SWITCH TO ORIGINAL NOW** (in run.sh mode a clean exit; run.sh SIGCONTs demo_v9)
  * the BOOT UI row
  * the switcher gesture
  * run.sh's trap
* The switch screen's run.sh text is now *RUN.SH MODE · CLEAN EXIT*.
* Nothing new on the device apart from the HOME footer. See `REVERT.md` "v6.8".

## CHANGES-v6.9

**Display conditioning of the graph and hero, a stable weight source and a weight dead band.** Only what is drawn has changed. Details and constants are in [§14](#14-display-conditioning-and-stable-weight-v69).

1. **Graph:**
   * median-3 + EMA 0.18 per sample
   * a pulse gate on the pressure
   * an activity change follows median-3 for 2 samples
   * scale = p90 of the active samples × 1.15, with ticks below the ceiling for samples above the top
   * pressure is only drawn during brew/water; pulses at idle are ticks on the baseline
   * the chart is "active" at idle only while a brew/water run is in the window
2. **Hero, gauge and BAR:** median-3 + EMA 0.25 at 10 Hz, snap to raw on a phase, source or setpoint change.
3. **Weight:**
   * The source is latched: word 39 by default, word 38 only during a real grind and its DONE.
   * A switch requires 300 ms; a UI-started switch happens immediately.
   * Dead band < 0.25 g → 0 in the hero, chart and stat field.
   * Signed (int16) all the way.
4. **DIAGNOSTICS → GRAPH:** smooth (default) / raw, only for the current session. The RAW chart is labelled **RAW**.
5. **The predecessor's v6.9 intermediate state** (kept as `merakiplus_v6_9wip.c` / `.bin`) had the conditioning, but:
   * no v6.9 self-tests and 2 red v6.6 self-tests
   * p90 over the whole window had been dropped in favor of a "hold" scale
   * a snap to raw on a phase change: a brew that ended in a deadhead pulse gave a hump that took over 2 s to decay
   * no gate: 2-sample pulses got through median-3 as a 1.5 s hump at the ceiling
   * the weight source followed the phase per sample
   * no dead band
   * the idle chart was still switched on by the maintenance heating cycles

   All of that has been fixed here. Along the way the before/after run showed two bugs in my own first version, which have also been fixed and have self-tests:
   * one flash of the grind weight (18.7 g) in the stat field when the source switched 38 → 39
   * a 2 s long tail when the cup was lifted off at idle
6. **Verification (local):**

| What | Result |
|---|---|
| Self-test (the device's code, preview build) | **354 checks, 0 errors** in EN and DA (v6.8: 329). Of which the v6.9 section: **25/25** — spike trains, steps, phase snap, deadhead pulse at a change, setpoint snap, idle after a brew, weight source during maintenance bits + a bit-6 glitch, real/UI-started grind, source change without a flash, cup off/on, dead band (idle + brew), signed tare |
| Self-test as a real 32-bit ARM binary (local ARMv7 container) | **353 / 0** (EN + DA; one host-specific check fewer); with `long` as the time type 10 wraparound checks fail (as before) |
| Mutation probes | **10/10 caught** — 4 from v6.7/v6.8 + 6 new: phase source, no dead band, unsigned weight, no gate, p90 over the whole window, pressure curve at idle |
| Device build vs. v6.8 | 0 warnings (device, SIM ARM, SIM host; -Wall -Wextra = the same two categories as v6.6). Reproducible. 17 changed + 23 new fragments, all display. **29 control/protocol fragments token-identical**: frame policy, write queue, activity recipes + guard, phase logic, SAVE packing, boot push, network guard, RX, CRC, flag |
| Previews vs. v6.8 | **90 of 126 byte-identical** (HOME at idle, lists, details, BOOT UI, switch pages). 36 intended: 20 HOME during an activity + the version string + DIAGNOSTICS' GRAPH row. **4 new scenarios × 2 languages**: `63_home_brew_spikes`, `64_home_brew_spikes_raw`, `65_home_idle_after_brew_pulses`, `66_diag_graph_raw` |
| Circle check | 67/67 + 67/67, 0 violations |
| Spec alignment · i18n · boot/config checks | ALL ALIGNED · appendix in sync (349 string ids) · green |
| E2E against the simulated MCU (quick, cf. [§15](#15-development-iteration-dev-iteration-policy)) | `sim/selftest.py` **120/120** · `run_e2e.py --all --quick --parallel 4` (EN): **5/5 scenarios, 97/97 checks in 44 s** (`quick_en/report.md`). The full suite has **not** been run in this round; it waits for CI/CD or a release. Before the policy arrived, the predecessor managed to run EN, DA and REF against the same SIM binary (`fbd8c89c…`): 25/25 scenarios and 644/644 checks in each, plus the UI scenarios in EN with 6/6. The UI scenarios in DA were interrupted and are incomplete |
| Before/after (graph + weight) | `ui_calm_graph` against v6.8 and v6.9 with the same sim and seed: `calm.txt` **3/3**, `calm_stats.txt` **8/8**. The weight at idle showed −0.2 / 0.0 / 0.2 g in v6.8 and now shows 0.0 g. There are 0 weight spikes, and the source switches exactly 2 times (grind in and out). The pressure graph at idle is inactive despite 4 + 24 maintenance pulses (raw up to 16 240). After the brew the y top stays at 1555..1585 despite pulses up to 15 152 and never jumps. The ≈BAR field changes 21 times during the brew, vs. 148 in v6.8. Images (left v6.8, right v6.9: idle, extraction, DONE, idle after a brew, steam, tare): `calm_key_v68_v69.png` · `calm_key_chart_v68_v69.png` |
| Self-healing on the device | **Not run.** The device did not answer on 2026-11-17 at 23:01: <display-ip> gave "Host is down", and no host in the LAN's ARP table had telnet port 23 open. `net_heal_proof.py` is ready and only requires the device to be on the network. Locally the network guard is proven with self-test 20/20 and the SIM Wi-Fi model 10/10 |

7. **Files:**

| File | md5 |
|---|---|
| `merakiplus.c` (v6.9) | `440be5124c0d986ec23e4c3b08f84149` |
| `merakiplus_v6` (device binary) | **`2dce4eba0b4ce68dcd0369e955cf3b77`** |
| `merakiplus_v6_sim_arm` · `merakiplus_v6_sim` | `cbebc8b128f413c1d40a83de2bd6343a` · `fbd8c89c236f508ec725505c519b7b91` (host; changes on every link) |
| `merakiplus_v6_9wip.c` / `.bin` (the state at the start of the round) | `048a40ea7894dab5b57a58f6d9af8ead` / `bcb1b50c913700c64a6cfc39bb5a5776` |
| `merakiplus_v6_8wip.c` / `.bin` (v6.8 = the previous device binary) | `5cdcc14b0503c67b939e27b6579655ff` / `75887fb154ffef20cc72696064e16c39` |

   Report: `sim/reports/v6.9/SUMMARY.md`. New/changed tools:
   * `build_sim.sh`: reference v6.8 (the v6.8 version kept as `round_v6/build_sim_v68.sh`)
   * `round_v6/build_previews.sh`: `v68_compare.txt`, 6 new mutants
   * `round_v6/fbcrop.py`: chart crops, before/after sheets
   * `round_v6/calm_stats.py`: before/after figures per segment (weight + pressure graph) from the state lines
   * `sim_e2e.py`: parts `ref` and `calm`
   * `sim_scenarios/ui_calm_graph.json`: UI side, additive

   `sim/` was not touched, apart from an addendum in `sim/CONTRACT.md` and the reports under `sim/reports/v6.9/`.

8. **Final round (2026-11-17, after the DEV-ITERATION-POLICY):** `merakiplus.c` has **not changed** (md5 `440be512…` = the state at the start of the round). Both binaries have been rebuilt from the source and are byte-identical to the delivered ones: `merakiplus_v6` `2dce4eba…` and `merakiplus_v6_sim_arm` `cbebc8b1…`, with 0 warnings in all three builds (fresh zig cache). In the round, `build_previews.sh` (1 min 13 s), `sim/selftest.py`, the quick run and the calm part were run. That filled in the table in item 6 and [§15](#15-development-iteration-dev-iteration-policy). `merakiplus_v6_9wip.c` / `.bin` are the predecessor's state and have been kept unchanged. The source has not changed, so there is no new "pre-round" copy.


## CHANGES-v6.10

**Pressure as a ≈bar estimate instead of "UNCALIBRATED".** Only what is shown has changed. Details, tables and caveats: [§16](#16-pressure-estimate-v610).

1. **Anchor and scale:** P0 = **358 raw** (the pre-infusion plateau in the recording, BREW.md §6) = config `preInfusionPressure` (**4 bar**) → k = 4/358 = **0.011173 bar/raw**. The anchor is read from the config, so a changed `preInfusionPressure` changes the scale (outside 1..15 → 4). Constants: `P_ANCHOR_RAW` 358, `P_ANCHOR_BAR_DEF` 4, `P_EST_MAX` 20. v6.9's hypothesis (`P_HYP_NUM/P_HYP_DEN` = 9 bar at 1346) has been removed.
2. **Computed values:** pre 358 → **4.0 bar** · extraction 1346 → **15.0 bar** (1379 → 15.4) · deadhead 16 670 → 186 bar linearly → **not shown**: *PULSE* on HOME, *MAINTENANCE PULSE* (idle) / *PULSE, NO PRESSURE* (activity) on DIAGNOSTICS. The limit is 20 bar = 1790 raw.
3. **Texts:** `≈BAR*` → **≈BAR · EST.**; `TRYK* UKALIBRERET` / `PRESSURE* UNCAL.` → **TRYK ≈BAR · EST.** / **PRESS. ≈BAR · EST.**. No string contains UKALIB/UNCAL any more (self-test).
4. **The footnote/basis:** a tap on the chart (when it shows pressure) or the BAR field (during a brew) → toast *PRE-INF. 4 BAR = RAW 358 · LINEAR*. No frames, no phase change (self-test). DIAGNOSTICS has a new block **PRESSURE · ESTIMATE**: the anchor line (`4 BAR = RAW 358 · k 0.01117`), the "now" line (raw word 51 → ≈bar or pulse, amber) and two caveat notes.
5. **The RAW view is unchanged raw:** with DIAGNOSTICS → GRAPH = RAW the BAR field shows word 51 as a raw number under the label **PRESSURE RAW** (v6.9 showed the unconditioned value converted to bar).
6. **The version string** is V6.10. `TX_DV_VERSION`/`_RO` have been moved from the class budget (168 px) to the explicit cell layout test (right column, 228 px): *MERAKI+ V6.10 RO* is 174 px and fits in the cell.
7. **Verification (local, cf. [§15](#15-development-iteration-dev-iteration-policy) — the full E2E suite has not been run):**

| What | Result |
|---|---|
| Self-test (preview build, the device's code) | **368 checks, 0 errors** in EN and DA (v6.9: 354). New section *v6.10 pressure estimate* **14/14**: scale (358 → 4.0; 1346 → 15.0; 1379 → 15.4), HOME during pre and extraction, caption + no UKALIB/UNCAL, toast from the chart and BAR field (no frames), no toast at idle without a brew, config anchor 3 bar (a config copy → 11.3) and back to 4, an anchor outside 1..15, the pulse limit, a persistent deadhead → PULSE, DIAGNOSTICS pulse/maintenance, idle without a pulse, the RAW view. Layout: the PRESSURE · ESTIMATE lines and the toast fit for anchors 1..15 and 5-digit raw values |
| Self-test as a 32-bit ARM binary (local ARMv7 container) | **367 / 0** (EN + DA); with `long` as the time type 10 wraparound checks fail (as before) |
| Mutation probes | **14/14** — the 10 from v6.7–v6.9 + 4 new: `hyp9` (the v6.9 scale), `ankerfast` (anchor not from the config), `ingenklem` (186 bar shown), `raabar` (RAW shows bar) |
| Device build vs. v6.9 | 0 warnings (device, SIM ARM, SIM host; fresh zig cache), reproducible, static. 12 changed + 6 new fragments, all intended. **37 fragments token-identical** (frame policy, write queue, activity, phase, SAVE, boot push, network guard, RX, CRC, flag + v6.9's conditioning, chart and weight source) |
| Previews vs. v6.9 | **100 of 134 byte-identical**. 34 intended: 16 HOME images **only** in the chart/stat field (x 96..384, y 240..340), the rest version string / DIAGNOSTICS. **3 new scenarios × 2 languages**: `67_diag_pressure_ext`, `68_diag_pressure_maint_pulse`, `69_home_brew_pinfo_toast` |
| Circle check | 70/70 + 70/70, **0 violations** |
| Spec alignment · i18n · boot/config checks | **ALL ALIGNED** · appendix in sync (361 string ids) · green |
| `sim/selftest.py` · `run_e2e.py --all --quick --parallel 4` (EN) | **120/0** · **5/5 scenarios, 97/97 checks, 44 s** (`quick_en/report.md`). In the brew scenario's state lines: PRE `4.0`, EXT `14.9` (raw 1318), a deadhead sample after the stop (raw 4901) has `pulse: true`, `bar: null` |
| Before/after images | `labels_before_after.png` (chart + stats, v6.9 \| v6.10, EN and DA: pre, ext, DONE, RAW) · `home_ext_before_after.png` · `new_v610_screens.png` |
| The device | **Not contacted** in this round (no telnet was needed). No reboots, flag, slot or config writes |

8. **Files:**

| File | md5 |
|---|---|
| `merakiplus.c` (v6.10) | `23672df8557a1fbb4d54cdb6b220c244` |
| `merakiplus_v6` (device binary, static ARMv7) | **`29bc801c1c777d9cb3484bfa960c66bc`** |
| `merakiplus_v6_sim_arm` · `merakiplus_v6_sim` (host) | `14f58597ae605f642e37026bd97b66b3` · `0ce8d412f20d3a07b507b89566fb2605` |
| `merakiplus_v6_9.c` / `merakiplus_v6_9.bin` (v6.9, rollback) | `440be5124c0d986ec23e4c3b08f84149` / `2dce4eba0b4ce68dcd0369e955cf3b77` |
| `merakiplus_v6_9_sim` / `merakiplus_v6_9_sim_arm` (v6.9 SIM) | `fbd8c89c236f508ec725505c519b7b91` / `cbebc8b128f413c1d40a83de2bd6343a` |

   Tools: `build_sim.sh` now compares with v6.9 (the v6.9 version kept as `round_v6/build_sim_v69.sh`); `round_v6/build_previews.sh` writes `v69_compare.txt` and has 4 new mutants. `boot/`, `sim/mcu_sim.py`, `sim/scenarios/` and `sim/run_e2e.py` were not touched. Report: `sim/reports/v6.10/SUMMARY.md`.


---

## CHANGES-v6.11

**UX review and optimization.** Only the display and touch routes have changed. The findings, prioritization and what has been deferred are in [§17](#17-ux-review-v611).

1. **Plain language (O4).** Word numbers have been removed from all notes (14 notes). The sleep, offline and error texts on HOME are plain language, and the E code is now in the chart hint. 8 toasts have been rewritten. DIAGNOSTICS and §4 still have the word numbers. The toast texts that the E2E scenarios quote (*GEM FEJLET*, *FEJL AKTIV*, *UDEN FOR SPEC*, *HOLD KNAPPEN NEDE FOR START*, *GEMT · …*) have been kept.
2. **HOME (O3, O8, E1, E2, E4):**
   * a hint in the empty chart field (`TX_C_HINT`, `TX_C_HINT2`, `TX_C_ERRCODE`, `TX_C_DIAGREF`); *NO ACTIVITY* (`TX_C_NOACT`) has been removed
   * the label *BREW BOILER · NOW* and the sub-line *TARGET 87.0°* in F_S
   * the stat labels *STEAM* / *BOILER* / *STEP* / *RAW*, with ≥ 16 px of air between labels
   * the chart is called *PRESSURE*, and the BAR field *≈BAR EST.*
3. **Navigation (O1, O5, O6):**
   * a gear on HOME (→ BREW)
   * edge taps also on the lists
   * a swipe guard with axis lock, ≤ ~30°
   * a close chevron and swipe-down in the detail and system views
4. **Contrast (O9).** Text 2 is now 176,183,196 (≥ 7:1) and text 3 is now 140,147,162 (≥ 4.5:1 also on list rows). The new color "disabled" (98,105,120) is used for inactive elements. The progress ring has kept its color.
5. **DIAGNOSTICS (O7).** The page is divided into the sections STATUS, BOOT, NETWORK, GRAPH, PRESSURE, *RAW COUNTERS · SERVICE* and ABOUT. The page is 38 px longer. The GRAPH pill's touch area is now 44 px high.
6. **The self-test section "v6.11 UX review" (19 checks):**
   * touch targets measured via `hit_test()`
   * WCAG contrast
   * swipe guard: slanted gesture, horizontal gesture and vertical start (list and HOME)
   * close chevron and swipe-down in the detail and BOOT UI, including that a changed value is not saved, and that a gesture started on − does not close
   * gear and edge taps without frames
   * hint and sub-line at idle, on error, offline and asleep
   * no word numbers, ACK or frame policy in user-facing strings
   * the order of the DIAGNOSTICS sections and a one-to-one placement of the cells

   Two older checks have been updated to v6.11's intent, without being weakened:
   * the REVERT zone now only allows the gear (page change, no frame), and the mutant `pille` is still caught
   * the chart text check now requires *PRESSURE* / *≈BAR EST.*
7. **Verification (local; DEV-ITERATION-POLICY [§15](#15-development-iteration-dev-iteration-policy)):**

| What | Result |
|---|---|
| Self-test (the device's code, preview build) | **387 checks, 0 errors** in EN and DA (v6.10: 368). Of which the v6.11 section **19/19** |
| Self-test as a real 32-bit ARM binary (local ARMv7 container) | EN + DA **ALL OK**. With `long` as the time type 10 wraparound checks fail (as before) |
| Mutation probes | **20/20 caught**. Six are new: `swipe610` (v6.10's swipe rule), `noback`, `lavkontr` (v6.10's text 3), `ordnote`, `ingenhint`, `stat12` (*STEAM BOILER* again) |
| Device build vs. v6.10 | 0 warnings in all three builds (fresh zig cache). Reproducible. 34 changed and 8 new fragments, all display or touch. **47 control/protocol fragments token-identical** |
| Previews vs. v6.10 | 140 compared. All differ intentionally (colors and texts), but **the gauge ring and the tick ring are byte-identical in all of them**. Excepted are pages without a ring (lists, DIAGNOSTICS) and the toast pill. **3 new scenarios × 2 languages**: `70_home_gear_pressed`, `71_detail_back_pressed`, `72_diag_raw_counters` |
| Circle check | 73/73 + 73/73, 0 violations |
| Spec alignment · i18n · boot/config checks | ALL ALIGNED · appendix in sync (365 string ids) · green |
| E2E against the simulated MCU (quick) | `sim/selftest.py` **120/120**. `run_e2e.py --all --quick --parallel 4` (EN): **5/5 scenarios, 97/97 checks in 44 s**. Targeted, because the touch routes have changed (DA): `ui_touch_navigate_save_start` **26/26** and `ui_rows_real_frames` **27/27**. The full suite has **not** been run; it waits for CI/CD or a release |
| The device | **Not contacted.** No reboots and no flag, slot or config writes |

8. **Files:**

| File | md5 |
|---|---|
| `merakiplus.c` (v6.11) | `e9eea8caebfa5d832884d96996dbd2c7` |
| `merakiplus_v6` (device binary) | **`725386ccadade594275f20cc998885eb`** |
| `merakiplus_v6_sim_arm` · `merakiplus_v6_sim` | `13b5ad2e374d5a79e097c008a581530e` · `3e12be0568223925c7c0ccd28f13c9d1` (host; changes on every link) |
| `merakiplus_v6_10.c` / `.bin` (v6.10 kept) | `23672df8557a1fbb4d54cdb6b220c244` / `29bc801c1c777d9cb3484bfa960c66bc` |
| `merakiplus_v6_10_sim_arm` · `merakiplus_v6_10_sim` | `14f58597ae605f642e37026bd97b66b3` · `0ce8d412f20d3a07b507b89566fb2605` |

   Report: `sim/reports/v6.11/SUMMARY.md` with before/after sheets (`EN` · `DA`; v6.10 on the left, v6.11 on the right).

   Tools:
   * `build_sim.sh`: reference v6.10 (the v6.10 version is kept as `round_v6/build_sim_v610.sh`)
   * `round_v6/build_previews.sh`: `v610_compare.txt` via the new `round_v6/v610_compare.py`, before/after sheets and 6 new mutants (the v6.10 version is kept as `round_v6/build_previews_v610.sh`)

   `sim/` was not touched, apart from the reports under `sim/reports/v6.11/`.


## CHANGES-v6.13

**Manual STOP for WATER, BREW and GRIND.** User bug: hot water could not be stopped from the Meraki+ screen. Only steam had a STOP button. The stop words and the evidence are in [§18](#18-manual-stop-v613). Auto-stop, the start recipes, the frame policy and the phase logic are unchanged.

1. **Stop frames, reverse-engineered from the MCU firmware** and cross-checked against the stock UI's own stop calls:
   * WATER: `aa 55 06 10 01 00 00 c0 0f` (word 16 = 0), plus `aa 55 06 13 01 00 00 c0 4b` (word 19 = 0) if word 19 ≠ 0
   * BREW: `aa 55 06 16 01 00 00 c0 87` (word 22 = 0)
   * GRIND: `aa 55 06 06 01 00 00 c4 47` (word 6 = 0)

   In all three cases it is the same word the MCU's own auto-stop resets. Words 9 and 3 are tare triggers and not stop words. REGISTERMAP §2.2 has swapped words 3 and 6 (§18.1).
2. **The STOP button on HOME** is in the same place and has the same style as STOP STEAM: *STOP WATER / STOP BREW / STOP GRIND* and *STOP VAND / STOP BRYG / STOP KVÆRN*, touch area 180 × 56 px. If a main activity runs at the same time as latched steam, there are two buttons of 148 × 56 px each. The steam pill is 160 px wide (before 150): "STOP WATER" / "STOP KVÆRN" need 14 px of air on each side.
3. **Feedback:**
   * *STOPPING WATER…* immediately on the tap
   * *WATER STOPPED* (green) when the work_sta bits have been cleared
   * *STOP: NO REPLY · TRY AGAIN* (red) if no ACK arrives after the retransmit
   * *STOP NOT CONFIRMED · TRY AGAIN* (yellow) if the bits are still set after 5 s

   An ACK alone does not count as confirmation. A double tap does not queue another frame. The sub-line is *AUTO-STOP AT 40.0 G · OR STOP*.
4. **New strings (EN/DA, 13):**
   * `TX_B_STOP_GRIND/BREW/WATER`
   * `TX_T_STOP_GRIND/BREW/WATER`
   * `TX_T_STOPPED_GRIND/BREW/WATER/STEAM`
   * `TX_T_STOP_NOACK/UNCONF/FAIL`

   `TX_H_AUTOSTOP_G` has been given *· OR STOP* / *· ELLER STOP*. Appendix A has been regenerated.
5. **SIM surface (additive):**
   * state JSON `stop: {btns, pending, last, req, ok, fail}`
   * the ctrl actions `grind_stop` / `brew_stop` / `water_stop`
   * layout targets with the same names

   `steam_stop` works as before.
6. **Verification (local dev iteration, §15):**

| What | Result |
|---|---|
| Self-test | **413 / 0** EN and **413 / 0** DA. New section "v6.13 manual STOP" **26/26**: frames byte for byte; policy; visible per phase; touch area ≥ 48 px; the whole sequence tap → STOPPING → frame → retransmit → ACK → bits cleared → STOPPED for water, brew and grind; no ACK; not confirmed; double tap; two buttons; auto-stop unchanged; READONLY |
| Self-test as a 32-bit ARM binary | **412 / 0** EN + DA (ARMv7 container) |
| Mutation probes | **25/25**. 5 new: `nostop`, `stop9`, `stop19`, `noconfirm`, `ackok` |
| Builds | device, SIM ARM and SIM host: **0 warnings**. The device build vs. v6.12: 13 new and 12 changed fragments, all on the list of intended ones. 47 control/protocol fragments are **token-identical** |
| Previews | 78 scenarios × 2 languages; **circle 0 violations** (156 images). Vs. v6.12: 146 compared, **128 byte-identical**, the rest only in the action row, the sub-line or the toast. 5 new scenarios (73–77) |
| Spec alignment | **ALL ALIGNED** |
| `sim/selftest.py` | **120 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | **5/5, 97/97**, 44 s |
| Targeted SIM (DA, `sim_scenarios_v613/`) | `ui_stop_water` / `ui_stop_grind` / `ui_stop_brew` **3/3, 51/51**: real FIFO touch → start → STOP → `write_words` sub16 / sub6 / sub22 = [0] + ACK + toast *STOPPING …*. `mcu_sim.py` does not yet stop on these words; see `SIM_HANDOFF` *(done since — sim/CONTRACT.md ADDENDUM v6.13)* |
| On-device | **not run: the machine was offline** (ping and ARP without a reply, telnet closed). `verify_stop_v613.py` is ready (§11 S), and its analyzer has been tested against a synthetic log (3/3) |

7. **Files and md5:**

| File | md5 |
|---|---|
| `merakiplus.c` (v6.13) | `eec5085ddc1861c212d396c70771e9dc` |
| `merakiplus_v6` (static ARMv7; reproducible) | `163692b5b3e11d8294471c5a0eae578b` |
| `merakiplus_v6_sim_arm` | `ed287001bc34d132a0da78026d1c806e` |
| `merakiplus_v6_sim` (host; changes on every link) | `84f194c731b7230201c102538d236195` |

   v6.12 is kept as `merakiplus_v6_12.c` (`e9eea8ca…`) / `merakiplus_v6_12.bin` (`725386cc…`, the deployed one) and `merakiplus_v6_12_sim` / `merakiplus_v6_12_sim_arm`. The old build scripts are kept as `round_v6/build_previews_v611.sh` and `round_v6/build_sim_v611.sh`.
8. **Not touched:** `boot/`, `sim/mcu_sim.py`, `sim/scenarios/`, `sim/run_e2e.py`, `config.json`, the flag and the slots. There were no reboots, and the device was not contacted beyond two connection attempts (ping and a TCP connect to :23), both of which failed.

## CHANGES-v6.14

**The physical power button.** User bug: the button did nothing while Meraki+ was running. merakiplus only read the touch node (`event0`); the button is the k62091 node (`event1`). The stock UI's semantics have been reverse-engineered from demo_v9 and the MCU firmware and mirrored; table and addresses in [§19](#19-the-power-button-v614). The start recipes, the STOP path, the write queue, the phase logic and the boot push are token-identical to v6.13. *(On the tested hardware the button turned out to be a hardware power switch — see [power.md](../power.md).)*

1. **The button node:** k62091 (`/dev/input/event1`) is opened read-only **without EVIOCGRAB** (the stock UI does not grab it either). Automatic selection by name → EV_KEY without EV_ABS → `event1`; `MERAKIPLUS_KBD=<path>` overrides. The touch node is grabbed as before.
2. **Power off (like the stock UI):** HOLD 1.5 s (stock: two second boundaries = 1–2 s) → the cmd8 time/alarm frame (alarm 0/0/0) → ≥ 80 ms → `aa 55 06 00 02 00 00 00 00 56 94` (word 0 = 0, word 1 = 0) → ACK → OFF. The backlight is turned off 1.5 s after the press (duty 99 + `pwm2/enable 0`). Keep-awake stands still while switched off. Blocked during brew/steam/grind with the same predicate as the stock UI (`isExtractionOrSteamSwitchOn`) — the stock UI never stops an activity; neither does merakiplus. Without a connection, with word 0 = 0 or in READ-ONLY nothing is sent. No ACK → back to ON with *POWER OFF: NO REPLY · TRY AGAIN*.
3. **Power on:** the button in OFF → backlight + TURNING ON…; the MCU wakes from the same hold (reset). When the controller replies with word 0 = 1 → PowerOn `aa 55 06 00 01 01 00 c5 5f` + awake `aa 55 06 01 01 00 00 c5 33` (as at startup) → HOME. 10 s without a reply → OFF again. If the controller replies by itself with word 0 = 1, merakiplus also powers on. After the MCU reset the existing re-push (v6.6) puts the config back. Touch in OFF shows the page for 8 s and is swallowed.
4. **Frame policy:** `word_ok(0)` now allows 0 and 1, but word 0 = 0 requires the new intent `I_POWEROFF` (otherwise `WJ_TRIG`). `I_POWEROFF` allows nothing else (start words, words 26/27/30 and word 1 = 1 are still rejected; `_Static_assert`). `rx_feed` counts the 0x88 acknowledgement for cmd8 (no other change).
5. **Verification (local dev iteration, §15):**

| What | Result |
|---|---|
| Self-test | **441 / 0** EN and **441 / 0** DA. New section "v6.14 power" **28/28**: cmd8 + PowerOff + PowerOn byte for byte; policy (I_POWEROFF); short press = nothing; hold → cmd8 at 1.5 s, cmd6 ≥ 80 ms later, ACK → OFF; exactly two frames (no stop frames); backlight off after 1.5 s; keep-awake 0 frames in 2 × 65 s (control: 2 in ON); touch swallowed + 8 s display; power on → PowerOn + awake only at word 0 = 1; the 10 s timeout; auto power-on; blocked during brew/steam/grind/bit 12 and a new hold afterwards; hot water not blocked and not stopped; no connection / word 0 = 0 / READ-ONLY; no ACK; an open detail; layout; DIAGNOSTICS; the black page. The old check "word0=0 rejected" now expects `WJ_TRIG` instead of `WJ_RANGE` |
| Self-test as a 32-bit ARM binary | **440 / 0** EN + DA (ARMv7 container) |
| Mutation probes | **34/34**. 9 new: `nokey`, `off1`, `nogap`, `nocmd8`, `noguard`, `kafight`, `noackwait`, `stopfirst`, `freeoff` |
| Builds | device, SIM ARM and SIM host: **0 warnings**; `-Wall -Wextra` the same two categories as v6.6. The device binary is reproducible. Vs. v6.13: 25 new and 23 changed fragments, all on the list of intended ones; **54** control/protocol fragments token-identical. EVIOCGRAB calls: 3 as before (touch only), 0 on the button node; no new `system()`/`popen()`, no `/sys/power`, `killall` or `reboot` |
| Previews | 83 scenarios × 2 languages; **circle 0 violations** (166 images). Vs. v6.13: 156 compared, **134 byte-identical**, the rest only in the DIAGNOSTICS list and the scroll indicator. 5 new scenarios (78–82: TURNING OFF, OFF, TURNING ON, blocked during a brew, DIAGNOSTICS) |
| Spec alignment | **ALL ALIGNED** |
| `sim/selftest.py` | **150 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | the v6.13 SIM binary (unchanged): **5/5, 97/97** · additionally against `merakiplus_v6_14_sim`: **5/5, 97/97** |
| `verify_power_v614.py --selftest` | **5/5** (a good log green; without cmd8 / without sleep / keep-awake in OFF / without re-push are caught) |
| On-device | **not run** (v6.13 was being installed in slot B at the same time; no contact with the device). `verify_power_v614.py` is ready (§11 T) |

6. **Files and md5 (SEPARATE — `merakiplus_v6` / `merakiplus_v6_sim*` are v6.13 and have NOT been touched):**

| File | md5 |
|---|---|
| `merakiplus.c` (the v6.14 source) | `94e7f577afb589d4e02c54b0942f78ab` |
| `merakiplus_v6_14.bin` (static ARMv7; reproducible) | `1dd63f42e4b0d649ef851660fc123a10` |
| `merakiplus_v6_14_sim_arm` | `d0559b45751a1fcddf75af57ac2df037` |
| `merakiplus_v6_14_sim` (host; changes on every link) | `deac1b7aff018619bb86afb582e9cd7d` |
| `merakiplus_v6_13.c` (the v6.13 source, a copy before the changes) | `eec5085ddc1861c212d396c70771e9dc` |

   Build scripts: `build_v614.sh` (builds ONLY the three `_14` files; log `round_v6/sim_build_check_v614.txt`) and `round_v6/build_previews_v614.sh` (+ `round_v6/v613_compare.py`). `build_sim.sh` and `round_v6/build_previews.sh` are the v6.13 versions and are unchanged — they write `merakiplus_v6` and must not be run before v6.14 has been promoted.
7. **Promotion (the parent, once the v6.13 installation is done):** `cp merakiplus_v6 merakiplus_v6_13.bin; cp merakiplus_v6_sim merakiplus_v6_13_sim; cp merakiplus_v6_sim_arm merakiplus_v6_13_sim_arm` (keep v6.13) → `cp merakiplus_v6_14.bin merakiplus_v6; cp merakiplus_v6_14_sim merakiplus_v6_sim; cp merakiplus_v6_14_sim_arm merakiplus_v6_sim_arm` → `md5 -q merakiplus_v6` = `1dd63f42…`. Then on-device: `python3 verify_power_v614.py` (run.sh mode from `/tmp`, no install needed for the proof).
8. **REVERT:** v6.14 only lives in `merakiplus.c` and the three `_14` files. Back to v6.13: `cp merakiplus_v6_13.c merakiplus.c` (md5 `eec5085d…`); the binaries `merakiplus_v6` (`163692b5…`) and `merakiplus_v6_sim*` are unchanged v6.13. If v6.14 has already been promoted/installed: `cp merakiplus_v6_13.bin merakiplus_v6` (from step 7) and reinstall via the normal boot install path. UI_v6.md before v6.14 does not exist as a copy — the sections §19, test plan T and this section are pure additions, and Appendix A is generated from the string table. No config, flag or slot changes to undo (none have been made). On the device an interrupted `verify_power_v614.py` leaves at most `/tmp/merakiplus_p/` (tmpfs) — run.sh SIGCONTs the stock UI, and merakiplus turns the backlight on at exit; a backlight left off after a crash: `echo 1 > /sys/class/pwm/pwmchip0/pwm2/enable`.
9. **Not touched:** `boot/`, `sim/mcu_sim.py`, `sim/scenarios/`, `sim/run_e2e.py`, `merakiplus_v6`, `merakiplus_v6_sim`, `merakiplus_v6_sim_arm`, `config.json`, the flag and the slots. No contact with the device. The previews in `round_v6/` are generated from the v6.14 source.

## CHANGES-v6.15

**The graph resets when an activity starts.** User bug: starting grind/brew/water tares the weight to 0, but the graph still held the pre-start history (e.g. a 200–300 g cup placed on the scale), so the y-autoscale was dominated by the cup step and the 0 → 36 g curve looked flat. Details in [§14.7](#147-the-graph-starts-over-when-an-activity-starts-v615). **Display only:** the write queue, frame policy, start/stop recipes, auto-stop, thresholds, guards and the phase logic are unchanged; `reg[]` and the state JSON's `regs` are raw.

1. **Reset:** `graph_push()` clears the ring (`graph_clear()`: `g_n = g_head = 0`, chart filters restart) on the idle → active transition of `phase_main()` (grind / brew / water / steam on its own). The scale is recomputed from the fresh ring on the next draw.
2. **No reset** when a secondary activity starts during the main one (wand flush / steam during a brew or grind), or when the main phase changes between active phases (PRE → EXT, STEAM → BREW). OFFLINE samples are not idle (a link gap keeps the curve).
3. **Re-anchor:** within 1.5 s (`G_REANCHOR_MS`) of the start the ring is cleared once more on a late weight-source latch (physical grind start, word 39 → 38 after 300 ms) or a late tare (weight drop > 2 g in one sample).
4. **State JSON (SIM):** `chart` += `buf`, `w_max`, `w_top`, `resets`, `reanchors`, `act_ms`. Preview/selftest only: `gr_off` (1 = v6.14 behaviour) for the before/after proof; the device build compiles it to a constant.
5. **Verification (local dev iteration, §15):**

| What | Result |
|---|---|
| Self-test | **454 / 0** EN and DA (repo fixture config). New section "v6.15 graph reset" **13/13**: cup at rest in the ring; ONE reset at the brew start, only post-start samples, none with the cup; y-autoscale recomputed (weight top 108, not 2700); wand flush during the brew = no reset, curve kept; 0 → 35 g at ≥ 85 % of the weight axis; display only (word 39 raw, no frames); RAW view; v6.14 behaviour (`gr_off`) = weight top 2700 and the 35 g point at 12 % (the bug); DONE → water = a second reset; late tare → one re-anchor; OFFLINE gap = no reset; physical grind start → one re-anchor, grind scale = target; idle → steam = reset, brew during steam = no reset. Two older checks adapted: "grind + wand" (the 300 ms of brew-scale samples are now re-anchored away) and "3 s into an extraction" (the mostly-not-brew window is now reached via a brew started during steam) |
| Self-test as a 32-bit ARM binary | **454 / 0** EN + DA (ARMv7 container, non-root) |
| Mutation probes | **5/5**: `noreset`, `anyswitch`, `offidle`, `noreanchor`, `everysample` |
| Builds | device, SIM ARM, SIM host, preview: **0 warnings** (fresh zig cache); `-Wall -Wextra` the same set as v6.14 (25 × missing field initializer + unused `fill_rect`). Device binary reproducible (same md5 from another directory) |
| Previews | 84 scenarios × 2 languages; **circle 0 violations**. Vs. v6.14: **134 byte-identical**; the 32 changed HOME activity frames differ only in the chart (+ sub-pixel gauge-ring AA in `04_home_brew_pre`, max channel delta 23). New: `83_home_brew_after_cup` (250 g cup, then a brew: 0 → 35 g fills the weight axis) |
| Spec alignment | **ALL ALIGNED** |
| `sim/selftest.py` | **187 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | **5/5, 97/97** |
| New scenario `graph_reset` | **21/21** EN, DA and reference client (REF profile replaces the 6 `chart` checks 1:1) |
| On-device | **not run** (install deferred) |

6. **Files:** `src/merakiplus.c`, `sim/scenarios/graph_reset.json`, `sim/run_e2e.py` (expect kind `chart`, additive), `sim/sim_ui_profile-ref.json` (6 overrides), `bin/` (`merakiplus`, `merakiplus_sim_arm`, host SIM builds; md5 in `bin/MD5SUMS`).

## CHANGES-v6.16

**Time & scheduling.** Three user reports: (1) the 24-hour flag was inverted relative to the stock UI, (2) merakiplus showed no clock, (3) there was no planned pre-heat / scheduled wake. The stock semantics (addresses, frame layout, MCU behaviour) are in [time-scheduling.md](../time-scheduling.md). Power semantics of v6.14 are unchanged (the button's kernel cut, cmd8 → 80 ms → cmd6 sub0 len2 `[0,0]`); the stop, gesture and graph-reset paths are untouched.

1. **TIME FORMAT fixed:** stock `update_time` 0xD5944 XORs `model[0x59]` (= `timemode != 0`) with 1 before the test (0xD5970) — `timemode = 1` is **24 h**, `0` is 12 h with A.M./P.M.; the stock switch "24-Hour Format" is ON for 1. v6.1 missed the `eor`; `OPT_TIME` is now `{12-HOUR, 24-HOUR}`, so SAVE writes what stock expects (config path unchanged).
2. **Clock:** HOME's top line (`W_CLOCK`, replaces the wordmark while the clock is valid) and DIAGNOSE → TIME · SCHEDULE. Local time = SoC UTC + stock zone table 0x1A5890 (136 entries, copied) + 1 h when `sumtime` and the region's DST window (EU / US / Levant / AU / NZ rules; stock's window is date-only and only exists after its HTTP sync). Format byte-identical to `update_time`. A clock before 2025 = not set: wordmark, NOT SET, no alarm. merakiplus never sets the clock. TIME ZONE (locked) shows the stock label and the place names.
3. **Planned pre-heat:** SYSTEM rows PRE-HEAT, PRE-HEAT AT (5-min steps), REPEAT (once / every day / Mon–Fri / Sat–Sun) in merakiplus's own file `/new_customer/merakiplus/schedule`. **Power-off** sends the next occurrence as the cmd8 alarm exactly like stock `performSleepActions` 0x148324 → `powerOffSend(day, hour, minute)`; no schedule → 0/0/0 (v6.14). **Pre-arm** (documented addition): while ON the same frame is sent when the alarm differs from what the current MCU session holds (start, MCU restart / wake, schedule change, daily roll-over); disarm = 0/0/0 once; never while turning off / off; no schedule = no new traffic. A one-shot switches itself off a minute after its time. HOME's swipe hint shows `⏰ PRE-HEAT · THU 06:30` while planned.
4. **Auto off (optional, default off — v6.23: default 30 min, see CHANGES-v6.23):** 15 / 20 / 30 / 60 min without touch, button or an active phase → the same power-off as the button (with the alarm); a 30-s warning toast (a touch dismisses it). Stock: a fixed 30 min (`SleepRefresh` 0x149268); `sleepMode`/`sleepModeSetting` are never read by stock and are not written by merakiplus.
5. **cmd8 time = stock local time** (v6.14 sent the SoC's UTC).
6. **SIM:** state JSON `clock` + `sched`; env `MERAKIPLUS_SIM_CLOCK`, `MERAKIPLUS_SCHEDFILE`; ctrl `clock`; `save` accepts `preheat`, `preheat_at`, `preheat_repeat`, `auto_off`. `run_e2e.py` (additive): scenario keys `ui_env` / `ui_only`, expect kind `state`, `time_sync_seen` with `alarm` / `max_count` / `count: 0`. `mcu_sim.py`: a CPU reset clears the cmd8 alarm (MCU RAM).
7. **Verification (local dev iteration, §15):**

| What | Result |
|---|---|
| Self-test | **487 / 0** EN and DA (repo fixture config), **488 / 0** with the device's config. New section "v6.16 time & scheduling" **33/33**: timemode semantics + 8 format vectors, zone table, 11 DST vectors, 11 next-occurrence vectors (month/year/leap wrap, strictly-after, weekday masks), alarm frame `aa 55 08 ea 07 0b 12 10 23 1c 13 06 1e be b2` (CRC computed independently), no alarm with an unset clock, schedule file round trip without config writes / cmd6, bad values → defaults, pre-arm once / no repeat / re-arm after an MCU restart / none in read-only / disarm 0/0/0, power-off carries the alarm, no pre-arm while off, re-arm after the wake, one-shot use, auto off (quiet, warning, touch reset, activity, power-off sequence), HOME clock 12/24 h, pre-heat hint |
| Self-test as a 32-bit ARM binary | **487 / 0** EN + DA (ARMv7 container, non-root) |
| Mutation probes | **8/8**: `inverted`, `ampm`, `alarm0`, `norearm`, `norestart`, `prearmoff`, `nodst`, `onceforever` |
| Builds | device, SIM ARM, SIM host, preview: **0 warnings** (fresh zig cache); `-Wall -Wextra`: + 4 × missing field initializer (the 4 new table rows, same pattern as before), nothing else new |
| Previews | 90 scenarios × 2 languages; **circle 0 violations**. Vs. v6.15: 66 byte-identical; the changed ones differ only in HOME's top line (wordmark → clock), the scroll indicator, or are SYSTEM/DIAGNOSE frames shifted by the new rows/block and the TIME ZONE label (listed in `v615_compare.txt`). New: `84_home_clock12_preheat`, `85_list_system_time`, `86_detail_preheat_at`, `87_detail_timezone`, `88_diag_time`, `89_home_autooff_warn` |
| Spec alignment | **ALL ALIGNED** |
| `sim/selftest.py` | **187 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | **5/5, 97/97** |
| New scenarios | `time_display` **30/30**, `timemode_stock_parity` **23/23**, `schedule_prewarm` **37/37** (pre-arm → power-off alarm → deep sleep → **RTC alarm wake** → re-push → ON → re-armed for tomorrow → disarm), EN and DA; reference client: SKIP (`ui_only`) |
| Regression (targeted) | `power_off_wake` 38/38, `power_during_activity` 29/29, `graph_reset` 21/21, `settings_save_matrix` 188/188, `language` 16/16 |
| On-device | **not run** (install deferred) |

8. **Files:** `src/merakiplus.c`, `docs/time-scheduling.md` (new), `sim/scenarios/{time_display,timemode_stock_parity,schedule_prewarm}.json`, `sim/run_e2e.py`, `sim/mcu_sim.py`, `sim/CONTRACT.md`, `bin/` (`merakiplus`, `merakiplus_sim_arm`, host SIM builds; md5 in `bin/MD5SUMS`), `tools/merakiplus.py` (VERSION 6.16 + md5).

## CHANGES-v6.17

**UX audit + IA consolidation.** The owner asked five questions about v6.16 — is there unnecessary text, does the experience make sense, is hold-to-start coherent while every setting is hidden, why so many settings pages, and would one page per feature with its settings be better. The audit, its evidence and the rejected alternatives are in [ux-audit-v617.md](../ux-audit-v617.md); this section records what changed in the code and how it was verified. **Display and navigation only:** `act_push`, `act_guard`, `frame_policy`, `save_begin`/`save_step`, the STOP path, the power button, the schedule and every limit are untouched.

1. **Pages** (`enum PG_*`): `HOME, GRIND, BREW, WATER, STEAM, SYS, DIAG` (v6.16: `HOME, T0, T1, T2, DIAG` in a ring). HOME is the root; `nav_open()` slides a page in, `nav_back()` returns to `page_parent()` (DIAGNOSTICS → where it was opened from, everything else → HOME). No carousel: `T_EDGE_L/R`, the page dots and the page swipes are removed. `SET[].tier` is now the page index (0 GRIND · 1 BREW · 2 WATER · 3 STEAM · 4 SYSTEM).
2. **The row model** (`FEAT_ROWS`, `SEC_ROWS`, `rows_get()`): one list of rows per page with one pitch; a row is a setting (`RK_SET`), a SYSTEM section header (`RK_SEC`), a system action (`RK_X`: BOOT UI / SWITCH TO ORIGINAL NOW) or the DIAGNOSTICS entry (`RK_DIAG`). `hit_test`, `draw_list`, the SIM `layout` hook and the self-test all walk `rows_get()`. Sections: DISPLAY (UI LANGUAGE, BRIGHTNESS, TEMP UNIT) · CLOCK (TIME FORMAT, DAYLIGHT SAVING, TIME ZONE) · SCHEDULE (PRE-HEAT, PRE-HEAT AT, REPEAT, AUTO OFF) · STOCK UI (BOOT UI, SWITCH TO ORIGINAL NOW, VOLUME, STOCK LANG., WATER SHORTCUT, CLEANING REMINDER, AUTO TIME, PAUSE TIME); one open at a time (`sec_open`), the opened header scrolls to the top, SYSTEM opens closed when entered from HOME. The read-only WATER SENSOR (`S_HOTT`) is in no list: it is the WATER page's live line.
3. **Feature pages:** header = close chevron (y 22) + icon and name (y 44) + live line (y 70; `page_live_text()`: the guard's text in amber when a start is blocked — `TX_G_WAIT` excepted, it is transient — otherwise the live values); list y 86..342 (4 rows in view); **HOLD TO START** (`T_START`, pill 232 × 48, touch 248 × 60) with the same `HOLD_MS` and the same `act_request()` as the icon, then `go_page(PG_HOME)`. In `ui_update()`: a feature page on screen while `phase_active(phase_main())` → HOME (level-triggered; SYSTEM, DIAGNOSTICS and editors are exempt).
4. **HOME:** a tap on an icon opens its page (`activate`: v6.16 showed a toast); each icon has its target under it (`HomeM.alab`, y 405); one hint line (`TX_C_HINT`; the planned pre-heat is the second line when there is one); the error/offline hint is a tap target (`T_DIAG`, only while it is on screen) that opens DIAGNOSTICS; the gear opens SYSTEM; no dots. Activity texts: label = what the number is (`DOSE`, `IN THE CUP`, `MILK`), sub-line = the target once, in F_S (`STOPS AT %s G`).
5. **STOP:** `stop_pill()` 216 × 54 (two: 152 × 54), `stop_hit()` 232 × 60 (two: 160 × 60), `C_STOP_T/B` = rgb(214,58,62) → rgb(182,42,50) with white text, F_M (two: F_S).
6. **Editors:** `detail_text()` no longer returns footer lines; `detail_status()` (new, shared with the self-test) = SAVING… / out of spec / CHANGED · WAS … / the zone's place names / `TX_D_HOLDFAST` when `detail_holdable()` (a number editor with ≥ 20 steps, untouched) / empty. Swipe right closes an editor like swipe down does. The system views (BOOT UI / SWITCH / exit reminder) keep their footers — they state consequences — but lose the "SAVED CHOICE" status.
7. **DIAGNOSTICS:** title only; the two caveat lines under PRESSURE · ESTIMATE are gone (`DG_RAW` −32 px; the text is in [calibration.md §16.3](../calibration.md)). Otherwise unchanged, including the GRAPH smooth/raw pill.
8. **Strings:** 449 → 435 ids (25 removed, 11 added); user-facing EN text 5 811 → 5 184 characters. The table of changes is in the audit, §6; Appendix A below is regenerated.
9. **SIM (additive, [CONTRACT.md](../../sim/CONTRACT.md) ADDENDUM v6.17):** `tab` values, `shown.live` / `shown.section`, `page` by name (+ `section`), `layout` targets (`gear`, `back`, `start`, `sec_*`, `diag`, `graph`; no `edge_*`). The §3 `page` keys are unchanged, so no existing scenario needed a change.
10. **Verification (local dev iteration, §15):**

| What | Result |
|---|---|
| Self-test | **510 / 0** EN and DA (repo fixture config), **511 / 0** with the device's config. New section "v6.17 IA consolidation" **23/23**: the row model (every parameter on exactly one page), reachability by real taps (every editor ≤ 3 taps from HOME, back by the chevron, no frame), SYSTEM sections, tap vs hold on the icons, HOLD TO START = the icon hold's frames byte for byte (4 activities), touch areas, the live line (values / guard text / no flicker / offline), an activity starting while a page is open, the back gestures, the error hint as the DIAGNOSTICS entry, STOP size + contrast, nothing drawn below an editor's buttons, the editor status line, banned words in user-facing strings, HOME texts, icon targets, header summaries. The older checks that asserted v6.16's texts or geometry were rewritten for the new ones (swipe guard, gear, STOP hit area, `≈BAR`, sub-lines, the UI LANGUAGE row, the SYSTEM rows); none was dropped |
| Self-test as a 32-bit ARM binary | **510 / 0** EN + DA (ARMv7 container, non-root) |
| Mutation probes | **11/11**: `taptoast`, `nobounce`, `startframe`, `orphan`, `footers`, `stopdim`, `sysopen`, `diagback`, `waitflicker`, `blindicon` (all caught in the v6.17 section) and `swipeleft` (caught by v6.11's swipe guard) |
| Builds | device, SIM ARM, SIM host, preview: **0 warnings** (fresh zig cache); `-Wall -Wextra`: 29 × missing field initializer (the settings table, as before) and nothing else — one fewer than v6.16 (`fill_rect` is used again) |
| Previews | 100 scenarios × 2 languages (v6.16: 90); **circle 0 violations**. No byte comparison against v6.16 — almost every screen changed on purpose; before/after pairs are in the audit |
| Spec alignment | **ALL ALIGNED** (41 spec parameters; the table's limits, steps and words are untouched) |
| `sim/selftest.py` | **187 / 0** |
| `run_e2e.py --all --quick --parallel 4` (EN) | **5/5, 97/97** |
| New scenarios | `ia_navigation` **45/45**, `feature_page_start` **29/29**, `activity_takes_home` **20/20**, EN and DA; reference client: SKIP (`ui_only`) |
| `run_e2e.py --all --parallel 4`, EN and DA (local, 5 min per language) | **39/39 scenarios, 1030/1030 checks** in each language — including `settings_save_matrix` 188/188, the STOP, power, schedule and replay scenarios, none of which needed a change |
| `boot/test_switcher.py`, `tests/test_installer.py`, `tests/hygiene.py` | green |
| On-device | **not run** (install deferred) |

11. **Not done / open** (also §7 of the audit): no on-device run; a page left open stays open (no idle return to HOME); the editor's lower third is empty now that the footers are gone; DIAGNOSTICS was moved, not redesigned.
12. **Files:** `src/merakiplus.c`, `docs/ux-audit-v617.md` + `docs/img/ux-audit-v617/` (new), `docs/ui-guide.md`, `README.md`, `screenshots/`, `sim/scenarios/{ia_navigation,feature_page_start,activity_takes_home}.json` (new), `sim/CONTRACT.md`, `sim/README.md`, `sim/PLAY.md`, `sim/play.py`, `sim/play_viewer.html`, `bin/` (`merakiplus`, `merakiplus_sim_arm`, host SIM builds; md5 in `bin/MD5SUMS`), `tools/merakiplus.py` (VERSION 6.17 + md5).

## CHANGES-v6.18

**Sound: a start jingle + one ready tone per boiler.** The owner: *the stock UI plays a start jingle and a sound when the machine is hot in both boilers — do the same, but with a distinct sound per boiler*. The stock audio path was reversed first ([audio.md](../audio.md): `deal_play` 0x13725C, the logo page's `power.mp4`, `preheatCompele.mp3` once per power-on when words 62 **and** 63 > 99, libhvplayer's MI_AO sequence, the MCU's heat stages). **Display and audio only:** no UART frame, guard, limit or timing changed; the self-test proves that no sound event touches the write queue or the TX buffer.

1. **Signal = the controller's heat stage** (words 62/63, MCU `boiler_heat` 0x3502: 0/30/60/100; 100 = extract boiler ≥ word 11 / steam boiler ≥ gear target − 10 °C). `docs/registermap.md` called them "heating power" — corrected. Fallback without the words: word 40 ≥ word 11 / word 47 ≥ steam target − 10 °C (`snd_ready`).
2. **Engine** (`snd_tick`, after `sched_step` in the main loop, also in the preview loop): `snd_machine()` = 0 hold (no link while PW_ON) / 1 off (PW_OFFING/OFF/WAKING, V_OFF, PH_SLEEP, word 0 ≠ 1) / 2 on. On → **jingle** if the machine was seen off/asleep or the display cold-booted (`/proc/uptime` < 120 s); otherwise a 1.5 s **settle** window decides which boilers are already hot (announced). Per boiler: re-arm when `snd_cooled()` (stage ≤ 30 or 15.0 °C below the ready point), fire once when ready — dropped (`suppressed`) while `phase_active`/steam bits/a start is pending. Off → both re-armed, queue flushed.
3. **Outcome per event** (`snd_emit`): SOUND off or the boiler not in READY TONE → `off`; config volume 0 → `muted`; no helper / a stuck helper → `noplayer` (device); queue full → `dropped`; else `played` (queued). Logged in a ring of 8 (state JSON, DIAGNOSTICS, `/tmp/merakiplus_snd.log` on the device).
4. **Device playback** (`snd_spawn`/`snd_service`, `DEVICE_RT` only): fork + exec `/new_customer/merakiplus/merakiplus_snd <file> <vol>` with `LD_LIBRARY_PATH=/lib:/customer/libs`, all fds closed, one at a time, reaped with `waitpid(WNOHANG)`; merakiplus sends **no signal** (still 0 `kill()` calls) — the helper ends itself after 8 s (`alarm`). Volume = config `volume` capped at 70 (stock `deal_play`).
5. **Helper** `src/merakiplus_snd.c` (new binary `bin/merakiplus_snd`, glibc 2.28 armhf, `-ldl` — not enough on a machine, fixed in [v6.18.1](#changes-v6181)): `dlopen` libmi_common (RTLD_GLOBAL — libmi_ao's `CamOs*` symbols), libmi_sys, libmi_ao; `MI_SYS_Init`; libhvplayer's exact `MI_AO` sequence and attributes (48 kHz, 16 bit, mono, FrmNum 6, PtNumPerFrm 2048; SendFrame retry on 0xA005200D; dB = min(vol·90/100 − 60, 15), 0 = mute). WAV 16 kHz is upsampled ×3. MP3 (STOCK SOUNDS) via libhvplayer's `mm_player_*` like stock. `--check` resolves the symbols only.
6. **Sounds** `audio/start.wav`, `ready_extract.wav`, `ready_steam.wav` — our own, synthesized by `tools/gen_audio.py` (stdlib), checked by `./build.sh` (`--check`) and `tests/hygiene.py` (`AUDIO_MD5`; no other audio/video file allowed).
7. **Settings / IA:** `S_SOUND` (OFF/ON/STOCK SOUNDS) and `S_SNDRDY` (OFF/BREW BOILER/STEAM BOILER/BOTH BOILERS), flag `SF_SND`, own file `/new_customer/merakiplus/sound` (`snd_cfg_load`/`snd_cfg_save`, atomic like the schedule; env `MERAKIPLUS_SOUNDFILE`). Section DISPLAY → **DISPLAY · SOUND** = UI LANGUAGE, BRIGHTNESS, TEMP UNIT, SOUND, READY TONE, **VOLUME** (moved from STOCK UI). SYSTEM closed is still 5 rows. SAVE of SOUND/READY TONE/VOLUME plays a test tone (`snd_preview`). New DIAGNOSTICS block **SOUND** (`DG_SND`, between TIME · SCHEDULE and ABOUT).
8. **Strings:** 435 → 461 ids: + 26 in one contiguous block (`TX_SL_SOUND` … `TX_DV_SO_DROP`); `TX_SEC_DISPLAY` → *DISPLAY · SOUND* / *SKÆRM · LYD*; `TX_SN_VOL` → *MERAKI+ + STOCK UI SOUNDS*. The v6.17 "the table is smaller than v6.16's" guard ignores the sound block. Appendix A regenerated.
9. **SIM (additive, [CONTRACT.md](../../sim/CONTRACT.md) ADDENDUM v6.18):** state `audio` + words 62/63 in `regs`; `mcu_sim.py` heat-stage model behind the new `boilers` event (off by default — nothing else changes); runner keys `sim.init_events`, `sandbox_files`; play viewer audio card + `/api/boilers`; four `ui_only` scenarios.
10. **Verification (local, §15):**

| What | Result |
|---|---|
| Self-test | **537 / 0** EN and DA (repo fixture config), **538 / 0** with the device's config. New section "v6.18 sound" **27/27**: settings + file (atomic, round trip, bad values), cold boot → jingle once, per-boiler stage thresholds and distinct files, no spam on dips, link glitch ≠ power cycle, re-arm by temperature and by stage, no frame / TX bytes, sleep and the OFF view silent + jingle again on power-on, start on a running machine (no jingle, settle window), fallback thresholds, mute / OFF / per-boiler mask / volume cap / stock files, activity suppression, SAVE test tone, event names, DIAGNOSTICS text |
| Self-test as a 32-bit ARM binary | **537 / 0** EN + DA (ARMv7 container, non-root) |
| Mutation probes | **9/9** caught by the v6.18 section: `rearm_any`, `linkoff`, `nosuppress`, `nomask`, `nocap`, `sametone`, `asleep`, `nosettle`, `frame` |
| Helper | host self-test against a mock MI_AO **14/14** (call order, attributes, chunking, retry, dB curve, the three files); ARMv7 container: runs, fails cleanly (rc 4) without the libraries, `--check` resolves all 9 symbols against the display's real `libmi_sys.so` / `libmi_ao.so` (with `libmi_common.so` stubbed — vendor files stay outside the repository) |
| Builds | device, SIM ARM, SIM host, helper: **0 warnings** (fresh zig cache); helper also `-Wall -Wextra -Werror`. `bin/` rebuilt with the **official** zig 0.17.1 tarball (macOS) = byte-identical to the same builds on linux/amd64 (the CI toolchain); the v6.17 files had been built with Homebrew's zig (`.comment` "Homebrew clang/LLD") — the cause of CI's failing drift check since it was added |
| Previews | 106 scenarios × 2 languages (+ `100`…`105`: DISPLAY · SOUND rows, the three editors, the SAVE toast, DIAGNOSTICS › SOUND); **circle 0 violations** |
| Spec alignment | **ALL ALIGNED** (the two sound rows are merakiplus's own, not spec parameters) |
| `sim/selftest.py` | **192 / 0** (T14 new) |
| `run_e2e.py --all --parallel 4`, EN and DA | **43/43 scenarios, 1126/1126 checks** in each language |
| `boot/test_switcher.py`, `tests/test_installer.py` (84 checks, incl. helper + sounds installed md5-gated and removed by revert), `tests/hygiene.py`, `tests/check_bins.py` | green |
| On-device | **not run** at release; the later hardware check found the helper silent ([CHANGES-v6.18.1](#changes-v6181)) |

11. **Not done / open:** no sound has been heard on a machine; amplifier-enable GPIO and MI multi-process behaviour are unverified (audio.md); no fault / water-tank sounds (stock has them; out of scope); the stock UI's "both boilers" sound is replaced, not added.
12. **Files:** `src/merakiplus.c`, `src/merakiplus_snd.c` (new), `audio/` (new), `tools/gen_audio.py` (new), `build.sh`, `bin/` (+ `merakiplus_snd`), `tools/merakiplus.py` (VERSION 6.18, `BIN_MD5`, `AUDIO_MD5`, install/revert), `tools/release_notes.py`, `.github/workflows/release.yml`, `tests/{hygiene,check_bins,test_installer}.py`, `docs/audio.md` (new), `docs/registermap.md`, `docs/ui-guide.md`, `README.md`, `CHANGELOG.md`, `sim/{mcu_sim,run_e2e,play,selftest}.py`, `sim/play_viewer.html`, `sim/{CONTRACT,README,PLAY}.md`, `sim/scenarios/audio_*.json` (new).

## CHANGES-v6.18.1

**Fix: the sound helper could not load the display's MI libraries — every sound was silent on a machine.** Found in the
first hardware check of v6.18 (2026-11-21): `merakiplus_snd --check` → `dlopen: /lib/libmi_common.so: undefined symbol:
sem_post`, exit 4; with `LD_PRELOAD=/lib/libpthread.so.0` it worked and the tone was heard ([audio.md §3](../audio.md#3-hardware-check-and-the-v6181-fix)).

1. **Root cause:** the display's `libmi_common.so` NEEDs only `libc.so.6` but uses 7 `sem_*` and 41 `pthread_*` symbols;
   glibc 2.28 defines the `sem_*` and 21 of the `pthread_*` only in `libpthread.so.0`; `demo_v9` links libpthread, the v6.18 helper (`-ldl` only) did not.
   The v6.18 container check used a stand-in `libmi_common.so` and so never exercised the real library's dependencies.
2. **Fix (`src/merakiplus_snd.c` only):** `pthread_global()` = `dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL)` before
   `libmi_common.so` in `mi_load` (WAV) and `mp3_play` (STOCK SOUNDS); plus `-lpthread` with one `sem_post` reference so
   `libpthread.so.0` is a real DT_NEEDED (plain `-lpthread` is dropped by the linker when nothing of it is referenced).
   `--check` prints `MI_SYS + MI_AO ok (sem_post global)`. MI_AO sequence/attributes, dB curve, `alarm(8)`, exit codes: unchanged.
3. **merakiplus:** only `MERAKIPLUS_VERSION` → `V6.18.1` (Appendix A regenerated: `TX_DV_VERSION`, `TX_DV_VERSION_RO`).
   No sound rule, setting, frame, guard or timing changed.
4. **Guard:** `tests/hygiene.py` fails without the `libpthread.so.0` DT_NEEDED or without the RTLD_GLOBAL load before
   `libmi_common.so` (mutation-checked against the v6.18 helper and against a source without the call).
5. **Verification (local, §15):**

| What | Result |
|---|---|
| Resolve test, ARMv7 container, byte-exact copies of the display's glibc 2.28 + `libmi_common/sys/ao.so` and its own `ld-2.28.so`, no LD_PRELOAD | v6.18 helper: `undefined symbol: sem_post`, exit 4 (= the machine); **v6.18.1: exit 0, `sem_post global`**; dlopen-only and link-only variants: exit 0 each |
| Helper self-test against a mock MI_AO | **14 / 14** |
| UI self-test (repo fixture config) | **537 / 0** EN and DA |
| `sim/selftest.py` | **192 / 0** |
| `run_e2e.py --all --quick --parallel 4` | **5 / 5** (quick level, EN) |
| Sound scenarios EN and DA | `audio_boiler_ready` 27/27, `audio_muted` 20/20, `audio_power_cycle` 25/25, `audio_sound_off` 24/24 |
| `tests/test_installer.py` (84), `boot/test_switcher.py`, `tests/hygiene.py`, `tests/check_bins.py` | green |
| On-device | v6.18.1 **not installed** yet — the check list is [audio.md §3.5](../audio.md#35-check-after-installing-v6181) |

6. **Files:** `src/merakiplus_snd.c`, `src/merakiplus.c` (version string), `build.sh` (`-lpthread`), `bin/` (`merakiplus_snd`,
   `merakiplus`, `merakiplus_sim_arm`, host SIM builds), `bin/MD5SUMS`, `tools/merakiplus.py` (VERSION 6.18.1, `BIN_MD5`),
   `tests/hygiene.py`, `docs/audio.md` §3, CHANGELOG, README badge, `bin/README.md`.

## CHANGES-v6.19

**User feedback on v6.18 sound:** (1) at a power-on with boilers that are already hot, the jingle and both ready tones
played back-to-back — redundant; (2) the tones sounded like plain beeps. Display and audio only: no frame, guard, limit,
timing, setting or string changed (only `TX_DV_VERSION*`); the helper `merakiplus_snd` is byte-identical to v6.18.1.

1. **Boot-already-hot rule (`snd_tick`):** at every transition to "on" — power-on with the jingle (cold boot of the
   display, OFF → on, sleep → wake) and, as before, a start on a running machine without it — the engine opens the
   1.5 s settle window (`SND_SETTLE_MS`) in which `armed[b] = !snd_ready(b)` on every tick: the **last** sample decides,
   so a boiler at stage 100 leaves the window with its latch already fired and no tone plays; a boiler below 100 leaves
   it armed and plays when it gets hot. v6.18 armed both boilers at a power-on. Stale samples (a 100 from before the
   power-off, the 0s the controller writes while asleep) are absorbed by the window (words 56+ are polled every 200 ms).
   Re-arm (stage ≤ 30 or 15.0 °C below the ready point), activity skip, mute, OFF/sleep silence: unchanged.
   [audio.md §2.6](../audio.md#26-power-on-with-hot-boilers-v619).
2. **Sounds (`tools/gen_audio.py`, `audio/`):** modal synthesis — marimba partials 1 : 3.9 : 9.2, glockenspiel
   1 : 2.76 : 5.40, each decaying at its own rate, mallet attack 1.5–4 ms, damped release — plus a 2-operator FM bell
   layer and a small Schroeder room; DC-blocked, −4 dBFS peak. Jingle G4-C5-E5-G5 → C6 (1.42 s), brew G4 → D5 marimba
   (0.80 s), steam E6 G#6 B6 glockenspiel (0.75 s); ~95 KB. One theme (a selector would need a setting row + 3× assets).
   `AUDIO_MD5`: `start.wav` `a03cb807d1cdb629b0614fdb740ec71d`, `ready_extract.wav` `4e7e24f18a80ee2f237e2f5ed220afbe`, `ready_steam.wav` `483d57ef122dc9929b004441fe01bc44`.
3. **Self-test:** new section *v6.19 boot-already-hot* (8 checks). v6.18 checks adjusted: the sleep → wake check now
   expects the jingle only; the OFF → on check heats the extract boiler after the power-on (tone) while the hot steam
   boiler stays quiet (its "quiet while OFF" count is now relative); the checks that heat up right after a power-on now
   wait past the settle window (`SND_SETTLE_MS + 800`), as does the `105_diag_sound` preview.
4. **Verification (local, §15):**

| What | Result |
|---|---|
| UI self-test (repo fixture config) | **545 / 0** EN and DA; also as a 32-bit ARM binary (ARMv7 container) EN + DA |
| Mutation probes (dev workspace, `workspace/round_v6/build_previews_v619.sh`) | **13 / 13** caught (9 v6.18 + `boot_armed` = the v6.18 behaviour, `first_hot_latches`, `short_settle`, and a no-op control that must stay green) |
| Previews / parity / circle | 106 EN + 106 DA, `parity_check` ALT AFSTEMT, 0 circle violations |
| Sound scenarios EN and DA | `audio_boiler_ready` 27/27, `audio_boot_hot_no_ready` 26/26 (new), `audio_cold_to_hot_ready` 22/22 (new), `audio_muted` 20/20, `audio_power_cycle` 25/25, `audio_sound_off` 24/24; against the v6.18.1 build the two new ones and `audio_power_cycle` fail |
| `sim/selftest.py` · `run_e2e.py --all --quick --parallel 4` | **192 / 0** · **5 / 5** (quick level, EN) |
| Helper self-test (mock MI_AO, plays the new WAVs) · `gen_audio.py --check` | **14 / 14** · OK |
| `tests/test_installer.py`, `boot/test_switcher.py`, `tests/hygiene.py`, `tests/check_bins.py` | green |
| Audio analysis (spectrogram, offline) | no clipping, no truncation clicks, all partials harmonic to the chord, < 0.02 % energy above 6 kHz |
| On-device | **not installed** — needs a listen on the real speaker and a power-on with hot boilers ([audio.md §3.5](../audio.md#35-check-after-installing-v6181), steps 3 and 5) |

5. **Files:** `src/merakiplus.c`, `tools/gen_audio.py`, `audio/*.wav`, `sim/scenarios/audio_{boot_hot_no_ready,cold_to_hot_ready}.json`
   (new), `sim/scenarios/audio_power_cycle.json`, `sim/CONTRACT.md`, `bin/` (`merakiplus`, `merakiplus_sim_arm`, host SIM
   builds), `bin/MD5SUMS`, `tools/merakiplus.py` (VERSION 6.19, `BIN_MD5`, `AUDIO_MD5`), docs, CHANGELOG, README badge.

## CHANGES-v6.20

**Steam during a brew** ([concurrent-brew-steam.md](../concurrent-brew-steam.md): feasibility §1–§10, hardware PASS §11, implementation §12).
The controller runs a latched steam session concurrently with a brew (`0x5327` with word 24 = 1, 20.7 s on the real machine), keeps
only one heater on and stops each activity on its own; the only blocker was `act_guard()`. Frames unchanged.

1. **Guard (`steam_brew_ok`, `act_guard`):** a steam start passes when `phase_raw` is PRE or EXT and work_sta has no bits 3/5/6/14 and
   word 24 = 0; then the error words (34–36 — `phase_raw` ranks a running brew above them, so this is checked explicitly), the queue /
   pending / SAVE and config checks apply as before. All other starts during an activity stay *ACTIVITY IN PROGRESS*. SAVE / SWITCH NOW /
   power-off / sleep guards unchanged. A pending steam start is confirmed by the steam bits (`ui_update`), because the brew keeps the main phase.
2. **HOLD·STEAM (`steam_hold_shown`, `T_STEAMH`):** HOME during a brew with no steam bits: STOP BREW in the left slot, HOLD·STEAM in the
   right slot (`stop_pill(2,·)` / `stop_hit(2,·)`); hold 650 ms → `act_request(ACT_STEAM)`; tap → guard reason or *KEEP HOLDING TO START*;
   the feature page's hold look in the steam colour, dimmed when the guard says no. New string `TX_B_HOLD_STEAM` (*HOLD·STEAM* / *HOLD·DAMP*,
   F_S, 152 px pill). SIM state `stop.steam_hold` (`on` / `dim` / `null`), layout target `steam_hold`.
3. **Self-test** *v6.20 steam during a brew* (10 checks); six older checks (STOP per phase, BREW stop, the valve pulse, STOP NOT CONFIRMED,
   the STOP size check now on WATER, the feature-page takeover) tap STOP BREW at x = 160.
4. **Simulator** (`sim/CONTRACT.md` ADDENDUM v6.20): brew start during steam accepted; heater rule for latched steam during a brew
   (no steam-heater slots until word 47 < target − 100, then bit 13 + cur1 ≈ 7.95 k). Scenarios `concurrent_brew_steam` (rewritten),
   `concurrent_brew_steam_stops`, `concurrent_steam_blocked`.
5. **Verification (local, §15):**

| What | Result |
|---|---|
| UI self-test (repo fixture config) | **555 / 0** EN and DA; also as a 32-bit ARM binary (ARMv7 container) EN + DA |
| Mutation probes (dev workspace, `workspace/round_v6/build_previews_v620.sh`) | **10 / 10** caught (guard unrelaxed / bits / word 24 / error, pending not cleared, no button, button during a flush, hold sends a brew, a tap starts, no-op control) |
| Previews / parity / circle | 106 EN + 106 DA, `parity_check` ALT AFSTEMT, 0 circle violations |
| `sim/selftest.py` | **195 / 0** (T15 new) |
| Full E2E (`run_e2e.py --all --parallel 4`) | EN **48 / 48** scenarios · 1269 / 1269 checks; DA **48 / 48** · 1269 / 1269 (no flakes this run) |
| New scenarios EN and DA | `concurrent_brew_steam` 37/37, `concurrent_brew_steam_stops` 35/35, `concurrent_steam_blocked` 23/23; all three fail against the v6.19 build |
| `tests/hygiene.py`, `tests/check_bins.py`, `tests/test_installer.py`, `boot/test_switcher.py` | green |
| On-device (2026-11-22) | installed (`./install.sh`, watchdog reload, no reboot); live: HOLD·STEAM during a brew → both ran, brew stopped by weight, steam by milk temperature; STOP STEAM mid-brew → the brew continued ([concurrent-brew-steam.md §12](../concurrent-brew-steam.md#12-implementation-v620)) |

6. **Files:** `src/merakiplus.c`, `sim/mcu_sim.py`, `sim/selftest.py`, `sim/scenarios/concurrent_*.json`, `sim/CONTRACT.md`, `sim/README.md`,
   `bin/` (`merakiplus`, `merakiplus_sim_arm`, host SIM builds), `bin/MD5SUMS`, `tools/merakiplus.py` (VERSION 6.20, `BIN_MD5`),
   `docs/concurrent-brew-steam.md` §12, `docs/ui-guide.md` §18.2, README (feature + screenshot), CHANGELOG, `bin/README.md`.

## CHANGES-v6.21

**Launcher, release directories, automatic rollback** ([ota-plan.md phase 1b](../ota-plan.md#phase-1b--implemented-2026-11-22)). The
UI itself changes only where the launcher needs it; frames, guards and limits are unchanged.

1. **Release directory (`ota_init`, `snd_helper`, `snd_path_of`):** the sound helper and the WAVs are taken from `MERAKIPLUS_REL` (set
   by the launcher), else from the UI's own exe directory when it holds `merakiplus_snd`, else from the v6.18–v6.20 paths. A UI
   exe `/new_customer/merakiplus/rel/<name>/demo_v9` counts as slot B (`ota_rel_exe`; BOOT UI, DIAGNOSTICS, the switch logic).
2. **Healthy marker (`ota_step`, `ota_mark_write`):** after ≥ 60 s running and ≥ 30 s of an unbroken controller link (`link_up`),
   once: `/tmp/merakiplus_healthy` (`boot=`, `pid=`, `rel=`, `up=`, `state=healthy`), on a trial (`MERAKIPLUS_TRIAL=1`) also
   `/new_customer/merakiplus/ota/healthy` (tmp + fsync + rename + fsync dir). An orderly exit writes `state=exit`.
3. **Exec-reload (`ota_reload_step`, device build only):** every 2 s, if `/tmp/merakiplus_reload` exists, the UI runs from slot B, no
   stock UI is paused (run.sh mode) and `/new_customer/demo_v9` is the launcher: when `ota_idle()` (HOME or OFF screen, no activity,
   write, save, dialog, touch or sound) it marks all descriptors close-on-exec and `execv`s the launcher with `MERAKIPLUS_LAUNCH=reload`
   (same PID; still no `kill()` in the binary). Otherwise it asks again 2 s later.
4. **SYSTEM line (`ota_line`)** and one-time toasts after the splash: vendor update (`TX_T_UPD_VENDOR`), a release that failed right
   before this start (`MERAKIPLUS_NOTE` → `TX_T_UPD_ROLLED`). The vendor scan (`ota_vendor_scan`) runs once at start: `temp.tar.gz` /
   `demo_temp` in `/customer`, or the switcher installed while `demo.sh` lacks the guard line. New strings `TX_OTA_TRIAL`,
   `TX_OTA_FALLBACK`, `TX_OTA_FAILED`, `TX_OTA_VENDOR`, `TX_T_UPD_VENDOR`, `TX_T_UPD_ROLLED` (Appendix A).
5. **Self-test** section *v6.21 OTA phase 1b* (14 checks). **Verification:** UI self-test **569 / 0** EN and DA (also as a 32-bit ARM
   binary in the ARMv7 container); launcher watchdog model 32/32 (host + ARM container) and 96 power-cut states; installer suite
   140/140 (incl. a 28-sync-point replay of a v6.20 migration); switcher self-test 55/0; `sim/selftest.py` 195/0; quick E2E 5/5;
   full E2E EN **48 / 48** · 1269 / 1269 checks, DA **48 / 48** · 1269 / 1269; hygiene + `check_bins` green. Not yet on a machine.
6. **Files:** `src/merakiplus.c`, `src/merakiplus_boot.c` (new), `boot/switcher.c`, `tools/merakiplus.py`, `tests/` (launcher model,
   replay, installer), `build.sh`, CI + release workflows, `bin/`, docs.

## CHANGES-v6.22

**Water dosing gate + manual hot water** ([water-dosing-compat.md §10–§11](../water-dosing-compat.md#11-implementation-v622),
the UI rules in [ui-guide.md §21](../ui-guide.md#21-hot-water-dosing-gate--manual-water-v622)).

1. **Gate (`mcu_ver_read`, `ver_parse`, `wd_ver_dosing`, `wd_dosing`):** words 80..87 (copy 88..95, both in the existing sub56 len56
   poll) → `<= 6 letters + N.N.N`; dosing = version > 5.1.2 (stock `mcu_has_hot_water_weight_ui` 0x10885C) && copy matches && no scale
   fault (word 35 & 0x1400). Evaluated every frame from the mirror; never written; config.json never written.
2. **Manual hot water (`act_push(ACT_WATER)`, `water_manual()`):** `sub19 len1` word 19 = 1 with the new intent `I_WATERMAN` (word 19
   joins `TRIG_START`); stop on a controller not > 5.1.2 = word 19 = 0 only. `frame_policy` rejects word 16 ≠ 0 unless dosing. The
   `TX_G_WATERMAN` guard is gone. No time limit, no display-side weight stop.
3. **UI:** WATER page without rows + three lines when not dosing (`WMAN_Y0`), HOME *MAN.* / *HOLD · HOT WATER · MANUAL* / *MANUAL ·
   RUNS UNTIL STOP* (no target mark, ring over 300 g, `wd.man` sticky per run), `err_text` *SCALE FAULT* (E2 incl. 0x1400),
   DIAGNOSTICS block CONTROLLER (`DG_MCU`, above ABOUT; `diag_mcu_text`).
4. **Scale fault (`wd_step`):** rising edge → pending water start dropped; > 5.1.2 and word 16 still ≠ 0 → word 16 = 0 once + toast.
5. **Strings:** one contiguous block `TX_ER_E2_SCALE` … `TX_DV_WD_FAULT` (18 ids, excluded from the v6.17 string-count rule like the
   v6.18 sound block); `TX_SN_HOTMODE` reworded; `TX_G_WATERMAN` removed.
6. **Self-test** section *v6.22 water dosing gate* (13 checks); `frame_trig_audit` knows word 19. **Previews** 106–112.
   **SIM:** state `water`, scenarios `dosing_*` (sim/CONTRACT.md addendum v6.22).
7. **Verification:** UI self-test **582 / 0** EN and DA (also 32-bit ARMv7); `sim/selftest.py` 204/0; full E2E **51/51 EN** and **51/51 DA** (`dosing_version_detect` 28, `dosing_gated_old_mcu` 44, `dosing_scale_fault` 34 checks), quick E2E 5/5; installer suite 140/140, launcher tests (96 power-cut states), switcher 55/0, hygiene + bin/ drift OK (official zig 0.17.1). Not yet on a machine.

## CHANGES-v6.23

**Auto off 30 min by default, like stock** ([time-scheduling.md §5.1](../time-scheduling.md#51-default-and-migration-v623),
[safety.md §3](../safety.md#3-where-meraki-differs-from-the-stock-ui)). Maintainer decision after the legal/safety review.

1. **Default:** `AO_DEF` = index 3 (30 min). `sch` starts with it, `sched_load()` resets to it, `SET[S_AUTOOFF].def` = 3,
   and `sched_step()` falls back to it for an out-of-range row value.
2. **Parsing (`sched_load`):** `autooff=` must be exactly `0|15|20|30|60` (`strtol` + end check); anything else — empty,
   `off`, `45`, `0x` — leaves the default. `autooff=0` stays an explicit OFF. `sched_save()` is unchanged and always writes
   the key, so the first save of any SCHEDULE row materialises the shown value.
3. **Migration:** no file → 30; v6.16–v6.22 files always carry the key → their value is kept (an `autooff=0` written as a
   side effect of saving another row cannot be told apart from a deliberate OFF → kept OFF, documented). The reference
   machine's file (`autooff=15`) is one of the self-test cases. The installer does not touch the file.
4. **Self-test** (section *v6.16 time & scheduling*): 13-case migration matrix, SYSTEM row 30 MIN with no file, explicit
   OFF survives a save of another row, default written as `autooff=30`, explicit OFF stays on for 61 min idle, default
   powers off at 30:00 (warning 29:30); the "bad values" check now expects 30. **SIM:** `schedule_prewarm` asserts
   `sched.auto_off = 30` / `sched.file = default` before the first save and `30` / `file` after it (mode `all`).
5. **Unchanged:** frames, frame policy, `config.json`, the power-off sequence, strings (OPT_AUTOOFF already has 30 MIN).
6. **Verification:** UI self-test **588 / 0** EN and DA (also 32-bit ARMv7); mutants: default back to off → 4 checks fail, the old `atoi` parsing → the matrix fails. `schedule_prewarm` 41/41 (39/41 vs the v6.22 SIM); full E2E **51/51 EN** and **51/51 DA**, quick 5/5; `sim/selftest.py` 204/0; installer 140/140, launcher (96 power-cut states), switcher 55/0; hygiene + bin/ drift OK (official zig 0.17.1). Not yet on a machine.

## CHANGES-v6.24

**Water tank messages** (issue #1, [ui-guide.md §22](../ui-guide.md#22-water-tank-messages-v624-issue-1)). Reported on a
Gen 2 machine where Meraki+ v6.23 runs: removing the water tank showed *ERROR · UNKNOWN FAULT*.

1. **Root cause:** `err_text()` mapped word 36 bits 4/5, word 34 bits 0/5, word 35 bits 0/1/3/4 and `0x1400` only. The
   controller's tank check (MCU `0xB268`) sets word 36 `0x8000` (tank switches read 0 → tank missing) or `0x0001`
   (they read 2 → tank low); the dry-pump check (MCU `0xB218`) latches word 35 `0x8000`. All three fell through to
   `TX_ER_UNKNOWN` / code `?`. Stock (`dealLackWaterAndBar` `0x10F6EC`) shows them as the icons `lackWaterTank`,
   `lack_water` (+ string 39 "Water tank is low, please refill water") and `lackWater`; none is in stock's fault-code
   list (`error_tip_deale` `0x10EB14`).
2. **Fix:** `err_id()` (the old `err_text()` returning the id; `err_text()` = `TR(err_id())`) + three ids after the
   existing ones: `TX_ER_E3_TANK`, `TX_ER_E3_LOW`, `TX_ER_E2_NOFLOW` (KSUB, one contiguous block after `TX_ER_UNKNOWN`,
   left out of the v6.17 string-count rule). `err_code()`: word 36 `& 0x8001` → E3, word 35 `& 0x8000` → E2, after the
   existing masks.
3. **Unchanged:** `phase_raw` (ERROR while any error word ≠ 0), `act_guard`, `frame_policy`, frames, config handling.
4. **SIM:** state JSON `err` = `null` | `{id, code}` (sim/CONTRACT.md addendum v6.24); scenario `fault_water_tank`.
5. **Self-test** section *v6.24 water tank messages* (4 checks): 14 bit combinations × EN/DA (sub-line, hint code,
   phase), guards + phase identical to an unmapped bit and no frame queued, word 36 = 0 → READY + brew allowed, widths.
6. **Verification:** UI self-test **592 / 0** EN and DA (also 32-bit ARMv7); 3 mutants caught. `fault_water_tank`
   35/35 EN + DA (22/35 vs the v6.23 SIM). Full E2E **52/52 EN** and **52/52 DA**, quick 5/5; `sim/selftest.py` 204/0; installer 152/152, launcher (96 power-cut states), switcher 55/0, upd 115/115; hygiene + bin/ drift OK (official zig 0.17.1). Gen 2 bits not verified (issue #1
   asks for the DIAGNOSTICS values); not yet on a machine.

## Appendix A — string table (id / EN / DA)

<!-- I18N-APPENDIX:BEGIN (generated by i18n_appendix.py — edit STR_TABLE in merakiplus.c, not here) -->

**488 string ids × 2 languages** (54 are the same in EN and DA: units, proper names, technical words and commands). Class = the field the string is shown in; the self-test measures every classified string in both languages against the field's pixel budget (`-` = measured in its context by the layout tests). `%s`/`%d` are placeholders for numbers and names; text in `code font` inside a string is shown in lower case (paths, commands). Source: `round_v6/strings.tsv` (`--dump-strings`). The EN and DA columns are the UI's actual strings and are deliberately kept in both languages.


**Units (identical in both languages)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_U_G` | - | `G` | `G` |
| `TX_U_S` | - | `S` | `S` |
| `TX_U_BAR` | - | `BAR` | `BAR` |
| `TX_U_C` | - | `°C` | `°C` |
| `TX_U_F` | - | `°F` | `°F` |

**Product, splash and pages**

| id | class | EN | DA |
|---|---|---|---|
| `TX_TAGLINE` | tagline | `ADVANCED BREWING INTERFACE` | `AVANCERET BRYGGE-INTERFACE` |
| `TX_SPL_CFG_OK` | - | `CONFIG LOADED` | `CONFIG INDLÆST` |
| `TX_SPL_CFG_MISSING` | - | `CONFIG MISSING` | `CONFIG MANGLER` |
| `TX_PG_SYSTEM` | page-title | `SYSTEM` | `SYSTEM` |
| `TX_PG_DIAG` | page-title | `DIAGNOSTICS` | `DIAGNOSE` |
| `TX_SEC_DISPLAY` | - | `DISPLAY · SOUND` | `SKÆRM · LYD` |
| `TX_SEC_CLOCK` | - | `CLOCK` | `UR` |
| `TX_SEC_SCHED` | - | `SCHEDULE` | `TIDSPLAN` |
| `TX_SEC_STOCK` | - | `STOCK UI` | `STOCK-UI` |
| `TX_LV_GRIND` | - | `ON THE SCALE %s G` | `PÅ VÆGTEN %s G` |
| `TX_LV_BREW` | - | `BOILER %s° · GROUP %s°` | `KEDEL %s° · GRUPPE %s°` |
| `TX_LV_WATER` | - | `WATER %s°` | `VAND %s°` |
| `TX_LV_STEAM` | - | `BOILER %s° · MILK %s°` | `KEDEL %s° · MÆLK %s°` |

**Settings: row labels (SL), notes (SN) and hints (SH)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_SL_GRINDW` | - | `GRIND DOSE` | `KVÆRN-DOSIS` |
| `TX_SN_GRINDW` | note | `STOPS AUTOMATICALLY AT THIS WEIGHT` | `STOPPER AUTOMATISK VED DENNE VÆGT` |
| `TX_SL_GRINDMODE` | - | `GRIND MODE` | `KVÆRN-TILSTAND` |
| `TX_SN_GRINDMODE` | note | `MERAKI+ STARTS WEIGHT MODE ONLY` | `MERAKI+ STARTER KUN VÆGT-TILSTAND` |
| `TX_SL_BREWW` | - | `BREW WEIGHT` | `BRYG-VÆGT` |
| `TX_SN_BREWW` | note | `STOPS AUTOMATICALLY AT THIS WEIGHT` | `STOPPER AUTOMATISK VED DENNE VÆGT` |
| `TX_SL_BREWT` | - | `BREW TEMP` | `BRYG-TEMP` |
| `TX_SN_BREWT` | note | `TARGET FOR THE BREW BOILER` | `MÅL FOR BRYGKEDLEN` |
| `TX_SL_PISW` | - | `PRE-INFUSION` | `PRE-INFUSION` |
| `TX_SN_PISW` | note | `SOAKS THE PUCK BEFORE FULL PRESSURE` | `VÆDER KAFFEN FØR FULDT TRYK` |
| `TX_SL_PITIME` | - | `PRE-INF. TIME` | `PRE-INF. TID` |
| `TX_SN_PITIME` | note | `SOAK TIME BEFORE FULL PRESSURE` | `VÆDE-TID FØR FULDT TRYK` |
| `TX_SL_PIPRESS` | - | `PRE-INF. PRESSURE` | `PRE-INF. TRYK` |
| `TX_SN_PIPRESS` | note | `THE MACHINE ONLY KNOWS 3 AND 4` | `MASKINEN KENDER KUN 3 OG 4` |
| `TX_SL_BREAK` | - | `PAUSE TIME` | `PAUSE-TID` |
| `TX_SN_BREAK` | note | `NO EFFECT · PRE-INF. TIME IS USED` | `INGEN EFFEKT · PRE-INF. TID BRUGES` |
| `TX_SL_HEADT` | - | `GROUP HEAD` | `GRUPPEHOVED` |
| `TX_SN_HEADT` | note | `TARGET FOR THE GROUP HEAD` | `MÅL FOR GRUPPEHOVEDET` |
| `TX_SL_STEAMT` | - | `MILK TEMP` | `MÆLKETEMP` |
| `TX_SN_STEAMT` | note | `STEAM STOPS AT THIS MILK TEMP` | `DAMPEN STOPPER VED DENNE MÆLKETEMP` |
| `TX_SL_STEAMG` | - | `STEAM LEVEL` | `DAMP-NIVEAU` |
| `TX_SN_STEAMG` | note | `STEAM POWER 1-4` | `DAMP-STYRKE 1-4` |
| `TX_SL_HOTW` | - | `HOT WATER` | `VARMT VAND` |
| `TX_SN_HOTW` | note | `STOPS AUTOMATICALLY AT THIS WEIGHT` | `STOPPER AUTOMATISK VED DENNE VÆGT` |
| `TX_SL_HOTMODE` | - | `WATER MODE` | `VAND-TILSTAND` |
| `TX_SN_HOTMODE` | note | `AUTO STOPS AT THE WEIGHT · MANUAL AT STOP` | `AUTO STOPPER VED VÆGTEN · MANUEL VED STOP` |
| `TX_SL_HOTT` | - | `WATER SENSOR` | `VAND-SENSOR` |
| `TX_SN_HOTT` | note | `MEASURED ONLY · NO TARGET EXISTS` | `KUN MÅLT · INTET MÅL FINDES` |
| `TX_SL_UILANG` | - | `UI LANGUAGE` | `UI-SPROG` |
| `TX_SN_UILANG` | note | `LIVE PREVIEW · SAVE TO KEEP` | `LIVE PREVIEW · GEM FOR AT BEHOLDE` |
| `TX_SL_BL` | - | `BRIGHTNESS` | `LYSSTYRKE` |
| `TX_SN_BL` | note | `LIVE PREVIEW` | `LIVE PREVIEW` |
| `TX_SL_VOL` | - | `VOLUME` | `LYDSTYRKE` |
| `TX_SN_VOL` | note | `MERAKI+ + STOCK UI SOUNDS` | `MERAKI+ + STOCK-UI LYDE` |
| `TX_SH_VOL` | - | `STEPS = ESTIMATE` | `TRIN = SKØN` |
| `TX_SL_TUNIT` | - | `TEMP UNIT` | `TEMP-ENHED` |
| `TX_SN_TUNIT` | note | `ALSO APPLIES IN MERAKI+` | `GÆLDER OGSÅ I MERAKI+` |
| `TX_SL_LANG` | - | `STOCK LANG.` | `STOCK-SPROG` |
| `TX_SN_LANG` | note | `STOCK UI ONLY` | `KUN STOCK-UI` |
| `TX_SL_HOTSC` | - | `WATER SHORTCUT` | `VAND-GENVEJ` |
| `TX_SN_HOTSC` | note | `STOCK UI HOME SHORTCUT` | `STOCK-UI HJEM-GENVEJ` |
| `TX_SL_DST` | - | `DAYLIGHT SAVING` | `SOMMERTID` |
| `TX_SL_CLEARTIP` | - | `CLEANING REMINDER` | `RENSE-PÅMINDELSE` |
| `TX_SN_CLEARTIP` | note | `STOCK UI TIP` | `STOCK-UI TIP` |
| `TX_SL_TIMEMODE` | - | `TIME FORMAT` | `TIDSFORMAT` |
| `TX_SL_AUTOTIME` | - | `AUTO TIME` | `AUTO-TID` |
| `TX_SN_AUTOTIME` | note | `STOCK UI NETWORK TIME` | `STOCK-UI NETVÆRKSTID` |
| `TX_SL_TZ` | - | `TIME ZONE` | `TIDSZONE` |
| `TX_SN_TZ` | note | `SET IT IN THE STOCK UI` | `SÆTTES I STOCK-UI'ET` |
| `TX_SH_EST` | - | `MAX = ESTIMATE` | `MAX = SKØN` |

**Enum names**

| id | class | EN | DA |
|---|---|---|---|
| `TX_E_OFF` | - | `OFF` | `FRA` |
| `TX_E_ON` | - | `ON` | `TIL` |
| `TX_E_WEIGHT` | - | `WEIGHT` | `VÆGT` |
| `TX_E_TIME` | - | `TIME` | `TID` |
| `TX_E_AUTO` | - | `AUTO` | `AUTO` |
| `TX_E_MANUAL` | - | `MANUAL` | `MANUEL` |
| `TX_E_GENTLE` | - | `GENTLE` | `BLID` |
| `TX_E_MILD` | - | `MILD` | `MILD` |
| `TX_E_MEDIUM` | - | `MEDIUM` | `MEDIUM` |
| `TX_E_STRONG` | - | `STRONG` | `STÆRK` |
| `TX_E_24H` | - | `24-HOUR` | `24-TIMERS` |
| `TX_E_12H` | - | `12-HOUR` | `12-TIMERS` |
| `TX_LG_EN` | - | `ENGLISH` | `ENGELSK` |
| `TX_LG_DE` | - | `GERMAN` | `TYSK` |
| `TX_LG_FR` | - | `FRENCH` | `FRANSK` |
| `TX_LG_IT` | - | `ITALIAN` | `ITALIENSK` |
| `TX_LG_ES` | - | `SPANISH` | `SPANSK` |
| `TX_LG_PL` | - | `POLISH` | `POLSK` |
| `TX_LG_AR` | - | `ARABIC` | `ARABISK` |
| `TX_LG_ZHS` | - | `CHINESE SIMPL.` | `KINESISK FORENKL.` |
| `TX_LG_ZHT` | - | `CHINESE TRAD.` | `KINESISK TRAD.` |
| `TX_LG_KO` | - | `KOREAN` | `KOREANSK` |
| `TX_LG_JA` | - | `JAPANESE` | `JAPANSK` |
| `TX_UL_EN` | - | `ENGLISH` | `ENGLISH` |
| `TX_UL_DA` | - | `DANSK` | `DANSK` |

**Phases, error codes and activities**

| id | class | EN | DA |
|---|---|---|---|
| `TX_PH_OFFLINE` | phase-chip | `OFFLINE` | `OFFLINE` |
| `TX_PH_SLEEP` | phase-chip | `ASLEEP` | `SOVER` |
| `TX_PH_ERROR` | phase-chip | `ERROR` | `FEJL` |
| `TX_PH_HEAT` | phase-chip | `HEATING UP` | `VARMER OP` |
| `TX_PH_READY` | phase-chip | `READY` | `KLAR` |
| `TX_PH_GRIND` | phase-chip | `GRINDING` | `KVÆRNER` |
| `TX_PH_PRE` | phase-chip | `PRE-INFUSION` | `PRE-INFUSION` |
| `TX_PH_EXT` | phase-chip | `EXTRACTION` | `EKSTRAKTION` |
| `TX_PH_WATER` | phase-chip | `HOT WATER` | `VARMT VAND` |
| `TX_PH_STEAM` | phase-chip | `STEAM` | `DAMP` |
| `TX_PH_DONE` | phase-chip | `DONE` | `FÆRDIG` |
| `TX_PH_STEAM_AUTO` | phase-chip | `STEAM·AUTO` | `DAMP·AUTO` |
| `TX_PH_STEAM_WAND` | phase-chip | `STEAM·WAND` | `DAMP·WAND` |
| `TX_ER_E3_BREW` | home-sub | `BREW BOILER TOO HOT` | `BRYGKEDLEN ER FOR VARM` |
| `TX_ER_E3_STEAM` | home-sub | `STEAM BOILER TOO HOT` | `DAMPKEDLEN ER FOR VARM` |
| `TX_ER_E1_GROUP` | home-sub | `GROUP HEAD TOO HOT` | `GRUPPEHOVEDET ER FOR VARMT` |
| `TX_ER_E1_MAINS` | home-sub | `PROBLEM WITH MAINS VOLTAGE` | `PROBLEM MED NETSPÆNDINGEN` |
| `TX_ER_E2_BREW` | home-sub | `BREW BOILER SENSOR FAULT` | `FEJL PÅ BRYGKEDEL-SENSOR` |
| `TX_ER_E2_GROUP` | home-sub | `GROUP HEAD SENSOR FAULT` | `FEJL PÅ GRUPPEHOVED-SENSOR` |
| `TX_ER_E2_STEAM` | home-sub | `STEAM BOILER SENSOR FAULT` | `FEJL PÅ DAMPKEDEL-SENSOR` |
| `TX_ER_E2_MILK` | home-sub | `MILK SENSOR FAULT` | `FEJL PÅ MÆLKESENSOR` |
| `TX_ER_UNKNOWN` | home-sub | `UNKNOWN FAULT` | `UKENDT FEJL` |
| `TX_ER_E3_TANK` | home-sub | `WATER TANK MISSING` | `VANDTANKEN MANGLER` |
| `TX_ER_E3_LOW` | home-sub | `WATER TANK LOW · PLEASE REFILL` | `LAVT VANDNIVEAU · FYLD TANKEN` |
| `TX_ER_E2_NOFLOW` | home-sub | `NO WATER FLOW · CHECK THE TANK` | `INTET VANDFLOW · TJEK TANKEN` |
| `TX_A_GRIND` | - | `GRIND` | `KVÆRN` |
| `TX_A_BREW` | - | `BREW` | `BRYG` |
| `TX_A_WATER` | - | `WATER` | `VAND` |
| `TX_A_STEAM` | - | `STEAM` | `DAMP` |

**Guards and toasts**

| id | class | EN | DA |
|---|---|---|---|
| `TX_G_RO` | toast | `READ-ONLY · NO WRITES` | `READ-ONLY · INGEN WRITES` |
| `TX_G_NOLINK` | toast | `NO CONNECTION` | `INGEN FORBINDELSE` |
| `TX_G_ERROR` | toast | `ERROR ACTIVE · START BLOCKED` | `FEJL AKTIV · START SPÆRRET` |
| `TX_G_SLEEP` | toast | `WAKING UP · ONE MOMENT` | `VÅGNER · ET ØJEBLIK` |
| `TX_G_BUSY` | toast | `ACTIVITY IN PROGRESS` | `AKTIVITET I GANG` |
| `TX_G_WAIT` | toast | `ONE MOMENT…` | `ET ØJEBLIK…` |
| `TX_G_NOCFG` | toast | `CONFIG MISSING · START BLOCKED` | `CONFIG MANGLER · START SPÆRRET` |
| `TX_G_OOR` | toast | `CONFIG OUT OF SPEC · FIX FIRST` | `CONFIG UDEN FOR SPEC · RET FØRST` |
| `TX_G_GRINDTIME` | toast | `GRIND = TIME · USE STOCK UI` | `KVÆRN = TID · BRUG STOCK-UI` |
| `TX_T_POLICY` | toast | `BLOCKED BY A SAFETY RULE` | `SPÆRRET AF EN SIKKERHEDSREGEL` |
| `TX_T_QFULL` | toast | `BUSY · TRY AGAIN` | `OPTAGET · PRØV IGEN` |
| `TX_T_RANGE` | toast | `OUT OF RANGE` | `UDEN FOR GRÆNSER` |
| `TX_T_CFG_UNAVAIL` | toast | `CONFIG UNAVAILABLE · NOT SAVED` | `CONFIG UTILGÆNGELIG · INTET GEMT` |
| `TX_T_NOLINK_SAVE` | toast | `NO CONNECTION · NOT SAVED` | `INGEN FORBINDELSE · INTET GEMT` |
| `TX_T_WAIT_ACT` | toast | `WAIT UNTIL THE ACTIVITY ENDS` | `VENT TIL AKTIVITET ER SLUT` |
| `TX_T_WRITE_REJ` | toast | `NOT ACCEPTED · NOT SAVED` | `IKKE ACCEPTERET · INTET GEMT` |
| `TX_T_NOACK_SAVE` | toast | `SAVE FAILED · NO REPLY` | `GEM FEJLET · INTET SVAR` |
| `TX_T_SAVED_MCU` | toast | `SAVED · ACTIVE NOW` | `GEMT · AKTIV NU` |
| `TX_T_SAVED_CFG` | toast | `SAVED` | `GEMT` |
| `TX_T_CFG_ERR` | toast | `CONFIG ERROR · NOT SAVED` | `CONFIG-FEJL · INTET GEMT` |
| `TX_T_LANG_SAVED` | toast | `LANGUAGE SAVED · ENGLISH` | `SPROG GEMT · DANSK` |
| `TX_T_LANG_NOFILE` | toast | `LANGUAGE ACTIVE · FILE NOT SAVED` | `SPROG AKTIVT · FIL IKKE GEMT` |
| `TX_T_DRYRUN` | toast | DRY RUN · SEE `cfgbak/boot_ui.log` | DRY-RUN · SE `cfgbak/boot_ui.log` |
| `TX_T_SW_FAIL_NOBIN` | toast | `SWITCH FAILED · NO STOCK BINARY` | `SKIFT FEJLEDE · INGEN STOCK-BINÆR` |
| `TX_T_SW_FAIL_FORK` | toast | `SWITCH FAILED · FORK ERROR` | `SKIFT FEJLEDE · FORK-FEJL` |
| `TX_T_SW_FAIL_DIED` | toast | `SWITCH FAILED · STOCK UI EXITED` | `SKIFT FEJLEDE · STOCK-UI STOPPEDE` |
| `TX_T_BOOT_SAVED_OURS` | toast | `SAVED · NEXT RESTART: MERAKI+` | `GEMT · NÆSTE GENSTART: MERAKI+` |
| `TX_T_BOOT_SAVED_ORIG` | toast | `SAVED · NEXT RESTART: ORIGINAL` | `GEMT · NÆSTE GENSTART: ORIGINAL` |
| `TX_T_BOOT_LOCKED` | toast | `LOCKED · FLAG NOT CHANGED` | `LÅST · FLAG IKKE ÆNDRET` |
| `TX_T_BOOT_ERR` | toast | `FLAG ERROR · NOTHING CHANGED` | `FLAG-FEJL · INTET ÆNDRET` |
| `TX_T_STOP_SENT` | toast | `STOPPING STEAM…` | `STOPPER DAMP…` |
| `TX_T_STOP_GRIND` | toast | `STOPPING GRIND…` | `STOPPER KVÆRN…` |
| `TX_T_STOP_BREW` | toast | `STOPPING BREW…` | `STOPPER BRYG…` |
| `TX_T_STOP_WATER` | toast | `STOPPING WATER…` | `STOPPER VAND…` |
| `TX_T_STOPPED_GRIND` | toast | `GRIND STOPPED` | `KVÆRN STOPPET` |
| `TX_T_STOPPED_BREW` | toast | `BREW STOPPED` | `BRYG STOPPET` |
| `TX_T_STOPPED_WATER` | toast | `WATER STOPPED` | `VAND STOPPET` |
| `TX_T_STOPPED_STEAM` | toast | `STEAM STOPPED` | `DAMP STOPPET` |
| `TX_T_STOP_NOACK` | toast | `STOP: NO REPLY · TRY AGAIN` | `STOP: INTET SVAR · PRØV IGEN` |
| `TX_T_STOP_UNCONF` | toast | `STOP NOT CONFIRMED · TRY AGAIN` | `STOP IKKE BEKRÆFTET · PRØV IGEN` |
| `TX_T_STOP_FAIL` | toast | `STOP NOT SENT · TRY AGAIN` | `STOP IKKE SENDT · PRØV IGEN` |
| `TX_T_PW_BUSY` | toast | `STOP THE ACTIVITY FIRST · THEN OFF` | `STOP AKTIVITETEN FØRST · SÅ SLUK` |
| `TX_T_PW_NOLINK` | toast | `NO CONNECTION · CANNOT TURN OFF` | `INGEN FORBINDELSE · SLUKKER IKKE` |
| `TX_T_PW_NOACK` | toast | `POWER OFF: NO REPLY · TRY AGAIN` | `SLUK: INTET SVAR · PRØV IGEN` |
| `TX_T_PW_ON` | toast | `MACHINE IS ON` | `MASKINEN ER TÆNDT` |
| `TX_T_HOLD_HINT` | toast | `KEEP HOLDING TO START` | `BLIV VED AT HOLDE FOR START` |
| `TX_T_PINFO` | - | `PRE-INF. %d BAR = RAW %d · LINEAR` | `PRÆ-INF. %d BAR = RÅ %d · LINEÆR` |
| `TX_T_NOACK` | toast | `THE MACHINE DID NOT ANSWER` | `MASKINEN SVAREDE IKKE` |
| `TX_T_NOSTART` | toast | `NO START · CHECK THE MACHINE` | `INGEN START · TJEK MASKINEN` |

**HOME (labels, sub-lines, hold hints, stats, chart, buttons)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_H_DOSE` | home-label | `DOSE` | `DOSIS` |
| `TX_H_CUP` | home-label | `IN THE CUP` | `I KOPPEN` |
| `TX_H_MILK_TGT` | home-label | `MILK · TARGET %s°` | `MÆLK · MÅL %s°` |
| `TX_H_MILK` | home-label | `MILK` | `MÆLK` |
| `TX_H_MILK_PROBE` | home-label | `MILK PROBE` | `MÆLKEPROBE` |
| `TX_H_BOILER` | home-label | `BREW BOILER` | `BRYGKEDEL` |
| `TX_H_CRC` | - | `CRC %u` | `CRC %u` |
| `TX_H_DONE_IN` | home-sub | `DONE IN %s` | `FÆRDIG PÅ %s` |
| `TX_H_AUTOSTOP_G` | home-sub | `STOPS AT %s G` | `STOPPER VED %s G` |
| `TX_H_AUTOSTOP_T` | home-sub | `STOPS AT %s°` | `STOPPER VED %s°` |
| `TX_H_WAND_NOSTOP` | home-sub | `RELEASE THE WAND TO STOP` | `SLIP WAND FOR AT STOPPE` |
| `TX_H_DONE_RISE` | home-sub | `DONE IN %s · RISES ≈%s° MORE` | `FÆRDIG PÅ %s · STIGER ≈%s° TIL` |
| `TX_H_NORESP` | home-sub | `NO CONNECTION TO THE MACHINE` | `INGEN FORBINDELSE TIL MASKINEN` |
| `TX_H_WAKING` | home-sub | `WAKING UP…` | `VÅGNER…` |
| `TX_H_HEATING_TO` | home-sub | `HEATING TO %s°` | `PÅ VEJ MOD %s°` |
| `TX_H_SET_TO` | home-sub | `TARGET %s°` | `MÅL %s°` |
| `TX_H_STARTING` | home-sub | `STARTING %s…` | `STARTER %s…` |
| `TX_H_HOLD_GRIND` | home-sub | `HOLD · GRIND %s G` | `HOLD · KVÆRN %s G` |
| `TX_H_HOLD_BREW` | home-sub | `HOLD · BREW %s G` | `HOLD · BRYG %s G` |
| `TX_H_HOLD_WATER` | home-sub | `HOLD · HOT WATER %s G` | `HOLD · VARMT VAND %s G` |
| `TX_H_HOLD_STEAM` | home-sub | `HOLD · STEAM TO %s°` | `HOLD · DAMP TIL %s°` |
| `TX_ST_TIME` | - | `TIME` | `TID` |
| `TX_ST_BAR` | - | `≈BAR` | `≈BAR` |
| `TX_ST_PRAW` | - | `RAW` | `RÅ` |
| `TX_ST_PULSE` | - | `PULSE` | `PULS` |
| `TX_ST_FLOW` | - | `FLOW` | `FLOW` |
| `TX_ST_CURRENT` | - | `CURRENT` | `STRØM` |
| `TX_ST_GRINDSTEP` | - | `STEP` | `TRIN` |
| `TX_ST_BOILER` | - | `BOILER` | `KEDEL` |
| `TX_ST_STEAMBOILER` | - | `STEAM` | `DAMP` |
| `TX_ST_LEVEL` | - | `LEVEL` | `NIVEAU` |
| `TX_ST_GROUP` | - | `GROUP` | `GRUPPE` |
| `TX_ST_WEIGHT` | - | `WEIGHT` | `VÆGT` |
| `TX_ST_MILK` | - | `MILK` | `MÆLK` |
| `TX_C_HINT` | - | `HOLD = START · TAP = SETTINGS` | `HOLD = START · TRYK = INDSTIL` |
| `TX_C_PREHEAT` | - | `{icon11} PRE-HEAT · %s %s` | `{icon11} FORVARMNING · %s %s` |
| `TX_C_ERRCODE` | - | `CODE %s · TAP FOR DETAILS` | `KODE %s · TRYK FOR DETALJER` |
| `TX_C_DIAGREF` | - | `TAP FOR DETAILS` | `TRYK FOR DETALJER` |
| `TX_C_PRESS` | - | `PRESSURE` | `TRYK` |
| `TX_C_WEIGHT` | - | `WEIGHT` | `VÆGT` |
| `TX_C_MILK` | - | `MILK TEMP` | `MÆLKETEMP` |
| `TX_C_TARGET` | - | `- - TARGET` | `- - MÅL` |
| `TX_C_RAW` | - | `RAW` | `RÅ` |
| `TX_B_STOP_STEAM` | - | `STOP STEAM` | `STOP DAMP` |
| `TX_B_STOP_GRIND` | - | `STOP GRIND` | `STOP KVÆRN` |
| `TX_B_STOP_BREW` | - | `STOP BREW` | `STOP BRYG` |
| `TX_B_STOP_WATER` | - | `STOP WATER` | `STOP VAND` |

**Buttons**

| id | class | EN | DA |
|---|---|---|---|
| `TX_B_START` | - | `HOLD TO START` | `HOLD FOR START` |
| `TX_B_HOLD_STEAM` | - | `HOLD·STEAM` | `HOLD·DAMP` |
| `TX_B_BACK` | button | `BACK` | `TILBAGE` |
| `TX_B_CANCEL` | button | `CANCEL` | `ANNULLER` |
| `TX_B_SAVE` | button | `SAVE` | `GEM` |
| `TX_B_SAVING` | button | `SAVING` | `GEMMER` |
| `TX_B_SWITCH` | button | `SWITCH NOW` | `SKIFT NU` |
| `TX_B_EXIT` | button | `EXIT` | `AFSLUT` |

**SYSTEM: startup / UI choice (BOOT UI, lock reasons, help texts)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_X_BOOTUI` | - | `BOOT UI` | `BOOT-UI` |
| `TX_X_SWITCH` | - | `SWITCH TO ORIGINAL NOW` | `SKIFT TIL ORIGINAL NU` |
| `TX_X_SWITCH_SUB` | row-sub | `IMMEDIATE · NO RESTART` | `STRAKS · UDEN GENSTART` |
| `TX_BN_ORIGINAL` | - | `ORIGINAL` | `ORIGINAL` |
| `TX_BN_OURS` | - | `MERAKI+` | `MERAKI+` |
| `TX_BN_SLOTB` | - | `SLOT B` | `SLOT B` |
| `TX_BL_NOMOUNT` | row-sub | `LOCKED · SLOT B NOT MOUNTED` | `LÅST · SLOT B IKKE MOUNTET` |
| `TX_BL_NOMOUNT_L` | note | `/new_customer` IS NOT MOUNTED | `/new_customer` ER IKKE MOUNTET |
| `TX_BL_NOBIN` | row-sub | LOCKED · SLOT B HAS NO `demo_v9` | LÅST · SLOT B UDEN `demo_v9` |
| `TX_BL_NOBIN_L` | note | `/new_customer/demo_v9` IS MISSING | `/new_customer/demo_v9` MANGLER |
| `TX_BL_VENDOR` | row-sub | `LOCKED · SLOT B IS NOT MERAKI+` | `LÅST · SLOT B ER IKKE MERAKI+` |
| `TX_BL_VENDOR_L` | note | `/new_customer/demo_v9` IS NOT MERAKI+ | `/new_customer/demo_v9` ER IKKE MERAKI+ |
| `TX_BS_OURS` | - | `NOW: MERAKI+ (B) AT BOOT` | `NU: MERAKI+ (B) VED BOOT` |
| `TX_BS_ORIG` | - | `NOW: ORIGINAL (A) AT BOOT` | `NU: ORIGINAL (A) VED BOOT` |
| `TX_BS_SLOTB` | - | `NOW: SLOT B (NOT MERAKI+) AT BOOT` | `NU: SLOT B (IKKE MERAKI+) VED BOOT` |
| `TX_BH1` | row-sub | `APPLIES AT NEXT RESTART` | `GÆLDER NÆSTE GENSTART` |
| `TX_BH2` | foot-1 | `OTA MAY OVERWRITE THIS` | `OTA KAN OVERSKRIVE DETTE` |
| `TX_BH3` | foot-2 | CHOICE — SEE BOOT`.md` §7 | VALG — SE BOOT`.md` §7 |
| `TX_SW_LOCK` | row-sub | `LOCKED · STOCK BINARY MISSING` | `LÅST · STOCK-BINÆR MANGLER` |
| `TX_SW_LOCK_L` | note | `/customer/demo_v9` IS MISSING | `/customer/demo_v9` MANGLER |

**Stepper detail**

| id | class | EN | DA |
|---|---|---|---|
| `TX_D_MEASURED` | - | `MEASURED VALUE · NOT A SETTING` | `MÅLT VÆRDI · INGEN INDSTILLING` |
| `TX_D_NOEDIT` | - | `CANNOT BE CHANGED HERE` | `KAN IKKE ÆNDRES HER` |
| `TX_D_NOPTS` | - | `%d OPTIONS` | `%d VALG` |
| `TX_D_SAVING` | status | `SAVING…` | `GEMMER…` |
| `TX_D_OOR` | status | `OUT OF SPEC · PICK A NEW VALUE` | `UDEN FOR SPEC · VÆLG NY VÆRDI` |
| `TX_D_HOLDFAST` | status | `HOLD - OR + TO GO FAST` | `HOLD - ELLER + FOR HURTIGT` |
| `TX_D_CHANGED` | - | `CHANGED · WAS %s %s` | `ÆNDRET · FØR %s %s` |
| `TX_D_LOCKED` | status | `LOCKED` | `LÅST` |

**System actions: switch confirmation, exit reminder, goodbye screen**

| id | class | EN | DA |
|---|---|---|---|
| `TX_Y_CHANGED` | - | `CHANGED · WAS %s` | `ÆNDRET · FØR %s` |
| `TX_Y_FLAG_YES` | foot-1 | FLAG `/customer/new` EXISTS | FLAG `/customer/new` FINDES |
| `TX_Y_FLAG_NO` | foot-1 | NO FLAG `/customer/new` | INTET FLAG `/customer/new` |
| `TX_Y_FLAG_HANDSOFF` | foot-2 | `FLAG IS NOT TOUCHED HERE` | `FLAGET RØRES IKKE HERFRA` |
| `TX_Y_NEXT` | - | `NEXT RESTART: %s` | `NÆSTE GENSTART: %s` |
| `TX_Y_NOSTOCK` | foot-1 | `NO STOCK UI TO START` | `INTET STOCK-UI AT STARTE` |
| `TX_Y_STAYS` | foot-2 | `MERAKI+ KEEPS RUNNING` | `MERAKI+ BLIVER KØRENDE` |
| `TX_Y_CLOSE_CONT` | status | `MERAKI+ CLOSES · STOCK UI RESUMES` | `MERAKI+ LUKKER · STOCK-UI FORTSÆTTER` |
| `TX_Y_CLOSE_START` | status | `MERAKI+ CLOSES · STOCK UI STARTS` | `MERAKI+ LUKKER · STOCK-UI STARTER` |
| `TX_Y_REMINDER_FOLLOWS` | note | `CONFIG CHANGED · REMINDER FOLLOWS` | `CONFIG ÆNDRET · PÅMINDELSE FØLGER` |
| `TX_Y_PAUSED_PID` | note | `STOCK UI IS PAUSED · PID %d` | `STOCK-UI ER PAUSET · PID %d` |
| `TX_Y_RUNSH` | foot-1 | `RUN.SH MODE · CLEAN EXIT` | `RUN.SH-TILSTAND · REN EXIT` |
| `TX_Y_BACK_RUNSH` | foot-2 | `BACK HERE: RUN.SH` | `TILBAGE HERTIL: RUN.SH` |
| `TX_Y_FRESH` | note | `FRESH START · READS CURRENT CONFIG` | `FRISK START · LÆSER AKTUEL CONFIG` |
| `TX_Y_BACK_GESTURE1` | foot-1 | `BACK HERE: 3 LONG PRESSES` | `TILBAGE HERTIL: 3 LANGE TRYK` |
| `TX_Y_BACK_GESTURE2` | foot-2 | `TOP LEFT CORNER (1.5 S)` | `ØVERST TIL VENSTRE (1.5 S)` |
| `TX_Y_NOSWITCHER` | foot-1 | `SWITCHER DAEMON NOT RUNNING` | `SWITCHER-DAEMON KØRER IKKE` |
| `TX_Y_BACK_RESTART` | foot-2 | `BACK HERE: RESTART` | `TILBAGE HERTIL: GENSTART` |
| `TX_Y_BACK_TELNET` | foot-2 | `BACK HERE: TELNET ONLY` | `TILBAGE HERTIL: KUN TELNET` |
| `TX_Y_EXIT_TITLE` | - | `CONFIG HAS CHANGED` | `CONFIG ER ÆNDRET` |
| `TX_Y_EXIT_SUB` | - | `STOCK UI HAS OLD VALUES` | `STOCK-UI HAR GAMLE VÆRDIER` |
| `TX_Y_EXIT_NOTE` | note | `RUN VIA TELNET · WATCHDOG RESTARTS IT` | `KØR VIA TELNET · WATCHDOG STARTER DEN IGEN` |
| `TX_Y_EXIT_F1` | foot-1 | `OTHERWISE STOCK UI SENDS` | `ELLERS SENDER STOCK-UI` |
| `TX_Y_EXIT_F2` | foot-2 | `OLD VALUES WHEN BREWING` | `GAMLE VÆRDIER VED BRYG` |
| `TX_Y_EXIT_L1` | - | `STOCK UI NEEDS A RESTART OF` | `STOCK-UI KRÆVER GENSTART AF` |
| `TX_Y_EXIT_L2` | - | `demo_v9` FOR NEW VALUES | `demo_v9` FOR NYE VÆRDIER |
| `TX_Y_EXIT_CMD` | - | `kill $(pidof demo_v9)` | `kill $(pidof demo_v9)` |
| `TX_Y_BYE_T` | - | `SWITCHING TO ORIGINAL` | `SKIFTER TIL ORIGINAL` |
| `TX_Y_BYE_CONT` | - | `STOCK UI RESUMES` | `STOCK-UI FORTSÆTTER` |
| `TX_Y_BYE_START` | - | `STOCK UI STARTING · ONE MOMENT` | `STOCK-UI STARTER · ET ØJEBLIK` |
| `TX_Y_BYE_CLOSED` | - | `MERAKI+ HAS CLOSED` | `MERAKI+ ER AFSLUTTET` |

**Other**

| id | class | EN | DA |
|---|---|---|---|
| `TX_PW_OFFING` | - | `TURNING OFF…` | `SLUKKER…` |
| `TX_PW_OFF` | - | `OFF` | `SLUKKET` |
| `TX_PW_WAKING` | - | `TURNING ON…` | `TÆNDER…` |
| `TX_PW_SUB_OFFING` | - | `THE MACHINE GOES TO SLEEP` | `MASKINEN GÅR I DVALE` |
| `TX_PW_SUB_OFF` | - | `HOLD THE POWER BUTTON TO TURN ON` | `HOLD KNAPPEN INDE FOR AT TÆNDE` |
| `TX_PW_SUB_WAKING` | - | `KEEP HOLDING THE BUTTON A MOMENT` | `HOLD KNAPPEN INDE ET ØJEBLIK` |
| `TX_PW_NOTE_OFF` | - | `THE SCREEN TURNS OFF · TOUCH TO SHOW` | `SKÆRMEN SLUKKER · RØR FOR AT VISE` |

**DIAGNOSTICS (labels, values, full-width lines, ABOUT block)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_DL_LINK` | - | `LINK` | `LINK` |
| `TX_DL_PHASE` | - | `PHASE` | `FASE` |
| `TX_DL_RXOK` | - | `RX OK` | `RX OK` |
| `TX_DL_RXCRC` | - | `RX CRC / RESYNC` | `RX CRC / RESYNC` |
| `TX_DL_WORKSTA` | - | `WORK_STA` | `WORK_STA` |
| `TX_DL_E123` | - | `E1 / E2 / E3` | `E1 / E2 / E3` |
| `TX_DL_CMD6` | - | `CMD6 OK·RTX·ERR·BLK` | `CMD6 OK·RTX·FEJL·BLOK` |
| `TX_DL_AWAKE` | - | `AWAKE (WORD 1)` | `VÅGEN (ORD 1)` |
| `TX_DL_MAINS` | - | `MAINS` | `NET` |
| `TX_DL_CURRENT` | - | `CURRENT 1 / 2` | `STRØM 1 / 2` |
| `TX_DL_FRAMEMS` | - | `FRAME MS NOW/MAX` | `FRAME MS NU/MAX` |
| `TX_DL_MIRROR` | - | `MIRROR 112-114` | `SPEJL 112-114` |
| `TX_DL_CFGMD5` | - | `CONFIG MD5` | `CONFIG MD5` |
| `TX_DL_VERSION` | - | `VERSION` | `VERSION` |
| `TX_DL_BP_A` | - | `BOOT PUSH 1-30` | `BOOT-PUSH 1-30` |
| `TX_DL_BP_B` | - | `BOOT PUSH 112-114` | `BOOT-PUSH 112-114` |
| `TX_DL_BP_A_RE` | - | `RE-PUSH %d · 1-30` | `RE-PUSH %d · 1-30` |
| `TX_DL_BP_B_RE` | - | `112-114` | `112-114` |
| `TX_DL_FLAG` | diag-label | FLAG `/customer/new` | FLAG `/customer/new` |
| `TX_DL_NEXTBOOT` | diag-label | `NEXT BOOT` | `NÆSTE BOOT` |
| `TX_DL_STARTED` | diag-label | `STARTED FROM` | `STARTET FRA` |
| `TX_DL_SLOTB` | diag-label | `SLOT B` | `SLOT B` |
| `TX_DL_SWITCHER` | diag-label | `SWITCHER DAEMON` | `SWITCHER-DAEMON` |
| `TX_DL_STOCK` | diag-label | `STOCK PROCESS` | `STOCK-PROCES` |
| `TX_DV_OK` | diag-value | `OK` | `OK` |
| `TX_DV_DOWN` | diag-value | `DOWN` | `NEDE` |
| `TX_DV_YES` | diag-value | `YES` | `JA` |
| `TX_DV_NO` | diag-value | `NO` | `NEJ` |
| `TX_DV_MAINS` | - | `%u V · %u HZ` | `%u V · %u HZ` |
| `TX_DV_VERSION` | - | `MERAKI+ V6.24` | `MERAKI+ V6.24` |
| `TX_DV_VERSION_RO` | - | `MERAKI+ V6.24 RO` | `MERAKI+ V6.24 RO` |

**Other**

| id | class | EN | DA |
|---|---|---|---|
| `TX_OTA_TRIAL` | - | `TRIAL` | `PRØVE` |
| `TX_OTA_FALLBACK` | - | `FALLBACK` | `RESERVE` |
| `TX_OTA_FAILED` | - | `%s FAILED` | `%s FEJLET` |
| `TX_OTA_VENDOR` | - | `VENDOR UPDATE · RUN INSTALL` | `PRODUCENT-OPD. · KØR INSTALL` |

**Guards and toasts**

| id | class | EN | DA |
|---|---|---|---|
| `TX_T_UPD_VENDOR` | toast | `VENDOR UPDATE FOUND · RUN INSTALL` | `PRODUCENT-OPD. · KØR INSTALL` |
| `TX_T_UPD_ROLLED` | toast | `%s FAILED · BACK ON %s` | `%s FEJLEDE · TILBAGE PÅ %s` |

**DIAGNOSTICS (labels, values, full-width lines, ABOUT block)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_DV_MISSING` | diag-value | `MISSING` | `MANGLER` |
| `TX_DV_FLAG_YES` | diag-value | `EXISTS` | `FINDES` |
| `TX_DV_FLAG_NO` | diag-value | `NOT PRESENT` | `FINDES IKKE` |
| `TX_DV_SLOTA` | diag-value | `SLOT A` | `SLOT A` |
| `TX_DV_SLOTB` | diag-value | `SLOT B` | `SLOT B` |
| `TX_DV_RUNSH` | diag-value | `RUN.SH` | `RUN.SH` |
| `TX_DV_UNKNOWN` | diag-value | `UNKNOWN` | `UKENDT` |
| `TX_DV_B_OK` | diag-value | `MERAKI+ FOUND` | `MERAKI+ FUNDET` |
| `TX_DV_B_NOMOUNT` | diag-value | `NOT MOUNTED` | `IKKE MOUNTET` |
| `TX_DV_B_NOBIN` | diag-value | `NO DEMO_V9` | `UDEN DEMO_V9` |
| `TX_DV_B_VENDOR` | diag-value | `NOT MERAKI+` | `IKKE MERAKI+` |
| `TX_DV_RUNNING` | diag-value | `RUNNING` | `KØRER` |
| `TX_DV_NOTRUNNING` | diag-value | `NOT RUNNING` | `KØRER IKKE` |
| `TX_DV_NONE` | diag-value | `NONE` | `INGEN` |
| `TX_DV_ALSO` | diag-value | `ALSO RUNNING!` | `KØRER SAMTIDIG!` |
| `TX_DV_PAUSED` | diag-value | `PAUSED` | `PAUSET` |
| `TX_DV_BP_OFF` | diag-value | `OFF` | `FRA` |
| `TX_DV_BP_WAIT` | diag-value | `WAITING` | `VENTER` |
| `TX_DV_BP_SENT` | diag-value | `SENDING` | `SENDER` |
| `TX_DV_BP_NOACK` | diag-value | `NO ACK` | `INGEN ACK` |
| `TX_DV_BP_BLOCKED` | diag-value | `REJECTED` | `AFVIST` |
| `TX_DV_BP_NOCFG` | diag-value | `NO CONFIG` | `INGEN CONFIG` |
| `TX_DV_BP_SKIP` | diag-value | `SKIPPED` | `SPRUNGET OVER` |
| `TX_DG_CAP` | full-line | `BOOT · SLOT · SWITCHER` | `BOOT · SLOT · SWITCHER` |
| `TX_DG_L_PID` | full-line | `PID · MERAKI+ / STOCK UI / SWITCHER` | `PID · MERAKI+ / STOCK-UI / SWITCHER` |
| `TX_DG_L_EXE` | full-line | RUNNING FROM · `/proc/self/exe` | KØRER FRA · `/proc/self/exe` |
| `TX_DG_L_EVT` | full-line | LAST SWITCH · `/tmp/switcher.log` | SENESTE SKIFT · `/tmp/switcher.log` |
| `TX_DG_L_NET` | full-line | `NETWORK · WPA PID / IP / STATE` | `NETVÆRK · WPA-PID / IP / TILSTAND` |
| `TX_NS_OFF` | - | `OFF` | `FRA` |
| `TX_NS_DOWN` | - | `DOWN` | `NEDE` |
| `TX_NS_STARTING` | - | `STARTING` | `STARTER` |
| `TX_NS_CONNECTING` | - | `CONNECTING` | `FORBINDER` |
| `TX_NS_UP` | - | `UP` | `OPPE` |
| `TX_DG_NOEXE` | full-line | `NOT READABLE` | `IKKE LÆSBAR` |
| `TX_DG_NOEVT` | full-line | `NO SWITCH EVENT IN THE LOG` | `INGEN SKIFT-HÆNDELSE I LOGGEN` |
| `TX_DG_NOLOG` | full-line | `LOG NOT READABLE` | `LOG IKKE LÆSBAR` |
| `TX_DG_L_GRAPH` | full-line | `GRAPH · TAP TO TOGGLE` | `GRAF · TRYK FOR AT SKIFTE` |
| `TX_DV_G_SMOOTH` | full-line | `SMOOTH · MEDIAN 3 + EMA` | `GLAT · MEDIAN 3 + EMA` |
| `TX_DV_G_RAW` | full-line | `RAW · UNFILTERED` | `RÅ · UFILTRERET` |
| `TX_DG_PCAP` | full-line | `PRESSURE · ESTIMATE` | `TRYK · ESTIMAT` |
| `TX_DG_L_PANCH` | full-line | `ANCHOR · PRE-INF. IN RECORDING` | `ANKER · PRÆ-INF. I OPTAGELSEN` |
| `TX_DV_PANCH` | - | %d BAR = RAW %d · `k` %.5f | %d BAR = RÅ %d · `k` %.5f |
| `TX_DG_L_PNOW` | full-line | `NOW · WORD 51 · PUMP SIDE, LINEAR` | `NU · ORD 51 · PUMPE-SIDE, LINEÆR` |
| `TX_DV_PNOW` | - | `RAW %d = ≈%.1f BAR` | `RÅ %d = ≈%.1f BAR` |
| `TX_DV_PPULSE` | - | `RAW %d · PULSE, NO PRESSURE` | `RÅ %d · PULS, IKKE TRYK` |
| `TX_DV_PMAINT` | - | `RAW %d · MAINTENANCE PULSE` | `RÅ %d · VEDLIGEHOLD-PULS` |
| `TX_DG_ABOUT` | full-line | `ABOUT` | `OM` |
| `TX_DG_RAWCAP` | full-line | `RAW COUNTERS · SERVICE` | `RÅ TÆLLERE · SERVICE` |
| `TX_DG_L_LANG` | full-line | UI LANGUAGE · `merakiplus/lang` | UI-SPROG · `merakiplus/lang` |
| `TX_DG_LANG_DEFAULT` | - | `%s · DEFAULT (NO FILE)` | `%s · STANDARD (INGEN FIL)` |
| `TX_DG_LANG_FILE` | - | `%s · FROM FILE` | `%s · FRA FIL` |
| `TX_DG_LANG_UNKNOWN` | - | `%s · UNKNOWN FILE VALUE` | `%s · UKENDT VÆRDI I FIL` |
| `TX_DG_LANG_SESSION` | - | `%s · NOT SAVED` | `%s · IKKE GEMT` |
| `TX_DG_PWCAP` | full-line | `POWER BUTTON` | `TÆND/SLUK-KNAP` |
| `TX_DG_L_PWNODE` | full-line | `BUTTON NODE · EVDEV · NO GRAB` | `KNAP-NODE · EVDEV · INTET GRAB` |
| `TX_DL_PW_STATE` | diag-label | `STATE` | `TILSTAND` |
| `TX_DL_PW_W0` | diag-label | `POWER · WORD 0` | `STRØM · ORD 0` |
| `TX_DL_PW_COUNT` | diag-label | `PRESS · OFF · ON` | `TRYK · SLUK · TÆND` |
| `TX_DL_PW_LAST` | diag-label | `LAST KEY` | `SIDSTE KNAP` |
| `TX_DV_PW_ON` | diag-value | `ON` | `TÆNDT` |
| `TX_DV_PW_OFFING` | diag-value | `TURNING OFF` | `SLUKKER` |
| `TX_DV_PW_OFF` | diag-value | `OFF` | `SLUKKET` |
| `TX_DV_PW_WAKING` | diag-value | `TURNING ON` | `TÆNDER` |
| `TX_DV_PW_NOREPLY` | diag-value | `NO REPLY` | `INTET SVAR` |
| `TX_DV_PW_KEY` | - | `%d = %d · %ld S` | `%d = %d · %ld S` |
| `TX_DV_PW_NOKEY` | diag-value | `NO PRESS YET` | `INGEN TRYK` |
| `TX_DV_PW_NONODE` | full-line | `NO BUTTON NODE FOUND` | `INGEN KNAP-NODE FUNDET` |

**Settings: row labels (SL), notes (SN) and hints (SH)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_SL_PREHEAT` | - | `PRE-HEAT` | `FORVARMNING` |
| `TX_SL_PHAT` | - | `PRE-HEAT AT` | `FORVARM KL.` |
| `TX_SL_PHREP` | - | `REPEAT` | `GENTAG` |
| `TX_SL_AUTOOFF` | - | `AUTO OFF` | `AUTO-SLUK` |
| `TX_SN_PREHEAT` | note | `THE MACHINE WAKES + HEATS ITSELF` | `MASKINEN VÅGNER + VARMER SELV` |
| `TX_SN_PHAT` | note | `LOCAL TIME` | `LOKAL TID` |
| `TX_SN_PHREP` | note | `ONCE = SWITCHES ITSELF OFF AFTER USE` | `EN GANG = SLÅR SIG SELV FRA BAGEFTER` |
| `TX_SN_AUTOOFF` | note | `NO TOUCH OR ACTIVITY · THEN OFF` | `INGEN BERØRING ELLER AKTIVITET · SÅ SLUK` |
| `TX_SN_TIMEFMT` | note | `HOME CLOCK + STOCK UI CLOCK` | `UR PÅ HJEM + STOCK-UI'ETS UR` |

**Enum names**

| id | class | EN | DA |
|---|---|---|---|
| `TX_E_ONCE` | - | `ONCE` | `EN GANG` |
| `TX_E_DAILY` | - | `EVERY DAY` | `HVER DAG` |
| `TX_E_WORKDAYS` | - | `MON-FRI` | `MAN-FRE` |
| `TX_E_WEEKEND` | - | `SAT-SUN` | `LØR-SØN` |
| `TX_E_MIN15` | - | `15 MIN` | `15 MIN` |
| `TX_E_MIN20` | - | `20 MIN` | `20 MIN` |
| `TX_E_MIN30` | - | `30 MIN` | `30 MIN` |
| `TX_E_MIN60` | - | `60 MIN` | `60 MIN` |

**Guards and toasts**

| id | class | EN | DA |
|---|---|---|---|
| `TX_T_OWN_SAVED` | toast | `SCHEDULE SAVED` | `TIDSPLAN GEMT` |
| `TX_T_OWN_NOFILE` | toast | `ACTIVE · FILE NOT SAVED` | `AKTIV · FIL IKKE GEMT` |

**Settings: row labels (SL), notes (SN) and hints (SH)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_SL_SOUND` | - | `SOUND` | `LYD` |
| `TX_SL_SNDRDY` | - | `READY TONE` | `KLAR-TONE` |
| `TX_SN_SOUND` | note | `START JINGLE + BOILER READY TONES` | `START-JINGLE + KLAR-TONER` |
| `TX_SN_SNDRDY` | note | `ITS OWN TONE PER BOILER · WHEN HOT` | `SIN EGEN TONE PR. KEDEL · NÅR VARM` |

**Enum names**

| id | class | EN | DA |
|---|---|---|---|
| `TX_E_SND_STOCK` | - | `STOCK SOUNDS` | `STOCK-LYDE` |
| `TX_E_RDY_BREW` | - | `BREW BOILER` | `BRYGKEDEL` |
| `TX_E_RDY_STEAM` | - | `STEAM BOILER` | `DAMPKEDEL` |
| `TX_E_RDY_BOTH` | - | `BOTH BOILERS` | `BEGGE KEDLER` |

**Guards and toasts**

| id | class | EN | DA |
|---|---|---|---|
| `TX_T_SND_SAVED` | toast | `SOUND SAVED` | `LYD GEMT` |

**DIAGNOSTICS (labels, values, full-width lines, ABOUT block)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_DG_SNDCAP` | full-line | `SOUND` | `LYD` |
| `TX_DL_SND` | diag-label | `SOUND · VOLUME` | `LYD · LYDSTYRKE` |
| `TX_DL_SND_LAST` | diag-label | `LAST SOUND` | `SIDSTE LYD` |

**Phases, error codes and activities**

| id | class | EN | DA |
|---|---|---|---|
| `TX_ER_E2_SCALE` | home-sub | `SCALE FAULT` | `VÆGTFEJL` |

**HOME (labels, sub-lines, hold hints, stats, chart, buttons)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_H_HOLD_WATER_MAN` | home-sub | `HOLD · HOT WATER · MANUAL` | `HOLD · VARMT VAND · MANUEL` |
| `TX_H_WATER_MAN` | home-sub | `MANUAL · RUNS UNTIL STOP` | `MANUEL · KØRER TIL STOP` |

**Phases, error codes and activities**

| id | class | EN | DA |
|---|---|---|---|
| `TX_A_WATER_MAN` | - | `MAN.` | `MAN.` |

**Other**

| id | class | EN | DA |
|---|---|---|---|
| `TX_W_MAN_T` | status | `MANUAL HOT WATER` | `MANUELT VARMT VAND` |
| `TX_W_MAN_RUN` | status | `RUNS UNTIL YOU TAP STOP` | `LØBER TIL DU TRYKKER STOP` |
| `TX_W_WHY_OLD` | status | `CONTROLLER < 5.1.3: NO WEIGHT STOP` | `STYRING < 5.1.3: INGEN VÆGT-STOP` |
| `TX_W_WHY_UNK` | status | `CONTROLLER VERSION UNKNOWN` | `STYRINGENS VERSION UKENDT` |
| `TX_W_WHY_FAULT` | status | `SCALE FAULT: NO WEIGHT STOP` | `VÆGTFEJL: INGEN VÆGT-STOP` |

**Guards and toasts**

| id | class | EN | DA |
|---|---|---|---|
| `TX_T_WATER_SCALE` | toast | `SCALE FAULT · WATER STOPPED` | `VÆGTFEJL · VAND STOPPET` |

**DIAGNOSTICS (labels, values, full-width lines, ABOUT block)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_DG_MCUCAP` | full-line | `CONTROLLER` | `STYRING` |
| `TX_DL_MCU_VER` | diag-label | `FIRMWARE` | `FIRMWARE` |
| `TX_DL_MCU_WD` | diag-label | `HOT WATER` | `VARMT VAND` |
| `TX_DV_MCU_NONE` | diag-label | `NOT REPORTED` | `IKKE MELDT` |
| `TX_DV_MCU_COPY` | diag-label | `COPY DIFFERS` | `KOPI AFVIGER` |
| `TX_DV_WD_AUTO` | diag-label | `AUTO + MANUAL` | `AUTO + MANUEL` |
| `TX_DV_WD_MAN` | diag-label | `MANUAL ONLY` | `KUN MANUEL` |
| `TX_DV_WD_FAULT` | diag-label | `MANUAL (SCALE)` | `MANUEL (VÆGT)` |
| `TX_DV_SND_ON` | diag-value | `ON · %d` | `TIL · %d` |
| `TX_DV_SND_STOCK` | diag-value | `STOCK · %d` | `STOCK · %d` |
| `TX_DV_SND_OFF` | diag-value | `OFF` | `FRA` |
| `TX_DV_SND_MUTE` | diag-value | `VOLUME 0` | `LYDSTYRKE 0` |
| `TX_DV_SND_NOHELPER` | diag-value | `NO PLAYER` | `INGEN AFSPILLER` |
| `TX_DV_SE_JINGLE` | diag-value | `JINGLE` | `JINGLE` |
| `TX_DV_SE_BREW` | diag-value | `BREW` | `BRYG` |
| `TX_DV_SE_STEAM` | diag-value | `STEAM` | `DAMP` |
| `TX_DV_SE_PREVIEW` | diag-value | `TEST` | `TEST` |
| `TX_DV_SO_PLAYED` | diag-value | `PLAYED` | `SPILLET` |
| `TX_DV_SO_MUTED` | diag-value | `MUTED` | `LYDLØS` |
| `TX_DV_SO_SKIP` | diag-value | `SKIPPED` | `UDELADT` |
| `TX_DV_SO_FAIL` | diag-value | `FAILED` | `FEJL` |
| `TX_DV_SO_DROP` | diag-value | `BUSY` | `OPTAGET` |

**Guards and toasts**

| id | class | EN | DA |
|---|---|---|---|
| `TX_T_AUTOOFF_SOON` | toast | `AUTO OFF IN 30 S · TOUCH TO STAY ON` | `AUTO-SLUK OM 30 S · RØR SKÆRMEN` |
| `TX_T_ONCE_USED` | toast | `ONE-TIME PRE-HEAT USED · NOW OFF` | `FORVARMNING BRUGT · NU FRA` |
| `TX_T_NOCLOCK` | toast | `CLOCK NOT SET · NO PRE-HEAT ALARM` | `UR IKKE SAT · INGEN FORVARM-ALARM` |

**Other**

| id | class | EN | DA |
|---|---|---|---|
| `TX_AM` | - | `A.M.` | `A.M.` |
| `TX_PM` | - | `P.M.` | `P.M.` |
| `TX_WD_SUN` | - | `SUN` | `SØN` |
| `TX_WD_MON` | - | `MON` | `MAN` |
| `TX_WD_TUE` | - | `TUE` | `TIR` |
| `TX_WD_WED` | - | `WED` | `ONS` |
| `TX_WD_THU` | - | `THU` | `TOR` |
| `TX_WD_FRI` | - | `FRI` | `FRE` |
| `TX_WD_SAT` | - | `SAT` | `LØR` |

**DIAGNOSTICS (labels, values, full-width lines, ABOUT block)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_DG_TCAP` | full-line | `TIME · SCHEDULE` | `TID · PLAN` |
| `TX_DL_T_CLOCK` | diag-label | `LOCAL TIME` | `LOKAL TID` |
| `TX_DL_T_ZONE` | diag-label | `ZONE · DST` | `ZONE · SOMMERTID` |
| `TX_DL_T_AUTOOFF` | diag-label | `AUTO OFF · IDLE` | `AUTO-SLUK · HVILE` |
| `TX_DL_T_ALARM` | diag-label | `ALARM SENT · D H:M` | `ALARM SENDT · D T:M` |
| `TX_DG_L_NEXT` | full-line | `NEXT PRE-HEAT · CONTROLLER ALARM` | `NÆSTE FORVARMNING · ALARM` |
| `TX_DV_T_NOTSET` | diag-value | `NOT SET` | `IKKE SAT` |
| `TX_DV_T_NONE` | diag-value | `NONE` | `INGEN` |
| `TX_DV_T_NOSCHED` | full-line | `NO PRE-HEAT PLANNED` | `INGEN FORVARMNING PLANLAGT` |
| `TX_DV_T_DST` | - | `%s · DST` | `%s · SOMMERTID` |
| `TX_DV_T_STD` | - | `%s · STANDARD` | `%s · NORMALTID` |
| `TX_DV_T_IDLE` | - | `%s · %d:%02d` | `%s · %d:%02d` |

**Console (stdout on exit; ASCII)**

| id | class | EN | DA |
|---|---|---|---|
| `TX_O_SWITCHED` | console | `== merakiplus: runtime switch B->A (user button): stock UI pid %d started from /customer/demo_v9; merakiplus exits\n` | `== merakiplus: runtime-skift B->A (bruger-knap): stock-UI pid %d startet fra /customer/demo_v9; merakiplus afslutter\n` |
| `TX_O_RESTART` | console | `== merakiplus: STOCK UI NEEDS A RESTART OF demo_v9 FOR NEW VALUES:   kill $(pidof demo_v9)\n==        (config.json changed by merakiplus; the watchdog restarts it, the boot push syncs the controller)\n` | `== merakiplus: STOCK-UI KRAEVER GENSTART AF demo_v9 FOR NYE VAERDIER:   kill $(pidof demo_v9)\n==        (config.json aendret af merakiplus; watchdog genstarter, boot-push synker controlleren)\n` |
| `TX_O_CFGCHANGED` | console | `== merakiplus: config.json changed %d time(s) - no paused stock UI is waiting; the next stock start reads it itself\n` | `== merakiplus: config.json aendret %d gang(e) - intet pauset stock-UI venter; naeste stock-start laeser den selv\n` |
| `TX_O_BLOCKED` | console | `== merakiplus: %ld cmd6 frame(s) rejected by the frame policy (see UI_v6.md)\n` | `== merakiplus: %ld cmd6-frame(s) afvist af frame-politikken (se UI_v6.md)\n` |

<!-- I18N-APPENDIX:END -->
