# Time & scheduling (v6.16) — clock, 12/24 h, planned pre-heat, auto off

This page records how the stock UI (`demo_v9`, symbols kept) and the controller firmware (MEM03) handle
time, and what Meraki+ does with it from v6.16. Addresses are virtual addresses in `demo_v9` unless
marked *MCU*. Strings in `demo_v9`'s read-only data are at file offset = VA − 0x10000.

## 1. Summary

| Topic | Stock UI (reversed) | Meraki+ v6.16 |
|---|---|---|
| `timemode` | **1 = 24 h, 0 = 12 h** (A.M./P.M.). `update_time` 0xD5944 XORs `model[0x59]` with 1 before testing it | Fixed. Up to v6.15 the row was inverted (it showed 24-HOUR for 0) |
| Clock source | SoC system clock, kept in **UTC**. Set only by an HTTP query (`time_sync_thread` 0x13FDEC) → `date -s` + `hwclock -w` | Reads the same SoC clock. **No network sync of its own** |
| Local time | UTC + zone table entry (`timeZone` index) + 1 h if `sumtime` and the DST window | Same. The DST window is computed from the region's rule (see §3.3) |
| Clock display | Status bar, `"%02d:%02d"` or `"%02d:%02d A.M./P.M."` | HOME top line + DIAGNOSE → TIME · SCHEDULE, same format |
| Pre-heat schedule | Up to 30 entries in the binary `/customer/config.bin` | One entry in `/new_customer/merakiplus/schedule` (not shared with stock) |
| Arming | Only at power-off: the next occurrence goes out as the **cmd8 alarm** | The same frame at power-off, **plus** pre-armed while on |
| Cancel | No entry → alarm 0/0/0 | Same; switching the schedule off sends 0/0/0 once |
| Auto off | Fixed 30 min idle → power-off (with the alarm) | Optional: off / 15 / 20 / 30 / 60 min |

## 2. `timemode` (TIME FORMAT)

* `model_data_init` 0xCEF40: `model[0x59] = (getTimeMode() != 0)` (store at 0xCEF94). `getTimeMode` 0xD2064 reads the
  config key `timemode` (string at 0x16539C).
* `update_time` 0xD5944 loads `model[0x59]`, XORs it with 1 at 0xD5970 (the step v6.1 missed) and branches on
  the result (0xD5978/0xD597C), as pseudocode:
  ```
  if model[0x59] == 1:                   # 24 h
      sprintf("%02d:%02d")               # format at 0x165D94
  else:                                  # 12 h
      if   hour > 12:  hour - 12, "… P.M."   # format at 0x165D74
      elif hour == 12: 12,        "… P.M."
      elif hour == 0:  12,        "… A.M."   # format at 0x165D84
      else:            hour,      "… A.M."
  ```
* The settings switch is labelled **"24-Hour Format"** (0x171F60); its click handler (0xFCD60) toggles
  `model[0x59]`, draws the switch ON when it is 1 (`switch_update`) and saves it with `setTimeMode` (0xFCDEC).
* Consequence: the factory config (`timemode = 1`) means a **24-hour** clock. v6.1 read the branch without the
  `eor` and labelled the values the other way round; v6.16 fixes the labels (`OPT_TIME = {12-HOUR, 24-HOUR}`),
  so SAVE now writes the value stock expects. The selftest checks the semantics and eight format vectors.

## 3. Clock

### 3.1 Source
* `_refresh_systime` 0x135548 (called from `biz_refresh` 0x135944): `gettimeofday` → `localtime` (the device has
  no `TZ`, so this is UTC) → `+ zone_table[timeZone].offset` → `+ 3600` if `sumtime` and `is_dst()` → `localtime`
  again → model fields: +0x10 year, +0x20 month (0-based), +0x24 day, +0x28 weekday, +0x14 hour, +0x18 minute,
  +0x1C second.
* Setting the clock: `biz_set_system_time` 0x1359B0 runs `date -s "<UTC>"`, `hwclock -w`, `sync`. Callers: the
  HTTP time sync (`time_sync_thread` 0x13FDEC, only when config `Automatically` is on and wifi is up; plain HTTP
  to a geo-IP service, no NTP) and the manual date/time pages. Nothing in this path talks to the MCU.
* Meraki+ reads the SoC clock (`time()`), applies the zone and DST itself and **never sets the clock**. A clock
  before 2025-01-01 counts as *not set*: HOME shows the wordmark instead of a time, DIAGNOSE says NOT SET, and no
  alarm is sent (a garbage alarm could wake the machine at a random time). `AUTO TIME` stays a config-only row.

