#!/usr/bin/env python3
"""test_mqtt.py — MQTT / Home Assistant: the feed records, the publish-only helper and its discovery document.

No device, no real broker: the helper (host build of src/merakiplus_mqtt.c) talks to sim/mqtt_broker.py on 127.0.0.1 and is fed
the UI's own feed lines (the preview build's --feed-demo: a canned grind, shot, steam, hot water and water-tank fault).

    python3 tests/test_mqtt.py            # after ./build.sh host (builds missing pieces itself with zig if needed)

Checks (docs/integrations.md §10 + §15): record schemas and key conventions; the shot record's Beanconqueror curve; the discovery
document (no command topic, only sensor / binary_sensor / event); CONNECT with the last will; every topic, QoS and retain flag;
NO SUBSCRIBE (packet types seen = CONNECT / PUBLISH / PINGREQ / DISCONNECT only); the password only inside CONNECT (never in a
payload, the log or the status file); a clean "offline" + DISCONNECT on EOF; an unsolicited PUBLISH from the broker ends the
connection; reconnect after an outage with the queued events; public broker addresses refused; a refused login; a bad config.
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "sim"))
from mqtt_broker import MockBroker  # noqa: E402

BUILD = ROOT / "build"
HELPER = BUILD / "merakiplus_mqtt_host"
SELFTEST = BUILD / "merakiplus_mqtt_selftest"
PREVIEW = BUILD / "merakiplus_preview"
PW = "PWMARK-9f1c-s3cret"
ID = "t3st0001"

fails = []


def check(name, ok, detail=""):
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        fails.append(name)


def build_missing():
    zig = shutil.which("zig")
    jobs = [(SELFTEST, ["-DMQTT_SELFTEST", "src/merakiplus_mqtt.c"]), (HELPER, ["src/merakiplus_mqtt.c"]),
            (PREVIEW, ["-DMERAKIPLUS_PREVIEW", "src/merakiplus.c", "-lm"])]
    for out, args in jobs:
        if not out.exists():
            if not zig:
                sys.exit(f"{out} missing and no zig on PATH — run ./build.sh host first")
            BUILD.mkdir(exist_ok=True)
            subprocess.run([zig, "cc", "-O2", "-o", str(out)] + args, cwd=ROOT, check=True)


def feed_demo():
    out = subprocess.run([str(PREVIEW), "--feed-demo", str(ROOT / "sim/fixtures/config.json")], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout
    return [tuple(l.split("\t", 1)) for l in out.splitlines() if l]


def conf(d, **kw):
    c = {"enabled": 1, "host": "127.0.0.1", "port": 1883, "user": "merakiplus", "pass": PW, "id": ID, "base": "merakiplus",
         "discovery": 1, "live": 1}
    c.update(kw)
    p = pathlib.Path(d) / "mqtt.conf"
    p.write_text("# test\n" + "".join(f"{k} = {v}\n" for k, v in c.items() if v is not None))
    os.chmod(p, 0o600)
    return p


class Helper:
    def __init__(self, d, confp):
        self.status, self.log = pathlib.Path(d) / "status", pathlib.Path(d) / "mqtt.log"
        self.p = subprocess.Popen([str(HELPER), str(confp), str(self.status), str(self.log)], stdin=subprocess.PIPE,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def send(self, lines, gap=0.0):
        for ch, js in lines:
            self.p.stdin.write(f"{ch}\t{js}\n".encode())
            self.p.stdin.flush()
            if gap:
                time.sleep(gap)

    def st(self):
        try:
            return self.status.read_text().split()
        except OSError:
            return []

    def close(self, timeout=8):
        try:
            self.p.stdin.close()
        except OSError:
            pass
        try:
            return self.p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.p.kill()
            return None


UNIT_SUFFIX = ("_c", "_g", "_s", "_ms", "_v", "_hz", "_bar", "_raw", "_gs", "_utc")
NUM_OK = {"seq", "feed_major", "e1", "e2", "e3", "ratio", "brew_heat_stage", "steam_heat_stage", "enabled", "pressure_choice",
          "grind_setting", "uptime_s", "t_s", "group_set_c"}


def walk_units(obj, path, bad):
    """units are in the key names: a numeric value's key ends in a unit (or is a count / code / ratio)"""
    if isinstance(obj, dict):
        for k, v in obj.items():
            if isinstance(v, (int, float)) and not isinstance(v, bool) and not k.endswith(UNIT_SUFFIX) and k not in NUM_OK:
                bad.append(f"{path}.{k}")
            if k != "curve":
                walk_units(v, f"{path}.{k}", bad)


