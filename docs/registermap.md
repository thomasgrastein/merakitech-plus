# REGISTERMAP — MerakiTech Gen 1 powerMcu (Nuvoton M451, RT-Thread 5.1.4)

> **Note (public edition):** This document consists of development notes from reverse engineering a MerakiTech Gen 1 owned by the author. The manufacturer's firmware, binaries and scripts referred to (e.g. `demo_v9`, `MEM03_V514_260923.bin`, `demo.sh`, `test.sh`) are **not** included in the repository. Addresses and short excerpts are included solely for interoperability. IP addresses and other personal data have been removed. See [README → Disclaimer](../README.md#️-disclaimer--read-this-before-you-do-anything). References to `HOTWATER.md`, `BREW.md`, `SESSION.md`, `STEAMLIVE.md` and `PARITY.md` are to the author's capture/analysis notes; their results are summarized inline.

**Date:** 2026-11-16 · **Method:** purely STATIC reverse engineering of `artifacts/MEM03_V514_260923.bin` (63,460 bytes, MD5 `e96155dad2443063acb9ecea959854a3`, flash base `0x0`, RAM 32 KB `0x20000000`). No network devices touched.
**Tools:** capstone 5.0.7 (recursive Thumb-2 disassembly + TBB/TBH jump tables + constant propagation), pyelftools. All addresses below are flash offsets in the MCU binary and can be verified with `artifacts/mcu_analysis/dump.py <start> <end>`.
**Prerequisite:** read [protocol.md](protocol.md) first (frame format `AA 55`, CRC-16/MODBUS, chip ID M451). This report **maps the register file** and **corrects** two errors in protocol.md (see §7).

---

## 0. Summary (TL;DR)

* The register file ("APP Param") = **129 × 16-bit words in RAM at `0x20000F96`** (index 0..128). Initialized @ `0x9966` (`memset` 256 bytes + defaults). The MCU additionally uses **internal words 128..131** (temperature array) right after the table.
* **Architecture:** the display writes *targets/recipes* (cmd 6, words 0..31 + 112..114). The MCU's control tasks read these and set **`work_sta` = word 33** (actuator bit field). A separate **"run" task (`0x2782`) mirrors word 33's bits to physical GPIO relays**. Sensor tasks write temperatures/weights/errors back to words 32..111, which are broadcast (cmd `0x83`, sub 32, len 80).
* **A cmd 6 write = a plain `memcpy` + ACK** (`0x99C6`): no value range checks; it writes the words and echoes them. Side effects are *indirect*: control tasks poll trigger words (tare, calibration, reboot) and react asynchronously.
* **UART3 is NOT `@C/@R/@W` ASCII** (a protocol.md error): it is a **binary `AA BB CMD LEN payload CRC16`** protocol (parser `0x43CC`, payload words **big-endian**). Used for a scale/control module (grind/brew/steam/hot water start/stop + calibration + identity).
* **Words 112..114 are NOT OTA control** (a protocol.md error): they are **head temperature/product setpoints** (`word114=head_set_temp`, `word112=pro_sw`). OTA runs **exclusively** via the `CC DD` protocol (see §5).
* **OTA (`CC DD`):** request → validate version/size → erase staging → ACK `0x81` → start the "updata" task → sequential 256-byte data packets to fragmented flash staging → checksum → write magic `0x87654321` + size + check @ `0x19800` → reboot. The bootloader copies staging → app.

---

## 1. Architecture & task map (evidence: task creator `0x2A40`, string comparisons)

RT-Thread tasks are created via `0x2A40` (name → entry, priority, tick, stack). Name strings @ `0x2D14..0x2D70`:

| Task | Entry | Prio | Tick | Role (evidence) |
|---|---|---|---|---|
| `init` | `0x2C00` | 6 | 10 ms | boot init; calls config load `0x92E` (`0x2C0E`) |
| `status` | `0x286E` | 8 | 10 ms | RX parse + sensor loop + broadcast (`0x29F6`→`0x9AAC`) |
| `run` | `0x2782` | 5 | 10 ms | **word 33 → GPIO actuators** (`0x27E6..0x2866`) |
| `updata` | `0x2768` | 5 | 1000 ms | **OTA processor** (`0x2774`→`0xBFD6`, loop `0xBAA8`) |
| `boiler_heat` | `0x3502` | 8 | 10 ms | extraction/steam boiler PID (`0xA5EE`), writes words 62/63 |
| `head_heat` | `0x399E` | 8 | 10 ms | brew head PID; feedback word 128, setpoint word 114 |
| `sleep` | `0x2F52` | 7 | 10 ms | sleep (started when word 0=0, `0x29D8`) |
| `drain` | `0x30E8` | 7 | 10 ms | draining/cleaning (started when word 1=1, `0x29E4`) |
| `product` | `0x4E12` | 7 | 10 ms | **brew/product state machine**; calls the UART3 parser `0x43CC` (`0x4F16`) |
| `pcb_product` | `0x5872` | 5 | 10 ms | factory test/PCB product |
| `self_inspection` | `0x2494` | 8 | 10 ms | **self-test + status broadcast** (`0x2744`) |

In addition there are "box" threads (`box_grind/extract/steam/execute/control`, strings `0x6010..0x6040`, created @ `0x5EB2..0x5EDA`) and helper tasks.

**Sensor/actuator chains (status task `0x286E`, modulo counters):**
* every 2nd tick: `0xB2DE`→**word 70** (e water level), `0xB306`→**word 68** (s water level) (`0x2928/0x2930`).
* every 4th tick: `0x933C`→**word 52** (flow velocity), `0xA7D8` (SPI2 ADC: vol/hz/cur) (`0x2950`).
* every 5th tick: `0x8A62` (EADC scan), `0x5A2` (`0x2940`).
* every 10th tick: `0xB268`, `0x226E` (add_water), **word 44** = input bits from 5 GPIOs (`0x2970..0x299C`).
* every 20th/30th tick: `0x1CB6`/`0xA1B2`/`0xA406` (errors), `0xA42` (grind calibration).
* `0xA336` (ADC temp) writes **words 40/41/42/47/48 + 128..131**; `0xA406` (error detector) writes **words 34/35/36**.
* **word 30** = reboot flag: `0x2A06` reads word 30; if ≠ 0 → reset it, delay 20, `SCB->AIRCR=0x05FA0004` (`0x2A18`).

---

## 2. REGISTER MAP — all 129 indices

Register file @ `0x20000F96`, word index `n` = address `0x20000F96 + 2n`.
**Direction:** `setup` = display→MCU target/recipe (writable via cmd 6); `trigger` = the display writes, the MCU consumes and resets; `sensor` = MCU→display status; `actuator` = MCU output (mirrored to GPIO/PWM); `error` = error code; `internal` = only used internally / not individually.
**Confidence:** high = unambiguous evidence (string + code); med = strong code indication; low = inferred/reserved.

