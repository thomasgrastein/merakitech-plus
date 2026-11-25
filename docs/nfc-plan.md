# NFC — reverse findings and read-first plan (planning, not implemented)

Status: **plan for review**. Nothing here is implemented. The tag format is documented in
[stock-coffeesense-nfc.md](stock-coffeesense-nfc.md); the preset model, the settings overlay and the file format are in
[presets-spec.md](presets-spec.md). Addresses are virtual addresses in `demo_v9` (1.1.1-69, symbols and debug info kept).
Confidence: **H** = proven from code; **M** = strong inference; **L** = guess.

The findings in §2 are **static only**. No reader register was touched, and the read-only device probe (§12) is still
outstanding.

**Goal: feature parity with stock for the tags people already own.** A CoffeeSense tag from a roaster or a vendor box
must do on Meraki+ what it does on the original UI: tap it, the recipe is applied, the bean card appears. Everything
else NFC can do — linking tags to presets, writing Meraki+'s own NDEF cards — comes after that and is optional.

> **What changed in this revision (read-first).** The earlier plan put Meraki+'s own tag format first and rejected
> reading vendor tags. That order is reversed: phase 1 is now *CoffeeSense read parity* (§5), with the key handled by
> runtime extraction on the owner's machine (§6). Writing or sharing tags (NDEF on NTAG) is demoted to an optional
> later phase (§8, phase 3). Meraki+ still **never writes a MIFARE Classic tag** and the repository still carries
> **no key material**.

## 1. Summary

| Topic | Finding / decision |
|---|---|
| Reader | An RC522-compatible ISO 14443 reader on **I²C bus 1 (`/dev/i2c-1`), 7-bit address 0x3F**. The init sequence unlocks extended registers through 0x37 and has a Type B mode, which **points to a Fudan FM175xx-class chip** (FM17550/17580), not an NXP MFRC522. Exact part: unknown until VersionReg is read. |
| Power/enable | `gpio0`, exported and set high by the boot script `/customer/demo.sh`. The stock NFC loop also re-writes `gpio0=1` on every pass. |
| Tags stock accepts | **MIFARE Classic 1K only**, with the default key A `FFFFFFFFFFFF`. It reads blocks 4–29 (sectors 1–7, 20 data blocks = 320 bytes). |
| Stock payload | ASCII **Base64 → AES-128-CBC (static key, IV = the same bytes) → JSON with 17 keys** (`gs pw bt bw n1 n2 el pr ct rl bgn o1 o2 o3 f1 f2 f3`). This is obfuscation, not security: the payload has no authentication. |
| UID | **Not used by stock.** There is no UID → profile mapping. |
| Writes tags? | **No.** `PcdWrite` and the SR176 write paths have no reachable caller. |
| On tap (stock) | The recipe goes to the controller **immediately and without confirmation**: one block frame cmd6 sub 4 len 21 (words 4–24), then `config.json` setters, then the stock preset is cleared. The decrypted JSON is written to `/customer/nfc.json`, and a bean card page appears with `nfc.mp3`. |
| Exclusivity | `demo_v9` opens `/dev/i2c-1` once and holds the fd. There is **no locking**. Meraki+ may only touch the reader while `demo_v9` is SIGSTOPped (run.sh mode) or not running (slot B boot). |
| **Meraki+ phase 1** | Read the same tags **read-only**, decode them with a key that is looked up **at runtime in the stock binary already on the machine** (§6), apply dose, brew temperature and yield through Meraki+'s own single-word path as a *tag recipe* layer that never writes `config.json` (§5.5), and show a bean card (§5.6). |
| Deliberate differences | No block frame with start words; range-checked values; applied **once per arrival** instead of every 200 ms; nothing stock-owned is written. Full list: §5.8. |

## 2. Stock NFC, reversed

### 2.1 Bus and chip

| Item | Detail | Where | Conf. |
|---|---|---|---|
| Device node | `hw_i2c_init` opens `/dev/i2c-1` `O_RDWR` once. fd global at 0x1A5F04. | 0x142708, called from `app_nfc_thread` 0x142934 | H |
| Addressing | `ioctl(fd, I2C_SLAVE=0x706, 0x3F)` before each transfer (the last address is cached at 0x1E0505). Then plain `write`/`read`, with the register as the first byte. No `I2C_RDWR`. | `hw_i2c_write` 0x142784, `hw_i2c_read` 0x14282C, `IIC_ReadOneByte` 0x140948 (addr at 0x140970), `IIC_Write_Byte` 0x140988 (0x1409BC) | H |
| `i2c_mcu_read/write` | These are misnamed generic wrappers whose only callers are the reader's byte helpers. **The brew controller is not on I²C**; it stays on `/dev/ttyS1` UART. | 0x14085C / 0x1408A8 | H |
| Touch | Touch is a separate bus: Techwin TW31xx kernel driver on i2c-0, address 0x0C. Bus 1 carries only the reader (as far as is known). | lsmod, input devices | M |
| Reset | `PcdReset`: CommandReg 0x01 = 0x0F (SoftReset). | 0x140C1C | H |
| Version check | `IC_ver` reads register 0x37 (VersionReg) and only tests it for **non-zero**; there is no expected value. Reader absent → reads 0 → the stock loop skips the pass. | 0x140D0C | H |
| Chip family | `PcdConfig('A'/'B')` **writes** 0x37: saves it, writes 0, then 0x5E, pokes 0x35=0xED and 0x3B=0xA5, writes 0x37=0xAE, then 0x3B=0x72, and restores 0x37. That is the extended-register unlock of the FM175xx family. A genuine MFRC522 has a read-only 0x37 and no Type B. | `PcdConfig` 0x140D4C | M |
| A-mode regs | ModeReg 0x11=0x3D, TxMode/RxMode 0x12/0x13=0x00, TxASK 0x15=0x40, RxSel 0x17=0x86, RFCfg 0x26=0x58, plus GsN/CWGsP | 0x140D4C | H |
| B-mode regs | 0x11=0x3F, 0x12/0x13=0x83, 0x1E=0x13 (dead path) | 0x140D4C | H |
| Enable line | `IC_ver` runs `system("echo 1 > /sys/class/gpio/gpio0/value")` on every pass. `/customer/demo.sh` exports gpio0 as an output = 1 at boot, before either slot's UI starts, so gpio0 is also high on a slot B boot. Whether gpio0 is *only* the reader enable or is shared (the boot script's comment area reads like panel power/reset) is unknown. | 0x140D1C; demo.sh line 7 | M |
| MIFARE key A | The global `PassWd` (6 bytes, 0x1A5EFC) holds `FF FF FF FF FF FF`, the public transport default of every blank MIFARE Classic. It is not a secret and not the payload key. | `.data` | H |
| Antenna | Not determinable statically. HOME uses `imgs/home/NFC_ring.png` (0x10B050), probably a "tap here" hint ring. | — | L |

### 2.2 The stock reader thread

`app_nfc_thread` (0x14291C) is started **unconditionally** from `main` (0x14BD3C → `portingInit`). No `config.json` switch
was found (M–H). One loop pass:

1. `PcdReset` → `IC_ver`. If the version reads 0, the pass is skipped.
2. For each block 4..29 except the sector trailers 7, 11, 15, 19, 23, 27: `card_block_check` (0x142644) → `ComReqA(mode=0, blk)`
   (0x141E80). Each block does **a full new session**: `PcdConfig('A')` (antenna off/on, ≈ 9 ms), `PcdRequest(0x52 WUPA)`,
   `PcdAnticoll` (**cascade level 1 only** → 4-byte UIDs), `PcdSelect`, `PcdAuthState(0x60 key A, FFFFFFFFFFFF)`,
   `PcdRead` (16 bytes).
3. The 320 bytes are concatenated → decode (§2.3).
4. `delay_ms(200)` (0x1438A4), then loop.