def test_records(lines):
    print("== feed records (the preview build's --feed-demo: grind, shot, steam, hot water, water-tank fault) ==")
    recs = {}
    bad_json = []
    for ch, js in lines:
        try:
            recs.setdefault(ch, []).append(json.loads(js))
        except ValueError:
            bad_json.append(ch)
    check("every line is '<channel>\\t<JSON>' and parses", not bad_json and len(lines) > 100, str(bad_json[:3]))
    schema = {"info": "merakiplus.info/1", "state": "merakiplus.state/1", "live": "merakiplus.live/1", "event": "merakiplus.event/1",
              "shot": "merakiplus.shot/1", "shotsum": "merakiplus.shot/1"}
    check("channels = info, state, live, event, shot, shotsum; each record carries its schema",
          set(recs) == set(schema) and all(r.get("schema") == schema[ch] for ch, rs in recs.items() for r in rs))
    bad = []
    for ch, rs in recs.items():
        for r in rs:
            walk_units(r, ch, bad)
    check("units are in the key names (_c, _g, _s, _ms, _v, _hz, _bar, _raw, _gs); counts/codes/ratio excepted", not bad, str(sorted(set(bad))[:6]))
    ev = [r["event_type"] for r in recs["event"]]
    check("events: grind_started, grind_finished, brew_started, shot_finished, steam_started, steam_finished, water_started, "
          "water_finished, error, error_cleared", ev == ["grind_started", "grind_finished", "brew_started", "shot_finished",
                                                          "steam_started", "steam_finished", "water_started", "water_finished",
                                                          "error", "error_cleared"], str(ev))
    shot, summ = recs["shot"][0], recs["shotsum"][0]
    companion = ["start_utc", "complete", "completion_reason", "duration_estimate_s", "settled_yield_g", "dose_target_g",
                 "yield_target_g", "grind_setting", "pre_infusion", "grind_workflow"]
    check("shot record: Companion's key names where the meaning is the same (start_utc, duration_estimate_s, settled_yield_g, "
          "dose/yield targets, grind_setting, pre_infusion, grind_workflow, complete, completion_reason)",
          all(k in shot for k in companion) and set(shot["pre_infusion"]) == {"enabled", "pressure_choice", "duration_setting_s"})
    check("shot record: brew_set_c (word 11 / 10, no offset) and NOT Companion's brew_target_c (its +5 °C is unresolved)",
          shot.get("brew_set_c") == 87.0 and "brew_target_c" not in shot)
    check("shot record: seq, clock_ok, end_utc (ISO 8601 UTC), machine {id, ui_version, controller_fw}, source Meraki+, bean/preset null",
          shot["seq"] == 418 and shot["clock_ok"] is True and re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", shot["end_utc"] or "")
          and shot["machine"] == {"id": "3fa9c2d1", "ui_version": "6.24", "controller_fw": "V5.1.4"} and shot["source"] == "Meraki+"
          and shot["bean"] is None and shot["preset"] is None)
    c = shot["curve"]
    n = len(c["t"])
    check("curve: Beanconqueror flow encoding — equal lengths, t deltas (ms) sum to the last sample, weight deltas (0.1 g) sum to the "
          "settled yield", n > 100 and n == len(c["weight"]) == len(c["flow_raw"]) == len(c["pump_raw"]) and c["t"][0] == 0
          and all(x > 0 for x in c["t"][1:]) and abs(sum(c["weight"]) / 10 - shot["settled_yield_g"]) < 0.05
          and c["encoding"] == "beanconqueror_flow")
    check("curve → Beanconqueror ADD_BREW 'flow' needs no conversion: {t, weight} as they are (sanity: running sums are monotone time)",
          [sum(c["t"][:i + 1]) for i in range(n)] == sorted(sum(c["t"][:i + 1]) for i in range(n)))
    check("shot summary = the record without 'curve' (all other keys equal)", "curve" not in summ
          and {k: v for k, v in shot.items() if k != "curve"} == summ)
    check("pump: raw values + a labelled estimate only (pressure_est_bar); nothing called a measured pressure",
          "extraction_raw" in shot["pump"] and "pressure_est_bar" in shot["pump"] and not any(k == "pressure_bar" for k in shot["pump"]))
    st = recs["state"][0]
    check("state: activity, temperatures, weights, heat stages + ready, water, faults + raw codes, mains",
          all(k in st for k in ("activity", "brew_boiler_c", "group_c", "steam_boiler_c", "milk_c", "brew_weight_g", "grind_weight_g",
                                "brew_ready", "steam_ready", "water_low", "faults", "e1", "e2", "e3", "mains_v", "mains_hz")))
    err = recs["event"][8]
    check("error event: stable fault names + the raw words", err["faults"] == ["water_tank_missing"] and err["e3"] == 32768)
    blob = "\n".join(js for _, js in lines)
    check("no identifiers: no serial block, no Wi-Fi name, no register dump in any record",
          "registers" not in blob and '"regs"' not in blob and "ssid" not in blob.lower() and "serial" not in blob.lower())
    return recs


