#!/usr/bin/env python3
"""selftest.py — mcu_sim selftests (DELIVERABLE 5): the protocol is proven WITHOUT
a merakiplus SIM build and without the runner.

Covers: CRC vectors, frame parsing (cmd9 byte-LEN, cmd8 fixed, cmd3 query),
chunked reads ≤32 B + resync, write policy (band/trigger/range), ACK echo +
latency/loss profiles, boot push + version handshake, packing 112-114,
auto-stop (grind/brew/water/steam auto+manual), MANUAL STOP v6.13
(word16/19/22/6 → auto-stop equivalent, T12), keep-awake assertion,
fault injection (E1/E2/E3, garbage, offline), cmd8 echo, DEEP SLEEP/WAKE v6.14
(word0=0 → silent deep sleep, button hold/alarm → reboot to factory defaults +
re-push, T13), and REPLAY verify against v3/v2/session/steam-live
(frame-for-frame with documented tolerances).

Run: python3 sim/selftest.py   (exit 0 = all green)
"""
import json
import os
import sys
import time
import tty
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
from proto import (crc16_modbus, build_frame, build_query, build_version_request,
                   build_version_response, build_power_frame, FrameParser, hexs,
                   pack112, pack113, unpack112, unpack113, config_to_words,
                   load_baseline_config, s16)
from mcu_sim import McuSim

N_PASS = N_FAIL = 0


def check(name, ok, detail=""):
    global N_PASS, N_FAIL
    N_PASS += bool(ok)
    N_FAIL += (not ok)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail and not ok else ""))


class RawClient:
    """Minimal raw protocol client on the sim's pty slave (like fuart_thread)."""
    def __init__(self, sim):
        self.sim = sim
        self.fd = os.open(sim.slave_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        self.parser = FrameParser()
        self.frames = []

    def send(self, data):
        os.write(self.fd, data)

    def pump(self, dt, chunk=32):
        # deterministic IO loop: advance the sim in 10 ms ticks and pump ITS RX
        # (master reads) — equivalent to sim.run()'s select loop, but without wall clock
        end = self.sim.t + dt
        while self.sim.t < end - 1e-9:
            self.sim.advance(min(self.sim.t + 0.010, end))
            if self.sim.master is not None:
                try:
                    data = os.read(self.sim.master, 4096)
                except (BlockingIOError, OSError):
                    data = b""
                if data:
                    for cmd, sub, payload, _b in self.sim.parser.feed(data):
                        self.sim.handle_frame(cmd, sub, payload)
        out = []
        while True:
            try:
                data = os.read(self.fd, chunk)   # ≤32 B chunks (fuart_thread)
            except BlockingIOError:
                break
            except OSError:
                break
            if not data:
                break
            out.extend(self.parser.feed(data))
        self.frames.extend(out)
        return out

    def close(self):
        os.close(self.fd)


def fresh_sim(**kw):
    kw.setdefault("pty", False)
    kw.setdefault("quiet", True)
    sim = McuSim(**kw)
    sim.next_pulse = 1e9            # deterministic: no stochastic idle pulses
    return sim


# ---------------------------------------------------------------- T1: CRC
def t1_crc():
    print("== T1: CRC-16/MODBUS + frame vectors ==")
    check("CRC over AA 55 89 01 00 00 = 0x816F (PROTOCOL §2.2)",
          crc16_modbus(bytes([0xAA, 0x55, 0x89, 0x01, 0x00, 0x00])) == 0x816F)
    check("version request frame = AA 55 89 01 00 00 6F 81",
          build_version_request() == bytes.fromhex("aa55890100006f81"))
    vecs = [
        ("cmd3 sub0 len56 query", "aa55030038dc06", lambda: build_query(0, 56)),
        ("cmd3 sub33 len7 query", "aa550321078446", lambda: build_query(33, 7)),
        ("cmd3 sub56 len56 query", "aa55033838cfc6", lambda: build_query(56, 56)),
        ("keep-awake sub1=[0] (SESSION §10.1)", "aa550601010000c533",
         lambda: build_frame(6, 1, [0])),
        ("sleep sub1=[1] (SESSION §10.1)", "aa550601010100c4a3",
         lambda: build_frame(6, 1, [1])),
        ("hotwater start (HOTWATER W1)",
         "aa5506090b01000000660300003c0003003c000100900100000000083e",
         lambda: build_frame(6, 9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])),
        ("steam start (SESSION §10.2)",
         "aa5506180801008a02000000000000000000000000cd67",
         lambda: build_frame(6, 24, [1, 650, 0, 0, 0, 0, 0, 0])),
        ("steam stop (SESSION §10.3)",
         "aa5506180800008a020000000000000000000000000cf7",
         lambda: build_frame(6, 24, [0, 650, 0, 0, 0, 0, 0, 0])),
        ("grind start (BREW W1)", "aa550603040100b9003c0001005d6e",
         lambda: build_frame(6, 3, [1, 185, 60, 1])),
        ("brew start (BREW W3)",
         "aa550605123c0000000000000001000000660300003c0003003c00000090010000000000000000010021df",
         lambda: build_frame(6, 5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0,
                                    400, 0, 0, 0, 0, 1])),
        ("boot-push preinf sub112 len3 (v6 selftest)",
         "aa55067003040105064600c702",
         lambda: build_frame(6, 112, [0x0104, 0x0605, 70])),
        # v6.13 (UI_v6 §18.1): manual stop frames, byte for byte
        ("water stop auto (v6.13 §18.1)", "aa550610010000c00f",
         lambda: build_frame(6, 16, [0])),
        ("water stop manual sw (v6.13 §18.1)", "aa550613010000c04b",
         lambda: build_frame(6, 19, [0])),
        ("brew stop (v6.13 §18.1)", "aa550616010000c087",
         lambda: build_frame(6, 22, [0])),
        ("grind stop (v6.13 §18.1)", "aa550606010000c447",
         lambda: build_frame(6, 6, [0])),
    ]
    for name, want, fn in vecs:
        got = fn().hex()
        check(f"frame vector: {name}", got == want, f"want={want} got={got}")
    # parse vectors
    p = FrameParser()
    fr = p.feed(bytes.fromhex("aa55030038dc06"))
    check("cmd3 parse: 7-byte query, LEN=expected words",
          fr == [(3, 0, 56, bytes.fromhex("aa55030038"))])
    fr = FrameParser().feed(build_version_request())
    check("0x89 parse: LEN=bytes, sub=0 (swapped order)",
          fr and fr[0][0] == 0x89 and fr[0][1] == 0 and fr[0][2] == b"\x00")
    fr = FrameParser().feed(build_version_response("1.1.1-69"))
    check("cmd9 parse: ASCII payload", fr and fr[0][0] == 9 and fr[0][2] == b"1.1.1-69")
    fr = FrameParser().feed(build_power_frame(bytes(10)))
    check("cmd8 parse: fixed 15-byte frame", fr and fr[0][0] == 8 and len(fr[0][2]) == 10)
    rt = FrameParser().feed(build_frame(0x83, 33, [0x0101, 0, 0, 0, 16, 65535, 64597]))
    check("0x83 roundtrip: words little-endian",
          rt and rt[0][2] == [0x0101, 0, 0, 0, 16, 65535, 64597])


