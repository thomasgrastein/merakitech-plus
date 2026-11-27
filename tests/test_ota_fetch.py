#!/usr/bin/env python3
"""On-device HTTPS check + install (docs/ota-plan.md phase 3): the updater's TEST build against a local TLS server.

    python3 tests/test_ota_fetch.py          host (needs ./build.sh host and OpenSSL >= 3)
    python3 tests/test_ota_fetch.py --arm    also the static ARM TEST build in the arm32v7 container (local; docker)

The server (tests/ota_fixture.py) answers for github.com and the release-asset host on one port with GitHub's
redirect chain and certificates from a throwaway TEST CA; releases are signed with a throwaway TEST key. Covered:
check (available / up to date / no signed release / older refused / bad or missing signature / unknown key / launcher
mismatch / notes), TLS (pinned roots only, expired / not yet valid / wrong host name / unknown CA, the clock floor with a
1970 and a far-future clock, ECDSA + RSA certificates), redirects (unpinned host, http://, loop, relative), HTTP errors
(404 / 500, refused connection), long URLs (GitHub-sized signed asset URLs by default; requests of 1023 / 1024 / 1025 /
1500 / 2048 bytes; the 4095-byte maximum and a refused 4096-byte URL; an install with every URL ~2 KB), install (staging,
MANIFEST in the installer's format, state pending + trial_seq with max_seq unchanged, extra state keys kept,
ota/healthy removed, the launcher starts the trial), max_seq only after a healthy commit (a rolled-back trial is offered
and installable again; after the commit it is current and older releases are refused), damaged downloads (tampered,
truncated, oversize, a server that stalls mid-transfer, chunked), a helper killed mid-download and run again, changed
release, trial in progress, no space, idempotent re-install, garbage collection (young directories kept), one run at a
time, nothing written on flash by a check, and that production builds carry none of the test hooks.
"""
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import ota_fixture as of   # noqa: E402

TEST = ROOT / "build" / "merakiplus_upd_test"
PROD = ROOT / "build" / "merakiplus_upd_host"
LAUNCHER = ROOT / "build" / "merakiplus_boot_host"
FAILS, N = [], [0]
EXIT = {"ok": 0, "signature": 3, "format": 4, "older": 5, "asset": 6, "io": 7, "net": 8, "busy": 9}


def check(name, cond, detail=""):
    N[0] += 1
    print(("  [PASS] " if cond else "  [FAIL] ") + name + ("" if cond else f"  -- {detail}"))
    if not cond:
        FAILS.append(name)