def test_selftest():
    print("== helper selftest (address rule, packets, discovery, mqtt.conf) ==")
    r = subprocess.run([str(SELFTEST)], capture_output=True, text=True)
    check("merakiplus_mqtt --selftest: ALL OK", r.returncode == 0 and "ALL OK" in r.stdout, r.stdout[-300:])


def disc_check(doc, base=f"merakiplus/{ID}"):
    comps = doc.get("components", {})
    plat = {c["platform"] for c in comps.values()}
    s = json.dumps(doc)
    return (doc["device"]["identifiers"] == [f"merakiplus_{ID}"] and doc["availability_topic"] == f"{base}/availability"
            and plat <= {"sensor", "binary_sensor", "event"} and "command_topic" not in s and "cmd_t" not in s
            and all(c["state_topic"].startswith(base + "/") for c in comps.values())
            and all(c["unique_id"] == f"merakiplus_{ID}_{k}" for k, c in comps.items())
            and "shot_finished" in comps["machine_event"]["event_types"] and doc["origin"]["name"] == "merakiplus_mqtt"
            and not any("curve" in c.get("json_attributes_topic", "") for c in comps.values())
            and comps["last_shot"]["json_attributes_topic"] == f"{base}/shot/summary")


def test_e2e(lines):
    print("== helper ↔ mock broker: connect, discovery, every topic, EOF ==")
    d = tempfile.mkdtemp(prefix="mqtt_t_")
    b = MockBroker()
    h = Helper(d, conf(d, port=b.port))
    try:
        ok = b.wait(lambda: any(p["type"] == "PUBLISH" and p["topic"].endswith("/availability") for p in b.packets), 8)
        check("connects to the local broker and publishes availability 'online'", ok)
        h.send(lines, gap=0.002)
        b.wait(lambda: len(b.pubs(f"merakiplus/{ID}/event")) >= 10 and b.pubs(f"merakiplus/{ID}/shot"), 10)
        time.sleep(0.5)
        con = [p for p in b.packets if p["type"] == "CONNECT"][0]
        check("CONNECT: MQTT 3.1.1, clean session, keep-alive 30 s, client id merakiplus-<id>, user + password",
              con["proto"] == "MQTT" and con["level"] == 4 and con["clean"] and con["keepalive"] == 30
              and con["client_id"] == f"merakiplus-{ID}" and con["user"] == "merakiplus" and con.get("password") == PW)
        check("last will: <base>/<id>/availability = 'offline', QoS 1, retained",
              con.get("will_topic") == f"merakiplus/{ID}/availability" and con.get("will_payload") == "offline"
              and con.get("will_qos") == 1 and con.get("will_retain"))
        dt = f"homeassistant/device/merakiplus_{ID}/config"
        dps = b.pubs(dt)
        doc = json.loads(dps[-1]["payload"]) if dps else {}
        check("Home Assistant device discovery: retained, QoS 1, one document; device by the random id; sw 6.24, hw controller V5.1.4",
              bool(dps) and dps[-1]["retain"] and dps[-1]["qos"] == 1 and doc["device"].get("sw_version") == "6.24"
              and doc["device"].get("hw_version") == "controller V5.1.4")
        check("discovery: only sensor / binary_sensor / event, no command topic, every state topic under merakiplus/<id>/, "
              "unique ids from the random id, machine event incl. shot_finished, last shot attributes from shot/summary",
              bool(doc) and disc_check(doc), json.dumps(doc)[:200])
        check("discovery: the live entities are disabled by default (recorder load), diagnostics marked",
              bool(doc) and all(doc["components"][k].get("enabled_by_default") is False for k in ("live_weight", "live_flow", "pressure_est", "pump_raw", "flow_raw"))
              and doc["components"]["mains_v"].get("entity_category") == "diagnostic")
        tp = {t: b.pubs(f"merakiplus/{ID}/{t}") for t in ("info", "state", "live", "event", "shot", "shot/summary", "availability")}
        check("info: retained QoS 1; state: retained QoS 0; live: not retained QoS 0; event: not retained QoS 1; shot + shot/summary: retained QoS 1",
              all(p["retain"] and p["qos"] == 1 for p in tp["info"]) and tp["info"]
              and all(p["retain"] and p["qos"] == 0 for p in tp["state"]) and tp["state"]
              and all(not p["retain"] and p["qos"] == 0 for p in tp["live"]) and len(tp["live"]) > 100
              and all(not p["retain"] and p["qos"] == 1 for p in tp["event"])
              and all(p["retain"] and p["qos"] == 1 for p in tp["shot"] + tp["shot/summary"]) and tp["shot"] and tp["shot/summary"])
        evs = [json.loads(p["payload"])["event_type"] for p in tp["event"]]
        check("all 10 events delivered once, in order (incl. shot_finished)", evs == [json.loads(js)["event_type"] for ch, js in lines if ch == "event"], str(evs))
        shot = json.loads(tp["shot"][-1]["payload"])
        check("the shot topic carries the complete record (curve included): a subscriber to .../shot alone gets everything",
              "curve" in shot and shot["schema"] == "merakiplus.shot/1" and shot["seq"] == 418)
        check("payloads are the UI's records unchanged (byte for byte)",
              tp["shot"][-1]["payload"] == next(js for ch, js in lines if ch == "shot")
              and tp["shot/summary"][-1]["payload"] == next(js for ch, js in lines if ch == "shotsum"))
        types = set(b.types())
        check("packet types from the helper: CONNECT, PUBLISH (+ PINGREQ / DISCONNECT) — NEVER SUBSCRIBE / UNSUBSCRIBE",
              types <= {"CONNECT", "PUBLISH", "PINGREQ", "DISCONNECT"} and "SUBSCRIBE" not in types, str(types))
        b.wait(lambda: len(h.st()) > 2 and int(h.st()[2]) >= len(b.pubs()) - 1, 3)
        stt = h.st()
        check("status file: 'mqtt1 connected <published> <dropped> <reconnects> <last publish> -'", stt[:2] == ["mqtt1", "connected"]
              and int(stt[2]) >= len(b.pubs()) - 1, str(stt))
        rc = h.close()
        b.wait(lambda: b.disconnects >= 1, 5)
        av = [p["payload"] for p in b.pubs(f"merakiplus/{ID}/availability")]
        check("EOF on the feed (UI ended / MQTT switched off): 'offline' (retained) is published, then DISCONNECT; exit code 0; status 'stopped'",
              rc == 0 and av[-1] == "offline" and b.disconnects == 1 and b.types()[-1] == "DISCONNECT" and h.st()[1] == "stopped", f"rc={rc} av={av}")
        blob = "".join(p.get("payload", "") for p in b.packets) + h.log.read_text() + h.status.read_text()
        check("the password appears ONLY inside CONNECT: not in any payload, the log or the status file", PW not in blob)
        check("the log stays small (< 16 KB incl. its one rotation) and names no secret", h.log.stat().st_size < 8192 + 512)
    finally:
        h.close()
        b.close()
        shutil.rmtree(d, ignore_errors=True)