# ---------------------------------------------------------------- T2: chunking
def t2_chunking():
    print("== T2: chunked reads ≤32 B + resync ==")
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    c.send(build_query(0, 56))
    fr = c.pump(0.02, chunk=32)
    check("119-byte reply to sub0/56 received in ≤32 B chunks",
          any(cmd == 0x83 and sub == 0 and len(w) == 56 for cmd, sub, w, _ in fr))
    resp = [f for f in fr if f[0] == 0x83 and f[1] == 0]
    # the setup words (0..32) are static; the sensor words have noise per tick
    check("reply words = model regs[0..32] (static setup words)",
          bool(resp) and resp[0][2][0:33] == sim.regs[0:33])
    # split TX frame across two writes w/ pause
    f = build_frame(6, 11, [880])
    c.send(f[:4])
    c.pump(0.01)
    c.send(f[4:])
    fr = c.pump(0.05)
    check("split write frame reassembled correctly (ACK 0x86)",
          any(cmd == 0x86 and sub == 11 and w == [880] for cmd, sub, w, _ in fr))
    check("write applied: regs[11]=880", sim.regs[11] == 880)
    # garbage + resync
    c.send(b"\x00\xff\x13\x37" * 5)
    c.send(build_query(33, 7))
    fr = c.pump(0.02)
    check("resync after garbage: sub33/7 reply",
          any(cmd == 0x83 and sub == 33 and len(w) == 7 for cmd, sub, w, _ in fr))
    # CRC-corrupt frame is rejected
    bad = bytearray(build_query(0, 56))
    bad[-1] ^= 0xFF
    n0 = sim.stats["queries"]
    c.send(bytes(bad))
    c.pump(0.02)
    check("CRC-corrupt query rejected (no response)", sim.stats["queries"] == n0)
    c.close()
    sim.alog.close()
    sim._close_pty()


# ---------------------------------------------------------------- T3: write policy
def t3_write_policy():
    print("== T3: write policy (band/trigger/range) ==")
    sim = fresh_sim()
    check("band: sub32 len1 rejected (MCU-owned)", not sim.handle_write(32, [1]))
    check("band: sub115 len1 rejected", not sim.handle_write(115, [1]))
    check("band: sub30 len4 rejected (crosses 32)", not sim.handle_write(30, [0, 0, 0, 0]))
    check("band: sub0 len32 ok (0..31)",
          sim.handle_write(0, [1, 0, 1, 0, 185, 60, 0, 0, 0, 0, 0, 870, 0, 60,
                               3, 60, 0, 400, 0, 0, 0, 0, 0, 360, 0, 650,
                               0, 0, 0, 0, 0, 0]))
    check("band: sub112 len3 ok (112..114)", sim.handle_write(112, [0x0104, 0x0605, 70]))
    check("band: sub113 len1 ok", sim.handle_write(113, [0x0605]))
    check("band: sub114 len1 ok", sim.handle_write(114, [70]))
    check("trigger: steam block w/ word30=1 rejected",
          not sim.handle_write(24, [1, 650, 0, 0, 0, 0, 1, 0]))
    check("trigger: word26=100 rejected", not sim.handle_write(26, [100]))
    check("trigger: word27=500 rejected", not sim.handle_write(27, [500]))
    check("trigger: word30=1 rejected (reboot)", not sim.handle_write(30, [1]))
    check("no reboot happened", sim.stats["reboots"] == 0)
    sim.handle_write(11, [1200])
    rv = [e for e in sim.alog.events if e["kind"] == "range_violation"]
    check("range: word11=1200 is LOGGED (MCU has no checks)", len(rv) == 1)
    check("range: the value is applied anyway (memcpy model)", sim.regs[11] == 1200)
    sim2 = fresh_sim(range_policy="reject")
    check("range-policy=reject: word11=1200 rejected", not sim2.handle_write(11, [1200]))
    sim3 = fresh_sim(trigger_policy="pass")
    sim3.handle_write(30, [1])
    check("trigger-policy=pass: word30=1 reboots the sim", sim3.stats["reboots"] == 1)
    ok = sim.handle_write(11, [880])
    check("normal write ok + ACK scheduled", ok and sim.regs[11] == 880)
    kinds = [e["kind"] for e in sim.alog.events]
    check("assertions log: write_rejected + range_violation + reboot_trigger logged",
          "write_rejected" in kinds and "range_violation" in kinds)


# ---------------------------------------------------------------- T4: packing
def t4_packing():
    print("== T4: byte packing word112/113/114 ==")
    cfg = load_baseline_config()
    w = config_to_words(cfg)
    check("config → word112 = 0x0104 (switch=1, pressure=4)", w[112] == 0x0104)
    check("config → word113 = 0x0605 (time=6, break=5)", w[113] == 0x0605)
    check("config → word114 = 70 (whole °C)", w[114] == 70)
    check("boot-hardcodes: word5/13/15 = 60", w[5] == 60 and w[13] == 60 and w[15] == 60)
    ok = True
    for sw in (0, 1):
        for pr in (3, 4):
            for ti in (5, 6, 15, 60):
                for br in (0, 5, 255):
                    ok &= unpack112(pack112(sw, pr)) == (sw, pr)
                    ok &= unpack113(pack113(ti, br)) == (ti, br)
    check("pack/unpack roundtrip: all (switch,pressure,time,break) combinations", ok)
    check("word112=(0,3)=0x0003 / (1,3)=0x0103", pack112(0, 3) == 0x0003 and pack112(1, 3) == 0x0103)


# ---------------------------------------------------------------- T5: boot-push/handshake
def t5_boot():
    print("== T5: version handshake + boot push on connect ==")
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    fr = c.pump(0.06)
    reqs = [f for f in fr if f[0] == 0x89]
    check("0x89 requests every ~20 ms from start", len(reqs) >= 2)
    c.send(build_version_response("1.1.1-69"))
    c.pump(0.05)
    n = sim.stats["version_reqs"]
    c.pump(0.2)
    check("0x89 stops after cmd9 reply", sim.stats["version_reqs"] == n)
    check("version handshake logged",
          any(e["kind"] == "version_handshake" for e in sim.alog.events))
    words = config_to_words(load_baseline_config())
    mirror = [0] * 31
    for wd, v in words.items():
        if 1 <= wd <= 30:
            mirror[wd] = v
    c.send(build_frame(6, 1, mirror[1:31]))
    c.send(build_frame(6, 112, [words[112], words[113], words[114]]))
    fr = c.pump(0.1)
    acks = [f for f in fr if f[0] == 0x86]
    check("boot push ACKed (sub1 len30 + sub112 len3)",
          any(a[1] == 1 and len(a[2]) == 30 for a in acks) and
          any(a[1] == 112 and len(a[2]) == 3 for a in acks))
    check("boot push logged in assertions", sim.boot_push_setup and sim.boot_push_preinf)
    check("boot push applied: regs[4]=185, regs[11]=870, regs[112]=0x0104",
          sim.regs[4] == 185 and sim.regs[11] == 870 and sim.regs[112] == 0x0104)
    # missing boot push is detected
    sim2 = fresh_sim(pty=True)
    c2 = RawClient(sim2)
    c2.send(build_version_response("x"))
    c2.pump(7.0)
    check("boot_push_missing logged when client does not push",
          any(e["kind"] == "boot_push_missing" for e in sim2.alog.events))
    c.close(); c2.close()
    sim.alog.close(); sim2.alog.close()
    sim._close_pty(); sim2._close_pty()