Defaults from init `0x9966` (non-zero only): word 0=1, 2=1, 4=180, 5=60, 11=920, 13=60, 14=4, 15=60, 17=360, 23=360, 25=550, 114=50. The `memset` @ `0x996E` covers words 0..127 (256 bytes); word 128 is set by the ADC.

### 2.1 Power / state (0..1)
| n | Name | Direction | Evidence (address + excerpt) | Conf. |
|---|---|---|---|---|
| 0 | **power** (1=on, 0=off) | setup | init `0x9978` `strh r1=1,[r0]`; status `0x29D4` `ldrh [r5]`→if 0 start "sleep"; protocol `PowerOn/Off` cmd6 sub0 | high |
| 1 | **sleep/drain state** (1=sleep/drain, 0=awake/normal) | setup | status `0x29DE` `ldrh [r5,#2]` cmp 1→start "drain" (`0x29E4`); protocol `MachSleep/Awaken` cmd6 sub1. *(Live 2026-11-17: v2/v3 active phases=0; sleep session=1. merakiplus v3/v4 keep-awake=1 is a BUG (sends the MCU to sleep); v5 writes 0.)* | high |

### 2.2 Grinder (2..8)
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 2 | **grind mode** (1=weight-based auto) | setup | debug `0x9930` "grind: mode=%d" reads `[r4,#4]`; grind ctl `0x1A30` cmp mode==1; init `0x997A`=1 | high |
| 3 | **grind start/tare trigger** (via UART3/product) | trigger | product `0x5664` `strh r6=1,[r4,#6]`; `0x5678` if word3≠0 → tare grind weight `0xAF90(0x200010C0)`, word3=0 | med |
| 4 | **grind auto_weight** (0.1 g) | setup | debug `0x992E` "auto_weight=%d" `[r4,#8]`; init `0x997E`=180; grind ctl `0x1A2E` | high |
| 5 | **grind over_time** (timeout) | setup | debug `0x992C` "over_tiome=%d" `[r4,#0xa]`; init `0x9982`=60; grind ctl `0x1A46` compares elapsed | high |
| 6 | **grind tare/zero trigger** | trigger | grind ctl `0x19F2` `ldrh [r6,#6]`; if ≠0 → `0xAF90` tare grind weight, word6=0 (`0x1A00`) | high |
| 7 | **grind run flag** | trigger | grind ctl `0x1A2A` `ldrh [r6,#0xc]`; controls grinder start/stop relative to weight/timeout | med |
| 8 | **self_inspection mode / drain state** | setup/internal | debug `0x488` "self:mode=%d" reads `[r4,#0x10]`; drain `0x30E8` reads it 20×; self-insp writes | med |

### 2.3 Brew / extraction (9, 11, 13, 22, 23)
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 9 | **brew start/tare trigger** (via UART3/product) | trigger | product `0x5668` `strh r6=1,[r4,#0x12]`; `0x5686` if word9≠0 → tare brew weight `0xAF90(0x200010DC)`, word9=0 | med |
| 10 | *(reserved recipe field)* | internal | **no individual MCU access** found (neither read nor write); in the display write band. Part of the recipe block sub4/sub5 | low |
| 11 | **brew set_temp** (0.1 °C) | setup | debug `0x9942` "extract: set_temp=%d" `ldrsh [r4,#0x16]`; init `0x998C`=920; boiler_heat `0x3502` reads it 9× | high |
| 12 | *(reserved recipe field)* | internal | no individual MCU access; in the write band | low |
| 13 | **brew recipe param** (pre-infusion/time; 0.1) | setup | init `0x999A`=60; brew ctl `0x1E9C` reads; NFC recipe `0x1E6E/0x1E7E` writes 440/340 | med |
| 22 | **brew recipe param** (time/quantity) | setup | brew ctl `0x1E9C` RW (`0x1EF2` reads word20-related); NFC `0x1E10` writes | med |
| 23 | **brew auto_weight** (0.1 g) | setup | debug `0x993C` "auto_weight=%d" `[r4,#0x2e]`; init `0x9998`=360; brew ctl reads | high |

### 2.4 Steam & milk (14, 15, 18, 24, 25)
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 14 | **steam gear** (MCU debug name; NFC writes temperature-like values) | setup | debug `0x995A` "steam: gear=%d" `[r4,#0x1c]`; init `0x999E`=4; boiler_heat reads it 4×; NFC `0x1E74` writes 460/360/320 | med |
| 15 | **steam recipe param** (time/quantity) | setup | init `0x99A0`=60; steam ctl `0x210E` reads; NFC `0x1E52/0x1E8E` writes 350/300 | med |
| 18 | **steam set_temp / recipe** | setup | steam ctl `0x210E` RW; NFC `0x1E4C/0x1E88` writes 870 | med |
| 24 | **steam switch** (word24=1 start, 0 stop) | trigger | steam ctl `0x210E` RW (9 writes); **start write = cmd6 sub24 len8 word24=1** (`AA 55 06 18 08 01 00 8A 02 … CD 67`); manual stop = sub24 len8 word24=0; **AUTO-STOP: word48 ≥ word25 (milk temp ≥ setpoint) → resets word24 + clears work_sta bits 5+14 (0x4020)** — no stop write needed. *(Static code evidence [SESSION.md §10]; live verification pending a session with actual steaming. The first guess "steam recipe param/sub18 len14" was wrong: sub18 len14 = mirror sync.)* | high (static) |
| 25 | **steam milk_set_temp** (0.1 °C) | setup | debug `0x9958` "milk_set_temp=%d" `[r4,#0x32]`; init `0x99A6`=550; steam ctl reads | high |

### 2.5 Hot water (16, 17, 19, 20, 21)
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 16 | **hot water auto** (flag) | setup | debug `0x9950` "hot: auto=%d" `[r4,#0x20]`; hot water ctl `0x1B8C` RW; NFC writes 370/320 | high |
| 17 | **hot water weight** (0.1 g) | setup | debug `0x994E` "weight=%d" `[r4,#0x22]`; init `0x99A8`=360; hot water ctl reads | high |
| 19 | **hot water sw** (start/stop) | setup/trigger | debug `0x994C` "sw=%d" `[r4,#0x26]`; hot water ctl `0x1B8C` RW | high |
| 20 | **water target / target quantity** (add_water) | setup | add_water `0x22AC` `ldrh [r4,#0x28]` cmp 0; brew/steam/hot water all read it; flow meter control | high |
| 21 | *(reserved recipe field)* | internal | no individual MCU access; in the write band | low |