Consequences:
* **NTAG / Ultralight / NFC Forum Type 2 tags never work with stock**: they have a 7-byte UID (CL2) and no Crypto1 auth (H).
* **No duplicate suppression in the thread.** A valid card left on the reader is re-read and re-applied every cycle (M; a
  UI-level guard was not found).
* `ComReqA(mode=1)` → `PcdWrite` exists, but the only caller passes 0. **Stock never writes tags** (H).
* Dead code (0 callers): `ComReqB`, `PcdRequestB`, `PcdSlotMarker`, `PcdAttriB`, `GetIdcardNum`, `PcdHaltB`,
  `SelectSr`/`ReadSr176`/`WriteSr176`/`ProtectSr176`, `Card_Check`, `ComReqA_Block`, `test_data`,
  `coffee_decoding_and_analysis`, `encrypt_aes`, and `page_home_test_trigger_nfc` (a test-tag injector for a
  "Codex Test Blend").

### 2.3 Stock payload

`decrypt_aes` (0x143C8C): `base64_decode` (0x143A90) → `AES_set_decrypt_key(128)` → `AES_cbc_encrypt(decrypt)`, 16 bytes at a
time with a chained IV, over the whole decoded length. The plaintext is parsed as JSON (cJSON, 0x142B14). All 17 keys must
be present, otherwise the log says "json error" (0x142D2C).

Details that matter for a compatible reader (all H unless marked):

* **Key and IV are two global 16-byte objects in `.data`, named `key` and `iv` in the symbol table, `iv` directly after
  `key`, with identical contents.** This confirms from the stock side what the tag generators do (IV = key bytes). How
  Meraki+ finds them at runtime, without the bytes ever entering this repository, is §6.
* **The padding is not checked.** The output is NUL-terminated at the ciphertext length and handed to the JSON parser, which
  stops at the end of the first complete value. PKCS#7 bytes after the closing brace are simply ignored.
* **Lookup is by name**, so the property order does not matter. The linked parser's default lookup is case-insensitive (M).
* **Number conversion:** `gs`, `bt` and `bw` are read as integers (a fraction is cut off). `pw` is read as a double,
  multiplied by 10 and converted to an unsigned integer, so one decimal survives.
* **No range check on any of the four recipe values.** Whatever the tag says is written to the register mirror and sent.
* Text fields are copied when they are strings; the literal `"00"` and a missing string are treated as an empty line (M).
  `el`, `pr`, `ct`, `rl` go through `atoi`.

| Key | Meaning | Stock use | Conf. |
|---|---|---|---|
| `gs` | recommended grind-dial setting | Display only: stored at `app+0x15D0` and shown as the grinder recommendation (`grinderRecommendValue` 0x110090), the same slot a stock preset's 0–40 grind value uses. Never sent to the controller. | H |
| `pw` | dose, g (one decimal) | ×10 → word 4 + `autoGrindWeight` | H |
| `bt` | brew temp, °C, integer | **×10 − 50** → word 11 + `brewTemper` (see the offset note below) | H |
| `bw` | yield, g, integer | ×10 → word 23 + `autobrewWeight` | H |
| `n1`, `n2` | name lines (`"00"` = empty) | card page | H |
| `o1`–`o3` | origin lines | card page | H |
| `f1`–`f3` | flavour notes | card page | H |
| `el` | elevation, m | card page | M |
| `pr`, `rl` | process, roast level (int) | card page | L |
| `ct` | coffee type (generator mapping: 0 blend, 1 single origin) | card page | L |
| `bgn` | background image name → `S:imgs/nfc/<bgn>.png` | card page | L–M |