# ---------------------------------------------------------------- T6: auto-stop
def t6_autostop():
    print("== T6: phase machine + auto-stop (calibrated against BREW/HOTWATER/SESSION) ==")
    sim = fresh_sim()
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(0.5)
    check("grind: bit6 set after ~0.39 s, word3 consumed",
          sim.regs[33] == 0x0141 and sim.regs[3] == 0)
    sim.advance(5.0)
    check("grind: weight rises monotonically + cur_ch2 active",
          0 < s16(sim.regs[38]) < 185 and sim.regs[58] > 400)
    sim.advance(11.0)
    check("grind: auto-stop at ~184 (target 185), bit6 cleared",
          sim.regs[33] == 0x0101 and 183 <= s16(sim.regs[38]) <= 187)
    sim.advance(12.0)
    check("grind: word6 (tare) consumed after stop", sim.regs[6] == 0)
    # brew
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(3.0)
    check("brew: pre-infusion ws=0x1107, word51≈358 plateau",
          sim.regs[33] == 0x1107 and 330 <= sim.regs[51] <= 390)
    check("brew: word9 consumed, word22=1 (active flag)",
          sim.regs[9] == 0 and sim.regs[22] == 1)
    sim.advance(9.0)
    check("brew: extraction ws=0x1307 (bit9), word51≈1346",
          sim.regs[33] == 0x1307 and 1250 <= sim.regs[51] <= 1500)
    sim.advance(30.0)
    check("brew: auto-stop at ~344 (< target 360), word22 cleared by MCU",
          sim.regs[33] in (0x0101, 0x0105) and sim.regs[22] == 0 and
          330 <= s16(sim.regs[39]) <= 370)
    sim.advance(33.0)
    check("brew: post-drip → final weight ~366", 356 <= s16(sim.regs[39]) <= 376)
    # hotwater
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])
    sim.advance(0.5)
    check("water: ws=0x030B (bits 1+3+9), tare word39",
          sim.regs[33] == 0x030B and s16(sim.regs[39]) < 50)
    sim.advance(7.0)
    check("water: auto-stop at word39≥400, word16 cleared by MCU",
          sim.regs[33] == 0x0101 and sim.regs[16] == 0 and s16(sim.regs[39]) >= 389)
    # steam auto
    sim = fresh_sim()
    sim.inject({"type": "wand", "on": True})
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(1.0)
    check("steam: ws=0x4121 (bits 5+14) w/ wand", sim.regs[33] == 0x4121)
    sim.advance(15.0)
    check("steam: milk ramp towards 650", 400 <= sim.regs[48] <= 640)
    sim.advance(32.0)
    check("steam: AUTO stop at word48≥word25, word24 cleared by MCU",
          sim.regs[33] == 0x0101 and sim.regs[24] == 0 and sim.regs[48] >= 645)
    # steam manual + display latch (v6.6: STEAMLIVE §5-6 — display steam does NOT
    # require the wand; SESSION §5.2's wand requirement was a static guess, disproven live.
    # require_wand is now a legacy no-op; the wand switch drives the flush path).
    sim = fresh_sim()
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(2.0)
    check("steam: display steam starts WITHOUT wand (latch path, STEAMLIVE §5)",
          sim.regs[33] == 0x4121 and sim.regs[24] == 1)
    sim.inject({"type": "wand", "on": True})
    sim.advance(sim.t + 1.0)
    check("steam: wand switch during display steam: word44 bit0 + word46=1, steam continues",
          (sim.regs[44] & 1) and sim.regs[46] == 1 and sim.regs[33] == 0x4121)
    sim.advance(sim.t + 4.0)
    sim.handle_write(24, [0, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(sim.t + 0.2)
    check("steam: MANUAL stop (word24=0) clears bits 5+14 before setpoint",
          sim.regs[33] == 0x0101 and sim.regs[48] < 650)
    # v6.6: wand flush (latch=0) — word24=0, NO milk auto-stop, release stop
    sim = fresh_sim()
    sim.inject({"type": "milk_jump", "value": 640})
    sim.inject({"type": "wand_down"})
    sim.advance(sim.t + 0.5)
    check("flush: wand_down → ws=0x4121, word24=0, word46=1 (latch=0)",
          sim.regs[33] == 0x4121 and sim.regs[24] == 0 and sim.regs[46] == 1)
    sim.advance(sim.t + 3.0)
    check("flush: NO auto-stop even though milk >650 (latch gating, STEAMLIVE §6)",
          sim.regs[48] > 650 and sim.regs[33] == 0x4121 and sim.regs[24] == 0)
    sim.inject({"type": "wand_up"})
    sim.advance(sim.t + 0.2)
    check("flush: wand_up → bits cleared, word46=0, word44 bit0=0",
          sim.regs[33] == 0x0101 and sim.regs[46] == 0 and not (sim.regs[44] & 1))
    # v6.6: concurrent phases — flush DURING brew (work_sta union, STEAMLIVE §7)
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(9.0)                      # brew in extraction
    w_before = s16(sim.regs[39])
    sim.inject({"type": "wand_down"})
    sim.advance(sim.t + 0.5)
    check("concurrent: brew+wand → work_sta=0x5327 (union)", sim.regs[33] == 0x5327)
    sim.advance(sim.t + 2.0)
    check("concurrent: brew weight continues undisturbed during flush",
          s16(sim.regs[39]) > w_before)
    sim.inject({"type": "wand_up"})
    sim.advance(sim.t + 0.2)
    check("concurrent: wand release → 0x1307 (brew continues)", sim.regs[33] == 0x1307)
    # v6.6: display steam (latch=1) on top of brew + auto-stop → brew keeps running
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(9.0)
    sim.inject({"type": "milk_jump", "value": 630})
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(sim.t + 1.0)
    check("concurrent: display steam during brew → 0x5327, word24=1",
          sim.regs[33] == 0x5327 and sim.regs[24] == 1)
    sim.advance(sim.t + 3.0)                      # milk 630→650 in ~1.6 s → auto-stop
    check("concurrent: milk auto-stop → 0x1307 (brew continues), word24=0",
          sim.regs[33] == 0x1307 and sim.regs[24] == 0)
    # v6.6: thermal overshoot — peak ~stop+39 (~689) ~11 s after auto-stop
    sim = fresh_sim()
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(40.0)                     # auto-stop ~30.2 (milk 232→650)
    t_stop = next(e["t"] for e in sim.alog.events
                  if e["kind"] == "activity_stop" and e.get("activity") == "steam")
    sim.advance(t_stop + 11.5)
    check("overshoot: milk peak 685-695 (~stop+39) ~11 s after auto-stop",
          685 <= sim.regs[48] <= 695, f"word48={sim.regs[48]}")
    check("overshoot: word24 reset by MCU after auto-stop (no stop write)",
          sim.regs[24] == 0)
    # sleep/drain
    sim = fresh_sim()
    sim.next_pulse = 1e9            # no stochastic pulses in exact checks
    s0 = sim.regs[47]
    sim.handle_write(1, [1])
    sim.advance(sim.t + 1.0)
    check("sleep: word1=1 → work_sta=0x0001 (drain)", sim.regs[33] == 0x0001)
    sim.advance(100.0)
    check("sleep: boilers cool (SESSION: ~0.11 units/s s-boiler)",
          sim.regs[47] < s0 - 8)
    sim.handle_write(1, [0])
    sim.advance(sim.t + 1.0)
    check("wake: word1=0 → work_sta=0x0101", sim.regs[33] == 0x0101)
    sim.handle_write(0, [0])
    sim.advance(sim.t + 1.0)
    check("power off: word0=0 → work_sta=0x0000 (sleep task)", sim.regs[33] == 0x0000)
    # stochastic idle pulses (live behaviour, separate sim w/ pulses on)
    sim = McuSim(pty=False, quiet=True, seed=3)
    seen = set()
    for _ in range(60):
        sim.advance(sim.t + 0.5)
        seen.add(sim.regs[33])
    check("idle pulses: bits 10/11/13 observed over 30 s (stochastic maintenance)",
          any(ws & 0x2C00 for ws in seen) and 0x0101 in seen)


# ---------------------------------------------------------------- T7: keep-awake
def t7_keepalive():
    print("== T7: keep-awake assertion (word1=0 every ≤30 s) ==")
    sim = fresh_sim()
    sim.first_rx_t = 0.0            # simulates connect (keep-awake is monitored after connect)
    sim.handle_write(1, [0])
    sim.advance(29.0)
    check("no gap at 29 s", not any(e["kind"] == "keepalive_gap" for e in sim.alog.events))
    sim.advance(32.0)
    check("gap logged at >30 s without awake write",
          any(e["kind"] == "keepalive_gap" for e in sim.alog.events))
    sim2 = fresh_sim()
    sim2.first_rx_t = 0.0
    sim2.handle_write(1, [0])
    for _ in range(3):
        sim2.advance(sim2.t + 25.0)
        sim2.handle_write(1, [0])
    check("refresh every 25 s → no gaps",
          not any(e["kind"] == "keepalive_gap" for e in sim2.alog.events))
    sim3 = fresh_sim()
    sim3.first_rx_t = 0.0
    sim3.expect_keepawake = False
    sim3.advance(40.0)
    check("expect_keepawake=false suppresses gap (sleep scenarios)",
          not any(e["kind"] == "keepalive_gap" for e in sim3.alog.events))


# ---------------------------------------------------------------- T8: fault injection
def t8_faults():
    print("== T8: fault injection (E1/E2/E3 + safety stop) ==")
    sim = fresh_sim()
    sim.inject({"type": "fault", "e1": 0x20, "e2": 0x10, "e3": 0x30, "dur": 5})
    sim.advance(0.1)
    check("E1/E2/E3 bits set in word34/35/36",
          sim.regs[34] == 0x20 and sim.regs[35] == 0x10 and sim.regs[36] == 0x30)
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(sim.t + 1.0)
    check("grind is safety-stopped during fault", sim.regs[33] == 0x0101)
    sim.advance(6.0)
    check("fault clears after dur → word34/35/36=0",
          sim.regs[34] == 0 and sim.regs[35] == 0 and sim.regs[36] == 0)
    sim.inject({"type": "fault", "e3": 0x10})
    sim.advance(0.1)
    sim.inject({"type": "fault_clear"})
    sim.advance(0.1)
    check("fault_clear clears everything", sim.regs[36] == 0)


# ---------------------------------------------------------------- T9: ACK profiles
def t9_ack():
    print("== T9: ACK latency/loss/retransmit profiles ==")
    sim = fresh_sim(pty=True, profile="normal")
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 11, [880]))
    fr = c.pump(0.015)
    check("normal: no ACK after 15 ms", not any(f[0] == 0x86 for f in fr))
    fr = c.pump(0.02)
    check("normal: ACK ~22 ms w/ exact echo",
          any(f[0] == 0x86 and f[2] == [880] for f in fr))
    c.close(); sim.alog.close(); sim._close_pty()
    # no_ack
    sim = fresh_sim(pty=True, profile="no_ack")
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 11, [880]))
    fr = c.pump(0.5)
    check("no_ack: no ACK (save must block in UI)",
          not any(f[0] == 0x86 for f in fr))
    check("no_ack: ack_dropped logged",
          any(e["kind"] == "ack_dropped" for e in sim.alog.events))
    c.close(); sim.alog.close(); sim._close_pty()
    # observed_v3: first ACK slow at grind start, retransmit prompt
    sim = fresh_sim(pty=True, profile="observed_v3")
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 3, [1, 185, 60, 1]))
    fr = c.pump(0.15)
    check("observed_v3: first ACK delayed (>150 ms, like v3's 248 ms)",
          not any(f[0] == 0x86 for f in fr))
    c.send(build_frame(6, 3, [1, 185, 60, 1]))   # retransmit +121 ms
    fr = c.pump(0.14)
    check("observed_v3: retransmit ACK prompt (~18 ms, like v3's 12 ms)",
          any(f[0] == 0x86 for f in fr))
    fr = c.pump(0.2)
    check("observed_v3: first ACK also arrives (~260 ms)",
          sum(1 for f in c.frames if f[0] == 0x86) >= 2)
    c.close(); sim.alog.close(); sim._close_pty()