### 2.6 Calibration & system (26..32)
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 26 | **grind_k calibration** (command/value) | trigger | grind calib `0xA4C` `ldrh [r4,#0x34]`; >10 → calibrate grind_k (weight×10, `0xB108`), save config[2]; 1..6 → set gears (cmd3..9 `0x7A2`); word26=0 | high |
| 27 | **extract_k calibration** (command/value) | trigger | grind calib `0xA98` `ldrh [r4,#0x36]`; ≠0 → calibrate extract_k, save config[0], cmd2 `0x7A2`; word27=0 | high |
| 28 | **drain param** (time/counter) | internal | drain `0x30E8` reads `[r5,#0x38]` (`0x31D4`) | low |
| 29 | **drain param** (time/counter) | internal | drain `0x30E8` reads `[r5,#0x3a]` | low |
| 30 | **reboot request** (write ≠0 → the MCU resets) | trigger | status `0x2A06` `ldrh [r5,#0x3c]`; ≠0 → word30=0, delay, `AIRCR` reset `0x2A18` | high |
| 31 | *(reserved)* | internal | no individual MCU access; in the write band | low |
| 32 | *(reserved — first word of the status block)* | internal | **no MCU write** found; broadcast (sub32) but always 0 | low |

### 2.7 Status block (33..75) — broadcast as cmd 0x83 sub32 len80
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 33 | **work_sta** (actuator bit field, see §2.9) | actuator | debug `0x448` "work_sta=%x" `[r4,#0x42]`; run task `0x27E6..` mirrors bits→GPIO; all control tasks write it | high |
| 34 | **E1 error code** | error | debug `0x3DA` "E1=%x" `[r4,#0x44]`; error detector `0xA5C2` `orr #0x20` (head>110 °C); voltage error `0xA94E` `orr #1` | high |
| 35 | **E2 error code** (sensor faults) | error | debug `0x3D6` "E2=%x" `[r4,#0x46]`; error detector: bit0=e boiler NTC (`0xA4B8`), bit1=head NTC (`0xA5E4`), bit3=s boiler NTC (`0xA57E`), bit4=milk NTC (`0xA44E`) | high |
| 36 | **E3 error code** (overheating) | error | debug `0x3D2` "E3=%x" `[r4,#0x48]`; error detector: bit4=e boiler>120 °C (`0xA49A`), bit5=s boiler>160 °C (`0xA4FE`); `0xB268` sets/clears bits | high |
| 37 | **grinder gear** (1..30, computed) | sensor | grind gear `0x181E` returns gear→`0x19EE`/`0x4EFC` `strh [r4,#0x4a]`; piecewise interpolation of word 69 against the gears config | high |
| 38 | **grind weight, calibrated** (0.1 g) | sensor | weight conversion `0xAebc`→grind box `0x65EA`/product `0x56A4` `strh [r4,#0x4c]`; tare `0xAFBE` resets | high |
| 39 | **brew weight, calibrated** (0.1 g) | sensor | weight conversion→`0x56B2` `strh [r4,#0x4e]`; tare `0xAFE4` resets; brew ctl reads | high |
| 40 | **e boiler temp** (0.1 °C) | sensor | ADC temp `0xA3A4` `strh [r1,#0x50]` (from word 129=e_boilert); error detector reads | high |
| 41 | **e boiler temp** (mirror of 40) | sensor | ADC temp `0xA3A0` `strh [r1,#0x52]`; error detector `0xA458` range check + >120 °C→E3 | high |
| 42 | **e head temp** (0.1 °C) | sensor | ADC temp `0xA3AC` `strh [r1,#4]`→word42 (from word 128); error detector `0xA588` >110 °C→E1 | high |
| 43 | **self_inspection sta** (state bits) | sensor/internal | debug `0x484` "self:...sta=%x" `[r4,#0x56]`; self-insp `0x2494` writes it 21× (state machine 0→1→3→7→0xF…) | high |
| 44 | **input bits** (e,s,power,c1,c2; see §2.9) | sensor | status `0x299C` `strh [r5,#0x58]` = 5 GPIO bits (`0x2970..0x299A`) | high |
| 45 | **flow meter brew** (pulse counter) | sensor | brew ctl `0x1E9C` writes; drain reads; add_water flow meter | med |
| 46 | **flow meter steam** (pulse counter) | sensor | steam ctl `0x2134` `strh [r2,#0x5c]` = flow meter PE.2 (`0x17D0`) | med |
| 47 | **s boiler temp** (0.1 °C) | sensor | ADC temp `0xA3AA` `strh [r1,#0xe]`→word47 (from word 130=s_boilert); error detector >160 °C→E3 | high |
| 48 | **milk temp** (0.1 °C) | sensor | ADC temp `0xA39A` `strh [r1,#0x60]`→word48 (from word 131=milk); error detector range check→E2 bit4 | high |
| 49 | **mains voltage** (V, measured) | sensor | SPI2 ADC `0xA89E` `strh [r0,#0x62]` = ADC reg8/3178; debug `0xF694` "vol=%d"; voltage check `0xA8F6` | high |
| 50 | **current channel 1** (cur_ch1) | sensor | SPI2 ADC `0xA8A4` `strh [r0,#2]` = ADC reg6/87; debug "cur_ch1=%d" | high |
| 51 | **SPI2 ADC ch 0xA — pressure candidate OR pump drive level** | sensor | SPI2 ADC `0xA8EE`/`0xA8AC` `strh [r1,#0x66]`; self-insp compares against 8000 (`0x26E4`); grind box reads. **Live evidence:** v2 [HOTWATER.md]: ~0–1 idle, ~1300 during free dispensing, spikes of 16670 when pumping against a closed valve (flow=0). v3 [BREW.md]: two sharp plateaus during brewing — pre-infusion **358 (σ9) for 6.06 s ≈ preInfusionTime=6 ✓**, extraction **1346 (σ15) for 21.35 s**; ratio 3.76. **Calibration unresolved:** anchor pre=4 bar → ext≈15 bar (fails the 9-bar sanity check, but possible for a vibration pump without an OPV); anchor ext=9 bar → pre≈2.4 bar (below the setpoint). The extraction level (~1346) ≈ the hot water level (~1300) despite very different back pressure → **alternative interpretation: pump drive level (PWM/phase angle feedback), not pressure**. The plateaus + 6 s timing + ratio are robust; an absolute bar scale requires physical verification (sensor ID on the board or a blind-basket test with a pressure gauge). | med |
| 52 | **flow velocity** (flow_velocity) | sensor | `0x933C` computes 125000/period→`0x2950` `strh [r5,#0x68]`; debug "flow_velocity=%d" | high |
| 53 | **grind weight raw** (raw/10000, g) | sensor | weight conversion `0xAEE8` `strh [r2,#0x6a]` (ch0); the UART3 cmd2 report reads it | high |
| 54 | **grind_k** (calibration factor) | setup/internal | config load `0x9C4`/save `0x80C` write it (config[2]); UART3 reads; debug "grind_k=%d" | high |
| 55 | **brew weight raw** (raw/10000, g) | sensor | weight conversion `0xAEE2` `strh [r2,#0x6e]` (ch1); UART3 reads | high |
| 56 | **extract_k** (calibration factor) | setup/internal | config load/save (config[0]); UART3 reads; debug "extract_k=%d" | high |
| 57 | **flow meter/pulse counter** (PD.5) | sensor | grind ctl `0x19E6`/product `0x4F0E` `strh [r4,#0x72]` = `0x17D0`(PD.5) | med |
| 58 | **current channel 2** (cur_ch2) | sensor | SPI2 ADC `0xA8B0` `strh [r0,#0x12]` = ADC reg7/87; debug "cur_ch2=%d"; grind box reads | high |
| 59 | **gears_min** (grinder calibration) | setup/internal | config load/save (config[4]); debug "min=%d"; grind gear interpolation | high |
| 60 | **gears_5** (grinder calibration) | setup/internal | config (config[6]); debug "5=%d" | high |
| 61 | **mains frequency** (50/60 Hz) | sensor | SPI2 ADC `0xA8AC`→`0xA880` range check 48..52/58..62 → snap to 50/60; debug "hz=%d" | high |
| 62 | **e boiler heat STAGE** 0/30/60/100 (v6.18 correction: not a power) | sensor/state | boiler_heat `0x35EE…0x3672` `strh [r7,#0x7c]`: e boiler < 40.0 °C → 0, < 60.0 → 30, < word 11 → 60, else 100; 100 while work_sta bit 12. The stock UI's "pre-heat complete" needs 62 **and** 63 > 99 ([audio.md](audio.md)) | high |
| 63 | **s boiler heat STAGE** 0/30/60/100 (v6.18 correction: not a power) | sensor/state | boiler_heat `0x358E/0x37E4…0x3820` `strh [sb,#0x7e]`: s boiler < 50.0 °C → 0, < 80.0 → 30, < steam target − 10.0 °C → 60, else 100 (target by gear word 14: 120/125/130/135 °C); 100 while bit 5 | high |
| 64 | **gears_25** (grinder calibration) | setup/internal | config (config[8]); debug "25=%d" | high |
| 65 | **gears_max** (grinder calibration) | setup/internal | config (config[0xA]); debug "max=%d" | high |
| 66 | **brew counter/timer** | sensor/internal | brew ctl `0x1F9E/0x2086` `strh [r6,#0x84]` (RW many times) | med |
| 67 | **set_voltage** (120/220) | setup/internal | config load/save (config[0x3C]); head_heat `0x3ACA`/PID reads; debug "set_voltage=%d" | high |
| 68 | **s water level** (ADC) | sensor | `0xB306` (EADC ch2)→`0x2930` `strh [r5,#0x88]`; debug "%s:adc=%d" (e_water/s_water) | med |
| 69 | **grinder position ADC** (raw) | sensor | grind gear `0x1836` `strh [r1,#0x8a]` = EADC raw converted (`0xFB0`); UART3 reads | high |
| 70 | **e water level** (ADC) | sensor | `0xB2DE` (EADC ch1)→`0x2928` `strh [r5,#0x8c]`; >2000→flag | med |
| 71 | **Mboard temp** (I2C sensor 0x48) | sensor | `0xA166` `0xA11E(0x48)`→`strh [r5,#0x8e]`; debug "temp:...Mboard=%d" | high |
| 72 | **Dboard temp** (I2C sensor 0x4E) | sensor | `0xA18E` `0xA11E(0x4E)`→`strh [r5,#0x90]`; debug "Dboard=%d" | high |
| 73 | **self_inspection result** (flag bits) | sensor/internal | debug `0x480` "result=%x" `[r4,#0x92]`; self-insp `0x24A6` init=0xFFFF, sets/clears bits | high |
| 74 | **gears_fu4** (grinder calibration) | setup/internal | config (config[0x40]); debug "-4=%d" | high |
| 75 | **gears_40** (grinder calibration) | setup/internal | config (config[0x42]); debug "40=%d" | high |

