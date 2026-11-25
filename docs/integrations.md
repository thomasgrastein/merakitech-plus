# Integrations — one read-only data interface (design, not implemented)

Status: **design for review. Nothing in this file is implemented, and no release contains it.**
It is the technical background for [issue #2](https://github.com/thomasgrastein/merakitech-plus/issues/2)
(shot history, coffee journal, MQTT and Home Assistant). Research date: **2026-11-25**.

The short version:

* Meraki+ gets **one** data interface, the *feed*: a versioned, read-only JSON model of what the machine is doing
  (state, live samples, events, shot records, recipe, maintenance counters).
* The feed has two outputs: **journal files** on the display's own flash, and **MQTT publishing** to a broker on
  your own network through a small helper, `merakiplus_mqtt`, with Home Assistant discovery.
* It is **read-only and outbound-only**. The machine publishes. It never subscribes, never listens and never
  accepts a command. It is **off by default**.
* **Home Assistant over MQTT is served first**: it has by far the largest reach (§2) and needs nothing on the
  machine that accepts input.
* The shot record uses the **same key names as Meraki Companion's journal** where the meaning is the same, and
  **Beanconqueror's `flow` encoding** for the curve. Beanconqueror and Visualizer are reached by a bridge that
  runs on your own computer or phone, never by the machine (§9).
* Meraki Companion's machine service (*Secure Link*) and Meraki+ can be installed on the same machine, but only
  one of them has data at a time (§3.6). §3.7 proposes how the two projects can share one interface.

## Contents

1. [Rules this design must keep](#1-rules-this-design-must-keep)
2. [Who would use it — reach ranking](#2-who-would-use-it--reach-ranking)
3. [Meraki Companion and Secure Link](#3-meraki-companion-and-secure-link)
4. [Architecture](#4-architecture)
5. [Data model](#5-data-model)
6. [MQTT binding](#6-mqtt-binding)
7. [Home Assistant discovery](#7-home-assistant-discovery)
8. [Local shot history and journal](#8-local-shot-history-and-journal)
9. [Mapping to Companion, Beanconqueror and Visualizer](#9-mapping-to-companion-beanconqueror-and-visualizer)
10. [Security review](#10-security-review)
11. [Versioning, Gen 1 and Gen 2](#11-versioning-gen-1-and-gen-2)
12. [Phases](#12-phases)
13. [Coordination and open questions](#13-coordination-and-open-questions)
14. [Sources](#14-sources)

## 1. Rules this design must keep

These rules come from the project's existing posture ([README → Security and OTA](../README.md#security-and-ota),
[CONTRIBUTING](../CONTRIBUTING.md), [safety.md](safety.md)). Every part of the design is checked against them
in §10.

| # | Rule | What it means here |
|---|---|---|
| R1 | **Read-only** | The machine publishes state. Nothing received from the network can start or stop an activity or change a setting. There is no command topic, no RPC and no "apply recipe". The rule that Meraki+ only sends frames the stock UI also sends is unaffected, because this feature sends no controller frames at all. |
| R2 | **Outbound-only** | No new listening port. The helper is a client that connects out to a broker. |
| R3 | **Local** | The broker must be on your own network. The machine contacts no cloud service. |
| R4 | **Opt-in** | Off by default. Without a configuration file nothing starts and nothing is sent. |
| R5 | **Own credentials file** | Broker credentials live in Meraki+'s own file, never in the stock `config.json`. |
| R6 | **No secrets or identifiers in the data** | No Wi-Fi name or password, no addresses, no serial number, no broker credentials in any payload. |
| R7 | **Small helper, separate process** | Network code lives in a small static helper (the `merakiplus_snd` pattern), not in the UI that owns the controller link. A helper fault cannot stall the UI. |
| R8 | **Fits the hardware** | About 22 MB free RAM, small flash, busybox 1.20.2, no usable TLS on the device ([ota-plan.md §3](ota-plan.md)). |

## 2. Who would use it — reach ranking

Numbers were read on 2026-11-25 unless a date is given. "Estimate" marks a number that is not a first-party count.

| Rank | Target | Reach | What kind of number | Fit with §1 |
|---|---|---|---|---|
| 1 | **Home Assistant** through its MQTT integration | **742,102** active installations that opted in to analytics; the same page says fewer than a quarter of users opt in [S1]. The MQTT integration is used by **48.2 %** of active installations [S2] — about 270,000 opted-in installations (271,755 on 2026-10-20 in a third-party snapshot [S3], estimate). The project's own household estimate was 2 million in May 2025 [S4] (estimate). | First-party count of opted-in installs; a lower bound | Very good: local, outbound MQTT, no cloud. The maintainer runs it and can test it. |
| 1b | Other MQTT consumers (Node-RED, openHAB, ioBroker, Grafana stacks) | The Mosquitto broker's official container image shows a 500M+ pull badge [S5] (a tier, not a count); 11,500 GitHub stars [S6] | Proxy numbers only | Served by the same topics at no extra cost. |
| 2 | **Beanconqueror** (bean and brew journal app, Android and iOS) | Google Play **10K+** downloads (the 10,000–49,999 tier), 763 reviews, last updated 2026-11-11 [S7]; App Store (US) 139 ratings [S8]; 829 GitHub stars [S6]. A third-party tracker estimates about 44,000 Android installs [S9] (estimate, not verified). | Store tier and counts | Good: the app is local-first. It imports a finished brew through a documented deep link [S10]. The conversion runs on a phone or computer, not on the machine. |
| 3 | **Visualizer.coffee** (shot sharing web service) | "Over **10,400** coffee people" on its home page [S11]; almost 7,000 users and more than 3.6 million shots in its five-year post, late 2025 [S12]; 163 GitHub stars [S6]. About 2,000 daily visitors in a third-party estimate [S13] (estimate, low confidence). | First-party user count | A cloud service, so the machine never talks to it (R3). It accepts Beanconqueror-format files [S14], so rank 2's format reaches it too. |
| 4 | **Gaggiuino** community | About 22,000 Discord members [S15] (undated listing); 2,706 GitHub stars [S6] | Community size of *another machine's* firmware | Not a consumer of Meraki data. *Gaggiuino Barista* [S16] is a Home Assistant add-on (3 stars) that reads the Gaggiuino firmware's own API. Useful as a pattern only. |
| 5 | **barista-memory** | 1 GitHub star, last updated 2026-11-10 [S17] | Project age and size | Built for GaggiMate machines; a Home Assistant add-on with MQTT entities. A pattern, not a consumer. |
| 6 | **Meraki Companion** (Rotary Pumpers) | Published 2026-11-24 as a source-only developer preview; 0 stars, 0 forks, no release builds [S18] | Project age and size | The only consumer made for this machine, and it already exports to Visualizer and Beanconqueror. See §3. |

What follows from the table:

* **Home Assistant wins by one to two orders of magnitude**, and it is the only target where every rule in §1 holds
  with no extra component. The two journal projects named in issue #2 are both Home Assistant add-ons, so the
  people who ask for a journal are likely to have a broker already.
* All of these numbers are ecosystem sizes. The real audience is the overlap with people who run Meraki+
  (the repository had 5 stars and 4 forks on the research date). The order still holds, and it decides what is
  built first.
* **One shot format reaches both journal targets.** Visualizer parses Beanconqueror's format [S14], and
  Companion's own Visualizer export is built in that shape (§3.4). The shot record in §5.6 is therefore aligned
  with Beanconqueror's fields and curve encoding.
* **Companion has the smallest reach today but the highest leverage per line of code**: if Companion can read a
  Meraki+ shot record, Meraki+ users get its journal, replay and both exporters without Meraki+ writing any of
  them.

## 3. Meraki Companion and Secure Link

Source: the public repository [S18] at commit `f39e41f` (Companion 0.4.60, MPL-2.0), read on 2026-11-25. The
Android client is public. **The machine-side service and its installer are not public** ("owner-test package"),
so everything about the machine side below is what the client code shows, and inferences are marked as such.

### 3.1 What it is

An Android app (8.0 or newer) with a shot journal, replay and comparison, a recipe library, bean bags and usage
totals. It connects over local Wi-Fi to a machine-side service called *Secure Link*. Its README says the machine
integration "adds a background service and makes a reversible startup change" and keeps the original display
program and the controller firmware. So it runs **next to the stock UI**, where Meraki+ **replaces** the UI.

### 3.2 The Secure Link protocol, as seen from the client

| Item | What the client does |
|---|---|
| Transport | TCP to the machine on port **9443**, TLS 1.2 only. The machine is the **server** (a listening port). |
| Trust | The app pins the SHA-256 of the machine's certificate. No trust-on-first-use, no certificate authority. |
| Address check | Only a private IPv4 address is accepted. |
| Pairing | A QR code or file with `protocol: "rp-secure-stream-v1"`, `port`, `certificate_sha256`, `token` (64 hex digits) and optionally `host`. The app stores it encrypted in the Android keystore. |
| Recorder stream | The app sends one line, `RP/1 <token>`. The machine answers with newline-delimited JSON: first one `metadata` record, then `sample` records. The app acknowledges every sample with `ACK <seq>` after it has stored it. |
| `metadata` | `format: "rp-display-cache-v1"`, `stream_protocol: 2`, `source: "display_cache"`, `register_count: 112`, `period_ms: 500`, `sensor_synchronous: false`, plus `display_sha256`, `pid`, `start_ticks` and `app_pointer`. The app refuses the stream unless `display_sha256` equals one fixed value. |
| `sample` | `seq`, `uptime_ns`, `gap_ns`, `read_duration_ns`, `registers` (112 words, 0..65535) and `preferences` (13 bytes: pre-infusion on/off, pressure choice, duration). |
| Control channel | A second connection opened with `RP-TEMP/1 <token>`, protocol `rp-temperature-v1`. The machine first sends a `capabilities` record. Requests: `set_temperature` (85–96 °C), `set_dose` (1.0–30.9 g), `set_yield` (10–99 g), `get_preinfusion` / `set_preinfusion` (on/off only), `get_home` / `set_home` (home screen colour and name), `get_result`. Each request has a UUID and an `epoch`; results are `verified`, `unchanged`, `incomplete`, `rejected`, `busy`, `pending` or `unknown`. |
| What it cannot do | There is no request that starts or stops grinding, brewing, steaming or hot water. The app says so too: activities are started on the machine. |

Two inferences from this, **to be confirmed by the author**:

* The register data is read from the **running stock display program** (`source: "display_cache"`, a process id,
  a start time and a pointer into that process), not from the controller link. The client only accepts one exact
  build of the display program; that build is the stock release 1.1.1-69, the same one Meraki+ was developed
  against.
* The setting changes go through the stock display program as well: results carry fields such as `native_reply`,
  `saved_match`, `return_home` and `home_verified`.

So Secure Link is **a reader and a settings writer with a listening port**. That is a legitimate design for an
app that needs a phone to connect in, with careful authentication. It is a different trade-off from §1, and
Meraki+ will not copy it (R1, R2).

### 3.3 Companion's data models

**Shot** (journal file `journal-v2.json.gz`: `{"version": 2, "shots": [...]}`; a shot's identity is `start_utc`):

| Key | Meaning |
|---|---|
| `start_utc` | ISO 8601 time of the first brew sample, from the phone's clock |
| `complete`, `finished`, `capture_quality` (`complete` / `incomplete` / `gapped`), `completion_reason` | Recording quality |
| `duration_estimate_s`, `duration_bounds_s`, `start_bounds`, `stop_bounds` | Brew time, bracketed by the 500 ms samples |
| `settled_yield_g` | Median of the last three weights once the scale has settled |
| `dose_target_g`, `yield_target_g`, `brew_target_c`, `grind_setting` | Machine targets at the start (see §3.5 for the temperature offset) |
| `pre_infusion` | `{enabled, pressure_choice, duration_setting_s}` |
| `grind_workflow` | The grind before the shot, if it ended at most 300 s earlier: `start_utc`, `duration_estimate_s`, `dose_setting_g`, `grind_setting`, `settled_dose_g`, `cup_movement_detected` |
| `samples[]` | Per sample: `uptime`, `weight_g`, `flow_raw`, `p1_w`, `brew_active`, `water_low`, the targets, and the raw `registers` array |
| `sampling_gaps[]`, `sampling_gap_limit_s`, `auto_tare_wait_s`, `pre_tare_samples_omitted` | Gaps and tare handling |
| Added by the user | `recipe`, `bag` (snapshot with age), `brand_assignment`, `notes`, `taste`, `rating`, `favourite` |

Definitions worth sharing, because they decide whether two tools report the same numbers for the same shot:
a brew shorter than 15 s is a *short event*, not a shot; the yield is settled when three samples at least 5 s after
the stop lie within 0.2 g; a settled weight under 2 g is not a shot; the first drip is the first sustained rise of
0.3 g; cup flow is the slope of the weight over the trailing two seconds.

**Recipe** (file `recipes-v1.json`: `{"format": "rp-companion-recipes", "version": 1, "recipes": [...]}`):
`id` (UUID), `name`, `dose_g`, `yield_g`, `yield_max_g`, `brew_c`, `grind` (a reminder for the person, not sent),
`time_s` / `time_max_s` / `shot_time_window`, `preinfusion_mode` (`keep` / `on` / `off`), `bag_id`, `home_color`,
`home_name`, `notes`.

**Bag** (file `bags-v1.json`, format `rp-companion-bags`): `id`, `name`, `brand`, `roast_date`, `opened_date`.

### 3.4 Its export formats

* **Visualizer:** a JSON file in the shape of a Beanconqueror brew export — `brew` (`brew_time`,
  `brew_beverage_quantity`, `grind_weight`, `grind_size`, `note`, `config.unix_timestamp`), `preparation`,
  `mill`, and `brewFlow.weight[]` / `brewFlow.realtimeFlow[]` with per-sample `brew_time`, `actual_weight` and
  `flow_value` — plus a `meta.meraki` block with provenance. Pressure and temperature curves are left empty on
  purpose ("not measured; omitted"). Direct upload uses Visualizer's OAuth and needs a client ID registered by
  whoever builds the app.
* **Beanconqueror:** the documented `ADD_BREW` version 1 hand-off [S10]: JSON, gzip, unpadded base64url, split
  into 400-character `shareBrewN` parameters of a `beanconqueror://ADD_BREW?len=…` link. The envelope has `v`,
  `app`, `brew` (`date`, `doseIn`, `waterIn`, `beverageOut`, `brewTime`, `temperature`, `grindSize`,
  `grinderName`, `preparationMethod`, `preparationType`, `bloomTime`, `firstDripTime`, `note`), `flow` (`t` in
  milliseconds since the previous sample, `weight` in decigrams since the previous sample, `waterDispensed`),
  `metrics[]`, `bean` and `imported`. Beanconqueror 8.8.2 or newer is needed.

### 3.5 Register cross-check

Companion's client reads the same register words that [registermap.md](registermap.md) documents. It was
written independently, so agreement is useful evidence, and the two differences are worth settling.

| Word | Companion uses it as | Meraki+ documentation | Agreement |
|---|---|---|---|
| 4 | dose target, 0.1 g | grind auto weight, 0.1 g | yes |
| 11 | brew target = word ÷ 10 **+ 5 °C** | brew set temperature, 0.1 °C, shown without an offset | **differs** — the same open question as Q-P1 in [presets-spec.md](presets-spec.md) |
| 16, 22 | hot water / brew in progress | start words for hot water and brew | yes |
| 23 | yield target, 0.1 g | brew auto weight, 0.1 g | yes |
| 33 | bit 6 grinding, bit 9 pump drive, bit 12 brew active | `work_sta` bit field | yes |
| 36 | bit 0 = water low | E3 error word | related to issue #1 |
| 37 | grinder dial setting | grinder gear, computed | yes |
| 38, 39 | grinder and brew scale, signed 0.1 g | the same | yes |
| 41, 42, 47 | range-checked as temperatures | brew boiler, group, steam boiler, 0.1 °C | yes |
| 51 | "P1", electrical power, shown only as a diagnostic | raw pump value, used for a pressure *estimate* | both projects decline to call it a measured pressure |
| 52 | raw flow | flow velocity | yes |

One more observation: a 112-word snapshot includes words 96–111, which hold the machine's serial and product
string on the reference machine. Raw register dumps are therefore identifying data. This is one reason the
Meraki+ feed publishes named fields and not the register array (R6).

### 3.6 Can the two be installed on one machine?

**Verdict: they can share a machine, but they cannot both have data at the same time. This has not been tested
on a machine.** At any moment the display runs either the stock UI or Meraki+.

| Area | Finding |
|---|---|
| Run time | Secure Link's data source is the running stock display program, pinned to one build. While Meraki+ is the UI, that program is not running, so Secure Link has nothing to read and no way to apply settings. With the stock UI running, Secure Link works and Meraki+ is idle. |
| Process name | Meraki+'s launcher and UI run under the stock program's name, because the manufacturer's watchdog only checks the name ([boot.md §1.2](boot.md)). A service that finds the display program by name must check what it found before it reads from it. Companion's client-side build check suggests the service does; this is question J2. |
| Controller link | Only one program can own the serial link to the controller. Nothing in the client suggests Secure Link opens it, so no conflict is expected. Question J3. |
| Startup script | Both projects change startup. Meraki+ touches the vendor boot script only with `--gesture` (one marked line) and the boot flag only with `--persist`. Its revert removes only its own line unless the script is otherwise identical to its backup, so it does not undo another tool's edit. What Secure Link changes is not public. Question J1. |
| False alarm | Meraki+'s `status` reports "the boot script changed since Meraki+ was installed (vendor update?)" when anything else edits that script. A Secure Link install after Meraki+ would trigger that hint. It is only a hint; the fix is to recognise the other tool's marker. |
| Slot B | Meraki+ lives in slot B (`/new_customer`). If Secure Link also stores files or a program there, the two installers must agree on names. Question J4. |
| Network surface | With the stock UI and Secure Link active the machine has an authenticated TLS port that can change settings. That is the Secure Link user's own choice and exists whether or not Meraki+ is installed. Meraki+ itself still opens no port. |
| Memory | The display has about 22 MB of free RAM. A TLS server next to a UI is plausible, but the combination has not been measured. |
| Vendor updates | A manufacturer update can overwrite either project's startup change ([README → Security and OTA](../README.md#security-and-ota)). |

Practical rule until someone has tested it: install one, check it, then install the other; remove them in the
reverse order; and expect live recording in Companion only while the stock UI is on screen.

### 3.7 One machine-side interface instead of two

Two parallel machine-side stacks cost every user of both a second installer and a second thing to break. Four
options, from least to most coupling:

| Option | What happens | Needs from Rotary Pumpers | Needs from Meraki+ |
|---|---|---|---|
| **A. Shared schema** (recommended now) | Both projects use the same names, units and definitions for a shot and a recipe (§5, §9). Companion imports a Meraki+ shot record from a file or from an MQTT bridge and gets replay, journal and both exporters. | Accept a record without raw `registers` — Companion already does this for records whose `source` is its own companion device — and accept `source: "Meraki+"`. Agree on the brew temperature convention (§3.5). | Publish the record exactly as specified, with fixtures. |
| **B. Secure Link reads the Meraki+ feed** | When Meraki+ is the UI, Secure Link takes its samples from Meraki+'s local feed (§4.3) instead of the stock program, and forwards them to the phone over its existing channel. Live recording in Companion then works under Meraki+. | A second source in the recorder (its `metadata.source` field already names the source). | The local feed socket (§4.3). Still no command path: under Meraki+ Companion's "apply" functions stay unavailable, and settings are made on the machine. |
| **C. Companion subscribes to MQTT** | The phone reads the same topics as Home Assistant from the user's broker. No machine-side service is needed for Meraki+ users. | An MQTT client in the app. | Nothing beyond phase 1. |
| D. Meraki+ implements the Secure Link server | — | — | **Rejected.** A listening port and a settings channel break R1 and R2. |

A and C keep the machine free of any listener. B keeps Secure Link's listener, which stays that user's choice
and that project's responsibility.

Companion's README also mentions a Home Assistant dashboard prototype that reads machine data. If it reads the
topics in §6, one dashboard works with both stacks. That is the cheapest alignment of all.

## 4. Architecture

```
 controller (MCU) ──serial──▶ Meraki+ UI (core) ──▶ screen
                                   │
                                   │  feed: one text line per record, one direction only
                                   ├──▶ journal files on flash                          (§8)
                                   │
                                   └──pipe──▶ merakiplus_mqtt (helper)
                                                   │  outbound TCP, publish only
                                                   ▼
                                        MQTT broker on your network
                                                   │
                         ┌─────────────────────────┼──────────────────────────┐
                         ▼                         ▼                          ▼
                  Home Assistant          bridge scripts (§9)         any other MQTT client
              (entities, automations,   Beanconqueror, Visualizer,
                   notifications)              Companion
```

### 4.1 The feed

The UI already keeps a mirror of the controller's registers, polled ten times a second, and already knows the
phase of every activity. The feed is a serialiser on top of that: on a change, or on a timer, it writes one line
`<channel><TAB><JSON><newline>`. Channels are the record types in §5. Producing a line allocates nothing and
does no I/O that can block.

The feed adds **no controller traffic**. It reads values the UI polls anyway.

### 4.2 Binding A — a pipe to the helper (phase 1)

The UI starts the helper with the read end of a pipe as its standard input, as it starts the sound helper today.

* The pipe has one direction. The UI holds only the write end, so there is no descriptor on which the helper —
  or anything behind it on the network — could send data to the UI. The operating system enforces this, not a
  convention in the code.
* The UI writes without blocking. If the pipe is full because the helper is stuck, the line is dropped and a
  counter goes up. Brewing never waits for the network.
* The helper exits when its input closes (the UI ended). The UI never signals the helper, as with sounds; a
  helper that ends is restarted with a growing delay, and after repeated failures not until the next boot or
  until the setting is toggled.

### 4.3 Binding B — a local socket for a second reader (optional, phase 3)

If option B in §3.7 is wanted, the UI can also offer the same lines on a UNIX-domain socket under
`/tmp/merakiplus/`. It is not a network port: it has no address outside the display. The UI shuts down the
receiving direction of every accepted connection and never reads from it. At most two readers; a slow reader
loses lines, it does not slow the UI. This binding is built only if somebody will use it.

### 4.4 The helper: `merakiplus_mqtt`

| Property | Design |
|---|---|
| Build | Static musl ARMv7 binary, no vendor libraries, part of every release and of the signed manifest ([signing.md](signing.md)). Target size about 50 KB without TLS. |
| Protocol | MQTT 3.1.1 over TCP. It can send CONNECT, PUBLISH (QoS 0 and 1), PINGREQ and DISCONNECT, and understands CONNACK, PUBACK and PINGRESP. **The binary contains no SUBSCRIBE packet.** Any other packet from the broker ends the connection. |
| Input | Its standard input (the feed) and its configuration file. Nothing else: no arguments from the network, no environment from the network, no shell. |
| Local-only check | The broker name is resolved, and the connection is refused unless the address is private, link-local or unique-local. There is no override in the first version (R3). |
| Session | Clean session, keep-alive 30 s, a last-will message `offline` on the availability topic. Reconnect with a delay from 2 s up to 60 s, with jitter. |
| On connect | Publishes the discovery document (§7), `info`, `online`, and the latest `state`, `recipe`, `maintenance` and `shot`. Because the helper cannot subscribe, it does not wait for Home Assistant's birth message; the retained discovery document and the republish on every connect cover that case. The display cold-boots every time the machine is switched on ([power.md](power.md)), so "every connect" is at least once per session. |
| Memory | One slot per retained topic (latest value wins) and a ring of 32 events. Live samples are dropped while disconnected. No spooling to flash or to `/tmp`. Target under 1 MB resident. |
| Log | A small ring in `/tmp` (RAM-backed, so capped at 16 KB). The password is never logged. |
| Diagnostics | *SYSTEM → DIAGNOSTICS → INTEGRATIONS*: off / connecting / connected / last error, counters for published, dropped and reconnects. |

### 4.5 Configuration

`/new_customer/merakiplus/mqtt.conf`, owned by Meraki+, mode 0600, written atomically:

```
enabled   = 1
host      = <broker-address>
port      = 1883
user      = merakiplus
pass      = <broker-password>
id        = 3fa9c2d1            # random, made at first setup; not derived from hardware
base      = merakiplus
discovery = 1
discovery_prefix = homeassistant
live      = 1
```

* The file is created from your computer with the installer's existing channel:
  `python3 tools/merakiplus.py mqtt --ip <display-ip> --host <broker-address> --user merakiplus`. It asks for the
  password without echo, supports `--dry-run`, and never writes the password to its log. The round display has no
  keyboard, so the screen only shows the status and an on/off switch (*SYSTEM → INTEGRATIONS*).
* `./revert.sh` removes the file. `mqtt --disable` turns the feature off and keeps the journal.
* The stock `config.json` is never read for this and never written (R5).

### 4.6 Cost on the device

| Resource | Budget |
|---|---|
| RAM | Helper under 1 MB; feed buffers in the UI under 16 KB |
| Flash | Helper about 50 KB per release; journal capped at about 3 MB (§8) |
| CPU | A few lines of text per second; negligible next to drawing the screen |
| Network | State every 5 s at most (about 400 bytes); live samples at 2 per second only during an activity |

## 5. Data model

### 5.1 Conventions

* JSON, UTF-8, one object per record. Every record has a `schema` string, for example `merakiplus.shot/1`.
* **Units are in the key name**: `_c` (°C), `_g` (grams), `_s` (seconds), `_ms`, `_v`, `_hz`, `_bar`. A key ending
  in `_raw` is an uncalibrated controller value with no unit.
* **A missing key means "this machine does not report it"**. It never means zero. Consumers must ignore keys
  they do not know.
* **The same key as Companion means the same thing as in Companion.** Where the meaning is not proven to be the
  same, the key is different on purpose (see `brew_set_c` below).
* Times: `start_utc` is ISO 8601 in UTC from the display's clock. That clock is set only by the stock UI's time
  sync ([time-scheduling.md](time-scheduling.md)), so every record that carries a time also carries `clock_ok`
  (false when the clock is before the build date). `uptime_s` is seconds since boot and always valid.
* Estimates are labelled. Nothing derived from word 51 is called a pressure without the word "estimate"
  ([calibration.md](calibration.md)).

Word numbers below refer to [registermap.md](registermap.md).

### 5.2 `info` — static facts (sent on connect)

```json
{"schema": "merakiplus.info/1", "feed_major": 1, "ui_version": "6.25", "model": "gen1",
 "controller_fw": "V5.1.4", "mains_set_v": 220,
 "caps": ["temps", "weights", "flow_raw", "pump_raw", "preinfusion", "water_dosing", "heat_stage"]}
```

| Key | Source | Note |
|---|---|---|
| `controller_fw` | words 80–87 | The version string the controller reports |
| `mains_set_v` | word 67 | 120 or 220 |
| `model` | installer fingerprint | `gen1`, or `unknown` |
| `caps` | computed | Which optional groups this machine provides (§11) |

No serial number, no network name, no address, no host name (R6).

### 5.3 `state` — slow values (on change, at most every 5 s; retained)

```json
{"schema": "merakiplus.state/1", "uptime_s": 512.4,
 "activity": "idle", "grinding": false, "brewing": false, "steaming": false, "water": false,
 "brew_boiler_c": 92.8, "group_c": 88.1, "steam_boiler_c": 131.5, "milk_c": 23.4,
 "brew_heat_stage": 100, "steam_heat_stage": 100, "brew_ready": true, "steam_ready": true,
 "water_low": false, "faults": [], "e1": 0, "e2": 0, "e3": 0,
 "mains_v": 231, "mains_hz": 50}
```

| Key | Source | Note |
|---|---|---|
| `activity` | word 33 and the UI's phase model | `idle`, `grind`, `brew`, `water`, `steam`, `brew_steam` |
| `brew_boiler_c`, `group_c`, `steam_boiler_c`, `milk_c` | words 41, 42, 47, 48 | 0.1 °C resolution |
| `brew_heat_stage`, `steam_heat_stage` | words 62, 63 | 0 / 30 / 60 / 100; `*_ready` is stage 100 |
| `water_low` | the same rule the UI uses for its own message | |
| `faults` | words 34–36, decoded with the UI's own table | Stable names such as `brew_boiler_sensor`, `steam_boiler_overtemp`, `scale`, `mains`, `water_tank` |
| `e1`, `e2`, `e3` | words 34, 35, 36 | Always sent raw too, so a consumer can cope with a firmware the table does not know |
| `mains_v`, `mains_hz` | words 49, 61 | |

There is no "off" state. When the machine is switched off the display loses power
([power.md](power.md)); the broker then publishes the last-will `offline` (§5.9).

### 5.4 `live` — fast values during an activity (2 per second; not retained)

```json
{"schema": "merakiplus.live/1", "uptime_s": 530.5, "activity": "brew", "phase": "extraction",
 "t_s": 12.5, "brew_weight_g": 14.2, "cup_flow_gs": 1.6, "flow_raw": 212, "pump_raw": 1346,
 "pressure_est_bar": 9.1, "group_c": 90.2}
```

| Key | Source | Note |
|---|---|---|
| `phase` | UI phase model | `preinfusion`, `extraction`, `done` for a brew |
| `brew_weight_g`, `grind_weight_g` | words 39, 38 | Signed, 0.1 g steps, accurate to roughly 0.4 g |
| `cup_flow_gs` | derived | Slope of the weight over the trailing two seconds (the definition in §3.3) |
| `flow_raw` | word 52 | Pump-side flow velocity, not calibrated |
| `pump_raw` | word 51 | Raw pump value |
| `pressure_est_bar` | derived from word 51 | **An estimate** scaled from one anchor point; pump side, not at the puck. Omitted when the estimate has no basis. |

Sent only while something runs, and only if `live = 1`.

### 5.5 `event` — things that happened (not retained, delivered at least once)

```json
{"schema": "merakiplus.event/1", "event_type": "shot_end", "uptime_s": 548.9,
 "start_utc": "2026-11-25T07:14:31Z", "clock_ok": true, "seq": 418}
```

`event_type` is one of: `shot_start`, `shot_end`, `grind_end`, `steam_end`, `water_end`, `brew_ready`,
`steam_ready`, `water_low`, `water_ok`, `fault`, `fault_cleared`. A `fault` event carries `faults` and the raw
codes. The key is named `event_type` because that is what Home Assistant's event entity expects.

### 5.6 `shot` — one record per finished brew (retained; also stored in the journal)

```json
{"schema": "merakiplus.shot/1", "source": "Meraki+", "seq": 418,
 "start_utc": "2026-11-25T07:14:03Z", "clock_ok": true, "uptime_s": 520.4,
 "kind": "espresso", "complete": true, "completion_reason": "target_weight",
 "duration_estimate_s": 28.4, "preinfusion_s": 6.0, "first_drip_s": 7.5,
 "settled_yield_g": 36.2, "yield_at_stop_g": 34.9,
 "dose_g": 18.1, "dose_source": "measured", "ratio": 2.0,
 "dose_target_g": 18.0, "yield_target_g": 36.0, "brew_set_c": 93.0, "group_set_c": 92.0,
 "grind_setting": 12,
 "pre_infusion": {"enabled": 1, "pressure_choice": 4, "duration_setting_s": 6},
 "grind_workflow": {"duration_estimate_s": 9.8, "dose_setting_g": 18.0, "settled_dose_g": 18.1},
 "temps": {"brew_boiler_start_c": 93.1, "group_start_c": 88.0, "group_avg_c": 90.4},
 "pump": {"preinfusion_raw": 358, "extraction_raw": 1346, "pressure_est_bar": 9.1},
 "preset": null, "bean": null,
 "machine": {"model": "gen1", "controller_fw": "V5.1.4", "ui_version": "6.25"},
 "curve": {"fidelity": "full", "t": [0, 250, 250], "weight": [0, 0, 1],
           "flow_raw": [0, 180, 205], "pump_raw": [350, 361, 358]}}
```

| Key | Meaning | Source |
|---|---|---|
| `seq` | Counter that only goes up, kept in the journal. With the device id it identifies the shot even when the clock is wrong. | Meraki+ |
| `kind` | `espresso`, or `short` for a brew under 15 s or under 2 g (a flush) | §3.3 definitions |
| `completion_reason` | `target_weight`, `manual_stop`, `fault`, `timeout` | UI phase model |
| `duration_estimate_s` | Brew start to brew stop | word 33 transitions |
| `preinfusion_s`, `first_drip_s` | Length of the pre-infusion plateau; first sustained 0.3 g rise | words 51, 39 |
| `settled_yield_g`, `yield_at_stop_g` | Weight after the last drips; weight at the stop | word 39 |
| `dose_g`, `dose_source` | Settled weight of the grind that ended at most 300 s before the shot (`measured`), otherwise the target (`target`) | word 38 or word 4 |
| `dose_target_g`, `yield_target_g` | Targets at the start | words 4, 23 |
| `brew_set_c` | The controller's brew set temperature, word 11 ÷ 10, **no offset**. Deliberately not named `brew_target_c`: Companion adds 5 °C to the same word (§3.5), and until that is settled the two keys must not be confused. | word 11 |
| `group_set_c` | Group head set temperature | word 114 |
| `grind_setting` | The grinder dial position the controller reports | word 37 |
| `pre_infusion` | Switch, pressure choice (3 or 4) and time, Companion's key names | words 112, 113 |
| `pump` | Mean raw pump value in each phase, and the labelled estimate | word 51 |
| `preset` | `{id, name}` of the active preset once [presets](presets-spec.md) exist, else null | presets file |
| `bean` | Always null from the machine in version 1. The machine does not know which coffee is in the hopper. Bean, roaster, roast date, taste and rating belong to the journal that receives the record (Home Assistant, Beanconqueror, Companion) and are attached there by `seq`. | — |
| `curve` | Parallel arrays. `t` is milliseconds since the previous sample and `weight` is decigrams since the previous sample — **Beanconqueror's `flow` encoding** [S10], which is also the controller's native 0.1 g unit. `flow_raw` and `pump_raw` are absolute. Four samples per second; `fidelity` becomes `downsampled` for brews over 120 s. | words 39, 52, 51 |

A full record is at most about 8 KB. A summary without `curve` is about 600 bytes.

### 5.7 `recipe` — the settings in force (on change; retained)

```json
{"schema": "merakiplus.recipe/1",
 "grind": {"mode": "weight", "dose_target_g": 18.0, "grind_setting": 12},
 "brew": {"yield_target_g": 36.0, "brew_set_c": 93.0, "group_set_c": 92.0,
          "pre_infusion": {"enabled": 1, "pressure_choice": 4, "duration_setting_s": 6}},
 "steam": {"gear": 4, "milk_target_c": 60.0},
 "water": {"mode": "auto", "target_g": 150},
 "preset": null}
```

Read-only exposure of words 2, 4, 23, 11, 114, 112/113, 14, 25, 16/19 and 17. When presets ship, this record
shows the *effective* values ([presets-spec.md §3.2](presets-spec.md)) and names the preset. Nothing can be
written back: a recipe is changed on the machine, or later by a preset or an NFC tag, never over the network.

### 5.8 `maintenance` — counters (after each activity; retained)

```json
{"schema": "merakiplus.maintenance/1", "counting_since_utc": "2026-12-01T00:00:00Z",
 "shots_total": 418, "short_events_total": 37, "grinds_total": 431, "ground_g_total": 7790.5,
 "brew_out_g_total": 15120.0, "hot_water_g_total": 9300.0,
 "steam_s_total": 5210, "pump_s_total": 14890, "on_s_total": 1310400}
```

* Every counter only goes up. They start when the feature is first enabled; the machine has no older history.
* **Reminders are computed by the consumer.** "Backflush after 50 shots" or "descale after so many litres" is a
  Home Assistant automation over these counters, with its own "done" button on the Home Assistant side. That
  keeps the machine free of any reset command (R1) and lets every user choose their own intervals.
* Water through the machine is only known as cup weight, hot water weight and steam time. It is a lower bound
  for descaling, not a meter reading.
* Faults and the low-water state arrive as events (§5.5), which is what a notification needs.

### 5.9 Availability

`online` when the helper connects, `offline` as the broker's last-will message. Because the display loses power
when the machine is switched off, `offline` is the normal resting state, and every retained record stays readable
while the machine is off.

## 6. MQTT binding

With `base = merakiplus` and `id = 3fa9c2d1`:

| Topic | Record | QoS | Retained | When |
|---|---|---|---|---|
| `merakiplus/3fa9c2d1/availability` | `online` / `offline` | 1 | yes | connect; last will |
| `merakiplus/3fa9c2d1/info` | §5.2 | 1 | yes | connect |
| `merakiplus/3fa9c2d1/state` | §5.3 | 0 | yes | on change, at most every 5 s |
| `merakiplus/3fa9c2d1/live` | §5.4 | 0 | no | 2 per second during an activity |
| `merakiplus/3fa9c2d1/event` | §5.5 | 1 | no | as they happen |
| `merakiplus/3fa9c2d1/shot` | §5.6 without `curve` | 1 | yes | shot end |
| `merakiplus/3fa9c2d1/shot/full` | §5.6 with `curve` | 1 | yes | shot end |
| `merakiplus/3fa9c2d1/recipe` | §5.7 | 1 | yes | on change |
| `merakiplus/3fa9c2d1/maintenance` | §5.8 | 1 | yes | after each activity |
| `homeassistant/device/merakiplus_3fa9c2d1/config` | §7 | 1 | yes | connect, if `discovery = 1` |

There are **no topics the machine reads**. Recommended broker setup: a dedicated user that may only *write* to
`merakiplus/#` and to its one discovery topic, and may read nothing (§10).

## 7. Home Assistant discovery

One *device discovery* document describes all entities [S2]. It needs a Home Assistant version with device-based
MQTT discovery (the 2024.11 line or newer; to be confirmed in the first test). Abbreviated example:

```json
{
  "dev": {"ids": ["merakiplus_3fa9c2d1"], "name": "Meraki", "mf": "MerakiTech",
          "mdl": "Gen 1 with Meraki+", "sw": "6.25", "hw": "controller V5.1.4"},
  "o": {"name": "merakiplus_mqtt", "sw": "1", "url": "https://github.com/thomasgrastein/merakitech-plus"},
  "avty_t": "merakiplus/3fa9c2d1/availability",
  "cmps": {
    "brew_boiler": {"p": "sensor", "name": "Brew boiler", "uniq_id": "merakiplus_3fa9c2d1_brew_boiler",
                    "stat_t": "merakiplus/3fa9c2d1/state", "val_tpl": "{{ value_json.brew_boiler_c }}",
                    "dev_cla": "temperature", "unit_of_meas": "°C", "state_class": "measurement"},
    "brew_ready": {"p": "binary_sensor", "name": "Brew ready", "uniq_id": "merakiplus_3fa9c2d1_brew_ready",
                   "stat_t": "merakiplus/3fa9c2d1/state",
                   "val_tpl": "{{ 'ON' if value_json.brew_ready else 'OFF' }}"},
    "shots_total": {"p": "sensor", "name": "Shots", "uniq_id": "merakiplus_3fa9c2d1_shots_total",
                    "stat_t": "merakiplus/3fa9c2d1/maintenance", "val_tpl": "{{ value_json.shots_total }}",
                    "state_class": "total_increasing"},
    "last_shot": {"p": "sensor", "name": "Last shot", "uniq_id": "merakiplus_3fa9c2d1_last_shot",
                  "stat_t": "merakiplus/3fa9c2d1/shot", "dev_cla": "timestamp",
                  "val_tpl": "{{ value_json.start_utc if value_json.clock_ok else now().isoformat() }}",
                  "json_attributes_topic": "merakiplus/3fa9c2d1/shot"},
    "machine_event": {"p": "event", "name": "Machine event", "uniq_id": "merakiplus_3fa9c2d1_event",
                      "stat_t": "merakiplus/3fa9c2d1/event",
                      "event_types": ["shot_start", "shot_end", "grind_end", "steam_end", "water_end",
                                      "brew_ready", "steam_ready", "water_low", "water_ok",
                                      "fault", "fault_cleared"]}
  }
}
```

Planned entities:

| Entity | Type | From | Default |
|---|---|---|---|
| Brew boiler, group, steam boiler, milk temperature | sensor (temperature) | `state` | on |
| Brew ready, steam ready | binary sensor | `state` | on |
| Activity | sensor (enum) | `state` | on |
| Water low, fault | binary sensor (problem) | `state` | on |
| Fault names | sensor (text) | `state` | on |
| Last shot (time, with the summary as attributes) | sensor (timestamp) | `shot` | on |
| Last shot yield, time, dose, ratio | sensors | `shot` | on |
| Shots, grinds, coffee ground, steam time, hot water, pump time | sensors (total increasing) | `maintenance` | on |
| Machine event | event | `event` | on |
| Controller firmware, mains voltage, mains frequency | sensors (diagnostic) | `info`, `state` | on |
| Live cup weight, cup flow | sensors | `live` | **off** — two values per second would fill the recorder |
| Pressure (estimate), raw pump, raw flow | sensors (diagnostic), named as estimates, no pressure device class | `live` | **off** |

Rules for the discovery document, checked by a test:

* It contains **no command topic** and no entity type that needs one (no switch, button, number, select or text).
* Entity ids come from the random `id`, never from hardware identifiers.
* The curve is not attached to any entity (`shot/full` has no entity), so the recorder's attribute size limit is
  never an issue.

Two things that then work with no code on the machine:

* **"Tell me when it is ready"** — an automation on *Brew ready* turning on.
* **Maintenance reminders** — a helper counter that follows *Shots* and a notification at a chosen threshold, with
  a "done" button that resets the helper inside Home Assistant.

## 8. Local shot history and journal

The journal works with no network at all and is the base for an on-screen history page.

| File (in `/new_customer/merakiplus/journal/`) | Content | Written | Size cap |
|---|---|---|---|
| `ledger.jsonl` | One summary line per shot (§5.6 without `curve`), about 600 bytes | Appended and synced once per shot | 512 KB, then rotated once (`ledger.1.jsonl`): at least the last 850 shots |
| `curves.jsonl` | Full records of the most recent shots | Appended once per shot | 1 MB, rotated once: at least the last 125 shots, typically 250–500 |
| `counters.json` | §5.8 | Replaced atomically after an activity | under 1 KB |

* **Flash wear:** at most three small writes per shot, none per sample. The curve is held in RAM during the brew.
  Nothing of this goes to `/tmp`, which is RAM.
* **Power cuts:** a line is complete or absent; a reader skips a broken last line. `counters.json` is written to
  a temporary file, synced and renamed, like every other Meraki+ file.
* **Clock:** each line has `seq`, `uptime_s`, `start_utc` and `clock_ok`. A journal reader orders by `seq`.
* **Getting it off the machine:** `python3 tools/merakiplus.py journal --ip <display-ip> --out shots.jsonl`
  copies the files over the installer's existing channel. Read-only on the display.
* **Catching up over MQTT** (phase 2): after a broker outage the helper can publish the ledger lines the broker
  has not acknowledged, by `seq`. Until then, `shots_total` lets a consumer notice a gap.
* **Removal:** `./revert.sh --purge` deletes the journal; a plain revert keeps it.
* **Journal on the screen** (phase 3): *LAST SHOTS* — time, yield, dose, ratio, and a small curve. Tasting notes
  are out of scope for a round display with no keyboard; they belong in the app that receives the record.

## 9. Mapping to Companion, Beanconqueror and Visualizer

| Meraki+ shot (§5.6) | Companion journal | Beanconqueror `ADD_BREW` | Visualizer (Beanconqueror export shape) |
|---|---|---|---|
| `start_utc` | `start_utc` | `brew.date` | `brew.config.unix_timestamp` |
| `duration_estimate_s` | `duration_estimate_s` | `brew.brewTime` | `brew.brew_time` |
| `settled_yield_g` | `settled_yield_g` | `brew.beverageOut` | `brew.brew_beverage_quantity` |
| `dose_g` (when measured) | `grind_workflow.settled_dose_g` | `brew.doseIn` | `brew.grind_weight` |
| `dose_target_g`, `yield_target_g` | same keys | note / `imported.params` | `meta` |
| `brew_set_c` | `brew_target_c` − 5 (open, §3.5) | `brew.temperature` | `meta` |
| `grind_setting` | `grind_setting` | `brew.grindSize` | `brew.grind_size` |
| `pre_infusion` | `pre_infusion` (same keys) | `brew.bloomTime` when on | `meta` |
| `first_drip_s` | computed by the app | `brew.firstDripTime` | `meta` |
| `curve.t`, `curve.weight` | `samples[].uptime`, `samples[].weight_g` (running sums) | `flow.t`, `flow.weight` **as they are** | `brewFlow.weight[]` (running sums) |
| `curve.flow_raw`, `curve.pump_raw` | `samples[].flow_raw`, `samples[].p1_w` × 10 | `metrics[]` (kind `measured`, unit named as raw) | not sent as pressure |
| — | — | `brew.waterIn` is required: 0 ml, as Companion sends | — |
| `bean` (null) | `bag` | `bean` | `meta` |

Who converts, and where:

| Path | Runs on | Machine involved? |
|---|---|---|
| Home Assistant entities and automations | your Home Assistant | publishes only |
| Shot → Beanconqueror | a small reference script (planned in `tools/bridge/`) on the Home Assistant host or any computer; it builds the `ADD_BREW` link and sends it to the phone as a notification to tap. Or Companion. | no |
| Shot → Visualizer | the same script, or Companion, uploading with **your** Visualizer account from **your** computer or phone | no — the machine never contacts a cloud service |
| Shot → Companion journal | file import, or option C in §3.7 | no |

The bridge scripts are ordinary programs for a computer. They are not installed on the machine and are not part
of what the installer pushes.

## 10. Security review

### 10.1 Against the rules

| Rule | How the design keeps it | How it is tested |
|---|---|---|
| R1 read-only | The UI has only the write end of a pipe. The helper has no SUBSCRIBE and treats an incoming PUBLISH as an error. Discovery has no command topic. The feed sends no controller frames. | A mock broker asserts that no SUBSCRIBE is ever sent and that an injected PUBLISH closes the connection. A test parses the discovery document for command keys. The frame-policy self-test shows an unchanged set of frames with the feed on and off. |
| R2 outbound-only | The helper never calls bind, listen or accept. Binding B is a UNIX socket with no network address. | Static check of the helper's imports; a port scan of the mock display before and after enabling. |
| R3 local | The broker address must be private, link-local or unique-local. No cloud endpoint exists in the code. | Unit test of the address check with public, private and mapped addresses. |
| R4 opt-in | No file, or `enabled = 0`, means the helper is not started. | Installer test: a default install has no helper process and no outbound connection. |
| R5 own file | `mqtt.conf` under `/new_customer/merakiplus/`; `config.json` untouched. | The existing "config.json unchanged" md5 checks, run with the feature on. |
| R6 no secrets | Payloads are built from a fixed list of fields. The id is random. | A test feeds a configuration with marker strings and asserts none of them, and no serial block content, appears in any payload or log. |
| R7 helper isolation | Separate process, non-blocking writes, no signals, restart with back-off. | Simulator scenario: the helper hangs or the broker blackholes traffic during a brew; the brew timeline is unchanged. |
| R8 resources | Budgets in §4.6. | Measured on the ARM build in the 32-bit container, then on a machine. |

### 10.2 What remains, stated plainly

| Risk | Detail | Mitigation |
|---|---|---|
| **The password can be read by anyone on your network** | The display has the manufacturer's open root telnet ([README](../README.md#security-and-ota)), so file permissions on `mqtt.conf` protect nothing against someone on the same network. | Use a **dedicated broker user** for the machine that may only publish to its own topics. Then a stolen password allows fake coffee data and nothing else. The setup tool prints this advice and a sample access rule. Keep the machine on an IoT network, as the README already says. |
| **Plain MQTT** | In phase 1 the password and data cross your network unencrypted, because the device has no TLS a helper can rely on. | Same as above; do not reuse a password. Phase 3 adds TLS with a pinned broker certificate, using the TLS client planned for the updater ([ota-plan.md](ota-plan.md)), with no dependence on the clock. |
| **Setup channel** | The configuration is pushed over the same unencrypted channel the installer uses. | Nothing new; stated in the tool's output. |
| **Data on the broker** | Retained records show when coffee is made. Home Assistant keeps history. | Local only; documented. `mqtt --disable --clear` publishes empty retained messages to remove them. |
| **A hostile or broken broker** | It can only send bytes to the helper. | Fixed-size parser for three packet types; anything else disconnects. The helper has no path to the UI. |
| **A fake machine** | Someone with the password could publish false states. | Consequence limited to wrong data. Do not let safety depend on these values; the README already says this about pressure and weight. |
| **Wrong clock** | Timestamps can be wrong after a power cut. | `clock_ok`, `seq` and `uptime_s`; consumers may stamp on arrival. |
| **Flash wear or a full flash** | Journal writes. | Caps in §8; a failed write disables the journal for the session and shows a diagnostics line; brewing is unaffected. |

Out of scope: closing the manufacturer's telnet, and the security of Secure Link, which belongs to its authors.

## 11. Versioning, Gen 1 and Gen 2

The register map was reversed on one Gen 1 with controller firmware V5.1.4. Gen 2 may differ, and a first Gen 2
owner has already reported a difference (issue #1).

* **`schema` on every record.** Within one major version, changes only add keys. A consumer ignores unknown keys
  and treats a missing key as "not available".
* **A breaking change gets a new topic** (`shot2`, `state2`), and the old one keeps being published for at least
  two releases.
* **`caps` in `info`** lists the groups this machine really provides. On a controller the UI does not fully know,
  the feed publishes only groups it has verified for that firmware, plus the raw `e1`/`e2`/`e3` and the
  `controller_fw` string so a report is useful. It never guesses a value.
* **Raw values stay raw.** `flow_raw` and `pump_raw` are published without a unit, so a later calibration adds new
  keys instead of changing the meaning of old ones.
* **Machine-readable definitions.** Phase 0 adds JSON Schema files and example payloads under `docs/schema/`,
  generated from simulator scenarios, so that other projects can test against them without a machine.

## 12. Phases

Ordered by the reach ranking in §2. Each phase is a separate, approved release; none is scheduled.

| Phase | Serves | Content | On the machine |
|---|---|---|---|
| **0 — agree the schema** | everyone | Review of this document with Rotary Pumpers and the issue #2 author. JSON Schema and fixture payloads from the simulator. The maintainer publishes the fixtures to his own broker from a laptop and checks the Home Assistant entities, automations and recorder load. | nothing |
| **1 — feed, journal, MQTT, Home Assistant** | rank 1 | The feed in the UI; shot detection and counters; journal files; `merakiplus_mqtt` (plain MQTT, local broker); discovery; setup tool; diagnostics page. Off by default. | UI + one helper |
| **2 — shot bridges** | ranks 2 and 3 | `shot/full` with the curve; reference bridge scripts to Beanconqueror and Visualizer; journal download; catch-up after an outage; import path into Companion if agreed. | small additions |
| **3 — hardening and options** | ranks 1–3, Companion | TLS with a pinned certificate; the local feed socket if Secure Link will use it; presets named in `recipe` and `shot`; the on-screen history page. | helper + UI |
| Not planned | — | Any command path, any listening port, any cloud connection from the machine, tasting notes on the display. | — |

## 13. Coordination and open questions

### 13.1 Proposal to Rotary Pumpers

1. **Share the shot and recipe vocabulary** (§5.6, §5.7, §9): same key, same unit, same definition — including the
   15 s, 2 g, 300 s and 0.3 g thresholds in §3.3, which Meraki+ would adopt as they are.
2. **Companion imports `merakiplus.shot/1`** without raw registers, the way it already accepts summary records
   from its own companion device.
3. **One set of MQTT topics for Home Assistant** (§6), so the dashboard prototype mentioned in Companion's README
   works with either machine-side stack.
4. **Coexistence by markers:** each installer adds and removes only its own marked lines in the vendor boot
   script, never restores a whole-file backup over the other's change, and recognises the other's marker.
5. **Later, if wanted:** Secure Link reads the Meraki+ feed when Meraki+ is the UI (§3.7 B).

Who exposes what: Meraki+ exposes read-only data when Meraki+ is the UI. Secure Link exposes data and settings
when the stock UI is. Neither needs to run the other's code.

### 13.2 Questions for Rotary Pumpers

| # | Question |
|---|---|
| J1 | What exactly is the "reversible startup change"? Which files are edited, and does the installer keep a whole-file backup that a later removal restores? |
| J2 | How does the service find the display program, and what does it do when that program is not the pinned stock build (as under Meraki+)? |
| J3 | Does the service ever open the controller's serial link or send controller frames, or do all setting changes go through the stock display program? |
| J4 | Does anything of Secure Link live in slot B (`/new_customer`) or depend on the boot flag? |
| J5 | Brew temperature: Companion shows word 11 ÷ 10 + 5 °C. Is the + 5 what the stock brew page shows, or only the preset and tag convention? (Our Q-P1.) |
| J6 | Word 51: is "P1" an identified electrical power reading, and of which circuit? It would settle the pressure question for both projects. |
| J7 | Would Companion accept a record with `source: "Meraki+"`, `seq`, `clock_ok` and the delta-coded `curve`, or is a `samples[]` array preferred? |
| J8 | Is the Home Assistant dashboard going to read MQTT? If so, are the topics and keys in §5–§6 acceptable, and what is missing? |
| J9 | Is the Secure Link service going to be published, and under which licence? |
| J10 | Which machine generations and controller versions has Secure Link run on? |

### 13.3 Questions for the author of issue #2

| # | Question |
|---|---|
| K1 | Which broker and which Home Assistant version do you run, and can the broker have a dedicated publish-only user? |
| K2 | Is one state update every 5 s, plus two live samples per second during a shot, enough for what you want to build? |
| K3 | Which maintenance reminders matter to you (backflush, descale, water filter, grinder cleaning), and are consumer-side counters acceptable? |
| K4 | For the journal: Beanconqueror, Visualizer, your own database, or all three? Would you run a small bridge script on your Home Assistant host? |
| K5 | Which machine generation and controller version do you have (*SYSTEM → DIAGNOSTICS → CONTROLLER*)? |
| K6 | Would you review or contribute the bridge scripts or the Home Assistant side? |

### 13.4 What the maintainer can test first

All of step 1 needs no change on the machine.

1. **Now:** create a broker user that can only publish to `merakiplus/#` and the discovery topic. Publish the
   example payloads from §5 and §7 with `mosquitto_pub` from a laptop. Check that the device and its entities
   appear, that *Brew ready* and *Machine event* can trigger an automation, that the availability topic turns
   everything unavailable, and that the recorder stays small with the live entities off.
2. **Phase 1 on the simulator:** run the UI's simulator build with the helper against the same broker and replay
   the recorded brew scenarios; compare each published shot with the scenario.
3. **Phase 1 on the machine:** enable it, pull one shot, compare the record with the screen (weight, time, dose);
   switch the machine off and on and check `offline` / `online` and rediscovery; stop the broker during a brew and
   confirm the brew is unaffected.

## 14. Sources

All read on 2026-11-25.

| | Source |
|---|---|
| S1 | Home Assistant Analytics — <https://analytics.home-assistant.io/> |
| S2 | Home Assistant MQTT integration documentation (usage share, discovery) — <https://www.home-assistant.io/integrations/mqtt/> |
| S3 | MQTT statistics, third-party snapshot of Home Assistant analytics, 2026-10-20 — <https://mqttviewer.app/learn/mqtt-statistics/> |
| S4 | Home Assistant 2025.5 release notes, "Two Million Strong" — <https://www.home-assistant.io/blog/2025/05/07/release-20255/> |
| S5 | Eclipse Mosquitto official image — <https://hub.docker.com/_/eclipse-mosquitto> |
| S6 | GitHub repository statistics (API): `eclipse-mosquitto/mosquitto`, `graphefruit/Beanconqueror`, `miharekar/visualizer`, `Zer0-bit/gaggiuino` |
| S7 | Beanconqueror on Google Play — <https://play.google.com/store/apps/details?id=com.beanconqueror.app> |
| S8 | Beanconqueror on the App Store (iTunes lookup API, US storefront) |
| S9 | AppBrain estimate for Beanconqueror — <https://www.appbrain.com/app/beanconqueror/com.beanconqueror.app> |
| S10 | Beanconqueror brew import API, `docs/brew-import-api.md` (develop branch) — <https://github.com/graphefruit/Beanconqueror> |
| S11 | Visualizer home page — <https://visualizer.coffee/> |
| S12 | Visualizer, "Thank you for the first 5 years" — <https://visualizer.coffee/updates/thank-you-for-the-first-5-years> |
| S13 | StatsCrop estimate for visualizer.coffee — <https://www.statscrop.com/www/visualizer.coffee> |
| S14 | Visualizer source, `app/models/parsers/` (Beanconqueror, Decent, Gaggiuino, GaggiMate, Meticulous parsers) — <https://github.com/miharekar/visualizer> |
| S15 | Gaggiuino Discord invite listing — <https://discord.com/invite/gaggiuino-890339612441063494> |
| S16 | Gaggiuino Barista — <https://github.com/nikosiaf/gaggiuino-barista> |
| S17 | barista-memory — <https://github.com/biokys/barista-memory> |
| S18 | Meraki Companion Preview, commit `f39e41f` — <https://github.com/RotaryPumpers/Meraki-Companion-Preview> |

Product names belong to their owners and are used only to say what the data is meant to work with. This project
is not affiliated with any of them.