# ---------------------------------------------------------------- T10: link/garbage
def t10_link():
    print("== T10: offline + garbage-stream + cmd8 ==")
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    sim.inject({"type": "link", "state": "silent"})
    c.send(build_query(0, 56))
    fr = c.pump(0.3)
    check("silent: no reply to polls (offline)",
          not any(f[0] == 0x83 for f in fr))
    sim.inject({"type": "link", "state": "up"})
    c.send(build_query(0, 56))
    fr = c.pump(0.1)
    check("link up: replies again", any(f[0] == 0x83 for f in fr))
    # garbage stream: client must resync
    sim.inject({"type": "garbage", "dur": 0.3, "crc_too": True})
    c.send(build_query(33, 7))
    fr = c.pump(0.5)
    check("garbage: CRC errors counted + valid frame at the end",
          c.parser.crc_bad > 0 and any(f[0] == 0x83 and f[1] == 33 for f in fr))
    # cmd8 echo
    payload = bytes([0xE9, 0x07, 0x0B, 0x11, 0x0E, 0x1E, 0x00, 0x00, 0x01, 0x00])
    c.send(build_power_frame(payload))
    fr = c.pump(0.05)
    check("cmd8 → 0x88 echo (15-byte frame)",
          any(f[0] == 0x88 and f[2] == payload for f in fr))
    # closed → reopen (new pty)
    old_path = sim.slave_path
    sim.inject({"type": "link", "state": "closed"})
    check("closed: pty closed", sim.master is None)
    sim.inject({"type": "link", "state": "up"})
    check("reopen: new pty path", sim.slave_path is not None and sim.slave_path != old_path)
    c.close(); sim.alog.close(); sim._close_pty()


