#!/usr/bin/env python3
"""run_e2e.py — E2E runner for merakiplus's SIMULATED test environment.

Orchestration per scenario (sim/scenarios/*.json):
  (a) sandbox root: config copy, flag file, /proc mocks, fb buffer, touch FIFO
  (b) mcu_sim on a pty (+ ctrl socket + assertions log)
  (c) merakiplus SIM build if present (CONTRACT.md), otherwise the reference client
      (sim/ref_client.py), which proves the sim
  (d) injects touch/scenario events (evdev FIFO / ctrl sockets)
  (e) samples UI state (JSONL) + fb frames (BGRA → PNG)
  (f) assertions: scenario expectations + circle check + colour/region checks
      + sim-log assertions (keep-awake, trigger policy, band, limits)
  (g) report: JSON + MD + PNG snapshots/contact sheet per scenario

Python stdlib only. No device contact. Usage:
  python3 sim/run_e2e.py --list
  python3 sim/run_e2e.py --scenario brew
  python3 sim/run_e2e.py --all
  python3 sim/run_e2e.py --scenario brew --ui-bin /path/to/merakiplus_v6_sim
  python3 sim/run_e2e.py --all --no-ui-profile   # raw — the scenarios assert merakiplus's
                                                 # guarded behaviour (v6.5-scenarios)
  python3 sim/run_e2e.py --all --ref-client      # reference oracle with REF profile
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
import png
from proto import (W, H, FB_BYTES, REGN, s16, touch_stream, load_baseline_config,
                   load_spec, pack112, pack113)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCEN_DIR = HERE / "scenarios"
REPORTS = HERE / "reports"
SWITCHER_HOST = ROOT / "build" / "switcher_host"
# ./build.sh output first, then the prebuilt SIM binary for this platform (bin/merakiplus_sim-<os>-<arch>)
SIM_BUILD_CANDIDATES = [ROOT / "build" / "merakiplus_sim",
                        ROOT / "bin" / ("merakiplus_sim-%s-%s" % ({"darwin": "macos"}.get(sys.platform, sys.platform),
                                                              {"aarch64": "arm64", "amd64": "x86_64"}.get(
                                                                  os.uname().machine.lower(), os.uname().machine.lower())))]
# v6.4 (CONTRACT.md "ADDENDUM v6.4"): UI profile = overrides between the reference client's
# synthetic layout/behaviour and the real merakiplus. Disabled with --no-ui-profile.
# v6.5-scenarios (RECONCILE.md): the scenarios now assert merakiplus's guarded behaviour
# DIRECTLY. The UI profile is therefore EMPTY (the mechanism is backward compatible), and the
# reference client gets its own REF profile with the inverse overrides (the oracle behaviour).
# The historical v6.4 profile can still be given explicitly with --ui-profile.
UI_PROFILE_DEFAULT = HERE / "sim_ui_profile-ui.json"
REF_PROFILE_DEFAULT = HERE / "sim_ui_profile-ref.json"


def _pmatch(item, pat):
    """All keys in pat must exist in item with the same value (numbers: ±0.02)."""
    for k, v in pat.items():
        if k not in item:
            return False
        w = item[k]
        if isinstance(v, (int, float)) and not isinstance(v, bool) \
                and isinstance(w, (int, float)) and not isinstance(w, bool):
            if abs(v - w) > 0.02:
                return False
        elif v != w:
            return False
    return True


def apply_ui_profile(scen, profile):
    """Return (new scenario dict, applied overrides, errors). The scenario file is not
    touched. Each override must hit EXACTLY the number of expectations/events it
    specifies (default 1) — otherwise an error is reported, so a stale profile cannot hide
    anything. Expectations are replaced 1:1 (the number of checks is unchanged)."""
    out = json.loads(json.dumps(scen))
    applied, errors = [], []
    for ov in (profile.get("scenarios", {}) or {}).get(scen.get("name"), []):
        why = ov.get("why", "")
        if "match_event" in ov:
            hits = [e for e in out.get("events", []) if _pmatch(e, ov["match_event"])]
            if len(hits) != ov.get("count", 1):
                errors.append(f"event-match {ov['match_event']} hit {len(hits)}")
                continue
            for e in hits:
                e.update(ov.get("set", {}))
            applied.append({"type": "event", "match": ov["match_event"],
                            "set": ov.get("set", {}), "why": why})
        elif "match" in ov:
            exp = out.get("expect", [])
            idx = [i for i, e in enumerate(exp) if _pmatch(e, ov["match"])]
            repl = ov.get("replace")
            if len(idx) != ov.get("count", 1) or not isinstance(repl, dict):
                errors.append(f"expect-match {ov['match']} hit {len(idx)}")
                continue
            for i in idx:
                new = dict(repl)
                for k in ov.get("keep", []):          # e.g. the time window from the original
                    if k in exp[i]:
                        new.setdefault(k, exp[i][k])
                new["_ui_profile"] = why
                exp[i] = new
            applied.append({"type": "expect", "match": ov["match"],
                            "replace": repl, "n": len(idx), "why": why})
    return out, applied, errors

# circle check ported from workspace/check_circle.py (same criteria)
CX = CY = 240.0
R_PHYS, R_SAFE = 240.0, 234.0


def circle_check(raw):
    if len(raw) < FB_BYTES:
        return False, {"error": f"too short: {len(raw)}"}
    viol = 0
    maxr = 0.0
    worst = None
    for y in range(H):
        dy = y - CY + 0.5
        base = y * W * 4
        for x in range(W):
            o = base + x * 4
            b, g, r = raw[o], raw[o + 1], raw[o + 2]
            if b == 0 and g == 0 and r == 0:
                continue
            dx = x - CX + 0.5
            rad = (dx * dx + dy * dy) ** 0.5
            if rad > maxr:
                maxr = rad
            if rad > R_PHYS:
                viol += 1
                if worst is None:
                    worst = [x, y, round(rad, 2), [r, g, b]]
    corners = []
    for (x, y) in [(0, 0), (479, 0), (0, 479), (479, 479)]:
        o = (y * W + x) * 4
        corners.append([raw[o + 2], raw[o + 1], raw[o]])
    corner_ok = all(c == [0, 0, 0] for c in corners)
    ok = viol == 0 and corner_ok and maxr <= R_SAFE + 0.71
    return ok, {"violations": viol, "max_radius": round(maxr, 2),
                "corners_black": corner_ok, "worst": worst}


# Serve replays run this many seconds ahead of the UI clock (scenario key replay.lead_s).
# 0.2 s is the centre of the measured passing band (0.1-0.3 s) of all replay scenarios;
# E2E_REPLAY_LEAD_S overrides it for experiments.
# Clock alignment (align_clocks): all scenario times are relative to the UI clock's t_ms=0,
# with these nominal offsets (medians measured on an idle 10-core machine with --parallel 4; the
# actual values ranged up to ~0.2 s there and were ~0.3-0.6 s on the 4-vCPU CI runner):
#   SIM_AT_UI0_NOM  the sim clock's value at UI t=0 (sim starts before the UI)
#   RUNNER_T0_NOM   the runner's event clock starts this long after UI t=0
# E2E_CLOCK_ALIGN=0 restores the old behaviour (each clock starts whenever its process did).
CLOCK_ALIGN = os.environ.get("E2E_CLOCK_ALIGN", "1") != "0"
SIM_AT_UI0_NOM = float(os.environ.get("E2E_SIM_AT_UI0", "0.03"))
RUNNER_T0_NOM = float(os.environ.get("E2E_RUNNER_T0", "0.06"))
REPLAY_LEAD_S = float(os.environ.get("E2E_REPLAY_LEAD_S", "0.2"))


class Ctrl:
    """JSONL client for the sim/ui ctrl unix sockets."""
    def __init__(self, path):
        self.path = str(path)
        self.seq = 0

    def call(self, req, timeout=5.0):
        self.seq += 1
        req = dict(req, id=self.seq)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(self.path)
        s.sendall((json.dumps(req) + "\n").encode())
        f = s.makefile("r")
        line = f.readline()
        s.close()
        if not line:
            raise RuntimeError(f"ctrl got no reply: {req}")
        return json.loads(line)


def find_ui_bin(explicit=None):
    if explicit:
        p = Path(explicit)
        return p if p.exists() else None
    env = os.environ.get("MERAKIPLUS_SIM_BIN")
    if env and Path(env).exists():
        return Path(env)
    for p in SIM_BUILD_CANDIDATES:
        if p.exists():
            return p
    return None


class ScenarioRun:
    def __init__(self, scen, run_dir, ui_bin=None, keep_sandbox=False,
                 ui_lang=None, ui_profile=None):
        self.ui_lang = ui_lang
        self.profile_applied, self.profile_errors = [], []
        if ui_profile:                      # v6.5-scenarios: also the REF profile (reference client)
            scen, self.profile_applied, self.profile_errors = \
                apply_ui_profile(scen, ui_profile)
        self.scen = scen
        self.name = scen["name"]
        self.dir = Path(run_dir) / self.name
        self.dir.mkdir(parents=True, exist_ok=True)
        self.sandbox = self.dir / "sandbox"
        self.snaps_dir = self.dir / "snapshots"
        self.snaps_dir.mkdir(exist_ok=True)
        self.ui_bin = ui_bin
        self.keep_sandbox = keep_sandbox
        self.samples = []          # (t, state)
        self.cfg_samples = []      # (t, cfg dict)
        self.flag_samples = []     # (t, bool)
        self.snapshots = []        # {t, label, bin, png, raw}
        self.checks = []
        self.gesture_results = {}
        self.replay_truth_cache = {}
        self.sim_proc = self.ui_proc = None
        self.sim_ctrl = self.ui_ctrl = None
        self.pty_path = None
        self.state_path = self.dir / "state.jsonl"
        self.fb_path = self.sandbox / "fb.bin"
        self.assert_path = self.dir / "sim_assertions.jsonl"
        self.sim_log = []
        self.ui_exit = None
        self.client_kind = "reference"
        self.t0 = None
        self.ui_origin = None      # monotonic time of the UI clock's t_ms=0
        self.clock = {}            # measured clock offsets (see align_clocks)
        # AF_UNIX paths must be ≤104 bytes (macOS) — the scratch path is too long,
        # so ctrl sockets are placed in a short /tmp directory per run.
        import tempfile
        self.sock_dir = Path(tempfile.mkdtemp(prefix=f"merakiplus_e2e_{os.getpid()}_",
                                              dir="/tmp"))
        self.sim_ctrl_path = self.sock_dir / f"{self.name[:24]}_sim.ctrl"
        self.ui_ctrl_path = self.sock_dir / f"{self.name[:24]}_ui.ctrl"

    # ------------------------------------------------------------ (a) sandbox
    def make_sandbox(self):
        sb = self.sandbox
        (sb / "customer").mkdir(parents=True, exist_ok=True)
        (sb / "new_customer" / "merakiplus" / "boot").mkdir(parents=True, exist_ok=True)
        (sb / "tmp").mkdir(exist_ok=True)
        (sb / "proc").mkdir(exist_ok=True)
        (sb / "dev" / "input").mkdir(parents=True, exist_ok=True)
        (sb / "sys" / "class" / "pwm" / "pwmchip0" / "pwm2").mkdir(parents=True, exist_ok=True)
        # config copy + scenario overrides
        cfg = load_baseline_config()
        cfg.update(self.scen.get("config", {}))
        self.cfg_path = sb / "customer" / "config.json"
        self.cfg_path.write_text(json.dumps(cfg, indent="\t", ensure_ascii=False) + "\n")
        # boot flag (scenario may start with B boot)
        self.flag_path = sb / "customer" / "new"
        if self.scen.get("boot_flag_initial"):
            self.flag_path.touch()
        # /proc mocks (enough for merakiplus's slot/exe checks)
        (sb / "proc" / "uptime").write_text("12345.67 8901.23\n")
        (sb / "proc" / "meminfo").write_text("MemTotal: 128000 kB\nMemFree: 41000 kB\n")
        (sb / "proc" / "version").write_text("Linux version 4.9.84 (mock) #1 SMP\n")
        (sb / "sys" / "class" / "pwm" / "pwmchip0" / "pwm2" / "duty_cycle").write_text("37\n")
        # v6.3/v6.4: merakiplus's UI language lives in its own file; in the sandbox (no file = English)
        self.lang_path = sb / "new_customer" / "merakiplus" / "lang"
        if self.ui_lang:
            self.lang_path.write_text(self.ui_lang)
        # v6.18 (additive): files a scenario wants in the sandbox before the UI starts, e.g.
        # {"new_customer/merakiplus/sound": "sound=off\n", "proc/uptime": "21.5 3.0\n"}. Paths stay inside the sandbox.
        for rel, text in (self.scen.get("sandbox_files") or {}).items():
            dst = (sb / rel).resolve()
            if not str(dst).startswith(str(sb.resolve()) + os.sep):
                raise RuntimeError(f"sandbox_files: {rel} leaves the sandbox")
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(text)
        # fb buffer + touch FIFO are created by client/sim; placeholder:
        self.fb_path.write_bytes(bytes(FB_BYTES))
        self.touch_path = sb / "dev" / "input" / "event0.fifo"
        # fresh state file
        self.state_path.write_text("")
        self.assert_path.write_text("")

    # ------------------------------------------------------------ (b) sim
    def start_sim(self):
        simcfg = self.scen.get("sim", {})
        cmd = [sys.executable, str(HERE / "mcu_sim.py"),
               "--config", str(self.cfg_path),
               "--pty-file", str(self.dir / "pty_path"),
               "--ctrl", str(self.sim_ctrl_path),
               "--assert-log", str(self.assert_path),
               "--profile", simcfg.get("profile", "normal"),
               "--seed", str(simcfg.get("seed", 1)),
               "--range-policy", simcfg.get("range_policy", "log"),
               "--trigger-policy", simcfg.get("trigger_policy", "reject"),
               "--quiet"]
        if simcfg.get("require_wand") is False:
            cmd.append("--no-require-wand")
        rp = self.scen.get("replay")
        if rp:
            cmd += ["--replay", str(ROOT / rp["csv"]),
                    "--replay-mode", rp.get("mode", "serve"),
                    "--speed", str(rp.get("speed", 1.0))]
            if rp.get("mode", "serve") == "serve":
                cmd.append("--replay-hold")       # started by anchor_replay() once the UI runs
        pf = self.dir / "pty_path"
        pf.unlink(missing_ok=True)      # old file from a previous run in the same directory
        self.sim_proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                         stderr=subprocess.PIPE, text=True)
        for _ in range(200):
            if pf.exists() and pf.read_text().strip():
                break
            if self.sim_proc.poll() is not None:
                raise RuntimeError("mcu_sim died at startup: "
                                   + (self.sim_proc.stderr.read() or ""))
            time.sleep(0.05)
        self.pty_path = pf.read_text().strip()
        self.sim_ctrl = Ctrl(self.sim_ctrl_path)
        # the sim writes pty_path before it binds the ctrl socket: on a cold, busy runner the
        # first ping could hit a socket that does not exist yet (v6.16 full-E2E: 4 runner errors)
        for _ in range(100):
            try:
                self.sim_ctrl.call({"cmd": "ping"})
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if self.sim_proc.poll() is not None:
                    raise RuntimeError("mcu_sim died at startup: " + (self.sim_proc.stderr.read() or ""))
                time.sleep(0.05)
        else:
            self.sim_ctrl.call({"cmd": "ping"})
        # v6.18 (additive): sim events applied BEFORE the UI starts (e.g. cold boilers for the audio scenarios)
        for ev in simcfg.get("init_events") or []:
            self.sim_ctrl.call({"cmd": "inject", "event": ev})

    # ------------------------------------------------------------ (c) UI client
    def start_ui(self):
        env = dict(os.environ)
        env.update({
            "MERAKIPLUS_SIM": "1",
            "MERAKIPLUS_SIM_ROOT": str(self.sandbox),
            "MERAKIPLUS_ROOT": str(self.sandbox),
            "MERAKIPLUS_SIM_TTY": self.pty_path,
            "MERAKIPLUS_SIM_STATE": str(self.state_path),
            "MERAKIPLUS_SIM_FB": str(self.fb_path),
            "MERAKIPLUS_SIM_TOUCH": str(self.touch_path),
            "MERAKIPLUS_SIM_CTRL": str(self.ui_ctrl_path),
            "MERAKIPLUS_CONFIG": str(self.cfg_path),
            "MERAKIPLUS_BAKDIR": str(self.sandbox / "customer" / "bak"),
            "MERAKIPLUS_LANGFILE": str(self.lang_path),
            # v6.3+: UI language file ("en"/"da") outside the /new_customer default;
            # missing file = fallback EN (unchanged behaviour for older builds)
            "MERAKIPLUS_LANGFILE": str(self.sandbox / "new_customer" / "merakiplus" / "lang"),
            "MERAKIPLUS_NOGRAB": "1",
            "MERAKIPLUS_NOBL": "1",
            "MERAKIPLUS_SWITCH_DRY": "1",
        })
        # v6.16 (additive): a scenario may set extra UI environment variables, e.g.
        # MERAKIPLUS_SIM_CLOCK (the UI's UTC wall clock). Not used by older scenarios.
        # (additive, OTA phase 3) "{sim}" in a value = this directory (e.g. a helper under sim/fixtures/)
        env.update({str(k): str(v).replace("{sim}", str(HERE)) for k, v in (self.scen.get("ui_env") or {}).items()})
        if self.ui_bin:
            self.client_kind = "sim-build"
            cmd = [str(self.ui_bin)]
        else:
            self.client_kind = "reference"
            cmd = [sys.executable, str(HERE / "ref_client.py")]
        m_spawn = time.monotonic()
        self.ui_proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE, text=True)
        self.ui_ctrl = Ctrl(self.ui_ctrl_path)
        # wait for the first state line (contract: state per frame)
        for _ in range(200):
            if self.state_path.exists() and self.state_path.stat().st_size > 0:
                break
            if self.ui_proc.poll() is not None:
                raise RuntimeError("UI client died at startup: "
                                   + (self.ui_proc.stderr.read() or ""))
            time.sleep(0.05)
        m_a = time.monotonic()
        resp = self.ui_ctrl.call({"cmd": "ping"})
        m_b = time.monotonic()
        # UI clock origin (its t_ms=0) on our monotonic clock; fall back to the spawn time
        ui_t = resp.get("t") if isinstance(resp, dict) else None
        origin = (m_a + m_b) / 2 - float(ui_t) if isinstance(ui_t, (int, float)) else m_spawn
        self.ui_origin = origin
        # sim clock origin likewise (its t=0 is the sim's process start)
        try:
            m_c = time.monotonic()
            sresp = self.sim_ctrl.call({"cmd": "ping"})
            m_d = time.monotonic()
            if isinstance(sresp.get("t"), (int, float)):
                self.clock["sim_at_ui0"] = round(origin - ((m_c + m_d) / 2 - sresp["t"]), 4)
        except Exception:
            pass
        rp = self.scen.get("replay")
        if rp and rp.get("mode", "serve") == "serve":
            self.anchor_replay(origin, float(rp.get("lead_s", REPLAY_LEAD_S)))

    def anchor_replay(self, ui_origin, lead_s):
        """Serve replays: start the capture clock relative to the UI's clock origin instead of
        the sim's process start. Assertion times are UI timestamps, and the gap between sim
        start and UI start varies with machine load (0.3-0.6 s on a busy CI runner, i.e. 2-5 s
        of capture time at 8x), which made the tight replay windows flaky."""
        elapsed = time.monotonic() - ui_origin + lead_s
        self.replay_anchor = self.sim_ctrl.call({"cmd": "replay_anchor", "elapsed": elapsed})

    # ------------------------------------------------------------ (d) events
    def fire_event(self, ev):
        target = ev.get("target", "ui")
        if target == "sim":
            if "event" in ev:
                self.sim_ctrl.call({"cmd": "inject", "event": ev["event"]})
            else:
                self.sim_ctrl.call({k: v for k, v in ev.items()
                                    if k not in ("t", "target")})
        elif target == "ui":
            req = {k: v for k, v in ev.items() if k not in ("t", "target")}
            if req.get("cmd") == "touch" and "layout" in req:
                # v6.5-scenarios: the tap target is looked up in the client's layout hook
                # (CONTRACT ADDENDUM v6.4 B) instead of synthetic coordinates.
                # The SIM build returns merakiplus's real geometry; the reference
                # client returns its own reference layout.
                name = req.pop("layout")
                resp = self.ui_ctrl.call({"cmd": "layout"})
                tgt = next((t for t in resp.get("targets", [])
                            if t.get("name") == name), None)
                if tgt is None:
                    raise RuntimeError(f"layout target '{name}' does not exist "
                                       f"(page={resp.get('page')})")
                req["x"], req["y"] = int(tgt["x"]), int(tgt["y"])
            self.ui_ctrl.call(req)
        elif target == "runner":
            self.runner_op(ev)

    def runner_op(self, ev):
        op = ev.get("op")
        if op == "snapshot":
            self.take_snapshot(ev.get("label", f"t{ev.get('t', 0):.0f}"), force=True)
        elif op == "config_set":
            cfg = json.loads(self.cfg_path.read_text())
            cfg[ev["field"]] = ev["value"]
            tmp = self.cfg_path.with_suffix(".tmp")
            tmp.write_text(json.dumps(cfg, indent="\t", ensure_ascii=False) + "\n")
            os.replace(str(tmp), str(self.cfg_path))
            if ev.get("reload"):
                self.ui_ctrl.call({"cmd": "config_reload"})
        elif op == "flag":
            if ev.get("value"):
                self.flag_path.touch()
            else:
                self.flag_path.unlink(missing_ok=True)
        elif op == "gesture":
            self.run_gesture(ev)
        elif op == "touch_stream":
            stream = touch_stream(ev.get("presses", 1), ev.get("x", 240),
                                  ev.get("y", 240), ev.get("hold_ms", 80),
                                  ev.get("gap_ms", 300),
                                  t0_ms=int(ev.get("t", 0) * 1000) + 100000)
            with open(self.touch_path, "wb") as f:
                f.write(stream)

    def run_gesture(self, ev):
        """Run the REAL switcher daemon (host build) in file mode against a
        synthetic evdev stream (same format as tinj_hold)."""
        name = ev.get("name", "gesture")
        presses = ev.get("presses", 3)
        stream = touch_stream(presses, ev.get("x", 75), ev.get("y", 75),
                              ev.get("hold_ms", 1600), ev.get("gap_ms", 300))
        gdir = self.dir / "gesture"
        gdir.mkdir(exist_ok=True)
        sf = gdir / f"{name}.evdev"
        lf = gdir / f"{name}.switcher.log"
        kf = gdir / f"{name}.lock"
        sf.write_bytes(stream)
        lf.unlink(missing_ok=True)
        kf.unlink(missing_ok=True)
        result = {"ran": False, "trigger": False, "log": ""}
        if SWITCHER_HOST.exists():
            env = dict(os.environ, SWITCHER_INPUT_FILE=str(sf),
                       SWITCHER_LOG=str(lf), SWITCHER_LOCK=str(kf),
                       SWITCHER_GESTURE=ev.get("gesture", "corner"),
                       SWITCHER_RAW_MAX="4095")
            try:
                r = subprocess.run([str(SWITCHER_HOST)], env=env,
                                   capture_output=True, text=True, timeout=30)
                log = lf.read_text() if lf.exists() else ""
                result = {"ran": True, "rc": r.returncode,
                          "trigger": "GESTURE TRIGGERED" in log, "log": log}
            except subprocess.TimeoutExpired:
                result = {"ran": True, "trigger": False, "log": "TIMEOUT"}
        self.gesture_results[name] = result
        if ev.get("inject_ui"):
            with open(self.touch_path, "wb") as f:
                f.write(stream)

    # ------------------------------------------------------------ (e) sampling
    def read_states(self):
        try:
            with open(self.state_path) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        st = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    t = st.get("t_ms", 0) / 1000.0
                    if not self.samples or t > self.samples[-1][0] or \
                            st.get("client") != self.samples[-1][1].get("client"):
                        self.samples.append((t, st))
        except OSError:
            pass

    def take_snapshot(self, label, force=False):
        try:
            raw = self.fb_path.read_bytes()
        except OSError:
            return
        if len(raw) < FB_BYTES:
            return
        idx = len(self.snapshots)
        binp = self.snaps_dir / f"{idx:02d}_{label}.bin"
        binp.write_bytes(raw)
        self.snapshots.append({"t": round(time.monotonic() - self.t0, 2),
                               "label": label, "bin": str(binp), "raw": raw})

    def sample_loop(self):
        duration = float(self.scen.get("duration_s", 20))
        events = sorted(self.scen.get("events", []), key=lambda e: e.get("t", 0))
        ei = 0
        last_cfg = last_snap = -1e9
        now_m = time.monotonic()
        if self.ui_origin is not None:
            self.clock["runner_t0_after_ui0"] = round(now_m - self.ui_origin, 4)
        # align_clocks: event times count from a FIXED point of the UI clock, not from whenever
        # startup happened to finish (startup takes longer on a loaded machine)
        self.t0 = (self.ui_origin + RUNNER_T0_NOM
                   if CLOCK_ALIGN and self.ui_origin is not None else now_m)
        while True:
            now = time.monotonic() - self.t0
            if now >= duration:
                break
            if self.ui_proc.poll() is not None:
                break
            while ei < len(events) and events[ei].get("t", 0) <= now:
                try:
                    self.fire_event(events[ei])
                except Exception as e:
                    self.checks.append({"name": f"event@{events[ei].get('t')}",
                                        "ok": False, "detail": str(e)})
                ei += 1
            self.read_states()
            if now - last_cfg >= 0.5:
                last_cfg = now
                try:
                    self.cfg_samples.append((now, json.loads(self.cfg_path.read_text())))
                except (OSError, json.JSONDecodeError):
                    pass
                self.flag_samples.append((now, self.flag_path.exists()))
            if now - last_snap >= 2.5:
                last_snap = now
                self.take_snapshot(f"auto{now:.0f}s")
            time.sleep(0.02)
        # final sampling + exit
        self.read_states()
        self.take_snapshot("final", force=True)
        try:
            self.ui_ctrl.call({"cmd": "exit", "code": 0}, timeout=2.0)
        except Exception:
            pass
        try:
            self.ui_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.ui_proc.terminate()
            self.ui_proc.wait(timeout=3)
        self.ui_exit = self.ui_proc.returncode
        # prefetch replay ground-truth windows WHILE the sim is still running
        for ex in self.scen.get("expect", []):
            if ex.get("kind") == "replay_truth":
                key = (ex["word"], ex["t0"], ex["t1"], ex.get("signed", False))
                try:
                    self.replay_truth_cache[key] = self.sim_ctrl.call(
                        {"cmd": "replay_window", "word": ex["word"],
                         "t0": ex["t0"], "t1": ex["t1"],
                         "signed": ex.get("signed", False)})
                except Exception as e:
                    self.replay_truth_cache[key] = {"ok": False, "error": str(e)}
        try:
            self.sim_ctrl.call({"cmd": "exit"}, timeout=2.0)
        except Exception:
            pass
        try:
            self.sim_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.sim_proc.terminate()
        self.read_states()
        try:
            self.sim_log = [json.loads(l) for l in
                            self.assert_path.read_text().splitlines() if l.strip()]
        except OSError:
            self.sim_log = []
        self.align_clocks()

    def align_clocks(self):
        """Sim-log times count from the sim's process start, which precedes the UI start by a
        load-dependent gap (a few 10 ms idle, 0.3-0.6 s on a busy CI runner). Re-base them so the sim
        clock reads SIM_AT_UI0_NOM at the UI's t_ms=0 — the offset the scenario windows were
        tuned with — instead of whatever the gap happened to be in this run."""
        act = self.clock.get("sim_at_ui0")
        if not CLOCK_ALIGN or act is None:
            return
        delta = SIM_AT_UI0_NOM - act
        self.clock["sim_log_shift"] = round(delta, 4)
        for e in self.sim_log:
            if isinstance(e.get("t"), (int, float)):
                e["t"] = round(e["t"] + delta, 4)

    # ------------------------------------------------------------ (f) assertions
    def add(self, name, ok, detail=""):
        self.checks.append({"name": name, "ok": bool(ok), "detail": detail})

    def states_in(self, t0, t1):
        return [st for (t, st) in self.samples if t0 <= t <= t1]

    def nearest_state(self, t, max_dist=1.5):
        best, bd = None, 1e9
        for (ts, st) in self.samples:
            d = abs(ts - t)
            if d < bd:
                best, bd = st, d
        return best if bd <= max_dist else None

    def eval_expect(self, ex):
        kind = ex.get("kind")
        win = ex.get("window", 0.75)
        if kind == "phase":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("phase") == ex["value"] for s in sts)
            self.add(f"phase={ex['value']}@{ex['t']}", ok,
                     f"seen={[s.get('phase') for s in sts][:8]}")
        elif kind == "phase_hold":
            sts = self.states_in(ex["t0"], ex["t1"])
            hits = sum(1 for s in sts if s.get("phase") == ex["value"])
            ratio = hits / len(sts) if sts else 0.0
            ok = bool(sts) and ratio >= ex.get("min_ratio", 0.9)
            self.add(f"phase_hold={ex['value']}[{ex['t0']},{ex['t1']}]", ok,
                     f"ratio={ratio:.2f} n={len(sts)}")
        elif kind == "phase_seq":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            seq = []
            for s in sts:
                p = s.get("phase")
                if not seq or seq[-1] != p:
                    seq.append(p)
            want = ex["values"]
            i = 0
            for p in seq:
                if i < len(want) and p == want[i]:
                    i += 1
            self.add(f"phase_seq={want}", i == len(want), f"seen={seq}")
        elif kind == "word":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            mask = int(ex.get("mask", 0xFFFF), 0) if isinstance(ex.get("mask"), str) \
                else ex.get("mask", 0xFFFF)
            val = int(ex["value"], 0) if isinstance(ex["value"], str) else ex["value"]
            w = str(ex["word"])
            ok = any((int(s.get("regs", {}).get(w, 0)) & mask) == (val & mask)
                     for s in sts)
            self.add(f"word{w}={val:#x}@{ex['t']}", ok,
                     f"seen={[s.get('regs', {}).get(w) for s in sts][:8]}")
        elif kind == "word_hold":
            sts = self.states_in(ex["t0"], ex["t1"])
            w = str(ex["word"])
            vals = []
            for s in sts:
                v = s.get("regs", {}).get(w)
                if v is None:
                    continue
                v = int(v)
                vals.append(s16(v) if ex.get("signed") else v)
            ok = bool(vals) and all(ex["min"] <= v <= ex["max"] for v in vals)
            self.add(f"word{w}∈[{ex['min']},{ex['max']}][{ex['t0']},{ex['t1']}]",
                     ok, f"n={len(vals)} min={min(vals) if vals else '-'} "
                     f"max={max(vals) if vals else '-'}")
        elif kind == "word_min_seen":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            w = str(ex["word"])
            vals = [s16(int(s["regs"][w])) if ex.get("signed") else int(s["regs"][w])
                    for s in sts if w in s.get("regs", {})]
            ok = bool(vals) and max(vals) >= ex["min"]
            self.add(f"word{w}≥{ex['min']} seen in [{ex.get('t0', 0)},{ex.get('t1', 1e9)}]",
                     ok, f"max={max(vals) if vals else '-'}")
        elif kind == "work_sta_seen":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            mask = int(ex.get("mask", "0x2C00"), 0)
            val = int(ex["value"], 0)
            ok = any((int(s.get("work_sta", 0)) & ~mask) == val for s in sts)
            self.add(f"work_sta={val:#x} seen in [{ex.get('t0', 0)},{ex.get('t1', 1e9)}]",
                     ok, f"seen={sorted({hex(int(s.get('work_sta', 0)) & ~mask) for s in sts})}")
        elif kind == "stop_ok":
            # v6.13: state.stop.ok (merakiplus + reference client) — stop confirmation
            # once the MCU has cleared the activity's bits (not just ACK).
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            oks = [int((s.get("stop") or {}).get("ok", 0)) for s in sts]
            fails = [int((s.get("stop") or {}).get("fail", 0)) for s in sts]
            ok = bool(oks) and max(oks) >= ex.get("min", 1)
            if "max_fail" in ex:
                ok = ok and max(fails) <= ex["max_fail"]
            self.add(f"stop.ok≥{ex.get('min', 1)} in [{ex.get('t0', 0)},{ex.get('t1', 1e9)}]",
                     ok, f"ok={max(oks) if oks else '-'} fail={max(fails) if fails else '-'}")
        elif kind == "power":
            # v6.14: state.power.state (on|turning_off|off|turning_on)
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any((s.get("power") or {}).get("state") == ex["value"] for s in sts)
            self.add(f"power={ex['value']}@{ex['t']}", ok,
                     f"seen={[(s.get('power') or {}).get('state') for s in sts][:8]}")
        elif kind == "power_hold":
            sts = self.states_in(ex["t0"], ex["t1"])
            hits = sum(1 for s in sts
                       if (s.get("power") or {}).get("state") == ex["value"])
            ratio = hits / len(sts) if sts else 0.0
            ok = bool(sts) and ratio >= ex.get("min_ratio", 0.9)
            self.add(f"power_hold={ex['value']}[{ex['t0']},{ex['t1']}]", ok,
                     f"ratio={ratio:.2f} n={len(sts)}")
        elif kind == "power_counter":
            # v6.14: state.power counter (offs/ons/blocked/fails/ack8/auto_on/…)
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            vals = [int((s.get("power") or {}).get(ex["field"], 0)) for s in sts]
            ok = bool(vals) and max(vals) >= ex.get("min", 1)
            self.add(f"power.{ex['field']}≥{ex.get('min', 1)}", ok,
                     f"max={max(vals) if vals else '-'}")
        elif kind == "chart":
            # v6.15: state.chart.<field> (merakiplus SIM build: the HOME graph — buf, resets, reanchors,
            # w_max, w_top, top, mode …) in [t0,t1]. "all" (default): every sample with the field
            # must be within [min,max]; "any": at least one. The reference client has no chart
            # (the REF profile replaces these 1:1).
            sts = self.states_in(ex["t0"], ex["t1"])
            f = ex["field"]
            vals = [s["chart"][f] for s in sts
                    if isinstance(s.get("chart"), dict) and f in s["chart"]]
            lo, hi = ex.get("min", -1e18), ex.get("max", 1e18)
            inr = [lo <= v <= hi for v in vals]
            ok = bool(vals) and (any(inr) if ex.get("mode") == "any" else all(inr))
            self.add(f"chart.{f}∈[{ex.get('min', '-')},{ex.get('max', '-')}]"
                     f"{'(any)' if ex.get('mode') == 'any' else ''}[{ex['t0']},{ex['t1']}]",
                     ok, f"n={len(vals)} min={min(vals) if vals else '-'} "
                     f"max={max(vals) if vals else '-'}")
        elif kind == "state":
            # v6.16 (additive): a generic state field by dotted path (e.g. "clock.shown",
            # "sched.armed"). Window: [t-window, t+window] or [t0, t1]. Tests: "equals",
            # "contains" (substring), "min"/"max". mode "any" (default) / "all".
            if "t0" in ex:
                sts = self.states_in(ex["t0"], ex["t1"])
                where = f"[{ex['t0']},{ex['t1']}]"
            else:
                sts = self.states_in(ex["t"] - win, ex["t"] + win)
                where = f"@{ex['t']}"
            vals = []
            for st in sts:
                v, okp = st, True
                for part in ex["path"].split("."):
                    if isinstance(v, dict) and part in v:
                        v = v[part]
                    else:
                        okp = False
                        break
                if okp:
                    vals.append(v)

            def _one(v):
                if "equals" in ex and v != ex["equals"]:
                    return False
                if "contains" in ex and ex["contains"] not in str(v):
                    return False
                if "not_contains" in ex and ex["not_contains"] in str(v):
                    return False
                if "min" in ex and not (isinstance(v, (int, float)) and v >= ex["min"]):
                    return False
                if "max" in ex and not (isinstance(v, (int, float)) and v <= ex["max"]):
                    return False
                return True
            res = [_one(v) for v in vals]
            ok = bool(vals) and (all(res) if ex.get("mode") == "all" else any(res))
            test = ", ".join(f"{k}={json.dumps(ex[k], ensure_ascii=False)}"
                             for k in ("equals", "contains", "not_contains", "min", "max") if k in ex)
            self.add(f"state.{ex['path']} {test}{'(all)' if ex.get('mode') == 'all' else ''}{where}",
                     ok, f"n={len(vals)} last={json.dumps(vals[-1], ensure_ascii=False) if vals else '-'}")
        elif kind == "page":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("page") == ex["value"] for s in sts)
            self.add(f"page={ex['value']}@{ex['t']}", ok)
        elif kind == "view":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("view") == ex["value"] for s in sts)
            self.add(f"view={ex['value']}@{ex['t']}", ok)
        elif kind == "toast":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(ex["contains"] in (s.get("toast") or {}).get("text", "")
                     for s in sts)
            self.add(f"toast~'{ex['contains']}'@{ex['t']}", ok)
        elif kind == "lang":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(s.get("lang") == ex["value"] for s in sts)
            self.add(f"lang={ex['value']}@{ex['t']}", ok)
        elif kind == "config":
            # last sample AT OR BEFORE the expectation time (avoid the next
            # save's ACK overwriting the value in a closer sample)
            best = None
            for (t, cfg) in self.cfg_samples:
                if t <= ex["t"] + 0.05:
                    best = cfg
            ok = best is not None and best.get(ex["field"]) == ex["value"]
            self.add(f"config.{ex['field']}={ex['value']}@{ex['t']}", ok,
                     f"found={best.get(ex['field']) if best else None}")
        elif kind == "flag":
            best = None
            for (t, fl) in self.flag_samples:
                if t <= ex["t"] + 0.05:
                    best = fl
            ok = best is not None and best == ex["exists"]
            self.add(f"boot-flag exists={ex['exists']}@{ex['t']}", ok)
        elif kind == "save":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any((s.get("save") or {}).get("state") == ex["state"] and
                     (ex.get("param") is None or
                      (s.get("save") or {}).get("param") == ex["param"])
                     for s in sts)
            self.add(f"save.{ex['state']}({ex.get('param', '')})@{ex['t']}", ok)
        elif kind == "save_rejected":
            sts = self.states_in(ex["t"] - win, ex["t"] + win)
            ok = any(("AFVIST" in (s.get("toast") or {}).get("text", "") or
                      "LÅST" in (s.get("toast") or {}).get("text", "")) for s in sts)
            self.add(f"save rejected ({ex.get('param', '')})@{ex['t']}", ok)
        elif kind == "exit_code":
            self.add(f"exit_code={ex['value']}", self.ui_exit == ex["value"],
                     f"actual={self.ui_exit}")
        elif kind == "crc_bad_min":
            last = self.samples[-1][1] if self.samples else {}
            ok = last.get("crc_bad", 0) >= ex["value"]
            self.add(f"crc_bad≥{ex['value']}", ok, f"actual={last.get('crc_bad')}")
        elif kind == "offline_seen":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            ok = any(s.get("phase") == "OFFLINE" or not s.get("link_up", True)
                     for s in sts)
            self.add("offline seen", ok)
        elif kind == "link_up_at":
            st = self.nearest_state(ex["t"])
            ok = st is not None and st.get("link_up", False)
            self.add(f"link up@{ex['t']}", ok)
        elif kind == "config_oor_min":
            sts = self.states_in(ex.get("t0", 0), ex.get("t1", 1e9))
            ok = any(len(s.get("config_oor", [])) >= ex.get("min", 1) for s in sts)
            self.add(f"config_oor≥{ex.get('min', 1)}", ok)
        elif kind == "cmd6":
            last = self.samples[-1][1] if self.samples else {}
            c6 = last.get("cmd6", {})
            field, op, val = ex["field"], ex.get("op", ">="), ex["value"]
            have = c6.get(field, 0)
            ok = {">=": have >= val, "==": have == val, "<=": have <= val}[op]
            self.add(f"cmd6.{field}{op}{val}", ok, f"actual={have}")
        elif kind == "sim_log":
            self.eval_sim_log(ex)
        elif kind == "circle":
            bad = []
            for sn in self.snapshots:
                ok, det = circle_check(sn["raw"])
                if not ok:
                    bad.append({"label": sn["label"], **det})
            self.add(f"circle check ({len(self.snapshots)} snapshots)",
                     not bad and bool(self.snapshots), json.dumps(bad[:3]))
        elif kind == "region":
            sn = self.nearest_snapshot(ex["t"])
            if sn is None:
                self.add(f"region@{ex['t']}", False, "no snapshot")
                return
            x, y, rw, rh = ex["rect"]
            raw = sn["raw"]
            rs = gs = bs = n = 0
            for yy in range(y, min(y + rh, H)):
                for xx in range(x, min(x + rw, W)):
                    o = (yy * W + xx) * 4
                    bs += raw[o]; gs += raw[o + 1]; rs += raw[o + 2]; n += 1
            mean = [rs / n, gs / n, bs / n]
            tol = ex.get("tol", 40)
            ok = all(abs(mean[i] - ex["color"][i]) <= tol for i in range(3))
            self.add(f"region{ex['rect']}~{ex['color']}@{ex['t']}", ok,
                     f"mean={[round(v, 1) for v in mean]}")
        elif kind == "point":
            sn = self.nearest_snapshot(ex["t"])
            if sn is None:
                self.add(f"point@{ex['t']}", False, "no snapshot")
                return
            x, y = ex["point"]
            o = (y * W + x) * 4
            raw = sn["raw"]
            got = [raw[o + 2], raw[o + 1], raw[o]]
            tol = ex.get("tol", 30)
            ok = all(abs(got[i] - ex["color"][i]) <= tol for i in range(3))
            self.add(f"point({x},{y})~{ex['color']}@{ex['t']}", ok, f"got={got}")
        elif kind == "replay_truth":
            # the ground truth in the capture (aliasing-safe at high replay speed);
            # prefetched in sample_loop while the sim is still running
            key = (ex["word"], ex["t0"], ex["t1"], ex.get("signed", False))
            resp = self.replay_truth_cache.get(key, {"ok": False, "error": "not prefetched"})
            ok = resp.get("ok") and resp.get("n", 0) > 0
            if ok and "peak_min" in ex:
                ok = resp["max"] is not None and resp["max"] >= ex["peak_min"]
            if ok and "val_min" in ex:
                ok = resp["min"] is not None and resp["min"] >= ex["val_min"]
            if ok and "val_max" in ex:
                ok = resp["max"] is not None and resp["max"] <= ex["val_max"]
            self.add(f"replay-truth word{ex['word']}[{ex['t0']},{ex['t1']}]",
                     ok, f"min={resp.get('min')} max={resp.get('max')} n={resp.get('n')}")
        elif kind == "gesture":
            res = self.gesture_results.get(ex["name"], {})
            ok = res.get("ran") and res.get("trigger") == ex["expect_trigger"]
            self.add(f"gesture {ex['name']} trigger={ex['expect_trigger']}", ok,
                     res.get("log", "")[-200:] if not ok else "")
        elif kind == "contract":
            need = {"t_ms", "page", "phase", "work_sta", "regs", "lang",
                    "boot_flag", "crc_ok", "crc_bad", "cmd6", "frame_ms"}
            last = self.samples[-1][1] if self.samples else {}
            missing = need - set(last.keys())
            fb_ok = self.fb_path.exists() and self.fb_path.stat().st_size >= FB_BYTES
            self.add("SIM contract: state fields + fb buffer", not missing and fb_ok,
                     f"missing={sorted(missing)} fb_ok={fb_ok}")
        else:
            self.add(f"unknown expect kind: {kind}", False)
        if ex.get("_ui_profile") and self.checks:
            self.checks[-1]["name"] = "[ui-profile] " + self.checks[-1]["name"]
            self.checks[-1]["ui_profile"] = ex["_ui_profile"]

    def nearest_snapshot(self, t, max_dist=2.0):
        best, bd = None, 1e9
        for sn in self.snapshots:
            d = abs(sn["t"] - t)
            if d < bd:
                best, bd = sn, d
        return best if bd <= max_dist else None

    def eval_sim_log(self, ex):
        a = ex["assert"]
        log = self.sim_log
        live = [e for e in log if e.get("source", "live") == "live"]
        t0, t1 = ex.get("t0", 0), ex.get("t1", 1e9)
        if a == "boot_push":
            bt0 = ex.get("t0", 0)     # v6.14: t0 = only pushes after this time (re-push)
            blocks = {e.get("block") for e in log
                      if e["kind"] == "boot_push" and e["t"] >= bt0}
            ok = {"sub1_len30", "sub112_len3"} <= blocks
            self.add(f"sim: boot-push (sub1 len30 + sub112 len3)"
                     + (f" after t={bt0}" if bt0 else ""), ok, str(blocks))
        elif a == "version":
            ok = any(e["kind"] == "version_handshake" for e in log)
            self.add("sim: version handshake (0x89→cmd9)", ok)
        elif a == "keepalive_ok":
            gaps = [e for e in log if e["kind"] == "keepalive_gap"]
            self.add("sim: keep-awake word1=0 ≤30 s", not gaps, json.dumps(gaps[:3]))
        elif a == "no_reboot":
            bad = [e for e in log if e["kind"] == "reboot_trigger"]
            self.add("sim: no word30=1 (reboot)", not bad, json.dumps(bad[:3]))
        elif a == "no_calib":
            bad = [e for e in log if e["kind"] == "calib_trigger"]
            self.add("sim: no word26/27≠0 (calibration)", not bad, json.dumps(bad[:3]))
        elif a == "no_band_rejects":
            bad = [e for e in log if e["kind"] == "write_rejected"
                   and e.get("verdict") == "band"]
            self.add("sim: no band violations", not bad, json.dumps(bad[:3]))
        elif a == "range_max":
            bad = [e for e in log if e["kind"] == "range_violation"]
            ok = len(bad) <= ex.get("max", 0)
            self.add(f"sim: range violations ≤{ex.get('max', 0)}", ok,
                     f"n={len(bad)} {[e.get('reason') for e in bad[:5]]}")
        elif a == "range_min":
            bad = [e for e in log if e["kind"] == "range_violation"]
            ok = len(bad) >= ex.get("min", 1)
            self.add(f"sim: range violations ≥{ex.get('min', 1)} (logged)", ok,
                     f"n={len(bad)}")
        elif a == "write_words":
            words = ex["words"]
            hits = [e for e in live if e["kind"] == "write"
                    and e.get("sub") == ex["sub"] and e.get("words") == words
                    and t0 <= e["t"] <= t1]
            self.add(f"sim: write sub{ex['sub']}={words} in [{t0},{t1}]",
                     bool(hits), f"n={len(hits)}")
        elif a == "ack_words":
            # v6.13: ACK echo (0x86) for a specific write — sub+words are an
            # exact echo (mcu_sim logs at scheduling time, after the loss profile).
            words = ex["words"]
            hits = [e for e in log if e["kind"] == "ack"
                    and e.get("sub") == ex["sub"] and e.get("words") == words
                    and t0 <= e["t"] <= t1]
            self.add(f"sim: ACK sub{ex['sub']}={words} in [{t0},{t1}]",
                     bool(hits), f"n={len(hits)}")
        elif a == "no_write_word":
            w = ex["word"]
            bad = []
            for e in live:
                if e["kind"] != "write" or not (t0 <= e["t"] <= t1):
                    continue
                sub, words = e.get("sub", 0), e.get("words", [])
                if sub <= w < sub + len(words) and words[w - sub] != 0:
                    bad.append(e)
            self.add(f"sim: no writes to word{w}≠0 in [{t0},{t1}]", not bad,
                     json.dumps(bad[:2]))
        elif a == "no_writes_between":
            exc = set(ex.get("except_subs", [0, 1, 112]))
            bad = [e for e in live if e["kind"] == "write"
                   and t0 <= e["t"] <= t1 and e.get("sub") not in exc]
            self.add(f"sim: no writes in [{t0},{t1}] (except {sorted(exc)})",
                     not bad, json.dumps([{k: e.get(k) for k in ('t', 'sub', 'words')}
                                          for e in bad[:3]]))
        elif a == "activity_stop":
            hits = [e for e in log if e["kind"] == "activity_stop"
                    and e.get("activity") == ex["activity"]
                    and t0 <= e["t"] <= t1
                    and ex.get("reason_contains", "") in e.get("reason", "")]
            self.add(f"sim: {ex['activity']} stopped ({ex.get('reason_contains', '')})",
                     bool(hits), json.dumps(hits[:2]))
        elif a == "activity_start":
            hits = [e for e in log if e["kind"] == "activity_start"
                    and e.get("activity") == ex["activity"] and t0 <= e["t"] <= t1]
            self.add(f"sim: {ex['activity']} started in [{t0},{t1}]", bool(hits))
        elif a == "sleep_write_seen":
            ok = any(e["kind"] == "sleep_write" for e in log)
            self.add("sim: sleep write (word1=1) seen", ok)
        elif a == "awake_write_seen":
            ok = any(e["kind"] == "write" and e.get("sub") == 1
                     and e.get("words") == [0] for e in live)
            self.add("sim: awake write (word1=0) seen", ok)
        elif a == "acks_dropped_min":
            n = sum(1 for e in log if e["kind"] == "ack_dropped")
            self.add(f"sim: ACK loss ≥{ex['value']}", n >= ex["value"], f"n={n}")
        elif a == "writes_min":
            n = sum(1 for e in live if e["kind"] == "write"
                    and e.get("sub") == ex["sub"] and t0 <= e["t"] <= t1)
            self.add(f"sim: writes sub{ex['sub']} ≥{ex['value']} in [{t0},{t1}]",
                     n >= ex["value"], f"n={n}")
        # ---- v6.14: deep sleep/wake (SIM_HANDOFF, UI_v6 §19) ----
        elif a == "dvale_seen":
            hits = [e for e in log if e["kind"] == "dvale_enter"
                    and t0 <= e["t"] <= t1]
            self.add(f"sim: deep sleep (word0=0 → silent) in [{t0},{t1}]", bool(hits),
                     json.dumps(hits[:2]))
        elif a == "no_acks_between":
            bad = [e for e in log if e["kind"] == "ack" and t0 <= e["t"] <= t1]
            self.add(f"sim: no ACK in [{t0},{t1}] (silent UART)", not bad,
                     json.dumps(bad[:3]))
        elif a == "time_sync_seen":
            hits = [e for e in log if e["kind"] == "time_sync"
                    and t0 <= e["t"] <= t1
                    and (not ex.get("alarm0") or e.get("alarm") == [0, 0, 0])
                    and ("alarm" not in ex or e.get("alarm") == ex["alarm"])]   # v6.16: exact alarm
            nmax = ex.get("max_count")                                            # v6.16: at most N
            ok = bool(hits) and (nmax is None or len(hits) <= nmax)
            if ex.get("count") == 0:                                              # v6.16: none at all
                ok = not hits
            self.add(f"sim: cmd8 time/alarm" + (" (0/0/0)" if ex.get("alarm0") else "")
                     + (f" alarm={ex['alarm']}" if "alarm" in ex else "")
                     + (" none" if ex.get("count") == 0 else "")
                     + (f" ≤{nmax}" if nmax is not None else "")
                     + f" in [{t0},{t1}]", ok, f"n={len(hits)} {json.dumps([e.get('alarm') for e in hits[:4]])}")
        elif a == "power_wake_seen":
            hits = [e for e in log if e["kind"] == "power_wake"
                    and t0 <= e["t"] <= t1
                    and ex.get("cause_contains", "") in e.get("cause", "")]
            self.add(f"sim: power wake ({ex.get('cause_contains', '')}) in [{t0},{t1}]",
                     bool(hits), json.dumps(hits[:2]))
        elif a == "reboot_seen":
            hits = [e for e in log if e["kind"] == "reboot"
                    and t0 <= e["t"] <= t1
                    and (not ex.get("factory") or e.get("factory"))]
            self.add("sim: reboot" + (" (factory start values)" if ex.get("factory") else "")
                     + f" in [{t0},{t1}]", bool(hits), json.dumps(hits[:2]))
        elif a == "factory_defaults":
            hits = [e for e in log if e["kind"] == "reboot" and e.get("factory")
                    and t0 <= e["t"] <= t1]
            want = {"0": 1, "11": 920, "114": 50}   # REGISTERMAP §2 (init 0x9966)
            ok = any(all((e.get("defaults") or {}).get(k) == v
                         for k, v in want.items()) for e in hits)
            self.add(f"sim: factory defaults (word0=1, word11=920, word114=50) in [{t0},{t1}]",
                     ok, json.dumps([e.get("defaults") for e in hits][:2]))
        else:
            self.add(f"unknown sim_log assert: {a}", False)

    def eval_universal(self):
        uni = self.scen.get("universal", {})
        if uni.get("circle", True):
            self.eval_expect({"kind": "circle"})
        if uni.get("contract", True):
            self.eval_expect({"kind": "contract"})
        if uni.get("no_reboot", True):
            self.eval_sim_log({"assert": "no_reboot"})
        if uni.get("no_calib", True):
            self.eval_sim_log({"assert": "no_calib"})
        if uni.get("no_band_rejects", True):
            self.eval_sim_log({"assert": "no_band_rejects"})
        if uni.get("ranges", True):
            self.eval_sim_log({"assert": "range_max", "max": 0})
        if uni.get("keepawake", True):
            self.eval_sim_log({"assert": "keepalive_ok"})
        if uni.get("boot_push", True):
            self.eval_sim_log({"assert": "boot_push"})
            self.eval_sim_log({"assert": "version"})
        if uni.get("exit0", True):
            self.eval_expect({"kind": "exit_code", "value": 0})
        if uni.get("no_cmd6_fail", True):
            self.eval_expect({"kind": "cmd6", "field": "fail", "op": "==", "value": 0})

    # ------------------------------------------------------------ (g) report
    def finish(self):
        for ex in self.scen.get("expect", []):
            try:
                self.eval_expect(ex)
            except Exception as e:
                self.add(f"expect error {ex.get('kind')}", False, str(e))
        self.eval_universal()
        for err in self.profile_errors:
            self.add(f"ui profile: {err}", False, "override does not match the scenario")
        passed = all(c["ok"] for c in self.checks)
        # PNG + contact sheet (auto snapshots keep only the PNG — .bin is 921 KB
        # and is deleted after conversion; explicit/final snapshots are kept)
        sheet_items = []
        for sn in self.snapshots:
            pngp = Path(sn["bin"]).with_suffix(".png")
            try:
                png.fb2png(sn["raw"], pngp)
                sn["png"] = str(pngp)
                if sn["label"].startswith("auto"):
                    Path(sn["bin"]).unlink(missing_ok=True)
                    sn["bin"] = None
            except Exception as e:
                sn["png_error"] = str(e)
        steps = max(1, len(self.snapshots) // 16)
        for sn in self.snapshots[::steps][:16]:
            ok, _ = circle_check(sn["raw"])
            sheet_items.append((sn["label"], sn["raw"], ok and passed))
        sheet = self.dir / "contact_sheet.png"
        if sheet_items:
            png.contact_sheet(sheet_items, sheet,
                              title=f"{self.name} {'PASS' if passed else 'FAIL'}")
        # final state summary
        last = self.samples[-1][1] if self.samples else {}
        report = {
            "name": self.name,
            "description": self.scen.get("description", ""),
            "pass": passed,
            "client": self.client_kind,
            "ui_bin": str(self.ui_bin) if self.ui_bin else None,
            "ui_lang": (last.get("ui_lang") or self.ui_lang),
            "ui_profile": self.profile_applied,
            "frame_ms": {k: last.get(k) for k in ("frame_ms_avg", "frame_ms_peak", "frames")},
            "duration_s": self.scen.get("duration_s"),
            "samples": len(self.samples),
            "snapshots": [{k: v for k, v in sn.items() if k != "raw"}
                          for sn in self.snapshots],
            "contact_sheet": str(sheet) if sheet_items else None,
            "checks": self.checks,
            "final_state": {k: last.get(k) for k in
                            ("phase", "work_sta", "page", "cmd6", "crc_ok",
                             "crc_bad", "config_oor", "boot_flag", "lang")},
            "sim_stats": next((e.get("stats") for e in reversed(self.sim_log)
                               if e["kind"] == "sim_exit"), None),
            "gesture_results": self.gesture_results,
            "clock": self.clock,
            "sandbox": str(self.sandbox),
            "sim_assertions_log": str(self.assert_path),
            "state_jsonl": str(self.state_path),
        }
        (self.dir / "report.json").write_text(json.dumps(report, indent=2,
                                                        ensure_ascii=False))
        shutil.rmtree(self.sock_dir, ignore_errors=True)
        return report


def load_scenarios(scen_dir=None):
    out = {}
    for p in sorted(Path(scen_dir or SCEN_DIR).glob("*.json")):
        try:
            s = json.loads(p.read_text())
            s.setdefault("name", p.stem)
            out[s["name"]] = s
        except json.JSONDecodeError as e:
            print(f"WARNING: {p} cannot be parsed: {e}", file=sys.stderr)
    return out


def write_summary(reports, run_dir):
    lines = ["# E2E report — merakiplus SIM environment", ""]
    lines.append(f"**Run:** {run_dir.name} · "
                 f"{time.strftime('%Y-%m-%d %H:%M:%S')} · "
                 f"client: {reports[0]['client'] if reports else '-'}"
                 + (f" · UI language: {reports[0].get('ui_lang')}"
                    if reports and reports[0].get("ui_lang") else ""))
    nov = sum(len(r.get("ui_profile") or []) for r in reports)
    if nov:
        lines.append("")
        lines.append(f"**UI profile:** {nov} documented overrides applied "
                     "(reference layout/behaviour → real merakiplus; see the bottom and "
                     "CONTRACT.md ADDENDUM v6.4). Checks marked `[ui-profile]`.")
    lines.append("")
    npass = sum(1 for r in reports if r["pass"])
    ntot = sum(len(r["checks"]) for r in reports)
    nokt = sum(1 for r in reports for c in r["checks"] if c["ok"])
    lines.append(f"**Result: {npass}/{len(reports)} scenarios PASS** · "
                 f"checks: {nokt}/{ntot}")
    lines.append("")
    lines.append("| Scenario | Result | Checks | Client | Description |")
    lines.append("|---|---|---|---|---|")
    for r in reports:
        nok = sum(1 for c in r["checks"] if c["ok"])
        lines.append(f"| {r['name']} | {'✅ PASS' if r['pass'] else '❌ FAIL'} | "
                     f"{nok}/{len(r['checks'])} | {r['client']} | "
                     f"{r['description'][:70]} |")
    lines.append("")
    for r in reports:
        lines.append(f"## {r['name']} — {'PASS' if r['pass'] else 'FAIL'}")
        lines.append("")
        if r.get("contact_sheet"):
            lines.append(f"![contact sheet]({r['contact_sheet']})")
            lines.append("")
        fails = [c for c in r["checks"] if not c["ok"]]
        if fails:
            lines.append("**Failed checks:**")
            for c in fails:
                lines.append(f"- `{c['name']}`: {c['detail'][:300]}")
            lines.append("")
        lines.append(f"Final state: `{json.dumps(r['final_state'], ensure_ascii=False)}`")
        lines.append("")
        lines.append(f"Details: [report.json]({r['name']}/report.json) · "
                     f"[sim-assertions]({r['name']}/sim_assertions.jsonl) · "
                     f"[state]({r['name']}/state.jsonl)")
        lines.append("")
    if nov:
        lines.append("## UI profile: applied overrides")
        lines.append("")
        lines.append("| Scenario | Type | Original | Replaced with | Reason |")
        lines.append("|---|---|---|---|---|")
        for r in reports:
            for ov in r.get("ui_profile") or []:
                new = ov.get("replace") if ov["type"] == "expect" else ov.get("set")
                lines.append(f"| {r['name']} | {ov['type']} | "
                             f"`{json.dumps(ov['match'], ensure_ascii=False)}` | "
                             f"`{json.dumps(new, ensure_ascii=False)}` | {ov.get('why', '')} |")
        lines.append("")
    fts = [r["frame_ms"] for r in reports if (r.get("frame_ms") or {}).get("frames")]
    if fts:
        lines.append("## Frame time (SIM build on the HOST — not the device)")
        lines.append("")
        lines.append("| Scenario | Frames | Avg ms | Max ms |")
        lines.append("|---|---|---|---|")
        for r in reports:
            ft = r.get("frame_ms") or {}
            if ft.get("frames"):
                lines.append(f"| {r['name']} | {ft['frames']} | "
                             f"{ft['frame_ms_avg']:.3f} | {ft['frame_ms_peak']:.3f} |")
        lines.append("")
    (run_dir / "report.md").write_text("\n".join(lines))
    (run_dir / "report.json").write_text(json.dumps(
        {"run": run_dir.name, "pass": npass, "total": len(reports),
         "scenarios": reports}, indent=2, ensure_ascii=False))


def main():
    ap = argparse.ArgumentParser(description="E2E runner for the merakiplus SIM environment")
    ap.add_argument("--scenario", help="run one scenario (name)")
    ap.add_argument("--all", action="store_true", help="run all scenarios")
    ap.add_argument("--list", action="store_true", help="list scenarios")
    ap.add_argument("--ui-bin", help="merakiplus SIM build (otherwise the reference client)")
    ap.add_argument("--ref-client", action="store_true",
                    help="use the reference client (sim/ref_client.py) even if a SIM build "
                         "exists — oracle run with the REF profile")
    ap.add_argument("--reports-dir", default=str(REPORTS))
    ap.add_argument("--keep-sandbox", action="store_true")
    ap.add_argument("--ui-lang", choices=["en", "da"],
                    help="merakiplus's UI language (written to the sandbox's language file; default: no file = English)")
    ap.add_argument("--ui-profile", help="profile w/ overrides (default: sim/sim_ui_profile-ui.json "
                                         "[empty] for the SIM build, sim/sim_ui_profile-ref.json for the reference client)")
    ap.add_argument("--no-ui-profile", action="store_true",
                    help="run the scenarios RAW, without any profile overrides")
    ap.add_argument("--run-name", help="name of the run directory (default: run-<timestamp>)")
    ap.add_argument("--scen-dir", help="alternative scenario directory (default: sim/scenarios)")
    ap.add_argument("--quick", action="store_true",
                    help="QUICK dev iteration: small core subset, one language (en), no REF/steam30/replay. "
                         "The full suite runs only in CI/CD or at a final milestone.")
    ap.add_argument("--parallel", type=int, default=1, metavar="N",
                    help="run up to N scenarios concurrently (independent sandboxes/ptys)")
    args = ap.parse_args()

    scens = load_scenarios(args.scen_dir)
    if args.list:
        for name, s in scens.items():
            print(f"{name:24s} {s.get('duration_s', '?'):>5}s  "
                  f"{s.get('description', '')[:70]}")
        return 0
    if not args.scenario and not args.all:
        ap.print_help()
        return 2
    names = list(scens) if args.all else [args.scenario]
    if args.quick:
        quick = ["idle", "brew", "steam_auto", "no_ack", "out_of_spec"]
        names = [n for n in quick if n in scens] if args.all or not args.scenario else names
        if not args.ui_lang:
            args.ui_lang = "en"
        print(f"[e2e] QUICK dev mode: {len(names)} core scenarios, one language (en). "
              f"Full suite = CI/CD / final milestone.")
    if args.scenario and args.scenario not in scens:
        print(f"unknown scenario: {args.scenario} (use --list)", file=sys.stderr)
        return 2
    ui_bin = find_ui_bin(args.ui_bin)
    if ui_bin and ui_bin.resolve() == (HERE / "ref_client.py").resolve():
        ui_bin = None                     # --ui-bin sim/ref_client.py = the reference client
    if args.ref_client:
        ui_bin = None                     # --ref-client: force an oracle run
    if ui_bin:
        print(f"[e2e] merakiplus SIM-build: {ui_bin}")
    else:
        print("[e2e] no merakiplus SIM build found — running the REFERENCE client "
              "(sim/ref_client.py). See CONTRACT.md for SIM build requirements.")
    # v6.5-scenarios: the scenarios assert merakiplus's guarded behaviour. The SIM build
    # runs with the EMPTY UI profile; the reference client with the REF profile (the
    # inverse overrides, so oracle runs are still green). --no-ui-profile
    # runs fully raw; --ui-profile <file> selects explicitly (backward compatible).
    profile = None
    if not args.no_ui_profile:
        default_pp = UI_PROFILE_DEFAULT if ui_bin else REF_PROFILE_DEFAULT
        pp = Path(args.ui_profile) if args.ui_profile else default_pp
        if pp.exists():
            profile = json.loads(pp.read_text())
            nov = sum(len(v) for v in (profile.get("scenarios") or {}).values())
            print(f"[e2e] {'UI' if ui_bin else 'REF'} profile: {pp} ({nov} overrides)")
        elif args.ui_profile:
            print(f"UI profile does not exist: {pp}", file=sys.stderr)
            return 2
    run_dir = Path(args.reports_dir) / (args.run_name or
                                        "run-" + time.strftime("%Y%m%d-%H%M%S"))
    run_dir.mkdir(parents=True, exist_ok=True)
    reports = []

    def run_one(name):
        scen = scens[name]
        print(f"\n[e2e] === {name} ({scen.get('duration_s', '?')}s) ===", flush=True)
        if scen.get("ui_only") and not ui_bin:
            # v6.16 (additive): a merakiplus-only scenario (clock/schedule state, pre-armed alarm) has no
            # oracle in the reference client — reported as skipped, not as a fake pass of its checks.
            print(f"[e2e] {name}: SKIP (merakiplus-only scenario; reference client)", flush=True)
            return {"name": name, "pass": True, "client": "reference", "skipped": True,
                    "description": scen.get("description", ""),
                    "checks": [{"name": "skipped: merakiplus-only scenario (no reference-client oracle)", "ok": True, "detail": ""}],
                    "final_state": {}, "snapshots": [], "samples": 0, "contact_sheet": None,
                    "sim_stats": None, "gesture_results": {}, "duration_s": 0, "ui_bin": None,
                    "sandbox": "", "sim_assertions_log": "", "state_jsonl": ""}
        sr = ScenarioRun(scen, run_dir, ui_bin=ui_bin,
                         keep_sandbox=args.keep_sandbox,
                         ui_lang=args.ui_lang, ui_profile=profile)
        try:
            sr.make_sandbox()
            sr.start_sim()
            sr.start_ui()
            sr.sample_loop()
            rep = sr.finish()
        except Exception as e:
            import traceback
            traceback.print_exc()
            rep = {"name": name, "pass": False, "client": sr.client_kind,
                   "description": scen.get("description", ""),
                   "checks": [{"name": "runner error", "ok": False, "detail": str(e)}],
                   "final_state": {}, "snapshots": [], "samples": 0,
                   "contact_sheet": None, "sim_stats": None, "gesture_results": {},
                   "duration_s": scen.get("duration_s"), "ui_bin": str(ui_bin) if ui_bin else None,
                   "sandbox": str(sr.sandbox), "sim_assertions_log": str(sr.assert_path),
                   "state_jsonl": str(sr.state_path)}
            for proc in (sr.ui_proc, sr.sim_proc):
                if proc and proc.poll() is None:
                    proc.terminate()
            shutil.rmtree(sr.sock_dir, ignore_errors=True)
        nok = sum(1 for c in rep["checks"] if c["ok"])
        print(f"[e2e] {name}: {'PASS' if rep['pass'] else 'FAIL'} "
              f"({nok}/{len(rep['checks'])} checks)", flush=True)
        for c in rep["checks"]:
            if not c["ok"]:
                print(f"      ✗ {c['name']}: {c['detail'][:160]}")
        return rep

    if args.parallel > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=args.parallel) as ex:
            reports = list(ex.map(run_one, names))
    else:
        reports = [run_one(n) for n in names]
    write_summary(reports, run_dir)
    npass = sum(1 for r in reports if r["pass"])
    print(f"\n[e2e] TOTAL: {npass}/{len(reports)} PASS · report: {run_dir}/report.md")
    return 0 if npass == len(reports) else 1


if __name__ == "__main__":
    sys.exit(main())