### 2.8 Identity / serial number & others (76..128)
| n | Name | Direction | Evidence | Conf. |
|---|---|---|---|---|
| 76..79 | *(reserved)* | internal | no MCU write/read found (between the gears config and the firmware version block) | low |
| 80..87 | **controller firmware version** (16 bytes ASCII, e.g. `"V5.1.4"`; earlier read as a UID) | status (read via `sub56 len56`) | init `0x99B0` `memcpy(table+0xA0, 0x20000000, 16)`; `0x20000000` is the version the OTA compare `0xB51C` uses; live `56 35 2E 31 2E 34` at word 80; stock reads it as "MCU Version" (`app+0x1088`) and gates hot water weighing on it — [water-dosing-compat.md](water-dosing-compat.md) | high |
| 88..95 | **controller firmware version, copy** (16 bytes) | status | init `0x99BC` `memcpy(table+0xB0, 0x20000000, 16)`; live identical to 80..87 | high |
| 96..111 | **serial number/product block** (32 bytes ASCII) | internal | config load `0x9F4` `memcpy(table+0xC0, config+0x1C, 32)`; default "B000000-0000-0000" (`0xCC8`); chip info `0x8B6` prints "\nSerial:%s" from table+0xC0; the cmd9/OTA version parser reads words 96/98 (`0x9E2A`, `0xB490`) | high |
| 97,99..111 | *(part of the 32-byte serial number block)* | internal | only bulk-copied (see 96..111); no individual access | high |
| 112 | **pro_sw** (product switch; high byte used) | setup | debug `0x9936` "pro_sw=%d" `ldrb [r4,#0xe1]`; NFC recipe `0x1E14` reads the high byte, `0x1E36` reads the low byte (variant 3/4) | high |
| 113 | **brew/product switch** | setup | NFC recipe `0x1E1A/0x1E1E` copies hi→lo byte; brew ctl `0x1FD6` `ldrb [r6,#0xe2]` | med |
| 114 | **head_set_temp** (brew head setpoint, 0.1 °C) | setup | debug `0x993E` "head_set_temp=%d" `[r4,#0xe4]`; init `0x9990`=50; head_heat `0x39D2/0x3A72` reads it 7× | high |
| 115..127 | *(reserved / internal)* | internal | no individual MCU access found; between the head setpoint and the temperature array | low |
| 128 | **e head temp** (0.1 °C, internal array[0]) | sensor | ADC temp `0xA396` `strh [r4]` (r4=`0x20001096`=word128); head_heat feedback `0x39E2/0x3AA0`; debug "e_head=%d" | high |