# ---------------------------------------------------------------- T11: replay
def t11_replay():
    print("== T11: REPLAY-verify frame-for-frame (v3/v2/session/steam-live) ==")
    for name, path in [
        ("v3 grind+brew", "sim/fixtures/captures/uart_live_v3.csv.gz"),
        ("v2 hot water", "sim/fixtures/captures/uart_live_v2.csv.gz"),
        ("session sleep/drain", "sim/fixtures/captures/uart_session.csv.gz"),
        ("steam-live (v6.6)", "sim/fixtures/captures/uart_session_live.csv.gz"),
    ]:
        sim = fresh_sim()
        sim.load_replay(str(proto.ROOT / path), mode="verify")
        rep = sim._verify_step()
        sim.alog.close()
        check(f"replay-verify {name}: PASS (match={rep['match_ratio']:.4f}, "
              f"exact_fail={rep['exact_fail']}, ws-sequence={'OK' if rep['work_sta_sequence_ok'] else 'FAIL'}, "
              f"timing={'OK' if rep['work_sta_timing_ok'] else 'FAIL'})",
              rep["pass"], json.dumps({w: s for w, s in rep["per_word"].items()
                                       if s["fail"] > 5})[:300])
        # v6.6: steam-live is the calibration target (≥99 %); the others ≥92 %
        th = 0.99 if "steam-live" in name else 0.92
        check(f"replay-verify {name}: match-ratio ≥{int(th*100)}%",
              rep["match_ratio"] >= th, f"{rep['match_ratio']:.4f}")
        check(f"replay-verify {name}: exact words 100%", rep["exact_fail"] == 0)


# ---------------------------------------------------------------- T12: manual STOP (v6.13)
def t12_manual_stop():
    print("== T12: manual STOP v6.13 (word16/19/22/6 → auto-stop equivalent) ==")
    BREW_START = [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1]
    # --- WATER in AUTO mode: stop on word16=0 (hotwater-ctl 0x1B8C,
    #     active=(word19|word16)≠0 is recomputed every pass — ALSO in auto) ---
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])
    sim.advance(3.0)
    check("water(auto): running ws=0x030B, word16=1",
          sim.regs[33] == 0x030B and sim.regs[16] == 1)
    w0 = s16(sim.regs[39])
    check("water(auto): weight ramp in progress", 100 < w0 < 300)
    sim.handle_write(16, [0])
    sim.advance(sim.t + 0.05)
    check("water(auto): MANUAL stop word16=0 clears bits 1+3+9 immediately (regardless of manual)",
          sim.regs[33] == 0x0101 and sim.act is None)
    check("water(auto): weight stops at CURRENT weight (no snap to target 400)",
          abs(s16(sim.regs[39]) - w0) <= 5 and s16(sim.regs[39]) < 300)
    check("water(auto): stop ACK logged (exact echo sub16=[0])",
          any(e["kind"] == "ack" and e.get("sub") == 16 and e.get("words") == [0]
              for e in sim.alog.events))
    sim.advance(sim.t + 2.5)
    check("water(auto): post/drip like auto-stop (weight −11, flow 0, word16/19=0)",
          s16(sim.regs[39]) <= w0 - 8 and sim.regs[52] == 0
          and sim.regs[16] == 0 and sim.regs[19] == 0)
    check("water(auto): activity_stop reason=manual",
          any(e["kind"] == "activity_stop" and e.get("activity") == "water"
              and "manual" in e.get("reason", "") for e in sim.alog.events))
    # --- WATER manual mode (word19=1): word16=0 alone does NOT stop ---
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 1])
    sim.advance(3.0)
    check("water(manual): starts with word16=0/word19=1",
          sim.regs[33] == 0x030B and sim.act and sim.act["manual"])
    sim.handle_write(16, [0])
    sim.advance(sim.t + 0.1)
    check("water(manual): word16=0 alone does NOT stop (active = word19|word16)",
          sim.regs[33] == 0x030B)
    w0 = s16(sim.regs[39])
    sim.handle_write(19, [0])
    sim.advance(sim.t + 0.05)
    check("water(manual): word19=0 → stop, bits cleared, weight held",
          sim.regs[33] == 0x0101 and abs(s16(sim.regs[39]) - w0) <= 5)
    # --- BREW: stop on word22=0 during extraction (brew-ctl 0x1E9C/0x1F92) ---
    sim = fresh_sim()
    sim.handle_write(5, BREW_START)
    sim.advance(12.0)
    check("brew: extraction ws=0x1307, word22=1",
          sim.regs[33] == 0x1307 and sim.regs[22] == 1)
    w0 = s16(sim.regs[39])
    check("brew: weight ramp in progress", 20 < w0 < 200)
    sim.handle_write(22, [0])
    sim.advance(sim.t + 0.05)
    check("brew: MANUAL stop word22=0 → idle/valve pulse (bits 1+2+9+12 cleared)",
          sim.regs[33] in (0x0101, 0x0105) and sim.act is None)
    sim.advance(sim.t + 0.3)
    check("brew: after valve pulse (~0.21 s) → ws=0x0101", sim.regs[33] == 0x0101)
    sim.advance(sim.t + 2.0)
    check("brew: post-drip +12 like auto-stop (NOT a ramp towards target 360)",
          w0 + 8 <= s16(sim.regs[39]) <= w0 + 16 and s16(sim.regs[39]) < 200)
    check("brew: word22=0, flow 0, reason=manual",
          sim.regs[22] == 0 and sim.regs[52] == 0
          and any(e["kind"] == "activity_stop" and e.get("activity") == "brew"
                  and "manual" in e.get("reason", "") for e in sim.alog.events))
    # --- BREW: stop during pre-infusion ---
    sim = fresh_sim()
    sim.handle_write(5, BREW_START)
    sim.advance(3.0)
    check("brew(pre): pre-infusion ws=0x1107", sim.regs[33] == 0x1107)
    sim.handle_write(22, [0])
    sim.advance(sim.t + 0.4)
    check("brew(pre): manual stop → idle, no extraction",
          sim.regs[33] == 0x0101 and sim.act is None)
    # --- GRIND: stop on word6=0 (grind-ctl 0x19C6; word3 is the tare trigger) ---
    sim = fresh_sim()
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(5.0)
    check("grind: running ws=0x0141, word6=1 (run flag)",
          sim.regs[33] == 0x0141 and sim.regs[6] == 1)
    w0 = s16(sim.regs[38])
    check("grind: weight ramp in progress", 40 < w0 < 150)
    sim.handle_write(6, [0])
    sim.advance(sim.t + 0.05)
    check("grind: MANUAL stop word6=0 clears bit6 immediately",
          sim.regs[33] == 0x0101 and sim.act is None)
    sim.advance(sim.t + 2.0)
    check("grind: the weight HOLDS (no ramp towards target+2=187)",
          abs(s16(sim.regs[38]) - w0) <= 2)
    check("grind: cur2 decay like auto-stop + word6=0 + reason=manual",
          sim.regs[58] < 60 and sim.regs[6] == 0
          and any(e["kind"] == "activity_stop" and e.get("activity") == "grind"
                  and "manual" in e.get("reason", "") for e in sim.alog.events))
    # --- stop words WITHOUT activity: ACKed, no effect ---
    sim = fresh_sim()
    ok = (sim.handle_write(22, [0]) and sim.handle_write(6, [0])
          and sim.handle_write(16, [0]))
    sim.advance(0.5)
    check("idle: stop writes are ACKed and do nothing (no activity_stop)",
          ok and sim.regs[33] == 0x0101
          and not any(e["kind"] == "activity_stop" for e in sim.alog.events))
    # --- word3/word9 are NOT stop words (tare triggers, UI_v6 §18.1) ---
    sim = fresh_sim()
    sim.handle_write(5, BREW_START)
    sim.advance(12.0)
    sim.handle_write(9, [0])
    sim.advance(sim.t + 0.2)
    check("brew: word9=0 during activity does NOT stop (tare trigger)",
          sim.regs[33] == 0x1307)
    sim = fresh_sim()
    sim.handle_write(3, [1, 185, 60, 1])
    sim.advance(5.0)
    sim.handle_write(3, [0])
    sim.advance(sim.t + 0.2)
    check("grind: word3=0 during activity does NOT stop (tare trigger)",
          sim.regs[33] == 0x0141)


