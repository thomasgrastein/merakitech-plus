#!/usr/bin/env python3
"""upd_sim.py — merakiplus_upd for the simulator (OTA phase 3 scenarios, sim/scenarios/ota_update_*.json).

The UI SIM build starts this instead of the device helper (scenario ui_env MERAKIPLUS_UPD_BIN). It runs the REAL
updater — the TEST build build/merakiplus_upd_test (./build.sh host) — against a local HTTPS stand-in for GitHub
(tests/ota_fixture.py: TLS with a throwaway test CA, GitHub's redirect chain, a release signed with a throwaway test key),
with the same arguments the UI passed (check | install --version V --status FILE ...). So a scenario exercises the
UI's consent flow, the helper's TLS + signature + anti-downgrade checks and the staging into the sandbox together.

MERAKIPLUS_UPD_SIM_CASE picks GitHub's answer:
  ok        latest = v6.99 (seq 9, signed, with notes)            current   latest = v6.25 (seq 3 = max_seq)
  older     latest = v6.20 (seq 2 < max_seq 3)                    badsig    latest = v6.99 with a tampered manifest
  offline   nothing answers (connection refused)                  redirect  GitHub redirects to an unpinned host
The scenario puts ota/state (max_seq 3) and a launcher stand-in into the sandbox (sandbox_files); the release's
platform launcher is made equal to it, so the helper's launcher check passes. PKI + key are kept in
<sandbox>/upd_sim/ between the check and the install.
"""
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests"))
import ota_fixture as of   # noqa: E402

HELPER = REPO / "build" / "merakiplus_upd_test"


def main():
    root = pathlib.Path(os.environ.get("MERAKIPLUS_ROOT") or os.environ["MERAKIPLUS_SIM_ROOT"])
    case = os.environ.get("MERAKIPLUS_UPD_SIM_CASE", "ok")
    work = root / "upd_sim"
    work.mkdir(exist_ok=True)
    pki = of.PKI(work / "pki", reuse=True)
    key = of.SavedKey(work, "sim-test")
    launcher = (root / "new_customer/demo_v9").read_bytes() if (root / "new_customer/demo_v9").is_file() else b"LAUNCHER-1\n"
    srv = of.Server(pki)
    try:
        def rel(version, seq, **kw):
            d = work / f"rel-{version}-{seq}"
            r = of.make_release(d, key, version, seq, launcher=launcher, released="2026-11-27",
                                notes="On-device update test\n- **Signed** by the test key\n- Served over TLS\n", **kw) \
                if not (d / "manifest.sig").is_file() or kw else None
            if r is None:                      # built by the check before: reuse (same bytes, same release name)
                import json
                m = json.loads((d / "manifest.json").read_bytes())
                ui = next(e for e in m["release"] if e["name"] == "demo_v9")
                r = {"dir": d, "version": version, "seq": seq, "manifest": m, "name": f"{version}-{ui['sha256'][:8]}"}
            srv.cfg["releases"][version] = r
            return r
        if case in ("ok", "redirect", "offline"):
            rel("6.99", 9)
            srv.cfg["latest"] = "6.99"
        elif case == "current":
            rel("6.25", 3)
            srv.cfg["latest"] = "6.25"
        elif case == "older":
            rel("6.20", 2)
            srv.cfg["latest"] = "6.20"
        elif case == "badsig":
            rel("6.98", 8, tamper=lambda d: (d / "manifest.json").write_bytes(
                (d / "manifest.json").read_bytes().replace(b'"seq": 8', b'"seq": 80')))
            srv.cfg["latest"] = "6.98"
        if case == "redirect":
            srv.cfg["redirect"] = {"latest/download/manifest.json": "https://evil.example.com/manifest.json"}
        env = of.helper_env(root, pki, 1 if case == "offline" else srv.port, key)
        p = subprocess.run([str(HELPER), *sys.argv[1:]], env=env, timeout=120)
        with open(work / "requests.log", "a") as f:
            f.write("".join(r + "\n" for r in srv.requests()))
        return p.returncode
    finally:
        srv.close()


if __name__ == "__main__":
    sys.exit(main())
