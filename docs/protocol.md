# PROTOCOL — MerakiTech Gen 1: UART protocol display ↔ powerMcu + chip ID

> **Note (public edition):** This document consists of development notes from reverse engineering a MerakiTech Gen 1 owned by the author. The manufacturer's firmware, binaries and scripts referred to (e.g. `demo_v9`, `MEM03_V514_260923.bin`, `demo.sh`, `test.sh`) are **not** included in the repository. Addresses and short excerpts are included solely for interoperability. IP addresses and other personal data have been removed. See [README → Disclaimer](../README.md#️-disclaimer--read-this-before-you-do-anything). References to `UARTLIVE.md`, `HOTWATER.md`, `SESSION.md` and `WRITETEST.md` are to the author's capture notes; their results are summarized inline.

**Date:** 2026-11-16 · **Method:** purely STATIC reverse engineering (binaries + datasheets/web). No contact with the device.
**Artifacts:** `artifacts/demo_v9` (display app, ARMv7 ELF, not stripped, MD5 `d44c2414…`) and `artifacts/MEM03_V514_260923.bin` (powerMcu fw 5.1.4, raw Cortex-M, MD5 `e96155da…`).
**Tools:** Apple LLVM `objdump` (ARM/Thumb), `capstone` (Thumb-2 recursive disassembly), `pyelftools`. All addresses below have been verified in the binaries.

---

## 0. Summary (TL;DR)

* **Physical:** UART **57600 baud, 8N1**, no flow control. Display = SigmaStar SSD202 `/dev/ttyS1`; MCU = **Nuvoton M451 UART1** on **PE.8 (TXD) / PE.9 (RXD)**.
* **Protocol:** binary register sync with header `AA 55`, a command byte, sub/register offset, length in 16-bit words and **CRC-16/MODBUS**. The MCU replies with command `| 0x80`.
* **MCU OTA:** separate header `CC DD`, length in bytes, sequential 256-byte data packets, flash staging + reboot.
* **Chip:** **Nuvoton NuMicro M451** (ARM **Cortex-M4F @ 72 MHz**, 32 KB SRAM, 128/256 KB flash). **Not** M031/M032 (those are M0/M0+ without FPU/Thumb-2), **not** GD32/HC32. The chain of evidence in §8 is unambiguous (SystemInit is byte for byte the M451 BSP's, including the PDID check `== 0x945`).
* **Both sides initiate:** the display polls with read queries (cmd 3) every ~100 ms; the MCU broadcasts status (cmd 0x83) every ~100–200 ms and asks for the display's version (cmd 0x89) every ~20 ms until it gets a reply. *(Live note 2026-11-16: in two captures — idle [UARTLIVE.md] and a Hot Water activity [HOTWATER.md] — **no unsolicited 0x83 broadcasts** were observed; all status came via the display's poll. The broadcast code exists in the binary, but the trigger condition has not been identified. The version handshake only happens at boot.)*

---

## 1. Physical layer

| Parameter | Value | Source (display) | Source (MCU) |
|---|---|---|---|
| Baud rate | **57600** | `set_opt(fd, 57600, 8, 'N', 1)` @ `0x146790` | `UART_Open(UART1, 57600)` @ `0x2C2A` (baud constant `0xE100` = 57600) |
| Data bits | 8 | `set_opt` CS8 | UART_Open WLS=8 |
| Parity | None (`'N'`=78) | `set_opt` clear PARENB | UART_Open |
| Stop bits | 1 | `set_opt` clear CSTOPB | UART_Open |
| Flow control | none | raw termios, IXON/RTS off | none |
| Port (display) | `/dev/ttyS1`, `open(...,0x902)` = `O_RDWR\|O_NOCTTY\|O_NONBLOCK` | `open_port(1)` @ `0x144D60` | — |
| Port (MCU) | **UART1** @ `0x40071000`, IRQ37 | — | literal @ `0x2E00`, vector `[37]` |
| MCU pins | **PE.8 = UART1_TXD, PE.9 = UART1_RXD** (MFP=1) | — | `GPE_MFPH` (SYS+0x54) `=0x11` @ `0x8944` |
| MCU clock | HCLK **72 MHz** (PLL from 12 MHz HXT); UART clock from PLL | — | `SystemCoreClock=72000000` @ `0x748` |
| Read mode | non-blocking (`VMIN=0,VTIME=0`), poll loop | `set_opt` | RX FIFO + IRQ → ring buffer |

**Baud verification on both sides:** the display calls `cfsetispeed/cfsetospeed(B57600=0x1001)`; the MCU computes the divisor in `UART_Open`/`0x86C2` from the UART clock (PLL) and `CLK_CLKDIV0`. Both end up at 57600. There is **no** 921600/115200 on the link itself (the string "921600" in demo_v9 belongs to another UART context).

> The MCU's second UART, **UART3** (`0x40073000`, IRQ49), runs a *separate binary* `AA BB CMD LEN payload + CRC16` protocol (payload words **big-endian**, parser @ `0x43CC`, called by the product task `0x4F16`) — a scale/control module with grind/brew/steam/hot-water start/stop + calibration + identity. It is **not** the display link. *(Correction 2026-11-16: originally given as `@C`/`@R`/`@W` ASCII @ `0x2BC` — those were Thumb instruction bytes mistaken for ASCII. Full mapping in [registermap.md](registermap.md) §4.)*