# ---------------------------------------------------------------- T13: deep sleep/wake (v6.14)
def t13_power():
    print("== T13: DEEP SLEEP/WAKE v6.14 (word0=0 → silent; button/alarm → reboot + re-push) ==")
    check("power-off frame = aa 55 06 00 02 00 00 00 00 56 94 (stock powerOff)",
          build_frame(6, 0, [0, 0]).hex() == "aa55060002000000005694")
    # --- word0=0 → ACK, then deep sleep ≤0.5 s (regs preserved, outputs 0) ---
    sim = fresh_sim()
    sim.handle_write(11, [880])
    ok = sim.handle_write(0, [0, 0])
    check("sub0 len2 [0,0] ACKed normally (write ok + ack scheduled)",
          ok and any(e["kind"] == "ack" and e.get("sub") == 0
                     and e.get("words") == [0, 0] for e in sim.alog.events))
    check("deep sleep scheduled ≤0.5 s (status task 500 ms), not entered yet",
          sim.dvale_at is not None and sim.power_state == "on")
    sim.advance(sim.t + 0.6)
    check("deep sleep: power_state=off, work_sta=0, mode=off, outputs 0 (62/63)",
          sim.power_state == "off" and sim.regs[33] == 0 and sim.mode == "off"
          and sim.regs[62] == 0 and sim.regs[63] == 0)
    check("deep sleep: register file PRESERVED (word11=880, word0=0)",
          sim.regs[11] == 880 and sim.regs[0] == 0)
    r40 = sim.regs[40]
    sim.advance(sim.t + 10.0)
    check("deep sleep: sensor regs frozen (no status task → no update)",
          sim.regs[40] == r40 and sim.regs[11] == 880)
    check("deep sleep: dvale_enter logged", any(e["kind"] == "dvale_enter"
                                          for e in sim.alog.events))
    # --- silent UART: polls/writes/cmd8 ignored (status task deleted) ---
    nq = sim.stats["queries"]
    sim.handle_frame(3, 0, 56)
    sim.handle_frame(6, 11, [900])
    sim.handle_frame(8, 0, bytes(10))
    check("deep sleep: cmd3/cmd6/cmd8 dropped (no reply, no effect)",
          sim.stats["queries"] == nq and sim.regs[11] == 880
          and sim.stats["dvale_rx_dropped"] == 3)
    # --- activities are stopped SAFELY on deep sleep (no post phase) ---
    sim = fresh_sim()
    sim.handle_write(5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1])
    sim.advance(12.0)
    check("brew running before power-off (ws=0x1307)", sim.regs[33] == 0x1307)
    sim.handle_write(0, [0, 0])
    sim.advance(sim.t + 0.6)
    check("deep sleep during brew: activity stopped safely (act/post=None, ws=0)",
          sim.act is None and sim.post is None and sim.regs[33] == 0)
    w0 = sim.regs[39]                    # at deep-sleep entry (≤0.5 s after write)
    sim.advance(sim.t + 5.0)
    check("deep sleep during brew: weight frozen (no drip/decay — tasks deleted)",
          sim.regs[39] == w0)
    check("deep sleep during brew: activity_stop reason=deep sleep logged",
          any(e["kind"] == "activity_stop" and e.get("activity") == "brew"
              and "deep sleep" in e.get("reason", "") for e in sim.alog.events))
    # --- keep-awake monitoring pauses in deep sleep ---
    sim = fresh_sim()
    sim.first_rx_t = 0.0
    sim.handle_write(1, [0])
    sim.advance(29.0)
    sim.handle_write(0, [0, 0])
    sim.advance(sim.t + 40.0)
    check("keep-awake: no gap log in deep sleep (40 s without word1=0 — paused)",
          sim.power_state == "off"
          and not any(e["kind"] == "keepalive_gap" for e in sim.alog.events))
    # --- wake: button hold <0.8 s does nothing; ≥0.8 s → reboot to factory defaults ---
    sim.inject({"type": "power_key", "value": 1})
    sim.advance(sim.t + 0.5)
    sim.inject({"type": "power_key", "value": 0})
    check("button hold 0.5 s (<0.8 s): NO wake", sim.power_state == "off"
          and sim.stats["reboots"] == 0)
    sim.inject({"type": "power_key", "value": 1})
    sim.advance(sim.t + 1.0)
    sim.inject({"type": "power_key", "value": 0})
    check("button hold 1.0 s (≥0.8 s): wake → CPU reset", sim.power_state == "on"
          and sim.stats["reboots"] == 1 and sim.stats["power_wakes"] == 1)
    check("wake: FACTORY start values (word0=1, word11=920, word114=50, word4=180, word25=550)",
          sim.regs[0] == 1 and sim.regs[11] == 920 and sim.regs[114] == 50
          and sim.regs[4] == 180 and sim.regs[25] == 550)
    check("wake: config NOT preloaded (word112/113=0, word1=0, ws=idle)",
          sim.regs[112] == 0 and sim.regs[113] == 0 and sim.regs[1] == 0
          and sim.regs[33] == 0x0101)
    check("wake: 0x89 round expected (version_received=False, boot_checked=False)",
          not sim.version_received and not sim.boot_checked
          and sim.first_rx_t is None)
    rb = [e for e in sim.alog.events if e["kind"] == "reboot"]
    check("wake: reboot logged w/ factory=True, cause=power_key + defaults snapshot",
          bool(rb) and rb[-1].get("factory") is True
          and rb[-1].get("cause") == "power_key"
          and (rb[-1].get("defaults") or {}).get("11") == 920)
    check("wake: power_wake logged (cause=power_key)",
          any(e["kind"] == "power_wake" and e.get("cause") == "power_key"
              for e in sim.alog.events))
    # --- re-push from host restores config ---
    words = config_to_words(load_baseline_config())
    mirror = [0] * 31
    for wd, v in words.items():
        if 1 <= wd <= 30:
            mirror[wd] = v
    sim.handle_write(1, mirror[1:31])
    sim.handle_write(112, [words[112], words[113], words[114]])
    check("re-push: config restored (word11=870, word114=70, word112=0x0104)",
          sim.regs[11] == 870 and sim.regs[114] == 70 and sim.regs[112] == 0x0104)
    check("re-push: boot_push logged (sub1_len30 + sub112_len3)",
          sim.boot_push_setup and sim.boot_push_preinf)
    # --- missing re-push after wake is detected ---
    sim2 = fresh_sim()
    sim2.inject({"type": "power_off"})
    sim2.inject({"type": "power_on"})
    sim2.handle_frame(3, 0, 56)          # first RX after wake (connect)
    sim2.advance(sim2.t + 7.0)
    check("wake without re-push: boot_push_missing logged",
          any(e["kind"] == "boot_push_missing" for e in sim2.alog.events))
    # --- alarm wake (cmd8's alarm field + RTC) ---
    sim = fresh_sim()
    payload = bytes([0xEA, 0x07, 11, 18, 16, 35, 28, 18, 16, 36])  # alarm 16:36
    sim.handle_frame(8, 0, payload)
    ts = [e for e in sim.alog.events if e["kind"] == "time_sync"]
    check("cmd8: time/alarm parsed (alarm=[18,16,36] logged)",
          bool(ts) and ts[-1].get("alarm") == [18, 16, 36])
    sim.inject({"type": "power_off"})
    sim.advance(sim.t + 40.0)            # RTC 16:35:28 → 16:36:08 → alarm match
    check("alarm wake: RTC=alarm in deep sleep → reboot (cause=alarm)",
          sim.power_state == "on" and sim.stats["power_wakes"] == 1
          and any(e["kind"] == "power_wake" and e.get("cause") == "alarm"
                  for e in sim.alog.events))
    check("alarm wake: factory defaults (word11=920)", sim.regs[11] == 920)
    sim = fresh_sim()
    sim.handle_frame(8, 0, bytes([0xEA, 0x07, 11, 18, 16, 35, 28, 0, 0, 0]))
    sim.inject({"type": "power_off"})
    sim.advance(sim.t + 120.0)
    check("alarm 0/0/0: NO alarm wake (deep sleep for 120 s)",
          sim.power_state == "off" and sim.stats["power_wakes"] == 0)
    # --- button while the MCU is ON: no MCU effect (the display owns power-off) ---
    sim = fresh_sim()
    sim.inject({"type": "power_key", "value": 1})
    sim.advance(sim.t + 2.0)
    sim.inject({"type": "power_key", "value": 0})
    check("button hold while on: no reboot/deep sleep (UI_v6 §19.1)",
          sim.power_state == "on" and sim.stats["reboots"] == 0)
    # --- ctrl conveniences + ping.power ---
    sim = fresh_sim()
    sim.inject({"type": "power_off"})
    ping = sim.ctrl_dispatch({"cmd": "ping"})
    check("ping.power: state=off in deep sleep", ping["power"]["state"] == "off")
    sim.inject({"type": "power_key", "action": "hold", "dur": 1.6})
    sim.advance(sim.t + 1.7)
    check("power_key hold convenience (auto-release): wake",
          sim.power_state == "on" and sim.stats["power_wakes"] == 1)
    ping = sim.ctrl_dispatch({"cmd": "ping"})
    check("ping.power: state=on after wake", ping["power"]["state"] == "on")
    # --- compat: ctrl reboot / word30 still preloads config ---
    sim = fresh_sim()
    sim.ctrl_dispatch({"cmd": "reboot"})
    rb = [e for e in sim.alog.events if e["kind"] == "reboot"]
    check("ctrl reboot (not power): config preloaded as before (word11=870, factory=False)",
          sim.regs[11] == 870 and rb[-1].get("factory") is False)
    # --- pty level: ACK → silence → wake → 0x89 → re-push → replies again ---
    sim = fresh_sim(pty=True)
    c = RawClient(sim)
    c.send(build_version_response("x"))
    c.pump(0.05)
    c.send(build_frame(6, 0, [0, 0]))
    fr = c.pump(0.05)
    check("pty: power-off ACK received BEFORE deep sleep",
          any(f[0] == 0x86 and f[1] == 0 and f[2] == [0, 0] for f in fr))
    c.pump(0.6)
    nq = sim.stats["queries"]
    c.send(build_query(0, 56))
    c.send(build_query(33, 7))
    fr = c.pump(0.3)
    check("pty: deep sleep = silent UART (no 0x83 reply to polls)",
          not any(f[0] == 0x83 for f in fr) and sim.stats["queries"] == nq)
    sim.inject({"type": "power_key", "action": "hold", "dur": 1.0})
    fr = c.pump(1.3)
    check("pty: wake → 0x89 version requests again",
          any(f[0] == 0x89 for f in fr))
    c.send(build_version_response("x"))
    c.pump(0.05)
    words = config_to_words(load_baseline_config())
    mirror = [0] * 31
    for wd, v in words.items():
        if 1 <= wd <= 30:
            mirror[wd] = v
    c.send(build_frame(6, 1, mirror[1:31]))
    c.send(build_frame(6, 112, [words[112], words[113], words[114]]))
    fr = c.pump(0.1)
    check("pty: re-push ACKed after wake",
          any(f[0] == 0x86 and f[1] == 1 and len(f[2]) == 30 for f in fr)
          and any(f[0] == 0x86 and f[1] == 112 and len(f[2]) == 3 for f in fr))
    c.send(build_query(0, 56))
    fr = c.pump(0.1)
    resp = [f for f in fr if f[0] == 0x83 and f[1] == 0]
    check("pty: polls answered again w/ config (word0=1, word11=870)",
          bool(resp) and resp[0][2][0] == 1 and resp[0][2][11] == 870)
    c.close()
    sim.alog.close()
    sim._close_pty()


