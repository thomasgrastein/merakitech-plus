# Safety notes

What Meraki+ can make the machine do, what it cannot, where it behaves differently from the stock UI,
and what has not been tested. This page describes the software as it is; it is not a guarantee. The
[disclaimer in the README](../README.md#️-disclaimer--read-this-before-you-do-anything) applies in full.

## 1. What Meraki+ is, and what protects the machine

- Meraki+ replaces **only the display's UI program**. It does not change, update or replace the
  controller's (MCU) firmware and has no way to alter the controller's own fault handling (error codes
  E1–E3: mains voltage, sensor faults, over-temperature — [registermap.md](registermap.md)).
- **The controller does not check the range of the settings a display writes to it.** A write is copied
  into the controller's registers and acknowledged ([registermap.md §3](registermap.md)). With the stock
  UI, the stock UI's own limits are what keeps values sensible. With Meraki+, Meraki+'s limits are.
- Meraki+ therefore enforces the stock UI's minimum, maximum and step for every setting, in one place
  that every outgoing write passes through (the *frame policy*, §2). **A bug in Meraki+ could still send
  a value outside those limits.** The policy is covered by the built-in self-test and by the simulator
  suite on every change; that reduces the risk, it does not remove it.
- This project has **not** examined the machine's hardware protections (for example thermal fuses or
  pressure relief) and makes no statement about them.

## 2. What Meraki+ can send to the controller

Meraki+ sends only frames the stock UI also sends ([protocol.md](protocol.md)). Every write is checked
before it reaches the serial link; a rejected write is not sent and is counted under *DIAGNOSTICS*
(the fourth number in **CMD6 OK·RTX·ERR·BLK**). The expected count in normal use is 0.

| It can | Detail |
|---|---|
| Start and stop grind, brew, hot water and steam | A start word is accepted only when the action that starts that activity asks for it. Saving a setting can never start anything. |
| Start hot water by weight | Only when the controller reports a firmware version above 5.1.2 ([water-dosing-compat.md](water-dosing-compat.md)). |
| Write settings | Only inside the stock UI's range for that setting. |
| Switch the machine on and off | Off only from the power button or the optional auto off. |
| Hand the controller a wake time (pre-heat) | The same alarm frame the stock UI sends ([time-scheduling.md](time-scheduling.md)). |

**It never sends**, and the write check rejects them even if a future bug tried:

- weight calibration (words 26 and 27, which write the controller's flash) and controller reboot
  (word 30) — these are also excluded at compile time;
- drain, back-flush, the controller's self-test and the sleep/drain word;
- anything outside the two bands the stock UI writes (words 0–31 and 112–114).

Meraki+ also contains no code for updating the controller's firmware.

## 3. Where Meraki+ differs from the stock UI

| | Stock UI | Meraki+ | What it means for you |
|---|---|---|---|
| **Auto off** | Switches the machine off after 30 minutes without use. | **30 minutes by default, like stock** (since v6.23). Selectable: off, 15, 20, 30 or 60 minutes (*SYSTEM → SCHEDULE → AUTO OFF*). An install that already had AUTO OFF saved as *off* keeps it off ([time-scheduling.md §5.1](time-scheduling.md#51-default-and-migration-v623)). | If AUTO OFF is set to *off*, the machine stays on and hot until you switch it off. Check the setting after updating from v6.22 or earlier. |
| **Planned pre-heat** | Hands the next wake time to the controller when the machine is switched off. | The same, and also while the machine is on. Meraki+ keeps its own schedule. | The machine heats **unattended** at the set time. Only plan a pre-heat for a time when you would be comfortable switching the machine on without being there. |
| **Manual hot water** | Runs until stopped; no time limit. | The same (*WATER MODE = MANUAL*, and always on a controller that cannot weigh). | Water runs until you tap **STOP WATER**. Stay at the machine. |
| **Steam during a brew** | Possible (start steam from HOME while a shot runs). | A **HOLD·STEAM** button on the brew screen. | The controller runs both and stops each by itself; checked on the reference machine ([concurrent-brew-steam.md](concurrent-brew-steam.md)). |

The pressure figure Meraki+ shows (≈BAR) is an estimate, not a measurement
([calibration.md](calibration.md)). Do not use it to judge whether anything is safe. It reads about
25 % high on a 100–120 V machine ([mains-voltage.md](mains-voltage.md)).

## 4. What has and has not been tested

- **Reference hardware:** one MerakiTech Gen 1, 220–240 V, controller firmware V5.1.4, stock display
  build 1.1.1-69. Everything Meraki+ sends was worked out from and checked against these.
- **Gen 2: confirmed running** (community report, [issue #1](https://github.com/thomasgrastein/merakitech-plus/issues/1):
  v6.23 installed and running healthy). Detailed hardware verification is still Gen 1 only — Gen 2's controller
  firmware version and whether its register map differs from Gen 1 are not known.
- **Not tested:** other controller firmware versions, 100–120 V machines.
- **Releases are not always checked on a machine before they are published.** Every release passes the
  automated tests (the UI self-test and the simulator). Whether a given build has also run on the reference machine is
  stated per version in [bin/README.md](../bin/README.md) and [CHANGELOG.md](../CHANGELOG.md) — read it
  before you install, and prefer a version marked as verified if that matters to you.

## 5. If something does not behave as expected

1. Tap the red **STOP** button for the running activity.
2. If the display does not respond, **pull the mains plug**. After 10 seconds plug it back in; without
   `--persist` the machine starts the original UI.
3. Go back to the original UI (*gear → STOCK UI → SWITCH TO ORIGINAL NOW*) or remove Meraki+
   (`./revert.sh`), and please open an issue describing what happened.

Never leave the machine unattended while an activity is running.