> **Note:** words 129/130/131 (right after the table, `0x20001098..`) = the internal temperature array `[e_boilert, s_boilert, milk]` written by ADC temp `0xA362/0xA370/0xA382`. They are **outside** the 129 protocol words, but are copied to words 40/41/47/48.

### 2.9 Actuator and input bit map (evidence: run task `0x2782`, status task `0x2970`)

**Word 33 `work_sta` → GPIO** (the run task mirrors each bit to a relay pin):
| Bit | Mask | GPIO | Pin |
|---|---|---|---|
| 0 | 0x0001 | `0x40004930` | PE.12 |
| 1 | 0x0002 | `0x400048F4` | PD.13 |
| 2 | 0x0004 | `0x400048EC` | PD.11 |
| 3 | 0x0008 | `0x400048E8` | PD.10 |
| 4 | 0x0010 | `0x400048F0` | PD.12 |
| 5 | 0x0020 | `0x40004918` | PE.6 |
| 6 | 0x0040 | `0x40004818` | PA.6 (grinder) |
| 9 | 0x0200 | `0x40004884` | PC.1 |
| 10 | 0x0400 | `0x4000488C` | PC.3 |
| 11 | 0x0800 | `0x40004888` | PC.2 |
| 13 | 0x2000 | `0x4000481C` | PA.7 |

Known bit assignments from the control tasks: bit6=grind active (`0x5006`), bit8=grinder running (`0x1A22`), bit1+9=brew (`0x505C` `orr #0x202`), bit4+9=hot water (`0x238C` `orr #0x210`), bit1+3=steam (`0x479E` clears `#0x210`), bit0=OTA active (`0x276E`).

> **Live correction 2026-11-16 (`HOTWATER.md`):** during actual hot water dispensing **bits 1+3+9 (`0x20A`)** are set — *not* bit4+9. **Bit 4 was never set** in any frame (6305 frames, two complete events). "bit0=OTA" and "bit8=grinder running" are furthermore **always set at idle** → the static labels are rejected as runtime semantics. `0x20A` is exactly the mask UART3 cmd 7 clears (see §4) → **cmd 7 is presumably hot water** (upgraded from low confidence); cmd 8's "bits 4,9 = hot water" association is therefore also uncertain.

> **Parity spec corrections 2026-11-17 (`PARITY.md`):** (a) **word 40** is also the "hot water temperature" shown in the original UI — a **read-only sensor**; no temperature setting for water exists. (b) **Words 112/113 are byte-packed:** 112=(preInfSwitch<<8)|pressure, 113=(preInfTime<<8)|breakTime, 114=headTemper (whole °C, MCU ×10). (c) **breakTime quirk:** the recipe function `0x1E10` only handles pressure 3/4, overwrites word13 and replaces breakTime with preInfTime → a separate breakTime has no effect in a standard brew. (d) **Safety:** trigger words **26/27/30 = 0** in normal blocks; **word30=1 reboots the MCU** (`0x2A06`). (e) brewMode is vestigial; sleepMode/sleepModeSetting have no UI editor. (f) boot push: sub1 len30 + sub112 len3 at app startup.

> **STEAMLIVE corrections 2026-11-17 (`STEAMLIVE.md`, proven live):** **Two steam paths with latch gating:** (1) UI start = cmd6 sub24 len8 word24=1 → the MCU latches and **auto-stops at word48 ≥ word25** (clears bits 5+14 + resets word24 itself; no stop write); (2) **physical wand/flush** = the PE.2 switch → word44 bit0=1 + word46 (flow meter steam) counts → the MCU sets `work_sta |= 0x4020` **WITHOUT word24=1** and does **NOT auto-stop**, even above 650 (momentary; stops when the wand is released). A flush sends **no UART frames**. Concurrent phases: brew+steam = work_sta **0x5327** (bits 1+2+9+12 ∪ 5+14); brew weight/flow undisturbed. Milk ramp 1.40 °C/s; overshoot +3.9 °C (~11 s of thermal inertia after the stop).

**Word 44 input bits ← GPIO** (status task `0x2970..0x299C`; names from debug `0x5EF` "In:bean,e,s,power,c1,c2"):
| Bit | GPIO | Name (debug) |
|---|---|---|
| 0 | PE.2 (`0x40004908`) | s (steam) |
| 1 | PA.15 (`0x4000483C`) | e (extract) |
| 2 | PC.8 (`0x400048A0`) | power |
| 3 | PE.7 (`0x4000491C`) | c2 |
| 4 | PC.15 (`0x400048BC`) | c1 |
| — | PD.5 (`0x400048D4`) | bean (not in word 44; read separately as a flow meter/grinder sensor) |

---

## 3. WRITE HANDLER (cmd 6) — semantics

**Dispatch** (`0x9C54`, in the RX parser `0x9AAC`):
```
9c54: ldrb r1,[r3,#3]      ; sub
9c56: ldrb r2,[r3,#4]      ; len (words)
9c58: adds r0,r1,r2
9c5a: cmp  r0,#0x21        ; sub+len < 33 ?
9c5c: bhs  9c68
9c5e: adds r3,r4,#5        ; payload
9c62: bl   99c6            ; normal write + ACK (words 0..31)
9c68: subs r0,#0x71        ; (sub+len)-113
9c6a: cmp  r0,#3           ; sub+len ∈ {113,114,115} ?
9c6c: bhs  9c80            ; otherwise error "06 error,addr=%d,num=%d" (0x9DA0)
9c6e: bl   99c6            ; extended write (words 112..114)
9c76: ldrb r0,[r7,#1]      ; [0x20000199]
9c78: cmp  r0,#2           ; if ==2 → reset flag
```
* **Allowed write bands:** `sub+len < 33` (words **0..31**) OR `sub+len ∈ {113,114,115}` (words **112..114**, head temperature/product setpoints). Everything else is rejected with the error log `0x9DA0` ("06 error,addr=%d,num=%d").
* **The write itself (`0x99C6`):** `memcpy(&table[sub], payload, 2·len)` (`0x99F6`→`0xDDA0`), then the ACK frame `AA 55 86 sub len <echo of the words> CRC` is built and sent (`0x88B2`). **No value range checks, no limits** — every 16-bit value is accepted.
* **ACK suppression:** if `[0x20000198]` ≠ 0 it is decremented and the ACK is skipped (`0x9A1C..0x9A28`).
* **Side effects:** the write does **not** drive the actuator directly. It only sets target/trigger words in the register file. The control tasks react asynchronously:
  * *Target values* (temperature, weight, gear): read continuously by the PID/control tasks.
  * *Trigger words* (consumed + reset): words 6/3 (grind tare), word 9 (brew tare), words 26/27 (calibration), word 30 (reboot).
  * *Recipe blocks* (sub 4 len 21/22, sub 5 len 18): stored; the NFC/product profile `0x1E10` can overwrite them.

