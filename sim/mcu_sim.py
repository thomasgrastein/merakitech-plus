#!/usr/bin/env python3
"""mcu_sim.py — complete SIMULATED powerMcu (MerakiTech Gen 1) for E2E tests.

Speaks the UART protocol (PROTOCOL.md) over a pty that merakiplus's /dev/ttyS1
is redirected to in the SIM build. No contact with a physical device — local development only.

Model:
  * 129-word register file initialized from the sandbox config.json (PARITY §1.3,
    incl. byte packing word112/113/114) + MCU defaults + sensor baseline.
  * cmd3 read query → 0x83 reply with exactly the sub/len requested (≤32 B
    chunked reads are handled by the client's parser; the sim replies in whole frames).
  * cmd6 write → band validation (0..31 + 112..114), trigger policy (26/27/30
    must be 0, otherwise REJECT+log), range log (parity limits), memcpy+ACK echo
    (0x86) with a configurable latency/loss/retransmit profile.
  * cmd8 → 0x88 echo; cmd9 reply to the 0x89 version request (every 20 ms until a reply).
  * Boot push (sub1 len30 + sub112 len3) is expected/logged on connect.
  * Phase machine with synthetic sensor curves calibrated against BREW.md/HOTWATER.md/
    SESSION.md (grind/brew/hotwater/steam/sleep/drain/idle pulses/heating).
  * Fault injection: E1/E2/E3, CRC garbage, missing ACKs, offline, wand,
    out-of-spec config, cup removal, reboot.
  * Manual STOP (v6.13, SIM_HANDOFF/UI_v6 §18): cmd6 sub16/sub19=0 (water,
    also in auto mode), sub22=0 (brew), sub6=0 (grind) run the MCU's own
    auto-stop path (post/drip/decay) and are ACKed; auto-stop is unchanged.
  * DEEP SLEEP/WAKE (v6.14, SIM_HANDOFF/UI_v6 §19): word0=0 (cmd6 sub0 len2 [0,0])
    → ACK, then ≤0.5 s until deep sleep (sleep task 0x2F52): work_sta=0, outputs
    0, activities are stopped safely, regs are kept, and the UART is SILENT (the status
    task is deleted → no replies to cmd3/cmd6, no 0x89). Keep-awake monitoring
    pauses while powered off. Wakes ONLY on a button hold ≥0.8 s
    (ctrl power_key) or an RTC alarm (cmd8's alarm field) → CPU reset to the
    FACTORY start values (word0=1, word11=920, word114=50 …) + 0x89 round →
    the host's boot push/re-push is expected again (config is restored).
  * Assertion log (JSONL): ALL writes w/ validation + keep-awake gap (>30 s)
    + trigger/band/range violations → read by run_e2e.py.
  * REPLAY: serve (replay frames.csv as ground truth on live polls) and verify
    (run the phase machine against captured writes and compare replies frame by frame
    with documented tolerances).

Python stdlib only. Usage: see README.md / --help.
"""
import argparse
import bisect
import calendar
import csv
import gzip
import heapq
import json
import math
import os
import random
import select
import socket
import struct
import sys
import termios
import time
import tty
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
from proto import (REGN, WS_IDLE, WS_SLEEP, WS_GRIND, WS_BREW_PRE, WS_BREW_EXT,
                   WS_WATER, WS_STEAM, WS_PULSE_MASK, crc16_modbus, build_frame,
                   build_version_request, hexs, s16, reg_name, config_to_words,
                   load_baseline_config, load_spec, word_bounds, pack112, pack113,
                   unpack112, unpack113, MCU_DEFAULTS, SENSOR_BASELINE,
                   serial_words, ACK_PROFILES, FrameParser)

TICK = 0.010            # 10 ms status task (PROTOCOL §6)
VER_INTERVAL = 0.020    # 0x89 every 20 ms until a cmd9 reply
POLL_LATENCY = 0.005    # ~5 ms reply time on polls (observed ~10 ms incl. UART)
KEEPALIVE_MAX = 30.0    # word1=0 expected every ≤30 s (merakiplus keep-awake)
BOOT_PUSH_WINDOW = 6.0  # boot push expected within 6 s of the first RX
DVALE_DELAY = 0.5       # v6.14: the status task checks word0 every 500 ms (0x29D4)
POWER_KEY_HOLD_MIN = 0.8  # v6.14: PC.8 low for 8×100 ms followed by a release → wake/reset

# Limits for boolean/trigger words (not in parity_spec's parameter table)
BOOL_WORDS = {0: (0, 1), 1: (0, 1), 3: (0, 1), 6: (0, 1), 7: (0, 1),
              8: (0, 1), 9: (0, 1), 16: (0, 1), 19: (0, 1), 20: (0, 1),
              22: (0, 1), 24: (0, 1), 28: (0, 2), 29: (0, 2)}
TRIGGER_WORDS = (26, 27, 30)


class AssertLog:
    """JSONL assertion log read by the e2e runner."""
    def __init__(self, path=None):
        self.f = open(path, "a", buffering=1) if path else None
        self.events = []

    def log(self, t, _kind, **kw):
        e = {"t": round(t, 4), "kind": _kind, **kw}
        self.events.append(e)
        if self.f:
            self.f.write(json.dumps(e, ensure_ascii=False) + "\n")

    def close(self):
        if self.f:
            self.f.close()