---

## 2. Frame format

### 2.1 Normal frame (both directions, header `AA 55`)

```
Offset:  0    1    2    3    4    5 … 5+2L-1         5+2L   6+2L
Byte:    AA   55   CMD  SUB  LEN  PAYLOAD (L words)  CRClo  CRChi
```

* `[0..1]` = header **`AA 55`**.
* `[2]` = **CMD** (command byte).
* `[3]` = **SUB** = register word offset / subcommand.
* `[4]` = **LEN** = payload length in **16-bit words** (little-endian words).
* `[5 … 5+2·LEN−1]` = payload = `LEN` 16-bit little-endian words.
* `[5+2·LEN]`, `[6+2·LEN]` = **CRC-16/MODBUS**, little-endian (low byte first).
* **Total length = `2·LEN + 7` bytes.** The CRC covers bytes `[0 … 4+2·LEN]` (i.e. `2·LEN+5` bytes).

**Hex example — the display switches the machine on (cmd 6, write register 0 = 1):**
```
AA 55 06 00 01 01 00 C5 5F
│header│cmd│sub│len│word0=1│CRC=0x5FC5 (lo C5, hi 5F)
```

**Hex example — display read query (cmd 3, sub 0, len 56 words, no payload):**
```
AA 55 03 00 38 DC 06        (7 bytes; LEN=0x38=56 words expected in the reply)
```

### 2.2 Exception: version frames (cmd 9 / 0x89) — LEN is in *bytes*

The version strings are ASCII, so here `LEN` is a **byte** count (not words), and the payload is a string:
```
AA 55 09 LENbyte 00 <ASCII string, LENbyte bytes> CRClo CRChi   (total LEN+7)
```
The MCU's fixed version request (cmd 0x89) is stored as a constant @ `0x9D70`:
```
AA 55 89 01 00 00 6F 81      (LEN=1 byte, sub=0, payload=0x00, CRC=0x816F)
```
The display replies with cmd 9 + its `softversion` ("1.1.1-69"). CRC over `AA 55 89 01 00 00` = **0x816F** → wire `6F 81` (verified against the binary).

### 2.3 Exception: power/time frame (cmd 8 / 0x88) — fixed 15 bytes

```
AA 55 08 <10 bytes payload: u32, u32, u16> CRClo CRChi   (15 bytes)
```
Payload (10 bytes) = `year lo, year hi, month 1-12, day, hour, minute, second, alarm day, alarm hour, alarm minute` — the display's **local** time and the wake alarm. The MCU echoes it as cmd 0x88, sets its RTC from the date/time bytes and keeps the alarm in RAM (0x20000E18; parser @ `0x9C88`, log "new time: …"). Its sleep task (0x2F52) resets the CPU — wakes the machine — when RTC day/hour/minute equal the alarm and day+hour+minute ≠ 0. The stock UI sends cmd8 only at power-off (`powerOffSend` 0x14700C) with the next scheduled pre-heat, or 0/0/0. See [time-scheduling.md](time-scheduling.md).

### 2.4 OTA frame (header `CC DD`) — see §7

```
CC DD CMD LEN16lo LEN16hi PAYLOAD (LEN16 bytes) CRClo CRChi   (total LEN16+7)
```
Here the length is a **16-bit byte count** in `[3..4]` (not words, not in `[4]`).

---

## 3. Checksum algorithm (CRC-16/MODBUS)

Verified in **both** binaries:

* Display: `crc16_modbus` @ `0x144798` (bitwise) and `CRC_GetModbus16` @ `0x144914` → `GetCrc_16` @ `0x144878` with the table `g_McRctable_16` @ `0x18BFA0`. The table is **identical** to the standard CRC-16/MODBUS table (all 256 entries verified).
* MCU: `0x98E8` (bitwise, init `0xFFFF`, polynomial `0xA001`).

**Algorithm:**
```
crc = 0xFFFF
for each byte b in the payload (from byte 0 up to and including the last payload byte, BEFORE the CRC field):
    crc ^= b
    repeat 8 times:
        if crc & 1:  crc = (crc >> 1) ^ 0xA001
        else:        crc = crc >> 1
return crc            # 16-bit, no final XOR
```
* Polynomial `0x8005` reflected = **`0xA001`**, init **`0xFFFF`**, refin/refout = yes, xorout = 0.
* Wire order: **little-endian** (low byte first, high byte last).
* The MCU's CRC function returns the value byte-swapped internally (`(crc<<8)|(crc>>8)` @ `0x9916`), and the RX parser does a `rev16` on the received CRC field (`0x9F34`) — so **both ends send/read little-endian on the wire**. The display stores with `strh` (LE). Consistent.

**Python:**
```python
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc
```

> Note: the OTA request's *firmware* checksum (`calculateChecksum` @ `0x1474C8`) is **not** a CRC, but a simple **32-bit sum of all bytes** in the .bin file. It is sent in the cmd 1 payload. The OTA frames themselves still use CRC-16/MODBUS.

---

## 4. Command table

Pattern: **the MCU's reply = display command `| 0x80`**. Register data is exchanged as 16-bit words in a shared register file (§5).

### 4.1 Display → MCU

