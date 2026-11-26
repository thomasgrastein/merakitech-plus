#!/usr/bin/env python3
"""test_launcher.py — the launcher (src/merakiplus_boot.c, docs/ota-plan.md phase 1b) without hardware.

    python3 tests/test_launcher.py [--bin build/merakiplus_boot_host] [--arm]

1. Selftest of the binary (SHA-256 vectors, state round trip).
2. Vendor-watchdog model (tests/launcher_watchdog.sh): fake UIs that crash before Wi-Fi, crash-loop,
   hang, become healthy and crash later, or exit on purpose; the fallback chain new -> previous -> stock.
   --arm also runs it in the arm32v7 container with the static ARM build bin/merakiplus_boot (local only).
3. Power-cut replay: the launcher logs every file operation (MERAKIPLUS_BOOT_OPLOG). For every prefix of
   that log (a cut after operation k) the flash contents are rebuilt — keeping only what was fsynced, with
   unsynced file data truncated or replaced by junk, and with renames after the last directory sync either
   landed or not — and the launcher is started on the result (a new boot). It must always parse the state
   (old or new, never a mix) and start a complete, hash-correct release, the same one as without the cut.
"""
import argparse
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
OTA = "/new_customer/merakiplus/ota"
REL = "/new_customer/merakiplus/rel"
FAILS = []
N_CUTS = 0


def check(name, cond, detail=""):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + ("" if cond else f"  -- {detail}"))
    if not cond:
        FAILS.append(name)


def mkrel(root, name, body=b"#!/bin/sh\nexit 0\n"):
    import hashlib
    d = root / REL.lstrip("/") / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "demo_v9").write_bytes(body)
    (d / "demo_v9").chmod(0o755)
    (d / "MANIFEST").write_text(f"merakiplus-release 1\nversion {name}\nfile demo_v9 {len(body)} "
                                f"{hashlib.sha256(body).hexdigest()}\n")


def mkroot(tmp, state, healthy=None, rels=("6.21-c", "6.20-p", "6.22-n")):
    root = pathlib.Path(tempfile.mkdtemp(dir=tmp)).resolve()
    for d in ("customer", "tmp", "proc/sys/kernel/random", OTA.lstrip("/")):
        (root / d).mkdir(parents=True, exist_ok=True)
    (root / "customer/demo_v9").write_text("#!/bin/sh\nexit 0\n")
    (root / "customer/demo_v9").chmod(0o755)
    (root / "proc/sys/kernel/random/boot_id").write_text("boot-a\n")
    (root / "proc/uptime").write_text("50.00 1.00\n")
    for r in rels:
        mkrel(root, r)
    (root / OTA.lstrip("/") / "state").write_text(state)
    if healthy:
        (root / OTA.lstrip("/") / "healthy").write_text(healthy)
    return root


def run(binary, root, *args, env=None):
    e = dict(os.environ, MERAKIPLUS_BOOT_ROOT=str(root), **(env or {}))
    p = subprocess.run([str(binary), *args], env=e, capture_output=True, text=True, timeout=30)
    return p.returncode, p.stdout, p.stderr


def decision(binary, root):
    rc, out, err = run(binary, root, "--dry")
    ex = [l for l in out.splitlines() if l.startswith("exec ")]
    return rc, (ex[-1] if ex else None), out + err


def snapshot(root):
    d = root / OTA.lstrip("/")
    return {"/" + str(p.relative_to(root)): p.read_bytes() for p in d.iterdir() if p.is_file()}


def parse_oplog(path, root):
    ops = []
    for ln in pathlib.Path(path).read_text().splitlines():
        kind, rest = ln.split(" ", 1)
        if kind in ("W", "A", "R"):
            p, hx = rest.split(" ", 1)
            data = bytes.fromhex(hx)
            if kind == "R":
                data = data.decode()[len(str(root)):]
            ops.append((kind, p, data))
        else:
            ops.append((kind, rest, None))
    return [o for o in ops if o[1].startswith(OTA) or (o[0] == "R" and o[2].startswith(OTA))]