---

## 4. UART3 PROTOCOL — binary `AA BB` (NOT ASCII!)

**Correction to protocol.md §1/§10:** UART3 (`0x40073000`, IRQ49, RX ring buffer `0x200002EC`) runs a **binary protocol**, not `@C/@R/@W`. The "ASCII parser @ ~0x2BC" was a misreading of Thumb instruction bytes (`40 57`=`@W`, `40 43`=`@C`, `40 52`=`@R` are random bytes in code @ `0x789B/0x82A5/0xBD71`). The real parser is **`0x43CC`** (called by the product task `0x4F16`).

**Frame format (both directions):**
```
AA BB CMD LEN PAYLOAD(LEN bytes) CRClo CRChi      (total LEN+7)
```
* `[0..1]` = header **`AA BB`** (the state machine waits for `0xAA`→`0xBB`, `0x440C/0x4422`).
* `[2]` = **CMD** (1..20 are validated @ `0x4438`: `cmd-1 < 20`).
* `[3]` = **LEN** = payload length in **bytes** (1..21, `0x4450`).
* `[4..]` = payload; **16-bit words are BIG-ENDIAN** (`rev16` @ `0x45B4/0x46A2/0x47D8`).
* CRC-16/MODBUS over `[0..3+LEN]`, **little-endian** on the wire (`0x44D6`→`0x98E8`, `rev16` of the received CRC @ `0x44E4`).
* Parser state @ `[0x200000BC+7]` (0..6), frame buffer `0x2000088C`, "frame ready" flag `[0x200000BC+6]=0x11` (`0x44EC`).

**Commands (TBB @ `0x44FC`, 22 cases):**
| CMD | Handler | Function |
|---|---|---|
| 1 | `0x4516` | **identity/echo**: write a byte to the sub-device, reply via `0x4398`; identity report `0x3C50` (firmware version `0x20000000`, display version `0x20000F82`, MCU UID `0x20000288`, serial number word96) |
| 2 | `0x4540` | **calibration/status report** `0x3CD8`: reads words 37,38,39,49,53,54,55,56,59,60,64,65,69,74,75 + set_voltage |
| 3 | `0x45AC` | set grind parameter: BE word payload → `[0x200000BC+0x14]` (becomes word 27 via the product task) |
| 4 | `0x45BA` | set byte param → `0x3E02` |
| 5 | `0x45C6` | **grind start/stop**: payload[4]→state byte 5; =0 → word33 &= ~0x40 (stop grinder, bit6); reply builder `0x3E6C` (resp CMD5) |
| 6 | `0x4616` | **brew/extract start/stop**: payload[4]→state byte 6; =0 → word33 &= 0xFDF9 (stop, bits 1,2,9); bytes 5,6,7,8 ×10 + BE words 9,11 ×10 → targets; reply `0x3ED4` (resp CMD6, tail `0x4704`) |
| 7 | `0x470A` | **control mode** (clears word33 bits 1,3,9 = 0x20A; state byte 7; reply `0x3F9C` resp CMD7). *Live upgrade [HOTWATER.md]: 0x20A = exactly the mask set during hot water dispensing → **presumably hot water** (med conf.)* |
| 8 | `0x476E` | **hot water start/stop**: payload[4]→state byte 8; =0 → word33 &= ~0x210 (stop, bits 4,9 = add_water bits); bytes/words ×10 → targets; reply `0x4004` (resp CMD8). *Live note: bit4 is never seen set during actual hot water → this association is uncertain; cmd7 (0x20A) is the better candidate.* |
| 9 | `0x481A` | **steam start/stop**: payload[4]→state byte 9; =0 → word33=1 (standby); 6 BE words ×10 → targets; reads GPIO PE.2; reply `0x40C4` (resp CMD9) |
| 16 | `0x48F4` | control (state byte 0xA) |
| 17 | `0x49B0` | control |
| 18 | `0x49D6` | control |
| 19 | `0x4A00` | control |
| 20 | `0x4AAA` | control |
| 10..15 | `0x4536` | default (no action) |

> **Mode identification:** the reply templates in the `0xF158` region (`0xF1A0`=CMD5, `0xF1B0`=CMD6, `0xF1CC`=CMD7, `0xF1DC`=CMD8, `0xF1EC`=CMD9) echo the request CMD. The cmd→mode association is established via (a) which `work_sta` bits are cleared on stop, and (b) the state bytes `[0x2000090B..0F]`. cmd8=hot water is unambiguous (clears bits 4,9 = exactly the add_water/hot water bits `0x238C`); cmd5=grind (bit6), cmd6=brew (bits 1,2,9) are unambiguous. cmd7/cmd9 are less clear-cut (see above).

* **×10 conversion:** payload values are in 0.1 units (g/°C) and are multiplied by 10 into internal units (`0x466A..0x46C2`: `add r1,r1,r1,lsl #2; lsls r1,#1` = ×10).
* **Responses:** built from templates @ `0xF158` (+ offsets) and sent via UART3 (`0x88B2`). ACK builder `0x4398`.
* **Error handling:** CRC error → the frame is discarded (state reset `0x44FA`/`0x4AD2`); invalid CMD/LEN → reset. No explicit NACK frame.
* **Interpretation:** the UART3 module is an **external scale/control unit** (or IoT/remote control module) that can start/stop grind/brew/steam/hot water with weight targets and read calibration + identity. It is **not** the display link (UART1).

---

## 5. MCU OTA (header `CC DD`) — state machine

**Two phases:** (a) the main RX parser (`0x9AAC`, status task) handles the **first cmd 1 request** and starts the "updata" task; (b) the "updata" task (`0x2768`→`0xBAA8`→`0xB646`) runs its **own `CC DD` parser** and handles cmd 1 (re-request) + cmd 2 (data). Both read the UART1 ring buffer `0x200002D8`; "updata" has a higher priority (5) than "status" (8) and runs a tight loop (`0x2778`: `0xBAA8` + WDT reset `0x158`) that consumes the OTA bytes first. *Note:* the main parser rejects `CC DD` cmd 2 (`0x9EBA` requires cmd 1), so the arbitration between the two parsers depends on scheduling — the exact locking is not fully mapped, but the "updata" task's dedicated parser is the one that writes the staging area.

**Frame format:** `CC DD CMD LENlo LENhi PAYLOAD(LEN bytes) CRClo CRChi` (LEN = 16-bit byte count, LE). CRC-16/MODBUS.