| CMD | Name | SUB | LEN | Payload | Meaning |
|---|---|---|---|---|---|
| `0x03` | read query | start word | number of words | *none* (7-byte frame) | Read `LEN` register words from `SUB`. The MCU replies with cmd `0x83`. |
| `0x06` | write registers | start word | number of words | `LEN` words | Write register words. The MCU writes + ACKs with cmd `0x86`. Retransmit until ACK. |
| `0x08` | power/time sync | — | — | 10 bytes (u32,u32,u16) | Machine time/status. The MCU ACKs with cmd `0x88`. Fixed 15-byte frame. |
| `0x09` | version reply | 0 | byte count | ASCII string | Reply to the MCU's `0x89` request (the display's `softversion`). |
| `0x01`* | OTA request | — | 15 | size,check,version | **CC DD** — see §7. |
| `0x02`* | OTA data | — | n+2 | seq,data | **CC DD** — see §7. |

\* OTA commands use the `CC DD` header, not `AA 55`.

### 4.2 MCU → display

| CMD | Name | SUB | LEN | Payload | Meaning |
|---|---|---|---|---|---|
| `0x83` (131) | register data | start word | number of words | `LEN` words | Reply to a cmd 3 query **or** an unsolicited status broadcast (sub=32, len=80). |
| `0x86` (134) | write ACK | start word | number of words | echo of the written words | Acknowledgement for cmd 6. Releases the display's retransmit (`_fuart_revice_mark`). |
| `0x88` (136) | power/time ACK | — | — | echo | Acknowledgement for cmd 8. |
| `0x89` (137) | version request | 0 | 1 | `0x00` | Fixed frame `AA 55 89 01 00 00 6F 81`. The display must reply with cmd 9. |
| `0x01`* | OTA request ACK | — | 9 | size,check,**status** | **CC DD**. `status=0x81` = accept. |
| `0x02`* | OTA data ACK | — | 4 | seq,status,pct | **CC DD**. `status=1`=OK, `pct`=progress %. |
| `0x03`* | OTA flash/verify done | — | — | status | **CC DD**. `buf[9]==1` = success. |
| `0x04`* | OTA reboot notification | — | 2 | bar,flag | **CC DD**. `buf[6]==1`, `buf[5]`=bar. |

### 4.3 Display-side sender functions (demo_v9)

| Function | Address | Role |
|---|---|---|
| `open_port` | `0x144D60` | opens `/dev/ttyS{index}` |
| `set_opt` | `0x144950` | termios: baud/parity/stop bits/raw |
| `usart_send` | `0x146D5C` | builds the cmd 6 write frame in `buf_tx`, sets up retransmit |
| `usart_ckeck` | `0x146E70` | builds the cmd 3 query frame (7 bytes) |
| `PowerOn/PowerOff` | `0x146EF8/0x146F34` | cmd 6 sub 0 = 1/0 |
| `MachSleep/MachAwaken` | `0x146F70/0x146FAC` | cmd 6 sub 1 = 0/1 |
| `powerOffSend` | `0x14700C` | cmd 8 (15-byte) frame |
| `verSend` | `0x1471A0` | cmd 9 version reply |
| `check` | `0x146164` | **RX parser** for `AA 55` (normal) |
| `ota_check` | `0x147AA4` | **RX parser** for `CC DD` (OTA ACKs) |
| `fuart_thread` | `0x14671C` | 10 ms TX/RX loop |
| `uartRxProcess` | `0x144E20` | 60 ms LVGL timer, UI update from the register mirror |

---

## 5. Register file (the shared "register bank")

The protocol is a **mirrored register file**: the display and the MCU each keep a copy of the 16-bit words, and cmd 3/6/0x83 synchronize them.

* **MCU:** register file = the "APP Param" struct in RAM at **`0x20000F96`**, **129 words** (index 0..128). Initialized @ `0x9966`.
* **Display:** mirror at `machine_struct + 0xFE8` (word offset `sub` → `machine + 0xFE8 + 2·sub`). `usart_send` reads from here; `uartRxProcess` writes here (`memcpy` src = RX payload `0x1E0525`).
* **Validation:** the MCU rejects cmd 3 if `sub+len ≥ 0x81` (129). For cmd 6: `sub+len < 0x21` (33) → normal write + ACK; i.e. **the display can write words 0..31**. `sub+len ∈ {113,114,115}` → an extended write band for **head temperature/product setpoints** (words 112..114; special ACK). Everything else is rejected. **Words 32..111 are MCU-owned status** (the display reads them but does not write them). *(Correction 2026-11-16: words 112..114 are NOT OTA control — OTA runs exclusively via `CC DD`. See [registermap.md](registermap.md) §3.)*

### 5.1 Known register words (word index = SUB)

Supported by display call sites + MCU defaults + `config.json`. Values are typically in **tenths** (870 = 87.0 °C, 185 = 18.5 g).