def rebuild(initial, ops, k, landed, junk):
    """Flash contents after a cut behind operation k (ops[:k] happened)."""
    files = {p: (b, True) for p, b in initial.items()}
    pre = ops[:k]
    if not landed:                                 # names (renames) only up to the last directory sync
        last_d = max([i for i, o in enumerate(pre) if o[0] == "D"], default=-1)
        pre = [o for i, o in enumerate(pre) if i <= last_d or o[0] in ("W", "F", "A")]
    for kind, p, data in pre:
        if kind == "W":
            files[p] = (data, False)
        elif kind == "A":
            old = files.get(p, (b"", True))[0]
            files[p] = (old + data, False)
        elif kind == "F" and p in files:
            files[p] = (files[p][0], True)
        elif kind == "R" and p in files:
            files[data] = files.pop(p)
    out = {}
    rnd = random.Random(k * 7 + landed)
    for p, (b, synced) in files.items():
        out[p] = b if synced else (bytes(rnd.randrange(256) for _ in range(len(b))) if junk else b"")
    return out


def replay(binary, tmp, name, state, healthy=None, env=None, expect_state=None, prep=None):
    """Run the launcher once (real exec of a fake UI), then replay every cut point."""
    global N_CUTS
    root = mkroot(tmp, state, healthy)
    if prep:
        prep(root)
    initial = snapshot(root)
    _, want, _ = decision(binary, root)               # what the very next start does (no cut)
    log = root.parent / (root.name + ".oplog")
    rc, _, err = run(binary, root, env=dict(env or {}, MERAKIPLUS_BOOT_OPLOG=str(log)))
    ops = parse_oplog(log, root)
    final = snapshot(root)
    new_state = final.get(OTA + "/state")
    old_state = initial.get(OTA + "/state")
    if expect_state is not None:
        check(f"{name}: the start wrote the expected state", new_state is not None and expect_state in new_state.decode(),
              new_state)
    _, after, _ = decision(binary, root)               # the start after a clean run
    bad = []
    cuts = 0
    for k in range(len(ops) + 1):
        for landed in (True, False):
            for junk in (False, True):
                flash = rebuild(initial, ops, k, landed, junk)
                r = root.parent / f"{root.name}.cut"
                if r.exists():
                    shutil.rmtree(r)
                shutil.copytree(root, r, symlinks=True)
                o = r / OTA.lstrip("/")
                shutil.rmtree(o)
                o.mkdir()
                for p, b in flash.items():
                    (r / p.lstrip("/")).write_bytes(b)
                shutil.rmtree(r / "tmp")
                (r / "tmp").mkdir()
                (r / "proc/sys/kernel/random/boot_id").write_text(f"boot-cut-{k}\n")
                st = flash.get(OTA + "/state")
                if st not in (old_state, new_state):
                    bad.append(f"cut {k} landed={landed} junk={junk}: state is neither old nor new: {st!r}")
                rc2, d, out = decision(binary, r)
                if rc2 != 0 or d not in (want, after):
                    bad.append(f"cut {k} landed={landed} junk={junk}: rc={rc2} start '{d}', expected '{want}' or '{after}'\n{out}")
                cuts += 1
    N_CUTS += cuts
    check(f"{name}: {len(ops)} file operations, {cuts} cut states replayed -> state old-or-new, same release started",
          not bad and rc == 0 and len(ops) > 0, "\n".join(bad[:4]) or f"rc={rc} ops={len(ops)} {err}")
    return root


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=str(ROOT / "build" / "merakiplus_boot_host"))
    ap.add_argument("--arm", action="store_true", help="also run the watchdog model in the arm32v7 container")
    a = ap.parse_args()
    binary = pathlib.Path(a.bin).resolve()
    if not binary.is_file():
        sys.exit(f"{binary} missing — run ./build.sh host first")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-launcher-")).resolve()

    print("== launcher selftest")
    p = subprocess.run([str(binary), "--selftest", str(tmp / "selftest")], capture_output=True, text=True)
    check("selftest " + p.stdout.strip(), p.returncode == 0, p.stderr)

    print("== vendor-watchdog model (host build)")
    p = subprocess.run(["sh", str(HERE / "launcher_watchdog.sh"), str(binary), str(tmp / "wd")], capture_output=True, text=True)
    print("\n".join("  " + l for l in p.stdout.splitlines() if "[FAIL]" in l or "MODEL" in l))
    check("watchdog model: all scenarios", p.returncode == 0, p.stdout[-2000:])
    if a.arm:
        print("== vendor-watchdog model (static ARM build, arm32v7 container)")
        w = tmp / "arm"
        w.mkdir()
        shutil.copy(ROOT / "bin" / "merakiplus_boot", w / "boot")
        shutil.copy(HERE / "launcher_watchdog.sh", w / "wd.sh")
        os.chmod(w, 0o777)
        p = subprocess.run(["docker", "run", "--rm", "--network", "none", "--platform", "linux/arm/v7", "--user", "1000:1000",
                            "-v", f"{w}:/w", "arm32v7/debian:bullseye-slim", "sh", "-c",
                            "getconf LONG_BIT; /w/boot --selftest /tmp/st && sh /w/wd.sh /w/boot /tmp/wd"],
                           capture_output=True, text=True)
        print("\n".join("  " + l for l in p.stdout.splitlines() if "[FAIL]" in l or "MODEL" in l or "selftest" in l))
        check("ARM (32-bit) build: selftest + watchdog model", p.returncode == 0 and p.stdout.startswith("32"), p.stdout[-1500:] + p.stderr[-500:])

    print("== power-cut replay (every prefix of the launcher's operation log)")
    st = "format=1\ncurrent=6.21-c\nprevious=6.20-p\npending=%s\ntries=%d\nlast=\nmax_seq=27\n"
    replay(binary, tmp, "trial start (tries 1 -> 2)", st % ("6.22-n", 1), expect_state="tries=2\n")
    replay(binary, tmp, "trial commit after healthy", st % ("6.22-n", 2), healthy="boot=x\npid=1\nrel=6.22-n\n",
           expect_state="current=6.22-n\nprevious=6.21-c\npending=\n")
    replay(binary, tmp, "trial rollback after 3 failed starts", st % ("6.22-n", 3),
           expect_state="pending=\ntries=0\nlast=rolled_back:6.22-n:crashloop\nmax_seq=27\n")
    # the on-device updater's trial_seq line (src/merakiplus_upd.c) must survive both outcomes unchanged: the updater counts
    # it for anti-downgrade only once current=<that release> (healthy commit), never after a rollback
    stt = st + "trial_seq=6.22-n 28\n"
    replay(binary, tmp, "trial commit keeps trial_seq", stt % ("6.22-n", 2), healthy="boot=x\npid=1\nrel=6.22-n\n",
           expect_state="current=6.22-n\nprevious=6.21-c\npending=\ntries=0\nlast=ok:6.22-n\nmax_seq=27\ntrial_seq=6.22-n 28\n")
    replay(binary, tmp, "trial rollback keeps trial_seq", stt % ("6.22-n", 3),
           expect_state="pending=\ntries=0\nlast=rolled_back:6.22-n:crashloop\nmax_seq=27\ntrial_seq=6.22-n 28\n")
    def damage(root):
        (root / REL.lstrip("/") / "6.21-c/demo_v9").write_bytes(b"#!/bin/sh\nexit 1\n# damaged\n")
    root = replay(binary, tmp, "damaged current (log append only)", st % ("", 0), prep=damage)
    rc, d, _ = decision(binary, root)
    check("damaged current: the next start is previous", d == "exec /new_customer/merakiplus/rel/6.20-p/demo_v9 trial=0", d)
    replay(binary, tmp, "first install, trial start with no previous", "format=1\ncurrent=\nprevious=\npending=6.22-n\ntries=0\nlast=\n",
           expect_state="pending=6.22-n\ntries=1\n")
    print(f"\nLAUNCHER TESTS: {'ALL PASS' if not FAILS else str(len(FAILS)) + ' FAIL: ' + ', '.join(FAILS)}"
          f"  ({N_CUTS} power-cut states replayed)")
    shutil.rmtree(tmp, ignore_errors=True)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
