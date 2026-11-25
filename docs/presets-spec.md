# Presets — specification (planning, not implemented)

Status: **plan / spec for review**. Nothing in this file is implemented yet. Phasing and the NFC side are in
[nfc-plan.md](nfc-plan.md). Addresses are virtual addresses in `demo_v9` (build 1.1.1-69, symbols kept).

A *preset* is a named recipe set: grind, brew, steam and hot water values. You switch between presets from the UI.
Presets need no NFC hardware.

**How this file relates to the read-first NFC plan** ([nfc-plan.md §8](nfc-plan.md#8-phasing)):

| nfc-plan phase | Uses from this file |
|---|---|
| 1a — overlay engine | §3.2, §4.2–§4.5, §5: the layers, the own file, boot push / re-push / assert. Built first, because both presets and tags stand on it |
| 1b–1d — CoffeeSense read parity | §4.6: a tapped CoffeeSense tag becomes a *tag recipe* layer in the same overlay. It is not a preset and does not use a preset slot |
| 2 — presets UI | §4.1, §6, §8: the list, the preset sheet, stock import, SAVE AS PRESET from a bean card |
| 3 — optional | §7 (Meraki+'s own NDEF card format) and tag links. Demoted: reading the tags people already own comes first |

## 1. Goals and non-goals

| | |
|---|---|
| **Goal** | Up to 8 named presets. Apply one with one tap. HOME shows which preset is active. The active preset survives a reboot and a controller restart. Works with no NFC hardware at all. |
| **Goal** | `config.json` stays stock-owned. **Applying a preset never writes `config.json`**, and neither does editing a value while a preset is active. The same holds for a tag recipe (§4.6). |
| **Goal** | Every controller write goes through the existing single-word save path and frame policy (`setting_mcu_push`, `word_ok`, `frame_policy`). No new frame shapes. |
| **Goal** | EN/DA strings in `STR_TABLE`. The selftest covers every path, plus a sim scenario set. |
| Non-goal | Machine-level settings in presets (head temp, break time, backlight, volume, sound, auto off, schedule). A preset is a drink recipe, not a machine profile. |
| Non-goal | Writing into stock's own `presetStore` (in `config.json`). Meraki+ only *imports* stock presets, read-only (§6). |
| Non-goal | Making the stock UI show Meraki+ presets. Stock keeps showing the base values from `config.json` (§4.5). |

## 2. Stock already has presets (reversed)

The stock UI in this build has its own preset system. On the reference machine `config.json` has `"presetStore": {}`,
so the feature exists but nobody has saved a preset yet.

| Item | Stock behaviour | Address |
|---|---|---|
| Storage | Inside `/customer/config.json`: `"presetStore": {"currentIndex": n, "items": [ … ]}` plus a top-level `"presetAppliedIndex"` (255 = none) | `getPresetStore` 0xD131C, `setPresetStore` 0xD16D8, `setPresetAppliedIndex` 0xD1A10 |
| Count | At most 8, clamped on load, on save and when the state is checked | 0xD146C, 0xD1708, `presetStoreValidateState` 0xD1244 |
| Item | `name` (20 UTF-8 characters, 81-byte buffer), `iconId` (0..7: house, sun, moon, star, heart, pac-man, music, grass), `grind`, `dose`, `brewTemp`, `yield`, `steamLevel`, `milkTemp` | `pagePresetNameLimit` 0x127238, item stride 0x5E |
| Defaults for missing fields | grind 12, dose 180, brewTemp 920, yield 360, steamLevel 4, milkTemp 550 | 0xD1528–0xD15FC |
| Capture | dose = word 4, brewTemp = **word 11 + 50**, yield = word 23, steamLevel = word 14, milkTemp = word 25, grind = grind-dial value (0..40). The dial value is advisory only and is never sent to the controller. | `pagePresetStoreCaptureCurrent` 0x127748 |
| Apply | Writes the 5 values into the register mirror, then calls the config setters (brewTemp − 50). Sends **one block frame, cmd6 sub 4 len 22 (words 4..25)**. Sets the applied index, then `data_save`. Toast: "Preset applied. Adjust grind dial to %s." | `pagePresetStoreApply` 0x127C84, frame at 0x127E0C |
| Clear | Changing dose, brew temp, yield, steam level or milk temp clears the applied index (255), with the toast "Coffee settings changed. Preset cleared." A grind change does not clear it. | `presetAppliedSettingsChanged` 0x11119C, `presetAppliedClearBySettingsChange` 0x111458 |

What Meraki+ keeps from stock:
* 8 slots
* the 8 icon ids
* a grind-dial *recommendation* that is never sent
* "applied" state shown on HOME

What it changes:
* **No block frame.** Stock's sub 4 len 22 also carries the start words 6/9/16/22/24 from the mirror. Meraki+ pushes only the
  words that changed, one at a time, through the frame policy.
* **No `config.json` write** (§4).
* **More fields:** grind mode, pre-infusion, hot water.
* **Editing does not throw the preset away.** An edit becomes a *tweak* that you can save into the preset or discard (§4.3).

> **Brew temperature offset (open question Q-P1).** Stock stores and shows preset/NFC brew temperatures as
> **word 11 + 5.0 °C**: preset capture adds 50, apply and the NFC path subtract 50. Meraki+ shows `brewTemper` / word 11
> unchanged, which is the documented mapping (docs/dev/UI_v6.md §4). Meraki+ presets and the tag recipe store the
> **raw config value**, the same unit as the `SET` row. Importing a stock preset (§6) or applying a CoffeeSense tag
> ([nfc-plan.md §5.4](nfc-plan.md#54-validate-and-normalize)) subtracts 50. The parity table lists stock's own
> brew-temperature page as showing the raw value (870 = 87.0 °C), which would mean a tag that says 92 shows as 87.0 on
> both UIs; one tap test under stock is still wanted to confirm it (nfc-plan §12, R3). If stock's page turns out to add
> 5.0 °C, the BREW row's label/hint is a separate fix and does not belong to the preset or tag work.

## 3. Data model

### 3.1 Fields

A preset contains whole **groups**. A group is either fully in the preset or not at all. A preset that only covers
GRIND + BREW (a bean preset) leaves the steam and water values alone. Values use **config units**, the same min, max
and step as the `SET` table row (`src/merakiplus.c` `SET[]`). A value that is outside the range or off-step is dropped
on load. It is not clamped.

| Group | Key | Setting (`SET` row) | Controller word | Range / step (config units) | Shown as |
|---|---|---|---|---|---|
| — | `n` | name | — | 1..12 glyphs from the UI font (A–Z, ÆØÅ, 0–9, space, `-`, `.`, `/`, `·`) | `ESPRESSO` |
| — | `i` | icon | — | 0..7 (stock icon ids) | |
| — | `id` | stable id | — | 8 hex digits, random at creation, unique in the file | |
| GRIND | `g` | grind-dial recommendation | **none (advisory)** | 0..40 | "GRINDER 12" in the apply toast |
| GRIND | `gm` | `grindMode` (S_GRINDMODE) | 2 | 1..2 | WEIGHT / TIME |
| GRIND | `d` | `autoGrindWeight` (S_GRINDW) | 4 | 10..309 / 1 | 18.0 G |
| BREW | `bt` | `brewTemper` (S_BREWT) | 11 | 850..960 / 10 | 93.0 ° |
| BREW | `bw` | `autobrewWeight` (S_BREWW) | 23 | 100..990 / 10 | 36 G |
| BREW | `ps` | `preInfusionSwitch` (S_PISW) | 112 hi | 0..1 | |
| BREW | `pt` | `preInfusionTime` (S_PITIME) | 113 hi | 5..15 (estimated limit, as SET) | |
| BREW | `pp` | `preInfusionPressure` (S_PIPRESS) | 112 lo | 3..4 | |
| STEAM | `mt` | `AtuoSteamTarge` (S_STEAMT) | 25 | 450..700 / 10 | 60.0 ° |
| STEAM | `sg` | `steamGears` (S_STEAMG) | 14 | 1..4 | |
| WATER | `hw` | `autoHotWaterTarget` (S_HOTW) | 17 | 300..5000 / 100 | 150 G |
| WATER | `hm` | `hotWaterMode` (S_HOTMODE) | — (config only; Meraki+ uses it in the UI) | 1..2 | |

> **v6.22 compatibility ([water-dosing-compat.md §10–§11](water-dosing-compat.md#11-implementation-v622)):** the WATER group
> is stored and applied to `config.json` on every machine (they are config values), but on a controller that cannot dose
> (≤ 5.1.2, version unknown, scale fault) the WATER page hides both rows and every start is manual; the apply toast should
> then add that water is manual. A preset never changes the gate.


`breakTime` (locked), `headTemper` (machine setting) and every SYSTEM row are deliberately not preset fields.
Words 112/113 are packed pairs. Apply uses the existing `pack112`/`pack113`, so a preset that sets the pre-infusion switch
never changes `breakTime` (113 lo).

### 3.2 Effective settings

```
base       = config.json (stock-owned; changed only by a SAVE while NO preset and NO tag recipe is active — today's path)
active     = preset P or none
tag        = tag recipe T or none: at most the three fields d, bt, bw from a CoffeeSense tag (§4.6)
tweaks     = per-field edits made while P or T is active (own file, §5)

effective(f) = tweaks[f]           if present
             = T[f]                if T is active and holds f
             = P[f]                if f's group is in P
             = base[f]             otherwise
```

Everything Meraki+ *shows* and *pushes* is `effective`. That covers the settings rows, HOME stats, the boot push, the
re-push after a controller restart, and the start checks. `base` is what the stock UI shows.

## 4. Behaviour

### 4.1 Apply (from the list, from a linked tag, or on boot)

1. **Guard:** the same conditions as `save_begin`. The link must be up, no activity may be running (grind/brew/steam/water), and the write queue must be idle.
   If the guard fails, nothing happens and a toast reads `BUSY — TRY AGAIN AFTER THE SHOT`. NFC taps that hit the guard are dropped. They are not queued.
2. Compute `target = effective` with P active, no tweaks and no tag recipe. Diff it against the controller's current
   words (the register mirror from the reads). Fields outside P's groups take the base value, because applying clears
   the tweaks and the tag recipe.
3. Push each differing word with `setting_mcu_push` (a single-word cmd6, waiting for the ACK) through `word_ok`/`frame_policy`.
   Start words are never part of a preset write. Words 26/27/30 stay compile-time forbidden.
4. **All ACKed:** set `active = P.id`, clear `tweaks` and the `[tag]` layer, write the presets file atomically (§5). Toast:
   `ESPRESSO APPLIED · GRINDER 12` (the grinder part only if `g` is present). If the file write fails, keep the state for
   this session (like `SCS_SESSION`) and show `APPLIED (NOT SAVED)`.
5. **Any NACK or timeout:** roll the words already pushed back to the previous effective values (the same rollback as
   `save_step`). The active preset stays as it was. Toast: `PRESET NOT APPLIED`.

At most 10 words are pushed. An apply takes well under one second (ACK ≈ 25 ms per word).

### 4.2 Base settings ("no preset")

Choosing **BASE SETTINGS** in the list sets `active = none`, clears `tweaks` and the tag recipe, and pushes `base` for
the words that differ. `config.json` is not touched.

### 4.3 Editing while a preset is active

The normal editor rows (GRIND/BREW/WATER/STEAM pages) keep their UI. Only the SAVE target changes:

| State | SAVE writes to | Controller |
|---|---|---|
| No preset and no tag recipe active | `config.json` (`cfg_set_int`, today's path) | single-word push (today) |
| Preset P and/or tag recipe T active | `tweaks[f]` in the presets file. **`config.json` is not touched.** | single-word push (same) |

If tweaks exist, the HOME badge shows a dot (`ESPRESSO •`). The preset sheet then offers **UPDATE PRESET** (move the
tweaks into P) and **DISCARD CHANGES** (re-apply P). Stock clears the preset on any edit instead. That was rejected here:
the other preset-applied fields would silently fall back to `base`, so the values on the screen would jump.

Editing a field whose group is *not* in P counts as a tweak too. It is not saved into `config.json`. The rule is simple:
while a preset or a tag recipe is active, nothing writes `config.json`.

### 4.4 Boot, controller restart, run.sh start

* `settings_load` reads `config.json` (base), then `presets_load` (the active preset and its tweaks).
* `boot_push_words` (slot B boot) and the re-push after a controller restart (`repush_step`) take their values from
  `effective`. They no longer read `sv()` directly. Today both read config only, so without this change the controller
  falls back to the base recipe after every reboot or controller restart.
* **run.sh mode** (stock ran first and pushed base): after start, once idle, Meraki+ runs an **assert step**. It diffs
  the controller's words against `effective` and pushes the difference (§4.1 steps 2–5, without changing the file).
  The selftest covers the "no preset active → assert pushes nothing" case.

### 4.5 Switching to the stock UI

Stock keeps base values in its RAM mirror and in `config.json`, and its next block write (for example its own preset
apply, sub 4 len 22) pushes them. To keep the screen and the controller in agreement, **SWITCH TO ORIGINAL pushes `base`
for the preset words before handing over** (when a preset is active and differs). After the switch back, the assert
step (§4.4) re-applies the active preset. The SYSTEM row's hint says so: `STOCK UI USES BASE SETTINGS`.

### 4.6 Tag recipe (a tapped CoffeeSense tag)

A CoffeeSense tag carries a dose, a brew temperature and a yield, plus a grind recommendation and a bean description.
Stock writes the three values into `config.json`. Meraki+ keeps them in a **tag recipe** layer `T` instead (§3.2). The
reader, the decode and the bean card are specified in [nfc-plan.md §5](nfc-plan.md#5-coffeesense-read-path--design);
this section fixes how the layer behaves.

| Event | Effect |
|---|---|
| Tag tapped (and decoded, values in range) | Guard as §4.1 step 1. `T` = the valid ones of `d`, `bt`, `bw`; tweaks on those same fields are dropped; the active preset and all other tweaks stay. Push the differing words (at most 4, 11, 23), single-word, with rollback as §4.1 step 5. Then one atomic file write (`[tag]` block, §5) |
| A tag value outside its `SET` range | That field is left out of `T` (not clamped); the others apply; the bean card shows it as not set |
| Edit of dose, temperature or yield while `T` is active | A tweak (§4.3). The tag recipe stays underneath |
| CLEAR TAG RECIPE (bean sheet) | `T` = none; tweaks on its three fields are dropped; the values underneath (preset or base) are pushed |
| Apply a preset, or BASE SETTINGS | Clears `T` (§4.1, §4.2) |
| SAVE AS PRESET (bean sheet, phase 2) | A new preset with the GRIND and BREW groups captured from `effective`, `g` from the tag's grind value when it is 0..40, and the bean-card fields kept with it. The new preset becomes active and `T` is cleared |
| Boot, controller restart, run.sh start, SWITCH TO ORIGINAL | §4.4 and §4.5 unchanged: they work on `effective`, which includes `T` |
| The same tag still resting on the reader | Nothing. A tag is applied once per arrival (nfc-plan §5.7) |

`T` is not a preset: it uses none of the 8 slots, has no groups and no id, and there is at most one. A tag never writes
`config.json`, never sends a start word, and can only set values the editor rows could set.

## 5. File format — `/new_customer/merakiplus/presets`

This file is Meraki+'s own. It follows the same pattern as `schedule`/`sound`: text, one `key=value` per line, unknown lines
ignored, a bad value dropped. It is written atomically (tmp + fsync + re-read/compare + rename + fsync of the directory,
the same as `sched_save`), and has a 4 KiB size limit.

```
# merakiplus presets
v=1
active=7f3a91c2
imported_stock=1
[preset]
id=7f3a91c2
n=ESPRESSO
i=3
groups=grind,brew,steam
g=12
gm=1
d=180
bt=930
bw=360
ps=1
pt=8
pp=3
mt=600
sg=4
[preset]
id=0c41d2e9
n=V60 BEANS
i=1
groups=grind,brew
d=150
bt=900
bw=500
[tag]
src=9c1f02ab
d=188
bw=360
c.gs=12
c.pw=18.8
c.bt=84
c.bw=36
c.n1=Example Coffee
c.n2=Example Roaster
c.o1=Colombia
c.f1=Cocoa
c.el=1200
c.pr=3
c.ct=1
c.rl=3
c.bgn=1.1.1
[tweaks]
bt=920
```

Rules:
* At most 8 `[preset]` blocks. Extra blocks are ignored. A block without a valid `id` or `n` is dropped.
* `groups` is the list of groups in the preset. A key whose group is not listed is ignored.
* `active` must match an `id`, otherwise the state is "none". `[tweaks]` is ignored when there is neither a matching
  `active` nor a `[tag]` block.
* At most one `[tag]` block (§4.6). `d`, `bt`, `bw` are the layer, in config units and validated like preset values; a
  missing one means the tag's value was not usable (in the example the tag's 84 °C maps below the BREW TEMP range, so
  there is no `bt` line). `src` identifies the tag payload it came from (nfc-plan §5.7). The `c.` lines are the bean card
  exactly as the tag gave it, after sanitizing: single-line text of at most 64 bytes each, used for display only.
* The list is shown in file order. "Move up/down" is a later option; no ordering field is needed.
* The same short keys are used on Meraki+'s own NDEF cards (§7). One parser serves both.
* The optional tag-link map lives in a **separate** file, `/new_customer/merakiplus/nfc`
  ([nfc-plan.md §8, phase 3a](nfc-plan.md#8-phasing)). That way linking never
  rewrites the preset file and the two cannot corrupt each other.
* Install/revert: the installer creates nothing; the file appears on the first save. `revert` removes it together with the
  other own files (the same list as `schedule`/`sound`). Its absence means "no presets, base active".

## 6. Importing stock presets (phase 2b)

If `config.json` has `presetStore.items` (read-only parse) and `imported_stock` is not set, the PRESETS list shows the row
`IMPORT 3 STOCK PRESETS`. The mapping:

| Stock | Meraki+ |
|---|---|
| `name` | `n`. Uppercased; glyphs the font lacks are dropped; cut to 12; empty → `PRESET n` |
| `iconId & 7` | `i` |
| `grind` | `g` |
| `dose` | `d` (GRIND group) |
| `brewTemp` | `bt = brewTemp − 50` (see Q-P1) (BREW group) |
| `yield` | `bw` (BREW group) |
| `steamLevel`, `milkTemp` | `sg`, `mt` (STEAM group) |

Out-of-range values drop the field, as on load. The import adds presets up to the 8-slot limit and sets `imported_stock=1`.
Stock's `presetStore` is never written. Stock's `presetAppliedIndex` is ignored.

## 7. NDEF tag format (optional phase 3b; the reader side is in nfc-plan.md)

This is Meraki+'s **own** card format for writing and sharing recipes. It is secondary: reading the CoffeeSense tags
people already own (§4.6, nfc-plan §5) comes first and does not depend on anything in this section.

Tags carry **no secrets**. Anyone can read them with a phone, and a UID can be cloned. A tag only *selects* or *proposes* a
recipe. It can never start the machine, and every value passes the same range check and frame policy as a manual SAVE.

**Target tags:** NFC Forum Type 2, i.e. NTAG213 (144 B user area), NTAG215 (504 B) or NTAG216 (888 B). Stock's vendor cards
are MIFARE Classic 1K with an encrypted vendor payload. Meraki+ reads those read-only (phase 1) and **never writes
MIFARE Classic**, so a vendor card cannot be damaged.

**NDEF message, in order:**

1. **URI record**, optional and written by default: `https://github.com/thomasgrastein/merakitech-plus`. A phone that taps
   the card opens the project page instead of showing "unsupported tag". Android dispatches on the first record, so the
   URI goes first.
2. **MIME record**, type `application/x.merakiplus.recipe` (the RFC 6838 `x.` unregistered tree), payload UTF-8 text:

```
v=1
id=7f3a91c2
n=ESPRESSO
i=3
groups=grind,brew
g=12
d=180
bt=930
bw=360
ps=1
pt=8
pp=3
```

   Optional *bean card* keys, shown on the card view and stored with the preset when saved:

| Key | Meaning | Limit |
|---|---|---|
| `bn` | bean / coffee name | 24 chars |
| `by` | roaster | 24 chars |
| `or` | origin | 24 chars |
| `pr` | process (washed, natural, …; free text) | 16 chars |
| `rl` | roast level 1..5 (light..dark) | |
| `al` | altitude in m | 0..4000 |
| `fl` | flavour notes, comma-separated, at most 3 | 16 chars each |
| `rd` | roast date `YYYY-MM-DD` (HOME can show "day 12 since roast", phase 4) | |

  These bean keys are Meraki+'s own. A bean card that came from a CoffeeSense tag keeps the stock field names with the
  `c.` prefix (§5); writing such a preset to an NDEF card maps `c.n1`→`bn`, `c.n2`→`by`, `c.o1`→`or`, `c.el`→`al`,
  `c.f1..f3`→`fl`. Stock's roast scale has 7 steps and its process is a code, so `rl` and `pr` are written as text labels.

* Recipe only ≈ 70–90 B, which fits NTAG213. With bean keys ≈ 160–220 B, NTAG215 is recommended.
* Unknown keys are ignored. `v` > 1 shows `NEWER CARD FORMAT` and applies nothing.
* `id` links the card to the preset it was written from. On another machine no preset has that `id`, so the tap shows the card
  with **SAVE AS PRESET**, which keeps the `id` so that later taps go straight to it.
* Stock's tag field names are reused where the meaning matches (`pr`, `rl`), but the format is not stock's (base64 +
  AES-CBC JSON on MIFARE Classic). Stock reading a Meraki+ tag gets "parse fail" and does nothing; that path was verified
  statically.
* A future signature (anti-cloning for "trusted" cards, for example a guest lock) would be an extra `sig=` line: an HMAC
  over the other lines with a per-machine key stored only on the display. It is not needed while tags only select recipes.

## 8. UI

The Meraki+ IA v6.17 conventions apply: HOME root, sheets that close with the chevron or a swipe right/down, 50 px rows, no
long-press.

### 8.1 HOME

* `W_LABEL` line (y≈110, idle): when a preset is active it shows a **pill badge** with icon + name (+ `•` when tweaked), the
  same style as the steam badge. When no preset is active but presets exist, it shows a dim `PRESETS` pill. When none exist,
  nothing. During an activity the line keeps showing the phase as today, so the badge is hidden.
* When a tag recipe is active (§4.6) the pill shows the bean's first name line instead (+ `•` when tweaked) and opens the
  bean sheet (nfc-plan §5.6); the active preset, if any, is named on that sheet.
* Otherwise tapping the pill opens the PRESETS sheet.
* The width budget of the badge text (12 glyphs) is enforced by the selftest's pixel-budget class for the label line.

### 8.2 PRESETS sheet (also SYSTEM → PRESETS)

```
  ‹  PRESETS
  ○  BASE SETTINGS              (config.json)
  ●  ★ ESPRESSO      18.0→36 G · 93.0°   ›
  ○  ☀ V60 BEANS     15.0→50 G · 90.0°   ›
  +  SAVE CURRENT AS PRESET
     IMPORT 3 STOCK PRESETS               (phase 2b, only if available)
```

* Tapping a row **applies** it (§4.1). The `●` marks the active preset. Tapping the `›` zone (right 64 px) opens that preset's sheet.
* `SAVE CURRENT AS PRESET` captures `effective` with all four groups, gives it the next free `PRESET n` name and the next unused icon,
  and opens its sheet. The row is disabled at 8 presets (`8 OF 8 — DELETE ONE FIRST`).

### 8.3 Preset sheet

| Row | Action |
|---|---|
| APPLY | §4.1 (hidden when already active without tweaks) |
| UPDATE FROM CURRENT | Copies `effective` into the preset's groups (with tweaks: "UPDATE PRESET") |
| DISCARD CHANGES | Only when active with tweaks: re-apply |
| GROUPS | Toggle GRIND / BREW / STEAM / WATER (at least one) |
| ICON | Cycles 0..7 |
| NAME | Phase 2a: `PRESET n` / imported / from a bean card. Phase 2b: a character roller editor (font charset, 12 glyphs) |
| GRINDER | Advisory dial value 0..40 or OFF |
| BEAN CARD | Only for a preset saved from a CoffeeSense tag: shows its card (nfc-plan §5.6) |
| LINK NFC TAG | Optional phase 3a (nfc-plan.md §8) |
| WRITE TO TAG | Optional phase 3b (NDEF, §7) |
| DELETE | Two-step confirm (existing pattern). Deleting the active preset → BASE (pushes base) |

### 8.4 Strings (EN / DA), new `STR_TABLE` entries (draft)

| ID | EN | DA |
|---|---|---|
| TX_PR_TITLE | PRESETS | FORVALG |
| TX_PR_BASE | BASE SETTINGS | GRUNDINDSTILLINGER |
| TX_PR_SAVE_NEW | SAVE CURRENT AS PRESET | GEM NUVÆRENDE SOM FORVALG |
| TX_PR_FULL | 8 OF 8 — DELETE ONE FIRST | 8 AF 8 — SLET ET FØRST |
| TX_PR_APPLIED | %s APPLIED | %s VALGT |
| TX_PR_GRINDER | GRINDER %d | KVÆRN %d |
| TX_PR_NOTSAVED | APPLIED (NOT SAVED) | VALGT (IKKE GEMT) |
| TX_PR_FAIL | PRESET NOT APPLIED | FORVALG IKKE VALGT |
| TX_PR_BUSY | BUSY — TRY AGAIN AFTER THE SHOT | OPTAGET — PRØV EFTER SHOTTET |
| TX_PR_UPDATE | UPDATE PRESET | OPDATER FORVALG |
| TX_PR_DISCARD | DISCARD CHANGES | KASSER ÆNDRINGER |
| TX_PR_GROUPS | GROUPS | GRUPPER |
| TX_PR_IMPORT | IMPORT %d STOCK PRESETS | IMPORTER %d STOCK-FORVALG |
| TX_PR_STOCKNOTE | STOCK UI USES BASE SETTINGS | STOCK-UI BRUGER GRUNDINDSTILLINGER |
| TX_PR_DEFNAME | PRESET %d | FORVALG %d |

The selftest checks for both languages, matching printf conversions, glyph coverage and pixel budget, as for every string.

## 9. Code touch points (`src/merakiplus.c`)

* New: `struct preset`, `pr_load`/`pr_save` (the `sched_*` pattern), `pr_effective(i)`, `pr_apply(id)`, `pr_assert()`,
  `pr_capture()`, `pr_import_stock()`; for the tag layer `tag_apply()`, `tag_clear()` (same guard, push and rollback as
  `pr_apply`).
* `sv()` callers that feed the controller or the display read `pr_effective()` instead: `boot_push_words`, `repush_step`,
  HOME stats, start checks.
* `save_begin`/`save_step` branch: config path vs tweak path (§4.3).
* Views: PRESETS list and preset sheet (V_DETAIL-style sheet), HOME badge, SYSTEM row, SWITCH TO ORIGINAL base push (§4.5).
* SIM: the presets path is overridable like the other own files. The ctrl socket gets `{"cmd":"preset","name":"apply","id":…}`
  and `list`, and the state JSON gets `presets:{active,tweaked,n}` (an additive `sim/CONTRACT.md` addendum).

## 10. Tests

Selftest section `st_presets` (v6.20):
* Parser: valid file, garbage lines, more than 8 blocks, an off-step value, an unknown group, a duplicate `id`, a missing
  `active`, a 4 KiB overflow. Round trip: save → load → byte-identical.
* Effective: base only, preset with a partial group set, tweak precedence, tag layer over a preset and over base, tweak
  over a tag value.
* Tag recipe (§4.6): apply pushes only the differing ones of words 4/11/23 as single-word frames; an out-of-range field is
  left out; a tweak on a tag field is dropped by a new tap and kept otherwise; clear pushes the values underneath; a preset
  apply and BASE clear the layer; NACK rolls back and leaves the file unchanged; the `[tag]` block round-trips, and hostile
  `c.` text (control characters, `[preset]`, over-long lines) cannot inject a block or a key.
* Apply: the diff pushes only the changed words; packed 112/113 keep `breakTime`; a NACK on word k rolls back words < k and
  leaves the file unchanged; busy guard; frame-policy assertion (no start word, never 26/27/30).
* **config.json invariant:** md5 is unchanged after apply / tweak / base / delete / import / tag apply / tag clear (the
  main guarantee).
* Boot push and controller-restart re-push use effective values; the run.sh assert pushes nothing when the controller already matches.
* SWITCH TO ORIGINAL pushes base when it differs.
* Import: stock fixture with 3 items (one with an out-of-range value, one with non-font glyphs).
* Strings: EN/DA presence, conversions, glyph, budget.

Sim scenarios (`sim/scenarios/`, additive): `preset_apply`, `preset_apply_busy`, `preset_apply_nack_rollback`,
`preset_tweak_update`, `preset_reboot_reassert`, `preset_switch_to_stock`, `preset_import_stock`. Each one checks the
controller words on the mcu_sim side and the unchanged `config.json`.

Preview scenarios: HOME with badge / tweaked / none, PRESETS list (0, 2, 8 presets, EN+DA), preset sheet, toast frames.
Circle-clip check as usual.

## 11. Open questions

* **Q-P1** Brew temp +5.0 °C (§2 box). Does stock's main brew page show word 11 + 5? The parity table says it shows the raw
  value; a tap test under stock should confirm it (nfc-plan §12, R3). It affects only labels: the arithmetic for import and
  for tags (subtract 50) is proven from the code.
* **Q-P2** Naming: is a character roller acceptable on the 480 px round screen, or are names only set via import, tag and the
  installer tool (`tools/merakiplus.py presets edit`, a later option)?
* **Q-P3** Should a preset optionally include `headTemper`? It is excluded for now (machine setting).
* **Q-P4** Stock's grind dial range 0..40: is that the same scale as the physical dial on Gen1 and Gen2? And how does it
  relate to a CoffeeSense tag's `gs`, which generators allow up to 100 (nfc-plan §12, R5)? Until that is known a tag's
  grind value is shown as given and copied into a preset's `g` only when it is 0..40.
