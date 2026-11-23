# Mains voltage: 120 V / 60 Hz vs 230 V / 50 Hz

**Short answer:** Meraki+ does not depend on the mains voltage. What matters is the machine itself.
The controller firmware (MEM03 5.1.4) has two modes, **120 V** and **220 V**. A **100–120 V machine**
(the North American version sold by the US/CA store) runs normally on 120 V / 60 Hz. A **220–240 V
machine** on 120 V stops at power-on with an **E1 mains-voltage error**, and the same happens the
other way round. The machine is **not dual-voltage**.

Meraki+ has been tested on a 230 V / 50 Hz machine only. Everything below about 120 V comes from
static analysis of the controller firmware and the stock UI, plus the vendor's published specs. It
has not been run on a 120 V machine.

## What the controller firmware does

| What | Firmware evidence (MEM03 5.1.4 flash offsets) |
|---|---|
| **`set_voltage`** (word 67) is a config byte in the controller's data flash, with value 120 or 220 | config load/save `0x7A2` (index 1 → config[0x3C], "set.voltage = %d") |
| A blank config defaults to **120** | config init `0x950`: version byte ≠ 3 → `set_voltage = 120` |
| **Boot check** (power-on only): the measured mains voltage (word 49) must lie in a window that depends on `set_voltage` | `0xA8F6`, called from the init task `0x2C86`: 120 → **85–135 V**, 220 → **170–265 V** |
| Failure = more than 30 consecutive out-of-window samples (100 ms each, within the first ~5 s) → **E1 bit 0** | `0xA94E` `orr #1` into word 34 |
| After E1 at boot, the init task returns **without starting the `run`, `boiler_heat` and `head_heat` tasks**: no heating, no relays. The `status` task (display link) still runs, so the error is shown | `0x2C50..0x2C7C`, `0x2D08 → 0x2E1C` |
| If `set_voltage` is neither 120 nor 220 (a corrupt value), the firmware picks 120 or 220 from the measured voltage and saves it | `0xA934..0xA980` |
| A missing or failed metering IC (chip ID ≠ `0x705321`) → word 36 (E3) bit 13 (`0x2000`), same lockout | `0xA986..0xA99C` |
| **Frequency** (word 61): 48–52 Hz is shown as 50 and 58–62 Hz as 60. **No frequency error exists.** Hz only sets a half-cycle timing value (100000 / 2·Hz) | `0xA880..0xA898`, `0x1CB6` |
| `set_voltage` also changes the tuning: group-head heater power cap (220 V: max 5/8 or 4/8; 120 V: 8/8) and pre-infusion pump-loop targets | head heat `0x3ACA`, `0x4C68`; recipe `0x1E10`, pump loop `0x1D28` |
| The boiler heater tasks do **not** read `set_voltage` | only readers of word 67/config[0x3C] are listed above |

**How `set_voltage` is written:** only through factory/service paths on the controller (UART3 cmd 2
payload `[flag, 120|220]` → product task `0x55CA`; factory-test task `pcb_product` frame cmd 9 →
`0x5836`). The stock UI **never writes it**. It is outside the display's write band (words 0..31 +
112..114), and Meraki+ does not write it either.

## What the stock UI and Meraki+ show

- Stock UI: the dashboard shows *Voltage setting* (word 67), *Electric voltage* (word 49) and
  *Electric frequency* (word 61). The E1 voltage page says **"Voltage error. Unplug immediately"** plus
  **"Powered by AC 100-120V"** if word 67 = 120, otherwise **"Powered by AC 220-240V"**.
- Meraki+: the **MAINS** cell at the top of *DIAGNOSTICS* shows word 49 / word 61 as `V · HZ`. E1 bit 0 is
  shown on HOME as **PROBLEM WITH MAINS VOLTAGE**. Meraki+ has no other mains logic.

**One known Meraki+ caveat on 120 V machines: the pressure estimate.** Meraki+'s ≈BAR estimate is
anchored to the pre-infusion plateau of word 51 measured on a 220 V machine (raw 358 = 4 bar, see
[calibration.md](calibration.md)). The controller regulates word 51 to targets that depend on the
voltage variant (`0x1E10`). For 4 bar the target is 350–370 at 220 V but **440–460 at 120 V**, and for
3 bar it is 300–320 vs 340–360. On a 120 V machine the estimate will therefore read **about 25 % high**
(for example ≈5 bar for a 4 bar pre-infusion). The raw value shown next to it is unaffected. The MCU's
own debug output calls word 51 `power_p1`, which suggests it is the pump's electrical power from the
metering IC, not a pressure.

## Vendor statements

- Meraki FAQ: the machine "is not dual-voltage" and is configured for the shipping destination:
  110-120V / 60Hz (default for US, CA) and 220-240V / 50-60Hz (default for EU, AU).
  <https://www.merakitech.com/pages/faq-page>
- Meraki voltage guide: two versions, 100–120 V (North America) and 220–240 V. They "share the
  hardware, boilers and pump, and differ in the heating configuration", which is why they are "not
  interchangeable". The guide advises against converters or rewiring (warranty).
  <https://www.merakitech.com/blogs/machine-guides/espresso-machine-voltage-and-plugs-us-uk-eu-and-au-explained>
- Product manual: "100-120V / 220-240V 50-60Hz", rated power "1600w at 120V, 1800w at 230V".
  <https://cdn.shopify.com/s/files/1/0487/2015/0691/files/Meraki_Espresso_Machine_Product_Manual.pdf>

The heater elements' ratings cannot be read from firmware. The firmware treats both variants the same
except for the points listed above. The vendor says the versions differ in the heating configuration.

## For owners on 120 V / 60 Hz

1. Check the rating plate on the machine (100–120 V or 220–240 V). Machines bought from the US/CA
   store are 100–120 V.
2. In Meraki+, open *DIAGNOSTICS*. The **MAINS** cell should show about 110–125 V and 60 Hz, and there should
   be no E1. In the stock UI, *Voltage setting* should read 120 V.
3. If E1 (mains voltage) appears at power-on, the machine's configuration does not match the
   outlet, or the supply is outside 85–135 V. **Unplug it** and contact the vendor.
4. **Do not try to change `set_voltage`.** It is a factory setting that must match the heater
   hardware. Setting a 220–240 V machine to 120 would only bypass the protection.
5. Treat the ≈BAR pressure estimate as about 25 % high (see above) until it is calibrated on a 120 V
   machine. Sharing a DIAGNOSTICS photo plus a pre-infusion graph in an issue would let us fix the anchor.