| Word | Field | Default (MCU) | config.json | Source |
|---|---|---|---|---|
| 0 | **power** (1=on, 0=off) | 1 | — | `PowerOn/PowerOff` (cmd6 sub0) |
| 1 | **sleep/drain** (1=sleep/drain, 0=awake) | — | sleepMode | `MachSleep/MachAwaken` (cmd6 sub1). *(Live correction 2026-11-17 [SESSION.md]: the values were swapped in the first edition — active captures (v2/v3) have word1=0, the sleep capture has word1=1 + the drain task. **merakiplus v3/v4's keep-awake wrongly writes 1 and sends the MCU to sleep; v5 must write 0.**)* |
| 2 | **grind mode** | 1 | grindMode | `grinderScaleUpdate` (cmd6 sub2 len2) |
| 4 | **grind auto_weight** (0.1 g) | 180 | autoGrindWeight=185 | MCU default `[8]=180`; "grind: mode,auto_weight" |
| 5 | grind over_time | 60 | — | MCU default `[0xA]=60` |
| 11 | **brew temperature** (0.1 °C) | 920 | brewTemper=870 | MCU default `[0x16]=920` |
| 14 | **pre-infusion pressure** (bar) | 4 | preInfusionPressure=4 | MCU default `[0x1C]=4` |
| 17 | **brew weight** (0.1 g) | 360 | autobrewWeight=360 | MCU default `[0x22]=360` |
| 23 | (brew-related) | 360 | — | MCU default `[0x2E]=360` |
| 25 | (steam-related) | 550 | AtuoSteamTarge=650 | MCU default `[0x32]=550` |
| 34 | **E1 error code** | — | — | display `GetE1tate`; MCU "E1=%x,E2=%x,E3=%x" |
| 35 | **E2 error code** | — | — | as above |
| 36 | **E3 error code** | — | — | as above |
| 112 | **pro_sw** (product switch; high byte) | — | — | debug `0x9936` "pro_sw=%d"; the NFC recipe reads the hi/lo byte |
| 113 | **brew/product switch** | — | — | the NFC recipe copies hi→lo; cmd6 sub113 |
| 114 | **head_set_temp** (brew head setpoint, 0.1 °C) | 50 | — | debug `0x993E`; init `0x9990`; the `head_heat` PID reads it 7× |

**Other write call sites (display):** sub 3 (len1/4), sub 4 (len1/3/21/22 = brew recipe/NFC profile), sub 5 (len18), sub 6, sub 8, sub 9 (len1/11 = hot water), sub 16/19 (hot water start/stop), sub 18 (len14 = steam), sub 20 (page_select), sub 21 (grindability), sub 23/25 (goto brew/steam), sub 24 (len8), sub 26–31. These are control/recipe fields; the exact per-word semantics require deeper UI analysis, but they all lie within the write band 0..31. *(Live verification [HOTWATER.md]: Hot Water start = **one cmd 6 write `sub 9, len 11`** (words 9..19) with word9=1 (start trigger), word16=1, word17=400 (40.0 g target), word19=0. ACK after ~22–25 ms. **No stop write** — the MCU stops autonomously at the target weight; the "sub 16/19 start/stop" guess is not seen in practice.)* *(Steam correction 2026-11-17 [SESSION.md]: **steam start = cmd6 sub24 len8 with word24=1**, manual stop = word24=0; the MCU auto-stops at word48 ≥ word25 and clears work_sta **bits 5+14 (0x4020)** — not bits 1+3 as guessed. "sub 18 len14 = steam" was wrong: sub18 len14 is a mirror sync.)*

### 5.2 Status broadcast (MCU → display)

The MCU sends an unsolicited **cmd 0x83, SUB=32, LEN=80** (words 32..111) — a status block with temperatures, weights, errors and state flags. *(Live note: not observed in the v1/v2 captures — see TL;DR. The contents are, however, fully verified via poll replies.)* Contents (from MCU strings):
* `adc:milk=%d,s_boilert=%d,e_boilert=%d,e_head=%d` → milk, steam boiler, extraction boiler and extraction head temperatures.
* `E1/E2/E3` (words 34/35/36), `work_sta`, `e_w_flag`, `s_w_flag`, `T_Coun`, `In:bean,e,s,power,c1,c2`.
* The display reads the error words and shows the error UI (`uartRxProcess` → `GetE1tate(1)/(32)`).

The display also polls the whole register file itself with cmd 3 queries: `(sub0,len56)`, `(sub33,len7)`, `(sub56,len56)` — together words 0..111.

---

## 6. Timing / who initiates

| Actor | Interval | Action | Source |
|---|---|---|---|
| Display `fuart_thread` | **10 ms** (`usleep(10000)`) | read ≤ 32 bytes → ring buffer → parse; TX arbitration | `0x146D2C` |
| Display `uartRxProcess` | **60 ms** (LVGL timer) | update the UI/mirror from parsed frames | `lv_timer_create(...,60)` @ `0x1467F0` |
| Display read queries | **100 ms** (every 10th loop) | cmd 3, rotating (sub0,len56)/(sub33,len7)/(sub56,len56)/(sub33,len7) | `0x146A94` (loop counter compared with 9) + switch @ `0x146B14` |
| Display cmd 6 retransmit | 10 ms, up to **20 times** (200 ms) | until the cmd 0x86 ACK | retry=20 @ `0x146E30`; ACK clear @ `0x146650` |
| MCU status task | **10 ms** (`rt_thread_delay(10)`, tick=1 ms) | RX parse + sensor loop | `0x2A38` |
| MCU status broadcast | **100 ms** (active) / **200 ms** (idle) | cmd 0x83 sub32 len80 | counter 10/20 @ `0x270E/0x2738`, send @ `0x2744` |
| MCU version request | **20 ms** (every 2nd loop) until a reply | cmd 0x89 | `0x2916` (bit0) → `0x9A94` |
| MCU other sensor tasks | 40/50/100/200/300/500/1000 ms | ADC, scale, heating | modulo counter @ `0x2934…0x29C8` |

**Conclusion:** *both* sides initiate. The display is the master for **writes** (cmd 6, with ACK/retransmit) and polls with cmd 3; the MCU is the master for **status push** (cmd 0x83) and the version handshake (cmd 0x89). There is no single "request/response" master — it is an asynchronous register sync link. *(Parity spec 2026-11-17: **boot push** — `fuart_thread` sends **sub1 len30 + sub112 len3 at startup**, so config values are synced to the MCU at every app restart; this corrects WRITETEST's "nothing is sent at boot".)*

**MCU RT-Thread tick:** SysTick reload = **71999** @ `0x750`, `SystemCoreClock=72 MHz` → **1 ms tick** (`RT_TICK_PER_SECOND=1000`). `rt_thread_delay(10)` = 10 ms.

---

## 7. MCU OTA protocol (header `CC DD`)

### 7.1 Frame format
```
CC DD CMD LENlo LENhi PAYLOAD (LEN bytes) CRClo CRChi      (total LEN+7)
```
`[3..4]` = 16-bit **byte** length (LE). CRC-16/MODBUS over `[0 … 4+LEN]`.

### 7.2 Commands

**Display → MCU:**
* **cmd 1 — OTA request** (LEN=15):
  ```
  CC DD 01 0F 00 <size:u32 LE> <check:u32 LE> <version:7 bytes> CRC
  ```
  * `size` = firmware size (63460 for 5.1.4).
  * `check` = **32-bit sum of all bytes** in the .bin (e.g. 5.1.4 = `0x00662BE4`).
  * `version` = "V5.1.4" (6 characters + null = 7 bytes).
* **cmd 2 — OTA data** (LEN=n+2, n≤256):
  ```
  CC DD 02 <LENlo> <LENhi> <seq:u16 LE> <data[n]> CRC
  ```

**MCU → display (ACKs):**
* **cmd 1 ACK** (LEN=9): `CC DD 01 09 00 <size:u32> <check:u32> <status:u8> CRC`. **`status=0x81` = accept** (the display reads `buf[13]==0x81`).
* **cmd 2 ACK** (LEN=4): `CC DD 02 04 00 <seq:u16> <status:u8> <pct:u8> CRC`. `status=1`=OK, `pct`=progress % (the display reads `buf[7]==1`, `buf[8]`=pct, `buf[5..6]`=seq).
* **cmd 3 — flash/verify done** (display: `buf[9]==1` → success).
* **cmd 4 — reboot notification** (LEN=2, display: `buf[6]==1`, `buf[5]`=bar).

### 7.3 Flow (verified on both ends)
1. The display downloads `MEM03_V514_*.bin` from the OTA server, MD5 check (`mcu_local_md5_check`).
2. `star_mcu_ota` → `mcu_ota_request("/customer/mcu.bin","V5.1.4")` (`0x14792C`).
3. `request_update` (`0x147314`) builds **CC DD 01** (size, byte sum, version) → `mcu_ota_container_tx`.
4. The MCU validates the version (`0xB470`), **erases the flash staging area**, ACKs `0x81` (`0xB5A2`).
5. Display `send_buf` (`0x14773C`): `fread` 256 bytes → `sendData` (**CC DD 02**, seq, data). Waits for an ACK per packet.
6. The MCU writes to the flash staging area (`0x9008` FMC write), tracks seq/pct, ACKs (`0xB604`).
7. After the last packet: the MCU verifies the checksum, sends **cmd 3** (done) + **cmd 4** (reboot), resets via `SCB->AIRCR` (`0x2A18`).
8. Display `mcu_update_status`: **2**=ready, **3**=waiting, **4**=success, **5**=reboot, **6**=error.

### 7.4 Flash staging layout (MCU)
* **128 KB flash:** app `0x0…0xF800`; staging `0xF800…0x18000` + `0x18800…0x19800` + `0x1A000…0x20000` (fragmented, 2 KB gaps).
* **256 KB flash:** staging `0x20000…0x38000`.
* Parameters/config in the FMC config area **`0x300000`** (ISP write @ `0x711C`).
* The MCU distinguishes the layout by reading the flash size (probe `0x20000`/`0x40000`, §8).

---

## 8. Chip identification: **Nuvoton NuMicro M451 (Cortex-M4F)**

**Conclusion:** the powerMcu is a **Nuvoton M451 series** MCU — ARM **Cortex-M4F @ 72 MHz**, **32 KB SRAM**, **128 KB or 256 KB flash** (M451**xE** / M451**xG**). The exact package/flash variant requires a physical reading, but **the family is unambiguous**. The M031/M032/GD32/HC32 hypothesis is **rejected**.

### 8.1 Chain of evidence (all addresses in `MEM03_V514_260923.bin`)

1. **The reset handler = the M451 BSP's `startup_M451Series.s`, byte for byte:**
   * Unlock `REGWPCTL` (**`0x40000100`**) with the keys **`0x59, 0x16, 0x88`** (`0xB3B8`).
   * Write **`0x5AA5`** to `PORCTL` (**`0x40000024`**) — "Init POR" (`0xB3CC`).
   * Clear bit 12 (`HXTSELTYP`) in `CLK_PWRCTL` (**`0x40000200`**) — "Select INV Type" (`0xB3D4`).
   * This is *exactly* the M451 startup (confirmed against OpenNuvoton/M451BSP + published source).
2. **`SystemInit` @ `0x6AB4` = M451 `system_M451Series.c`:**
   * `if ((SYS->PDID >> 12) == 0x945) M32(GCR_BASE+0x14) |= BIT7;` — the "One-time POR18" workaround (`0x6AD2…0x6AE4`). **`0x945` is the M451's PDID series number.**
   * `CLK->PWRCTL &= ~CLK_PWRCTL_HXTSELTYP_Msk;`
   * `SCB->CPACR |= 0xF00000` (CP10/CP11 full access) → **FPU enable** (`0x6AF8`, literal `0xE000ED88`).
3. **"FMC PID = 0x%x"** is printed by reading **`0x40000000`** (= `SYS_PDID`) @ `0x87C`. (The string is a vendor misnomer — it is the *SYS* PDID, not FMC. The FMC itself sits at `0x4000C000`.)
4. **The peripheral bases match the M451 memory map** (from disassembly literals):
   | Base | M451 peripheral |
   |---|---|
   | `0x40000000` | SYS/GCR (PDID, REGWPCTL@0x100) |
   | `0x40000200` | CLK (CLKSEL1@0x214, CLKDIV0@0x220, PLLCTL@0x240) |
   | `0x40004000` | GPIO (MFP: GPA_MFPL@0x30 … GPE_MFPH@0x54) |
   | `0x4000C000` | **FMC** (flash controller, ISP) |
   | `0x40071000` | **UART1** (display link) |
   | `0x40073000` | UART3 (scale) |
   | `0x40051000` | TIMER2 (100 kHz control tick) |
   | `0x40081000` | I2C1 |
5. **Vector table: 64 IRQs**, layout = M451 (`UART1_IRQn=37`, `TMR2=34`, `RTC=6`, `GPD=19`, `GPE=20`, `I2C1=39`, `ADC00=42`, `UART3=49`). The active handlers in the binary match.
6. **Thumb-2 + FPU** → Cortex-M4/M7/M33 class. `MOVW`/`MOV.W` (32-bit) are used everywhere; the hard-fault handler prints **"FPU active!"** (`0xBE50`). **Rules out M031/M032** (Cortex-M0/M0+ have neither Thumb-2 `MOVW` nor FPU/CPACR).
7. **Clock table** @ `0xF2B8` = `{HXT=12 000 000, PLL=0 (computed), LXT=32 768, HIRC=22 118 400}`. **HIRC=22.1184 MHz is an M451 signature** (the M031 has a 48 MHz HIRC, GD32/HC32 others).
8. **`SystemCoreClock=72 000 000`** @ `0x748`; **SysTick reload=71999** @ `0x750` → 72 MHz, 1 ms tick. M451 max = 72 MHz. ✓
9. **SRAM:** the string **"Flash = %dk,Sram = 32k"** (`0xBE8`); RAM references up to `0x20003DE0` (SRAM0) + initial **SP=`0x200045E0`** (in SRAM1). M451 32 KB SRAM = SRAM0 `0x20000000–0x20003FFF` + SRAM1 `0x20004000–0x20007FFF`. ✓ (The SP is in SRAM1 — the stack is placed there by the linker script; data in SRAM0.)
10. **Flash:** the size probe reads `0x20000` (128 K) / `0x40000` (256 K) (`0x914E`); the binary is 63 460 bytes. M451 flash = 40/72/128/256 KB; the firmware assumes ≥ 128 K → **M451xE (128 K) or M451xG (256 K)**.
11. **RT-Thread** ("2006–2020 Copyright by rt-thread team"), build **"Sep 23 2026"**, board **"MEM03"**, fw **5.1.4** — consistent with the OTA manifest.

### 8.2 Why not the other hypotheses?
* **M031/M032:** the same SYS/CLK/GPIO/FMC bases and the same `0x59/0x16/0x88` unlock, **but** Cortex-M0/M0+ → no `MOVW`, no FPU/CPACR, HIRC=48 MHz (not 22.1184). The binary requires Thumb-2 + FPU → rejected.
* **GD32F10x / HC32:** a different peripheral map (GD32 FMC@`0x40022000`, USART@`0x40013800` etc.), a different unlock sequence, no `0x945` PDID. Rejected.
* **"FMC PID"** is Nuvoton terminology, but the register read is `SYS_PDID` — and the PDID check `0x945` + the whole SystemInit point unambiguously to the **M451**.

**Most likely part:** `M451LE6AE` (128 K flash / 32 K SRAM, LQFP64) or `M451LG6AE` (256 K / 32 K, LQFP64) — or an equivalent M451/M452/M453 variant. The exact flash + package can be confirmed by reading `SYS_PDID` (via SWD/ISP) or the chip marking on the board.

---

## 9. Future use — Python recipe (pyserial)

With board access (or via the display's `/dev/ttyS1`), the protocol can be spoken directly. **Note:** the display app `demo_v9` keeps `/dev/ttyS1` open itself and polls — to avoid collisions, either stop `demo_v9` or only listen/send passively at a time when the link is idle. Below is a minimal, correct client.

```python
import serial, struct, time

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def frame(cmd, sub, words):
    """AA 55 CMD SUB LEN payload(16-bit LE words) CRC16 LE."""
    payload = b''.join(struct.pack('<H', w & 0xFFFF) for w in words)
    body = bytes([0xAA, 0x55, cmd, sub, len(words)]) + payload
    c = crc16_modbus(body)
    return body + struct.pack('<H', c)

def query(sub, nwords):
    """7-byte cmd 3 read query (no payload)."""
    body = bytes([0xAA, 0x55, 0x03, sub, nwords])
    return body + struct.pack('<H', crc16_modbus(body))

ser = serial.Serial('/dev/ttyS1', 57600, bytesize=8, parity='N',
                    stopbits=1, timeout=0.2)   # on the MCU board: the corresponding UART1

# Example 1: read the status block (words 32..111)
ser.write(query(32, 80))
# ... read and parse the cmd 0x83 reply (AA 55 83 20 50 <160 bytes> CRC)

# Example 2: switch the machine on (write register 0 = 1)
ser.write(frame(0x06, 0, [1]))
# ... wait for the cmd 0x86 ACK

# Example 3: set the brew temperature to 87.0 °C (register 11 = 870)
ser.write(frame(0x06, 11, [870]))

def parse_frames(buf):
    """Yield (cmd, sub, words) for complete AA 55 frames in buf."""
    i = 0
    while i + 7 <= len(buf):
        if buf[i] != 0xAA or buf[i+1] != 0x55:
            i += 1; continue
        cmd, sub, L = buf[i+2], buf[i+3], buf[i+4]
        total = 2*L + 7
        if i + total > len(buf): break
        body = buf[i:i+total-2]
        crc_rx = struct.unpack_from('<H', buf, i+total-2)[0]
        if crc16_modbus(body) == crc_rx:
            words = [struct.unpack_from('<H', body, 5+2*k)[0] for k in range(L)]
            yield cmd, sub, words
            i += total
        else:
            i += 1

# Listen for the MCU's status broadcasts (cmd 0x83) and version requests (0x89):
buf = b''
while True:
    buf += ser.read(256)
    for cmd, sub, words in parse_frames(buf):
        if cmd == 0x83:
            print("status regs", sub, words)
        elif cmd == 0x89:
            # the MCU asks for the version — reply with cmd 9 and an ASCII string
            ver = b'1.1.1-69'
            body = bytes([0xAA,0x55,0x09,len(ver),0x00]) + ver
            ser.write(body + struct.pack('<H', crc16_modbus(body)))
    buf = buf[-512:]
```

**Important caveats:**
* The register semantics in §5 are partly inferred; validate against a logic analyzer/PCAP before writing to unknown words.
* The MCU expects the version handshake (cmd 0x89 → cmd 9) and periodic queries; a pure "listener" receives status broadcasts without having to send anything.
* OTA (§7) should **not** be tried against a running machine without a backup — it erases the flash staging area and reboots.

---

## 10. Appendix — key addresses and disassembly excerpts

### 10.1 demo_v9 (display, ARMv7, VMA = file offset + 0x10000 for .text/.rodata)

| Symbol | Address | Contents |
|---|---|---|
| `fuart_thread` | `0x14671C` | `open_port(1)`; `set_opt(fd,57600,8,'N',1)`; `lv_timer_create(uartRxProcess,60)`; loop `usleep(10000)` |
| `set_opt` | `0x144950` | `cfsetispeed/ospeed(0x1001=B57600)`; CS8; clear PARENB/CSTOPB; VMIN=VTIME=0 |
| `usart_send` | `0x146D5C` | `buf_tx[2]=cmd,[3]=sub,[4]=len`; `memcpy(buf+5, machine+0xFE8+2·sub, 2·len)`; `CRC_GetModbus16(buf,2·len+5)` → `buf[5+2·len]` |
| `usart_ckeck` | `0x146E70` | 7-byte query: `buf[2..4]=cmd,sub,len`; CRC over 5 bytes → `buf[5]` |
| `check` (RX parser) | `0x146164` | header table `AA 55` @ `0x18C378`; cmd 0x86 clears retransmit; cmd 0x89 → `verSend` |
| `ota_check` (OTA parser) | `0x147AA4` | header table `CC DD` @ `0x18C37C`; dispatch cmd 1/2/3/4 |
| `crc16_modbus` | `0x144798` | init 0xFFFF, poly 0xA001 (bitwise) |
| `CRC_GetModbus16` | `0x144914` | table @ `0x18BFA0` (= the standard MODBUS table) |
| `calculateChecksum` | `0x1474C8` | 32-bit byte sum of a file (OTA check) |
| `request_update` | `0x147314` | `CC DD 01 0F 00 size check version`; CRC @ offset 20 |
| `sendData` | `0x147560` | `CC DD 02 LEN seq data`; CRC |
| `buf_tx` | `0x1A5F28` | initial `AA 55 …` |
| `powerSendBuf` | `0x1A6030` | `AA 55 08 …` (15 bytes) |
| `verSendBuf` | `0x1A6040` | `AA 55 09 …` |
| `mcu_ota_container` | `0x1A61D0` | initial `CC DD …` |

**`usart_send` frame construction** (`0x146D78..0x146E2C`), as pseudocode:
```
buf_tx[2..3] = cmd            # 16-bit store; byte [3] is overwritten by sub next
buf_tx[3]    = sub
buf_tx[4]    = len            # in 16-bit words
memcpy(buf_tx + 5, machine + 0xFE8 + 2*sub, 2*len)
crc = CRC_GetModbus16(buf_tx, 5 + 2*len)        # over buf_tx[0 .. 4+2*len]
buf_tx[5+2*len .. 6+2*len] = crc                 # little-endian
```

### 10.2 MEM03_V514_260923.bin (MCU, raw Cortex-M, flash base 0x0)

| Function / data | Address | Contents |
|---|---|---|
| Vector table | `0x000` | SP=`0x200045E0`, Reset=`0xB3B9`, 64 IRQs |
| Reset_Handler | `0xB3B8` | M451 unlock/POR/INV type (see §8.1) |
| SystemInit | `0x6AB4` | PDID>>12==0x945 → POR18; CPACR FPU |
| chip info print | `0x868` | "FMC PID"=`[0x40000000]`; "Flash=%dk,Sram=32k"; UID |
| flash size probe | `0x914E` | ISP read `0x20000`/`0x40000` → 128/256 K |
| UART_Open | `0x8908` | UART1=`0x40071000`, UART3=`0x40073000`; MFP GPE_MFPH=0x11 |
| baud computation | `0x86C2` | clk/(div+1) / baud; DIV16/DIV8 |
| board init (UART) | `0x2C2A` | baud constant `0xE100` (57600); `UART_Open(UART1,…)` |
| RX parser (normal+OTA) | `0x9AAC` | 19-state jump-table state machine @ `0x9AD8`; header AA/CC |
| cmd dispatch | `0x9C28` | cmd 3→`0x9A3A`, 6→`0x99C6`, 8→`0x9C88`, 9→`0x9E12` |
| status builder (cmd 0x83) | `0x9A3A` | `AA 55 83 sub len` + register words + CRC |
| write ACK (cmd 0x86) | `0x99C6` | writes the register file + ACK |
| version request (0x89) | `0x9A94` | constant frame @ `0x9D70` = `AA 55 89 01 00 00 6F 81` |
| MCU CRC | `0x98E8` | init 0xFFFF poly 0xA001; returns byte-swapped |
| register file / APP Param | `0x20000F96` | 129 words; init @ `0x9966` |
| status task loop | `0x286E…0x2A3E` | `rt_thread_delay(10)`; broadcast sub32 len80 @ `0x2744` |
| OTA request handler | `0x9F00` / `0xB700` | cmd 1: size/check/version; flash erase; ACK 0x81 |
| OTA data handler | `0xB90C` | cmd 2: seq check, FMC write, pct, ACK |
| OTA ACK builders | `0xB5A2`(cmd1) / `0xB604`(cmd2) | CC DD templates |
| OTA templates (const) | `0xF304`(cmd1), `0xF314`(cmd2), `0xF2F8`(cmd4) | `CC DD …` |
| SystemCoreClock | `0x748` | 72 000 000 |
| SysTick reload | `0x750` | 71999 (1 ms) |
| clock table | `0xF2B8` | {12 MHz, PLL, 32768, 22.1184 MHz} |
| RT-Thread banner | `0xCD70` | build "Sep 23 2026" (`0xCD94`) |

**MCU cmd 6 write + ACK** (dispatch at `0x9C54`): the handler takes `sub` (frame byte 3) and `len` (frame byte 4).
If `sub + len < 33` it passes the payload (frame byte 5 onwards) to `0x99C6`, which writes the register file and
sends the cmd 0x86 ACK. Otherwise it falls through to the extended write band (words 112..114) special case; see
[registermap.md §3](registermap.md#3-write-handler-cmd-6--semantics).

**MCU status broadcast** (`0x2744`): the status task calls the status builder `0x9A3A` on the UART1 handle with
`sub = 32` (`0x20`) and `len = 80` (`0x50`) words, i.e. it sends `AA 55 83 20 50 <words 32..111> CRC`.

---

## 11. Verification status

| Layer | Verified against | Confidence |
|---|---|---|
| Baud/parity/stop bits | both binaries (display `set_opt`, MCU `UART_Open`) | **High** |
| Frame header/length/CRC | both binaries + CRC match against a constant frame (`0x816F`) | **High** |
| Command dispatch (3/6/8/9 ↔ 0x83/0x86/0x88/0x89) | both binaries | **High** |
| OTA frames + flow | both binaries (request/data/ACK/reboot) | **High** |
| Register file base + size | MCU (`0x20000F96`, 129 words) + display mirror (`+0xFE8`) | **High** |
| Per-word register semantics | partly (defaults + call sites + config.json) | **Medium** — validate with PCAP |
| Chip = M451 | SystemInit byte match + PDID 0x945 + peripheral map + FPU + HIRC | **High** |
| Exact flash/package | flash probe (128/256 K), SP in SRAM1 | **Medium** — requires a physical reading |

*— research sub-agent, 2026-11-16. Static analysis; the device was not touched.*
