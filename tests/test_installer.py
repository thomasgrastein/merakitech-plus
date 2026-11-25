#!/usr/bin/env python3
"""End-to-end tests of tools/merakiplus.py (install / re-install / revert / dry-run / migration of
a pre-rename install; phase 1a hardening: sync-before-rename, SHA-256, health check, vendor-update
detection, leftovers, free space; phase 1b: release directories + launcher, migration of a v6.20 layout,
exec-reload, deferral while busy, switcher refresh, installer power-cut replay) against
tests/mock_device.py. No hardware, no network beyond 127.0.0.1. The launcher's decisions come from its
host build (build/merakiplus_boot_host, made by ./build.sh host).

    python3 tests/test_installer.py
"""
import hashlib
import io
import contextlib
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(HERE))
import merakiplus as mp          # noqa: E402
import mock_device as md         # noqa: E402
import sigtest                   # noqa: E402

# hermetic: the installer looks for the release's signed manifest on GitHub; here it finds none unless a test
# passes a --bin-dir that holds one (signed with a throwaway TEST key, see the "phase 2" scenario)
mp.RELEASE_DL = "file:///nonexistent-merakiplus-test/{tag}/{name}"

FAILS = []
HOST_LAUNCHER = ROOT / "build" / "merakiplus_boot_host"
if HOST_LAUNCHER.is_file():
    import os
    os.environ["MERAKIPLUS_TEST_LAUNCHER_HOST"] = str(HOST_LAUNCHER)
else:
    sys.exit("build/merakiplus_boot_host missing — run ./build.sh host first")


def check(name, cond, detail=""):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + ("" if cond else f"  -- {detail}"))
    if not cond:
        FAILS.append(name)


def md5f(p):
    return hashlib.md5(pathlib.Path(p).read_bytes()).hexdigest()


def tree_digest(root):
    h = hashlib.md5()
    for p in sorted(pathlib.Path(root).rglob("*")):
        if p.is_file() and not p.is_symlink() and p.name not in ("events", ".mockfuncs.sh"):
            h.update(str(p.relative_to(root)).encode() + p.read_bytes())
    return h.hexdigest()


