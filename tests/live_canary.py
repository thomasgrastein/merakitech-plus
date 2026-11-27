#!/usr/bin/env python3
"""live_canary.py — the on-device updater against the REAL GitHub, with only the pinned roots (weekly CI: live-canary.yml).

    ./build.sh host && python3 tests/live_canary.py

The machines can only fetch the release that would fix a broken update channel through that same channel. This canary
catches, before users do, what the local TLS tests cannot: GitHub or its asset CDN moving to a CA outside keys/tls/,
dropping TLS 1.2 / our cipher suites, a redirect to a host that is not pinned, or signed asset URLs that grow towards
the updater's URL limit. It runs:

  1. `check` with the PRODUCTION host build (build/merakiplus_upd_host: no test hooks, only the pinned roots and the
     built-in signing keys) -> must report a newer signed release (an empty display state) and read its notes;
  2. `install --version <latest>` with the TEST build in a temporary root (MERAKIPLUS_UPD_ROOT only — no test CA, no
     redirection, no test key: same pinned roots and keys) -> every release file is downloaded, size + SHA-256 checked
     against the signed manifest and staged;
  3. the length of every signed asset URL GitHub hands out now for the release's assets (one redirect, not followed):
     fails at 3/4 of the limit.

Read-only towards GitHub (anonymous GETs of public release files); writes only below a temp directory and /tmp.
"""
import http.client
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "tools"))
import merakiplus as mp  # noqa: E402

PROD = ROOT / "build" / "merakiplus_upd_host"
TEST = ROOT / "build" / "merakiplus_upd_test"
URL_LIMIT = 4095                    # src/merakiplus_upd_net.c URL_MAX - 1
FAILS = []


def check(name, cond, detail=""):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + ("" if cond else f"  -- {detail}"))
    if not cond:
        FAILS.append(name)


def status(p):
    try:
        return dict(ln.split("=", 1) for ln in pathlib.Path(p).read_text().splitlines() if "=" in ln)
    except FileNotFoundError:
        return {}


def location(url):
    """the Location of one GET (not followed) — measured with Python's TLS (system roots); the helper validates itself"""
    u = urllib.parse.urlsplit(url)
    c = http.client.HTTPSConnection(u.netloc, timeout=30)
    c.request("GET", u.path + ("?" + u.query if u.query else ""), headers={"User-Agent": "merakiplus-canary"})
    r = c.getresponse()
    loc = r.getheader("Location")
    c.close()
    return r.status, loc


def main():
    for b in (PROD, TEST):
        if not b.is_file():
            sys.exit(f"{b.relative_to(ROOT)} missing — run ./build.sh host first")
    clean = {k: v for k, v in os.environ.items() if not k.startswith("MERAKIPLUS_")}
    with tempfile.TemporaryDirectory(prefix="merakiplus-canary-") as tmp:
        tmp = pathlib.Path(tmp)

        print("== 1. check (production build, pinned roots only) ==")
        st = tmp / "check.status"
        p = subprocess.run([str(PROD), "check", "--status", str(st)], env=clean, capture_output=True, text=True, timeout=180)
        s = status(st)
        print("  " + (p.stdout.strip() or p.stderr.strip()))
        check("check: TLS to github.com and the asset host, signature and format OK -> a signed release is available",
              p.returncode == 0 and s.get("result") == "available" and s.get("version"), f"rc={p.returncode} {s}")
        ver = s.get("version", "")
        if not ver:
            return finish()
        # the asset list of the live signed manifest (the helper verified its signature above)
        with urllib.request.urlopen(urllib.request.Request(mp.RELEASE_DL.format(tag="v" + ver, name="manifest.json"),
                                                           headers={"User-Agent": "merakiplus-canary"}), timeout=60) as r:
            assets = [a["name"] for a in json.loads(r.read())["assets"]]
        if "notes.txt" in assets:                       # releases after 6.25 carry the display's notes
            check("check: the release's notes (notes.txt) were fetched", bool(s.get("notes1")), str(s))

        print(f"== 2. install --version {ver} (test build in a temp root: still only the pinned roots + built-in keys) ==")
        root = tmp / "root"
        (root / "tmp").mkdir(parents=True)
        (root / "new_customer/merakiplus/ota").mkdir(parents=True)
        (root / "new_customer/merakiplus/rel").mkdir(parents=True)
        (root / "new_customer/merakiplus/ota/state").write_text(
            "format=1\ncurrent=0.0-00000000\nprevious=\npending=\ntries=0\nlast=\nmax_seq=0\n")
        # the release must match the installed launcher (platform section of the signed manifest): use the release's own
        boot = tmp / "merakiplus_boot"
        rc = subprocess.run(["curl", "-fsSL", "-o", str(boot), mp.RELEASE_DL.format(tag="v" + ver, name="merakiplus_boot")]).returncode
        check("the release's launcher downloaded (for the launcher match)", rc == 0 and boot.is_file())
        if rc == 0:
            (root / "new_customer/demo_v9").write_bytes(boot.read_bytes())
        env = dict(clean, MERAKIPLUS_UPD_ROOT=str(root))
        p = subprocess.run([str(TEST), "install", "--version", ver], env=env, capture_output=True, text=True, timeout=600)
        s = status(root / "tmp/merakiplus_upd.status")
        print("  " + (p.stdout.strip() or p.stderr.strip()))
        name = s.get("name", "")
        rel = root / "new_customer/merakiplus/rel" / name
        check("install: every release file downloaded over HTTPS, size + SHA-256 = signed manifest, staged as the trial",
              p.returncode == 0 and s.get("result") == "staged" and name and (rel / "MANIFEST").is_file()
              and f"pending={name}\n" in (root / "new_customer/merakiplus/ota/state").read_text(), f"rc={p.returncode} {s}")

        print("== 3. signed asset URL lengths (limit: %d bytes) ==" % URL_LIMIT)
        names = ["manifest.json", "manifest.sig"] + sorted(assets)
        longest = 0
        for n in names:
            code, loc = location(mp.RELEASE_DL.format(tag="v" + ver, name=n))
            host = urllib.parse.urlsplit(loc or "").hostname or "-"
            print(f"  {n:22s} {code} -> {host} ({len(loc or '')} bytes)")
            check(f"{n}: redirected to a pinned asset host",
                  code in (301, 302, 303, 307, 308) and loc and loc.startswith("https://")
                  and host in ("release-assets.githubusercontent.com", "objects.githubusercontent.com"), f"{code} {host}")
            longest = max(longest, len(loc or ""))
        check(f"longest signed asset URL {longest} bytes < 3/4 of the updater's limit ({URL_LIMIT * 3 // 4})",
              longest < URL_LIMIT * 3 // 4, str(longest))
    return finish()


def finish():
    print("LIVE CANARY: " + ("OK" if not FAILS else f"{len(FAILS)} FAILED: {FAILS}"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