def tree(root):
    """every file under new_customer/ with its content hash (to prove 'nothing written')"""
    base = pathlib.Path(root) / "new_customer"
    return {str(p.relative_to(base)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(base.rglob("*")) if p.is_file()}


class Run:
    def __init__(self, rig, exe=TEST, connect="127.0.0.1", wrap=None):
        self.rig, self.exe, self.connect, self.wrap = rig, exe, connect, wrap
        self.k = 0

    def root(self, **kw):
        self.k += 1
        d = self.rig.work / f"dev{self.k}"
        kw.setdefault("launcher", self.rig.srv.cfg["releases"][self.rig.srv.cfg.get("latest") or "6.26"]["launcher"]
                      if self.rig.srv.cfg["releases"] else b"LAUNCHER-1\n")
        return of.device_root(d, **kw)

    def __call__(self, root, *args, now=None, free=None, io_timeout=None):
        env = of.helper_env(root, self.rig.pki, self.rig.srv.port, self.rig.key, self.connect, now, free, io_timeout)
        cmd = [str(self.exe), *args]
        if self.wrap:
            cmd = self.wrap(root, cmd, env)
        p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
        s = of.read_status(pathlib.Path(root) / "tmp/merakiplus_upd.status")
        return p.returncode, p.stdout.strip(), s


def scenario(run):
    rig = run.rig
    srv = rig.srv
    r26 = rig.release("6.26", 4, notes="Signed test release\n- **On-device update** test\n* third line\nfourth\nfifth\n")
    rig.release("6.25", 3)
    srv.cfg["latest"] = "6.26"

    print("== check ==")
    root = run.root()
    before = tree(root)
    rc, out, s = run(root, "check")
    check("check: a newer signed release is available (seq 4 > max_seq 3)",
          rc == 0 and s.get("state") == "ok" and s.get("result") == "available" and s.get("version") == "6.26" and s.get("seq") == "4"
          and out.startswith("ok available"), f"{rc} {out} {s}")
    check("check: status file names the current release, no block, op=check", s.get("current") == "6.25-aaaaaaaa"
          and s.get("blocked") == "" and s.get("op") == "check" and s.get("error") == "", str(s))
    check("check: the signed notes.txt -> first 4 lines, markdown stripped, upper case",
          [s.get(f"notes{i}") for i in range(1, 5)] == ["SIGNED TEST RELEASE", "ON-DEVICE UPDATE TEST", "THIRD LINE", "FOURTH"], str(s))
    check("check: nothing written on flash (only the status file in /tmp)", tree(root) == before)
    reqs = srv.requests()
    check("check: manifest + signature + notes via GitHub's redirect chain (latest -> tag -> asset host), no asset download",
          any("latest/download/manifest.json" in r for r in reqs) and any(r.startswith(of.ASSETS + "/v6.26/manifest.sig") for r in reqs)
          and not any("merakiplus_upd?" in r or "/merakiplus?" in r for r in reqs), str(reqs[-6:]))

    root = run.root(max_seq=4, current="6.26-0123abcd")
    rc, out, s = run(root, "check")
    check("check: same seq -> up to date", rc == 0 and s.get("result") == "current", f"{rc} {s}")
    root = run.root(max_seq=None, current="6.26-0123abcd")
    rc, out, s = run(root, "check")
    check("check: no max_seq (unsigned dev install) but the same version -> up to date", rc == 0 and s.get("result") == "current", f"{rc} {s}")
    root = run.root(max_seq=5)
    rc, out, s = run(root, "check")
    check("check: latest seq 4 < max_seq 5 -> OLDER VERSION REFUSED", rc == EXIT["older"] and s.get("error") == "older"
          and s.get("state") == "fail", f"{rc} {s}")

    srv.cfg["latest"] = None
    root = run.root()
    rc, out, s = run(root, "check")
    check("check: no signed release published (404) -> result none, not an error", rc == 0 and s.get("result") == "none", f"{rc} {s}")
    srv.cfg["latest"] = "6.26"

    root = run.root(launcher=b"LAUNCHER-OLD\n")
    rc, out, s = run(root, "check")
    check("check: a release built for another launcher -> available but blocked=launcher", rc == 0 and s.get("result") == "available"
          and s.get("blocked") == "launcher", f"{rc} {s}")
    rc, out, s = run(root, "install", "--version", "6.26")
    check("install: refused for a different launcher (NEEDS INSTALL FROM A COMPUTER), nothing staged",
          rc == EXIT["busy"] and s.get("error") == "launcher" and not (root / "new_customer/merakiplus/rel" / r26["name"]).exists(), f"{rc} {s}")

    print("== signatures ==")
    bad = rig.release("6.27", 5, tamper=lambda d: (d / "manifest.json").write_bytes((d / "manifest.json").read_bytes().replace(b'"seq": 5', b'"seq": 6')))
    srv.cfg["latest"] = "6.27"
    rc, out, s = run(run.root(), "check")
    check("check: tampered manifest -> UPDATE REJECTED: SIGNATURE", rc == EXIT["signature"] and s.get("error") == "signature", f"{rc} {s}")
    other = of.st.TestKey(rig.work, "other")
    rig.release("6.28", 6, sign_key=other)
    srv.cfg["latest"] = "6.28"
    rc, out, s = run(run.root(), "check")
    check("check: signed by an unknown key -> signature", rc == EXIT["signature"] and "unknown key" in s.get("msg", ""), f"{rc} {s}")
    srv.cfg["latest"] = "6.26"
    srv.cfg["status"] = {"manifest.sig": 404}
    rc, out, s = run(run.root(), "check")
    check("check: manifest without signature (404) -> signature", rc == EXIT["signature"] and s.get("error") == "signature", f"{rc} {s}")
    srv.cfg["status"] = {}
    del bad

    print("== TLS: pinned roots, host names, validity, clock floor ==")
    for cert, why in (("untrusted", "not from a pinned root"), ("wrongname", "another host"), ("expired", "expired"), ("future", "expired or not yet")):
        srv.cfg["cert"] = cert
        rc, out, s = run(run.root(), "check")
        check(f"TLS: {cert} certificate refused ({why})", rc == EXIT["net"] and s.get("error") == "tls" and why.split()[0] in s.get("msg", ""), f"{rc} {s}")
    srv.cfg["cert"] = "good_rsa"
    rc, out, s = run(run.root(), "check")
    check("TLS: RSA server certificate (ECDHE-RSA) accepted", rc == 0 and s.get("result") == "available", f"{rc} {s}")
    srv.cfg["cert"] = None
    rc, out, s = run(run.root(), "check", now=0)
    check("clock floor: device clock at 1970 -> certificates checked at the floor date -> still valid", rc == 0 and s.get("result") == "available", f"{rc} {s}")
    rc, out, s = run(run.root(), "check", now=9_000_000_000)
    check("clock floor: a far-future clock (year 2255) -> floor -> still valid", rc == 0 and s.get("result") == "available", f"{rc} {s}")
    rc, out, s = run(run.root(), "check", now=2_200_000_000)
    check("clock: a plausible clock after the certificate's end (2039) is NOT floored -> expired", rc == EXIT["net"] and s.get("error") == "tls", f"{rc} {s}")

    print("== redirects + HTTP errors ==")
    for loc, name in (("https://evil.example.com/x", "unpinned host"), ("http://github.com/x", "plain http"),
                      ("https://github.com:444/x", "explicit port"), ("https://user@github.com/x", "userinfo")):
        srv.cfg["redirect"] = {"latest/download/manifest.json": loc}
        rc, out, s = run(run.root(), "check")
        check(f"redirect to {name} refused before connecting", rc == EXIT["net"] and s.get("error") == "redirect"
              and not srv.requests("evil.example.com"), f"{rc} {s}")
    srv.cfg["redirect"] = {"latest/download/manifest.json": f"https://{of.GH}/{of.REPO}/releases/latest/download/manifest.json"}
    rc, out, s = run(run.root(), "check")
    check("redirect loop -> refused after 5 redirects", rc == EXIT["net"] and s.get("error") == "redirect" and "more than 5" in s.get("msg", ""), f"{rc} {s}")
    srv.cfg["redirect"] = {"latest/download/manifest.json": f"/{of.REPO}/releases/download/v6.26/manifest.json"}
    rc, out, s = run(run.root(), "check")
    check("relative redirect on the same host followed", rc == 0 and s.get("result") == "available", f"{rc} {s}")
    srv.cfg["redirect"] = {}
    srv.cfg["status"] = {"latest/download/manifest.json": 500}
    rc, out, s = run(run.root(), "check")
    check("HTTP 500 -> NO CONNECTION TO GITHUB (http), graceful", rc == EXIT["net"] and s.get("error") == "http" and "500" in s.get("msg", ""), f"{rc} {s}")
    srv.cfg["status"] = {}
    run2 = Run(rig, run.exe, run.connect, run.wrap)
    saved = srv.port
    srv.port = 1                                     # nothing listens there
    rc, out, s = run2(run.root(), "check")
    srv.port = saved
    check("no server (connection refused) -> net, graceful", rc == EXIT["net"] and s.get("error") in ("net", "timeout") and s.get("state") == "fail", f"{rc} {s}")

    print("== long URLs (GitHub's signed asset URLs: ~900 bytes, varying per download) ==")
    u0 = of.asset_url("v6.26", "manifest.json")
    over = of.request_len(u0) - len(u0)                  # request bytes beyond the URL itself
    def mreq():                                          # the size of the last request for the manifest on the asset host
        z = [n for r, n in srv.sizes if r.startswith(of.ASSETS + "/v6.26/manifest.json")]
        return z[-1] if z else None
    rc, out, s = run(run.root(), "check")
    check(f"default fixture URLs are GitHub-sized (manifest {len(u0)} B, ready_extract.wav request "
          f"{of.request_len(of.asset_url('v6.26', 'ready_extract.wav'))} B > the old 1023-byte limit) and work",
          rc == 0 and s.get("result") == "available" and of.request_len(of.asset_url("v6.26", "ready_extract.wav")) > 1023, f"{rc} {s}")
    for req in (1023, 1024, 1025, 1500, 2048):
        srv.cfg["url_len"] = {"manifest.json": req - over}
        rc, out, s = run(run.root(), "check")
        check(f"long URL: a {req}-byte request (URL {req - over} B) works", rc == 0 and s.get("result") == "available" and mreq() == req,
              f"{rc} {s} sent={mreq()}")
    srv.cfg["url_len"] = {"manifest.json": 4095}
    rc, out, s = run(run.root(), "check")
    check("long URL: the maximum, a 4095-byte redirect URL, works", rc == 0 and s.get("result") == "available" and mreq() == 4095 + over, f"{rc} {s}")
    n0 = len(srv.requests(of.ASSETS))
    srv.cfg["url_len"] = {"manifest.json": 4096}
    rc, out, s = run(run.root(), "check")
    check("long URL: a 4096-byte redirect URL is refused cleanly (redirect, 'longer than'), nothing sent to the asset host",
          rc == EXIT["net"] and s.get("error") == "redirect" and "longer than 4095" in s.get("msg", "") and len(srv.requests(of.ASSETS)) == n0, f"{rc} {s}")
    srv.cfg["url_len"] = {}

    print("== install ==")
    root = run.root(previous="6.24-bbbbbbbb", extra="custom_key=kept\n")
    (root / "new_customer/merakiplus/ota/healthy").write_text("rel=6.25-aaaaaaaa\n")
    old = time.time() - 3600
    for junk in ("6.20-cccccccc", ".stage-6.21-dddddddd", ".damaged-6.22-x-1"):
        (root / "new_customer/merakiplus/rel" / junk).mkdir()
        os.utime(root / "new_customer/merakiplus/rel" / junk, (old, old))
    for young in ("6.30-99999999", ".stage-6.31-young"):  # a computer install filling / just renamed (state not written yet)
        (root / "new_customer/merakiplus/rel" / young).mkdir()
    (root / "new_customer/merakiplus/rel/6.23-eeeeeeee").mkdir()
    rc, out, s = run(root, "install", "--version", "6.26", "--running", "6.23-eeeeeeee")
    rel = root / "new_customer/merakiplus/rel" / r26["name"]
    st = (root / "new_customer/merakiplus/ota/state").read_text()
    check("install: staged -> result staged, name <version>-<sha8 of the UI>", rc == 0 and s.get("result") == "staged"
          and s.get("name") == r26["name"] and s.get("pct") == "100", f"{rc} {out} {s}")
    files = [(e["name"], (r26["dir"] / e["src"]).read_bytes()) for e in r26["manifest"]["release"]]
    check("install: every release file byte-identical, modes from the manifest (755 / 644)",
          all((rel / n).read_bytes() == b for n, b in files) and os.stat(rel / "demo_v9").st_mode & 0o777 == 0o755
          and os.stat(rel / "audio/start.wav").st_mode & 0o777 == 0o644 and (rel / "merakiplus_upd").is_file())
    check("install: MANIFEST written in the installer's format (tools/merakiplus.py manifest())",
          (rel / "MANIFEST").read_bytes() == of.mp.manifest("6.26", files))
    check("install: state pending=<new>, tries=0, trial_seq=<new> 4, max_seq still 3 (raised only after a healthy commit); "
          "current/previous unchanged; unknown keys kept",
          f"pending={r26['name']}\n" in st and "tries=0\n" in st and "max_seq=3\n" in st and f"trial_seq={r26['name']} 4\n" in st
          and "current=6.25-aaaaaaaa\n" in st
          and "previous=6.24-bbbbbbbb\n" in st and "custom_key=kept\n" in st and st.startswith("format=1\n"), st)
    check("install: ota/healthy of the old trial removed (like the installer)", not (root / "new_customer/merakiplus/ota/healthy").exists())
    left = sorted(p.name for p in (root / "new_customer/merakiplus/rel").iterdir())
    check("install: GC (under ota/lock) removed old stages, damaged and unreferenced releases; kept current, previous, running, "
          "the new one and directories changed in the last 10 minutes (a computer install in progress)",
          left == sorted(["6.24-bbbbbbbb", "6.25-aaaaaaaa", "6.23-eeeeeeee", r26["name"], "6.30-99999999", ".stage-6.31-young"]), str(left))
    check("install: no temp files left (state tmp, own stage)", not list((root / "new_customer").rglob("*.tmp*"))
          and [p.name for p in (root / "new_customer").rglob(".stage-*")] == [".stage-6.31-young"])
    check("install: ota/lock released (the launcher can take it)", lock_free(root))
    if LAUNCHER.is_file():
        p = subprocess.run([str(LAUNCHER), "--dry"], env={**os.environ, "MERAKIPLUS_BOOT_ROOT": str(root)}, capture_output=True, text=True)
        check("the launcher (host build, --dry) starts the staged release as a trial", r26["name"] in p.stdout and "trial" in (p.stdout + p.stderr).lower(), p.stdout + p.stderr)
    n_before = len(srv.requests("merakiplus_upd?"))
    rc, out, s = run(root, "install", "--version", "6.26")
    check("install again (AT NEXT START was chosen before): already complete -> staged, nothing downloaded again",
          rc == 0 and s.get("result") == "staged" and len(srv.requests("merakiplus_upd?")) == n_before, f"{rc} {s}")
    rc, out, s = run(root, "check")
    check("check while staged (not started yet): still offered (max_seq is not raised at staging); install is idempotent",
          rc == 0 and s.get("result") == "available", f"{rc} {s}")

    print("== max_seq only after a healthy commit: a rolled-back trial can be tried again ==")
    stf = root / "new_customer/merakiplus/ota/state"
    def launcher_writes(**kv):                         # what the launcher writes (it keeps unknown lines: tests/test_launcher.py)
        lines = stf.read_text().splitlines()
        stf.write_text("".join((f"{l.split('=')[0]}={kv[l.split('=')[0]]}" if l.split("=")[0] in kv else l) + "\n" for l in lines))
    launcher_writes(pending="", tries="0", last=f"rolled_back:{r26['name']}:crashloop")
    rc, out, s = run(root, "check")
    check("after a rollback: check -> available again (not 'current'): the rolled-back seq does not count",
          rc == 0 and s.get("result") == "available" and s.get("version") == "6.26", f"{rc} {s}")
    n_before = len(srv.requests("merakiplus_upd?"))
    rc, out, s = run(root, "install", "--version", "6.26")
    st = stf.read_text()
    check("after a rollback: install again -> staged as the trial again (the complete directory is reused)",
          rc == 0 and s.get("result") == "staged" and f"pending={r26['name']}\n" in st and f"trial_seq={r26['name']} 4\n" in st
          and "max_seq=3\n" in st and len(srv.requests("merakiplus_upd?")) == n_before, f"{rc} {s} {st}")
    launcher_writes(current=r26["name"], previous="6.25-aaaaaaaa", pending="", tries="0", last=f"ok:{r26['name']}")   # healthy -> committed
    rc, out, s = run(root, "check")
    check("after the healthy commit: check -> up to date (the committed seq 4 counts)", rc == 0 and s.get("result") == "current", f"{rc} {s}")
    srv.cfg["latest"] = "6.25"
    rc, out, s = run(root, "check")
    check("after the healthy commit: an older signed release (seq 3 < 4) is refused", rc == EXIT["older"] and s.get("error") == "older", f"{rc} {s}")
    srv.cfg["latest"] = "6.29"
    r29 = rig.release("6.29", 7)
    rc, out, s = run(root, "install", "--version", "6.29")
    st = stf.read_text()
    check("the next install persists the committed seq (max_seq=4) and records its own trial (trial_seq=<6.29> 7)",
          rc == 0 and s.get("result") == "staged" and "max_seq=4\n" in st and f"trial_seq={r29['name']} 7\n" in st, f"{rc} {s} {st}")
    launcher_writes(pending="", tries="0", last=f"rolled_back:{r29['name']}:unhealthy")
    srv.cfg["latest"] = "6.25"
    rc, out, s = run(root, "check")
    check("...and after that trial is rolled back, anti-downgrade still holds (seq 3 < 4 refused)", rc == EXIT["older"], f"{rc} {s}")
    srv.cfg["latest"] = "6.26"
    del srv.cfg["releases"]["6.29"]

    def failed_install(name, cfg, want_err, **kw):
        saved = {k: srv.cfg.get(k) for k in cfg}
        srv.cfg.update(cfg)
        root = run.root()
        before = (root / "new_customer/merakiplus/ota/state").read_text()
        rc, out, s = run(root, "install", "--version", kw.pop("version", "6.26"), **kw)
        srv.cfg.update(saved)
        relx = root / "new_customer/merakiplus/rel"
        clean = not (relx / r26["name"]).exists() and not list(relx.glob(".stage-*"))
        unchanged = (root / "new_customer/merakiplus/ota/state").read_text() == before
        check(f"install: {name} -> {want_err}, stage discarded, state untouched", s.get("error") == want_err and rc != 0 and clean and unchanged,
              f"{rc} {s} clean={clean} unchanged={unchanged}")

    failed_install("tampered asset", {"tamper": {"merakiplus_snd": b"evil" * 225}}, "asset")
    failed_install("asset larger than its signed size (Content-Length)", {"oversize": {"merakiplus": 10}}, "asset")
    failed_install("truncated download", {"truncate": {"merakiplus": 1000}}, "truncated")
    failed_install("server stalls mid-transfer (headers + 1000 bytes, then silence)", {"stall": {"merakiplus": 1000}, "stall_s": 6},
                   "timeout", io_timeout=2)
    failed_install("asset 404", {"status": {"/v6.26/start.wav": 404}}, "notfound")
    failed_install("release changed since the check (latest is 6.25 now)", {"latest": "6.25"}, "changed")
    failed_install("not enough space", {}, "space", free=1000)
    srv.cfg["latest"] = "6.26"
    root = run.root(pending="6.24-ffffffff")
    rc, out, s = run(root, "install", "--version", "6.26")
    check("install: refused while another release is on trial", rc == EXIT["busy"] and s.get("error") == "trial", f"{rc} {s}")
    root = run.root(max_seq=4)
    rc, out, s = run(root, "install", "--version", "6.26")
    check("install: seq 4 <= max_seq 4 -> OLDER VERSION REFUSED", rc == EXIT["older"] and s.get("error") == "older", f"{rc} {s}")

    srv.cfg["url_len"] = {f: 2000 for f in [e["src"] for e in r26["manifest"]["release"]] + ["manifest.json", "manifest.sig", "notes.txt"]}
    root = run.root()
    rc, out, s = run(root, "install", "--version", "6.26")
    srv.cfg["url_len"] = {}
    check("install: every URL ~2 KB long (requests > 2100 bytes) -> staged, every file correct",
          rc == 0 and s.get("result") == "staged" and max(n for _, n in srv.sizes[-30:]) > 2100
          and (root / "new_customer/merakiplus/rel" / r26["name"] / "demo_v9").read_bytes() == (r26["dir"] / "merakiplus").read_bytes(), f"{rc} {s}")

    if not run.wrap:                     # host only: killing `docker run` does not reliably kill the container's process
        srv.cfg.update(stall={"merakiplus": 1000}, stall_s=10)
        root = run.root()
        before = (root / "new_customer/merakiplus/ota/state").read_text()
        env = of.helper_env(root, rig.pki, srv.port, rig.key, run.connect, io_timeout=30)
        pr = subprocess.Popen([str(run.exe), "install", "--version", "6.26"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 20 and not list((root / "new_customer/merakiplus/rel").glob(".stage-*/demo_v9")):
            time.sleep(0.05)
        time.sleep(0.5)
        pr.kill(); pr.wait()
        srv.cfg.update(stall={})
        stage = list((root / "new_customer/merakiplus/rel").glob(".stage-*"))
        check("helper killed mid-download: a partial stage is left, the state is untouched",
              bool(stage) and (root / "new_customer/merakiplus/ota/state").read_text() == before, str(stage))
        rc, out, s = run(root, "install", "--version", "6.26")
        check("...run again: staged, the partial stage replaced, no stage left", rc == 0 and s.get("result") == "staged"
              and not list((root / "new_customer/merakiplus/rel").glob(".stage-*"))
              and (root / "new_customer/merakiplus/rel" / r26["name"] / "demo_v9").read_bytes() == (r26["dir"] / "merakiplus").read_bytes(), f"{rc} {s}")

    srv.cfg["chunked"] = True
    root = run.root()
    rc, out, s = run(root, "install", "--version", "6.26")
    srv.cfg["chunked"] = False
    check("install: chunked transfer encoding decoded correctly", rc == 0 and s.get("result") == "staged"
          and (root / "new_customer/merakiplus/rel" / r26["name"] / "demo_v9").read_bytes() == (r26["dir"] / "merakiplus").read_bytes(), f"{rc} {s}")

    if not run.wrap:                     # host only: a flock taken on the Mac does not reach the container's VM
        root = run.root()
        import fcntl
        lk = open(root / "tmp/merakiplus_upd.lock", "w")
        fcntl.flock(lk, fcntl.LOCK_EX)
        rc, out, s = run(root, "check")
        lk.close()
        check("one updater at a time: a second run -> busy", rc == EXIT["busy"] and s.get("error") == "busy", f"{rc} {s}")
    root = run.root()
    (root / "new_customer/merakiplus/ota/state").unlink()
    rc, out, s = run(root, "install", "--version", "6.26")
    check("install: no release state (pre-1b install) -> refused, install from a computer", rc == EXIT["busy"] and s.get("error") == "launcher"
          and not (root / "new_customer/merakiplus/rel" / r26["name"]).exists(), f"{rc} {s}")


def lock_free(root):
    import fcntl
    with open(pathlib.Path(root) / "new_customer/merakiplus/ota/lock", "a") as f:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False


def static_checks():
    print("== production builds ==")
    for p in [PROD, ROOT / "build/merakiplus_upd", ROOT / "bin/merakiplus_upd"]:
        if p.is_file():
            b = p.read_bytes()
            check(f"{p.relative_to(ROOT)}: no test hooks (MERAKIPLUS_UPD_TEST_*, test root)",
                  b"MERAKIPLUS_UPD_TEST" not in b and b"MERAKIPLUS_UPD_ROOT" not in b)
            check(f"{p.relative_to(ROOT)}: only the pinned hosts, the fixed discovery URL",
                  b"https://github.com/" + of.REPO.encode() + b"/releases/latest/download/" in b and b"release-assets.githubusercontent.com" in b)
    if PROD.is_file():
        p = subprocess.run([str(PROD), "--selftest"], capture_output=True, text=True)
        check("production selftest (URL rules, clock floor, vectors)", p.returncode == 0, p.stdout[-300:])


def arm_wrap(work):
    """run the static ARM TEST build in the arm32v7 container against the host's TLS server"""
    def wrap(root, cmd, env):
        envs = []
        for k in ("MERAKIPLUS_UPD_TEST_CA", "MERAKIPLUS_UPD_TEST_CONNECT", "MERAKIPLUS_UPD_TEST_KEY", "MERAKIPLUS_UPD_TEST_NOW", "MERAKIPLUS_UPD_TEST_FREE",
                  "MERAKIPLUS_UPD_TEST_IO_TIMEOUT"):
            if k in env:
                envs += ["-e", f"{k}={env[k]}"]
        return ["docker", "run", "--rm", "--platform", "linux/arm/v7", "--user", f"{os.getuid()}:{os.getgid()}",
                "--add-host", "host.docker.internal:host-gateway", "-v", f"{work}:{work}", "-e", f"MERAKIPLUS_UPD_ROOT={root}", *envs,
                "arm32v7/debian:bullseye-slim", *cmd]
    return wrap


def build_arm_test(work):
    bs = ["-I" + str(ROOT / "third_party/bearssl/inc"), "-I" + str(ROOT / "third_party/bearssl/src")]
    tgt = ["-target", "arm-linux-musleabihf", "-mcpu=cortex_a7", "-static", "-O2", "-s"]
    obj = work / "crypto_arm.o"
    subprocess.run(["zig", "build-obj", str(ROOT / "src/merakiplus_crypto.zig"), "-target", "arm-linux-musleabihf", "-mcpu", "cortex_a7",
                    "-O", "ReleaseSmall", "-fstrip", "-fsingle-threaded", f"-femit-bin={obj}"], check=True)
    out = work / "upd_test_arm"
    srcs = sorted(str(p) for p in (ROOT / "third_party/bearssl/src").rglob("*.c"))
    subprocess.run(["zig", "cc", *tgt, *bs, "-DMERAKIPLUS_UPD_TEST", "-o", str(out), str(ROOT / "src/merakiplus_upd.c"),
                    str(ROOT / "src/merakiplus_upd_net.c"), str(obj), *srcs], check=True)
    return out


def main():
    if not TEST.is_file():
        sys.exit("build/merakiplus_upd_test missing — run ./build.sh host first")
    static_checks()
    rig = of.Rig()
    try:
        print("== host TEST build ==")
        scenario(Run(rig))
        if "--arm" in sys.argv:
            print("== static ARM TEST build in the arm32v7 container (32-bit) ==")
            arm = build_arm_test(rig.work)
            rig2 = of.Rig(pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-ota-arm-", dir=str(rig.work))))
            rig2.srv.close()
            rig2.srv = of.Server(rig2.pki, host="0.0.0.0")
            os.chmod(rig.work, 0o777)
            for p in rig.work.rglob("*"):
                if p.is_dir():
                    os.chmod(p, 0o777)
            shutil.copy(arm, rig2.work / "upd_test_arm")
            scenario(Run(rig2, exe=rig2.work / "upd_test_arm", connect="host.docker.internal", wrap=arm_wrap(rig.work)))
            rig2.srv.close()
    finally:
        rig.close()
    print(f"OTA FETCH TESTS: {N[0] - len(FAILS)}/{N[0]} passed" + (f"; FAILED: {FAILS}" if FAILS else ""))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
