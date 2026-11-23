# PARITY — complete feature parity specification for merakiplus (MerakiTech Gen 1, 480×480)

> **Note (public edition):** This document consists of development notes from reverse engineering a MerakiTech Gen 1 owned by the author. The manufacturer's firmware, binaries and scripts referred to (e.g. `demo_v9`, `MEM03_V514_260923.bin`, `demo.sh`, `test.sh`) are **not** included in the repository. Addresses and short excerpts are included solely for interoperability. IP addresses and other personal data have been removed. See [README → Disclaimer](../../README.md#️-disclaimer--read-this-before-you-do-anything).

**Date:** 2026-11-17 · **Method:** purely STATIC reverse engineering of `artifacts/demo_v9` (display app, ARMv7 ELF, not stripped, debug_info, MD5 `d44c2414…`) + `artifacts/MEM03_V514_260923.bin` (powerMcu 5.1.4). **No contact with the device** (offline). All addresses can be verified in `demo_v9.dis` / the MCU binary.
**Prerequisites:** [protocol.md](../protocol.md) (frame/CRC), [registermap.md](../registermap.md) (129-word map), `BREW.md`/`HOTWATER.md`/`SESSION.md` (live-verified writes), `WRITETEST.md` (config persistence) — the latter are the author's capture notes.
**Machine-readable counterpart:** `parity_spec.json` (in this repo: `sim/fixtures/parity_spec.json`). **Analysis tools:** `parity_analysis.py` (resolves all `usart_send` calls, config accessors and mirror writes), `sdump.py` (annotated disassembly). Intermediate lists: `parity_accessors.json`, `parity_calls.json`, `parity_funcs.json`.

---

## 0. TL;DR — the most important parity facts