def run(srv, root, *args, unsigned=True):
    """unsigned=True: installs pass --allow-unsigned (the repo's bin/ has no signed manifest here; the signed-by-default
    behaviour itself is tested in the "phase 2" scenario with unsigned=False)."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mp.main([args[0], "--ip", "127.0.0.1", "--port", str(srv.port), "--test-prefix", str(root),
                      "--yes", "--log", str(root.parent / "test.log")] +
                     (["--bin-dir", str(ROOT / "bin"), "--health-seconds", "2"] if args[0] == "install" else []) +
                     (["--allow-unsigned"] if args[0] == "install" and unsigned else []) +
                     list(args[1:]))
    out = buf.getvalue()
    if rc != 0:
        print(out)
    return rc, out


def ui(root):
    res = []
    for d in sorted((root / "proc").iterdir()):
        ep = d / "exe_path"
        if ep.exists() and ep.read_text().endswith("/demo_v9"):
            res.append((d.name, ep.read_text()[len(str(root)):]))
    return res


def scenario(name, kind, steps):
    print(f"== {name} ==")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-test-")).resolve()
    root = md.make_root(tmp / "dev", kind)
    srv = md.MockServer(root)
    try:
        steps(srv, root)
    finally:
        srv.close()


MERAKIPLUS = mp.BIN_MD5["merakiplus"]
LAUNCHER = mp.BIN_MD5["merakiplus_boot"]
REL = mp.rel_name(mp.VERSION, (ROOT / "bin/merakiplus").read_bytes())
REL_EXE = f"{mp.REL_DIR}/{REL}/demo_v9"


def ota(root):
    p = root / mp.OTA_STATE.lstrip("/")
    return mp.kv_text(p.read_text()) if p.exists() else {}


def rel_ok(root, name=REL):
    """the release directory is complete: every file listed in its MANIFEST has the listed size + sha256"""
    d = root / mp.REL_DIR.lstrip("/") / name
    if not (d / "MANIFEST").is_file():
        return False
    for ln in (d / "MANIFEST").read_text().splitlines():
        if ln.startswith("file "):
            _, n, size, h = ln.split()
            f = d / n
            if not f.is_file() or f.stat().st_size != int(size) or hashlib.sha256(f.read_bytes()).hexdigest() != h:
                return False
    return True


def stock_a(srv, root):
    orig_sh = md5f(root / "customer/demo.sh")
    orig_a = md5f(root / "customer/demo_v9")
    orig_b = md5f(root / "new_customer/demo_v9")

    before = tree_digest(root)
    rc, out = run(srv, root, "install", "--dry-run")
    check("dry-run install exits 0", rc == 0)
    check("dry-run changes nothing on the device", tree_digest(root) == before)

    rc, out = run(srv, root, "install")
    check("install exits 0", rc == 0)
    check("slot B now holds the launcher", md5f(root / "new_customer/demo_v9") == LAUNCHER)
    check("the release directory is complete (MANIFEST: size + sha256 of every file)", rel_ok(root)
          and md5f(root / REL_EXE.lstrip("/")) == MERAKIPLUS)
    check("healthy -> committed: current = the new release, no trial, no previous (first install)",
          ota(root).get("current") == REL and not ota(root).get("pending") and not ota(root).get("previous")
          and ota(root).get("last") == "ok:" + REL, ota(root))
    check("vendor slot-B backup is byte-identical", md5f(root / "new_customer/merakiplus/boot/demo_v9.vendor") == orig_b)
    check("demo.sh backup is byte-identical", md5f(root / "new_customer/merakiplus/boot/demo.sh.bak") == orig_sh)
    check("demo.sh untouched without --gesture", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot A untouched", md5f(root / "customer/demo_v9") == orig_a)
    check("boot flag NOT written without --persist", not (root / "customer/new").exists())
    check("exactly one UI runs: the release, started by the launcher", [e for _, e in ui(root)] == [REL_EXE], ui(root))
    check("old stock UI was stopped (SIGSTOP first, then killed)",
          "kill -STOP 600" in (root / "events").read_text() and not (root / "proc/600").exists())
    check("state records original flag=0", "orig_flag=0" in (root / "new_customer/merakiplus/boot/merakiplus.state").read_text())
    snd = root / mp.REL_DIR.lstrip("/") / REL / "merakiplus_snd"
    aud = root / mp.REL_DIR.lstrip("/") / REL / "audio"
    check("v6.21: no flat-layout files on a fresh install", not (root / mp.SND_HELPER.lstrip("/")).exists()
          and not (root / mp.STAGE.lstrip("/")).exists())
    check("v6.18: sound helper installed in the release (md5-gated, executable)",
          snd.is_file() and md5f(snd) == mp.BIN_MD5["merakiplus_snd"] and (snd.stat().st_mode & 0o111) != 0)
    check("v6.18: our three sounds installed, byte-identical to audio/",
          sorted(p.name for p in aud.iterdir()) == sorted(mp.AUDIO_MD5)
          and all(md5f(aud / n) == h and md5f(ROOT / "audio" / n) == h for n, h in mp.AUDIO_MD5.items()))
    check("v6.18: no vendor audio touched (/customer/audio unchanged or absent)",
          not (root / "customer/audio").exists() or all(p.suffix == ".mp3" for p in (root / "customer/audio").iterdir()))

    pid_before = ui(root)
    rc, out = run(srv, root, "install")
    check("re-install is idempotent (exit 0)", rc == 0)
    check("re-install: nothing re-uploaded, same UI process", "already" in out and ui(root) == pid_before, out[-400:])

    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("install --gesture --persist exits 0", rc == 0)
    sh = (root / "customer/demo.sh").read_text()
    check("demo.sh has exactly one guard line", sum(mp.GUARD_KEY in l for l in sh.splitlines()) == 1)
    lines = sh.splitlines()
    gi = next(i for i, l in enumerate(lines) if mp.GUARD_KEY in l)
    check("guard line sits right before the boot slot branch", lines[gi + 1] == mp.ANCHOR and "starting from B" in lines[gi + 2])
    check("only that one line was added", [l for l in lines if mp.GUARD_KEY not in l] ==
          (root / "new_customer/merakiplus/boot/demo.sh.bak").read_text().splitlines())
    check("switcher + shim installed", md5f(root / "new_customer/merakiplus/boot/switcher") == mp.BIN_MD5["switcher"]
          and md5f(root / "new_customer/merakiplus/boot/grab_shim.so") == mp.BIN_MD5["grab_shim.so"])
    check("boot flag set by --persist", (root / "customer/new").exists())
    check("backups were not overwritten", md5f(root / "new_customer/merakiplus/boot/demo.sh.bak") == orig_sh)

    before = tree_digest(root)
    rc, out = run(srv, root, "revert", "--dry-run")
    check("dry-run revert changes nothing", rc == 0 and tree_digest(root) == before)

    rc, out = run(srv, root, "revert")
    check("revert exits 0", rc == 0)
    check("demo.sh byte-identical to the original", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot B vendor binary restored", md5f(root / "new_customer/demo_v9") == orig_b)
    check("boot flag removed (it was not set originally)", not (root / "customer/new").exists())
    check("switcher removed", not (root / "new_customer/merakiplus/boot/switcher").exists())
    check("v6.18: sound helper and sounds removed by revert",
          not (root / mp.SND_HELPER.lstrip("/")).exists() and not (root / mp.AUDIO_DIR.lstrip("/")).exists())
    check("v6.21: release directories and the launcher state removed by revert, backups kept",
          not (root / mp.REL_DIR.lstrip("/")).exists() and not (root / mp.OTA_DIR.lstrip("/")).exists()
          and (root / mp.BAK_B.lstrip("/")).exists())
    check("stock UI running from slot A, nothing else", [e for _, e in ui(root)] == ["/customer/demo_v9"], ui(root))
    check("slot A still untouched", md5f(root / "customer/demo_v9") == orig_a)

    rc, out = run(srv, root, "revert")
    check("second revert is a no-op", rc == 0 and "nothing to revert" in out, out[-300:])


def vendor_b(srv, root):
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, out = run(srv, root, "install")
    check("install on a B-booted device exits 0", rc == 0)
    check("activated through the watchdog exec-reload", "watchdog exec-reload" in out)
    check("one UI, Meraki+ from slot B (launcher -> release)", [e for _, e in ui(root)] == [REL_EXE]
          and md5f(root / "new_customer/demo_v9") == LAUNCHER)
    check("flag (set by the vendor) left alone", (root / "customer/new").exists())
    rc, out = run(srv, root, "revert")
    check("revert exits 0", rc == 0)
    check("vendor slot B restored", md5f(root / "new_customer/demo_v9") == orig_b)
    check("original flag restored (vendor B boots again)", (root / "customer/new").exists())


def empty_b(srv, root):
    rc, out = run(srv, root, "install")
    check("install with an empty slot B exits 0", rc == 0)
    check("empty slot B recorded", (root / "new_customer/merakiplus/boot/demo_v9.vendor.absent").exists())
    rc, out = run(srv, root, "revert", "--purge")
    check("revert --purge exits 0", rc == 0)
    check("slot B empty again", not (root / "new_customer/demo_v9").exists())
    check("/new_customer/merakiplus purged", not (root / "new_customer/merakiplus").exists())
    check("stock UI running", [e for _, e in ui(root)] == ["/customer/demo_v9"])


def make_legacy(root):
    """Turn a current install (made by the test before) into the pre-rename layout an older
    release left behind: old directory, old staged binary name, old guard line, old binary marker,
    a dev run.sh with the old env prefix, plus language / schedule / settings-backup files."""
    new, old = root / mp.OURDIR.lstrip("/"), root / mp.LEGACY_DIR.lstrip("/")
    new.rename(old)
    import shutil
    for d in ("rel", "ota"):                      # releases + launcher state did not exist before the rename
        shutil.rmtree(old / d, ignore_errors=True)
    fake = b"\x7fELF FAKE PRE-RENAME UI " + mp.LEGACY_MARKS[0].encode() + b"V6.16\n" * 40
    (old / mp.LEGACY_ID).write_bytes(fake)        # its staged copy
    (root / "new_customer/demo_v9").write_bytes(fake)
    for d in (root / "proc").iterdir():           # the running UI is that old build
        if (d / "exe_path").exists() and (d / "exe_path").read_text().endswith("/new_customer/demo_v9"):
            (d / "image").write_bytes(fake)
    sh = root / "customer/demo.sh"
    sh.write_text(sh.read_text().replace(mp.GUARD_LINE, mp.GUARD_LINE.replace(mp.OURDIR, mp.LEGACY_DIR)))
    (old / "lang").write_text("da\n")
    (old / "schedule").write_text("preheat=1\nat=390\n")
    (old / "cfgbak").mkdir(exist_ok=True)
    (old / "cfgbak/config_writes.log").write_text("synthetic\n")
    (old / "run.sh").write_text(f"#!/bin/sh\n{mp.LEGACY_ENV_PREFIX}NOBOOTPUSH=1 ./{mp.LEGACY_ID}\n")


def no_legacy_left(root):
    old_id = mp.LEGACY_ID.encode()
    hits = [str(p) for p in (root / "new_customer").rglob("*") if old_id in p.name.encode()]
    hits += [str(p) for p in (root / "new_customer").rglob("*")
             if p.is_file() and p.name not in ("demo_v9.vendor",) and old_id in p.read_bytes()]
    return hits + (["demo.sh"] if old_id in (root / "customer/demo.sh").read_bytes() else [])


def legacy_upgrade(srv, root):
    orig_sh = md5f(root / "customer/demo.sh")
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, _ = run(srv, root, "install", "--gesture", "--persist")
    check("setup: current install", rc == 0)
    make_legacy(root)
    new = root / mp.OURDIR.lstrip("/")
    check("setup: pre-rename layout in place", not new.exists() and (root / mp.LEGACY_DIR.lstrip("/")).is_dir())

    before = tree_digest(root)
    rc, out = run(srv, root, "install", "--dry-run")
    check("dry-run on a pre-rename install changes nothing", rc == 0 and tree_digest(root) == before, out[-400:])
    check("dry-run reports the migration", "Migrating an install from before the rename" in out)
    check("dry-run finds the old backups (no new vendor backup planned)", "slot-B backup already exists" in out, out[-600:])

    rc, out = run(srv, root, "install")
    check("install over a pre-rename install exits 0", rc == 0)
    check("old directory gone", not (root / mp.LEGACY_DIR.lstrip("/")).exists())
    check("backups moved, byte-identical", md5f(new / "boot/demo_v9.vendor") == orig_b
          and md5f(new / "boot/demo.sh.bak") == orig_sh)
    check("state moved (original flag=0 kept)", "orig_flag=0" in (new / "boot/merakiplus.state").read_text())
    check("language, schedule and settings backups moved",
          (new / "lang").read_text() == "da\n" and (new / "schedule").read_text().startswith("preheat=1")
          and (new / "cfgbak/config_writes.log").exists())
    check("switcher + shim moved", (new / "boot/switcher").exists() and (new / "boot/grab_shim.so").exists())
    check("old staged binary removed, the release directory installed", not (new / mp.LEGACY_ID).exists()
          and not (root / mp.STAGE.lstrip("/")).exists() and rel_ok(root))
    check("run.sh rewritten (new env prefix + binary name)",
          (new / "run.sh").read_text() == f"#!/bin/sh\n{mp.ENV_PREFIX}NOBOOTPUSH=1 ./merakiplus\n")
    sh = (root / "customer/demo.sh").read_text().splitlines()
    check("demo.sh: exactly one guard line, the current one", [l for l in sh if "/boot/switcher" in l] == [mp.GUARD_LINE])
    check("demo.sh otherwise unchanged", [l for l in sh if l != mp.GUARD_LINE] ==
          (new / "boot/demo.sh.bak").read_text().splitlines())
    check("slot B upgraded to the launcher + current build, running", md5f(root / "new_customer/demo_v9") == LAUNCHER
          and [e for _, e in ui(root)] == [REL_EXE])
    check("no pre-rename identifier left on the device", not no_legacy_left(root), no_legacy_left(root))

    rc, out = run(srv, root, "install")
    check("second install after the migration is idempotent", rc == 0 and "Migrating" not in out)
    rc, out = run(srv, root, "revert")
    check("revert after the migration exits 0", rc == 0)
    check("demo.sh byte-identical to the original", md5f(root / "customer/demo.sh") == orig_sh)
    check("vendor slot B restored", md5f(root / "new_customer/demo_v9") == orig_b)


def legacy_revert(srv, root):
    orig_sh = md5f(root / "customer/demo.sh")
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, _ = run(srv, root, "install", "--gesture", "--persist")
    check("setup: current install", rc == 0)
    make_legacy(root)

    before = tree_digest(root)
    rc, out = run(srv, root, "revert", "--dry-run")
    check("dry-run revert on a pre-rename install changes nothing", rc == 0 and tree_digest(root) == before)

    rc, out = run(srv, root, "revert")
    check("revert of a pre-rename install exits 0", rc == 0)
    check("old build recognised by its legacy marker and stopped", "stock UI running again" in out, out[-600:])
    check("demo.sh byte-identical to the original (old guard line removed)", md5f(root / "customer/demo.sh") == orig_sh)
    check("vendor slot B restored from the moved backup", md5f(root / "new_customer/demo_v9") == orig_b)
    check("boot flag removed", not (root / "customer/new").exists())
    check("stock UI running from slot A", [e for _, e in ui(root)] == ["/customer/demo_v9"], ui(root))
    check("old directory migrated", not (root / mp.LEGACY_DIR.lstrip("/")).exists()
          and (root / mp.OURDIR.lstrip("/") / "lang").exists())


def legacy_purge(srv, root):
    rc, _ = run(srv, root, "install", "--gesture")
    check("setup: current install", rc == 0)
    make_legacy(root)
    (root / mp.OURDIR.lstrip("/")).mkdir()          # both layouts present (interrupted migration)
    (root / mp.OURDIR.lstrip("/") / "lang").write_text("en\n")
    rc, out = run(srv, root, "revert", "--purge")
    check("revert --purge exits 0", rc == 0)
    check("merge kept the newer file", "merged" in out)
    check("both directories purged", not (root / mp.OURDIR.lstrip("/")).exists()
          and not (root / mp.LEGACY_DIR.lstrip("/")).exists())
    check("stock UI running", [e for _, e in ui(root)] == ["/customer/demo_v9"])


def renames_after_sync(root):
    """Every rename of a staged file (.part / .tmp) must come right after a sync (data on flash first)."""
    ev = [l for l in (root / "events").read_text().splitlines() if l.split(" ", 1)[0] in ("sync", "mv", "chmod", "cp")]
    renames = [i for i, l in enumerate(ev) if l.startswith("mv ") and
               any(k in l for k in (".part ", ".tmp ", ".tmp.I ", "/.stage-", "/.demo_v9.launcher "))]
    bad = [ev[i] for i in renames if i == 0 or ev[i - 1] != "sync"]
    return len(renames), bad


def hardening(srv, root):
    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("1a: install exits 0", rc == 0, out[-600:])
    n, bad = renames_after_sync(root)
    check(f"1a/1b: every staged file, release directory, state file and the launcher is synced before its rename ({n} renames)",
          n >= 12 and not bad, bad)
    check("1a: files verified by sha256 on the computer", "sha256 " + mp.BIN_SHA256["merakiplus"][:16] in out)
    check("1b: health check: the UI's own healthy marker (this boot, this pid) + live controller link",
          "Meraki+ reports healthy" in out and "controller link live" in out and "healthy and running" in out, out[-500:])
    state = (root / mp.STATE.lstrip("/")).read_text()
    check("1a: state records the slot-A and demo.sh baselines",
          f"a_md5={md5f(root / 'customer/demo_v9')}" in state and "sh_md5=" in state, state)
    rc, out = run(srv, root, "status")
    check("1a: status after install: no vendor traces", rc == 0 and "vendor update      : no traces" in out, out[-500:])

    # leftovers of an interrupted run + an old development build
    ours = root / mp.OURDIR.lstrip("/")
    (ours / "merakiplus_snd.part").write_bytes(b"half")
    (ours / "boot/.demo_v9.vendor.tmp").write_bytes(b"half")
    (ours / "merakiplus_v6_17").write_bytes(b"\x7fELF old build " + mp.MERAKIPLUS_MARK.encode() + b"V6.17\n")
    (ours / "notes.txt").write_text("user file, not ours\n")
    before = tree_digest(root)
    rc, out = run(srv, root, "status")
    check("1a: status reports 2 interrupted copies and 1 old build, read-only",
          "interrupted copies : 2" in out and "merakiplus_v6_17" in out and "notes.txt" not in out
          and tree_digest(root) == before, out[-600:])
    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("1a: install removes the temp files, keeps the old build with --yes, never touches other files",
          rc == 0 and not (ours / "merakiplus_snd.part").exists() and not (ours / "boot/.demo_v9.vendor.tmp").exists()
          and (ours / "merakiplus_v6_17").exists() and (ours / "notes.txt").exists(), out[-600:])

    # free space is checked for updates too
    (root / "df_kb").write_text("400\n")
    before = tree_digest(root)
    rc, out = run(srv, root, "install")
    check("1a: update refused when /new_customer is nearly full, nothing changed",
          rc == 1 and "KB free" in out and tree_digest(root) == before, out[-400:])
    (root / "df_kb").unlink()

    # a vendor update ran: new slot A, demo.sh replaced (no guard line), flag cleared, slot B replaced, tarball pending
    orig_b = md5f(ours / "boot/demo_v9.vendor")
    (root / "customer/demo_v9").write_bytes(b"\x7fELF MOCK VENDOR UI slot A v2\n" * 50)
    (root / "customer/demo.sh").write_text(md.DEMO_SH.replace("ota bookkeeping", "ota bookkeeping v2"))
    (root / "customer/new").unlink()
    (root / "new_customer/demo_v9").write_bytes(b"\x7fELF MOCK VENDOR UI slot B v2\n" * 50)
    (root / "customer/temp.tar.gz").write_bytes(b"vendor tarball")
    before = tree_digest(root)
    rc, out = run(srv, root, "status")
    check("1a: status detects every vendor-update trace (read-only)",
          rc == 0 and "vendor update      : DETECTED" in out and "temp.tar.gz exists" in out
          and "is no longer Meraki+" in out and "no guard line any more" in out
          and "slot A (stock UI) changed" in out and "boot flag is not set" in out
          and "run ./install.sh again" in out and tree_digest(root) == before, out[-1200:])
    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("1a: re-install repairs it (guard line, slot B, flag), exit 0", rc == 0
          and md5f(root / "new_customer/demo_v9") == LAUNCHER and (root / "customer/new").exists()
          and sum(mp.GUARD_KEY in l for l in (root / "customer/demo.sh").read_text().splitlines()) == 1, out[-600:])
    check("1a: the replaced vendor slot-B program got its own backup; the first backup is unchanged",
          any(p.name.startswith("demo_v9.vendor.") for p in (ours / "boot").iterdir())
          and md5f(ours / "boot/demo_v9.vendor") == orig_b)
    check("1a: the vendor tarball itself is left alone", (root / "customer/temp.tar.gz").exists())
    rc, out = run(srv, root, "status")
    check("1a: after the repair only the pending tarball is reported",
          "temp.tar.gz exists" in out and "no longer Meraki+" not in out and "slot A (stock UI) changed" not in out
          and "demo.sh changed" not in out and "no guard line" not in out, out[-800:])
    (root / "customer/temp.tar.gz").unlink()
    (root / "customer/demo.sh").write_text(md.DEMO_SH.replace("ota bookkeeping", "ota bookkeeping v3") +
                                           "\n" + mp.GUARD_LINE + "\n")
    rc, out = run(srv, root, "status")
    check("1a: a changed demo.sh that still has the guard line is reported", "demo.sh changed since" in out, out[-600:])
    rc, out = run(srv, root, "revert")
    check("1a: revert still works after all that", rc == 0 and [e for _, e in ui(root)] == ["/customer/demo_v9"])


def health_fail(flag, expect):
    def steps(srv, root):
        (root / flag).write_text("1\n")
        rc, out = run(srv, root, "install", "--health-seconds", "12")
        check(f"health check ({flag}): install reports failure (exit 1)", rc == 1 and "HEALTH CHECK FAILED" in out
              and expect in out and "./revert.sh --ip" in out, out[-700:])
        check(f"health check ({flag}): the new release stays on TRIAL (the launcher rolls it back after 3 failed starts)",
              md5f(root / "new_customer/demo_v9") == LAUNCHER and ota(root).get("pending") == REL
              and "stays on TRIAL" in out, (ota(root), out[-300:]))
        rc, out = run(srv, root, "revert")
        check(f"health check ({flag}): revert.sh brings the stock UI back", rc == 0
              and [e for _, e in ui(root)] == ["/customer/demo_v9"], (out[-500:], ui(root)))
    return steps


def degraded(srv, root):
    (root / "no_stat").write_text("1\n")
    (root / "no_sha256").write_text("1\n")
    rc, out = run(srv, root, "install")
    check("no stat / no sha256sum on the display: install still succeeds (md5 gate; health from the UI's marker)",
          rc == 0 and "Meraki+ reports healthy" in out and "healthy and running" in out, out[-600:])
    check("ls -e fallback parser: 7 s and a padded day",
          mp.ls_e_age("crw-rw----    1 root     root        4,  65 Sun Nov  2 10:00:00 2026 /dev/ttyS1",
                      "Sun Nov  2 10:00:07 2026") == 7 and mp.ls_e_age("crw 4, 65 Nov 2 10:00", "x") is None)


def tampered(srv, root):
    bad = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-bad-"))
    for n in mp.BIN_MD5:
        (bad / n).write_bytes((ROOT / "bin" / n).read_bytes())
    b = bytearray((bad / "merakiplus").read_bytes())
    b[-1] ^= 1
    (bad / "merakiplus").write_bytes(bytes(b))
    before = tree_digest(root)
    rc, out = run(srv, root, "install", "--bin-dir", str(bad))
    check("tampered binary: refused by the sha256 check, nothing changed",
          rc == 1 and "sha256" in out and "expected " + mp.BIN_SHA256["merakiplus"][:8] in out and tree_digest(root) == before, out[-400:])


def not_a_display(srv, root):
    (root / "customer/test.sh").unlink()
    before = tree_digest(root)
    rc, out = run(srv, root, "install")
    check("unrecognised device: install refuses (exit 1)", rc == 1 and "does not look like" in out)
    check("unrecognised device: nothing changed", tree_digest(root) == before)

# ============================================================ phase 1b (launcher, release directories)
FAKE620 = b"\x7fELF FAKE MERAKI+ UI " + b"@(#)merakiplus V6.20 meraki-plus display-ui\n" * 40


def make_v620(root, gesture=True):
    """The layout a v6.20 install with --persist [--gesture] left behind: the UI itself is /new_customer/demo_v9 (running,
    slot-B boot, flag set), a staged copy, the sound helper + WAVs next to it, backups, bookkeeping, an old switcher."""
    import shutil
    ours = root / mp.OURDIR.lstrip("/")
    (ours / "boot").mkdir(parents=True)
    orig_b, orig_sh = (root / "new_customer/demo_v9").read_bytes(), (root / "customer/demo.sh").read_text()
    (ours / "boot/demo_v9.vendor").write_bytes(orig_b)
    (ours / "boot/demo.sh.bak").write_text(orig_sh)
    (root / "new_customer/demo_v9").write_bytes(FAKE620)
    (root / "new_customer/demo_v9").chmod(0o755)
    (ours / "merakiplus").write_bytes(FAKE620)
    shutil.copy(ROOT / "bin/merakiplus_snd", ours / "merakiplus_snd")
    (ours / "audio").mkdir()
    for n in mp.AUDIO_MD5:
        shutil.copy(ROOT / "audio" / n, ours / "audio" / n)
    (ours / "lang").write_text("da\n")
    (root / "customer/new").touch()
    (root / "watchdog_slot").write_text("B\n")
    (root / "proc/650/cmdline").write_bytes(b"/bin/sh\0/customer/test.sh\0new_customer\0")
    (root / "proc/600/image").write_bytes(FAKE620)
    (root / "proc/600/exe_path").write_text(str(root / "new_customer/demo_v9"))
    state = {"orig_flag": "0", "orig_b": "vendor", "first_install": "2026-11-20T10:00:00Z",
             "a_md5": md5f(root / "customer/demo_v9"), "sh_md5": hashlib.md5(orig_sh.encode()).hexdigest()}
    if gesture:
        (ours / "boot/switcher").write_bytes(b"\x7fELF old switcher (v6.20)\n" * 20)
        shutil.copy(ROOT / "bin/grab_shim.so", ours / "boot/grab_shim.so")
        lines = orig_sh.splitlines(keepends=True)
        i = max(k for k, l in enumerate(lines) if l.rstrip("\n") == mp.ANCHOR)
        lines.insert(i, mp.GUARD_LINE + "\n")
        (root / "customer/demo.sh").write_text("".join(lines))
        sw = root / "proc/660"
        sw.mkdir()
        (sw / "image").write_bytes((ours / "boot/switcher").read_bytes())
        (sw / "exe_path").write_text(str(ours / "boot/switcher"))
        import os
        os.symlink(sw / "image", sw / "exe")
        (sw / "cmdline").write_bytes(b"switcher\0")
    (ours / "boot/merakiplus.state").write_text("".join(f"{k}={v}\n" for k, v in state.items()))
    return orig_b, orig_sh


def migrate_v620(srv, root):
    orig_b, orig_sh = make_v620(root)
    old_rel = mp.rel_name("6.20", FAKE620)
    ours = root / mp.OURDIR.lstrip("/")
    rc, out = run(srv, root, "status")
    check("1b: status of a v6.20 layout: no launcher, no vendor traces", rc == 0 and "launcher" not in out.split("vendor update")[0]
          and "vendor update      : no traces" in out, out[-600:])
    before = tree_digest(root)
    rc, out = run(srv, root, "install", "--dry-run", "--gesture", "--persist")
    check("1b: dry-run of the migration changes nothing", rc == 0 and tree_digest(root) == before, out[-500:])
    (root / "snap_on").touch()
    (root.parent / "snaps").mkdir()
    rc, out = run(srv, root, "install", "--gesture", "--persist")
    (root / "snap_on").unlink()
    check("1b: install over v6.20 exits 0", rc == 0, out[-800:])
    check("1b: the running v6.20 build became a release directory (rollback target), complete",
          rel_ok(root, old_rel) and (ours / "rel" / old_rel / "demo_v9").read_bytes() == FAKE620
          and md5f(ours / "rel" / old_rel / "merakiplus_snd") == mp.BIN_MD5["merakiplus_snd"]
          and sorted(p.name for p in (ours / "rel" / old_rel / "audio").iterdir()) == sorted(mp.AUDIO_MD5))
    check("1b: activated through the vendor watchdog (the v6.20 UI cannot exec-reload)", "watchdog exec-reload" in out)
    check("1b: committed after healthy: current = new, previous = v6.20", ota(root).get("current") == REL
          and ota(root).get("previous") == old_rel and not ota(root).get("pending"), ota(root))
    check("1b: slot B = launcher, the new release runs", md5f(root / "new_customer/demo_v9") == LAUNCHER
          and [e for _, e in ui(root)] == [REL_EXE], ui(root))
    check("1b: backups untouched (the first backup is never overwritten), settings kept",
          (ours / "boot/demo_v9.vendor").read_bytes() == orig_b and (ours / "boot/demo.sh.bak").read_text() == orig_sh
          and (ours / "lang").read_text() == "da\n")
    check("1b: the flat staged copy is gone; the flat sound files stay (the v6.20 release uses them)",
          not (ours / "merakiplus").exists() and (ours / "merakiplus_snd").exists() and (ours / "audio").is_dir())
    sw = [d for d in (root / "proc").iterdir() if (d / "exe_path").exists() and (d / "exe_path").read_text().endswith("/switcher")]
    check("1b: switcher updated (release paths) and the running one restarted with the new build",
          md5f(ours / "boot/switcher") == mp.BIN_MD5["switcher"] and len(sw) == 1 and sw[0].name != "660"
          and md5f(sw[0] / "image") == mp.BIN_MD5["switcher"], [d.name for d in sw])
    check("1b: demo.sh still has exactly one guard line", sum(mp.GUARD_KEY in l for l in (root / "customer/demo.sh").read_text().splitlines()) == 1)
    rc, out = run(srv, root, "status")
    check("1b: status reports launcher, releases and the healthy marker", "launcher           : installed in slot B" in out
          and f"current {REL}, previous {old_rel}" in out and "healthy marker     : healthy" in out, out[-900:])
    replay_install(root, old_rel)
    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("1b: a second install is a no-op (committed, running, nothing re-uploaded)", rc == 0 and "already the committed release" in out
          and "already the running UI" in out and "uploaded" not in out, out[-600:])
    rc, out = run(srv, root, "revert")
    check("1b: revert after the migration exits 0", rc == 0, out[-600:])
    check("1b: revert: vendor slot B back, releases + state + flat files gone, flag as before Meraki+, stock running",
          md5f(root / "new_customer/demo_v9") == hashlib.md5(orig_b).hexdigest() and not (ours / "rel").exists()
          and not (ours / "ota").exists() and not (ours / "merakiplus_snd").exists() and not (root / "customer/new").exists()
          and [e for _, e in ui(root)] == ["/customer/demo_v9"] and (root / "customer/demo.sh").read_text() == orig_sh, ui(root))


def replay_install(root, old_rel):
    """Power-cut replay of the installer: every `sync` during the install left a snapshot of /new_customer. A cut
    between two syncs leaves the state of the earlier one (+ unsynced temp files, which are junk). On every snapshot
    the next boot must start a complete Meraki+ release (the old or the new one) — or, before the launcher was
    renamed into place, the untouched old program."""
    import shutil
    import subprocess
    snaps = sorted((root.parent / "snaps").iterdir())
    bad, kinds = [], {}
    for sn in snaps:
        for junk in (False, True):
            r = root.parent / "replay"
            if r.exists():
                shutil.rmtree(r)
            for d in ("customer", "tmp", "proc/sys/kernel/random"):
                (r / d).mkdir(parents=True)
            shutil.copytree(sn, r / "new_customer", symlinks=True)
            (r / "customer/demo_v9").write_text("#!/bin/sh\n")
            (r / "customer/demo_v9").chmod(0o755)
            (r / "proc/sys/kernel/random/boot_id").write_text(f"cut-{sn.name}\n")
            if junk:               # data written after the snapshot's sync never reached flash: temp files are garbage
                for f in (r / "new_customer").rglob("*"):
                    if f.is_file() and any(k in str(f) for k in (".part", ".tmp", "/.stage-", ".launcher")):
                        f.write_bytes(b"\0" * 7)
            b = (r / "new_customer/demo_v9").read_bytes()
            if mp.LAUNCHER_MARK.encode() in b:
                p = subprocess.run([str(HOST_LAUNCHER), "--dry"], capture_output=True, text=True,
                                   env={"MERAKIPLUS_BOOT_ROOT": str(r), "PATH": "/usr/bin:/bin"})
                ex = [l.split()[1] for l in p.stdout.splitlines() if l.startswith("exec ")]
                got = ex[-1] if ex else "?"
                okrel = got.split("/")[-2] if got.startswith(mp.REL_DIR) else None
                if okrel not in (old_rel, REL) or not rel_ok(r, okrel):
                    bad.append(f"{sn.name} junk={junk}: the launcher starts {got}")
                kinds["launcher -> " + str(okrel)] = kinds.get("launcher -> " + str(okrel), 0) + 1
            elif b == FAKE620:
                kinds["old v6.20 program"] = kinds.get("old v6.20 program", 0) + 1
            else:
                bad.append(f"{sn.name} junk={junk}: /new_customer/demo_v9 is neither the old program nor the launcher")
    check(f"1b: installer power-cut replay: {len(snaps)} sync points x 2 (temp data synced / junk) -> always a complete "
          f"release or the old program ({', '.join(f'{k}: {v}' for k, v in sorted(kinds.items()))})",
          not bad and len(snaps) >= 20 and kinds.get("launcher -> " + REL, 0) > 0 and kinds.get("old v6.20 program", 0) > 0,
          "\n".join(bad[:5]))


def variant(tag):
    """A different UI build in a temporary bin dir (+ pinned tables patched in-process), as a 'newer release'."""
    import shutil
    d = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-v-"))
    for n in list(mp.BIN_MD5):
        shutil.copy(ROOT / "bin" / n, d / n)
    b = (ROOT / "bin/merakiplus").read_bytes() + b"\0variant " + tag.encode()
    (d / "merakiplus").write_bytes(b)
    return d, b


def update_reload(srv, root):
    rc, out = run(srv, root, "install")
    check("1b: setup: v6.21 installed and running", rc == 0 and [e for _, e in ui(root)] == [REL_EXE])
    pid0 = ui(root)[0][0]
    keep = (mp.VERSION, dict(mp.BIN_MD5), dict(mp.BIN_SHA256))
    try:
        d, b = variant("6.22")
        mp.VERSION, mp.BIN_MD5["merakiplus"], mp.BIN_SHA256["merakiplus"] = "6.22", hashlib.md5(b).hexdigest(), hashlib.sha256(b).hexdigest()
        r22 = mp.rel_name("6.22", b)
        rc, out = run(srv, root, "install", "--bin-dir", str(d))
        check("1b: update 6.21 -> 6.22: exec-reload by the running UI (same PID, no signal), healthy, committed",
              rc == 0 and "exec-reload: the running Meraki+" in out and ui(root) == [(pid0, f"{mp.REL_DIR}/{r22}/demo_v9")]
              and ota(root).get("current") == r22 and ota(root).get("previous") == REL
              and "kill -TERM" not in (root / "events").read_text().split("reload")[-1], (out[-700:], ui(root)))
        d, b = variant("6.23")
        mp.VERSION, mp.BIN_MD5["merakiplus"], mp.BIN_SHA256["merakiplus"] = "6.23", hashlib.md5(b).hexdigest(), hashlib.sha256(b).hexdigest()
        r23 = mp.rel_name("6.23", b)
        (root / "ui_busy").touch()
        rc, out = run(srv, root, "install", "--bin-dir", str(d), "--reload-wait", "3")
        check("1b: machine busy: never restarted during an activity — 6.23 stays a trial that starts when idle",
              rc == 0 and "starts by itself" in out and ui(root) == [(pid0, f"{mp.REL_DIR}/{r22}/demo_v9")]
              and ota(root).get("pending") == r23 and ota(root).get("current") == r22, (out[-600:], ui(root), ota(root)))
        check("1b: older releases are removed after a commit (only current + previous kept)",
              sorted(p.name for p in (root / mp.REL_DIR.lstrip("/")).iterdir()) == sorted([REL, r22, r23]))
        (root / "ui_busy").unlink()
        import time
        for _ in range(20):
            if ui(root) == [(pid0, f"{mp.REL_DIR}/{r23}/demo_v9")]:
                break
            time.sleep(0.5)
        check("1b: ... and once idle it reloads itself into the trial (same PID)", ui(root) == [(pid0, f"{mp.REL_DIR}/{r23}/demo_v9")], ui(root))
    finally:
        mp.VERSION = keep[0]
        mp.BIN_MD5.clear(); mp.BIN_MD5.update(keep[1]); mp.BIN_SHA256.clear(); mp.BIN_SHA256.update(keep[2])
    rc, out = run(srv, root, "revert")
    check("1b: revert from a trial state", rc == 0 and [e for _, e in ui(root)] == ["/customer/demo_v9"]
          and not (root / mp.REL_DIR.lstrip("/")).exists())


def no_activate(srv, root):
    rc, out = run(srv, root, "install", "--no-activate")
    check("1b: --no-activate: installed as a trial, stock keeps running, starts at the next start of the launcher",
          rc == 0 and ota(root).get("pending") == REL and [e for _, e in ui(root)] == ["/customer/demo_v9"]
          and md5f(root / "new_customer/demo_v9") == LAUNCHER, out[-500:])
    rc, out = run(srv, root, "status")
    check("1b: status shows the trial", f"TRIAL {REL}" in out, out[-600:])


def signed_install(srv, root):
    """OTA phase 2: the installer verifies the release's signed manifest on the computer before touching the display."""
    import shutil
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-sig-"))
    key, other = sigtest.TestKey(tmp, "test"), sigtest.TestKey(tmp, "other")
    saved = dict(mp.TRUSTED_KEYS)
    mp.TRUSTED_KEYS.clear()
    mp.TRUSTED_KEYS.update(key.trusted())
    try:
        good = tmp / "good"
        data, _ = sigtest.signed_release(good, key, seq=5)

        def variant_dir(name, mdata=None, sigt=None):
            d = tmp / name
            shutil.copytree(good, d)
            if mdata is not None:
                (d / "manifest.json").write_bytes(mdata)
            if sigt is not None:
                (d / "manifest.sig").write_text(sigt)
            return d

        before = tree_digest(root)
        rc, out = run(srv, root, "install", unsigned=False)
        check("2: signed by default: no signed manifest -> refused, display untouched",
              rc == 1 and "signed by default" in out and "--allow-unsigned" in out and tree_digest(root) == before,
              out[-400:])
        rc, out = run(srv, root, "install", "--require-signed", unsigned=False)
        check("2: --require-signed (now the default) is still accepted and refuses the same way",
              rc == 1 and "signed by default" in out and tree_digest(root) == before, out[-400:])
        rc, out = run(srv, root, "install", "--require-signed", "--allow-unsigned", unsigned=False)
        check("2: --require-signed with --allow-unsigned is a usage error", rc == 1 and "contradict" in out
              and tree_digest(root) == before, out[-300:])
        bad = variant_dir("tampered", data.replace(b'"seq": 5', b'"seq": 6'))
        rc, out = run(srv, root, "install", "--bin-dir", str(bad), unsigned=False)
        check("2: tampered manifest (old signature): refused, display untouched",
              rc == 1 and "BAD SIGNATURE" in out and tree_digest(root) == before, out[-400:])
        wk = variant_dir("wrongkey", sigt=other.sign(data))
        rc, out = run(srv, root, "install", "--bin-dir", str(wk), unsigned=False)
        check("2: manifest signed by an untrusted key: refused, display untouched",
              rc == 1 and "unknown key" in out and tree_digest(root) == before, out[-400:])
        nosig = variant_dir("nosig")
        (nosig / "manifest.sig").unlink()
        rc, out = run(srv, root, "install", "--bin-dir", str(nosig), unsigned=False)
        check("2: manifest without manifest.sig: refused", rc == 1 and "no manifest.sig" in out
              and tree_digest(root) == before, out[-400:])
        mis = tmp / "mismatch"                                   # validly signed, but lists another sound helper
        sigtest.make_dist(mis, tweak=lambda d: (d / "merakiplus_snd").write_bytes(b"\x7fELF other"))
        mdata = sigtest.sr.build_manifest(mis, mp.VERSION, "0" * 40, "2026-11-23",
                                          [p.name for p in mis.iterdir()], 5)
        shutil.copy(ROOT / "bin" / "merakiplus_snd", mis / "merakiplus_snd")
        (mis / "manifest.json").write_bytes(mdata)
        (mis / "manifest.sig").write_text(key.sign(mdata))
        rc, out = run(srv, root, "install", "--bin-dir", str(mis), unsigned=False)
        check("2: a file that differs from the signed manifest: refused, display untouched",
              rc == 1 and "merakiplus_snd does not match the signed manifest" in out and "--allow-unsigned" in out
              and tree_digest(root) == before, out[-400:])
        for bad_args, why in ((("--bin-dir", str(bad)), "BAD SIGNATURE"), (("--bin-dir", str(wk)), "unknown key")):
            rc, out = run(srv, root, "install", *bad_args)                     # unsigned=True: --allow-unsigned
            check(f"2: --allow-unsigned does not excuse a forged manifest ({why})",
                  rc == 1 and why in out and tree_digest(root) == before, out[-400:])

        rc, out = run(srv, root, "install", "--bin-dir", str(good), unsigned=False)
        check("2: signed release: verified on the computer, installed, max_seq recorded on the display",
              rc == 0 and "signature valid, all" in out and ota(root).get("max_seq") == "5"
              and [e for _, e in ui(root)] == [REL_EXE], (out[-600:], ota(root)))
        rc, out = run(srv, root, "status")
        check("2: status shows the newest accepted seq", "newest accepted seq 5" in out, out[-500:])
        rc, out = run(srv, root, "install", "--bin-dir", str(good), unsigned=False)
        check("2: re-install of the same signed release (seq == max_seq) is fine", rc == 0 and ota(root).get("max_seq") == "5")

        old = tmp / "old"
        sigtest.signed_release(old, key, seq=3)
        before = tree_digest(root)
        rc, out = run(srv, root, "install", "--bin-dir", str(old), unsigned=False)
        check("2: older signed release (seq 3 < 5): OLDER VERSION REFUSED, display untouched",
              rc == 1 and "OLDER VERSION REFUSED" in out and "--allow-downgrade" in out and tree_digest(root) == before,
              out[-400:])
        rc, out = run(srv, root, "install", "--bin-dir", str(old), "--allow-downgrade", unsigned=False)
        check("2: --allow-downgrade installs it on purpose and resets max_seq",
              rc == 0 and "DOWNGRADE on purpose" in out and ota(root).get("max_seq") == "3", (out[-400:], ota(root)))
        before = tree_digest(root)
        rc, out = run(srv, root, "install", unsigned=False)
        check("2: display with max_seq, no signed manifest: refused by default, display untouched",
              rc == 1 and "signed by default" in out and tree_digest(root) == before, out[-400:])
        rc, out = run(srv, root, "install")                                     # --allow-unsigned alone
        check("2: display with max_seq: --allow-unsigned alone is refused (would bypass the signed record)",
              rc == 1 and "already accepted signed release seq 3" in out and "--allow-downgrade" in out
              and tree_digest(root) == before, out[-400:])
        rc, out = run(srv, root, "install", "--bin-dir", str(mis))             # dev build vs. a signed manifest
        check("2: display with max_seq: --allow-unsigned with files that differ from the signed release is refused too",
              rc == 1 and "not the signed release" in out and tree_digest(root) == before, out[-400:])
        rc, out = run(srv, root, "install", "--allow-downgrade")
        check("2: --allow-unsigned --allow-downgrade: UNSIGNED warning, installed, max_seq kept",
              rc == 0 and "UNSIGNED install" in out and ota(root).get("max_seq") == "3", out[-400:])
        rc, out = run(srv, root, "revert")
        check("2: revert removes the OTA state (incl. max_seq)", rc == 0 and not ota(root))
    finally:
        mp.TRUSTED_KEYS.clear()
        mp.TRUSTED_KEYS.update(saved)
        shutil.rmtree(tmp, ignore_errors=True)