class McuSim:
    """MCU model + pty link + ctrl socket + replay engine."""

    def __init__(self, config=None, spec=None, seed=1, profile="normal",
                 require_wand=True, range_policy="log", trigger_policy="reject",
                 assert_path=None, pty=True, ctrl_path=None, pty_file=None,
                 quiet=False):
        # Since v6.6 require_wand is a LEGACY no-op (kept for ctrl/API compat):
        # STEAMLIVE §5-6 proved that display steam (word24=1) runs WITHOUT the wand
        # switch (the latch path), and wand flush is driven by the switch edge. The flag
        # no longer gates any path; ping/ctrl still report it.
        self.quiet = quiet
        self.rng = random.Random(seed)
        self.spec = spec or load_spec()
        self.bounds = word_bounds(self.spec)
        self.cfg = config if config is not None else load_baseline_config()
        self.require_wand = require_wand
        self.range_policy = range_policy
        self.trigger_policy = trigger_policy
        self.profile = dict(ACK_PROFILES[profile]) if isinstance(profile, str) else dict(profile)
        self.alog = AssertLog(assert_path)
        self.stats = {"writes_ok": 0, "writes_rejected": 0, "acks_sent": 0,
                      "acks_dropped": 0, "queries": 0, "crc_bad_rx": 0,
                      "garbage_tx_bytes": 0, "version_reqs": 0, "boot_pushes": 0,
                      "reboots": 0, "dvale_entries": 0, "power_wakes": 0,
                      "dvale_rx_dropped": 0}
        # ---- v6.14: deep sleep/wake (UI_v6 §19, SIM_HANDOFF) ----
        self.power_state = "on"       # on|off (the MCU is binary; the UI has turning_*)
        self.dvale_at = None          # scheduled deep-sleep entry (word0=0 written)
        self.power_key_down = False   # simulated PC.8 button (ctrl power_key)
        self.power_key_t0 = 0.0
        self._key_up_at = None        # "hold" convenience: auto-release
        self.rtc = None               # {y,mo,d,h,mi,s} from cmd8 (the MCU's RTC)
        self.rtc_frac = 0.0
        self.alarm = None             # (day,hour,minute) from cmd8; None = 0/0/0
        self.alarm_fired = False
        # ---- register model ----
        self.regs = [0] * REGN
        for w, v in MCU_DEFAULTS.items():
            self.regs[w] = v
        for w, v in SENSOR_BASELINE.items():
            self.regs[w] = v
        for w, v in serial_words().items():
            self.regs[w] = v
        for w, v in config_to_words(self.cfg).items():
            self.regs[w] = v
        # ---- phase machine ----
        self.t = 0.0
        self.mode = "idle"            # idle|grind|brew|water|steam|sleep|off
        self.act = None               # active phase data (primary: grind/brew/water)
        self.pending = None           # delayed start (trigger → actuator)
        self.post = None              # after stop (drip/decay/pulse)
        # v6.6 (STEAMLIVE §5-7): INDEPENDENT steam channel w/ latch gating.
        #   latch=1 (display): word24=1 → auto-stop at word48≥word25, the MCU itself
        #     clears bits 5+14 and resets word24 (~0.18 s after the bits, measured live).
        #   latch=0 (wand/flush): PE.2 switch → bits 5+14 WITHOUT word24, NO
        #     milk auto-stop (even >650); stops on wand release. Can run
        #     CONCURRENTLY with a primary activity (work_sta = union, e.g. 0x5327).
        self.steam_ch = None          # {"latch","t0","milk0","target","src"}
        self.pending_steam = None     # delayed display start (write → bits)
        self.steam_clear_at = None    # MCU's delayed word24 reset
        # v6.6: the steam after-phase (milk overshoot/cooling, boiler recovery,
        # cur decay) is INDEPENDENT of primary activities — live, the milk keeps
        # cooling through grind/brew. {"t0","auto"|"flush","milk_stop","milk_peak"}
        self.steam_post = None
        # v6.18 (additive): the boiler heat model for words 62/63. None = the pre-v6.18 behaviour
        # (62 = 63 = 100 whenever awake). Switched on by the "boilers" event:
        # {"e_rate": units/s, "s_rate": units/s} (0.1 °C per second while heating toward 997 / 1300).
        self.heat = None
        self.wand_state = bool(self.regs[44] & 1)   # PE.2 switch (debounced)
        self.steam_consumed = False   # steam has lowered the s water level → refill
        # float accumulators for slow temperatures/levels (int regs
        # would truncate otherwise); 68/70 accumulate since v6.6 (live refill)
        self.ft = {w: float(self.regs[w]) for w in (40, 42, 47, 48, 68, 70)}
        self.ft_last = {w: self.regs[w] for w in self.ft}
        # ADC baselines from the seed (replay) or SENSOR_BASELINE (live)
        self.adc_base = {w: self.regs[w] for w in (49, 69, 71, 72)}
        self.wraw_base = {53: self.regs[53], 55: self.regs[55]}
        self.next_pulse = self.rng.uniform(2.0, 6.0)
        self.pulse = None
        # v6.6: small maintenance heating cycles (cur1 ~400-1000, word51 ~400-900)
        # — observed in ALL captures (v3/v2/session/steam-live: 28-47 % of
        # idle frames have cur1 200-1500). Without ws bits (live: 0x0101 during them).
        self.next_small = self.rng.uniform(1.0, 3.0)
        self.small_pulse = None
        self.faults = {}              # word -> (value, until|None)
        self.version_received = False
        self.next_ver = 0.0
        self.first_rx_t = None
        self.boot_push_setup = False
        self.boot_push_preinf = False
        self.last_awake_write = 0.0
        self.expect_keepawake = True
        self.keepalive_logged_at = -1e9
        self.boot_checked = False
        self.last_write_t = -1e9
        self.link = "up"              # up|silent|closed
        self.garbage_until = -1.0
        self.garbage_crc = False
        # ---- replay ----
        self.replay = None            # dict w/ frames/timelines (see load_replay)
        self.rp_mode = None           # serve|verify
        self.rp_speed = 1.0
        self.rp_t = 0.0
        self.rp_idx = 0
        self.rp_verify = None
        self.rp_hold = False          # serve: replay clock frozen until 'replay_anchor'
        # ---- link/io ----
        self.pty = pty
        self.master = self.slave = None
        self.slave_path = None
        self.pty_file = pty_file
        if pty:
            self._open_pty()
        self.parser = FrameParser()
        self.txq = []                 # heap (due, seq, bytes)
        self.txseq = 0
        self.ctrl_path = ctrl_path
        self.ctrl_listener = None
        self.ctrl_clients = {}        # fd -> buf
        if ctrl_path:
            self._open_ctrl()
        self.running = True
        self.t0_wall = time.monotonic()
        # play mode: live speed/pause (1.0 = unchanged wall-clock real time)
        self.speed = 1.0
        self.paused = False
        self.calm = 1.0          # 1.0 = raw (faithful to captures); <1 = median-3+EMA smoothing (demo/play)
        self.calm_prev = {}
        self.calm_hist = {}
        self.maint = True        # idle maintenance pulses (deadhead); False = demo-only without pulses
        self._last_wall = 0.0

    # ------------------------------------------------------------ link setup
    def _open_pty(self):
        self.master, self.slave = os.openpty()
        os.set_blocking(self.master, False)
        # raw 8N1 on the slave side (like the display's set_opt @0x144950):
        # no echo/ONLCR/ICRNL/IXON/IUTF8 — binary frames must not be mangled
        try:
            tty.setraw(self.slave)
            attrs = termios.tcgetattr(self.slave)
            attrs[0] &= ~(termios.IXON | termios.IXOFF | termios.IXANY)
            attrs[1] &= ~termios.OPOST
            attrs[2] = (attrs[2] & ~termios.CSIZE) | termios.CS8
            attrs[2] &= ~(termios.PARENB | termios.CSTOPB)
            attrs[4] = termios.B57600
            attrs[5] = termios.B57600
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.slave, termios.TCSANOW, attrs)
        except (OSError, termios.error):
            pass
        self.slave_path = os.ttyname(self.slave)
        if self.pty_file:
            Path(self.pty_file).write_text(self.slave_path + "\n")
        if not self.quiet:
            print(f"[mcu_sim] pty: {self.slave_path}", flush=True)

    def _close_pty(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = None

    def _open_ctrl(self):
        p = Path(self.ctrl_path)
        p.unlink(missing_ok=True)
        self.ctrl_listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.ctrl_listener.bind(str(p))
        self.ctrl_listener.listen(4)
        self.ctrl_listener.setblocking(False)

    def log(self, msg):
        if not self.quiet:
            print(f"[mcu_sim t={self.t:8.3f}] {msg}", flush=True)

    # ------------------------------------------------------------ tx scheduling
    def send_tx(self, data: bytes, delay: float = 0.0):
        if self.link != "up" or not self.pty or self.power_state == "off":
            return
        self.txseq += 1
        heapq.heappush(self.txq, (self.t + delay, self.txseq, data))

    def _flush_tx(self):
        while self.txq and self.txq[0][0] <= self.t:
            _, _, data = heapq.heappop(self.txq)
            if self.link != "up" or self.power_state == "off":
                continue
            try:
                os.write(self.master, data)
            except BlockingIOError:
                self.txseq += 1
                heapq.heappush(self.txq, (self.t + 0.005, self.txseq, data))
            except OSError as e:
                self.alog.log(self.t, "link_tx_error", err=str(e))
                self.link = "silent"

    # ------------------------------------------------------------ writes
    def _check_write(self, sub, words):
        """Return (ok, reasons[]) — band + trigger + range per the task/PARITY."""
        reasons = []
        n = len(words)
        end = sub + n
        band_ok = end < 33 or end in (113, 114, 115)
        if not band_ok or end > REGN:
            reasons.append(f"band:sub{sub}+len{n}={end}")
        for i, w in enumerate(words):
            word = sub + i
            if word in TRIGGER_WORDS and w != 0:
                reasons.append(f"trigger:word{word}={w}")
        if self.range_policy != "off":
            for i, w in enumerate(words):
                word = sub + i
                if word in self.bounds:
                    lo, hi, _ = self.bounds[word]
                    if not (lo <= w <= hi):
                        reasons.append(f"range:word{word}={w}∉[{lo},{hi}]")
                elif word in BOOL_WORDS:
                    lo, hi = BOOL_WORDS[word]
                    if not (lo <= w <= hi):
                        reasons.append(f"range:word{word}={w}∉[{lo},{hi}]")
                elif word == 112:
                    sw, pr = unpack112(w)
                    if pr not in (3, 4):
                        reasons.append(f"range:word112.tryk={pr}∉[3,4]")
                    if sw not in (0, 1):
                        reasons.append(f"range:word112.switch={sw}")
                elif word == 113:
                    ti, br = unpack113(w)
                    if ti < 5:
                        reasons.append(f"range:word113.tid={ti}<5")
        return (not reasons), reasons

    def handle_write(self, sub, words, source="live"):
        n = len(words)
        ok, reasons = self._check_write(sub, words)
        band_ok = not any(r.startswith("band:") for r in reasons)
        trig_ok = not any(r.startswith("trigger:") for r in reasons)
        range_ok = not any(r.startswith("range:") for r in reasons)
        entry = {"sub": sub, "len": n, "words": list(words),
                 "named": {reg_name(sub + i): w for i, w in enumerate(words)},
                 "source": source}
        if not band_ok:
            self.stats["writes_rejected"] += 1
            self.alog.log(self.t, "write_rejected", verdict="band", reasons=reasons, **entry)
            self.log(f"REJECTED (band) cmd6 sub{sub} len{n}: {reasons}")
            return False
        if not trig_ok and self.trigger_policy == "reject":
            self.stats["writes_rejected"] += 1
            self.alog.log(self.t, "write_rejected", verdict="trigger", reasons=reasons, **entry)
            self.log(f"REJECTED (trigger) cmd6 sub{sub} len{n}: {reasons}")
            return False
        if not range_ok:
            for r in reasons:
                if r.startswith("range:"):
                    self.alog.log(self.t, "range_violation", reason=r, **entry)
            if self.range_policy == "reject":
                self.stats["writes_rejected"] += 1
                self.alog.log(self.t, "write_rejected", verdict="range", reasons=reasons, **entry)
                return False
        # memcpy (REGISTERMAP §3: no MCU range checks; the sim logs them)
        self.regs[sub:sub + n] = [w & 0xFFFF for w in words]
        self.stats["writes_ok"] += 1
        prev_write_t = self.last_write_t
        self.last_write_t = self.t
        self.alog.log(self.t, "write", verdict="ok",
                      range_violations=[r for r in reasons if r.startswith("range:")],
                      **entry)
        # actual state writes (for the assertion layer)
        for i, w in enumerate(words):
            word = sub + i
            if word == 1:
                if w == 0:
                    self.last_awake_write = self.t
                    self.keepalive_logged_at = -1e9
                else:
                    self.alog.log(self.t, "sleep_write", word=1, value=w)
            if word == 0:
                self.alog.log(self.t, "power_write", word=0, value=w)
                # v6.14: word0=0 → the status task starts sleep within ≤0.5 s
                # (0x29D4 checks every 500 ms). It is ACKed normally first.
                if w == 0 and self.power_state == "on" and self.dvale_at is None:
                    self.dvale_at = self.t + DVALE_DELAY
            if word == 30 and w != 0:
                self.alog.log(self.t, "reboot_trigger", word=30, value=w)
                if self.trigger_policy == "pass":
                    self.do_reboot(cause="word30")
            if word in (26, 27) and w != 0:
                self.alog.log(self.t, "calib_trigger", word=word, value=w)
        # boot push detection (PARITY §1.4)
        if sub == 1 and n == 30:
            self.boot_push_setup = True
            self.stats["boot_pushes"] += 1
            self.alog.log(self.t, "boot_push", block="sub1_len30")
        if sub == 112 and n == 3:
            self.boot_push_preinf = True
            self.stats["boot_pushes"] += 1
            self.alog.log(self.t, "boot_push", block="sub112_len3")
        # ACK 0x86 = exact echo, w/ latency/loss profile
        if source == "live":
            self._schedule_ack(sub, words, prev_write_t)
        return True

    def _schedule_ack(self, sub, words, prev_write_t=-1e9):
        p = self.profile
        if self.rng.random() < p.get("loss", 0.0):
            self.stats["acks_dropped"] += 1
            self.alog.log(self.t, "ack_dropped", sub=sub, len=len(words))
            return
        heavy = False
        for i, w in enumerate(words):
            word = sub + i
            if word == 3 and w == 1:
                heavy = True
            if word == 9 and w == 1 and sub <= 22 < sub + len(words) and words[22 - sub] == 1:
                heavy = True
        delay_ms = p["latency_ms"]
        if heavy and p.get("first_delay_ms") and self.t - prev_write_t > 2.0:
            delay_ms = p["first_delay_ms"]   # v3: first ACK ~250-280 ms at grind/brew start
        delay_ms += self.rng.uniform(-p.get("jitter_ms", 0), p.get("jitter_ms", 0))
        ack = build_frame(0x86, sub, words)
        self.send_tx(ack, max(0.001, delay_ms / 1000.0))
        self.stats["acks_sent"] += 1
        # v6.13: the ACK echo is logged so scenarios can assert the stop write's ACK
        # byte for byte (sub+words = exact echo, cf. 0x99C6).
        self.alog.log(self.t, "ack", sub=sub, len=len(words), words=list(words))

    # ------------------------------------------------------------ other cmds
    def handle_query(self, sub, n):
        if sub + n > REGN:
            self.alog.log(self.t, "read_rejected", sub=sub, len=n)
            self.log(f"REJECTED (03 error) sub{sub} len{n}")
            return
        self.stats["queries"] += 1
        self.send_tx(build_frame(0x83, sub, self.regs[sub:sub + n]), POLL_LATENCY)

    def handle_frame(self, cmd, sub, payload):
        if self.power_state == "off":
            # v6.14: deep sleep — the status task (RX parser + replies) is deleted:
            # everything on the UART is ignored (polls, writes, cmd8). Silent MCU.
            self.stats["dvale_rx_dropped"] += 1
            return
        if self.link == "silent":
            return
        if self.first_rx_t is None:
            self.first_rx_t = self.t
            self.alog.log(self.t, "link_connect")
        if cmd == 0x03:
            self.handle_query(sub, payload if isinstance(payload, int) else 0)
        elif cmd == 0x06:
            self.handle_write(sub, payload)
        elif cmd == 0x08:
            # v6.14: cmd8 = time/alarm (10 bytes: year u16, month 1-12, day,
            # hour, minute, second, alarm day/hour/minute). The MCU (0x9C88)
            # stores them and replies with a 0x88 echo. The sleep task wakes on the alarm
            # (day+hour+minute ≠ 0 and RTC = the alarm) — model: the RTC runs with
            # sim time, and an alarm match in deep sleep → CPU reset (_power_wake).
            p = payload
            self.rtc = {"y": p[0] | (p[1] << 8), "mo": p[2], "d": p[3],
                        "h": p[4], "mi": p[5], "s": p[6]}
            self.rtc_frac = 0.0
            ad, ah, am = p[7], p[8], p[9]
            self.alarm = (ad, ah, am) if (ad or ah or am) else None
            self.alarm_fired = False
            self.alog.log(self.t, "time_sync", payload=hexs(payload),
                          alarm=[ad, ah, am])
            body = bytes([0xAA, 0x55, 0x88]) + payload
            self.send_tx(body + struct.pack("<H", crc16_modbus(body)), 0.005)
        elif cmd == 0x09:
            self.version_received = True
            ver = payload.decode("ascii", "replace").rstrip("\x00")
            self.alog.log(self.t, "version_handshake", version=ver)
            self.log(f"version handshake OK: {ver}")
        else:
            self.alog.log(self.t, "unknown_cmd", cmd=cmd, sub=sub)

    # ------------------------------------------------------------ phase machine
    def _start_activity(self, kind):
        r = self.regs
        self.post = None              # a new activity takes over the after-phase
        if kind == "grind":
            target = r[4]
            # v6.6 (STEAMLIVE §8): live stop ~187 at target 185 (target+2);
            # cur2 peak 1500 / mean 868 (live; v3: 1355/656 — within fast-tol)
            self.act = {"kind": kind, "t0": self.t, "target": target,
                        "stop_w": target + 2, "rate": 19.4}
            r[3] = 0                      # trigger consumed (BREW §5.1)
            r[38] = 0                     # tare
            r[33] = WS_GRIND
            self.mode = "grind"
        elif kind == "brew":
            target = r[23]
            pre = unpack113(r[113])[0] if unpack112(r[112])[0] else 0
            # v6.6 (STEAMLIVE §3/§7): the weight ramp starts ~pre+2.75 s after start,
            # rate ~18.6 units/s, predictive stop ~target-8 (live 352/360, after-drip
            # +5..15). The bit9 pulse placement is stochastic (v3: el 1.14 s;
            # steam-live: el 4.97 s) — the sim uses the v3 placement; verify filters
            # short pre-pulses out of the ws sequence (like idle pulses, see _verify_report).
            self.act = {"kind": kind, "t0": self.t, "target": target,
                        "pre": float(pre), "stop_w": max(1, target - 8),
                        "rate": 18.6, "drip_t": pre + 2.75,
                        "bit9_pulse_done": pre < 2, "flush_overlap": False}
            r[9] = 0
            r[39] = 0xFFFd                # tare ~ -3 (the puck absorbs)
            r[33] = WS_BREW_PRE | (0x4020 if self.steam_ch else 0)   # v6.20: steam keeps its bits
            self.mode = "brew"
        elif kind == "water":
            target = r[17]
            manual = r[16] == 0 and r[19] == 1
            self.act = {"kind": kind, "t0": self.t, "target": target,
                        "manual": manual, "rate": 63.8,
                        "overshoot": self.rng.randint(0, 10)}
            r[9] = 0
            r[39] = 0
            r[33] = WS_WATER
            self.mode = "water"
        self.alog.log(self.t, "activity_start", activity=kind,
                      target=self.act.get("target"), work_sta=hex(r[33]))
        self.log(f"START {kind} (work_sta={r[33]:#06x})")

    def _start_steam(self, latch, src="display"):
        """v6.6: the steam channel (latch=1 display / latch=0 wand flush)."""
        r = self.regs
        if self.steam_ch:
            # display start on top of a running flush: the latch takes over (milk continues)
            self.steam_ch.update(latch=1, t0=self.t, milk0=self.ft[48],
                                 target=r[25], src="display")
        else:
            self.steam_ch = {"latch": latch, "t0": self.t, "milk0": self.ft[48],
                             "target": r[25] if latch else None, "src": src}
        self.steam_post = None        # new steam takes over the milk after-phase
        if self.act is None:
            self.mode = "steam"
        r[33] |= 0x4020
        self.alog.log(self.t, "activity_start", activity="steam",
                      target=r[25] if latch else None, work_sta=hex(r[33]),
                      latch=latch, src=src)
        self.log(f"START steam ({src}, latch={latch}, work_sta={r[33]:#06x})")

    def _stop_steam(self, reason):
        """v6.6: stop the steam channel. Auto-stop (latch=1, milk≥target) → the MCU
        itself clears word24 ~0.18 s after the bits (live t+96.676→96.838) + thermal
        overshoot +39 units (~11 s to peak, STEAMLIVE §5.2). Manual/error stop
        keeps v6.5's small deterministic peak. Flush (latch=0): short
        boiler lag, the milk is left to idle/verify sync."""
        r = self.regs
        sc = self.steam_ch or {}
        auto = bool(sc.get("latch")) and "milk" in reason
        r[33] &= ~0x4020              # the primary activity's bits are kept (union)
        milk_stop = r[48]
        if sc.get("latch"):
            if auto:
                self.steam_clear_at = self.t + 0.18
                self.steam_post = {"t0": self.t, "auto": True,
                                   "milk_stop": milk_stop,
                                   "milk_peak": milk_stop + 39}
            else:
                r[24] = 0             # manual/error: clear immediately (the write/the error)
                self.steam_clear_at = None
                self.steam_post = {"t0": self.t, "auto": False,
                                   "milk_stop": milk_stop,
                                   "milk_peak": milk_stop + self.rng.randint(1, 8)}
        else:
            self.steam_post = {"t0": self.t, "flush": True}
        self.steam_ch = None
        self.mode = ("off" if r[0] == 0 else "sleep" if r[1] == 1 else
                     (self.act["kind"] if self.act else "idle"))
        self.alog.log(self.t, "activity_stop", activity="steam", reason=reason,
                      work_sta=hex(r[33]), weight=r[38],
                      latch=sc.get("latch"), milk=milk_stop)
        self.log(f"STOP steam ({reason}, work_sta={r[33]:#06x})")

    def _stop_activity(self, reason, manual=False):
        r = self.regs
        act = self.act or {}
        kind = act.get("kind", self.mode)
        if kind == "grind":
            r[33] = WS_IDLE
            # v6.13: a MANUAL stop (word6=0) halts the grinder immediately —
            # the weight stops rising (w_end=w0). Auto-stop already reaches
            # stop_w=target+2, so the after-phase there is flat; a manual stop must NOT
            # keep ramping toward the target. Cur2 decay + word6 clear are shared.
            self.post = {"kind": "grind", "t0": self.t, "w0": r[38],
                         "w_end": r[38] if manual else
                         min(65535, act.get("target", r[38]) + 2),
                         "tare_clear_at": self.t + 0.4}
        elif kind == "brew":
            r[33] = WS_IDLE
            # v6.6: word22 is cleared ~0.16 s after stop (live 24.577→24.739 /
            # 245.851→246.007); after-drip +12 (live +5..+15). The post-stop
            # bit2 pulse is suppressed if a flush overlapped the brew (live
            # brew#2: the pulse (+179 ms, 90 ms) was not sampled by sub33 —
            # the ws sequence in verify must match the CAPTURED sequence).
            self.post = {"kind": "brew", "t0": self.t, "w0": r[39],
                         "w_end": r[39] + 12, "pulse_until": self.t + 0.21,
                         "pulse": not act.get("flush_overlap"),
                         "flag_clear_at": self.t + 0.16}
        elif kind == "water":
            r[33] = WS_IDLE
            r[16] = 0                     # the MCU clears hotwater_auto itself
            r[19] = 0
            self.post = {"kind": "water", "t0": self.t, "w0": r[39],
                         "w_end": max(0, r[39] - 11)}
        if self.steam_ch:
            r[33] |= 0x4020           # steam keeps running on top of the stop
        self.alog.log(self.t, "activity_stop", activity=kind, reason=reason,
                      work_sta=hex(r[33]), manual=manual,
                      weight=r[39] if kind in ("brew", "water") else r[38])
        self.log(f"STOP {kind} ({reason}, work_sta={r[33]:#06x})")
        self.act = None
        self.mode = ("off" if r[0] == 0 else "sleep" if r[1] == 1 else
                     ("steam" if self.steam_ch else "idle"))

    # ------------------------------------------------------------ v6.14: deep sleep/wake
    def _enter_dvale(self):
        """v6.14 (MEM03 sleep task 0x2F52, UI_v6 §19): word0=0 → work_sta=0,
        outputs 0, boiler_heat/head_heat/status/run are deleted. Activities
        are stopped SAFELY (no after-phase — the tasks are gone), the register
        file is kept, and the UART is silent (no replies, no 0x89)."""
        act_kind = self.act["kind"] if self.act else None
        had_steam = self.steam_ch is not None
        if act_kind:
            self.alog.log(self.t, "activity_stop", activity=act_kind,
                          reason="deep sleep (word0=0)", work_sta=hex(0))
        if had_steam:
            self.alog.log(self.t, "activity_stop", activity="steam",
                          reason="deep sleep (word0=0)", work_sta=hex(0))
        self.act = self.pending = self.post = None
        self.steam_ch = self.pending_steam = self.steam_clear_at = None
        self.steam_post = None
        self.pulse = self.small_pulse = None
        self.regs[33] = 0x0000
        self.regs[62] = 0                 # outputs 0 (0x2F0C): heating power
        self.regs[63] = 0
        self.txq = []                     # nothing more on its way out
        self.power_state = "off"
        self.dvale_at = None
        self.mode = "off"
        self.stats["dvale_entries"] += 1
        self.alog.log(self.t, "dvale_enter", word0=0, work_sta=0,
                      activity=act_kind, steam=had_steam)
        self.log("DEEP SLEEP (word0=0): silent UART, work_sta=0, regs kept")

    def _power_wake(self, cause):
        """v6.14: wake from deep sleep (button hold ≥0.8 s or alarm) → CPU reset
        (0x326) to the FACTORY start values — not the config. The host's
        boot push/re-push (the 0x89 round) restores the config."""
        if self.power_state != "off":
            return
        self.stats["power_wakes"] += 1
        self.alog.log(self.t, "power_wake", cause=cause)
        self.do_reboot(factory=True, cause=cause)

    def _power_key_release(self):
        """PC.8 released: held ≥0.8 s (8×100 ms) in deep sleep → wake/reset.
        While the MCU is on, the button does nothing in the MCU (the display owns
        the power-off semantics — UI_v6 §19.1)."""
        if not self.power_key_down:
            return
        held = self.t - self.power_key_t0
        self.power_key_down = False
        self.alog.log(self.t, "power_key", value=0, held_s=round(held, 3))
        if self.power_state == "off" and held >= POWER_KEY_HOLD_MIN:
            self._power_wake("power_key")

    def _tick_rtc(self, dt):
        """The MCU's RTC (set by cmd8) runs with sim time. An alarm match in deep sleep
        (day+hour+minute ≠ 0) → wake/reset (0x2FE2–0x3036 → 0x326)."""
        if self.rtc is None:
            return
        self.rtc_frac += dt
        r = self.rtc
        while self.rtc_frac >= 1.0:
            self.rtc_frac -= 1.0
            r["s"] += 1
            if r["s"] >= 60:
                r["s"] = 0
                r["mi"] += 1
            if r["mi"] >= 60:
                r["mi"] = 0
                r["h"] += 1
            if r["h"] >= 24:
                r["h"] = 0
                r["d"] += 1
                if r["d"] > calendar.monthrange(r["y"], r["mo"])[1]:
                    r["d"] = 1
                    r["mo"] += 1
                    if r["mo"] > 12:
                        r["mo"] = 1
                        r["y"] += 1
        if (self.alarm and not self.alarm_fired and self.power_state == "off"
                and (r["d"], r["h"], r["mi"]) == self.alarm):
            self.alarm_fired = True
            self._power_wake("alarm")

    def do_reboot(self, factory=False, cause="ctrl"):
        self.stats["reboots"] += 1
        self.regs = [0] * REGN
        for w, v in MCU_DEFAULTS.items():
            self.regs[w] = v
        for w, v in SENSOR_BASELINE.items():
            self.regs[w] = v
        for w, v in serial_words().items():
            self.regs[w] = v
        defaults = None
        if factory:
            # v6.14 power wake: CPU reset to the start values (REGISTERMAP §2,
            # init 0x9966) — the config is NOT preloaded; the host's
            # boot push/re-push is expected (boot_push_missing otherwise).
            self.regs[33] = WS_IDLE       # word0=1, word1=0 → awake idle
            defaults = {str(w): self.regs[w] for w in (0, 4, 11, 14, 17, 23, 25, 114)}
        else:
            # compat: ctrl reboot / word30 preloads the config (as before)
            for w, v in config_to_words(self.cfg).items():
                self.regs[w] = v
            self.regs[33] = WS_SLEEP
        self.alog.log(self.t, "reboot", factory=factory, cause=cause,
                      defaults=defaults)
        # v6.16: the cmd8 alarm lives in MCU RAM (0x20000E18) — a CPU reset clears it; the RTC
        # peripheral keeps counting. The host re-arms it (merakiplus: pre-arm after the wake).
        if self.alarm is not None:
            self.alog.log(self.t, "alarm_cleared", alarm=list(self.alarm), cause="reset")
        self.alarm = None
        self.alarm_fired = False
        self.ft = {w: float(self.regs[w]) for w in (40, 42, 47, 48, 68, 70)}
        self.ft_last = {w: self.regs[w] for w in self.ft}
        self.adc_base = {w: self.regs[w] for w in (49, 69, 71, 72)}
        self.wraw_base = {53: self.regs[53], 55: self.regs[55]}
        self.mode = "idle"
        self.act = self.pending = self.post = None
        self.steam_ch = self.pending_steam = self.steam_clear_at = None
        self.steam_post = None
        self.wand_state = bool(self.regs[44] & 1)
        self.steam_consumed = False
        self.version_received = False
        self.next_ver = self.t + 0.05
        self.boot_push_setup = self.boot_push_preinf = False
        # new connect round: boot push/version handshake expected again
        self.boot_checked = False
        self.first_rx_t = None
        self.last_awake_write = self.t
        self.keepalive_logged_at = -1e9
        # v6.14: a reboot takes the MCU out of deep sleep (word0 starts as 1)
        self.power_state = "on"
        self.dvale_at = None
        self.power_key_down = False
        self._key_up_at = None
        if factory:
            self.alarm = None             # the alarm lived in RAM (0x20000E18) — gone on reset
            self.alarm_fired = False
        self.log(f"REBOOT ({cause}" + (", factory start values)" if factory else ")"))

    def _tick_triggers(self):
        r = self.regs
        awake = r[0] == 1 and r[1] == 0
        if self.pending:
            if self.t >= self.pending["at"]:
                kind = self.pending["kind"]
                self.pending = None
                if awake and (self.mode == "idle" or
                              (self.mode == "steam" and kind == "brew")):
                    self._start_activity(kind)
        # v6.6: display steam (word24=1) is an INDEPENDENT latch path — does NOT require
        # the wand switch (STEAMLIVE §5: live wand bit0=0 throughout display steam;
        # the require_wand gating was a verify hole, now removed). Can start on top of
        # a running primary activity (bits 5+14 are disjoint from brew/grind).
        if self.pending_steam:
            if self.t >= self.pending_steam["at"]:
                self.pending_steam = None
                if awake and not (r[34] or r[35] or r[36]):
                    self._start_steam(latch=1, src="display")
        # MCU's delayed word24 reset after auto-stop (live ~188 ms)
        # — BEFORE the trigger check, so it does not restart steam on word24=1.
        if self.steam_clear_at is not None and self.t >= self.steam_clear_at:
            r[24] = 0
            self.steam_clear_at = None
            self.alog.log(self.t, "steam_ord24_clear", word=24)
        if (r[24] != 0 and self.pending_steam is None
                and (self.steam_ch is None or not self.steam_ch["latch"])
                and awake and not (r[34] or r[35] or r[36])
                and self.steam_clear_at is None):
            # word24=1 during a running flush (latch=0) → the display latch takes over
            self.pending_steam = {"at": self.t + 0.30}
        # primary triggers (idle; v6.20: a BREW also during steam — brew-ctl 0x1E9C does not
        # read the steam bits/words, docs/concurrent-brew-steam.md §3.2. Grind/water during
        # steam stay unmodelled: their triggers are not consumed while steam runs.)
        if self.mode not in ("idle", "steam") or self.pending:
            return
        if not awake:
            return
        if self.mode == "steam":
            if r[9] != 0 and r[22] == 1 and r[3] == 0 and r[16] != 1 and r[19] != 1:
                self.pending = {"kind": "brew", "at": self.t + 0.17}
            return
        if r[3] != 0:
            self.pending = {"kind": "grind", "at": self.t + 0.39}
        elif r[9] != 0:
            if r[16] == 1 or r[19] == 1:
                self.pending = {"kind": "water", "at": self.t + 0.18}
            elif r[22] == 1:
                # v6.6: live write→brew bits ~0.17 s (STEAMLIVE §3: 218.956→
                # 219.123); v3's 0.39 s was too late for steam-live's ws timing.
                self.pending = {"kind": "brew", "at": self.t + 0.17}
            else:
                r[9] = 0                  # plain tare without a product flag
                r[39] = 0

    def _sync_ft(self):
        """Sync float accumulators on external changes (inject/replay/
        verify physync) — not on our own noise (random walk otherwise)."""
        for w in self.ft:
            if self.regs[w] != self.ft_last.get(w):
                self.ft[w] = float(self.regs[w])

    @staticmethod
    def _milk_rate(temp):
        """v6.6 milk ramp (the STEAMLIVE §5 curve): mid rate 14.0 units/s
        (1.403 °C/s, kept from v6.5); cold start ~9.8 units/s (<150) and
        end phase ~12.6 units/s (≥600) — linear fit of the live curve
        (116→155: 9.6; 223→500: 14.15; 571→646: 12.5)."""
        if temp < 150:
            return 9.8
        if temp < 250:
            return 9.8 + (temp - 150) * 0.042
        if temp < 600:
            return 14.0
        return 12.6

    def _tick_activity(self, dt):
        r = self.regs
        a = self.act
        if not a:
            return
        self._sync_ft()
        el = self.t - a["t0"]
        kind = a["kind"]
        # error → safety stop (MCU stop path, SESSION §5.2)
        if r[34] or r[35] or r[36]:
            self._stop_activity("fault E1/E2/E3")
            return
        # v6.13 (SIM_HANDOFF/UI_v6 §18): MANUAL stop. The control tasks
        # recompute the run flags every 10 ms round, and the MCU's own
        # auto-stop writes exactly the same words to 0 — a manual write
        # therefore hits the same stop path (post/drip/decay):
        #   grind: runs while word6≠0 (grind-ctl 0x19C6; word3 is tare).
        #   brew:  running=(word22≠0) on change (brew-ctl 0x1E9C/0x1F94).
        #   water: active=(word19|word16)≠0 (hotwater-ctl 0x1B8C) — ALSO in
        #          auto mode; stops at the current weight (no snap to target).
        if kind == "grind" and r[6] == 0:
            self._stop_activity("manual stop word6=0", manual=True)
            return
        if kind == "brew" and r[22] == 0:
            self._stop_activity("manual stop word22=0")
            return
        if kind == "water" and r[16] == 0 and r[19] == 0:
            self._stop_activity("manual stop word16/19=0")
            return
        if kind == "grind":
            w = min(a["stop_w"], int(el * a["rate"]))
            r[38] = w & 0xFFFF
            r[53] = self.wraw_base[53] + w // 40
            # v6.6: cur2 live peak 1500 / mean 868 (STEAMLIVE §8; v3: 1355/656)
            r[58] = 1500 if el < 0.25 else 868 + self.rng.randint(-60, 60)
            r[33] = WS_GRIND
            if w >= a["stop_w"] or el > max(5, r[5]):
                self._stop_activity("weight target" if w >= a["stop_w"] else "timeout word5")
        elif kind == "brew":
            pre = a["pre"]
            if el < pre + 0.55:
                r[33] = WS_BREW_PRE
                if not a["bit9_pulse_done"] and 1.14 <= el <= 1.36:
                    r[33] |= 0x0200       # short bit9 pulse (BREW §5.2; stochastic
                                          # placement — verify filters, see §9)
                if el > 1.36:
                    a["bit9_pulse_done"] = True
                r[51] = 358 + self.rng.randint(-9, 9)
                r[52] = 360 + self.rng.randint(-20, 20)
                r[50] = 1533 + self.rng.randint(-40, 40)
                r[39] = 0xFFFd
            else:
                ee = el - (pre + 0.55)
                r[33] = WS_BREW_EXT
                if ee < 0.4:
                    r[51] = 1481
                    r[52] = 919
                else:
                    decay = min(1.0, (ee - 0.4) / 21.0)
                    r[51] = int(1379 - 54 * decay) + self.rng.randint(-15, 15)
                    r[52] = (637 if ee < 1.0 else
                             int(85 + 143 * min(1.0, ee / 21.0)) + self.rng.randint(-30, 30))
                r[50] = 1046 + self.rng.randint(-40, 40)
                if el >= a["drip_t"]:
                    x = el - a["drip_t"]
                    if a.get("quad"):
                        w0, b, ac = a["quad"]   # seeded pre-capture brew (fit)
                        w = int(w0 + b * x + ac * x * x)
                    else:
                        w = int(x * a["rate"])
                    r[39] = min(a["stop_w"], w) & 0xFFFF
                    r[55] = self.wraw_base[55] + w // 45
                    if r[39] >= a["stop_w"]:
                        self._stop_activity("weight target (predictive)")
                elif el > 180:
                    self._stop_activity("safety")
        elif kind == "water":
            r[33] = WS_WATER
            r[50] = 1556 if el < 0.15 else 1050 + self.rng.randint(-30, 30)
            r[52] = min(610, int(el * 1300)) + self.rng.randint(-15, 15)
            r[51] = 1300 + self.rng.randint(-20, 20)
            w = int(el * a["rate"])
            r[39] = w & 0xFFFF
            r[55] = self.wraw_base[55] + w // 50
            # v6.13: a manual stop (word16/19=0) is handled at the top of the tick —
            # also in auto mode — and stops at the CURRENT weight (r[39]=w),
            # not at the target. The auto-stop snaps to target+overshoot as before.
            if w >= a["target"]:
                r[39] = (a["target"] + a["overshoot"]) & 0xFFFF
                self._stop_activity("weight target word39≥word17")
            elif el > 180:
                self._stop_activity("safety")

    def _tick_steam(self, dt):
        """v6.6: the steam channel — runs independently of (and concurrently with) a primary
        activity. Two paths (STEAMLIVE §5-6):
          latch=1 display: milk ramp + AUTO-stop at word48≥word25; cur1 ~7340,
            word51 ~16650 (deadhead), word52=0, s-boiler drop ~6.5→0.4 units/s.
          latch=0 wand flush: NO milk auto-stop (even >650); stops on
            wand release; s-boiler drop ~11.6 units/s (free venting);
            cur1/word51 ≈ brew level (live flush-during-brew ~1024/~1387).
        word46 = wand switch 0/1 (the STEAMLIVE §6.3 correction of REGISTERMAP)."""
        r = self.regs
        sc = self.steam_ch
        if not sc:
            return
        self._sync_ft()
        el = self.t - sc["t0"]
        if r[0] == 0 or r[1] == 1:
            self._stop_steam("sleep/power-off")
            return
        if r[34] or r[35] or r[36]:
            self._stop_steam("fault E1/E2/E3")
            return
        if self.act and self.act["kind"] == "brew":
            self.act["flush_overlap"] = True
        # milk (shared thermal model; a flush without a milk jug heats the probe
        # faster live — verify physync syncs the few flush frames)
        self.ft[48] += self._milk_rate(self.ft[48]) * dt
        r[48] = int(self.ft[48]) & 0xFFFF
        # s-boiler (word47) + s water level (word68)
        # v6.20: latched steam DURING A BREW follows the MCU heater task 0x3502 (docs/concurrent-
        # brew-steam.md §3.4, hardware §11): the steam heater (work_sta bit 13) gets NO slots while
        # the brew runs (and none during phase-controlled pre-infusion) until the steam boiler has
        # fallen below target - 10.0 °C (target by gear word 14: 1=120, 2=125, 3=130, else 135 °C);
        # from then on (latch r5) it runs 100 % until the target, each slot still stopped above
        # target. The brew heater (bit 10) is off during any brew. Live: 129.8 -> 117.3 °C, bit 13
        # first at 119.7 °C, cur1 ~1.05 k (pump) -> ~7.95 k (pump + steam heater).
        brew = bool(self.act and self.act["kind"] == "brew")
        heat = False
        if sc["latch"] and brew:
            a = self.act
            tgt = {1: 1200, 2: 1250, 3: 1300}.get(r[14], 1350)
            pre_inf = a["pre"] > 0 and (self.t - a["t0"]) < a["pre"]
            if not pre_inf and self.ft[47] < tgt - 100:
                sc["r5"] = True
            heat = bool(sc.get("r5")) and not pre_inf and self.ft[47] < tgt
            if heat:
                self.ft[47] = self._approach(self.ft[47], tgt, 1.0 * dt)
            else:
                self.ft[47] = max(1000.0, self.ft[47] - 6.5 * dt)
            if el < 1.5:
                self.ft[68] = max(800.0, self.ft[68] - 230.0 * dt)
            else:
                self.ft[68] = self._approach(self.ft[68], 858, 2.0 * dt)
            r[68] = max(0, int(self.ft[68]) + self.rng.randint(-12, 12))
        elif sc["latch"]:
            if el < 2.5:
                pass                  # live: the boiler holds ~1300 for the first ~2.5 s
            elif el < 8.5:
                self.ft[47] = max(1243.0, self.ft[47] - 9.5 * dt)
            else:
                self.ft[47] = self._approach(self.ft[47], 1253, 0.4 * dt)
            if el < 1.5:                # flash boiling at start (live -230/s)
                self.ft[68] = max(800.0, self.ft[68] - 230.0 * dt)
            else:
                self.ft[68] = self._approach(self.ft[68], 858, 2.0 * dt)
            r[68] = max(0, int(self.ft[68]) + self.rng.randint(-12, 12))
        else:
            self.ft[47] = max(1240.0, self.ft[47] - 11.6 * dt)
            # live flush draw: ~200 units over ~1.5 s (free venting, STEAMLIVE §7)
            self.ft[68] = max(1050.0, self.ft[68] - 130.0 * dt)
            r[68] = max(0, int(self.ft[68]) + self.rng.randint(-5, 5))
        r[47] = max(0, int(self.ft[47]) + self.rng.randint(-3, 3))
        self.steam_consumed = True
        # current/pressure/flow
        if sc["latch"] and brew:
            # v6.20: the brew owns pressure/flow (word 51/52); cur1 = pump (+ steam heater)
            if heat:
                r[50] = 7950 + self.rng.randint(-150, 150)
        elif sc["latch"]:
            if el < 5:
                r[50] = 54 + self.rng.randint(-10, 20)
                r[51] = 93 + self.rng.randint(-20, 40)
            else:
                r[50] = 7340 + self.rng.randint(-200, 300)   # live median 7340
                r[51] = 16650 + self.rng.randint(-180, 300)  # deadhead/max-drive
            r[52] = 0                   # steam is generated in the boiler, no water flow
        else:
            if self.act is None:
                r[50] = 1024 + self.rng.randint(-30, 30)
                r[51] = 1387 + self.rng.randint(-20, 20)
                r[52] = 0
            elif self.act["kind"] == "brew":
                r[51] += 85             # live: brew pressure ~1301→1387 during flush
        if not (self.act and self.act["kind"] == "grind"):
            r[58] = 5 + self.rng.randint(0, 1)   # grinder off during steam
        r[46] = 1 if (r[44] & 1) else 0
        # v6.6: update ft_last IMMEDIATELY for words written from ft in this tick —
        # otherwise later _sync_ft calls see our own noise/truncation as an
        # "external change" (random walk in boiler/level + lost ramp rate).
        for w in (47, 48, 68):
            self.ft_last[w] = r[w]
        # work_sta = union (primary bits | steam bits | maintenance pulses)
        r[33] |= 0x4020
        if heat:
            r[33] |= 0x2000           # v6.20: steam-boiler heater slot (bit 13)
        if self.pulse and self.t < self.pulse["until"]:
            r[33] |= self.pulse["bits"]
        # stop conditions per path (latch gating, STEAMLIVE §6.1)
        if sc["latch"]:
            if r[24] == 0:
                self._stop_steam("manual stop word24=0")
            elif r[35] & 0x10:
                self._stop_steam("E2 milk NTC")
            elif r[48] >= sc["target"]:
                self._stop_steam("milk ≥ setpoint word48≥word25")
            elif el > max(5, r[15]):
                self._stop_steam("timeout word15")
        else:
            if not (r[44] & 1):
                self._stop_steam("wand released")
            elif el > max(5, r[15]):
                self._stop_steam("timeout word15")

    def _tick_post(self, dt):
        r = self.regs
        p = self.post
        if not p:
            return
        self._sync_ft()
        el = self.t - p["t0"]
        kind = p["kind"]
        if kind == "grind":
            r[38] = int(p["w0"] + (p["w_end"] - p["w0"]) * min(1.0, el / 1.0)) & 0xFFFF
            r[58] = max(5, int(727 * math.exp(-2.5 * el)))
            if self.t >= p.get("tare_clear_at", 1e9):
                r[6] = 0                  # word6 is consumed late (BREW §5.1)
            if el > 2.5:
                self.post = None
        elif kind == "brew":
            if p.get("pulse", True) and el < 0.21:
                r[33] = 0x0105            # post-stop bit2 pulse (BREW §5.2)
            else:
                r[33] = WS_IDLE
            if self.steam_ch:
                r[33] |= 0x4020           # steam keeps running on top of the brew stop
            if self.t >= p.get("flag_clear_at", 0):
                r[22] = 0                 # the MCU clears the brew flag ~0.16 s after stop
            r[39] = int(p["w0"] + (p["w_end"] - p["w0"]) * min(1.0, el / 1.6)) & 0xFFFF
            if not self.steam_ch:         # the steam channel owns 50/51/52 if active
                r[51] = int(1300 * math.exp(-3.0 * el)) if el < 1.5 else self.rng.randint(0, 12)
                if 0.4 < el < 2.2 and self.rng.random() < 0.06:
                    r[51] = self.rng.choice([4876, 10385, 15428])   # deadhead-spikes
                r[52] = 0
                r[50] = max(5, int(1046 * math.exp(-2.2 * el)))
            if el > 3.0:
                self.post = None
        elif kind == "water":
            r[39] = int(p["w0"] + (p["w_end"] - p["w0"]) * min(1.0, el / 2.0)) & 0xFFFF
            r[50] = max(5, int(1050 * math.exp(-2.2 * el)))
            r[52] = max(0, int(610 * max(0.0, 1.0 - el / 1.7)))
            r[51] = self.rng.randint(0, 8) if el > 0.5 else int(1300 * math.exp(-3.0 * el))
            if el > 2.5:
                self.post = None

    def _tick_steam_post(self, dt):
        """v6.6: the steam after-phase — INDEPENDENT of primary activities (live:
        the milk keeps cooling through grind/brew, STEAMLIVE §5.2/§3). Owns word48
        (milk) and word47 (boiler recovery); current/pressure/flow only when no primary
        activity/after-phase is running (otherwise they own them)."""
        sp = self.steam_post
        if not sp:
            return
        self._sync_ft()
        r = self.regs
        el = self.t - sp["t0"]
        if sp.get("flush"):
            # wand flush release: the s-boiler drop of ~11.6 units/s continues for ~0.9 s
            # (thermal lag, live 1259→1249), then recovery ~3 units/s.
            if el < 0.9:
                self.ft[47] = max(1240.0, self.ft[47] - 11.6 * dt)
            else:
                self.ft[47] = self._approach(self.ft[47], 1300, 3.0 * dt)
            r[47] = max(0, int(self.ft[47]) + self.rng.randint(-3, 3))
            self.ft_last[47] = r[47]
            if not self.act and not self.post:
                r[50] = 5 + self.rng.randint(0, 1)
                r[51] = self.rng.randint(0, 1)
                r[52] = 0
            if el > 2.5:
                self.steam_post = None
            return
        r[46] = 1 if (r[44] & 1) else 0
        if not (self.act and self.act["kind"] == "grind"):
            r[58] = 5 + self.rng.randint(0, 1)
        if sp.get("auto"):
            # v6.6 AUTO-stop overshoot (STEAMLIVE §5.2, measured live +3.9 °C):
            # the milk keeps rising for ~11 s after stop (thermal inertia in
            # probe/jug): exponential approach toward peak = stop+39
            # (tau 3.2 s → ~97 % at 11 s), holds the peak ~40 s (live
            # 689-690 at t+108..158), then cools ~0.3/0.52 units/s.
            if el < 11.5:
                self.ft[48] += (sp["milk_peak"] - self.ft[48]) * (dt / 3.2)
                r[48] = int(self.ft[48]) & 0xFFFF
            elif el < 50:
                r[48] = int(self.ft[48] + self.rng.randint(-1, 1)) & 0xFFFF
            else:
                cool = 0.3 if el < 130 else 0.52
                self.ft[48] = max(230.0, self.ft[48] - cool * dt)
                r[48] = int(self.ft[48]) & 0xFFFF
            # s-boiler recovers to ~1313 (live 1249→1313 over ~11 s),
            # decay ~1.1 units/s to ~1296 (live 107→125 s), then PID
            if el < 12:
                self.ft[47] = self._approach(self.ft[47], 1313, 5.8 * dt)
            elif el < 30:
                self.ft[47] = self._approach(self.ft[47], 1296, 1.1 * dt)
            else:
                self.ft[47] = self._approach(self.ft[47], 1300, 0.3 * dt)
            r[47] = max(0, int(self.ft[47]) + self.rng.randint(-3, 3))
            # cur1: ~7300 for ~3 s, then oscillating decay (boiler refill).
            # After ~19 s the idle pulses take over (live: maintenance cycles
            # continue for the rest of the window).
            if not self.act and not self.post and el < 19:
                if el < 3:
                    r[50] = 7300 + self.rng.randint(-150, 150)
                    r[51] = 16570 + self.rng.randint(-120, 120)
                else:
                    r[50] = max(5, int(7300 * math.exp(-(el - 3) / 3.5)))
                    if self.rng.random() < 0.5:
                        r[50] = self.rng.randint(800, 5500)
                    r[51] = self.rng.choice([0, 0, self.rng.randint(3000, 16670)])
                r[52] = 0
            if el > 50 and self.ft[48] <= 240:
                self.steam_post = None
        else:
            # v6.5 manual/error stop: DETERMINISTIC small peak (+1..8),
            # held ≥0.15 s (≥ one poll interval) before 0.5 units/s cooling
            # (the steam_auto flake fix, unchanged for backward compatibility).
            r[48] = int(sp["milk_peak"] - max(0.0, el - 0.15) * 0.5) & 0xFFFF
            if not self.act and not self.post:
                r[50] = 5 + self.rng.randint(0, 1)
                r[51] = self.rng.randint(0, 1)
                r[52] = 0
            if el > 4.0:
                self.steam_post = None
        # ft_last immediately (truncation/random-walk guard, like _tick_steam)
        for w in (47, 48):
            self.ft_last[w] = r[w]

    def _tick_idle_sensors(self, dt):
        r = self.regs
        self._sync_ft()
        awake = r[0] == 1 and r[1] == 0
        # v6.6: the steam channel is independent of primary activity
        steam_act = self.steam_ch is not None
        # v6.5 scenarios: during after-steam (post) _tick_post owns word48 —
        # the idle noise (±1) must not overwrite the peak (second part of the v6.4 flake).
        steam_post = self.steam_post is not None
        flush_post = bool(self.steam_post and self.steam_post.get("flush"))
        # boiler maintenance (awake) / cooling (sleep/drain/off)
        if awake:
            er, sr = (self.heat["e_rate"], self.heat["s_rate"]) if self.heat else (5.0, 5.0)
            self.ft[40] = self._approach(self.ft[40], 997, er * dt)
            r[40] = int(self.ft[40]) + self.rng.randint(-3, 3)
            if not steam_act and not steam_post:
                self.ft[47] = self._approach(self.ft[47], 1300, sr * dt)
                r[47] = int(self.ft[47]) + self.rng.randint(-3, 3)
            if self.heat:
                r[62], r[63] = self.heat_stage(r)
            else:
                r[62] = 100
                r[63] = 100
        else:
            # SESSION §5.1: s-boiler 102.5→94.3 °C / 729 s = 0.1125 units/s;
            # e-boiler 85.2→80.1 °C / 729 s = 0.07 units/s (heating off, drain)
            self.ft[47] = max(200.0, self.ft[47] - 0.1125 * dt)
            self.ft[40] = max(200.0, self.ft[40] - 0.07 * dt)
            r[47] = int(self.ft[47])
            r[40] = int(self.ft[40])
            if self.heat:                 # heating off (sleep/drain): boiler_heat writes 0
                r[62] = r[63] = 0
        r[40] = max(0, min(65535, r[40]))
        r[41] = r[40]                     # mirror always identical (proven live)
        r[47] = max(0, min(65535, r[47]))
        # e-head (word42): v6.6 (STEAMLIVE §9): a cold head (<600) heats up toward
        # equilibrium ~585 with tau ~155 s (live 467→583 over 729 s); a warm head
        # is PID-held around the setpoint (v3 behavior, unchanged).
        head_target = r[114] * 10 if awake else 230
        if awake and self.ft[42] < 600:
            self.ft[42] += (580 - self.ft[42]) * (dt / 155.0)
        else:
            self.ft[42] = self._approach(self.ft[42], head_target,
                                         (0.03 if awake else 0.05) * dt)
        r[42] = int(self.ft[42]) + self.rng.randint(-2, 2)
        r[128] = r[42]
        if not steam_act and not (steam_post and not flush_post):
            self.ft[48] = self._approach(self.ft[48], 230, 0.05 * dt)
            r[48] = max(0, int(self.ft[48]) + self.rng.randint(-1, 1))
        # water levels (v6.6, STEAMLIVE §9): word68 (s-boiler) flash-boils during
        # steam (owned by _tick_steam) and is then refilled toward ~1345 (fast to
        # ~1270, slowly after that); a low level (<1200) rises ~0.35 units/s.
        # word70 (e-boiler) is refilled toward ~910 with tau ~280 s from a low level.
        # Sleep: slow rise (session capture: +0.055/+0.06 units/s).
        if awake:
            if not steam_act:         # the steam channel owns word68 while running
                if self.steam_consumed and self.ft[68] < 1340:
                    # live refill: ~20 units/s up to ~1080, then exponential
                    # approach toward ~1290 (k 0.05), finally slowly toward ~1345
                    if self.ft[68] < 1080:
                        self.ft[68] += 20.0 * dt
                    elif self.ft[68] < 1285:
                        self.ft[68] = self._approach(
                            self.ft[68], 1290,
                            max(0.5, (1290 - self.ft[68]) * 0.05) * dt)
                    else:
                        self.ft[68] = self._approach(
                            self.ft[68], 1345,
                            max(0.15, (1345 - self.ft[68]) * 0.004) * dt)
                elif self.ft[68] < 1200:
                    self.ft[68] += 0.35 * dt
                r[68] = int(self.ft[68]) + self.rng.randint(-3, 3)
            if self.ft[70] < 900:
                self.ft[70] += (910 - self.ft[70]) * (dt / 280.0)
            r[70] = int(self.ft[70]) + self.rng.randint(-3, 3)
        else:
            self.ft[68] += 0.055 * dt
            self.ft[70] += 0.06 * dt
            r[68] = int(self.ft[68])
            r[70] = int(self.ft[70])
        for w in self.ft:
            self.ft_last[w] = r[w]
        # current/flow/pressure baseline (the steam after-phase's cur decay owns them for ~19 s)
        sp_cur = bool(self.steam_post and not self.steam_post.get("flush")
                      and (self.t - self.steam_post["t0"]) < 19)
        if not self.act and not self.post and not steam_act and not sp_cur:
            r[50] = 5 + self.rng.randint(0, 1)
            r[58] = 5 + self.rng.randint(0, 1)
            r[52] = 0
            r[51] = self.rng.randint(0, 1)
        # mains/ADC noise around SEED values (non-accumulating)
        r[49] = self.adc_base[49] + self.rng.randint(-3, 1)
        r[61] = 50
        r[69] = self.adc_base[69] + self.rng.randint(-1, 1)
        r[71] = self.adc_base[71] + self.rng.randint(-2, 2)
        r[72] = self.adc_base[72] + self.rng.randint(-2, 2)
        # raw load cells loosely follow the calibrated weights (BREW §8; live /45)
        r[53] = self.wraw_base[53] + max(0, s16(r[38])) // 40 + self.rng.randint(-1, 1)
        r[55] = self.wraw_base[55] + max(0, s16(r[39])) // 45 + self.rng.randint(-1, 1)
        # work_sta in idle/sleep/off + stochastic maintenance pulses.
        # v6.6 (STEAMLIVE §9.3 item 12): the pulses (bits 10/11/13) also run
        # DURING steam (live 0x6121/0x4521) — only the ws bits are overlaid here;
        # the steam channel/after-phase still owns current/pressure.
        if self.act or self.post:
            return
        if r[0] == 0:
            r[33] = 0x0000                # sleep-task (word0=0)
            self.mode = "off"
        elif r[1] == 1:
            r[33] = WS_SLEEP              # drain-task
            self.mode = "sleep"
        else:
            self.mode = "steam" if steam_act else "idle"
            base = WS_IDLE | (0x4020 if steam_act else 0)
            if self.pulse and self.t < self.pulse["until"]:
                r[33] = base | self.pulse["bits"]
                if not steam_act and not sp_cur:
                    r[50] = int(self.pulse["cur"] * self.calm)
                    r[51] = int(self.pulse["p"] * self.calm)
            else:
                self.pulse = None
                r[33] = base
                if self.t >= self.next_pulse and self.maint:
                    bits = self.rng.choice([0x0400, 0x0800, 0x2000, 0x0C00, 0x2800])
                    self.pulse = {"until": self.t + self.rng.uniform(0.10, 0.25),
                                  "bits": bits, "cur": self.rng.randint(800, 4900),
                                  "p": self.rng.randint(3000, 16670)}
                    self.next_pulse = self.t + self.rng.uniform(3.0, 9.0)
            # v6.6: small maintenance heating cycles (cur1 400-1000, word51
            # 400-900 in ~60 % of the cycles) — WITHOUT ws bits (live 0x0101).
            if not steam_act and not sp_cur:
                if self.small_pulse and self.t < self.small_pulse["until"]:
                    if not (self.pulse and self.t < self.pulse["until"]):
                        r[50] = int(self.small_pulse["cur"] * self.calm)
                        if self.small_pulse["p"]:
                            r[51] = int(self.small_pulse["p"] * self.calm)
                else:
                    self.small_pulse = None
                    if self.t >= self.next_small and self.maint:
                        self.small_pulse = {
                            "until": self.t + self.rng.uniform(0.5, 1.5),
                            "cur": self.rng.randint(400, 1000),
                            "p": (self.rng.randint(400, 900)
                                  if self.rng.random() < 0.6 else 0)}
                        self.next_small = self.t + self.rng.uniform(1.5, 4.0)

    @staticmethod
    def heat_stage(r):
        """v6.18: words 62/63 as the MCU's boiler_heat (MEM03 0x3502) writes them — a heat STAGE, not a power:
        extract (62): e boiler (word 40) < 40.0 °C -> 0, < 60.0 -> 30, < brew set temp (word 11) -> 60, else 100;
                      100 while work_sta bit 12.
        steam (63):   s boiler (word 47) < 50.0 °C -> 0, < 80.0 -> 30, < steam target - 10.0 °C -> 60, else 100;
                      target by steam gear (word 14): 1 -> 120.0, 2 -> 125.0, 3 -> 130.0, else 135.0 °C; 100 while bit 5.
        (The MCU's extra internal-state test before 100 is not modelled.) The stock UI plays preheatCompele.mp3 once
        per power-on when BOTH are > 99 (docs/audio.md)."""
        e, s, ws = r[40], r[47], r[33]
        st_e = 0 if e < 400 else 30 if e < 600 else 60 if e < r[11] else 100
        tgt = {1: 1200, 2: 1250, 3: 1300}.get(r[14], 1350)
        st_s = 0 if s < 500 else 30 if s < 800 else 60 if s < tgt - 100 else 100
        if ws & 0x1000:
            st_e = 100
        if ws & 0x0020:
            st_s = 100
        return st_e, st_s

    @staticmethod
    def _approach(cur, target, step):
        if cur < target:
            return min(target, cur + step)
        return max(target, cur - step)

    def _tick_faults(self):
        for w in (34, 35, 36):
            if w in self.faults:
                val, until = self.faults[w]
                if until is not None and self.t >= until:
                    del self.faults[w]
                    self.regs[w] = 0
                    self.alog.log(self.t, "fault_clear", word=w)
                else:
                    self.regs[w] = val
            elif self.regs[w] and w not in self.faults:
                pass                      # natural errors (not injected) are kept

    def _tick_keepalive(self):
        if self.power_state == "off":
            return                        # v6.14: keep-awake pauses in deep sleep
        if not self.expect_keepawake or self.first_rx_t is None:
            return
        gap = self.t - self.last_awake_write
        # the client's keep-awake period is wall clock (30 s); at play speed
        # >1× that corresponds to 30*speed sim seconds (at 1× unchanged behavior)
        kmax = KEEPALIVE_MAX * max(1.0, self.speed)
        if gap > kmax + 0.5 and self.t - self.keepalive_logged_at > kmax:
            self.keepalive_logged_at = self.t
            self.alog.log(self.t, "keepalive_gap", gap_s=round(gap, 2),
                          expected_max_s=KEEPALIVE_MAX)

    def _tick_boot_push_check(self):
        if self.power_state == "off":
            return                        # v6.14: no RX expected in deep sleep
        if self.first_rx_t is None or self.replay or self.boot_checked:
            return
        if self.t - self.first_rx_t > BOOT_PUSH_WINDOW:
            if not self.boot_push_setup:
                self.alog.log(self.t, "boot_push_missing", block="sub1_len30")
            if not self.boot_push_preinf:
                self.alog.log(self.t, "boot_push_missing", block="sub112_len3")
            self.boot_checked = True      # only one check per connect

    # ------------------------------------------------------------ replay engine
    def load_replay(self, csv_path, mode="serve", speed=1.0):
        frames = []
        t0 = None
        opener = gzip.open if str(csv_path).endswith(".gz") else open
        with opener(csv_path, "rt", newline="") as f:
            for row in csv.DictReader(f):
                raw = bytes.fromhex(row["raw_hex"].replace(" ", ""))
                parsed = list(FrameParser().feed(raw))
                if not parsed:
                    continue
                cmd, sub, payload, _ = parsed[0]
                hh, mm, ss = row["ts"].split(":")
                ts = int(hh) * 3600 + int(mm) * 60 + float(ss)
                if t0 is None:
                    t0 = ts
                direction = "tx" if row["direction"].startswith("display") else "rx"
                if cmd in (0x08, 0x88, 0x09, 0x89):
                    continue              # not register data
                words = payload if isinstance(payload, list) else []
                frames.append({"t": ts - t0, "dir": direction, "cmd": cmd,
                               "sub": sub, "words": words})
        self.replay = {"frames": frames, "path": str(csv_path),
                       "duration": frames[-1]["t"] if frames else 0}
        self.rp_mode = mode
        self.rp_speed = speed
        self.rp_t = 0.0
        self.rp_idx = 0
        # seed: first RX reply per band = starting ground truth
        seen = set()
        for fr in frames:
            if fr["dir"] == "rx" and fr["cmd"] == 0x83:
                key = (fr["sub"], len(fr["words"]))
                if key not in seen:
                    seen.add(key)
                    self.regs[fr["sub"]:fr["sub"] + len(fr["words"])] = fr["words"]
        # serve: the version handshake still runs (MCU "restarted"); verify: no pty
        self.version_received = (mode == "verify")
        self.next_ver = 0.0
        self.ft = {w: float(self.regs[w]) for w in (40, 42, 47, 48, 68, 70)}
        self.ft_last = {w: self.regs[w] for w in self.ft}
        self.adc_base = {w: self.regs[w] for w in (49, 69, 71, 72)}
        self.wraw_base = {53: self.regs[53], 55: self.regs[55]}
        self.mode = "sleep" if self.regs[1] == 1 else "idle"
        self.act = self.pending = self.post = None
        self.steam_ch = self.pending_steam = self.steam_clear_at = None
        self.steam_post = None
        self.wand_state = bool(self.regs[44] & 1)
        self.steam_consumed = False
        if mode == "verify":
            self._seed_precapture_brew()
            self._verify_init()
        self.alog.log(0, "replay_load", path=str(csv_path), mode=mode,
                      frames=len(frames), duration=self.replay["duration"])
        self.log(f"REPLAY {mode}: {len(frames)} frames, {self.replay['duration']:.1f} s")

    def _seed_precapture_brew(self):
        """v6.6 (STEAMLIVE §9.3): if the capture starts IN THE MIDDLE of a brew (start write
        before the window — a replay artifact), the brew activity is seeded from the
        observed dynamics: the pre→ext time gives t0; the weight ramp's
        start/end gives rate/drip_t/stop_w. The bit9 pulse lies before the window
        (invisible in the capture) and is suppressed. Verify mode only."""
        r = self.regs
        ws = r[33] & ~WS_PULSE_MASK
        if (ws & 0x0006) != 0x0006 or ws & 0x4060:
            return
        frames = self.replay["frames"]
        pre = unpack113(r[113])[0] if unpack112(r[112])[0] else 0
        t_ext = t_stop = t_ramp = None
        w_stop = w_ramp = None
        for fr in frames:
            if fr["dir"] != "rx" or fr["cmd"] != 0x83:
                continue
            if fr["sub"] <= 33 < fr["sub"] + len(fr["words"]):
                m = fr["words"][33 - fr["sub"]] & ~WS_PULSE_MASK
                if t_ext is None and m == WS_BREW_EXT:
                    t_ext = fr["t"]
                if t_ext is not None and t_stop is None and m in (WS_IDLE, 0x0105):
                    t_stop = fr["t"]
            if fr["sub"] <= 39 < fr["sub"] + len(fr["words"]):
                w39 = s16(fr["words"][39 - fr["sub"]])
                if t_stop is not None and w_stop is None and abs(fr["t"] - t_stop) < 0.5:
                    w_stop = w39
                if t_ext is not None and t_ramp is None and w39 > 10:
                    t_ramp, w_ramp = fr["t"], w39
        if t_ext is None:
            t_ext = frames[0]["t"] if frames else 0.0
        t0 = t_ext - (pre + 0.55)
        target = r[23]
        if (t_stop is not None and t_ramp is not None and w_stop is not None
                and w_stop > 10 and t_stop > t_ramp):
            rate = max(5.0, (w_stop - (w_ramp or 0)) / (t_stop - t_ramp))
            stop_w = w_stop
            # Live weight ramps ACCELERATE (flow rises through the extraction):
            # fit a quadratic w(x) = w0 + b·x + a·x² through ramp start,
            # midpoint and stop (x = time since ramp start). Linear fallback.
            t_mid = (t_ramp + t_stop) / 2
            w_mid, best = None, 1e9
            for fr in frames:
                if (fr["dir"] == "rx" and fr["cmd"] == 0x83
                        and fr["sub"] <= 39 < fr["sub"] + len(fr["words"])):
                    d = abs(fr["t"] - t_mid)
                    if d < best:
                        best, w_mid = d, s16(fr["words"][39 - fr["sub"]])
            quad = None
            x3 = t_stop - t_ramp
            x2 = t_mid - t_ramp
            if w_mid is not None and 0 < x2 < x3:
                d2 = w_mid - (w_ramp or 0)
                d3 = w_stop - (w_ramp or 0)
                ac = (d3 - d2 * x3 / x2) / (x3 * (x3 - x2))
                b = (d2 - ac * x2 * x2) / x2
                if ac >= 0 and b > 0:
                    quad = ((w_ramp or 0), b, ac)
            if quad:
                drip_t = t_ramp - t0      # the quadratic starts at w_ramp
            else:
                # the ramp starts at w=0 in the sim — compensate for the fit intercept
                drip_t = t_ramp - t0 - (w_ramp or 0) / rate
        else:
            rate, stop_w, drip_t = 18.6, max(1, target - 8), pre + 2.75
            quad = None
        self.act = {"kind": "brew", "t0": t0, "target": target, "pre": float(pre),
                    "stop_w": stop_w, "rate": rate, "drip_t": drip_t,
                    "quad": quad,
                    "bit9_pulse_done": True, "flush_overlap": False, "seeded": True}
        self.mode = "brew"
        self.alog.log(0, "replay_seed_brew", t0=round(t0, 3), rate=round(rate, 2),
                      stop_w=stop_w, drip_t=round(drip_t, 2))
        self.log(f"SEED brew (started before the window): t0={t0:.2f} rate={rate:.1f} "
                 f"stop_w={stop_w} drip_t={drip_t:.2f}")

    def _replay_advance(self, dt):
        if not self.replay:
            return
        if self.rp_mode == "serve":
            if self.rp_hold:
                return
            self.rp_t += dt * self.rp_speed
            frames = self.replay["frames"]
            while self.rp_idx < len(frames) and frames[self.rp_idx]["t"] <= self.rp_t:
                fr = frames[self.rp_idx]
                self.rp_idx += 1
                if fr["dir"] == "rx" and fr["cmd"] == 0x83:
                    self.regs[fr["sub"]:fr["sub"] + len(fr["words"])] = fr["words"]
                elif fr["dir"] == "tx" and fr["cmd"] == 0x06:
                    self.handle_write(fr["sub"], fr["words"], source="replay")
            if self.rp_idx >= len(frames) and not self.replay.get("ended"):
                self.replay["ended"] = True
                self.alog.log(self.t, "replay_end", rp_t=self.rp_t)

    # ---- verify: frame-by-frame comparison against captured ground truth ----
    # Tolerance classes (documented in README.md):
    #   exact  : setup/flag/static — must match exactly
    #   event  : trigger/work_sta — time-shifted match ±0.75 s allowed
    #   sensor : temperatures/ADC — |Δ| ≤ 15; v6.6: physical events are synced
    #            (word48 milk refill/probe: rec<sim-30 or dev>60; word68
    #            water consumption: dev>60) — like weight physync
    #   weight : weights — |Δ| ≤ 30 or time-shifted; physical jumps are synced
    #            (v6.6: >150 for calibrated, >40 for raw cells + base adjustment)
    #   fast   : current/flow/pressure — |Δ| ≤ max(400, 35 %) or time-shifted ±15 %;
    #            stochastic idle maintenance cycles are tolerated on both sides
    #            (min<200 vs max>200 — small 400-1000 and large >1000, all captures)
    # ws sequence: short bit9 pre-pulses are filtered on both sides (stochastic
    # placement, v3 vs. steam-live — see _filter_bit9_pulses).
    EXACT_WORDS = set([0, 1, 2, 4, 5, 7, 8, 10, 12, 13, 14, 15, 17, 18, 19, 20,
                       21, 23, 25, 26, 27, 28, 29, 30, 31, 32, 34, 35, 36, 37,
                       43, 44, 45, 46, 54, 56, 57, 61, 62, 63, 66, 67, 73]
                      + list(range(59, 61)) + list(range(64, 66))
                      + list(range(74, 112)) + [112, 113, 114, 128])
    EVENT_WORDS = {3, 6, 9, 16, 22, 24, 33}
    SENSOR_WORDS = {40, 41, 42, 47, 48, 49, 68, 69, 70, 71, 72}
    WEIGHT_WORDS = {38, 39, 53, 55}
    FAST_WORDS = {50, 51, 52, 58}
    TIMESHIFT = 0.75

    def _verify_init(self):
        frames = self.replay["frames"]
        self.rp_verify = {
            "words": {}, "n": 0, "ok": 0, "exact_fail": 0,
            "physical_sync": 0, "spike_ok": 0, "timeshift_ok": 0,
            "transitions_rec": [], "transitions_sim": [],
            "last_ws_rec": None, "last_ws_sim": None,
            "events": [],
        }
        # captured value timelines per word (for time-shifted match);
        # work_sta is masked for stochastic idle pulses (bits 10/11/13)
        self.rp_timelines = {}
        for fr in frames:
            if fr["dir"] == "rx" and fr["cmd"] == 0x83:
                for i, w in enumerate(fr["words"]):
                    word = fr["sub"] + i
                    if word == 33:
                        w &= ~WS_PULSE_MASK
                    tl = self.rp_timelines.setdefault(word, [])
                    if not tl or tl[-1][1] != w:
                        tl.append((fr["t"], w))
        self.rp_frame_idx = 0

    def _verify_step(self):
        """Called in verify mode: advance the sim to the next captured frame and compare."""
        frames = self.replay["frames"]
        v = self.rp_verify
        while self.rp_frame_idx < len(frames):
            fr = frames[self.rp_frame_idx]
            self.advance(fr["t"])
            self.rp_frame_idx += 1
            if fr["dir"] == "tx" and fr["cmd"] == 0x06:
                self.handle_write(fr["sub"], fr["words"], source="replay")
                v["events"].append({"t": fr["t"], "kind": "write", "sub": fr["sub"],
                                    "words": fr["words"]})
            elif fr["dir"] == "rx" and fr["cmd"] == 0x83:
                # v6.6: the wand switch is a PHYSICAL input (no UART write,
                # STEAMLIVE §6) — in verify it is reconstructed from the capture's
                # word44 bit0, so the flush model is tested autonomously against ground truth.
                if fr["sub"] <= 44 < fr["sub"] + len(fr["words"]):
                    rec_wand = bool(fr["words"][44 - fr["sub"]] & 1)
                    if rec_wand != self.wand_state:
                        self.inject({"type": "wand", "on": rec_wand})
                ws_rec = fr["words"][0] if fr["sub"] == 33 else None
                if ws_rec is not None:
                    m = ws_rec & ~WS_PULSE_MASK
                    if m != v["last_ws_rec"]:
                        v["transitions_rec"].append((fr["t"], m))
                        v["last_ws_rec"] = m
                ws_sim = self.regs[33] & ~WS_PULSE_MASK
                if ws_sim != v["last_ws_sim"]:
                    v["transitions_sim"].append((self.t, ws_sim))
                    v["last_ws_sim"] = ws_sim
                for i, rec in enumerate(fr["words"]):
                    word = fr["sub"] + i
                    sim = self.regs[word]
                    self._verify_word(word, fr["t"], sim, rec)
        return self._verify_report()

    def _verify_word(self, word, t, sim, rec):
        v = self.rp_verify
        st = v["words"].setdefault(word, {"n": 0, "exact": 0, "tol": 0,
                                          "timeshift": 0, "spike": 0,
                                          "physync": 0, "fail": 0, "max_dev": 0})
        st["n"] += 1
        v["n"] += 1
        if word == 33:
            sim &= ~WS_PULSE_MASK
            rec &= ~WS_PULSE_MASK
        dev = abs(s16(sim) - s16(rec))
        st["max_dev"] = max(st["max_dev"], dev)
        if sim == rec:
            st["exact"] += 1
            v["ok"] += 1
            return
        cls = ("exact" if word in self.EXACT_WORDS else
               "event" if word in self.EVENT_WORDS else
               "sensor" if word in self.SENSOR_WORDS else
               "weight" if word in self.WEIGHT_WORDS else
               "fast" if word in self.FAST_WORDS else "exact")
        if cls == "exact":
            st["fail"] += 1
            v["exact_fail"] += 1
            return
        if cls == "sensor":
            if dev <= 15:
                st["tol"] += 1
                v["ok"] += 1
                return
            # v6.6 physical sensor events are synced from ground truth (like weights):
            # word48: cold milk poured in / probe moved (rec < sim-20) or
            # steam jet directly on the probe during flush (dev > 60). word68:
            # water consumption/refill events (dev > 40).
            if word == 48 and (dev > 60 or s16(rec) < s16(sim) - 20):
                self.regs[word] = rec
                st["physync"] += 1
                v["physical_sync"] += 1
                v["ok"] += 1
                return
            if word == 68 and dev > 40:
                self.regs[word] = rec
                st["physync"] += 1
                v["physical_sync"] += 1
                v["ok"] += 1
                return
        if cls == "weight":
            if dev <= 30:
                st["tol"] += 1
                v["ok"] += 1
                return
            # v6.6: cup removed / tare glitch = jump >100 (raw cells >40);
            # sync from ground truth AND adjust the raw-cell base so the formula follows.
            # (100 not 150: a cup removal can land on an intermediate value whose
            # distance to the final value is <150 — then the sim gets "stuck" there.)
            th = 40 if word in (53, 55) else 100
            if dev > th:
                self.regs[word] = rec
                if word == 55:
                    self.wraw_base[55] = rec - max(0, s16(self.regs[39])) // 45
                elif word == 53:
                    self.wraw_base[53] = rec - max(0, s16(self.regs[38])) // 40
                elif word == 39:
                    rec55 = self._tl_value(55, t)
                    if rec55 is not None:
                        self.wraw_base[55] = rec55 - max(0, s16(rec)) // 45
                elif word == 38:
                    rec53 = self._tl_value(53, t)
                    if rec53 is not None:
                        self.wraw_base[53] = rec53 - max(0, s16(rec)) // 40
                st["physync"] += 1
                v["physical_sync"] += 1
                v["ok"] += 1
                return
        if cls == "fast":
            ws_m = self.regs[33] & ~WS_PULSE_MASK
            idle = ws_m in (WS_IDLE, 0x0105) and not self.act
            # v6.6: stochastic idle maintenance cycles are tolerated on both sides:
            # live cur1 400-1000 / word51 400-900 in ~30-45 % of idle frames
            # (ALL captures) — the phase cannot be matched deterministically. Same
            # principle as the large idle spikes (>1000 vs <200).
            if idle and min(sim, rec) < 200 and max(sim, rec) > 200:
                st["spike"] += 1
                v["spike_ok"] += 1
                v["ok"] += 1
                return
            if dev <= max(400, 0.35 * abs(s16(rec))):
                st["tol"] += 1
                v["ok"] += 1
                return
        # time-shifted match: does the sim value occur in the capture within ±0.75 s?
        tl = self.rp_timelines.get(word, [])
        times = [x[0] for x in tl]
        lo = bisect.bisect_left(times, t - self.TIMESHIFT)
        hi = bisect.bisect_right(times, t + self.TIMESHIFT)
        window = [x[1] for x in tl[lo:hi]]
        if lo > 0:
            # v6.6 fix: the value that was ACTIVE at the window start (last change
            # before lo) — otherwise long static periods fail (e.g. trigger words
            # that stay put for minutes between two changes).
            window.append(tl[lo - 1][1])
        if sim in window:
            st["timeshift"] += 1
            v["timeshift_ok"] += 1
            v["ok"] += 1
            return
        if cls == "fast" and window and any(
                abs(s16(sim) - s16(w)) <= max(200, 0.15 * abs(s16(w))) for w in window):
            st["timeshift"] += 1
            v["timeshift_ok"] += 1
            v["ok"] += 1
            return
        st["fail"] += 1

    def _tl_value(self, word, t):
        """Captured value of word at time t (last change ≤ t)."""
        val = None
        for tt, vv in self.rp_timelines.get(word, []):
            if tt <= t:
                val = vv
            else:
                break
        return val

    @staticmethod
    def _filter_bit9_pulses(trans):
        """v6.6: short bit9 pulses DURING pre-infusion (0x1107→0x1307→0x1107,
        <0.5 s) are stochastic: the v3 capture has the pulse at el 1.14 s, steam-live
        at el 4.97 s — same pre length, different placement. No deterministic
        model can hit both; the pulses are therefore filtered out of the ws sequence on
        BOTH sides (same principle as WS_PULSE_MASK for idle pulses bits 10/11/13)."""
        out = []
        skip_return = False
        for i, (t, w) in enumerate(trans):
            if skip_return:
                skip_return = False   # the pulse returning to pre is not a real transition
                continue
            if (w == WS_BREW_EXT and 0 < i < len(trans) - 1
                    and trans[i - 1][1] == WS_BREW_PRE
                    and trans[i + 1][1] == WS_BREW_PRE
                    and trans[i + 1][0] - t < 0.5):
                skip_return = True
                continue
            out.append((t, w))
        return out

    def _verify_report(self):
        v = self.rp_verify
        ratio = v["ok"] / v["n"] if v["n"] else 0.0
        f_rec = self._filter_bit9_pulses(v["transitions_rec"])
        f_sim = self._filter_bit9_pulses(v["transitions_sim"])
        tr_rec = [x[1] for x in f_rec]
        tr_sim = [x[1] for x in f_sim]
        seq_ok = tr_rec == tr_sim
        timing_ok = True
        if seq_ok and len(f_rec) == len(f_sim):
            for (ta, _), (tb, _) in zip(f_rec, f_sim):
                if abs(ta - tb) > 1.5:
                    timing_ok = False
        exact_ok = v["exact_fail"] == 0
        passed = exact_ok and ratio >= 0.92 and seq_ok and timing_ok
        rep = {
            "replay": self.replay["path"], "frames": len(self.replay["frames"]),
            "duration_s": self.replay["duration"],
            "samples": v["n"], "match_ratio": round(ratio, 5),
            "exact_fail": v["exact_fail"], "physical_sync": v["physical_sync"],
            "spike_ok": v["spike_ok"], "timeshift_ok": v["timeshift_ok"],
            "work_sta_sequence_ok": seq_ok, "work_sta_timing_ok": timing_ok,
            "transitions_recorded": [(round(t, 3), hex(w)) for t, w in v["transitions_rec"]],
            "transitions_sim": [(round(t, 3), hex(w)) for t, w in v["transitions_sim"]],
            "per_word": {str(w): s for w, s in sorted(v["words"].items())},
            "pass": passed,
            "criteria": "exact=100%, match≥92%, work_sta sequence identical, timing ±1.5 s",
        }
        return rep

    # ------------------------------------------------------------ ctrl socket
    def _ctrl_accept(self):
        try:
            conn, _ = self.ctrl_listener.accept()
        except BlockingIOError:
            return
        conn.setblocking(False)
        self.ctrl_clients[conn.fileno()] = {"sock": conn, "buf": b""}

    def _ctrl_read(self):
        for fd in list(self.ctrl_clients):
            c = self.ctrl_clients[fd]
            try:
                data = c["sock"].recv(65536)
            except BlockingIOError:
                continue
            except OSError:
                data = b""
            if not data:
                c["sock"].close()
                del self.ctrl_clients[fd]
                continue
            c["buf"] += data
            while b"\n" in c["buf"]:
                line, c["buf"] = c["buf"].split(b"\n", 1)
                if not line.strip():
                    continue
                try:
                    req = json.loads(line)
                except json.JSONDecodeError:
                    continue
                resp = self.ctrl_dispatch(req)
                resp["id"] = req.get("id")
                try:
                    c["sock"].sendall((json.dumps(resp, ensure_ascii=False) + "\n").encode())
                except OSError:
                    pass

    def ctrl_dispatch(self, req):
        cmd = req.get("cmd")
        if cmd == "ping":
            return {"ok": True, "t": self.t, "mode": self.mode, "link": self.link,
                    "work_sta": self.regs[33], "version": self.version_received,
                    "speed": self.speed, "paused": self.paused,
                    "profile": self.profile, "require_wand": self.require_wand,
                    "wand": self.wand_state,
                    "steam": ({"latch": self.steam_ch["latch"],
                               "src": self.steam_ch["src"]}
                              if self.steam_ch else None),
                    # v6.14: the MCU's power state (deep sleep/wake)
                    "power": {"state": self.power_state,
                              "key_down": self.power_key_down,
                              "hold_s": (round(self.t - self.power_key_t0, 3)
                                         if self.power_key_down else 0.0),
                              "dvale_pending": self.dvale_at is not None,
                              "alarm": list(self.alarm) if self.alarm else None,
                              "wakes": self.stats["power_wakes"],
                              "rx_dropped": self.stats["dvale_rx_dropped"]}}
        if cmd == "regs":
            words = req.get("words", list(range(REGN)))
            return {"ok": True, "values": {str(w): self.regs[w] for w in words}}
        if cmd == "stats":
            return {"ok": True, "stats": self.stats,
                    "boot_push_setup": self.boot_push_setup,
                    "boot_push_preinf": self.boot_push_preinf}
        if cmd == "replay_window":
            # ground-truth window: min/max of a word in the capture within
            # serve wall time [t0,t1] (converted with speed) — aliasing-safe
            if not self.replay:
                return {"ok": False, "error": "no replay"}
            w = int(req.get("word", 39))
            t0 = float(req.get("t0", 0)) * self.rp_speed
            t1 = float(req.get("t1", 1e9)) * self.rp_speed
            vals = []
            for fr in self.replay["frames"]:
                if fr["dir"] == "rx" and fr["cmd"] == 0x83 and t0 <= fr["t"] <= t1:
                    if fr["sub"] <= w < fr["sub"] + len(fr["words"]):
                        vals.append(fr["words"][w - fr["sub"]])
            if req.get("signed"):
                vals = [s16(v) for v in vals]
            return {"ok": True, "n": len(vals),
                    "min": min(vals) if vals else None,
                    "max": max(vals) if vals else None}
        if cmd == "replay_anchor":
            # Start the serve replay on the E2E runner's clock: 'elapsed' is the wall time
            # since the UI client's clock origin (t_ms=0), so capture time = elapsed * speed
            # stays aligned with the UI's timestamps no matter how long startup took.
            if not self.replay or self.rp_mode != "serve":
                return {"ok": False, "error": "no serve replay loaded"}
            self.rp_t = max(0.0, float(req.get("elapsed", 0.0))) * self.rp_speed
            self.rp_hold = False
            self.alog.log(self.t, "replay_anchor", rp_t=round(self.rp_t, 3))
            return {"ok": True, "rp_t": self.rp_t}
        if cmd == "inject":
            return {"ok": self.inject(req.get("event", {}))}
        if cmd == "expect_keepawake":
            self.expect_keepawake = bool(req.get("value", True))
            if self.expect_keepawake:
                self.last_awake_write = self.t
            return {"ok": True}
        if cmd == "ack_profile":
            p = req.get("profile", "normal")
            self.profile = dict(ACK_PROFILES[p]) if isinstance(p, str) else dict(p)
            return {"ok": True, "profile": self.profile}
        if cmd == "speed":
            # play mode: scale sim time (1×/2×/8×). The UI client still runs in
            # wall-clock real time; only the MCU's curves/time are sped up.
            try:
                self.speed = max(0.1, min(64.0, float(req.get("factor", 1.0))))
            except (TypeError, ValueError):
                return {"ok": False, "error": "invalid factor"}
            return {"ok": True, "speed": self.speed}
        if cmd == "pause":
            self.paused = bool(req.get("value", True))
            return {"ok": True, "paused": self.paused}
        if cmd == "require_wand":
            self.require_wand = bool(req.get("value", True))
            return {"ok": True, "require_wand": self.require_wand}
        if cmd == "calm":
            self.calm = max(0.05, min(1.0, float(req.get("factor", 1.0))))
            if "maint" in req:
                self.maint = bool(req["maint"])
            self.calm_prev = {}
            self.calm_hist = {}
            return {"ok": True, "calm": self.calm, "maint": self.maint}
        if cmd == "reboot":
            self.do_reboot(cause="ctrl")
            return {"ok": True}
        if cmd == "exit":
            self.running = False
            return {"ok": True}
        return {"ok": False, "error": f"unknown cmd: {cmd}"}

    def inject(self, ev):
        """Sim-side event (from the runner/scenario)."""
        typ = ev.get("type")
        r = self.regs
        if typ == "fault":
            for w, key in ((34, "e1"), (35, "e2"), (36, "e3")):
                if key in ev:
                    dur = ev.get("dur")
                    self.faults[w] = (int(ev[key]) & 0xFFFF,
                                      self.t + dur if dur else None)
                    r[w] = int(ev[key]) & 0xFFFF
                    self.alog.log(self.t, "fault_set", word=w, value=r[w], dur=dur)
            return True
        if typ == "fault_clear":
            for w in (34, 35, 36):
                self.faults.pop(w, None)
                r[w] = 0
            self.alog.log(self.t, "fault_clear", word="all")
            return True
        if typ == "boilers":
            # v6.18 (additive): boiler temperatures + the heat-stage model for words 62/63.
            # {"e": 0.1 °C, "s": 0.1 °C, "e_rate": 0.1 °C/s, "s_rate": 0.1 °C/s}; every key optional.
            # Rates default to the idle model's 5.0 (0.5 °C/s). "model": false switches the model off again.
            if ev.get("model", True) is False:
                self.heat = None
            else:
                self.heat = {"e_rate": float(ev.get("e_rate", (self.heat or {}).get("e_rate", 5.0))),
                             "s_rate": float(ev.get("s_rate", (self.heat or {}).get("s_rate", 5.0)))}
            for w, key in ((40, "e"), (47, "s")):
                if key in ev:
                    self.ft[w] = float(int(ev[key]))
                    r[w] = int(ev[key]) & 0xFFFF
                    self.ft_last[w] = r[w]
            r[41] = r[40]
            if self.heat:
                r[62], r[63] = self.heat_stage(r)
            self.alog.log(self.t, "boilers", e=r[40], s=r[47], st62=r[62], st63=r[63],
                          model=self.heat is not None)
            return True
        if typ == "set_reg":
            w = int(ev["word"])
            r[w] = int(ev["value"]) & 0xFFFF
            self.alog.log(self.t, "set_reg", word=w, value=r[w])
            return True
        if typ in ("wand", "wand_down", "wand_up"):
            # v6.6: PE.2 wand switch (STEAMLIVE §6). word44 bit0 + word46 mirror
            # the switch (0/1 — NOT a flowmeter counter; the REGISTERMAP correction).
            # The wand EDGE starts/stops the flush path (latch=0, no word24,
            # no milk auto-stop); display steam (latch=1) is unaffected.
            on = (typ == "wand_down") or (typ == "wand" and bool(ev.get("on", True)))
            if typ == "wand_up":
                on = False
            if on == self.wand_state:
                return True               # idempotent (no edge)
            self.wand_state = on
            r[44] = (r[44] | 1) if on else (r[44] & ~1)
            r[46] = 1 if on else 0
            self.alog.log(self.t, "wand", on=on, input_bits=r[44], ord46=r[46])
            if on:
                if (self.steam_ch is None and r[0] == 1 and r[1] == 0
                        and r[63] == 100 and not (r[34] or r[35] or r[36])):
                    self._start_steam(latch=0, src="wand")
            elif self.steam_ch is not None and not self.steam_ch["latch"]:
                self._stop_steam("wand released")
            return True
        if typ == "power_key":
            # v6.14: simulated power button (PC.8 / k62091). value=1 down,
            # value=0 release; hold ≥0.8 s in deep sleep → wake/reset. Or a single
            # combined hold: {"action":"hold","dur":1.6} (auto-release after dur).
            if ev.get("action") == "hold":
                dur = float(ev.get("dur", 1.6))
                self.power_key_down = True
                self.power_key_t0 = self.t
                self._key_up_at = self.t + dur
                self.alog.log(self.t, "power_key", value=1, hold_s=dur)
                return True
            v = int(ev.get("value", 1))
            if v == 1:
                if not self.power_key_down:
                    self.power_key_down = True
                    self.power_key_t0 = self.t
                    self.alog.log(self.t, "power_key", value=1)
            else:
                self._power_key_release()
            return True
        if typ == "power_off":
            # convenience: deep sleep without a UART write (as if word0=0 was written)
            if self.power_state == "on":
                self.regs[0] = 0
                self.alog.log(self.t, "power_write", word=0, value=0,
                              source="inject")
                self._enter_dvale()
            return True
        if typ == "power_on":
            # convenience: wake/reset without a button hold (like alarm/button)
            self._power_wake("ctrl")
            return True
        if typ == "alarm":
            # set the alarm directly (like cmd8's alarm field): day/hour/minute
            self.alarm = (int(ev.get("day", 0)), int(ev.get("hour", 0)),
                          int(ev.get("minute", 0)))
            if not any(self.alarm):
                self.alarm = None
            self.alarm_fired = False
            self.alog.log(self.t, "alarm_set", alarm=list(self.alarm)
                          if self.alarm else [0, 0, 0])
            return True
        if typ == "garbage":
            self.garbage_until = self.t + float(ev.get("dur", 2.0))
            self.garbage_crc = bool(ev.get("crc_too", True))
            self.alog.log(self.t, "garbage_start", dur=ev.get("dur", 2.0))
            return True
        if typ == "link":
            state = ev.get("state", "up")
            if state == "closed" and self.link != "closed":
                self._close_pty()
                self.link = "closed"
                self.alog.log(self.t, "link_closed")
            elif state == "up" and self.link == "closed":
                self._open_pty()
                self.link = "up"
                self.version_received = False
                self.alog.log(self.t, "link_reopened", pty=self.slave_path)
            elif state in ("up", "silent"):
                self.link = state
                self.alog.log(self.t, "link_state", state=state)
            return True
        if typ == "cup_removed":
            w = 38 if ev.get("scale") == "grind" else 39
            r[w] = int(ev.get("value", 64607)) & 0xFFFF
            self.alog.log(self.t, "cup_removed", word=w, value=r[w])
            return True
        if typ == "milk_jump":
            r[48] = int(ev["value"]) & 0xFFFF
            return True
        if typ == "bad_len":
            # v6.5 scenarios (rx-stall-repro): one 0x83 frame with a BIT ERROR IN THE
            # LENGTH BYTE (default: set bit7 → L+128 > REGN). A parser that
            # blindly waits for 2*L+7 bytes stalls until the buffer overflows
            # (UI_v6.md v6.4 finding: ~3.5 s in merakiplus's rx_feed). The CRC is still
            # valid for the ORIGINAL length — the error is only in LEN.
            sub = int(ev.get("sub", 33))
            ln = max(1, min(int(ev.get("len", 7)), 60))
            fr = bytearray(build_frame(0x83, sub, self.regs[sub:sub + ln]))
            fr[4] = int(ev.get("len_byte", fr[4] | 0x80)) & 0xFF
            self.send_tx(bytes(fr))
            self.alog.log(self.t, "bad_len_inject", sub=sub, len=ln,
                          len_byte=fr[4], frame=hexs(bytes(fr)))
            return True
        self.alog.log(self.t, "inject_unknown", event=ev)
        return False

    # ------------------------------------------------------------ main loop
    def advance(self, target_t):
        """Advance sim time to target_t in ≤10 ms ticks (deterministic)."""
        while self.t < target_t - 1e-9:
            dt = min(TICK, target_t - self.t)
            self.t += dt
            # v6.14: word0=0 written → deep sleep within ≤0.5 s (status task 500 ms)
            if self.dvale_at is not None and self.t >= self.dvale_at:
                self._enter_dvale()
            if self.power_state == "off":
                # Deep sleep: regs frozen, UART silent, activities stopped. Only
                # RTC/alarm and a button hold can wake the MCU (CPU reset).
                self._tick_rtc(dt)
                if self._key_up_at is not None and self.t >= self._key_up_at:
                    self._key_up_at = None
                    self._power_key_release()
                continue
            self._tick_rtc(dt)
            if self._key_up_at is not None and self.t >= self._key_up_at:
                self._key_up_at = None
                self._power_key_release()
            self._tick_faults()
            if self.replay and self.rp_mode == "serve":
                self._replay_advance(dt)
            else:
                self._tick_triggers()
                self._tick_activity(dt)
                self._tick_steam(dt)
                self._tick_post(dt)
                self._tick_steam_post(dt)
                self._tick_idle_sensors(dt)
            self._tick_keepalive()
            self._tick_boot_push_check()
            if self.calm < 1.0 and not self.replay:
                a = self.calm
                for w in (38, 39, 40, 42, 47, 48, 50, 51, 52, 53, 55, 58, 69):
                    signed = w in (38, 39)
                    raw = s16(self.regs[w]) if signed else self.regs[w]
                    hist = self.calm_hist.setdefault(w, [])
                    hist.append(raw)
                    if len(hist) > 3:
                        hist.pop(0)
                    med = sorted(hist)[len(hist) // 2]      # median-3: kills single-sample spikes
                    prev = self.calm_prev.get(w, med)
                    val = int(round(prev + (med - prev) * a))
                    if signed and abs(val) < 2.5:      # deadband: <0.25 g → 0 (user's request)
                        val = 0
                    self.regs[w] = (val & 0xFFFF) if signed else val
                    self.calm_prev[w] = val
            if self.pty and self.link == "up":
                if not self.version_received and self.t >= self.next_ver:
                    self.send_tx(build_version_request())
                    self.stats["version_reqs"] += 1
                    self.next_ver = self.t + VER_INTERVAL
                if self.t < self.garbage_until:
                    n = self.rng.randint(1, 24)
                    junk = bytes(self.rng.getrandbits(8) for _ in range(n))
                    try:
                        os.write(self.master, junk)
                        self.stats["garbage_tx_bytes"] += n
                    except OSError:
                        pass
                    if self.garbage_crc and self.rng.random() < 0.3:
                        bad = bytearray(build_frame(0x83, 33, self.regs[33:40]))
                        bad[-1] ^= 0xFF       # CRC corruption
                        try:
                            os.write(self.master, bytes(bad))
                        except OSError:
                            pass
                self._flush_tx()

    def run(self):
        while self.running:
            wall = time.monotonic() - self.t0_wall
            dt = wall - self._last_wall
            self._last_wall = wall
            if not self.paused and dt > 0:
                self.advance(self.t + dt * self.speed)
            rfds = list(self.ctrl_clients)
            if self.ctrl_listener:
                rfds.append(self.ctrl_listener.fileno())
            if self.pty and self.master is not None and self.link != "closed":
                rfds.append(self.master)
            try:
                r, _, _ = select.select(rfds, [], [], 0.005)
            except (OSError, ValueError):
                r = []
            for fd in r:
                if self.ctrl_listener and fd == self.ctrl_listener.fileno():
                    self._ctrl_accept()
                elif fd in self.ctrl_clients:
                    self._ctrl_read()
                elif self.pty and self.master is not None and fd == self.master:
                    try:
                        data = os.read(self.master, 4096)
                    except BlockingIOError:
                        data = b""
                    except OSError:
                        data = b""
                        if self.link == "up":
                            self.link = "silent"
                    if data:
                        for cmd, sub, payload, _body in self.parser.feed(data):
                            self.handle_frame(cmd, sub, payload)
        self.alog.log(self.t, "sim_exit", stats=self.stats)
        self.alog.close()
        self._close_pty()
        if self.ctrl_listener:
            self.ctrl_listener.close()
            Path(self.ctrl_path).unlink(missing_ok=True)


# ---------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(description="Simulated MerakiTech powerMcu (E2E)")
    ap.add_argument("--config", help="sandbox config.json (default: sim/fixtures/config.json)")
    ap.add_argument("--spec", help="parity_spec.json (default: sim/fixtures/parity_spec.json)")
    ap.add_argument("--pty-file", help="file the pty slave path is written to")
    ap.add_argument("--ctrl", help="unix socket for injection/control (JSONL)")
    ap.add_argument("--assert-log", help="JSONL assertion log")
    ap.add_argument("--profile", default="normal",
                    help="ACK profile: normal|observed_v3|slow|lossy|no_ack (or JSON)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-require-wand", action="store_true",
                    help="legacy no-op (v6.6): display steam does not require the wand "
                         "(STEAMLIVE §5); the flag is kept for compatibility")
    ap.add_argument("--range-policy", choices=["log", "reject", "off"], default="log")
    ap.add_argument("--trigger-policy", choices=["reject", "pass"], default="reject")
    ap.add_argument("--replay", help="frames.csv for REPLAY mode")
    ap.add_argument("--replay-mode", choices=["serve", "verify"], default="serve")
    ap.add_argument("--speed", type=float, default=1.0, help="replay playback speed")
    ap.add_argument("--replay-hold", action="store_true",
                    help="serve replay: hold the replay clock at 0 until a 'replay_anchor' ctrl command")
    ap.add_argument("--verify-report", help="JSON report for replay verify")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    cfg = load_baseline_config(args.config) if args.config else load_baseline_config()
    spec = load_spec(args.spec) if args.spec else load_spec()
    profile = args.profile
    if profile.strip().startswith("{"):
        profile = json.loads(profile)

    if args.replay and args.replay_mode == "verify":
        sim = McuSim(config=cfg, spec=spec, seed=args.seed, profile=profile,
                     require_wand=not args.no_require_wand,
                     range_policy=args.range_policy, trigger_policy=args.trigger_policy,
                     assert_path=args.assert_log, pty=False, quiet=args.quiet)
        sim.load_replay(args.replay, mode="verify")
        rep = sim._verify_step()
        sim.alog.close()
        if args.verify_report:
            Path(args.verify_report).write_text(json.dumps(rep, indent=2, ensure_ascii=False))
        print(f"[replay-verify] {args.replay}: samples={rep['samples']} "
              f"match={rep['match_ratio']:.4f} exact_fail={rep['exact_fail']} "
              f"ws-sequence={'OK' if rep['work_sta_sequence_ok'] else 'FAIL'} "
              f"timing={'OK' if rep['work_sta_timing_ok'] else 'FAIL'} "
              f"→ {'PASS' if rep['pass'] else 'FAIL'}")
        if not args.quiet:
            for t, w in rep["transitions_sim"]:
                print(f"  sim  t={t:8.3f} work_sta={w}")
        sys.exit(0 if rep["pass"] else 1)

    sim = McuSim(config=cfg, spec=spec, seed=args.seed, profile=profile,
                 require_wand=not args.no_require_wand,
                 range_policy=args.range_policy, trigger_policy=args.trigger_policy,
                 assert_path=args.assert_log, pty=True, ctrl_path=args.ctrl,
                 pty_file=args.pty_file, quiet=args.quiet)
    if args.replay:
        sim.load_replay(args.replay, mode="serve", speed=args.speed)
        sim.rp_hold = args.replay_hold
    try:
        sim.run()
    except KeyboardInterrupt:
        sim.running = False
        sim.alog.log(sim.t, "sim_interrupt")
        sim.alog.close()


if __name__ == "__main__":
    main()