1. **Two parallel truths:** the original UI keeps (a) a **config.json** (persistent, cJSON) and (b) a **register mirror** in RAM (`machine_struct+0xFE8`, 129 words) that is synced to the MCU via `cmd 6`. A parameter often has to be written **in both places**: `setXxx()` → config.json **and** mirror word → `usart_send`. merakiplus must do the same for full parity.
2. **`setNumber` writes config.json IMMEDIATELY** (not only on exit): `setNumber`/`setString` → `cJSON_ReplaceItemInObject` → `configFileClose` → `fseek(0)/ftruncate/fwrite/fflush/fsync` ([0xd0fa4](#), [0xd0c40](#)). Every `setXxx` accessor calls `setNumber`. So every UI change is persisted immediately.
3. **CORRECTION to WRITETEST:** there **IS** a parameter push to the MCU at boot/restart. At startup `fuart_thread` sends `cmd6 sub1 len30` (words 1..30, [0x14685c](#)) and shortly after `cmd6 sub112 len3` (words 112..114, [0x146b0c](#)) from the mirror (which `model_data_init` has filled from the config). WRITETEST's "no config push at boot" is wrong — it only looked at `app_init`/`PowerOn`. **A kill + watchdog restart therefore syncs new config values to the MCU automatically.**
4. **Hot water has NO temperature setting.** The displayed "hot water temperature" is **the e-boiler's measured temperature** (word 40, sensor, read-only) — `hotWaterTemperUpdate` [0x112c9c](#) reads word 40. There is no hot water temperature setpoint in either the config or the write blocks. (Answers the task's "find the hot water TEMP": it is **fixed = boiler temperature**.)
5. **`brewMode` is vestigial:** `getExtractModeValue`/`setExtractModeValue` (config `brewMode`) have **0 callers** in the entire binary. The field exists in the config but is not used by the UI.
6. **`sleepMode`/`sleepModeSetting` cannot be edited in the original UI:** `setSleepModeState`/`setSleepModeValue` have **0 callers**. The fields are only read at boot (`model_data_init` → model+0x54/0x55); the sleep timing is **hard-coded** (screensaver after ~600 s idle, power-off after ~1800 s idle — `recodeTime` [0x148164](#) sets the counter=1800, `SleepRefresh` [0x149268](#)). `sleepModeSetting` is only shown as "%dmin" in the settings menu.
7. **Words 112/113 are byte-packed pre-infusion fields** (not simple words):
   - **word112** = `(preInfusionSwitch << 8) | preInfusionPressure` (high byte = switch/pro_sw, low byte = pressure).
   - **word113** = `(preInfusionTime << 8) | breakTime` (high byte = pre-inf time, low byte = pause).
   - **word114** = `headTemper` (whole °C, the MCU multiplies by 10).
8. **Word 14 is multiplexed** between `steamGears` (steam gear) and the pre-infusion pressure role in the recipe block. The MCU's recipe function `0x1E10` reads the **low byte of word112** (pressure) and only handles pressure **3 or 4**.
9. **Word 13 (pre-inf time parameter) is hard-coded to 60 at boot** but **is overwritten by the MCU's recipe function `0x1E10`** during brew setup (if `preInfusionSwitch` is on). `preInfusionTime` (word113 hi) is copied by the MCU to word113 lo and is part of the timing `word13 + word113_lo`.
10. **The MCU has NO range checks on cmd6** (a plain `memcpy` + ACK, registermap §3). **The original UI's limits are the only guard.** merakiplus must enforce the same min/max/step — otherwise extreme values can reach the MCU unfiltered.

---

## 1. Persistence & sync architecture (implementation-critical)

### 1.1 Config reading (boot)
* `configfileOpen` [0xd0ac4]: returns immediately if the global cJSON tree (`0x1acedc`) ≠ NULL; otherwise `fopen("/customer/config.json","r")` → `fread` → `cJSON_Parse`. **The file is only read once, at boot.**
* `model_data_init` [0xcef40]: reads all config fields into model globals **and** mirror words (see the table in §1.3). Called by `app_init` [0xd90bc] **before** `fuart_thread` is started.
* `getNumber(key, default, &out)` [0xd0eec]: if a field is missing → it is created with the default **and the file is rewritten** (`configFileClose`). I.e. even a read can add missing fields.

### 1.2 Config writing (immediate)
* `setXxx(v)` → `setNumber(key, v)` [0xd0fa4] → `cJSON_ReplaceItemInObject` (or `AddItemToObject` if new) → `configFileClose` [0xd0c40] → `fopen("w")` (persistent fd `0x1aced8`) → `cJSON_Print` → `fseek(0)/ftruncate(0)/fwrite/fflush/fsync`.
* **Consequence:** every `setXxx` persists immediately. There is no "wait until exit" semantics.

### 1.3 model_data_init — config → model/mirror (boot) [0xcef40]
| config field | model offset | mirror word | Note |
|---|---|---|---|
| Temperature | +0x51 | — | °C/°F flag (display) |
| timemode | +0x59 | — | 12/24 h (display) |
| Automatically | +0x58 | — | automatic time sync (display) |
| timeZone | +0x56 | — | UTC offset index (display) |
| language | +0x50 | — | language index (display) |
| backlight | +0x57 (=v×10) | — | PWM duty (display) |
| volume | +0x53 | — | sound (display) |
| sleepMode | +0x54 | — | **only read, no UI editor** |
| sleepModeSetting | +0x55 | — | **only read, shown as "%dmin"** |
| clearTip | +0x5a | — | tip flag |
| brewTemper | — | **word11** | 0.1 °C |
| autoGrindWeight | — | **word4** | 0.1 g |
| grindMode | — | **word2** | 1/2 |
| steamGears | — | **word14** | 1..4 |
| preInfusionPressure | +0x5c | (word112 lo) | packed later |
| preInfusionTime | +0x60 | (word113 hi) | packed later |
| breakTime | +0x64 | (word113 lo) | packed later |
| headTemper | +0x68 | (word114) | packed later |
| preInfusionSwitch | +0x5b | (word112 hi) | packed later |
| sumtime | +0x6c | — | display flag |
| autobrewWeight | — | **word23** | 0.1 g |
| AtuoSteamTarge | — | **word25** | 0.1 °C |
| autoHotWaterTarget | — | **word17** | 0.1 g |
| hotWaterShortcut | +0x6d | — | display flag |
| presetStore | +0x1174 | — | JSON object |

After reading the fields, `model_data_init` packs [0xcf284..0xcf43c]:
* **word112** = `(preInfusionSwitch?0x100:0) | (preInfusionPressure & 0xFF)`
* **word113** = `(preInfusionTime << 8) | (breakTime & 0xFF)`
* **word114** = `headTemper`

### 1.4 Boot push to the MCU (CORRECTION to WRITETEST)
`fuart_thread` [0x14671c], after `open_port(1)` + `set_opt(57600,8N1)`:
1. Sets the mirror **word5=60, word13=60, word15=60** [0x146810..0x14684c] (hard-coded recipe parameters).
2. **`usart_send(6, 1, 30)`** [0x14685c] → writes **words 1..30** from the mirror (all setup words incl. weights/temperatures/gear).
3. In the poll loop, the first time the flag `0x1a63d2` ≠ 0: **`usart_send(6, 112, 3)`** [0x146b0c] → writes **words 112..114** (pre-infusion + head temperature), resets the flag.
4. Then the normal poll rotation (cmd3 sub0/56 len56, sub33 len7 ×2).

**Parity consequence:** when merakiplus changes config.json and restarts demo_v9 (kill + watchdog), the new value will be **pushed to the MCU at boot** via the above. But if merakiplus *itself* holds ttyS1 and writes directly, it must send the same blocks. Order: **config first, then the MCU write** (or let the boot push handle it on restart).

### 1.5 Mirror vs. MCU-owned words
* **Writable (display→MCU):** words **0..31** (`sub+len<33`) and words **112..114** (`sub+len∈{113,114,115}`). Everything else is rejected by the MCU ("06 error").
* **MCU-owned status (words 32..111):** the display must **not** write these (sensors/flags/work_sta). merakiplus must only read them.
* **Trigger words** (consumed + reset by the MCU): words 3/9 (start/tare), 6 (grind tare), 26/27 (calibration), 30 (reboot). **They must be 0 in normal write blocks** — otherwise an unintended action is triggered.

---

## 2. PARAMETER INVENTORY

Confidence: **H**=high (unambiguous code + live), **M**=medium (code, not live-verified here), **L**=low/inferred.
"write" gives the **primary single-parameter write** (sub, len, word offset in the payload). Block writes (recipe/start) are in §3.
`persist`: `config+mcu` = both config.json and an MCU write; `config` = config.json only (display-local); `mcu` = MCU write only (no config).

### 2.1 TIER 1 — core brewing parameters

| # | Parameter | UI label (EN) | Page / handler | config field (current) | Unit/scale | word | write (sub,len,off) | min–max (step) | default | persist | Conf. |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Grind weight** (dose) | "grinder weight" / "Dose" | page_grinder_weight, onClick [0xf4938] | `autoGrindWeight`=185 | 0.1 g (185=18.5 g) | **4** | (4,1,0) | **1.0–30.9 g (0.1)** | 180 | config+mcu | H |
| 2 | **Brew weight** (yield) | "brew temper" page, yield roller | page_brew_temper, onClick [0x12bc5c] | `autobrewWeight`=360 | 0.1 g (360=36.0 g) | **23** | (23,1,0) | **10–99 g (1)** | 360 | config+mcu | H |
| 3 | **Hot water weight** | "water weight" | page_water_weight, onClick [0xfbeb4] | `autoHotWaterTarget`=400 | 0.1 g (400=40.0 g) | **17** | (17,1,0) | **30–500 g (10)** | 3600 | config+mcu | H |
| 4 | **Brew temperature** | "brew temper" / "Brew Temperature" | page_brew_temper, onClick [0x12bc5c] | `brewTemper`=870 | 0.1 °C (870=87.0 °C) | **11** | (11,1,0) | **85–96 °C (1)** [=185–205 °F] | 870 | config+mcu | H |
| 5 | **Group head temp** | "Temperature of Group Head" | page_time_zone (mode 4), onClick [0xeb1ac] | `headTemper`=70 | **whole °C** (70=70 °C); MCU ×10 | **114** | (114,1,0) | **30–80 °C (10)** ¹ | 50 | config+mcu | H |
| 6 | **Steam/milk target** | "Steam level"/milk temp | page_brew_temper (mode 2), onClick [0x12bc5c] | `AtuoSteamTarge`=650 | 0.1 °C (650=65.0 °C) | **25** | (25,1,0) | **45–70 °C (1)** [=110–160 °F] | 550 | config+mcu | H |
| 7 | **Steam gear** | "Steam level": Gentle/Mild/Medium/Strong | page_steam_select, onClick [0xdcfe0] | `steamGears`=3 | gear 1..4 | **14** | (14,1,0) | **1–4 (1)** | 4 | config+mcu | H |
| 8 | **Pre-infusion pressure** | "Pre-infusion" (pressure) | page_time_zone (mode 1) / switch_ctr / tip_dec | `preInfusionPressure`=4 | bar (integer) | **112 low byte** | (112,2,0)² | **3–4 bar (1)** ³ | 4 | config+mcu | M |
| 9 | **Pre-infusion time** | "Pre-infusion" (time) | page_time_zone (mode 2), onClick [0xeb1ac] | `preInfusionTime`=6 | seconds (integer) | **113 high byte** | (112,2,0)² | **5–? s (1)** ¹ | 15 | config+mcu | M |
| 10 | **Pre-infusion switch** | "Pre-infusion" (on/off) | page_switch_ctr [0x103858] / tip_dec [0x133074] | `preInfusionSwitch`=1 | bool 0/1 | **112 high byte (bit8)** | (112,2,0)² | **0/1** | 0 | config+mcu | H |
| 11 | **breakTime** (pause) | "Pre-infusion" (break/pause) | page_time_zone (mode 3), onClick [0xeb1ac] | `breakTime`=5 | seconds (integer) | **113 low byte** | (112,2,0)² | **0–? s (1)** ¹ | 5 | config+mcu | M |
| 12 | **Grind mode** | grinder mode (weight/time) | home/grinder, move_deale [0x122180], grinderScaleUpdate [0x119c68] | `grindMode`=1 | 1=weight auto, 2=time | **2** | (2,2,0) | **1–2 (1)** | 1 | config+mcu | H |
| 13 | **Hot water mode** | Hot Water auto/manual | hotWaterSetMode [0x11344c], page_water | `hotWaterMode`=1 | 1=auto, 2=manual | **16/19** (in a block) | (9,11,0) | **1–2** | 1 | config+mcu | H |
| 14 | **Hot water TEMP** | (display) | hotWaterTemperUpdate [0x112c9c] | **NO config** | — | **40 (sensor, read-only)** | **no write** | **fixed = e-boiler temperature** | — | — (read-only) | H |

¹ **Picker max not fully mapped:** the value formula is proven (see §2.1.1), but the number of roller options in the picker is not directly visible (the picker navigation is anomalous, see §6.6). min + step are certain; max is given as an MCU/config-consistent estimate. Verify when the device is online.
² The pre-infusion fields are packed into **word112/113** and sent together as `sub112 len2` (words 112+113) or `len3` (+114). Single-field writes do not exist; the whole word112/113 is written with the other byte preserved.
³ **The MCU's recipe function `0x1E10` only handles pressure 3 or 4** (sets different word13/14/17 or word15/16/18 depending on the mains voltage 120/220 V). Other pressure values are ignored by the MCU. The UI formula is `idx+3` (min 3).

#### 2.1.1 Value formulas from the roller index (proven in the onClick code)
* **Grind weight** [0xf4984]: `word4 = (roller1_sel+1)*10 + roller2_sel`. roller1 = 01..30 (`lvgl_roller_create(1,30)` [0xf4c8c]), roller2 = 0..9. → 10..309 (1.0–30.9 g).
* **Brew temp** [0x12bcfc]: `atoi(roller_str) × 10`; with °F display: `GetCelsius(F)` first. roller 85..96 °C / 185..205 °F ([0x12c370/0x12c478]).
* **Brew weight** [0x12be5c]: `atoi × 10`. roller 10..99 ([0x12c754]).
* **Steam target** [0x12bf80]: `atoi × 10` (or GetCelsius). roller 45..70 °C / 110..160 °F ([0x12c9bc/0x12cab4]).
* **Hot water weight** [0xfbf0c]: `roller_sel × 10`. `roller_step_create(30,500,10)` [0xfc194] → 30,40,…,500 g.
* **Steam gear** [0xdd028]: `sel 0..3 → word14 = 1..4` (compared with 3, 4 cases). Options "Gentle\nMild\nMedium\nStrong".
* **Head temp** [0xeb8a0]: `word114 = idx*10 + 30` → 30,40,50,… °C.
* **Pre-inf pressure** [0xeb374]: `model+0x5c = idx + 3` → word112 low byte.
* **Pre-inf time** [0xeb5b4]: `model+0x60 = idx + 5` → word113 high byte.
* **breakTime** [0xeb740]: `model+0x64 = idx` → word113 low byte.

### 2.2 TIER 2 — comfort / display-local (no MCU write)

| # | Parameter | UI label | Page / handler | config field (cur.) | Unit | word | write | min–max (step) | default | persist | Conf. |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 15 | **Backlight** | "Brightness" | page_brightness, onClick [0xe4bcc] + slider_event_cb [0xe4d28] | `backlight`=9 | 0..10 (slider 0..100 ÷10) | — | no MCU | **0–10 (1)** | 8 | config | H |
| 16 | **Volume** | "Sound Volume" | page_brightness slider [0xe4ce4] + page_setting_com [0xe1b98] | `volume`=31 | 0..100 (slider) / discrete High/Medium/Low/Mute | — | no MCU | **0–100** ⁴ | 40 | config | M |
| 17 | **Temperature unit** | "Temperature Unit" (°C/°F) | page_setting_com, onClick [0xe1938→0xe1b00] | `Temperature`=1 | 0/1 (°F/°C) | — | no MCU | **0/1** | 0 | config | H |
| 18 | **Theme** | "Customization" (Light/Dark) | page_setting_com [0xe1c08] | `theme` (not in config.json) | 0/1 | — | no MCU | **0/1** | 1 | config | H |
| 19 | **Language** | "Language" | page_setting_com [0xe1c78] | `language`=0 | index 0..10 | — | no MCU | **0–10** | 0 | config | H |
| 20 | **hotWaterShortcut** | (hot water shortcut) | page_switch_ctr [0x103cd0] | `hotWaterShortcut`=1 | bool | — (model+0x6d) | no MCU | **0/1** | 0 | config | H |
| 21 | **sumtime** | (daylight saving time/DST — not a timer display; accessor `getSumerTimeSwitchValue`, cf. the v6.1 finding) | page_switch_ctr [0x103c70] | `sumtime`=0 | bool | — (model+0x6c) | no MCU | **0/1** | 0 | config | H |
| 22 | **clearTip** | (cleaning tip) | page_clear, onClick [0xfe060→0xfe168] | `clearTip`=0 | bool | — | no MCU | **0/1** | 0 | config | H |
| 23 | **Sleep mode** | (sleep on/off) | **no UI editor** | `sleepMode`=1 | bool | — | no MCU | 0/1 | 1 | config (read only) | H |
| 24 | **Sleep time** | "%dmin" (display) | **no UI editor** | `sleepModeSetting`=0 | minutes | — | no MCU | — | 0 | config (read only) | H |

⁴ **Volume has two controls:** (a) page_brightness **slider 0..100** (the value is stored directly, `setVolume` [0xe4ce4]); (b) page_setting_com **discrete selector** "High/Medium/Low/Mute" (4 options, `setVolume` [0xe1b98], value = option index). The config `volume=31` comes from the slider. The exact mapping for the discrete selector (whether it maps to fixed values) has not been fully mapped — the slider is the fine-grained, authoritative control. **No MCU write** (sound is display-local via `mmplayer_setVolume`/`set_volumn_type`).

**Language list (11 languages, index 0..10):** English, Deutsch, French, Italian, Español, Polish, العربية, 简体中文, 繁体中文, 한국어, 日本語 (strings @ 0x166d84..0x166df4). `language=0` = English.

### 2.3 TIER 3 — system / time / Wi-Fi (document, but leave out of merakiplus v1)

| # | Parameter | UI label | Page / handler | config field (cur.) | Unit | word | write | persist | Conf. |
|---|---|---|---|---|---|---|---|---|---|
| 25 | **timeZone** | "Date & Time"→zone | page_time_zone (mode 0) [0xeb238], time_sync_thread | `timeZone`=48 | UTC offset index | — | cmd8 (time) | config | H |
| 26 | **timemode** | 12/24 h (v6.16: **1 = 24 h, 0 = 12 h**, `update_time` 0xD5944 XORs with 1) | page_dateAndtime [0xfcb58→0xfcdec] | `timemode`=1 | 0/1 | — | no MCU | config | H |
| 27 | **Automatically** | automatic time sync | page_dateAndtime [0xfcb58→0xfccd0] | `Automatically`=1 | bool | — | no MCU | config | H |
| 28 | **Date/time** | "Date & Time" | page_time_select [0xdded4], page_date_select [0xe6074] | `datetime` (string) | — | — | cmd8 + `biz_set_system_time` | config+system | H |
| 29 | **brewMode** | (none) | **0 callers — vestigial** | `brewMode`=0 | — | — | none | config (unused) | H |
| 30 | **wifionoff** | Wi-Fi on/off | app_wifi_setOn [0x13a538] | `wifionoff`=1 | bool | — | no MCU | config | H |
| 31 | **ssid / password** | Wi-Fi | page_wifi [0xef0ac] | `ssid`="<ssid-redacted>", `password`="<password-redacted>" | strings | — | no MCU | config | H |
| 32 | **presetStore / NFC** | "Preset"/NFC profile | page_preset* + app_nfc_thread | `presetStore`={}, `presetAppliedIndex` | JSON | **4..25** | (4,21/22,0) | config+mcu | H |
| 33 | **init / patch / version** | (setup/OTA) | page_first_config, lage_patch | `init`=1, `patch`=1, `*version` | — | — | none | config | H |

**⚠️ Wi-Fi policy (task requirement):** merakiplus must **NOT touch** `wifionoff`/`ssid`/`password` or the Wi-Fi functions. Documented here only. Likewise, OTA (`page_software_update`, `page_mcu_soft_update`) and the factory test should be avoided in v1.

#### 2.3.1 Service controls (write-only, no config — leave out of the normal UI)
These exist in the original UI as service/factory pages and are included in the write atlas (§3.8) and `parity_spec.json` (tier 3, `persist:"mcu"`). **They are dangerous or service-only and should not be exposed in merakiplus v1:**
| Control | Page | word(s) | write | Hazard |
|---|---|---|---|---|
| Back-flush clean (~30 s) | page_clear / clear_run | 20 | (20,1) | cleaning cycle |
| Drain Mode / Drain Valve | page_drain_value | 28, 29 | (28,1)+(29,1) | draining |
| Grind scale calibration | page_test_scale | 26 | (26,1) | writes MCU flash (grind_k) |
| Extract scale calibration | page_test_scale | 27 | (27,1) | writes MCU flash (extract_k) |
| MCU reboot (factory reset) | page_reset | 30 | (30,1) | **resets the MCU** |
| Self-test / factory test | page_self_test / factoryTest | 8 | (8,1) | test mode |
| Service block | page_service_setting | 1..29 | (1,29) | word1=1, word28/29=2 |

---

## 3. WRITE BLOCK ATLAS (word for word)

Frame format: `AA 55 06 SUB LEN <LEN words, 16-bit LE> CRC16lo CRChi` (CRC-16/MODBUS over bytes[0..4+2·LEN]). `usart_send(cmd,sub,len)` [0x146d5c] copies `len` words from the mirror `machine+0xFE8+2·sub`. **Payload = the mirror words at the time of writing** — merakiplus must make sure the mirror contains the desired values (incl. 0 in the trigger words) before sending.

### 3.1 sub3 len4 — GRIND START (words 3..6) · live-verified [BREW W1]
Call: home onClick [0x109278]. Before that: word3=1 [0x109250], word38=0 (clear the grind weight) [0x109268]. Followed by `sub6 len1` word6=1 (tare) [0x10928c].
| Word | Field | Value (observed) | Role |
|---|---|---|---|
| 3 | grind_start_trigger | **1** | the MCU consumes + resets |
| 4 | grind_auto_weight | 185 | target (0.1 g) |
| 5 | grind_over_time | 60 | timeout (hard-coded 60 at boot) |
| 6 | grind_tare_trigger | **1** | tare the grind weight |
Observed frame: `word3=1, word4=185, word5=60, word6=1`. The MCU stops autonomously at the target weight (bit6 work_sta).

### 3.2 sub5 len18 — BREW START (words 5..22) · live-verified [BREW W3]
Call: home onClick [0x109700]. Before that: word9=1 [0x109584], word39=0 (clear the brew weight) [0x10959c], word22 toggles to 1 [0x10965c].
| Word | Field | Value | Role |
|---|---|---|---|
| 5 | grind_over_time | 60 | sent along |
| 6 | grind_tare | 0 | — |
| 7 | grind_run | 0 | — |
| 8 | self_mode | 0 | — |
| 9 | **brew_start_trigger** | **1** | the MCU consumes + resets |
| 10 | (reserved) | 0 | — |
| 11 | **brew_set_temp** | **870** | = config brewTemper |
| 12 | (reserved) | 0 | — |
| 13 | brew_recipe_param (pre-inf) | 60 | see §6.3 (overwritten by MCU 0x1E10) |
| 14 | steam_gear / pre-inf pressure | 3 | = config steamGears (multiplexed) |
| 15 | steam_recipe_param | 60 | sent along |
| 16 | hotwater_auto | **0** | NOT hot water |
| 17 | hotwater_weight | 400 | sent along (not used for brewing) |
| 18 | steam_switch | 0 | — |
| 19 | hotwater_sw | 0 | — |
| 20 | water_target | 0 | brewing is weight-controlled |
| 21 | (reserved) | 0 | — |
| 22 | **brew_active flag** | **1** | the MCU clears it itself on stop |
Observed payload (words 5..22): `60,0,0,0,1,0,870,0,60,3,60,0,400,0,0,0,0,1`. The MCU stops autonomously at the target weight (predictively, after-drip).

### 3.3 sub9 len11 — HOT WATER START (words 9..19) · live-verified [HOTWATER W1]
Call: hotWaterStartAutoSwitch [0x113d88] (word16=1, word19=0) / hotWaterStartManualSwitch [0x113e0c] (word16=0, word19=1). Before that: word9=1, word39=0.
| Word | Field | Value (auto) | Role |
|---|---|---|---|
| 9 | **start_trigger** | **1** | the MCU consumes + resets (tare) |
| 10 | (reserved) | 0 | — |
| 11 | brew_set_temp | 870 | sent along |
| 12 | (reserved) | 0 | — |
| 13 | brew_recipe_param | 60 | sent along |
| 14 | steam_gear | 3 | sent along |
| 15 | steam_recipe_param | 60 | sent along |
| 16 | **hotwater_auto** | **1** | auto flag (the MCU clears it on stop) |
| 17 | **hotwater_weight** | **400** | dispensing target (0.1 g) |
| 18 | steam_switch | 0 | — |
| 19 | hotwater_sw | **0** | manual switch (1 in manual mode) |
Observed frame: `aa 55 06 09 0b 01 00 00 00 66 03 00 00 3c 00 03 00 3c 00 01 00 90 01 00 00 00 00 08 3e`. The MCU stops autonomously at word39≥word17. **No stop write** (auto). Manual stop: `sub16 len1` word16=0 [0x11468c] + `sub19 len1` word19=0 [0x114720].

### 3.4 sub24 len8 — STEAM START/STOP (words 24..31) · static + SESSION
Call: home onClick [0x109548/0x109900]. word24 toggles 0↔1 [0x1094fc/0x1098b4].
| Word | Field | Value (start) | Role |
|---|---|---|---|
| 24 | **steam_switch** | **1** (0=stop) | start/active flag |
| 25 | **milk_set_temp** | **650** | = config AtuoSteamTarge |
| 26 | grind_k_calib | **0** | ⚠️ trigger — MUST be 0 |
| 27 | extract_k_calib | **0** | ⚠️ trigger — MUST be 0 |
| 28 | drain_param | 0 | — |
| 29 | drain_param | 0 | — |
| 30 | **reboot** | **0** | ⚠️ MUST be 0 (otherwise an MCU reset!) |
| 31 | (reserved) | 0 | — |
Observed start frame [SESSION §10.2]: `aa 55 06 18 08 01 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 cd 67`. **Auto-stop:** the MCU stops by itself at word48 (milk) ≥ word25, clears word24 + work_sta bits 5+14 (0x4020). Manual stop = the same block with word24=0. **The physical wand switch (PE.2) is required** to start. *(Later disproven live — see [ui-guide.md §4a](../ui-guide.md) item 10: display steam starts without the wand switch.)*

### 3.5 sub18 len14 — STEAM MIRROR SYNC (words 18..31) · SESSION
Call: home onClick [0x109848]. word18=0 [0x109818]. **Not a trigger** — syncs the recipe words incl. word25.
| Word | Field | Value |
|---|---|---|
| 18 | steam_switch | 0 |
| 19 | hotwater_sw | 0 |
| 20 | water_target | 0 |
| 21 | (reserved) | 0 |
| 22 | brew_active | 0 |
| 23 | brew_auto_weight | 360 |
| 24 | steam_switch | 0 |
| 25 | milk_set_temp | 650 |
| 26..31 | calib/drain/reboot/resv | **0** |
Example [SESSION §10.2]: `aa 55 06 12 0e 00 00 … 68 01 00 00 8a 02 … fc 29`. Can be sent before a steam start to make sure the setpoint is in the mirror.

### 3.6 sub4 len21 / len22 — RECIPE BLOCK (NFC / preset)
* **NFC:** `sub4 len21` = **words 4..24** (app_nfc_thread [0x1437e8]). NFC sets word4/11/23 from the card; the other words come from the mirror.
* **Preset apply:** `sub4 len22` = **words 4..25** (pagePresetStoreApply [0x127e18]). Sets word4/11/23/14/25 from the preset.
| Word | Field | Source |
|---|---|---|
| 4 | grind_auto_weight | preset/NFC |
| 5 | grind_over_time | mirror (60) |
| 6..8 | grind tare/run/self | 0 |
| 9 | brew_start_trigger | 0 (no start) |
| 10 | reserved | 0 |
| 11 | brew_set_temp | preset/NFC |
| 12 | reserved | 0 |
| 13 | pre-inf param | mirror (60) |
| 14 | steam_gear | preset |
| 15 | steam param | mirror (60) |
| 16 | hotwater_auto | 0 |
| 17 | hotwater_weight | mirror |
| 18 | steam_switch | 0 |
| 19 | hotwater_sw | 0 |
| 20 | water_target | 0 |
| 21 | reserved | 0 |
| 22 | brew_active | 0 |
| 23 | brew_auto_weight | preset/NFC |
| 24 | (steam-related) | 0 |
| 25 | milk_set_temp | **preset only (len22)** |
**Difference between len21 and len22:** NFC does not include word25 (milk temp); the preset does. Both start at word4.

### 3.7 sub112 len2 / len3 — PRE-INFUSION + HEAD TEMP (words 112..114)
Call: page_time_zone [0xeb570 len2], switch_ctr [0x103bc8 len2], tip_dec [0x133334 len2], boot [0x146b0c len3].
| Word | Byte packing | Contents |
|---|---|---|
| 112 | high byte = **preInfusionSwitch** (bit8/pro_sw), low byte = **preInfusionPressure** | `(switch<<8)\|pressure` |
| 113 | high byte = **preInfusionTime**, low byte = **breakTime** | `(time<<8)\|break` |
| 114 | the whole word = **headTemper** (°C) | MCU ×10 → 0.1 °C |
* Single-field change: read the mirror word, replace only the relevant byte, send `sub112 len2` (or `sub113 len1` for word113 only, `sub114 len1` for the head temperature only).
* **sub113 len1** [0xeb700/0xeb880]: word113 only (pre-inf time/break).
* **sub114 len1** [0xeb9f4]: word114 only (head temperature).

### 3.8 Other single-word writes (full atlas)
| sub,len | Word(s) | Field / action | Call (address) | Value |
|---|---|---|---|---|
| 0,1 | 0 | **power** on/off | PowerOn [0x146ef8]/PowerOff [0x146f34] | 1/0 |
| 1,1 | 1 | **sleep/drain** (1=sleep, 0=awake) | MachSleep/Awaken [0x146f70/0x146fac] (dead code), ensure_service_mode [0xdfebc] | 0/1 |
| 1,29 | 1..29 | **service block** (word1=1, word28=2, word29=2) | page_service_setting [0xf7054] | mixed |
| 1,30 | 1..30 | **boot push** (all setup words) | fuart_thread [0x14685c] | mirror |
| 2,2 | 2..3 | **grind mode** (word2=1/2) | grinderScaleUpdate [0x119d14], move_deale [0x122278], home [0x108eb8] | 1/2 |
| 3,1 | 3 | grind start trigger | home [0x10a888], test_scale [0xe0cdc] | 1 |
| 4,1 | 4 | **grind weight** | page_grinder_weight [0xf49fc], goto_grinder_page [0x11680c] | 0.1 g |
| 4,3 | 4..6 | grind recipe (word6=0) | home [0x109150] | — |
| 6,1 / 6,2 | 6(/7) | **grind tare** | home [0x10928c/0x1093ac] | 1 |
| 8,1 | 8 | **self-test mode** | page_self_test [0xf5584/0xf55ec] | 1/0 |
| 9,1 | 9 | brew/hot water start trigger | home [0x10a740], hotWaterSetMode [0x1134ac], scale [0xe3a00] | 1 |
| 11,1 | 11 | **brew temp** | page_brew_temper [0x12bda8], home onEnter [0x10aec0] | 0.1 °C |
| 14,1 | 14 | **steam gear** | page_steam_select [0xdd0d4], clear onHandle [0xfff94] | 1..4 |
| 16,1 | 16 | **hotwater_auto** (stop=0) | hotWaterStartStop [0x11468c], home onHandle [0x10d16c] | 0/1 |
| 17,1 | 17 | **hot water weight** | page_water_weight [0xfbf88] | 0.1 g |
| 18,1 | 18 | steam switch (pulse/manual) | home [0x109970] | 0/1 |
| 19,1 | 19 | **hot water manual sw** | set_manual_hot_water_switch [0x1051b0], hotWaterStartStop [0x114720] | 0/1 |
| 20,1 | 20 | **water_target / back-flush clean** | page_clear [0xfe218/0xfff28/0x100074] | 0/1 |
| 21,1 | 21 | grindability / error clear | page_error [0xf80d8], hidGrindeability [0x1187f0], home [0x10a810] | 1 |
| 23,1 | 23 | **brew weight** | page_brew_temper [0x12bee8], goto_brew_page [0x11a564] | 0.1 g |
| 25,1 | 25 | **steam/milk target** | page_brew_temper [0x12c07c], goto_steam_page [0x11e2f0] | 0.1 °C |
| 26,1 | 26 | **grind_k calibration** | page_test_scale [0xe0b94…] | 100/200/500 (weight) or 1..6 (gear) |
| 27,1 | 27 | **extract_k calibration** | page_test_scale [0xe0bc0…] | 100/200/500 |
| 28,1 | 28 | **drain param** | page_drain_value [0x1265ac] | 1 (2 in service) |
| 29,1 | 29 | **drain param** | page_drain_value [0x12665c] | 1 (2 in service) |
| 30,1 | 30 | **MCU reboot** | page_reset [0xe2f5c] | 1 ⚠️ |
| 31,1 | 31 | (reserved/uartPageGoto) | uartPageGoto [0x12348c] | 0 |
| 113,1 | 113 | pre-inf time/break | page_time_zone [0xeb700/0xeb880] | packed |
| 114,1 | 114 | **head temp** | page_time_zone [0xeb9f4] | °C |

**Unknown/reserved words (explicit):** words 10, 12, 21, 31, 32, 76..79, 115..127 have no individual MCU access (registermap §2). In write blocks they are sent as 0 (or the mirror value). Word 24 in the recipe block sub4 is "steam-related" but without unambiguous semantics — send 0.

---

## 4. MCU-side interpretation of pre-infusion (multiplexing details)

The MCU's brew control `brew-ctl` [0x1e9c] calls the recipe function **`0x1E10`** during setup:
```
0x1E14: read word112 high byte (preInfusionSwitch)
0x1E18: if 0 → word113 lo=0 (no pre-infusion), return
0x1E1A: read word113 high byte (preInfusionTime)
0x1E1E: copy to word113 low byte   ← preInfusionTime goes into the "breakTime" slot
0x1E36: read word112 low byte (preInfusionPressure)
0x1E34: compare mains voltage (0x78=120V / 0xdc=220V)
  120V: pressure==3 → word17=840, word13=340, word14=360
        pressure==4 → word17=840, word13=440, word14=460
  220V: pressure==3 → word18=870, word15=300, word16=320
        pressure==4 → word18=870, word15=350, word16=370
  (other pressure values → no change)
```
After that, `brew-ctl` [0x1fd4] uses **`word13 + word113_lo`** as the pre-infusion duration (compared against a timer).

**Parity consequences:**
* `preInfusionSwitch` (word112 hi) **gates** the whole pre-infusion.
* `preInfusionTime` (word113 hi) is copied to word113 lo and is part of the timing.
* `preInfusionPressure` (word112 lo) selects a **fixed profile** (only 3/4 are handled).
* `breakTime` (word113 lo) is **overwritten** by the MCU with preInfusionTime — i.e. the separate breakTime value has **no effect** in a standard brew (it is replaced). This is a firmware quirk; document it but do not expect breakTime to change the brew.
* `word13` is hard-coded to 60 at boot, but overwritten by 0x1E10 (340/440) when preInfusionSwitch is on.

---

## 5. PARITY SCOPE — recommendation for merakiplus v6

### TIER 1 (must expose — core brewing, full parity)
Grind weight, brew weight, hot water weight, brew temp, group head temp, steam/milk target, steam gear, pre-infusion (pressure/time/switch), breakTime, grind mode, hot water mode.
**Rationale:** these are the parameters the user actually adjusts to brew. All have a config + MCU write and are verified live/statically. The pre-infusion fields must be byte-packed correctly into word112/113 (§3.7) — otherwise the behavior is wrong.

### TIER 2 (should expose — comfort, display-local, low risk)
Backlight, volume, temperature unit (°C/°F), theme, language, hotWaterShortcut, sumtime, clearTip.
**Rationale:** pure config writes (no MCU write), low risk. °C/°F must also affect how the tier 1 temperatures are **displayed** (but word11/25 are always 0.1 °C). Language requires the 11 language strings (or let merakiplus be Danish-only and document the deviation). *(merakiplus later got its own EN/DA UI language, v6.3.)*

### TIER 3 (document, but LEAVE OUT of v1)
* **timeZone/timemode/Automatically/date-time:** requires cmd8 time sync + DST logic (`time_sync_thread`, `calc_*_dst`) — complex, not core. Can be added later.
* **sleepMode/sleepModeSetting:** the original UI has **no working editor** (hard-coded timing). merakiplus can either (a) leave it out, or (b) implement a *better* editor that actually writes word1 — but that is an **extension**, not parity. Recommendation: leave it out of v1, document it.
* **brewMode:** vestigial (0 callers). Leave out.
* **wifi/ssid/password:** **MUST NOT be touched** (task requirement). Leave out.
* **OTA/factory test/calibration (word26/27)/reboot (word30)/drain (word28/29)/self-test (word8):** dangerous or service-only. Leave out of the normal UI; calibration/reboot could be a hidden service menu. **In particular: word30=1 reboots the MCU, word26/27≠0 triggers calibration — these must ALWAYS be 0 in normal write blocks.**
* **presetStore/NFC:** nice to have. Preset apply (sub4 len22) is straightforward; NFC requires the MF522 driver + card format. Recommendation: preset in v2, leave NFC out.

### Persistence safeguards (procedure for merakiplus)
Cf. `WRITETEST §7` + §1 here:
1. **Backup + md5 before every write:** copy `/customer/config.json` → backup, log the md5 (baseline `<config-md5-redacted>`).
2. **Generate byte-exact cJSON** (`"key":\tvalue`, tab indentation) — only the desired fields change.
3. **Atomic write:** temp file + `mv` (NEVER in place — demo_v9's persistent write fd would otherwise overwrite/corrupt it).
4. **Order:** **config first, then the MCU write.** If merakiplus writes directly to the MCU (holds ttyS1), send the mirror block with correctly packed word112/113 and 0 in the trigger words. If merakiplus only changes the config and restarts demo_v9, the **boot push** (§1.4) handles the MCU sync automatically.
5. **Reboot-free reload:** `kill $(pidof demo_v9)` → the watchdog `test.sh` restarts it within **< 4 s** and reads the new values + boot-pushes them. Effective downtime ~3–5 s. (No full reboot needed.)
6. **Verify:** md5 after the write + the relevant sysfs/UI signal (e.g. PWM duty for the backlight: `duty = 100 − 7·config_v`).
7. **md5 log:** log every config change (before/after md5, field, value, timestamp) cf. WRITETEST.
8. **MCU write first or config first?** For an *immediate* MCU effect without a restart: **MCU write first** (so the action happens), then the config (so it is persisted). For persistence *only*: config + kill. merakiplus should do **both** (MCU write + config) for full parity with the original UI's onClick handlers (which do both).

---

## 6. SAFETY NOTES & QUIRKS

1. **No MCU range checks:** cmd6 = a plain `memcpy` + ACK (registermap §3). All 16-bit values are accepted. **merakiplus's min/max/step are the only guard.** Enforce the limits in §2 before writing.
2. **Trigger words must be 0:** words 3/6/9 (start/tare), 26/27 (calibration), 30 (reboot) in every block that is not explicitly meant to trigger them. In particular **sub24 len8 (steam)** and **sub18 len14** include words 26/27/30 — set them to 0.
3. **word30=1 → MCU reboot** (AIRCR reset). Only in page_reset. Never in normal blocks.
4. **word26/27 ≠ 0 → calibration** (grind_k/extract_k are written to the MCU flash). Only in page_test_scale.
5. **Extreme temperatures:** the MCU's error detector sets E1/E2/E3 (head>110 °C→E1, e-boiler>120 °C→E3, s-boiler>160 °C→E3). But cmd6 still accepts the value. merakiplus should cap to the UI limits (85–96 °C brew, 45–70 °C milk, 30–80 °C head).
6. **Pre-infusion picker anomaly:** the rows of page_time_zone (pressure/time/break/head temp) push `nav+0x6c` on click — which `app_pages_init` registers as **the Wi-Fi page** [0xf1c40]. This is either a firmware quirk or a shared picker page. **The value formulas are nevertheless proven** from the CLICKED path (§2.1.1). The picker's number of options (max values) has therefore not been fully mapped — verify when the device is online. Implement the formula + sensible limits.
7. **breakTime is overwritten by the MCU** with preInfusionTime (§4) — a separate breakTime has no effect in a standard brew.
8. **brewMode vestigial**, **sleepMode/sleepModeSetting without an editor** (§0.5/0.6) — do not implement an editor as "parity" (it does not exist in the original).
9. **Hot water temp is read-only** (the e-boiler sensor word40). No setpoint write exists.
10. **Steam requires the physical wand switch** (PE.2, word44 bit0) — a write alone does not start steam without the wand (SESSION §5.2). merakiplus cannot replace the wand with a write alone. *(Later disproven live: display steam starts without the wand switch — see [ui-guide.md §4a](../ui-guide.md) item 10.)*

---

## 7. VERIFICATION STATUS

| Layer | Method | Confidence |
|---|---|---|
| Frame format/CRC/sub+len bands | protocol + live (BREW/HOTWATER/SESSION) | **High** |
| Config accessors (key + default) | all 86 resolved in parity_accessors.json | **High** |
| All 82 usart_send call sites (sub,len) | parity_calls.json (automatically resolved) | **High** |
| Single-parameter writes (grind/brew/water/temp/gear) | code + live frames | **High** |
| Pre-infusion byte packing (word112/113) | onClick + model_data_init + MCU 0x1E10 | **High** (packing), **Medium** (picker max) |
| Boot push (sub1 len30 + sub112 len3) | fuart_thread disassembly | **High** (static; corrects WRITETEST) |
| Hot water temp = read-only | hotWaterTemperUpdate reads word40 | **High** |
| brewMode/sleep without an editor | 0 callers of the setters | **High** |
| MCU pre-infusion profile (pressure 3/4, 120/220 V) | MCU 0x1E10 disassembly | **High** (static) |
| Picker number of options (max for pressure/time/break/head) | anomalous navigation (nav+0x6c) | **Low** — requires online verification |
| Volume discrete selector mapping | setting_com High/Medium/Low/Mute | **Medium** |

**Reproduction (in the author's working environment):**
```bash
.venv/bin/python workspace/parity_analysis.py     # builds the accessors/calls/funcs JSON
.venv/bin/python workspace/sdump.py <start_hex> <end_hex>   # annotated disassembly
```

---

## 8. APPENDIX — key addresses (demo_v9)

| Symbol | Address | Role |
|---|---|---|
| `configfileOpen` / `configFileClose` | 0xd0ac4 / 0xd0c40 | config read / immediate rewrite |
| `getNumber` / `setNumber` | 0xd0eec / 0xd0fa4 | cJSON get/set (+ persist) |
| `model_data_init` | 0xcef40 | config→model/mirror at boot |
| `app_init` | 0xd9080 | calls model_data_init, PowerOn |
| `fuart_thread` | 0x14671c | UART loop; boot push sub1 len30 [0x14685c] + sub112 len3 [0x146b0c] |
| `usart_send` | 0x146d5c | cmd6 write from the mirror |
| `usart_ckeck` | 0x146e70 | cmd3 query |
| home `onClick` | 0x1088a4 | grind/brew/steam/hot water start blocks |
| `pagePresetStoreApply` | 0x127c84 | preset sub4 len22 |
| `app_nfc_thread` | 0x14291c | NFC sub4 len21 |
| `SleepRefresh` / `powerOff` / `recodeTime` | 0x149268 / 0x149074 / 0x148164 | hard-coded sleep (1800/600) |
| MCU `brew-ctl` / recipe / `head_heat` | 0x1e9c / 0x1e10 / 0x399e | pre-infusion + head temp ×10 |

*— research sub-agent, 2026-11-17. Purely STATIC analysis; the device was not touched. All addresses can be verified in demo_v9.dis / MEM03_V514_260923.bin.*