### 5.1 cmd 1 — OTA request (LEN=15)
`CC DD 01 0F 00 <size:u32 LE> <check:u32 LE> <version:7 bytes> CRC`
* Main parser `0x9F44`: parses size→`[0x20001238]`, check→`[0x2000123C]`, version→`[0x2000122F]`; calls **`0xB470`** (validation).
* **`0xB470` validation:**
  * The flash size `[0x20000018]` must be `0x80` (128 K) or `0x100` (256 K), otherwise error.
  * Parses the serial number word96 (byte0='B'→offset 5, byte2 letter/digit) → the version number must be ≥ 26 (`0xB4C6`), otherwise error.
  * Reads the boot version from flash `0x100DCC` (`0x911E`), compares with **"V120"** (`0x30323156`, `0xB4F0`); error → "V120boot version error" (`0xB858`).
  * Size check: 128 K → size ≤ `0xF800`; 256 K → size ≤ `0x18000` (`0xB500/0xB50C`), otherwise "ota_e3/e4,size" (`0xB874/0xB88C`).
  * Compares the version with the expected one (`0xB51C..`); returns **status `0x81`=accept** (`0xB526`).
* **Flash erase** (staging, `0x9FC0`/`0xB7DA`):
  * 128 K: erase `0xF800` (len `0x8800`), `0x18800` (len `0x1000`), `0x1A000` (len `0x6000`). Staging start `[0x20001240]=0xF800`.
  * 256 K: erase `0x20000` (len `0x18000`). Staging start `0x20000`.
  * (`0x8EC8` = FMC page erase, 2 KB pages.)
* **ACK cmd 1** (`0xB5A2`): `CC DD 01 09 00 <size:u32 LE> <check:u32 LE> <status:u8> CRC`. `status=0x81`=accept.
* **Start the "updata" task** (`0xA01A`→`0x2A40("updata")`); the task sets word33 bit0 (`0x276E`).

### 5.2 cmd 2 — OTA data (LEN=n+2, n≤256)
`CC DD 02 <LENlo> <LENhi> <seq:u16 LE> <data[n]> CRC`
* Parser in the "updata" task **`0xB646`** (state machine `[0x200001BC+1]`, 0..7): waits for `0xCC`→`0xDD`→CMD(1/2)→LENlo→LENhi→payload→CRClo→CRChi; CRC check (`0x98E8`, `rev16`); dispatch cmd2→**`0xB90C`**.
* **`0xB90C` data handler:**
  * **seq check:** received seq (`[r7+8]`) vs expected seq (`[0x20001236]`). Mismatch → status 0, log "rx id error,0x%x 0x%x" (`0xBB54`), ACK with the expected seq (the display retransmits).
  * **Length:** total byte count < `0x108` (264) → status **2** (last/partial packet); otherwise status **1** (full 256-byte packet).
  * **FMC write** (`0x9008`): dest = staging pointer `[0x20001240]`, src = data (`0x2000112A`), len = LEN16−2. Write error → status 0 ("addr=%x,write=%x,check_write=%x", `0x91FC`).
  * **On success** (`0xB96E`): expected seq++; staging pointer += len; **fragmented staging jumps:** pointer==`0x18000`→`0x18800`, ==`0x19800`→`0x1A000` (`0xB990..0xB9A4`); accumulated += len; **pct = accumulated·100/size** (capped at 99 until done, `0xB9C4`).
  * **When accumulated == size** (`0xB9CC`): **checksum verification** — byte sum of the staging fragments via `0xB414`, compared with the received check (`[0x2000123C]`). Match → status 1, pct=100; mismatch → "ota check sum error,cal=0x%x rx=0x%x" (`0xBB8C`).
  * **ACK cmd 2** (`0xB604`): `CC DD 02 04 00 <seq:u16 LE> <status:u8> <pct:u8> CRC`.

### 5.3 Completion + reboot (`0xBAA8`)
* When pct==100 (`0xBAB4`): send the last cmd2 ACK (seq−1, status 1).
* **Write OTA metadata** to flash `0x19800` (erase `0xC` bytes first): **magic `0x87654321`** @ `0x19800`, **size** @ `0x19804`, **check** @ `0x19808` (`0xBAD6..0xBAEA`). Log "ota:addr=%x,size=%d,check=%x" (`0xBBFC`).
* Unlock REGWPCTL (`0xBB34`), enable FMC ISP (`FMC+0 |= 2`, `0xBAFE`), delay 1000 ms, **system reset** `SCB->AIRCR=0x05FA0004` (`0xBB1E..0xBB2A`).
* **On reboot:** the bootloader copies staging → app (magic-based). **Note:** the staging → app copy itself is **not in this app binary** (no read of `0xF800`/the magic found in the app code) — it is performed by the bootloader (LDROM / boot region). The app has a separate boot flag check @ `0x18C` that reads `0x19800` and compares it with **`0x12345678`** (a different magic, "ota_flag=%x,size=%d,check=0x%x" `0x218`) — this is a separate handshake, not the OTA copy.
* **On interruption:** the staging area is fragmented and sequence-numbered; an interrupted OTA leaves incomplete staging + no magic @ `0x19800` → the bootloader starts the existing app. The display retransmits per packet (seq ACK). There is no rollback mechanism in the app code.

---

## 6. Appendix — naming strings (dictionary for the register map)

Full extract: `artifacts/mcu_analysis/strings.json` (230 strings) and `strings_appendix.json` (159 filtered). Key strings with address + xref:

