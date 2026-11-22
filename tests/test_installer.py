#!/usr/bin/env python3
"""End-to-end tests of tools/merakiplus.py (install / re-install / revert / dry-run / migration of
a pre-rename install; phase 1a hardening: sync-before-rename, SHA-256, health check, vendor-update
detection, leftovers, free space) against
tests/mock_device.py. No hardware, no network beyond 127.0.0.1.

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

FAILS = []


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


def run(srv, root, *args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mp.main([args[0], "--ip", "127.0.0.1", "--port", str(srv.port), "--test-prefix", str(root),
                      "--yes", "--log", str(root.parent / "test.log")] +
                     (["--bin-dir", str(ROOT / "bin"), "--health-seconds", "2"] if args[0] == "install" else []) +
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
    check("slot B now holds Meraki+", md5f(root / "new_customer/demo_v9") == MERAKIPLUS)
    check("vendor slot-B backup is byte-identical", md5f(root / "new_customer/merakiplus/boot/demo_v9.vendor") == orig_b)
    check("demo.sh backup is byte-identical", md5f(root / "new_customer/merakiplus/boot/demo.sh.bak") == orig_sh)
    check("demo.sh untouched without --gesture", md5f(root / "customer/demo.sh") == orig_sh)
    check("slot A untouched", md5f(root / "customer/demo_v9") == orig_a)
    check("boot flag NOT written without --persist", not (root / "customer/new").exists())
    check("exactly one UI runs, from slot B", [e for _, e in ui(root)] == ["/new_customer/demo_v9"], ui(root))
    check("old stock UI was stopped (SIGSTOP first, then killed)",
          "kill -STOP 600" in (root / "events").read_text() and not (root / "proc/600").exists())
    check("state records original flag=0", "orig_flag=0" in (root / "new_customer/merakiplus/boot/merakiplus.state").read_text())
    snd = root / mp.SND_HELPER.lstrip("/")
    aud = root / mp.AUDIO_DIR.lstrip("/")
    check("v6.18: sound helper installed (md5-gated, executable)",
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
    check("stock UI running from slot A, nothing else", [e for _, e in ui(root)] == ["/customer/demo_v9"], ui(root))
    check("slot A still untouched", md5f(root / "customer/demo_v9") == orig_a)

    rc, out = run(srv, root, "revert")
    check("second revert is a no-op", rc == 0 and "nothing to revert" in out, out[-300:])


def vendor_b(srv, root):
    orig_b = md5f(root / "new_customer/demo_v9")
    rc, out = run(srv, root, "install")
    check("install on a B-booted device exits 0", rc == 0)
    check("activated through the watchdog exec-reload", "watchdog exec-reload" in out)
    check("one UI, Meraki+ from slot B", [e for _, e in ui(root)] == ["/new_customer/demo_v9"]
          and md5f(root / "new_customer/demo_v9") == MERAKIPLUS)
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
    (old / pathlib.PurePosixPath(mp.STAGE).name).rename(old / mp.LEGACY_ID)
    fake = b"\x7fELF FAKE PRE-RENAME UI " + mp.LEGACY_MARKS[0].encode() + b"V6.16\n" * 40
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
    check("old staged binary removed, new one staged", not (new / mp.LEGACY_ID).exists()
          and md5f(root / mp.STAGE.lstrip("/")) == MERAKIPLUS)
    check("run.sh rewritten (new env prefix + binary name)",
          (new / "run.sh").read_text() == f"#!/bin/sh\n{mp.ENV_PREFIX}NOBOOTPUSH=1 ./merakiplus\n")
    sh = (root / "customer/demo.sh").read_text().splitlines()
    check("demo.sh: exactly one guard line, the current one", [l for l in sh if "/boot/switcher" in l] == [mp.GUARD_LINE])
    check("demo.sh otherwise unchanged", [l for l in sh if l != mp.GUARD_LINE] ==
          (new / "boot/demo.sh.bak").read_text().splitlines())
    check("slot B upgraded to the current build, running", md5f(root / "new_customer/demo_v9") == MERAKIPLUS
          and [e for _, e in ui(root)] == ["/new_customer/demo_v9"])
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
    renames = [i for i, l in enumerate(ev) if l.startswith("mv ") and (".part " in l or ".tmp " in l)]
    bad = [ev[i] for i in renames if i == 0 or ev[i - 1] != "sync"]
    return len(renames), bad


def hardening(srv, root):
    rc, out = run(srv, root, "install", "--gesture", "--persist")
    check("1a: install exits 0", rc == 0, out[-600:])
    n, bad = renames_after_sync(root)
    check(f"1a: every staged file is synced before its rename ({n} renames)", n >= 8 and not bad, bad)
    check("1a: files verified by sha256 on the computer", "sha256 42cd7b9d28ebf670" in out)
    check("1a: health check ran: same process + live controller link",
          "same process for 2 s" in out and "controller link live" in out and "healthy and running" in out, out[-500:])
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
          and md5f(root / "new_customer/demo_v9") == MERAKIPLUS and (root / "customer/new").exists()
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
        check(f"health check ({flag}): Meraki+ stays installed (no automatic rollback in 1a)",
              md5f(root / "new_customer/demo_v9") == MERAKIPLUS)
        rc, out = run(srv, root, "revert")
        check(f"health check ({flag}): revert.sh brings the stock UI back", rc == 0
              and [e for _, e in ui(root)] == ["/customer/demo_v9"], (out[-500:], ui(root)))
    return steps


def degraded(srv, root):
    (root / "no_stat").write_text("1\n")
    (root / "no_sha256").write_text("1\n")
    rc, out = run(srv, root, "install")
    check("no stat / no sha256sum on the display: install still succeeds (md5 gate, warning)",
          rc == 0 and "could not be measured" in out and "healthy and running" in out, out[-600:])
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
          rc == 1 and "sha256" in out and "expected 42cd7b9d" in out and tree_digest(root) == before, out[-400:])


def not_a_display(srv, root):
    (root / "customer/test.sh").unlink()
    before = tree_digest(root)
    rc, out = run(srv, root, "install")
    check("unrecognised device: install refuses (exit 1)", rc == 1 and "does not look like" in out)
    check("unrecognised device: nothing changed", tree_digest(root) == before)


if __name__ == "__main__":
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
    print(f"\nINSTALLER TESTS: {'ALL PASS' if not FAILS else str(len(FAILS)) + ' FAIL: ' + ', '.join(FAILS)}")
    sys.exit(1 if FAILS else 0)