# ---------------------------------------------------------------- T14: heat stages (v6.18)
def t14_heat_stage():
    print("== T14: boiler heat stages words 62/63 + the boilers event (v6.18, MCU boiler_heat 0x3502) ==")
    sim = fresh_sim()
    sim.advance(0.5)
    check("default (no heat model): awake -> words 62 = 63 = 100 (pre-v6.18 behaviour)",
          sim.regs[62] == 100 and sim.regs[63] == 100 and sim.heat is None)
    r = list(sim.regs)
    r[11], r[14], r[33] = 870, 3, 0x0101
    cases = [((399, 499), (0, 0)), ((400, 500), (30, 30)), ((599, 799), (30, 30)), ((600, 800), (60, 60)),
             ((869, 1199), (60, 60)), ((870, 1200), (100, 100))]
    ok = True
    for (e, s_), want in cases:
        r[40], r[47] = e, s_
        ok &= McuSim.heat_stage(r) == want
    r[40], r[47], r[33] = 500, 600, 0x1107 | 0x20
    ok &= McuSim.heat_stage(r) == (100, 100)          # bit 12 / bit 5 force 100
    for g, thr in ((1, 1100), (2, 1150), (3, 1200), (4, 1250)):
        r[14], r[33], r[47] = g, 0x0101, thr - 1
        ok &= McuSim.heat_stage(r)[1] == 60
        r[47] = thr
        ok &= McuSim.heat_stage(r)[1] == 100
    check("heat_stage = MCU: 40/60 degC, set temp (extract); 50/80 degC, gear target - 10 degC (steam); bits 12/5 force 100", ok)
    sim.inject({"type": "boilers", "e": 550, "s": 700, "e_rate": 30, "s_rate": 24})
    check("boilers event: cold boilers -> stages 30 / 30, model on",
          sim.regs[62] == 30 and sim.regs[63] == 30 and sim.heat is not None)
    sim.advance(sim.t + 12.0)
    e_ready = sim.regs[62] == 100 and sim.regs[63] == 60
    sim.advance(sim.t + 12.0)
    check("heats up: the extract boiler reaches stage 100 first (~11 s), the steam boiler later (~21 s)",
          e_ready and sim.regs[63] == 100)
    sim.inject({"type": "boilers", "model": False})
    sim.advance(sim.t + 0.5)
    check("model off again -> 62 = 63 = 100", sim.heat is None and sim.regs[62] == 100)