def test_hostile_and_outage(lines):
    print("== robustness: unsolicited PUBLISH, broker outage + reconnect, queued events ==")
    d = tempfile.mkdtemp(prefix="mqtt_t_")
    b = MockBroker()
    h = Helper(d, conf(d, port=b.port))
    try:
        b.wait(lambda: b.pubs(f"merakiplus/{ID}/availability"), 8)
        h.send([l for l in lines if l[0] in ("info", "state")][:3])
        b.wait(lambda: b.pubs(f"merakiplus/{ID}/state"), 5)
        b.inject_publish("merakiplus/cmd", b"brew")
        ok = b.wait(lambda: b.closed >= 1, 5)
        log = h.log.read_text() if h.log.exists() else ""
        check("a PUBLISH from the broker (e.g. a 'command') ends the connection — the helper accepts no messages", ok and "PUBLISH" in log, log[-200:])
        ok = b.wait(lambda: b.connections >= 2 and len(b.pubs(f"merakiplus/{ID}/availability")) >= 2, 10)
        check("…then it reconnects (back-off 2 s ± jitter) and republishes availability + discovery + retained state", ok
              and len(b.pubs(f"homeassistant/device/merakiplus_{ID}/config")) >= 2 and len(b.pubs(f"merakiplus/{ID}/state")) >= 2)
        n_ev0 = len(b.pubs(f"merakiplus/{ID}/event"))
        b.outage(True)
        b.wait(lambda: h.st()[1:2] == ["waiting"] or h.st()[1:2] == ["connecting"], 6)
        evs = [l for l in lines if l[0] == "event"][:5]
        h.send(evs + [l for l in lines if l[0] == "live"][:20])
        time.sleep(1.0)
        check("during the outage nothing reaches the broker; status = waiting", len(b.pubs(f"merakiplus/{ID}/event")) == n_ev0
              and h.st()[1] in ("waiting", "connecting"), str(h.st()))
        b.outage(False)
        ok = b.wait(lambda: len(b.pubs(f"merakiplus/{ID}/event")) >= n_ev0 + 5, 12)
        got = [json.loads(p["payload"])["event_type"] for p in b.pubs(f"merakiplus/{ID}/event")[n_ev0:]]
        check("after the outage: the 5 queued events are delivered in order (QoS 1); live samples of the outage are dropped, not spooled",
              ok and got[:5] == [json.loads(js)["event_type"] for _, js in evs], str(got))
    finally:
        h.close()
        b.close()
        shutil.rmtree(d, ignore_errors=True)