`/customer/nfc.json` (written by `nfc_data_save` 0x1438B0) is the decoded JSON, kept for reference only; `demo_v9` never
reads it back. One file written by stock on the reference machine has exactly the 17 properties in the generator order,
with `gs`, `bt`, `bw` as integers, `pw` as a one-decimal number and the other 13 as strings (including `el`, `pr`, `ct`,
`rl` and a three-part `bgn`). That closes the "stock-generated `nfc.json`" gap listed in
[stock-coffeesense-nfc.md](stock-coffeesense-nfc.md#verification-and-open-questions) for the shape and types; the file
itself is not reproduced here.

> **The 5 °C offset.** A tag's `bt` is not the value of the BREW TEMP setting. Stock sends `bt × 10 − 50` to word 11 and
> stores the same number as `brewTemper`; a tag that says 92 therefore sets what the stock brew-temperature page — and
> Meraki+'s BREW TEMP row — show as 87.0 °C (page unit: [PARITY](dev/PARITY.md), setting 4). Stock presets carry the same
> +5.0 °C on their side. Meraki+ copies the arithmetic exactly (§5.4) and shows both numbers on the bean card (§5.6).
> One on-device observation is still wanted to close [presets-spec Q-P1](presets-spec.md#11-open-questions) (§12, R3).

### 2.4 Stock UI flow on tap

1. The thread decodes the card, then `usart_send(6, 4, 21)` (0x1437E8) **writes words 4..24 as one block**. That includes word 24
   (the steam start word, sent with its mirror value) and the other start words in that range.
2. Setters for `autoGrindWeight`/`brewTemper`/`autobrewWeight` → `setPresetAppliedIndex(255)` (the stock preset is cleared) →
   `data_save` (`config.json`). Both saves are skipped when the flag `app+0x15B1` is set (purpose unknown).
3. `app+0x15A9 = 1` (0x143888) → `deal_nfc_update` (0x10DB2C, from HOME's handler) → `clickGotoNfcPage` (0x120D80) →
   `home_nfc_deal` (0x10E17C) draws the bean card (name, background, origin/flavour/elevation/process/roast),
   `nfc_enter_anim`, and `audio/nfc.mp3` via `deal_play` (0x137404). `nfcCountDownExit` (0x121E00) closes it after ≈ 5 s (L–M).
4. `dealBrewNfc` (0x11B138) marks the brew page as "applied from card" (star icons).

**Meraki+ will not copy three things:** the block write that includes start words, the unchecked values, and the silent
re-apply while a card rests on the reader.

## 3. Constraints for Meraki+

| # | Constraint | Consequence |
|---|---|---|
| C1 | One physical reader, no locking, stock keeps its fd | Meraki+ touches the reader **only** when `demo_v9` is stopped (`/proc/<pid>/stat` state `T`) or absent. The NFC helper checks this before every session and goes idle otherwise. |
| C2 | SIGSTOP can land mid-transaction (Crypto1 on, half-filled FIFO, RF field on) | Every Meraki+ session starts with SoftReset + full `PcdConfig`. Stock does the same on every pass, so after SIGCONT it recovers within one cycle (M–H). Before handing back to stock, Meraki+ leaves the chip with Crypto1 off, the antenna off and idle. |
| C3 | A vendor card left on the reader when stock resumes is applied by stock (block write + `config.json`) | This is stock behaviour and cannot be changed. The SWITCH TO ORIGINAL hint mentions it. |
| C4 | The vendor payload needs the vendor key, and the key must not be in the repository | The key is **looked up at runtime in the stock binary on the owner's machine** and kept in memory only (§6). Without a usable key source the tag is reported as unreadable; nothing else changes. |
| C5 | Slot B boot: gpio0 is set by `demo.sh` before the UI starts | The reader should be powered on slot B as well. To be verified (§12 H3). Meraki+ never writes GPIO unless that is proven necessary *and* gpio0's other users are known. |
| C6 | I²C syscalls are blocking; a full 20-block read takes a noticeable fraction of a second | They must stay off the UI loop. Hence a helper process (§4). |
| C7 | The RF field must not disturb the scale or load cell | Stock polls constantly without visible effect, so the risk is low. Meraki+ still polls **only while idle** (no grind/brew/steam/water), since taps during a shot are ignored anyway. |
| C8 | Vendor and roaster cards are other people's property in the user's hands | **Read-only.** The phase 1 helper contains no tag-write command at all (§5.2). |
| C9 | `config.json` is stock-owned | A tag never writes it (§5.5); the invariant from presets-spec (§10 there) covers the tag path too. |

## 4. Architecture: `merakiplus_nfc` helper

This follows the `merakiplus_snd` precedent: a separate small binary in `bin/` (static musl, `./build.sh`, reproducible, listed in
`MD5SUMS`/`BIN_MD5`), started and stopped by the UI.

```
merakiplus (UI loop)  ──stdin cmds──►  merakiplus_nfc  ──/dev/i2c-1 @0x3F──►  FM175xx ⇄ tag
                      ◄──stdout JSONL──       │
                                              └── read-only: stock binary (key lookup, §6)
```

The helper owns everything that touches the reader **and** everything that touches the key: reader session, MIFARE read,
Base64, AES, JSON validation. The UI process receives decoded, normalized fields and never sees key or ciphertext.

* **Driver.** A minimal RC522/FM175xx Type A driver:
  * SoftReset, A-mode config (the stock register values from §2.1, as data)
  * REQA/WUPA, anticollision CL1 (+ CL2 so that other tag types can be named), SELECT, HLTA, CRC_A via the chip
  * MIFARE Classic: `MFAuthent` with key A, READ (0x30). **No write, no value-block command, no key change.**

  Type B is not needed. NTAG READ/WRITE is not part of phase 1 (it arrives with the optional phase 3b).
* **Poll.** One WUPA every 250 ms (4 Hz), but only while the UI says `poll on`. That is HOME/sheets idle; it is off during
  activity and in `V_OFF`. Between polls the antenna is off.
* **Events** (stdout, one JSON object per line, ≤ 1 KiB):

  | Event | Meaning |
  |---|---|
  | `{"ev":"ready","ver":"0x92","key":"stock"}` | Reader found (VersionReg value); key source `stock`, `file` or `none` (§6) |
  | `{"ev":"tag","kind":"cs","uid":"3B9C11F0","pid":"9c1f02ab","cs":{…17 fields…}}` | A CoffeeSense tag was read and decoded. `pid` is a CRC-32 of the 320 raw bytes, used only to recognise the same payload again (§5.7) |
  | `{"ev":"tag","kind":"classic","uid":"…","err":"auth"}` | A MIFARE Classic tag that could not be used. `err`: `auth`, `read`, `b64`, `nokey`, `decode` (wrong key or not a CoffeeSense payload), `fields` (fewer than 17 keys) |
  | `{"ev":"tag","kind":"other","uid":"04A1B2C3D4E580","sak":"00"}` | Not a MIFARE Classic tag (phase 1: toast only) |
  | `{"ev":"gone"}` | 2 consecutive empty polls after a tag |
  | `{"ev":"busy","why":"stock"}` / `{"ev":"absent"}` | Exclusivity guard tripped / no reader |

* **Commands** (stdin, one per line): `poll on|off`, `diag`, `quit`. There is no `write` command in phase 1.
* **Diag.** VersionReg value, chip guess, poll/session/error counters, last UID and tag kind, last error, the exclusivity state
  and the **key source state** (never the key) are shown on DIAGNOSTICS → NFC. The first hardware session needs this.
* **Exclusivity guard (C1).** Before each session it re-checks that no `demo_v9` is running unless stopped. If one is running, it
  reports `busy` and does nothing.
* **Reader absent.** VersionReg 0 or I²C NACK at 0x3F → `absent` once. The UI hides every NFC row; machines without a reader
  just see no NFC UI.
* **SIM.** §9.2.

## 5. CoffeeSense read path — design

The path from a tap to an applied recipe has six steps. Each is specified so that it can be tested without hardware (§9).

```
tap → 5.1 session → 5.2 read 20 blocks → 5.3 Base64 + AES + JSON → 5.4 validate/normalize
    → 5.5 apply as tag recipe (3 words, single-word frames) → 5.6 bean card + sound
```

### 5.1 Reader access

| Step | Rule |
|---|---|
| Gate | Only when the UI has sent `poll on` (idle, not `V_OFF`) **and** the exclusivity guard passes: no `demo_v9` process, or every `demo_v9` process is in state `T`. Checked before every session, not once. |
| Slot B / run.sh | Slot B boot: stock is not running, the reader is free. run.sh mode: stock is SIGSTOPped by Meraki+ and holds its fd; that is harmless while it is stopped (C1, C2). In any other state the helper stays idle and reports `busy`. |
| Power | `gpio0` is **read**, never written. If it reads 0 the helper reports `absent` with reason `gpio0` on DIAG (H3). |
| Session start | SoftReset, wait for the command register to clear, then the full A-mode configuration. Always — stock may have been stopped mid-transaction (C2). |
| Presence poll | Antenna on, settle, one WUPA. No answer → antenna off, done (≈ 10 ms). |
| Session end | HLTA, clear the Crypto1 state bit, antenna off, command register idle. Also on every error path and on `quit`/SIGTERM. |
| Errors | One retry per session for an I²C error or a CRC/timeout error. Five consecutive failed sessions → `absent`, with a slow re-probe (every 10 s). |
| Rate | At most one full 20-block read per arrival (§5.7). While a tag rests on the reader the helper only sends presence WUPAs. |

### 5.2 MIFARE Classic, read-only

1. WUPA → anticollision → SELECT. A 4-byte UID with a MIFARE Classic SAK (0x08 and the usual 1K-compatible variants) is the
   stock case. Classic tags with 7-byte UIDs are read too (stock cannot: it has no cascade level 2); other tags → `kind:"other"`.
2. For sectors 1..7: `MFAuthent` key A `FFFFFFFFFFFF` on the sector's first block — **one authentication per sector**, in a
   single session (stock re-does the whole activation for each of the 20 blocks).
3. READ the data blocks `4–6, 8–10, 12–14, 16–18, 20–22, 24–26, 28–29`. Sector trailers are never read. Each 16-byte block
   is protected by the chip-checked CRC_A.
4. Concatenate → 320 bytes. Any failed step ends the session with an `err` event; partial data is never passed on.

**Never write.** The phase 1 helper has no code path that sends a MIFARE write (0xA0), an Ultralight write (0xA2), a
value-block command (0xC0/0xC1/0xC2/0xB0) or a trailer update. This is enforced by a driver test that fails on any
such command byte reaching the chip model (§9.1) — vendor and roaster cards cannot be damaged by Meraki+.

Timing: 7 authentications + 20 reads in one session. The estimate is 0.3–0.8 s over register-at-a-time I²C; it must be
measured (§12 H9). A tag pulled away mid-read produces no event; the next poll starts over.

### 5.3 Decode

1. **Trim** the 320 bytes at the first NUL. Empty → not a CoffeeSense tag (`decode`).
2. **Base64**: standard alphabet with `=` padding; the length must be a multiple of 4; the decoded length must be a
   multiple of 16, at least 16 and at most 240 bytes.
3. **AES-128-CBC decrypt** with the key from the key provider (§6) and IV = the same 16 bytes. The helper carries its own
   small AES-128 decryption (table-based, no library; verified against the published FIPS-197 and SP 800-38A vectors).
4. **Find the JSON object.** Padding is not verified, because stock does not verify it either (§2.3): the plaintext must
   *start* with a complete JSON object, and whatever follows is ignored. A wrong key yields noise, which fails here.
5. **Parse** with a strict flat parser: one object, at most 32 members, no nesting, strings ≤ 240 bytes, valid UTF-8,
   `\uXXXX` escapes decoded, numbers read as decimal fixed-point (no floating point). Names are matched case-insensitively.
   All 17 names must be present, as in stock; otherwise `fields`.

A payload longer than the 320-byte window (the 344-character exports described in the reference) loses its last cipher
block, the JSON is incomplete and the tag is reported as `decode` — the same outcome as on stock.

### 5.4 Validate and normalize

The helper normalizes; the UI validates against its settings table again before anything is pushed (two independent checks).

| Tag field | Normalization (stock arithmetic) | Meraki+ setting | Controller word | Accepted when |
|---|---|---|---|---|
| `pw` | tenths of a gram: integer part and first decimal, further digits cut | `d` = `autoGrindWeight` | 4 | 10..309 |
| `bt` | integer part, then **× 10 − 50** | `bt` = `brewTemper` | 11 | 850..960 on step 10 (tag values 90..101) |
| `bw` | integer part, then × 10 | `bw` = `autobrewWeight` | 23 | 100..990 on step 10 (tag values 10..99) |
| `gs` | integer part | none — advisory `GRINDER n` | — | 0..100 for display; copied to a preset's `g` only when 0..40 |

* The acceptance ranges are the `SET` table rows (`src/merakiplus.c`), i.e. exactly what the editor allows. **A tag can never
  set a value the touch screen could not.**
* **A field outside its range is skipped, not clamped**, and the others still apply. The bean card marks the skipped value
  (`92° — OUT OF RANGE, NOT SET`). Stock would send it unchecked. Whether real roaster tags fall outside these ranges —
  in particular `bt` below 90 — is open (§12, R4).
* The four recipe values must be JSON numbers. A numeric *string* is treated as invalid for that field (stock would read 0
  and send it).
* Text fields: must be strings (anything else → empty). `""` and `"00"` are empty. Control characters are removed, runs of
  white space collapsed, each value cut to 64 bytes on a character boundary.
* `el`: digits only, 0..9999, else empty. `pr`: `1`..`7` (anything else, including `00` → unspecified). `rl`: `1`..`7`.
  `ct`: `0` or `1`.
* `bgn` must match `^\d{1,2}(\.\d{1,2}){1,2}$`, otherwise it is dropped. Only then is a file name built from it (§5.6), so
  a tag cannot name a path.

### 5.5 Apply — a tag recipe layer, not a `config.json` write

Stock writes the three values into `config.json`. Meraki+ does not: `config.json` stays stock-owned, exactly as for presets.
Instead the values become a third overlay layer between the tweaks and the active preset
([presets-spec §3.2 and §4.6](presets-spec.md#32-effective-settings)):

```
effective(f) = tweaks[f]      if present
             = tag[f]         if a tag recipe is active and f ∈ {d, bt, bw} and the tag supplied a valid value
             = P[f]           if a preset P is active and f's group is in P
             = base[f]        otherwise (config.json)
```

On a decoded tag event the UI does this:

1. **Mode.** Setting `NFC TAGS`: `APPLY ON TAP` (default — stock parity), `ASK FIRST` (the bean card opens with an APPLY
   button and nothing is pushed until it is tapped), `OFF` (the helper is not started).
2. **Guard**, the same as a preset apply: link up, no activity, write queue idle. Otherwise nothing is pushed, the toast reads
   `BUSY — TAP THE TAG AGAIN`, and the tap is dropped (not queued).
3. **Target.** Build the new tag layer from the valid fields; drop any tweak on those same fields (the tag wins over an
   older manual edit of the same value, as on stock); keep the active preset and every other tweak.
4. **Push** the words that differ from the controller mirror — at most three: 4, 11, 23 — one at a time with
   `setting_mcu_push` through `word_ok`/`frame_policy`, each waiting for its ACK. No block frame, no start word, never
   words 26/27/30.
5. **All ACKed:** commit the layer and the bean card to the presets file in one atomic write (`[tag]` block,
   [presets-spec §5](presets-spec.md#5-file-format--new_customermerakipluspresets)), show the bean card, play the tag sound.
6. **Any NACK or timeout:** roll back the words already pushed (the `save_step` rollback), keep the previous state, toast
   `TAG NOT APPLIED`.

What follows from the overlay model, with no extra code:

* The recipe **survives a reboot and a controller restart** (boot push and re-push read `effective`), like stock's does.
* Editing dose, temperature or yield afterwards becomes a tweak; HOME shows the bean pill with a dot.
* Steam, hot water, grind mode and pre-infusion are untouched by a tag — as on stock.
* **CLEAR TAG RECIPE** on the bean sheet removes the layer and pushes the values underneath (preset or base). Applying a
  preset or BASE SETTINGS clears it too.
* **SAVE AS PRESET** (phase 2) turns the current effective GRIND + BREW values and the bean card into a normal preset.
* SWITCH TO ORIGINAL pushes `base` first (presets-spec §4.5), and the assert step restores the tag recipe on return.
* Stock clears its own applied-preset marker on a tap. Meraki+ keeps the preset active underneath, because only three
  values changed and the rest of the preset is still what the controller holds.

Rejected alternative: writing the three values to `config.json` through the existing SAVE path. It is what stock does and
it would need no overlay, but it lets a tag change a stock-owned file with no way back, and it breaks the rule that makes
presets safe to reason about. The overlay engine is needed for presets anyway (phase 1a).

### 5.6 Bean card

A sheet in the Meraki+ style (it is a re-design, not a copy of the stock page):

```
  ‹  BEAN CARD
     EXAMPLE COFFEE                      n1
     EXAMPLE ROASTER                     n2
     COLOMBIA                            o1..o3
     COCOA · CHERRY                      f1..f3
     1200 M · WASHED · SINGLE ORIGIN     el, pr, ct
     ROAST ●●●○○○○  MEDIUM-LIGHT         rl
     18.0 G → 36 G · 92°  (SET 87.0°)    pw, bw, bt and the BREW TEMP value it maps to
     GRINDER 12                          gs
     CLEAR TAG RECIPE     SAVE AS PRESET (phase 2)
```

* **Text.** Tag text is arbitrary UTF-8; the UI font has A–Z, ÆØÅ, digits and a few signs. Text is upper-cased, Latin
  letters with diacritics are folded to their base letter, anything else without a glyph is dropped, and each line is
  cut with an ellipsis at its pixel budget for the round screen. Non-Latin scripts cannot be shown (limitation, §12 R8).
* **Labels** for `pr`, `rl` and `ct` are Meraki+'s own EN/DA strings for the codes in the
  [reference mapping](stock-coffeesense-nfc.md#json-fields-and-encoding); an unknown code shows nothing.
* **Temperature.** The card shows the tag's number and, in brackets, the BREW TEMP value it results in, because the two
  differ by 5 °C (§2.3). The bracket disappears if Q-P1 ends with Meraki+ changing how it labels brew temperature.
* **Background (phase 1d).** `bgn` → `/customer/imgs/nfc/<bgn>.png`, read at runtime from the **stock image directory on
  the machine** and never shipped by this project. It needs a small PNG decoder in the UI (none exists today): bounded
  size, non-interlaced 8-bit only, a dark scrim under the text. Missing file, unsupported PNG or decoder error → the plain
  card. Which files exist per firmware is open (§12, R6).
* **Sound.** A new generated `tag.wav` (`tools/gen_audio.py`, the `merakiplus_snd` path, follows the sound setting). The
  stock `nfc.mp3` is a vendor file and is not used.
* **Lifetime.** The card closes on tap or after 8 s. The bean stays reachable: HOME shows a pill with the first name line
  (`EXAMPLE COFFEE`, with `•` when tweaked) that reopens the sheet, and the GRIND and BREW rows mark values that come
  from the tag.
* Meraki+ does **not** write `/customer/nfc.json`. The bean card lives in Meraki+'s own file.

New strings (EN / DA, draft; the usual selftest classes apply):

| ID | EN | DA |
|---|---|---|
| TX_TAG_TITLE | BEAN CARD | BØNNEKORT |
| TX_TAG_APPLIED | %s · RECIPE SET | %s · OPSKRIFT SAT |
| TX_TAG_FAIL | TAG NOT APPLIED | TAG IKKE ANVENDT |
| TX_TAG_BUSY | BUSY — TAP THE TAG AGAIN | OPTAGET — HOLD TAGGET IGEN |
| TX_TAG_UNREADABLE | TAG NOT READABLE | TAG KAN IKKE LÆSES |
| TX_TAG_UNSUPPORTED | UNSUPPORTED TAG | TAG UNDERSTØTTES IKKE |
| TX_TAG_NOKEY | CANNOT READ COFFEESENSE TAGS — SEE DIAGNOSTICS | KAN IKKE LÆSE COFFEESENSE-TAGS — SE DIAGNOSE |
| TX_TAG_RANGE | %s — OUT OF RANGE, NOT SET | %s — UDEN FOR OMRÅDE, IKKE SAT |
| TX_TAG_SETNOTE | (SET %s) | (SAT %s) |
| TX_TAG_CLEAR | CLEAR TAG RECIPE | RYD TAG-OPSKRIFT |
| TX_TAG_MODE | NFC TAGS | NFC-TAGS |
| TX_TAG_MODE_TAP / _ASK / _OFF | APPLY ON TAP / ASK FIRST / OFF | ANVEND VED TAP / SPØRG FØRST / FRA |

### 5.7 Arrival, debounce and a tag left on the reader

Stock re-reads and re-applies a resting tag about every 200 ms, which also overwrites any manual change to the three
values for as long as the tag lies there. Meraki+'s policy is **apply once per arrival**:

| Situation | Behaviour |
|---|---|
| Tag arrives (seen after ≥ 2 empty polls) | One full read, one `tag` event, one apply |
| Tag stays on the reader | Presence polls only. No further read, no event, no apply — manual edits stick |
| Tag lifted and tapped again | A new arrival: applied again and the card is shown again (the deliberate way to get the tag's values back) |
| Field-edge flapping | `gone` needs 2 consecutive empty polls (≥ 0.5 s); the UI also ignores a second apply of the same `pid` within 2 s |
| Polling pauses for an activity and resumes with the same tag still there | Same UID and same `pid` as before the pause → still the same arrival, nothing happens |
| Tag put on the reader during an activity | It becomes an arrival when polling resumes, and is applied then |
| Boot or helper start with a tag resting | Its `pid` equals the stored tag layer's `src` → nothing happens. A different tag → normal arrival |
| Tap hits the busy guard | Dropped with a toast; lift and tap again |
| SWITCH TO ORIGINAL with a tag resting | Stock applies it its own way (C3) |

### 5.8 Parity table

| Stock behaviour | Meraki+ phase 1 | Same? |
|---|---|---|
| Reads MIFARE Classic 1K, key A default, blocks 4–29 | Same blocks, same key A, one session | ✔ |
| Base64 → AES-128-CBC → 17-key JSON, padding unchecked | Same | ✔ |
| Dose ×10 → word 4; `bt` ×10 − 50 → word 11; yield ×10 → word 23 | Same arithmetic | ✔ |
| Applies immediately on tap, no confirmation | Same by default; `ASK FIRST` is available | ✔ |
| Grind value shown as a recommendation only | `GRINDER n` on the card | ✔ |
| Bean card with names, origin, flavours, elevation, process, roast, background | Own layout; background in phase 1d | ✔ (look differs) |
| Sound on tap | Own generated sound | ✔ (different sound) |
| Recipe persists across reboot | Persists (own file) | ✔ |
| One block frame, words 4–24, start words included | Up to three single-word frames, never a start word | ✘ deliberate |
| No range checks | Editor ranges; out-of-range fields skipped and shown | ✘ deliberate |
| Writes `config.json` and `/customer/nfc.json` | Writes only `/new_customer/merakiplus/presets` | ✘ deliberate |
| Re-applies a resting tag every cycle | Once per arrival | ✘ deliberate |
| Clears the applied stock preset | Keeps the Meraki+ preset underneath | ✘ deliberate |
| Only 4-byte UIDs | 4- and 7-byte UIDs | ✘ superset |

## 6. Key handling

This is the crux of read parity. The payload key is a vendor secret that sits in the stock program. The rule from
[CONTRIBUTING](../CONTRIBUTING.md#safety-rules-non-negotiable) stands: **no key material in this repository** — not the
key, not the IV, and not anything derived from them. What the repository documents and implements is the **method**: where
the key lives in the program that is already on every machine, and how to read it there.

### 6.1 Where the key is (verified statically)

| Fact | Evidence | Conf. |
|---|---|---|
| The stock binary is an unstripped ELF with a full `.symtab` | Section headers of `demo_v9` | H |
| Two global data objects named **`key`** and **`iv`**, each 16 bytes, live in `.data`; `iv` starts exactly 16 bytes after `key` | Symbol table; the only readers are `decrypt_aes` (0x143C8C) and the dead `encrypt_aes` | H |
| Their contents are identical (IV = key) | Byte comparison inside the analysis tool; nothing printed | H |
| In 1.1.1-69: `key` at 0x1A5F08 (file offset 0x185F08), `iv` at 0x1A5F18 | Symbol values + section mapping | H |
| An older vendor build (the one found in slot B of the reference machine) has the same two symbols at different addresses (`key` at 0x19048C), **with the same contents** | Same tool, compared by an in-memory digest only | H |
| Slot A (`/customer/demo_v9`) is never modified by Meraki+ | Safety rule 1; installer | H |

Because the address differs between builds, the lookup goes **by symbol name**, not by a fixed offset.

### 6.2 Runtime extraction (the documented method)

At start, before the first poll, the helper's *key provider* does this:

1. **Candidates**, in order: `/customer/demo_v9` (slot A, the stock program), then
   `/new_customer/merakiplus/boot/demo_v9.vendor` (the installer's backup of the vendor binary that was in slot B).
   Opened `O_RDONLY`; nothing is ever written to either.
2. **Parse** as ELF32 little-endian ARM with hard bounds (file ≤ 8 MiB, ≤ 64 sections, every offset and size checked
   against the file length). Locate the symbol table section and its string table.
3. **Find** the symbols named exactly `key` and `iv`: type *object*, size 16, defined in a section that has file
   contents (not `.bss`). Map each symbol value to a file offset through its section header.
4. **Check the structure** — no secret is needed for this: `iv` is at `key + 16`; the two 16-byte runs are identical; they
   are not all the same byte; and the function symbols `decrypt_aes` and `app_nfc_thread` exist in the same file (it is
   the NFC-capable program).
5. **Load** the 16 bytes into memory, expand the AES decryption schedule, and overwrite the raw copy.
6. If a candidate fails any step, try the next. If all fail → §6.3.

Step 4 makes a wrong lookup fail closed. The final proof that the key is right is functional: the first tag that decodes
to a 17-key JSON object flips the DIAG state from `KEY: FOUND` to `KEY: CONFIRMED`.

**In memory only.** The helper

* never writes the key, the IV or the schedule to a file, a pipe, a log, the DIAG page, the sim state or any network
  payload — its only output is the event lines of §4, which carry decoded fields;
* disables core dumps for itself and marks itself non-dumpable;
* keeps no key bytes in the UI process at all (the UI receives fields, not ciphertext).

The repository correspondingly contains **no value derived from the key**: no hash, no key check value and no
ciphertext produced with the real key. Any of those could be used to test guesses offline, so they are treated like the
key itself (§9.4, §10).

### 6.3 Fallbacks

| Order | Source | When | Notes |
|---|---|---|---|
| 1 | Symbols in the stock binary (§6.2) | Every build analysed so far | Default; needs nothing from the user |
| 2 | Build table: SHA-256 of a stock binary → file offset of the 32 bytes | A future build that ships stripped | The table holds hashes of the vendor program and offsets, never key bytes. Empty today. The structure check (step 4, without the symbol tests) still applies |
| 3 | **User-supplied key file** `/new_customer/merakiplus/nfc.key` | Nothing else works, or a different tag ecosystem | 32 hex digits on one line. The user obtains and installs it themselves; the project does not distribute it. Must not be group- or world-readable. Not created by the installer, not included in backups, logs or support output; removed by `revert` |
| — | None usable | — | `{"ev":"ready",…,"key":"none"}`. CoffeeSense tags show `CANNOT READ COFFEESENSE TAGS — SEE DIAGNOSTICS` once per arrival. Everything else works |

### 6.4 Why this design (legal and hygiene rationale)

This is the maintainers' reasoning, **not legal advice**; item 10 of the README's legal notes applies.

* **Purpose: interoperability.** Meraki+ is an independently written program. Reading the tags the owner already has is
  needed for it to interoperate with the CoffeeSense tag format, and the format cannot be read without this one piece of
  information.
* **EU Software Directive 2009/24/EC.** Art. 6 permits a lawful user to obtain, from the program they hold, the
  information needed to make an independently created program interoperable, when that information is not otherwise
  readily available and the act is confined to the necessary parts; Art. 5(3) covers observing and studying how the
  program works; contractual terms cannot remove either (Art. 8). Art. 6(2) limits what may be done with the result — in
  particular it may not be passed on beyond what interoperability needs. **That is why the key is not published:** each
  machine reads its own copy, for its own owner, and the information never leaves the device.
* **Minimal.** 32 bytes are read. The stock program is not copied, modified, patched or redistributed, and it keeps
  working unchanged.
* **No protection of anyone's work is defeated.** The AES layer hides a recipe (numbers and a bean description) on a tag
  the user holds; it does not gate access to the program or to any other protected work. The payload is not authenticated
  and the MIFARE sector key is the public transport default.
* **Read-only towards third parties.** Meraki+ never writes MIFARE Classic, so it neither alters nor produces CoffeeSense
  tags. A roaster's tag is read the way the machine it was made for reads it.
* **Other jurisdictions differ** (for example the conditions of the US interoperability exemption). The feature is off
  when `NFC TAGS` is `OFF`, and a user who is unsure can leave it there.
* **Hygiene.** A leaked key cannot be un-published. Keeping it — and everything derived from it — out of the tree, the
  binaries, CI and the test fixtures removes the question entirely (§9.4).

## 7. NFC features, prioritized

| Prio | Feature | Needs | Phase |
|---|---|---|---|
| **1** | **CoffeeSense tags work: tap → recipe applied → bean card** (§5), plus DIAGNOSTICS → NFC | reader, key provider, overlay engine | **1** |
| 2 | Bean card backgrounds from the machine's stock image directory | PNG decoder | 1d |
| 3 | **Presets in the UI** (list, sheet, save current) and **SAVE AS PRESET** from a bean card (presets-spec) | overlay engine (1a) | 2 |
| 4 | Import stock presets; name editor | 2a | 2b |
| 5 | *Optional:* link any tag's UID to a preset (a tag as a "preset button") | 1b, 2a | 3a |
| 6 | *Optional:* Meraki+'s own recipe/bean cards on NTAG (NDEF, presets-spec §7): write a preset to a tag, read it on any Meraki+ machine, phone-writable | NTAG read/write (H6) | 3b |
| 7 | Roast-date freshness, maintenance tags, guest mode, user profile tags, show the last stock bean card (`/customer/nfc.json`, read-only) | 1 or 3 | 4 |
| — | Tap-to-wake / tap-to-preheat | Off by default and only behind an explicit setting: a tag should not heat a boiler unattended | 4 (optional) |
| — | Writing CoffeeSense (MIFARE Classic) tags | **Rejected.** Meraki+ never writes MIFARE Classic and never encrypts with the vendor key. The [reference](stock-coffeesense-nfc.md#generate-an-mct-file-and-write-it-to-an-existing-tag) describes how users can write tags with other tools | — |

## 8. Phasing

Effort is in focused developer days including selftest, previews, docs and the review loop. It excludes waiting for the
machine. Every phase lands as pull requests against `main`; code phases need the full E2E gate; versions are assigned
only in a release PR.

### Phase 0 — hardware probe and key-source check (read-only), in parallel with 1a · 0.5 d + one session at the machine
* **Scope:** the read-only probe from §12 (fds, threads, gpio state, bus listing, the stock binary's size and checksum,
  the stock NFC image directory). The one register read (VersionReg 0x37) is done only with `demo_v9` stopped by a
  running Meraki+, and only after explicit approval, because a bus read from a second process is not strictly "no device
  interaction".
* **Output:** chip part, confirmation of 0x3F, gpio0 on slot A and slot B boots, antenna position (tap a CoffeeSense tag
  while stock runs and watch its card), the list of background PNGs with sizes, and which stock build is on the machine
  (to be matched against §6.1). Results go into §2 and §12.

### Phase 1 — CoffeeSense read parity · 11–14 d

**1a — overlay engine (no NFC, no hardware) · 2.5–3 d**
* **Scope:** the part of presets-spec that everything else stands on: the own file (§5 there), `effective` with the
  tweak/tag/preset/base layers (§3.2), the tweak SAVE path (§4.3), boot push / controller-restart re-push / run.sh assert
  from `effective` (§4.4), base push on SWITCH TO ORIGINAL (§4.5), HOME pill, BASE SETTINGS / clear. No preset list yet.
* **Tests:** presets-spec §10 for those parts, including the **`config.json` md5 invariant**.
* **Risk:** it touches the boot push, the most safety-relevant path; it needs the frame-policy tests.

**1b — `merakiplus_nfc` helper · 4–5 d**
* **Scope:** driver (session, CL1/CL2, `MFAuthent`, READ), poll/arrival logic, exclusivity guard, key provider (§6),
  Base64/AES/JSON decode (§5.3), normalization (§5.4), events, diag, host and fake-reader builds.
* **Files:** new `src/merakiplus_nfc.c`, `build.sh`, `bin/merakiplus_nfc` + `MD5SUMS`/`BIN_MD5`/`BIN_SHA256`,
  `tests/check_bins.py` (reproducible list), `tests/hygiene.py` (§9.4), `tests/test_nfc.py`, `tools/gen_nfc_fixtures.py`,
  `tools/merakiplus.py` (install/revert the helper; `revert` removes `nfc.key`), `sim/` addendum.

**1c — UI: apply, bean card, diagnostics · 3–4 d**
* **Scope:** helper lifecycle and poll gating, event parser, tag layer apply (§5.5), arrival policy (§5.7), bean card sheet
  (text), HOME pill, row markers, `NFC TAGS` setting, DIAGNOSTICS → NFC, tag sound, EN/DA strings, docs
  (`docs/dev/UI_v6.md`, `docs/ui-guide.md`, README, CHANGELOG).

**1d — bean card backgrounds · 1.5–2 d** (can ship after 1c)
* **Scope:** bounded PNG decoder, `bgn` → file, scrim, fallback. Needs the phase 0 file listing.

**Hardware verify for phase 1** — §9.3. **Exit criterion:** every CoffeeSense tag the testers own produces the same
dose, brew temperature and yield on the controller as it does under stock.

### Phase 2 — presets UI · 3–3.5 d (2a ≈ 2 d, 2b ≈ 1.5 d)
* **2a:** PRESETS list and preset sheet, save current, apply/update/discard, groups, icon, delete, SAVE AS PRESET from a
  bean card (keeps the card fields with the preset). presets-spec §8.
* **2b:** stock preset import (§6 there) and the name roller editor (if Q-P2 = yes).
* No hardware dependency: if the machine session for phase 1 is delayed, phase 2 can be done before 1b–1d.

### Phase 3 — optional: tag links and Meraki+'s own cards
* **3a — link a tag to a preset · 1.5–2 d.** LINK NFC TAG in the preset sheet ("HOLD A TAG TO THE READER"); tapping a linked
  tag applies its preset. Any tag works, by UID. A CoffeeSense tag that is also linked applies the preset, not its own
  recipe. Map file `/new_customer/merakiplus/nfc` (same atomic pattern):
  ```
  v=1
  tag=04A1B2C3D4E580 7f3a91c2
  tag=3B9C11F0 0c41d2e9
  ```
  At most 32 `tag=` lines. A UID links to one preset; a preset can have several tags. Deleting a preset drops its links.
* **3b — NDEF write/share · 4–5 d.** NTAG READ/WRITE/GET_VERSION in the driver, NDEF TLV build/parse (Type 2: CC page 3,
  TLV 0x03, short and long records), the MIME + URI message from presets-spec §7, WRITE TO TAG (NTAG only, capacity check,
  write → read back → compare, **refuse MIFARE Classic** and locked tags), card view, APPLY ONCE / SAVE AS PRESET.
  Depends on H6 (does the reader handle NTAG21x at all?). Tests: NDEF round trip per tag size; phone-written cards parse;
  oversize refused; `v=2` → `NEWER CARD FORMAT`; a half-written tag → no apply; stock ignores the tag.

### Phase 4 — extras · 1–2 d each
Roast-date freshness, maintenance tags and reminders, guest mode, user profiles, last stock bean card, optional
tap-to-wake (default off). Each is specified when it is picked.

## 9. Tests and verification

### 9.1 Host tests (no hardware, in CI)

| Area | Cases |
|---|---|
| AES-128 | FIPS-197 appendix vectors; SP 800-38A CBC decrypt vectors (public test data) |
| Base64 | RFC 4648 vectors; NUL trimming; bad length; bad alphabet; the 239/240-byte JSON boundary (320 vs 344 characters) |
| CoffeeSense decode | Round trip of generated fixtures under the **test key**: the synthetic 17-field example from the reference; shuffled property order; upper-case names; trailing garbage after the object; missing property → `fields`; number as string → field invalid; fractional `bt`/`bw`/`gs` cut; `pw` decimals; `"00"` and `""` text; non-string text; over-long text; hostile `bgn` (`../`, empty parts, letters); nested values; invalid UTF-8; truncated ciphertext; wrong key → `decode` |
| Normalization | Table-driven: every boundary of §5.4 (`pw` 0.9/1.0/30.9/31.0, `bt` 89/90/101/102, `bw` 9/10/99/100, `gs` −1/0/40/41/100/101) |
| Key provider | Synthetic ELF built by the test with `key`/`iv` holding the test key → found. Negative: no symbol table; wrong symbol size; `iv` not adjacent; `key` ≠ `iv`; constant bytes; symbol in `.bss`; offsets past the end; truncated file; absurd section counts; missing function symbols; second candidate used when the first fails; key file (good, wrong length, bad characters, world-readable). Mutation fuzz of the ELF under a sanitizer build: never a crash, never a key from a file that fails the structure check |
| Key secrecy | Run the host helper end to end with the test key and scan everything it emits (stdout, stderr, diag, sim state) for the key in raw, hex and Base64 form → must be absent |
| Driver vs. chip model | A scripted register model of the reader with a fake MIFARE Classic tag loaded from a fixture dump. Asserts: SoftReset first; exactly 7 authentications; exactly the 20 data blocks, no trailer; Crypto1 off and antenna off at the end and on every error path; one retry then error; tag removed mid-read → no event; wrong key A → `auth`; 7-byte UID; non-Classic tag → `other`; **any write-class command byte fails the test** |
| UI selftest `st_tag` | Event parser fuzz (truncated, oversized, wrong types); apply pushes only single-word frames for words 4/11/23 with the expected values; skip of an out-of-range field; NACK → rollback and unchanged file; busy guard → dropped; once per arrival; re-tap; `pid` de-duplication; boot with a stored tag layer → boot push uses it; clear → values underneath pushed; preset apply clears the layer; `ASK FIRST` pushes nothing before APPLY; `OFF` → helper not started; stock running → idle; **`config.json` md5 unchanged after every one of these**; strings EN/DA, glyphs, pixel budgets; text folding |

### 9.2 Simulator

Two levels, both additive to `sim/CONTRACT.md` (the directory is shared with the test harness):

* **Event level.** The sim UI build does not spawn the helper; the ctrl socket injects the helper's event lines:
  `{"cmd":"nfc","name":"tap","cs":{…17 fields…}}`, `tap` with `"err":"auth"`, `remove`, `absent`, `busy`. The UI-side parser
  is the one used on the device. State JSON gains `nfc:{reader,poll,key,tag,last_ev,applied:[…]}`.
* **Full chain.** The helper is built with a fake I²C backend and started for real. The sim's reader model holds a tag
  loaded from an `.mct` fixture (`{"cmd":"nfc","name":"tap","mct":"<fixture>"}`), and the key provider is pointed at a
  synthetic stock binary containing the test key. This runs read → key lookup → decode → apply → controller words on
  `mcu_sim` end to end.

Scenarios (`sim/scenarios/`, additive): `nfc_cs_tap_apply`, `nfc_cs_left_on_reader`, `nfc_cs_retap`, `nfc_cs_busy`,
`nfc_cs_out_of_range`, `nfc_cs_bad_payload`, `nfc_cs_nokey`, `nfc_cs_reboot_persist`, `nfc_cs_switch_to_stock`,
`nfc_cs_ask_first`, `nfc_absent`, `nfc_stock_running`. Each checks the controller words on the `mcu_sim` side, the frame
shapes (single-word only) and the unchanged `config.json`.

### 9.3 Hardware verification with real tags (needs the machine and its owner)

Preconditions: phase 0 done; a set of real CoffeeSense tags (at least one vendor tag, one roaster tag and one tag written
from a generator export); Meraki+ running with stock stopped or on a slot B boot.

| # | Step | Pass when |
|---|---|---|
| V1 | DIAGNOSTICS → NFC after start | VersionReg shown; poll count rising; `KEY: FOUND` with source `stock` |
| V2 | Under **stock** first: tap each tag, note the three values on the stock pages and the controller words | Baseline recorded per tag |
| V3 | Under Meraki+: tap each tag | `KEY: CONFIRMED`; words 4/11/23 equal the stock baseline; bean card text matches the tag |
| V4 | Leave a tag on the reader, change the dose by hand, wait 30 s | The manual value stays; no further frames |
| V5 | Lift and tap again | Values return to the tag's; card shown again |
| V6 | Tap during a grind and during a brew | Nothing happens during; applied once when idle |
| V7 | Reboot (slot B) with the tag recipe active, tag removed | Recipe still effective; boot push used it; no card pop-up |
| V8 | SWITCH TO ORIGINAL and back, tag removed | Stock shows base; on return the tag recipe is asserted |
| V9 | SWITCH TO ORIGINAL with a tag resting on the reader | Stock still reads and applies it (the stock NFC path is intact after Meraki+ used the reader) |
| V10 | A blank MIFARE Classic, an NTAG, a bank or transit card | Toast only (`TAG NOT READABLE` / `UNSUPPORTED TAG`); nothing pushed |
| V11 | Read back every tag with a phone before and after the session | Byte-identical: Meraki+ wrote nothing |
| V12 | `config.json` checksum before and after the whole session (compared on the machine, not published) | Unchanged by Meraki+ (V2 and V9 are stock's own writes — take the baseline after them) |
| V13 | Timing: full read duration and the shortest tap that works; 1 h soak with a tag resting | Read < 1 s; error count ≈ 0; UI frame time unchanged |
| V14 | Phase 1d: tags with different `bgn` codes | The right background, or the plain card when the file is absent |

### 9.4 Keeping key material out — checks

* `tests/hygiene.py` (CI): no `nfc.key` and no tag dump (`.mct`, `.nfc`, `.mfd`) anywhere except `tests/fixtures/nfc/`;
  **every fixture there must decode under the published test key** (which proves it was not made with the real one);
  the helper source contains no 16-byte key constant outside the test-only block.
* Fixtures are generated (`tools/gen_nfc_fixtures.py`) from plaintext JSON — the reference's synthetic example and
  variations, plus plaintext shapes contributed from Dial-In — and encrypted with a test key that is plainly labelled
  as such. Real exports and real tag dumps are **never committed**; a contributor can point the test at a private directory
  and a local stock binary (`MERAKIPLUS_NFC_REAL_DIR`, `MERAKIPLUS_NFC_STOCK`) to run them locally.
* Maintainer-local, before pushing a phase 1 branch: `tools/check_no_vendor_key.py --stock <path>` derives the key from a
  local stock binary with the §6.2 method and searches the working tree and the built binaries for it in raw, hex, Base64
  and C-array form. The tool contains the method, not the key, and prints only "clean" or the offending path.

## 10. Security and privacy

* **A tag can never start anything.** Tags only select values. Every value passes the `SET` range check and the frame policy; start
  words are never written by a tag path; words 26/27/30 stay compile-time forbidden.
* **A tag can only set what the touch screen can**: three recipe values, inside the editor ranges. Whoever can reach the
  reader can reach the screen. This is why applying on tap — stock's behaviour — is acceptable as the default.
* **Tag content is untrusted input.** The payload is unauthenticated: anyone with the format and a key can make a tag. The
  decoder is bounded and fuzzed; text is sanitized before it is drawn or stored; `bgn` cannot name a path.
* **Read-only.** Phase 1 has no tag-write code. The optional phase 3b writes NTAG only and refuses MIFARE Classic.
* **A UID is not identity.** UIDs are readable by anyone and cloneable. Linking a UID (phase 3a) is a convenience button and
  never an authorization.
* **Tags carry no secrets** and Meraki+ stores none from them: no Wi-Fi data, no personal data beyond a bean description.
* **Key material:** §6. In memory on the device only; nothing derived from it in the repository, the binaries, CI or logs.
* `presets` (with the tag layer and bean card), the optional `nfc` map and the optional `nfc.key` live under
  `/new_customer/merakiplus/` and are removed by `revert`. Nothing under `/customer/` is written.

## 11. Effort overview

| Phase | Effort | Depends on |
|---|---|---|
| 0 probe + key-source check | 0.5 d + session | machine online |
| 1a overlay engine | 2.5–3 d | — |
| 1b NFC helper (read, key, decode) | 4–5 d | 0 for the chip details; can start against the chip model |
| 1c UI: apply, bean card, DIAG | 3–4 d | 1a, 1b |
| 1d backgrounds | 1.5–2 d | 1c, 0 (file listing) |
| 2 presets UI | 3–3.5 d | 1a |
| 3a tag links (optional) | 1.5–2 d | 1b, 2a |
| 3b NDEF write/share (optional) | 4–5 d | 1b, 2a, H6 |
| 4 extras | 1–2 d each | 1 or 3 |

## 12. Open questions, risks and the read-only probe

### Hardware

| # | Question | How to answer |
|---|---|---|
| H1 | Exact chip: the VersionReg (0x37) value and the chip marking | A register read with stock stopped (approval needed), or a photo of the board |
| H2 | Is 0x3F the only device on i2c-1? | `ls /sys/bus/i2c/devices/` (read-only). **No `i2cdetect`**: its probe writes can upset devices |
| H3 | gpio0 on a slot B boot; is gpio0 shared with something else? | `cat /sys/class/gpio/gpio0/{direction,value}` on both boots; read the board/boot script context |
| H4 | Does `demo_v9` hold `/dev/i2c-1` open? | `ls -l /proc/$(pidof demo_v9)/fd`. An earlier session already saw the fd and labelled it "touch"; touch is on i2c-0, so it is the reader |
| H5 | Can Meraki+ read while stock is SIGSTOPped, and does stock recover after SIGCONT? | §9.3 V1, V9. **Risk:** if not, NFC works on slot B boots only |
| H6 | Does the reader handle NTAG21x (CL2, READ/WRITE) at the antenna's range? | Only matters for the optional phase 3b |
| H7 | Antenna position and read range (through the display glass?) | Tap test |
| H8 | Which machines have the reader at all (Gen 1, Gen 2)? | Probe, and reports from owners (a Gen 2 owner runs Meraki+; ask for the phase 0 listing). The UI hides NFC when it is absent |
| H9 | Duration of a full 20-block read over I²C; shortest tap that works | §9.3 V13. **Risk:** if it is well over 1 s, taps feel unreliable; mitigation is a "hold the tag" hint and a faster register access pattern |

### CoffeeSense read path

| # | Question / risk | Plan |
|---|---|---|
| R1 | **Key lookup on other firmware builds.** Verified on two builds (§6.1). A build without symbols, or with a different key, breaks the default source | Phase 0 records the build on each tester's machine. Fallbacks §6.3. The structure check fails closed |
| R2 | **Does the same key cover roaster and third-party tags?** Generator-written tags work on stock, so they must use the stock key — but this is inference until V3 | §9.3 V3 with tags from several sources |
| R3 | **The 5 °C offset (Q-P1).** The arithmetic is proven; what the user *sees* on stock after a tap is taken from the parity table, not from a tap test | §9.3 V2: tap a tag with a known `bt` under stock and read the stock brew-temperature page |
| R4 | **Range parity.** Stock applies any value; Meraki+ skips values outside the editor range. Generators allow `bt` 85–100, and 85–89 maps below the BREW TEMP minimum | Collect the values on real tags (V2). If real tags need it, decide separately whether the BREW TEMP range itself is too narrow — a tag never widens it |
| R5 | **`gs` scale.** Generators allow 0–100; stock presets use 0–40 for the same display slot; stock applies no clamp on the NFC path. What stock shows for `gs` > 40, and whether 0 means "no recommendation", is unknown | Tap test under stock with `gs` 0, 40, 41, 100. Until then: show 0..100 as given, copy to a preset only when 0..40 (presets-spec Q-P4) |
| R6 | **Background PNGs per firmware.** The reference lists 47 codes; which files exist on a given machine, their size and PNG type are unknown | Phase 0 listing (names, sizes, IHDR). Fallback is the plain card |
| R7 | **Stock labels** for `pr`/`rl`/`ct`, and the meaning of process code 6 (generators call it "Wet-Panting") | V2 observation; Meraki+ uses its own labels and shows nothing for a code it cannot name |
| R8 | **Non-Latin text** (stock ships CJK, Korean and Arabic UI languages) cannot be drawn by the Meraki+ font | Documented limitation; lines without drawable characters are omitted |
| R9 | **Reader contention with stock** (C1–C3): a session while stock runs corrupts both | Guard before every session; reset per session; V9 as the regression test |
| R10 | **Payload at the 320-byte boundary** and stock's Base64 decoder details (line breaks, missing padding) are not traced | V2 with a boundary tag from the reference's author; Meraki+ accepts the strict form only until a difference is shown |
| R11 | **Legal view of runtime key extraction** (§6.4) may differ by jurisdiction or be contested by the vendor | Key never distributed; setting to turn the feature off; the key provider is one isolated module that can be removed without touching presets |

### Read-only probe

Each command only reads; none touches the bus, and none prints key material.

```sh
ls -l /dev/i2c-* ; for d in /sys/bus/i2c/devices/*; do echo "$d $(cat $d/name 2>/dev/null)"; done
P=$(pidof demo_v9); grep State /proc/$P/status; ls -l /proc/$P/fd
for t in /proc/$P/task/*; do echo "$t $(cat $t/comm)"; done
ls /sys/class/gpio; cat /sys/class/gpio/gpio0/direction /sys/class/gpio/gpio0/value
ls -l /customer/demo_v9 /new_customer/merakiplus/boot/demo_v9.vendor; md5sum /customer/demo_v9
ls -l /customer/nfc.json /customer/audio/nfc.mp3 /customer/imgs/nfc/
for f in /customer/imgs/nfc/*.png; do echo "$f $(dd if=$f bs=1 skip=16 count=10 2>/dev/null | hexdump -C | head -1)"; done
grep -c presetStore /customer/config.json
```