### 3.2 Zone table
`0x1A5890`, 136 entries × 12 bytes: `{int offset_s; char *label; char *name}` (e.g. #48 = +3600,
"UTC+01:00", "Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna"). Meraki+ copies it verbatim (`TZT[]`), including
two stock quirks: #30 Newfoundland is −2:30 under the label −03:30, and #135 Kiritimati is +14:00 under the label
+13:00. The TIME ZONE row stays locked (it is set in the stock UI); v6.16 shows the label and the place names.

### 3.3 Daylight saving (`sumtime`)
Stock's DST window (model+0x15B8..0x15CC, RAM only) is written only by the HTTP time sync, from the dates in the
answer, and only if DST is in effect at that moment; `is_dst` 0x1354D4 compares **dates only**. Without a sync the
window is empty, so `sumtime = 1` has no effect offline, and southern-hemisphere windows never match (start >
end within one year). Meraki+ keeps `sumtime` as the user's switch and computes the window from the region rules
stock's `calc_dst_dates_for_timezone` 0x13BB14 uses:

| Rule | Zones (index) | Window |
|---|---|---|
| EU | 41–71 except UTC and the Levant | last Sun Mar 01:00 UTC → last Sun Oct 01:00 UTC |
| US/CA | 0–28, 30, 35, 38 | 2nd Sun Mar 02:00 → 1st Sun Nov 02:00 local |
| Levant | 53, 55, 58, 59, 62 | last Fri Mar → last Fri Oct (02:00 local) |
| AU | 113–120 | 1st Sun Oct → 1st Sun Apr (southern) |
| NZ | 127–131 | last Sun Sep → 1st Sun Apr (southern) |
| none | all others | never (as stock) |

## 4. Planned pre-heat (scheduled wake)

### 4.1 Stock storage
`data_save` 0xD091C copies `model+0x70` (0xF78 bytes) into a buffer that `write_config` 0xD070C writes to
**`/customer/config.bin`** (0x11AC bytes; string at 0x1650C4). The schedule list is 30 entries × 20 bytes at
`model+0x74`:

| Byte | Meaning |
|---|---|
| 0 | entry exists |
| 1 | enabled |
| 2, 3 | hour, minute |
| 4–10 | repeat flags Sunday … Saturday (`judgeWeek` 0x14955C / `judgeWeekEqual` 0x1495E0); all 0 = one-shot ("Never") |

UI: `page_scheduled_init` 0x10335C, `page_scheduled_repeat_init` 0x132FCC; strings "Scheduled Preheat",
"Add your first preheat scheduled.", repeat names Never / Workday / Weekend / Everyday.

### 4.2 Stock arming = the power-off frame
Nothing is sent while the machine is on. `powerOff` 0x149074 sets the sleep state, clears mirror word 0 and calls
`performSleepActions` 0x148324:

1. For each enabled entry: if it repeats today (`judgeWeekEqual`) or is a one-shot, and `hh*60+mm > now` (minutes,
   strictly later), it is a candidate for **today**; the earliest wins.
2. Otherwise the next flagged weekday (`getLastWeek` 0x1496C0; a one-shot → tomorrow) gives *days ahead*; the
   nearest day, then the earliest time, wins.
3. Today → `powerOffSend(mday, hh, mm)`; a later day → `powerOffSend(mday + days, hh, mm)` with the day wrapped by
   `biz_days_in_month` 0x135AE8; nothing → `powerOffSend(0, 0, 0)`.
4. `usleep(80000)`, then cmd6 sub0 len2 `[0, 0]` (word 0 = off, word 1 = awake).

`powerOffSend` 0x14700C builds the 15-byte cmd8 at 0x1A6030:

```
AA 55 08 | year lo, year hi | month 1-12 | day | hour | minute | second | alarm day | alarm hour | alarm minute | CRC lo, hi
```
(CRC-16/MODBUS over the first 13 bytes; the time is the model's **local** time). A chosen one-shot is remembered
(`model+0x1758..0x176C`: year, month, day, hour, minute, index); `deal_preheat_data` 0xCF49C switches it off at the
next start once that time has passed.

### 4.3 The controller (MCU)
* cmd8 handler *MCU* 0x9C88: copies the 10 payload bytes to RAM 0x20000E18, echoes them as cmd 0x88, sets its RTC
  from the date/time bytes (0x9DCE → RTC driver 0x7494) and logs `new time: …`. It does this on **every** cmd8.
* Sleep task *MCU* 0x2F52 (started when word 0 = 0): every 100 ms it reads the RTC and the button (PC.8). Once per
  second, if alarm day + hour + minute ≠ 0 and they equal the RTC's day, hour and minute, it resets the CPU (0x326).
  A button hold of ≥ 0.8 s does the same.
* After the reset word 0 starts as 1 — the boilers heat. On the tested hardware the display module gets power
  again and **cold-boots** (see [power.md](power.md)); Meraki+ starts and runs its boot push, so the controller
  is back on the config's values. The alarm lived in RAM and is gone after the reset.

### 4.4 Meraki+ v6.16
* **Settings → SYSTEM**: PRE-HEAT (off/on), PRE-HEAT AT (00:00–23:55, 5-min steps, shown per TIME FORMAT),
  REPEAT (once / every day / Mon–Fri / Sat–Sun), AUTO OFF (off / 15 / 20 / 30 / 60 min). Saved in
  `/new_customer/merakiplus/schedule` (atomic), never in `config.json` or `config.bin`:
  ```
  preheat=1
  at=06:30
  repeat=daily
  autooff=30
  once_at=0
  ```
* **Power-off** (button hold, or auto off): the cmd8 carries the next occurrence exactly as stock
  (`pw_off_begin` → `sched_alarm`, one entry, the same algorithm); no schedule → 0/0/0 as in v6.14.
* **Pre-arm** (addition, documented deviation): while ON (link, word 0 = 1, boot push done, write queue idle,
  not read-only) the same cmd8 is sent whenever the alarm differs from what the current MCU session last got —
  after start-up, after an MCU restart or wake, after a schedule change, and once a day when the occurrence moves
  on. Reason: on this hardware the power button's ≥ 0.8 s hold cuts the display before Meraki+ can send anything,
  so only a pre-armed alarm can survive that path (if the controller goes to sleep). No schedule and nothing
  armed → nothing is sent (the v6.15 UART traffic is unchanged); switching off sends 0/0/0 once. Never while
  turning off / off.
* **One-shot**: saved for the next hh:mm after SAVE (`once_at`); a minute after that time it switches itself off
  and is saved (toast ONE-TIME PRE-HEAT USED).
* **After a wake**: Meraki+ cold-boots (or, if it stayed on, sees word 0 = 1 again and powers on), runs the boot
  push and re-arms the next occurrence (e.g. tomorrow) once the link is up.
* **Display**: HOME shows a second hint line `⏰ PRE-HEAT · THU 06:30` while a pre-heat is planned (v6.17: the rows are under
  *SYSTEM › SCHEDULE*, whose closed header shows the time or OFF); DIAGNOSE →
  TIME · SCHEDULE shows local time, zone + DST state, auto off + idle time, the last alarm sent (day h:m ·
  sent/acknowledged) and the next pre-heat.

## 5. Auto off (sleep)
Stock: `SleepRefresh` 0x149268 runs a 1800 s counter (`recodeTime` 0x148164) that counts down once per second
and restarts on every touch (`touch_feedback_cb` → `sleepTimeUpfdate` 0x14818C) and every second an extraction,
steam or grind runs (`isExtractionOrSteamSwitchOn` 0x1481E0). After 10 min it shows its sleep page; at 0
`updateSleepState` 0x14827C calls `powerOff` (with the schedule alarm). Config `sleepMode` / `sleepModeSetting`
are loaded (`model+0x54/0x55`) but never read; the setting's own table (0x1A02D0) lists 15/20/30/60 min.

Meraki+: AUTO OFF is **off by default**. When on, every touch, button event and any active phase restarts the
timer; 30 s before the end a toast warns (a touch dismisses it); then the same power-off as the button. It only
fires when the power-off itself is allowed (link, word 0 = 1, not read-only, no SAVE in flight).

## 6. Safety notes
* The power semantics of v6.14 are unchanged: the physical button's kernel cut (≥ 0.8 s) still turns the display
  off before Meraki+ sees the hold; software power-off still sends cmd8 → 80 ms → cmd6 sub0 len2 `[0, 0]`.
* The only new UART frame is cmd8 (the frame stock already sends); no cmd6 word changes; config writes only via
  the TIME FORMAT / DAYLIGHT SAVING rows (the existing atomic save path).
* Meraki+'s schedule does not appear in the stock UI and the stock UI's schedules are not used by Meraki+.

## 7. Tests
* Selftest section *v6.16 time & scheduling* (33 checks): timemode semantics, 8 format vectors, zone table, 11 DST
  vectors, 11 next-occurrence vectors (incl. month/year/leap wrap), the alarm frame byte for byte, schedule
  file round trip without config writes, pre-arm / no repeat / re-arm after an MCU restart / no pre-arm in
  read-only / disarm 0/0/0, power-off with the alarm, no pre-arm while off, re-arm after the wake, one-shot use,
  auto off (warning, touch reset, activity, power-off sequence), HOME clock 12/24 h and the pre-heat hint.
* Simulator scenarios: `time_display`, `timemode_stock_parity`, `schedule_prewarm` (alarm wake end-to-end).