def test_refusals():
    print("== refusals: public broker, refused login, bad / disabled config ==")
    for host in ("8.8.8.8", "1.1.1.1", "2001:4860:4860::8888", "100.64.0.1"):
        d = tempfile.mkdtemp(prefix="mqtt_t_")
        h = Helper(d, conf(d, host=host, port=1883))
        ok = False
        for _ in range(40):
            if h.st()[1:2] == ["not_local"]:
                ok = True
                break
            time.sleep(0.05)
        log = h.log.read_text() if h.log.exists() else ""
        check(f"broker {host}: refused before any connection attempt (status not_local)", ok and "connected" not in log, log[-160:])
        h.close()
        shutil.rmtree(d, ignore_errors=True)
    d = tempfile.mkdtemp(prefix="mqtt_t_")
    b = MockBroker(connack_rc=5)
    h = Helper(d, conf(d, port=b.port))
    try:
        ok = b.wait(lambda: h.st()[1:2] == ["auth"], 6)
        check("a refused login (CONNACK 5): status auth, nothing published, no fast retry loop", ok and not b.pubs() and b.connections == 1)
    finally:
        h.close()
        b.close()
        shutil.rmtree(d, ignore_errors=True)
    for kw, why in (({"enabled": 0}, "disabled"), ({"host": None}, "bad_host"), ({"id": "a/b"}, "bad_id"), ({"base": "x/#"}, "bad_base")):
        d = tempfile.mkdtemp(prefix="mqtt_t_")
        h = Helper(d, conf(d, **kw))
        time.sleep(0.3)
        stt = h.st()
        rc = h.close(4)
        check(f"config {kw}: not started (status config {why}), drains the feed, exits on EOF", stt[1:2] == ["config"] and stt[-1] == why and rc == 3, str(stt))
        shutil.rmtree(d, ignore_errors=True)


