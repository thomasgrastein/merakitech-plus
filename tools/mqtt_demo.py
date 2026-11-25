#!/usr/bin/env python3
"""mqtt_demo.py — try the Meraki+ MQTT / Home Assistant integration WITHOUT a coffee machine.

Runs the real publish-only helper (host build of src/merakiplus_mqtt.c) against YOUR broker and feeds it the UI's own
records of a canned session (the preview build's --feed-demo: a grind, a shot -> shot_finished, steam, hot water, a
water-tank fault and its clearing). Home Assistant then shows the device "Meraki" with its entities, the shot lands on
merakiplus/<id>/shot, and "offline" is published when the demo ends — exactly what the display would send.

    ./build.sh host                                   # once: builds the helper + the preview build (needs zig 0.17.1)
    python3 tools/mqtt_demo.py --host 192.168.1.50 --user merakiplus --password-file ~/.merakiplus_mqtt_pw
    python3 tools/mqtt_demo.py --host 192.168.1.50 --speed 1          # real time (about 2.5 minutes)

The broker must be on your local network (the helper refuses anything else, as on the display). The demo uses its own
device id (default "demo0001"), so it does not mix with a real machine's entities; delete the device in Home
Assistant afterwards, or clear the retained topics (see docs/integrations.md §16).
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
HELPER = ROOT / "build" / "merakiplus_mqtt_host"
PREVIEW = ROOT / "build" / "merakiplus_preview"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", required=True, help="broker address on your network")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user")
    ap.add_argument("--password-file", help="file with the broker password (else none)")
    ap.add_argument("--id", default="demo0001", help="device id for the demo (letters/digits, max 12)")
    ap.add_argument("--speed", type=float, default=10.0, help="time lapse (default 10x: the session takes ~15 s)")
    ap.add_argument("--no-live", action="store_true", help="skip the 2/s live samples")
    a = ap.parse_args()
    if not PREVIEW.exists():
        subprocess.run(["zig", "cc", "-DMERAKIPLUS_PREVIEW", "-O2", "-o", str(PREVIEW), "src/merakiplus.c", "-lm"], cwd=ROOT, check=True)
    if not HELPER.exists():
        subprocess.run(["zig", "cc", "-O2", "-o", str(HELPER), "src/merakiplus_mqtt.c"], cwd=ROOT, check=True)
    lines = subprocess.run([str(PREVIEW), "--feed-demo", str(ROOT / "sim/fixtures/config.json")], cwd=ROOT,
                           capture_output=True, text=True, check=True).stdout.splitlines()
    pw = pathlib.Path(a.password_file).expanduser().read_text().strip() if a.password_file else ""
    d = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus_mqtt_demo_"))
    conf = d / "mqtt.conf"
    conf.write_text("".join(f"{k} = {v}\n" for k, v in (("enabled", 1), ("host", a.host), ("port", a.port), ("user", a.user or ""),
                                                          ("pass", pw), ("id", a.id), ("live", 0 if a.no_live else 1)) if v != ""))
    os.chmod(conf, 0o600)
    st = d / "status"
    p = subprocess.Popen([str(HELPER), str(conf), str(st), str(d / "log")], stdin=subprocess.PIPE)
    print(f"helper started; topics merakiplus/{a.id}/#, discovery homeassistant/device/merakiplus_{a.id}/config")
    for _ in range(50):
        s = st.read_text().split() if st.exists() else []
        if s[1:2] == ["connected"]:
            break
        if s[1:2] in (["not_local"], ["auth"], ["config"]):
            sys.exit(f"helper: {' '.join(s)} — see {d / 'log'}")
        time.sleep(0.2)
    else:
        print("warning: not connected yet (" + (st.read_text().strip() if st.exists() else "no status") + ") — records queue")
    t_prev = None
    for ln in lines:
        ch, js = ln.split("\t", 1)
        up = json.loads(js).get("uptime_s")
        if t_prev is not None and up is not None and up > t_prev:
            time.sleep(min((up - t_prev) / a.speed, 5.0))
        if up is not None:
            t_prev = up
        if ch == "event":
            print("  event", json.loads(js)["event_type"])
        p.stdin.write((ln + "\n").encode())
        p.stdin.flush()
    time.sleep(1.0)
    p.stdin.close()
    p.wait(timeout=15)
    print("done: 'offline' published; status:", st.read_text().strip() if st.exists() else "-")
    print(f"log: {d / 'log'}")


if __name__ == "__main__":
    main()