def dev_build_and_refs(srv, root):
    """Signed by default, development side: a development build (files that differ from the signed release of
    v<VERSION>) installs only with --allow-unsigned on a display without max_seq; --ref accepts only release tags and full
    commit SHAs; the default ref is this installer's release tag."""
    import shutil
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-dev-"))
    key = sigtest.TestKey(tmp, "test")
    saved = dict(mp.TRUSTED_KEYS)
    mp.TRUSTED_KEYS.clear()
    mp.TRUSTED_KEYS.update(key.trusted())
    try:
        mis = tmp / "devbuild"                                   # a signed release whose sound helper differs from bin/
        sigtest.make_dist(mis, tweak=lambda d: (d / "merakiplus_snd").write_bytes(b"\x7fELF released"))
        mdata = sigtest.sr.build_manifest(mis, mp.VERSION, "0" * 40, "2026-11-23", [p.name for p in mis.iterdir()], 7)
        shutil.copy(ROOT / "bin" / "merakiplus_snd", mis / "merakiplus_snd")
        (mis / "manifest.json").write_bytes(mdata)
        (mis / "manifest.sig").write_text(key.sign(mdata))
        before = tree_digest(root)
        rc, out = run(srv, root, "install", "--bin-dir", str(mis), unsigned=False)
        check("2b: development build (differs from the signed release): refused by default with a hint",
              rc == 1 and "does not match the signed manifest" in out and "latest release tag" in out
              and tree_digest(root) == before, out[-400:])
        rc, out = run(srv, root, "install", "--bin-dir", str(mis))
        check("2b: --allow-unsigned on a display without max_seq: installed as UNSIGNED, no max_seq recorded",
              rc == 0 and "UNSIGNED install" in out and "not the signed release" in out and "max_seq" not in ota(root)
              and [e for _, e in ui(root)] == [REL_EXE], (out[-500:], ota(root)))

        before = tree_digest(root)
        for ref in ("main", "feature/x", "v6", "v6.24-rc1", "deadbeef", "../../evil", "6.24", "V6.24", "a" * 39):
            rc, out = run(srv, root, "install", "--ref", ref)
            check(f"2b: --ref {ref!r} refused before anything happens",
                  rc == 1 and "must be a release tag" in out and tree_digest(root) == before, out[-300:])
        ok = all(mp.check_ref(r) == r for r in ("v6.24", "v6.24.1", "v10.0", "0123456789abcdef0123456789abcdef01234567"))
        check("2b: release tags and full commit SHAs are accepted", ok)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            try:
                mp.main(["install", "--help"])
            except SystemExit:
                pass
        check("2b: the default --ref is this installer's release tag", f"(default v{mp.VERSION}" in buf.getvalue(),
              buf.getvalue()[-600:])
    finally:
        mp.TRUSTED_KEYS.clear()
        mp.TRUSTED_KEYS.update(saved)
        shutil.rmtree(tmp, ignore_errors=True)