| Address | String | Associated register/function |
|---|---|---|
| `0x5D7` | `E1=%x,E2=%x,E3=%x` | words 34/35/36 (errors) |
| `0x5EF` | `In:bean=%d,e=%d,s=%d,power=%d,c1=%d,c2=%d` | input bits (word 44 + PD.5) |
| `0x61F` | `work_sta=%x,e_w_flag=%d,s_w_flag=%d,T_Coun=%d` | word 33 + water level flags |
| `0x657` | `self:mode=%d,sta=%x,result=%x,%x,%d` | words 8/43/73/33/51 |
| `0x9CEB` | `grind: mode=%d,auto_weight=%d,over_tiome=%d` | words 2/4/5 |
| `0x9D20` | `hot: auto=%d,weight=%d,sw=%d` | words 16/17/19 |
| `0x9D44` | `steam: gear=%d,milk_set_temp=%d` | words 14/25 |
| `0xF608` | `extract: set_temp=%d,head_set_temp=%d,auto_weight=%d,pro_sw=%d` | words 11/114/23/112 |
| `0xF64C` | `temp:milk=%d,s_boilert=%d,e_boilert=%d,e_head=%d,Mboard=%d,Dboard=%d` | words 131/130/129/128/71/72 |
| `0xA530` | `adc:milk=%d,s_boilert=%d,e_boilert=%d,e_head=%d` | raw ADC (0x20000E48) |
| `0xF694` | `vol=%d,hz=%d,cur_ch1=%d,power_p1=%d,cur_ch2=%d,power_p2=%d,work_sta=0x%x` | words 49/61/50/58/33 |
| `0xF6E0` | `%s: adc=%d,fil_adc=%d,offer_adc=%d,weight=%d,offer_weight=%d,G_k=%d,...` | weight/calibration (grind/extract) |
| `0xF4DC` | `add_water:step=%d,time=%d,old_time=%d,work_sta=%x,flowmeter=%d,%d,%d` | words 20/33 + flow meter |
| `0xF524` | `extract_heat:temp=%d,set_temp=%d,power=%d,work_sta=0x%x,...flow_velocity=%d,...` | words 11/62/33/52 |
| `0xF598` | `steam_heat:temp=%d,set_temp=%d,power=%d,work=0x%x,...` | words 25/63/33 |
| `0xAD8/0xAEC/0xB07` | `set.voltage/extract_k/grind_k = %d` | words 67/56/54 (config) |
| `0xB1C/0xB34/0xB44/0xB58/0xB70/0xB88` | `set.gears_min/5/25/max/fu4/40 = %d` | words 59/60/64/65/74/75 |
| `0xC28..0xCC0` | `APP Param / flag / set_voltage / extract_k / grind_k / grind: min/5/25/max / CRC16 / -4 / 40` | config dump (chip info `0x868`) |
| `0xC18` | `\nSerial:%s` | serial number words 96..111 |
| `0xCC8` | `B000000-0000-0000` | default serial number |
| `0xAE1B/0xAE38` | `grind weight config %x / extract weight config %x` | weight calibration |
| `0xB1BF/0xB1D8` | `Extract calibration. / Grind calibration.` | tare/calibration (`0xAebc`) |
| `0xB398` | `%s:adc=%d,adc_fil=%d,flag=%d` | water level (e_water/s_water) |
| `0x9D83/0x9DA0` | `03 error,addr=%d,num=%d / 06 error,...` | read/write range errors |
| `0xA047` | `new time: %d.%d.%d-%d:%d:%d %d.%d:%d` | cmd 8 time sync |
| `0xB844/0xB858` | `boot version:%s / V120boot version error` | OTA boot version "V120" |
| `0xB874/0xB88C` | `ota_e3,size=0x%x / ota_e4,size=0x%x` | OTA size errors |
| `0xBB54/0xBB6F/0xBB8C/0xBBB4` | `rx id error / update percent / ota check sum error / ota crc err` | OTA seq/pct/checksum/CRC |
| `0xBBFC` | `ota:addr=%x,size=%d,check=%x` | OTA metadata @0x19800 |
| `0x218` | `ota_flag=%x,size=%d,check=0x%x` | boot flag check @0x18C |
| `0x91D0/0x91EC/0x91FC` | `erase failure / erase success / addr=%x,write=%x,check_write=%x` | FMC flash |
| `0xBE8` | `Flash = %dk,Sram = 32k` | chip info |
| `0xBB7` | `MEM03` | board name |
| `0xCD94` | `Sep 23 2026` | build date |

---

## 7. Corrections to protocol.md

1. **UART3 protocol (§1, §10):** protocol.md stated "`@C`/`@R`/`@W` ASCII parser @ ~0x2BC". **Wrong.** UART3 runs a binary `AA BB CMD LEN payload CRC16` (parser `0x43CC`, payload words big-endian). "@C/@R/@W" were Thumb instruction bytes mistaken for ASCII. "presumably a weight/scale module" is partly confirmed: it is a scale/control module with grind/brew/steam/hot water control + calibration + identity.
2. **Words 112..114 (§5.1):** protocol.md stated "OTA control". **Wrong.** They are **head temperature/product setpoints** (word 114=`head_set_temp` read by head_heat, word 112=`pro_sw`, word 113=brew switch). The cmd 6 special case `sub+len∈{113,114,115}` is the **extended write band** for these, not OTA. OTA runs exclusively via `CC DD` (§5).
3. **Words 14/25 (§5.1):** protocol.md guessed "pre-infusion pressure" (word 14) and "steam target" (word 25). The MCU's own debug output (`0x9920`) calls word 14 **"steam gear"** and word 25 **"milk_set_temp"**. Word 14 is, however, multiplexed (the NFC recipe `0x1E10` writes temperature-like values).
4. **Register file size:** 129 protocol words (0..128), but the MCU uses **internal words 128..131** as a temperature array (word 128=e_head, 129=e_boiler, 130=s_boiler, 131=milk) — beyond the 129.

---

## 8. Verification status & artifacts

| Layer | Method | Confidence |
|---|---|---|
| Register base + 129 words | literal refs + memset/init `0x9966` | **high** |
| Words 0..75, 96..114, 128 semantics | constant propagation + strings + task roles | **high/med** (see per word) |
| Words 10/12/21/31/32/76..79/115..127 | no individual access found → reserved/internal | **low** (negative evidence) |
| cmd 6 write semantics | full disassembly `0x99C6` + dispatch `0x9C54` | **high** |
| UART3 `AA BB` protocol | parser `0x43CC` + TBB + handlers | **high** |
| OTA `CC DD` state machine | `0xB470/0xB646/0xB90C/0xBAA8` + FMC | **high** |
| OTA staging → app copy | not in the app binary → bootloader (LDROM) | **med** (cannot be seen directly) |
| Actuator GPIO map (word 33) | run task `0x2782` | **high** |

**Intermediate artifacts (in `artifacts/mcu_analysis/`, not part of this repository):**
* `disasm.py` / `disasm.pkl` — recursive disassembly (25,117 instructions, 19,855 reachable, 21 TBB/TBH tables, literal pool + MOVW/MOVT).
* `regrefs.py` / `regrefs.pkl` / `byidx.json` — constant propagation, 1,763 register accesses, 78 indices resolved.
* `regrefs2.py` / `regrefs2.pkl` — aggressive union scan (verification; false positives from writeback bases noted).
* `dump.py` — annotated disassembly (`python dump.py <start_hex> <end_hex>`).
* `register_evidence.txt` — per-index access summary.
* `strings.json` / `strings_appendix.json` — all strings + xrefs.
* `fn_*.txt`, `rx_parser.txt`, `product_task.txt`, `status_task.txt`, `tasks_init.txt`, `cal_writers.txt` — function dumps.

**Reproduction:**
```bash
cd artifacts/mcu_analysis
../../.venv/bin/python disasm.py        # builds disasm.pkl
../../.venv/bin/python regrefs.py       # builds byidx.json
../../.venv/bin/python dump.py 9966 9a3a  # register init + write handler
```

*— research sub-agent, 2026-11-16. Static analysis; no devices touched. All addresses can be verified in `MEM03_V514_260923.bin` (MD5 e96155dad2443063acb9ecea959854a3).*