def t15_concurrent_brew_steam():
    """v6.20: brew + latched steam both ways round, and the MCU heater rule during the overlap
    (docs/concurrent-brew-steam.md §3.4 / §11)."""
    BREW = [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1]
    # brew started DURING latched steam: the MCU accepts it (brew-ctl does not read steam)
    sim = fresh_sim()
    sim.inject({"type": "milk_jump", "value": 120})
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    sim.advance(sim.t + 1.0)
    ok0 = sim.regs[33] == 0x4121 and sim.mode == "steam"
    sim.handle_write(5, BREW)
    sim.advance(sim.t + 1.0)
    ws = sim.regs[33]
    check("concurrent: brew start DURING latched steam is accepted (brew bits 1+2 on top of 5+14, word24 stays 1)",
          ok0 and (ws & 0x0006) == 0x0006 and (ws & 0x4020) == 0x4020 and sim.regs[24] == 1
          and any(e["kind"] == "activity_start" and e.get("activity") == "brew" for e in sim.alog.events),
          f"ws={ws:#06x}")
    # heater rule: steam latched during the brew -> no steam heater until word47 < target-100, then bit 13
    sim = fresh_sim()
    sim.handle_write(5, BREW)
    sim.advance(sim.t + 3.0)
    sim.inject({"type": "milk_jump", "value": 120})
    sim.handle_write(24, [1, 650, 0, 0, 0, 0, 0, 0])
    tgt = {1: 1200, 2: 1250, 3: 1300}.get(sim.regs[14], 1350)
    first, early, both, cur_ok, w_min = None, False, False, True, 99999
    while sim.act and sim.act["kind"] == "brew" and sim.t < 120:
        sim.advance(sim.t + 0.1)
        ws, w47 = sim.regs[33], sim.regs[47]
        if (ws & 0x4020) != 0x4020 or not (ws & 0x0006) == 0x0006:
            continue
        w_min = min(w_min, w47)
        if ws & 0x2000:
            if first is None:
                first = w47
            if sim.regs[50] < 7000:
                cur_ok = False
        elif first is None and sim.regs[50] > 2000:
            cur_ok = False
        if (ws & 0x2000) and first is None and w47 >= tgt - 100 + 5:
            early = True
        if (ws & 0x0400) and (ws & 0x2000):
            both = True
    check("heater rule: during brew + latched steam the steam heater (bit 13) stays off until the steam boiler is < target-10 °C, then on; never with bit 10",
          first is not None and tgt - 100 - 15 <= first < tgt - 100 + 5 and not early and not both,
          f"target={tgt} first bit13 at word47={first} min={w_min}")
    check("heater rule: cur1 = pump only (~1 k) before, pump + steam heater (~8 k) once bit 13 runs",
          cur_ok)


# ---------------------------------------------------------------- T16: controller version + manual water (v6.22)
def t16_water_dosing():
    print("== T16: controller firmware string words 80..95 + MANUAL hot water (word19 alone) + scale fault (v6.22) ==")
    sim = fresh_sim()
    s = b"".join(bytes((w & 0xFF, w >> 8)) for w in sim.regs[80:88])
    check("baseline: words 80..87 = 'V5.1.4' (lo byte first) and 88..95 = the same copy; dosing by the stock rule",
          s.rstrip(b"\x00") == b"V5.1.4" and sim.regs[80:88] == sim.regs[88:96] and sim.mcu_version_dosing())
    sim.inject({"type": "mcu_version", "version": "V5.1.2"})
    old = sim.regs[80:88] == McuSim._ver_words("V5.1.2") and sim.regs[88:96] == sim.regs[80:88] and not sim.mcu_version_dosing()
    sim.do_reboot()
    check("mcu_version event: V5.1.2 in words 80..95 (not dosing); survives a controller reset",
          old and sim.regs[80:88] == McuSim._ver_words("V5.1.2") and not sim.mcu_version_dosing())
    sim.inject({"type": "mcu_version", "version": "V5.1.4", "copy": "V5.1.3"})
    mm = sim.regs[80:88] != sim.regs[88:96]
    sim.inject({"type": "mcu_version", "version": ""})
    check("mcu_version event: a copy mismatch and an empty string can be injected",
          mm and sim.regs[80:96] == [0] * 16 and not sim.mcu_version_dosing())
    # manual hot water: word19 alone starts, no weight stop, no timeout, word19=0 stops
    sim = fresh_sim()
    sim.inject({"type": "mcu_version", "version": "V5.1.2"})
    sim.handle_write(19, [1])
    sim.advance(sim.t + 1.0)
    st = sim.regs[33] == 0x030B and sim.act and sim.act["manual"]
    sim.advance(sim.t + 200.0)
    check("MANUAL: sub19 len1 word19=1 starts hot water (no word 9); still running after 200 s, past the word17 target (no weight stop, no timeout)",
          st and sim.regs[33] == 0x030B and s16(sim.regs[39]) > sim.regs[17] + 100)
    sim.handle_write(19, [0])
    sim.advance(sim.t + 0.05)
    check("MANUAL: word19=0 stops it (bits cleared)", sim.regs[33] == 0x0101 and sim.act is None)
    check("no legacy_word16 event (no word16=1 was written)",
          not any(e["kind"] == "legacy_word16" for e in sim.alog.events))
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])
    check("a word16=1 write while the controller reports V5.1.2 is logged as legacy_word16",
          any(e["kind"] == "legacy_word16" for e in sim.alog.events))
    # scale fault: stops AUTO (word16=0), not MANUAL
    sim = fresh_sim()
    sim.handle_write(9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0])
    sim.advance(sim.t + 1.5)
    run = sim.regs[33] == 0x030B
    sim.inject({"type": "fault", "e2": 0x1000})
    sim.advance(sim.t + 0.05)
    check("scale fault (word35 0x1000) during AUTO: the controller clears word16 and stops",
          run and sim.regs[16] == 0 and sim.regs[33] & 0x000A == 0
          and any(e["kind"] == "activity_stop" and "scale" in e.get("reason", "") for e in sim.alog.events))
    sim = fresh_sim()
    sim.handle_write(19, [1])
    sim.advance(sim.t + 1.0)
    sim.inject({"type": "fault", "e2": 0x0400})
    sim.advance(sim.t + 2.0)
    man = sim.regs[33] == 0x030B and sim.regs[19] == 1
    sim.inject({"type": "fault", "e2": 0x0001})
    sim.advance(sim.t + 0.05)
    check("scale fault during MANUAL: keeps running; any other E2 bit still safety-stops it",
          man and sim.regs[33] & 0x000A == 0)


def main():
    t0 = time.monotonic()
    for fn in (t1_crc, t2_chunking, t3_write_policy, t4_packing, t5_boot,
               t6_autostop, t7_keepalive, t8_faults, t9_ack, t10_link, t11_replay,
               t12_manual_stop, t13_power, t14_heat_stage, t15_concurrent_brew_steam,
               t16_water_dosing):
        fn()
    dt = time.monotonic() - t0
    print(f"\nSELFTEST: {N_PASS} PASS / {N_FAIL} FAIL ({dt:.1f}s)")
    return 1 if N_FAIL else 0


if __name__ == "__main__":
    sys.exit(main())