def install_sh_bootstrap():
    """install.sh without a clone around it (standalone, e.g. piped from curl): it resolves the LATEST PUBLISHED release
    tag, fetches tools/merakiplus.py from that tag (never from a branch) and passes --ref <tag>. MERAKIPLUS_REF accepts
    only a release tag or a full commit SHA. Network-free: a fake `curl` first on PATH logs every request."""
    import os
    import shutil
    import subprocess
    print("== install.sh: standalone bootstrap (fake curl, no network) ==")
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-sh-"))
    try:
        (tmp / "bin").mkdir()
        log = tmp / "curl.log"
        (tmp / "bin" / "curl").write_text(f"""#!/bin/sh
echo "$*" >> "{log}"
for a in "$@"; do last="$a"; done
case "$*" in
  *url_effective*) case "$FAKE_LATEST" in fail) exit 22;; esac; printf '%s' "$FAKE_LATEST"; exit 0;;
esac
out=""; prev=""
for a in "$@"; do [ "$prev" = "-o" ] && out="$a"; prev="$a"; done
printf '%s\n' 'import sys; print("TOOL ARGS:", " ".join(sys.argv[1:]))' > "$out"
""")
        os.chmod(tmp / "bin" / "curl", 0o755)
        standalone = tmp / "standalone"
        standalone.mkdir()
        shutil.copy(ROOT / "install.sh", standalone / "install.sh")

        def sh(env_extra, *args):
            env = {k: v for k, v in os.environ.items() if k != "MERAKIPLUS_REF"}
            env.update(PATH=f"{tmp / 'bin'}{os.pathsep}{os.environ['PATH']}", **env_extra)
            if log.exists():
                log.unlink()
            r = subprocess.run(["bash", str(standalone / "install.sh"), *args], env=env, capture_output=True, text=True,
                               timeout=60)
            return r.returncode, r.stdout + r.stderr, (log.read_text() if log.exists() else "")

        latest = "https://github.com/thomasgrastein/merakitech-plus/releases/tag/v6.24"
        rc, out, calls = sh({"FAKE_LATEST": latest}, "--ip", "192.0.2.1", "--dry-run")
        check("install.sh: standalone resolves the latest published release and fetches the installer from that tag",
              rc == 0 and "releases/latest" in calls and "/merakitech-plus/v6.24/tools/merakiplus.py" in calls
              and "/main/" not in calls, (out, calls))
        check("install.sh: standalone passes --ref <that tag> to the installer",
              "TOOL ARGS: install --ip 192.0.2.1 --dry-run --ref v6.24" in out, out)
        sha = "0123456789abcdef0123456789abcdef01234567"
        rc, out, calls = sh({"FAKE_LATEST": latest, "MERAKIPLUS_REF": sha}, "--ip", "192.0.2.1")
        check("install.sh: MERAKIPLUS_REF=<full SHA> is used as given (no latest lookup)",
              rc == 0 and "releases/latest" not in calls and f"/{sha}/tools/merakiplus.py" in calls
              and f"--ref {sha}" in out, (out, calls))
        for bad in ("main", "v6.24;rm -rf ~", "../x", "deadbeef"):
            rc, out, calls = sh({"FAKE_LATEST": latest, "MERAKIPLUS_REF": bad}, "--ip", "192.0.2.1")
            check(f"install.sh: MERAKIPLUS_REF={bad!r} refused before any download", rc == 2 and not calls
                  and "release tag" in out, (rc, out, calls))
        rc, out, calls = sh({"FAKE_LATEST": "https://github.com/thomasgrastein/merakitech-plus/releases"},
                            "--ip", "192.0.2.1")
        check("install.sh: no release tag in the answer -> stops, nothing fetched",
              rc == 1 and "tools/merakiplus.py" not in calls, (out, calls))
        rc, out, calls = sh({"FAKE_LATEST": "https://github.com/thomasgrastein/merakitech-plus/releases/tag/main"},
                            "--ip", "192.0.2.1")
        check("install.sh: a 'latest' that is not a release tag is refused", rc == 1 and "unexpected" in out
              and "tools/merakiplus.py" not in calls, (out, calls))
        rc, out, calls = sh({"FAKE_LATEST": "fail"}, "--ip", "192.0.2.1")
        check("install.sh: offline (latest unknown) -> stops with a hint", rc == 1 and "MERAKIPLUS_REF" in out, out)
        if log.exists():
            log.unlink()
        r = subprocess.run(["bash", str(ROOT / "install.sh"), "--help"], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, PATH=f"{tmp / 'bin'}{os.pathsep}{os.environ['PATH']}"))
        check("install.sh: in a clone it uses the local installer (no download) and shows --allow-unsigned",
              r.returncode == 0 and "--allow-unsigned" in r.stdout and not log.exists(), r.stdout[-300:] + r.stderr)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    install_sh_bootstrap()
    scenario("fresh machine running stock from slot A", "stock_a", stock_a)
    scenario("machine running a vendor update from slot B", "vendor_b", vendor_b)
    scenario("slot B empty", "empty_b", empty_b)
    scenario("not a MerakiTech display", "stock_a", not_a_display)
    scenario("upgrade of a pre-rename install (legacy migration)", "stock_a", legacy_upgrade)
    scenario("revert of a pre-rename install (legacy migration)", "stock_a", legacy_revert)
    scenario("revert --purge with both layouts present", "stock_a", legacy_purge)
    scenario("1a hardening: sync order, sha256, leftovers, free space, vendor-update traces", "stock_a", hardening)
    scenario("1a health check: the new UI crashes", "stock_a", health_fail("crash_new_ui", "stopped after"))
    scenario("1a health check: the controller link is silent", "stock_a", health_fail("uart_dead", "no data from"))
    scenario("1a health check: the UI does not open the link", "vendor_b", health_fail("no_uart_fd", "does not hold"))
    scenario("1a: display without stat / sha256sum", "stock_a", degraded)
    scenario("1a: tampered local binary", "stock_a", tampered)
    scenario("1b: migration of a v6.20 install (--persist --gesture) + installer power-cut replay + revert", "stock_a", migrate_v620)
    scenario("1b: update by exec-reload, deferral while busy, older releases collected", "stock_a", update_reload)
    scenario("1b: --no-activate leaves a trial for the next start", "stock_a", no_activate)
    scenario("2: signed release manifest (signed by default, tamper, wrong key, downgrade, --allow-unsigned)", "stock_a",
             signed_install)
    scenario("2b: development builds (--allow-unsigned) and --ref validation", "stock_a", dev_build_and_refs)
    print(f"\nINSTALLER TESTS: {'ALL PASS' if not FAILS else str(len(FAILS)) + ' FAIL: ' + ', '.join(FAILS)}")
    sys.exit(1 if FAILS else 0)