def test_discovery_cli():
    print("== --discovery (used for the docs' Home Assistant test guide) ==")
    d = tempfile.mkdtemp(prefix="mqtt_t_")
    try:
        r = subprocess.run([str(HELPER), "--discovery", str(conf(d)), '{"ui_version":"6.24","controller_fw":"V5.1.4"}'],
                           capture_output=True, text=True)
        topic, doc = r.stdout.split("\n", 1)
        doc = json.loads(doc)
        check("prints the topic + the same document the helper publishes", r.returncode == 0 and topic == f"homeassistant/device/merakiplus_{ID}/config"
              and disc_check(doc) and len(doc["components"]) >= 25)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_static():
    print("== static: the helper source builds no SUBSCRIBE and opens no listening socket ==")
    src = (ROOT / "src/merakiplus_mqtt.c").read_text()
    code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
    defs = dict(re.findall(r"#define (P_[A-Z]+)\s+(0x[0-9A-Fa-f]+)", code))
    shipped = code[:code.rindex("#else")]                 # the shipped program (the selftest after #else probes every type on purpose)
    outs = [o for o in re.findall(r"mq_out\([^,]+,\s*([^,]+),", shipped) if o.strip() != "uint8_t first"]
    allowed = {"P_CONNECT", "P_PUBLISH", "P_PINGREQ", "P_DISCONNECT"}
    check("packet types: only CONNECT / CONNACK / PUBLISH / PUBACK / PINGREQ / PINGRESP / DISCONNECT are defined; every mq_out() call "
          "builds CONNECT, PUBLISH, PINGREQ or DISCONNECT; no SUBSCRIBE anywhere; no bind / listen / accept",
          set(defs) == {"P_CONNECT", "P_CONNACK", "P_PUBLISH", "P_PUBACK", "P_PINGREQ", "P_PINGRESP", "P_DISCONNECT"}
          and not {int(v, 16) >> 4 for v in defs.values()} & {8, 9, 10, 11} and outs
          and all(o.strip() in allowed or o.strip().startswith("(uint8_t)(P_PUBLISH|") for o in outs)
          and "SUBSCRIBE" not in code and not re.search(r"\bbind\s*\(|\blisten\s*\(|\baccept\s*\(", code), f"{defs} {outs}")
    ui = re.sub(r"/\*.*?\*/", "", (ROOT / "src/merakiplus.c").read_text(), flags=re.S)
    feed = ui[ui.index("static int feed_on(void)"):ui.index("static void ui_touch(")]
    check("the UI's feed code sends no controller frame and opens no network socket (no wq_push / act_push / connect / AF_INET)",
          not re.search(r"\bwq_push|\bact_push|\buart_send|\bconnect\s*\(|AF_INET\b", feed))


def main():
    build_missing()
    lines = feed_demo()
    test_selftest()
    test_records(lines)
    test_static()
    test_discovery_cli()
    test_e2e(lines)
    test_hostile_and_outage(lines)
    test_refusals()
    print(f"MQTT: {'ALL OK' if not fails else str(len(fails)) + ' FAILED'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
