#!/usr/bin/env python3
"""merakiplus.py — install / revert / inspect Meraki+ on a MerakiTech Gen1/Gen2 display.

Normally used through ./install.sh and ./revert.sh. Python 3.8+, standard library only.

    merakiplus.py status  --ip ADDR            read-only overview (safe)
    merakiplus.py scan                          find the display on your /24 (telnet fingerprint)
    merakiplus.py install --ip ADDR [--persist] [--gesture] [--dry-run] [--yes]
    merakiplus.py revert  --ip ADDR [--purge]  [--dry-run] [--yes]
    merakiplus.py verify-release DIR            check a downloaded release against its signed manifest

How it talks to the display: the vendor firmware runs an unauthenticated root telnet
server on port 23. Commands are sent over that; files are pushed with the display's own
busybox `nc -l` (raw TCP), and every file is md5-verified after transfer and after install
(plus SHA-256 when the display's busybox has sha256sum). On the computer every file is checked
against the pinned SHA-256 and md5 tables below before anything is sent.

Safety model (see README "Security and OTA" and the disclaimer):
  * Slot A (/customer/demo_v9, libs, test.sh) is never written. config.json is never written.
  * Slot B's existing binary and /customer/demo.sh are backed up (md5-verified) before anything
    is changed; backups are never overwritten by later runs.
  * Replacements are atomic: copy to a temp file on the SAME filesystem, md5-gate, `sync`, then
    rename, then `sync` again. The data reaches flash BEFORE the name changes, so a power cut leaves
    either the complete old file or the complete new one (plus, at worst, a stale temp file that the
    next install removes). A rename over a running executable is fine; a cross-fs copy over it
    fails with ETXTBSY.
  * The boot flag (/customer/new) is only written with --persist. /customer/demo.sh is only
    written with --gesture (one guard line, reversible).
  * No reboot is ever sent. Activation uses the vendor watchdog (exec-reload) or a
    stop-first runtime switch (SIGSTOP old UI, start new, verify, then kill old; on failure
    the old UI is resumed with SIGCONT).
  * Since v6.21 (docs/ota-plan.md phase 1b) every Meraki+ version lives complete in its own release
    directory /new_customer/merakiplus/rel/<version>-<sha>/ (UI, sound helper, sounds, MANIFEST), staged
    under .stage-<name> and renamed into place after a sync. A small launcher becomes /new_customer/demo_v9:
    it starts the release named by the state file ota/state (written tmp -> sync -> rename -> sync) and
    rolls back on its own: a new version that does not become healthy within 3 starts -> the previous
    version -> the stock UI from slot A (read-only; the boot flag is never touched).
  * A new version is installed as a TRIAL (pending). It is activated when the machine is idle — by the
    running UI itself (exec-reload, same PID) or, for older UIs, through the vendor watchdog — and the
    health check waits for the UI's own healthy marker (60 s running, controller answering). Then the
    installer commits it (current/previous); otherwise the launcher rolls it back after 3 failed starts.
"""
import argparse
import base64
import datetime
import gzip
import hashlib
import ipaddress
import json
import os
import pathlib
import random
import re
import socket
import sys
import tempfile
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor

VERSION = "6.24"
REPO = "thomasgrastein/merakitech-plus"
RAW_BASE = "https://raw.githubusercontent.com/" + REPO + "/{ref}/bin/{name}"
RAW_AUDIO = "https://raw.githubusercontent.com/" + REPO + "/{ref}/audio/{name}"

# md5 of the shipped device binaries in bin/ (kept in sync with bin/MD5SUMS by CI).
BIN_MD5 = {
    "merakiplus": "9182753f4d277821f9f06b810a066096",
    "switcher": "e02a463c16278545c04b8e4022b8369f",
    "grab_shim.so": "c42076c807c54d477a5868977404186d",
    "merakiplus_snd": "a7659c176b8ef78385b0b612a69e81ca",
    "merakiplus_boot": "c0035acd75049bea2570a6ea99c4ab5d",
}
# v6.18: Meraki+'s own sounds (tools/gen_audio.py; docs/audio.md; v6.19: marimba / glockenspiel timbres) — installed next to the sound helper
AUDIO_MD5 = {
    "start.wav": "a03cb807d1cdb629b0614fdb740ec71d",
    "ready_extract.wav": "4e7e24f18a80ee2f237e2f5ed220afbe",
    "ready_steam.wav": "483d57ef122dc9929b004441fe01bc44",
}
# SHA-256 of the same files (docs/ota-plan.md phase 1a). Checked on the computer before upload, and on the display
# when its busybox has sha256sum. md5 stays as well (the display always has md5sum; older docs/scripts quote it).
BIN_SHA256 = {
    "merakiplus": "df92571082c0dc975b92314f266d81b69c5d55666241624fa26ebf69f3685057",
    "switcher": "6af721e297e8d13dedc9ad9c14bf39d925eb7d4cc7ae9fab49f99b428873d5bf",
    "grab_shim.so": "c02f61b53ec4613f04950966bcfab7e7aa2ad53dfe1035eae22a542861d268d7",
    "merakiplus_snd": "84c06c507a04d0c06ae9250a60405f10bcf05b105e481144869180fd30d479cb",
    "merakiplus_boot": "4cbcc5fb20469ba25b79ee1523b4639a2a81af84e96b996e97d2af0d535fd8ed",
}
AUDIO_SHA256 = {
    "start.wav": "4633a9ecd800119732886e5255f404d76eb9743dda3ebdd2c270d5106650ff01",
    "ready_extract.wav": "f8dbf35ce5789480cfe183bc2002a01984b219719cef0f0f0b08ddc0ef537499",
    "ready_steam.wav": "85bd297c597f9239829cb5db1dc67fc6ce6dbef910a7972555ad0826377f91f1",
}
MERAKIPLUS_MARK = "@(#)merakiplus "        # embedded in every Meraki+ binary (src/merakiplus.c MERAKIPLUS_MARK)
LAUNCHER_MARK = "@(#)merakiplus-launcher "  # only in the launcher (src/merakiplus_boot.c)
RELOAD_MARK = "/tmp/merakiplus_reload"     # a UI that contains this string can exec-reload itself (v6.21+)

# ---- device layout (vendor A/B scheme, see docs/boot.md) ----
A_DIR, B_DIR = "/customer", "/new_customer"
A_EXE, B_EXE = "/customer/demo_v9", "/new_customer/demo_v9"
DEMO_SH, TEST_SH, FLAG = "/customer/demo.sh", "/customer/test.sh", "/customer/new"
OURDIR = "/new_customer/merakiplus"
BOOTDIR = OURDIR + "/boot"
STAGE = OURDIR + "/merakiplus"                          # staged copy (same fs as slot B)
BAK_B = BOOTDIR + "/demo_v9.vendor"                     # slot B's original binary
BAK_B_ABSENT = BOOTDIR + "/demo_v9.vendor.absent"       # marker: slot B had no binary
BAK_SH = BOOTDIR + "/demo.sh.bak"                       # original /customer/demo.sh
STATE = BOOTDIR + "/merakiplus.state"
SND_HELPER = OURDIR + "/merakiplus_snd"                 # v6.18: plays a sound via the display's own libmi_ao.so
AUDIO_DIR = OURDIR + "/audio"                           # v6.18: start.wav, ready_extract.wav, ready_steam.wav
SWITCHER, SHIM = BOOTDIR + "/switcher", BOOTDIR + "/grab_shim.so"
# v6.21 (docs/ota-plan.md §5.2): release directories + the launcher's state
REL_DIR = OURDIR + "/rel"                               # rel/<version>-<sha8>/{demo_v9, merakiplus_snd, audio/, MANIFEST}
OTA_DIR = OURDIR + "/ota"
OTA_STATE = OTA_DIR + "/state"                          # the commit point: current / previous / pending / tries / last
OTA_HEALTHY = OTA_DIR + "/healthy"                      # written by a UI on trial once it is healthy
OTA_LOG = OTA_DIR + "/launch.log"                       # the launcher's events (rollbacks, fallbacks)
HMARK = "/tmp/merakiplus_healthy"                       # the UI's healthy marker (boot_id + pid), every start
RELOAD = RELOAD_MARK                                    # ask the running UI to exec the launcher when idle
LAUNCH_VOL = "/tmp/merakiplus_launch"                   # the launcher's per-boot counters
BOOT_ID = "/proc/sys/kernel/random/boot_id"
LAUNCHER_NEW = B_DIR + "/.demo_v9.launcher"             # the uploaded launcher, selftested before the rename
GUARD_LINE = ("[ -x /new_customer/merakiplus/boot/switcher ] && { [ -f /new_customer/merakiplus/boot/grab_shim.so ] "
              "&& export LD_PRELOAD=/new_customer/merakiplus/boot/grab_shim.so${LD_PRELOAD:+:$LD_PRELOAD}; "
              "/new_customer/merakiplus/boot/switcher & }")
GUARD_KEY = "/new_customer/merakiplus/boot/switcher"    # any line containing this is ours
ENV_PREFIX = "MERAKIPLUS_"                              # prefix of the UI's environment variables
ANCHOR = 'if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then'
INSMOD_MARKER = "# kernel_mod_list"
B_LDP, A_LDP = "/new_customer/libs:/config/wifi", "/customer/libs:/config/wifi"
UART_DEV = "/dev/ttyS1"                                 # the UI's serial link to the coffee machine's controller
HEALTH_S = 60                                           # post-install health check (docs/ota-plan.md "healthy")
LINK_FRESH_S = 20                                       # controller data must be younger than this (atime: 8 s steps)
# vendor OTA leftovers: a downloaded vendor update waiting for the next boot (docs/ota-plan.md §2.3)
VENDOR_PENDING = (A_DIR + "/temp.tar.gz", A_DIR + "/demo_temp")
# temp files our own atomic steps create; a power cut can leave them behind (never the final names)
LEFTOVER_GLOBS = (OURDIR + "/*.part", OURDIR + "/.*.tmp", AUDIO_DIR + "/*.part", BOOTDIR + "/*.part",
                  BOOTDIR + "/.*.tmp", BOOTDIR + "/*.tmp", B_DIR + "/.demo_v9.tmp", DEMO_SH + ".part",
                  A_DIR + "/.demo.sh.tmp", OTA_DIR + "/*.tmp*", B_DIR + "/.demo_v9.launcher*")

# ============================================================ legacy migration only
# BEGIN legacy-compat (legacy migration only)
# Installs made before the rename (releases up to v6.16) used a different identifier for the
# install directory, the staged binary, the UI's environment variables and the binary marker.
# This block is the ONLY place in the repository where that identifier may appear
# (tests/hygiene.py enforces it). Nothing else may use these literals.
LEGACY_ID = "ourui"
LEGACY_DIR = B_DIR + "/" + LEGACY_ID                    # former OURDIR
LEGACY_GUARD_KEY = LEGACY_DIR + "/boot/switcher"        # former GUARD_KEY (demo.sh guard line)
LEGACY_ENV_PREFIX = LEGACY_ID.upper() + "_"             # former ENV_PREFIX (only used in a dev run.sh)
LEGACY_MARKS = ("@(#)" + LEGACY_ID + " ",               # v6.2 .. v6.16 what(1) marker
                "== " + LEGACY_ID + ": config.json aendret")   # v6.1 (no marker; its exit text)


def legacy_path(path):
    """The pre-rename location of a path under OURDIR."""
    return LEGACY_DIR + path[len(OURDIR):] if path.startswith(OURDIR) else path


def legacy_rewrite(data):
    """Rewrite a pre-rename helper script (a dev run.sh) to the current identifiers."""
    return (data.replace(LEGACY_ENV_PREFIX.encode(), ENV_PREFIX.encode())
                .replace(LEGACY_ID.encode(), pathlib.PurePosixPath(OURDIR).name.encode()))


def legacy_inspect(dev):
    """{'dir': legacy install directory exists, 'guard': legacy guard lines in demo.sh}."""
    _, g = dev.sh(f"grep -c -F '{LEGACY_GUARD_KEY}' {dev.d(DEMO_SH)} 2>/dev/null; true", check=False)
    last = g.strip().splitlines()[-1] if g.strip() else ""
    return {"dir": dev.exists(LEGACY_DIR), "guard": int(last) if last.isdigit() else 0}


def legacy_migrate(dev, o, st, rewrite_guard):
    """Move a pre-rename install to OURDIR (backups, state, switcher, language, schedule, settings
    backups, a dev run.sh) and, with rewrite_guard, turn its demo.sh guard line into GUARD_LINE.
    Idempotent. Returns True when something was (or, in --dry-run, would be) changed."""
    lg = st["legacy"]
    if not lg["dir"] and not (lg["guard"] and rewrite_guard):
        return False
    o.step(f"Migrating an install from before the rename ({LEGACY_DIR} -> {OURDIR})")
    if lg["dir"]:
        old, new = dev.d(LEGACY_DIR), dev.d(OURDIR)
        if not dev.exists(OURDIR):
            dev.w(f"mv {old} {new} && sync")
            o.ok(f"{LEGACY_DIR} moved to {OURDIR} (same filesystem, atomic)")
        else:
            # both exist (interrupted migration): move what the new directory lacks, never overwrite;
            # anything left over is kept aside in OURDIR/legacy-leftover (removed only by --purge)
            dev.w(f"cd {old} && for f in $(find . -type f -o -type l); do [ -e {new}/$f ] || "
                  f"{{ mkdir -p {new}/$(dirname $f) && mv $f {new}/$f; }}; done; cd /; "
                  f"if [ -n \"$(find {old} -type f -o -type l)\" ]; then rm -rf {new}/legacy-leftover; "
                  f"mv {old} {new}/legacy-leftover; else rm -rf {old}; fi; sync")
            o.ok(f"{LEGACY_DIR} merged into {OURDIR} (existing files kept)")
        src = OURDIR if not dev.dry else LEGACY_DIR
        old_stage = src + "/" + LEGACY_ID
        if dev.exists(old_stage):
            dev.w(f"rm -f {dev.d(old_stage)}; sync")
            o.ok("old staged copy removed (install stages the new binary as " + STAGE + ")")
        run_sh = src + "/run.sh"
        if dev.exists(run_sh):
            cur = dev.read(run_sh)
            new_b = legacy_rewrite(cur)
            if new_b != cur:
                dev.push(new_b, OURDIR + "/run.sh", mode="755")
                o.ok("run.sh rewritten to the new directory, binary name and environment variables")
    if lg["guard"] and rewrite_guard:
        lines = dev.read(DEMO_SH).decode("utf-8", "surrogateescape").splitlines(keepends=True)
        out, placed = [], any(GUARD_KEY in l for l in lines)
        for l in lines:
            if LEGACY_GUARD_KEY in l:
                if not placed:
                    out.append(GUARD_LINE + "\n")
                    placed = True
                continue
            out.append(l)
        new_sh = "".join(out).encode("utf-8", "surrogateescape")
        dev.push(new_sh, DEMO_SH, mode="755")
        if not dev.dry and dev.read(DEMO_SH) != new_sh:
            raise Abort("demo.sh read-back differs from what was written — run ./revert.sh now")
        o.ok("demo.sh: the old guard line now points at " + BOOTDIR)
    return True
# END legacy-compat

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


class Abort(Exception):
    """Stop cleanly with a message (nothing half-done is left behind at the call sites)."""


def md5b(b):
    return hashlib.md5(b).hexdigest()


def sha256b(b):
    return hashlib.sha256(b).hexdigest()


# ============================================================ console helpers
class Out:
    def __init__(self, logfile=None, verbose=False):
        self.logf = open(logfile, "a", encoding="utf-8") if logfile else None
        self.verbose = verbose

    def _log(self, s):
        if self.logf:
            self.logf.write(datetime.datetime.now().strftime("%H:%M:%S ") + s + "\n")
            self.logf.flush()

    def say(self, s=""):
        print(s, flush=True)
        self._log(s)

    def step(self, s):
        self.say("\n==> " + s)

    def ok(self, s):
        self.say("  [ok] " + s)

    def warn(self, s):
        self.say("  [!]  " + s)

    def debug(self, s):
        self._log(s)
        if self.verbose:
            print("      " + s.replace("\n", "\n      "), flush=True)


def confirm(question, assume_yes):
    if assume_yes:
        return True
    try:
        tty = sys.stdin if sys.stdin.isatty() else open("/dev/tty")
    except OSError:
        raise Abort("No terminal to ask for confirmation — re-run with --yes if you are sure.")
    sys.stdout.write(question + " [y/N] ")
    sys.stdout.flush()
    ans = tty.readline().strip().lower()
    return ans in ("y", "yes", "j", "ja")


# ============================================================ telnet transport
class Device:
    """One persistent telnet shell on the display. sh() always runs (use it for reads);
    w() is for writes and is skipped in --dry-run."""

    def __init__(self, host, port, out, dry=False, prefix=""):
        self.host, self.port, self.o, self.dry = host, port, out, dry
        self.p = prefix                 # test hook: path prefix for the mock device (tests/)
        self.sock = None
        self.changed = False            # set once anything on the display was modified
        self._sha = None                # does the display's busybox have sha256sum? (probed once)

    # -- low level --
    def _strip_iac(self, chunk):
        clean, i = bytearray(), 0
        while i < len(chunk):
            b = chunk[i]
            if b == IAC and i + 1 < len(chunk):
                cmd = chunk[i + 1]
                if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(chunk):
                    try:
                        self.sock.sendall(bytes([IAC, WONT if cmd in (DO, DONT) else DONT, chunk[i + 2]]))
                    except OSError:
                        pass
                    i += 3
                    continue
                if cmd == SB:
                    j = chunk.find(bytes([IAC, SE]), i)
                    i = j + 2 if j >= 0 else len(chunk)
                    continue
                if cmd == IAC:
                    clean.append(IAC)
                i += 2
                continue
            clean.append(b)
            i += 1
        return bytes(clean)

    def connect(self):
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=8)
        except OSError as e:
            raise Abort(f"Cannot reach {self.host}:{self.port} ({e}). Is the machine on, on the same "
                        f"network, and is the IP right? Try: ./install.sh --scan")
        self.sock.settimeout(0.4)
        banner, t0 = b"", time.time()
        while time.time() - t0 < 1.2:
            try:
                c = self.sock.recv(65536)
                if not c:
                    break
                banner += self._strip_iac(c)
            except socket.timeout:
                if banner:
                    break
        if re.search(rb"login:|[Pp]assword:", banner):
            raise Abort("The telnet server asks for a login. This firmware is not the open-telnet "
                        "variant Meraki+ was developed on — stopping without changing anything.")

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def sh(self, cmd, timeout=30, check=True):
        """Run one shell command, return (rc, output). Raises Abort on transport failure."""
        if self.sock is None:
            self.connect()
        tag = "%08x" % random.getrandbits(32)
        wire = f"printf 'S_%s\\n' {tag}; {{ {cmd} ; }} 2>&1; printf 'E_%s:%s\\n' {tag} $?\r\n"
        self.o.debug("$ " + cmd)
        try:
            self.sock.sendall(wire.encode())
        except OSError as e:
            self.close()
            raise Abort(f"Lost the telnet connection ({e}).")
        end_re = re.compile(rb"(?:^|\n)E_" + tag.encode() + rb":(\d+)\r?\n")
        data, deadline = b"", time.time() + timeout
        while time.time() < deadline:
            try:
                c = self.sock.recv(65536)
                if not c:
                    break
                data += self._strip_iac(c)
                if end_re.search(data):
                    break
            except socket.timeout:
                continue
        m = end_re.search(data)
        if not m:
            self.close()
            raise Abort(f"No answer from the display within {timeout}s for: {cmd[:80]}")
        text = data[:m.start()].decode("utf-8", "replace").replace("\r", "")
        lines = text.split("\n")
        try:
            body = "\n".join(lines[lines.index("S_" + tag) + 1:])
        except ValueError:
            body = text
        rc = int(m.group(1))
        self.o.debug(f"  rc={rc} " + body[-1500:])
        if check and rc != 0:
            raise Abort(f"Command failed on the display (rc={rc}): {cmd[:120]}\n{body[-600:]}")
        return rc, body.strip("\n")

    def w(self, cmd, timeout=30, check=True):
        if self.dry:
            self.o.say("  [dry-run] would run: " + cmd)
            return 0, ""
        self.changed = True
        return self.sh(cmd, timeout, check)

    # -- helpers (all paths go through d() so the mock device can relocate them) --
    def d(self, path):
        return self.p + path

    def exists(self, path):
        return self.sh(f"[ -e {self.d(path)} ] && echo Y || echo N")[1].strip().endswith("Y")

    def md5(self, path):
        rc, out = self.sh(f"md5sum {self.d(path)} 2>/dev/null", check=False)
        m = re.search(r"\b([0-9a-f]{32})\b", out)
        return m.group(1) if (rc == 0 and m) else None

    def has_sha256(self):
        """busybox 1.20 usually has sha256sum, but it is not proven on this firmware (ota-plan probe P2)."""
        if self._sha is None:
            _, out = self.sh("printf abc | sha256sum 2>/dev/null; true", check=False)
            self._sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" in out
            self.o.debug(f"sha256sum on the display: {self._sha}")
        return self._sha

    def sha256(self, path):
        rc, out = self.sh(f"sha256sum {self.d(path)} 2>/dev/null", check=False)
        m = re.search(r"\b([0-9a-f]{64})\b", out)
        return m.group(1) if (rc == 0 and m) else None

    def is_ours(self, path):
        pats = " ".join(f"-e '{m}'" for m in (MERAKIPLUS_MARK,) + LEGACY_MARKS)
        return self.sh(f"grep -q -F {pats} {self.d(path)} 2>/dev/null && echo Y || echo N",
                       check=False)[1].strip().endswith("Y")

    def read(self, path):
        """Byte-exact download (cat through the pty would mangle tabs/newlines)."""
        _, out = self.sh(f"gzip -c {self.d(path)} | base64", timeout=60)
        b64 = "".join(l.strip() for l in out.splitlines() if re.fullmatch(r"[A-Za-z0-9+/=]+", l.strip()))
        return gzip.decompress(base64.b64decode(b64))

    def ui_procs(self):
        """[(pid, exe)] for every running process whose exe is a demo_v9.
        Scans /proc instead of `pidof demo_v9`: the vendor watchdog greps `ps` for 'demo_v9',
        and a helper process with that string in its command line would fool it."""
        # After a binary is renamed over, the kernel reports "<path> (deleted)" for the old
        # inode that is still running — that process must still be seen (and stopped).
        _, out = self.sh(f"for d in {self.d('/proc')}/[0-9]*; do e=$(readlink $d/exe 2>/dev/null); "
                         f"case \"$e\" in */demo_v9|*'/demo_v9 (deleted)') echo \"P ${{d##*/}} $e\";; esac; done",
                         check=False)
        res = []
        for ln in out.splitlines():
            parts = ln.split(" ", 2)
            if len(parts) == 3 and parts[0] == "P" and parts[1].isdigit():
                exe = parts[2].strip()
                if exe.endswith(" (deleted)"):
                    exe = exe[:-len(" (deleted)")]
                if self.p and exe.startswith(self.p):
                    exe = exe[len(self.p):]
                res.append((int(parts[1]), exe))
        return res

    def watchdog_slot(self):
        """'A' / 'B' / None — which slot the vendor watchdog (test.sh <slot>) restarts."""
        _, out = self.sh(f"ps 2>/dev/null | grep '[t]est.sh' | sed 's/^/W /'; "
                         f"for d in {self.d('/proc')}/[0-9]*; do c=$(tr '\\000' ' ' < $d/cmdline 2>/dev/null); "
                         f"case \"$c\" in *test.sh*) echo \"W $c\";; esac; done", check=False)
        for ln in out.splitlines():
            if ln.startswith("W ") and "test.sh" in ln:
                if re.search(r"test\.sh\s+new_customer", ln):
                    return "B"
                if re.search(r"test\.sh\s+customer", ln):
                    return "A"
        return None

    def pids_named(self, name):
        _, out = self.sh(f"for d in {self.d('/proc')}/[0-9]*; do e=$(readlink $d/exe 2>/dev/null); "
                         f"case \"$e\" in */{name}|*'/{name} (deleted)') echo \"P ${{d##*/}}\";; esac; done", check=False)
        return [int(l.split()[1]) for l in out.splitlines() if l.startswith("P ") and l.split()[1].isdigit()]

    def push(self, local_bytes, dest, mode="755"):
        """Raw upload via the display's busybox `nc -l`, written to dest.part on dest's own
        filesystem, md5-verified (+ SHA-256 when available), synced, then renamed over dest.

        Power-cut safety, step by step (UBIFS: a rename is atomic, but it may reach flash before
        data that was not synced — hence the sync BEFORE the rename):
          upload / verify .part  -> dest untouched; a stale .part (removed by the next install)
          chmod + sync           -> same; the .part is now complete on flash
          mv .part dest + sync   -> dest is the complete old OR the complete new file, never a mix
        """
        want = md5b(local_bytes)
        want256 = sha256b(local_bytes)
        dpath = self.d(dest)
        part = dpath + ".part"
        if self.dry:
            self.o.say(f"  [dry-run] would upload {len(local_bytes)} bytes (md5 {want}) -> {dest}")
            return want
        last_err = None
        self.changed = True
        for attempt in range(3):
            port = random.randint(40000, 49999)
            _, out = self.sh(f"rm -f {part}; nohup nc -l -p {port} > {part} 2>/dev/null & echo NCPID=$!; sleep 1")
            ncpid = (re.search(r"NCPID=(\d+)", out) or [None, ""])[1]
            sent = False
            for _ in range(10):
                try:
                    s = socket.create_connection((self.host, port), timeout=10)
                    s.settimeout(30)
                    s.sendall(local_bytes)
                    s.shutdown(socket.SHUT_WR)
                    try:
                        while s.recv(4096):
                            pass
                    except OSError:
                        pass
                    s.close()
                    sent = True
                    break
                except OSError as e:
                    last_err = e
                    time.sleep(0.7)
            got = None
            for _ in range(20 if sent else 1):
                got = self.md5(dest + ".part")
                if got == want:
                    break
                time.sleep(0.5)
            if ncpid:
                self.sh(f"kill {ncpid} 2>/dev/null; true", check=False)
            if got == want and self.has_sha256() and self.sha256(dest + ".part") != want256:
                got = "sha256 mismatch"
            if got == want:
                self.sh(f"chmod {mode} {part} && sync")              # data + mode on flash first ...
                self.sh(f"mv -f {part} {dpath} && sync")             # ... then the atomic rename
                if self.md5(dest) != want or (self.has_sha256() and self.sha256(dest) != want256):
                    raise Abort(f"checksum mismatch after rename: {dest}")
                self.o.debug(f"push ok {dest} {want}")
                return want
            self.sh(f"rm -f {part}", check=False)
            last_err = last_err or f"md5 {got} != {want}"
            self.o.warn(f"upload attempt {attempt + 1} failed ({last_err}); retrying")
        raise Abort(f"Could not upload {dest}: {last_err}")

    def atomic_copy(self, src, dest, want_md5, mode="755"):
        """cp src -> .dest.tmp (same fs as dest), md5-gate, rename over dest, verify."""
        dp = pathlib.PurePosixPath(self.d(dest))
        tmp = str(dp.parent / ("." + dp.name + ".tmp"))
        if self.dry:
            self.o.say(f"  [dry-run] would copy {src} -> {dest} via {tmp} (md5 {want_md5[:8]}…)")
            return
        self.changed = True
        self.sh(f"rm -f {tmp}; cp {self.d(src)} {tmp} && chmod {mode} {tmp} && sync")
        got = self.md5(str(pathlib.PurePosixPath(dest).parent / ("." + dp.name + ".tmp")))
        if got != want_md5:
            self.sh(f"rm -f {tmp}", check=False)
            raise Abort(f"Copy to {tmp} has md5 {got}, expected {want_md5}; {dest} left untouched.")
        self.sh(f"mv -f {tmp} {self.d(dest)} && sync")
        if self.md5(dest) != want_md5:
            raise Abort(f"{dest} has the wrong md5 after rename — run revert.sh.")


# ============================================================ binaries
def repo_root():
    return pathlib.Path(__file__).resolve().parent.parent


def get_binary(name, args, out, sub="bin"):
    """Local bin/<name> (git clone) or download from GitHub raw; md5-verified either way.
    sub="audio": the same for audio/<name> (v6.18), gated by AUDIO_MD5."""
    want = (AUDIO_MD5 if sub == "audio" else BIN_MD5)[name]
    want256 = (AUDIO_SHA256 if sub == "audio" else BIN_SHA256)[name]
    cands = []
    if getattr(args, "bin_dir", None):                           # a downloaded release: binaries + WAVs side by side
        cands.append(pathlib.Path(args.bin_dir) / name)
    cands.append(repo_root() / sub / name)
    for p in cands:
        if p.is_file():
            data = p.read_bytes()
            if sha256b(data) != want256 or md5b(data) != want:
                raise Abort(f"{p} has sha256 {sha256b(data)}, expected {want256}. Corrupt or modified file — "
                            f"re-download the repo (or pass --bin-dir to a verified copy).")
            out.ok(f"{name}: local {p} (sha256 {want256[:16]}…, md5 {want})")
            return data
    url = (RAW_AUDIO if sub == "audio" else RAW_BASE).format(ref=args.ref, name=name)
    out.say(f"  downloading {url}")
    try:
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
    except Exception as e:
        raise Abort(f"Download failed ({e}). Clone the repo instead: git clone https://github.com/{REPO}")
    if sha256b(data) != want256 or md5b(data) != want:
        raise Abort(f"Downloaded {name} has sha256 {sha256b(data)}, expected {want256} — refusing to use it.")
    out.ok(f"{name}: downloaded, sha256 {want256[:16]}… and md5 {want} verified")
    return data


def check_arm_elf(data, name, shared=False):
    if not (data[:4] == b"\x7fELF" and data[4] == 1 and int.from_bytes(data[18:20], "little") == 40):
        raise Abort(f"{name} is not a 32-bit ARM ELF")
    et = int.from_bytes(data[16:18], "little")
    if et != (3 if shared else 2):
        raise Abort(f"{name}: unexpected ELF type {et}")


# ============================================================ signed release manifests (docs/signing.md, OTA phase 2)
# A release carries manifest.json (every asset with size + SHA-256, a monotonic `seq`) and manifest.sig (Ed25519 over the
# exact manifest bytes, signify format). It is signed OFFLINE by the maintainer (tools/sign_release.py); CI cannot sign.
# The trusted public keys are pinned here, in src/merakiplus_upd_keys.h and in keys/ (tests/hygiene.py keeps them equal).
# key id (hex of the signify key number) -> Ed25519 public key (hex)
TRUSTED_KEYS = {
    "26f673cde8584ba0": "f16f17c0b8576b95931c0593b924c81748ad10668dfac9fecfa8e7404aa0bc1d",   # primary, generated 2026-11-23 (docs/signing.md)
}
RELEASE_DL = "https://github.com/" + REPO + "/releases/download/{tag}/{name}"
MANIFEST_MAX = 65536                                      # bytes; the device verifier has the same limit
SEQ_MAX = 2**31 - 1
_SAFE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9._+-]{0,63}\Z")
_HEX64 = re.compile(r"[0-9a-f]{64}\Z")
_VER = re.compile(r"\d{1,4}\.\d{1,4}(?:\.\d{1,4})?\Z")

# --- Ed25519 verification (RFC 8032 §5.1.7, cofactorless; the Python standard library has no Ed25519) ---
_P = 2**255 - 19
_L = 2**252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_SQRTM1 = pow(2, (_P - 1) // 4, _P)


def _ed_add(a, b):                                        # extended twisted Edwards coordinates (X, Y, Z, T)
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    pa, pb = (y1 - x1) * (y2 - x2) % _P, (y1 + x1) * (y2 + x2) % _P
    pc, pd = t1 * 2 * _D * t2 % _P, z1 * 2 * z2 % _P
    e, f, g, h = pb - pa, pd - pc, pd + pc, pb + pa
    return (e * f % _P, g * h % _P, f * g % _P, e * h % _P)


def _ed_mul(s, p):
    q = (0, 1, 1, 0)
    while s:
        if s & 1:
            q = _ed_add(q, p)
        p = _ed_add(p, p)
        s >>= 1
    return q


def _ed_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * pow(_D * y * y + 1, _P - 2, _P) % _P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P:
        x = x * _SQRTM1 % _P
    if (x * x - x2) % _P:
        return None
    return _P - x if (x & 1) != sign else x


def _ed_decode(b):
    if len(b) != 32:
        return None
    y = int.from_bytes(b, "little")
    sign, y = y >> 255, y & ((1 << 255) - 1)
    x = _ed_x(y, sign)
    return None if x is None else (x, y, 1, x * y % _P)


def _ed_encode(p):
    x, y, z, _ = p
    zi = pow(z, _P - 2, _P)
    x, y = x * zi % _P, y * zi % _P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


_ED_GY = 4 * pow(5, _P - 2, _P) % _P
_ED_G = (_ed_x(_ED_GY, 0), _ED_GY, 1, _ed_x(_ED_GY, 0) * _ED_GY % _P)


def ed25519_verify(pk, msg, sig):
    """True only for a valid RFC 8032 Ed25519 signature `sig` (64 bytes) of `msg` under public key `pk` (32 bytes)."""
    if len(pk) != 32 or len(sig) != 64:
        return False
    a, r = _ed_decode(pk), _ed_decode(sig[:32])
    s = int.from_bytes(sig[32:], "little")
    if a is None or r is None or s >= _L:
        return False
    k = int.from_bytes(hashlib.sha512(sig[:32] + pk + msg).digest(), "little") % _L
    return _ed_encode(_ed_mul(s, _ED_G)) == _ed_encode(_ed_add(r, _ed_mul(k, a)))


# --- signify-format keys / signatures, canonical manifest ---
class SigError(Abort):
    """A signed manifest was rejected. kind: signature | format | older | asset."""

    def __init__(self, kind, msg):
        super().__init__(msg)
        self.kind = kind


def signify_blob(text, kind_len):
    """The base64 line of a signify .pub (kind_len 42) or .sig (74) file -> (key id hex, payload bytes)."""
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if len(lines) != 2 or not lines[0].startswith("untrusted comment: "):
        raise SigError("signature", "not a signify file (expected 'untrusted comment:' + one base64 line)")
    try:
        raw = base64.b64decode(lines[1].strip(), validate=True)
    except ValueError:
        raise SigError("signature", "bad base64 in signify file")
    if len(raw) != kind_len or raw[:2] != b"Ed":
        raise SigError("signature", f"not an Ed25519 signify blob ({len(raw)} bytes)")
    return raw[2:10].hex(), raw[10:]


def manifest_bytes(obj):
    """Canonical serialization (the exact bytes that are signed): sorted keys, 1-space indent, ASCII, final newline."""
    return (json.dumps(obj, sort_keys=True, indent=1, ensure_ascii=True, separators=(",", ": ")) + "\n").encode()


def _strict_json(data):
    def pairs(kv):
        keys = [k for k, _ in kv]
        if len(set(keys)) != len(keys):
            raise ValueError("duplicate key")
        return dict(kv)

    def no_const(c):
        raise ValueError(f"constant {c} not allowed")

    def no_float(f):
        raise ValueError("floats are not allowed")
    return json.loads(data.decode("ascii"), object_pairs_hook=pairs, parse_constant=no_const, parse_float=no_float)


def _plain_strings(v):
    """Every key and string value is printable ASCII without '"' or '\\' (keeps the device's JSON parser trivial)."""
    if isinstance(v, str):
        return all(" " <= c <= "~" and c not in "\"\\" for c in v)
    if isinstance(v, dict):
        return all(_plain_strings(k) and _plain_strings(x) for k, x in v.items())
    if isinstance(v, list):
        return all(_plain_strings(x) for x in v)
    return True


def check_manifest(data, signed=True):
    """Parse + validate manifest bytes (format 1). Returns the dict. Raises SigError('format', …)."""
    def bad(msg):
        raise SigError("format", "manifest: " + msg)
    if len(data) > MANIFEST_MAX:
        bad(f"larger than {MANIFEST_MAX} bytes")
    try:
        m = _strict_json(data)
    except (ValueError, UnicodeDecodeError) as e:
        bad(f"not strict JSON ({e})")
    if not isinstance(m, dict) or not _plain_strings(m):
        bad("not an object of plain ASCII strings")
    if manifest_bytes(m) != data:
        bad("not in canonical form")
    isint = lambda v: isinstance(v, int) and not isinstance(v, bool)
    if m.get("format") != 1:
        bad(f"unsupported format {m.get('format')!r}")
    if m.get("product") != "merakiplus" or m.get("channel") != "stable":
        bad("wrong product/channel")
    ver = m.get("version")
    if not isinstance(ver, str) or not _VER.match(ver) or m.get("tag") != "v" + ver:
        bad("bad version/tag")
    if signed and not (isint(m.get("seq")) and 1 <= m["seq"] <= SEQ_MAX):
        bad("seq missing or out of range")
    if not signed and "seq" in m:
        bad("an unsigned manifest carries no seq")
    if not (isinstance(m.get("commit"), str) and re.fullmatch(r"[0-9a-f]{40}", m["commit"])):
        bad("bad commit")
    if not (isinstance(m.get("released"), str) and re.fullmatch(r"\d{4}-\d{2}-\d{2}", m["released"])):
        bad("bad released date")
    if m.get("base_url") != RELEASE_DL.format(tag=m["tag"], name=""):
        bad("base_url does not pin this repository's tag")
    assets = {}
    if not isinstance(m.get("assets"), list) or not m["assets"]:
        bad("no assets")
    for e in m["assets"]:
        if not (isinstance(e, dict) and isinstance(e.get("name"), str) and _SAFE.match(e["name"])
                and not e["name"].startswith("manifest") and isint(e.get("size")) and 0 <= e["size"] <= SEQ_MAX
                and isinstance(e.get("sha256"), str) and _HEX64.match(e["sha256"]) and e["name"] not in assets):
            bad(f"bad asset entry {e!r}")
        assets[e["name"]] = e
    for sect in ("release", "platform"):
        names = set()
        if not isinstance(m.get(sect), list) or not m[sect]:
            bad(f"no {sect} section")
        for e in m[sect]:
            n = e.get("name") if isinstance(e, dict) else None
            if not (isinstance(n, str) and n not in names and len(n) <= 128
                    and all(_SAFE.match(c) for c in n.split("/"))):
                bad(f"bad {sect} entry {e!r}")
            names.add(n)
            a = assets.get(e.get("src"))
            if a is None or a["size"] != e.get("size") or a["sha256"] != e.get("sha256"):
                bad(f"{sect} entry {n} does not match its asset {e.get('src')!r}")
            if sect == "release" and e.get("mode") not in ("644", "755"):
                bad(f"bad mode for {n}")
    return m


def verify_manifest(data, sig_text, keys=None, max_seq=None):
    """Signature first (Ed25519 by a trusted key), then the format, then anti-downgrade (seq >= max_seq).
    Returns the manifest dict with '_keyid' added."""
    keys = TRUSTED_KEYS if keys is None else keys
    if len(data) > MANIFEST_MAX:
        raise SigError("format", "manifest too large")
    keyid, sig = signify_blob(sig_text, 74)
    if keyid not in keys:
        raise SigError("signature", f"signed by an unknown key (id {keyid})")
    if not ed25519_verify(bytes.fromhex(keys[keyid]), data, sig):
        raise SigError("signature", f"BAD SIGNATURE (key id {keyid}) — manifest was modified or not signed by us")
    m = check_manifest(data, signed=True)
    if max_seq is not None and m["seq"] < max_seq:
        raise SigError("older", f"OLDER VERSION REFUSED: v{m['version']} has seq {m['seq']}, "
                                f"the display already accepted seq {max_seq}")
    m["_keyid"] = keyid
    return m


def verify_assets(m, files):
    """files: asset name -> bytes. Every given file must be listed with the same size + SHA-256."""
    byname = {e["name"]: e for e in m["assets"]}
    for n, b in files.items():
        e = byname.get(n)
        if e is None:
            raise SigError("asset", f"{n} is not listed in the signed manifest")
        if e["size"] != len(b) or e["sha256"] != sha256b(b):
            raise SigError("asset", f"{n} does not match the signed manifest (sha256 {sha256b(b)[:16]}…, "
                                    f"signed {e['sha256'][:16]}…)")


def fetch_signed_manifest(args, o):
    """(manifest bytes, sig text) for v<VERSION>: from --bin-dir when it holds them, else the GitHub release.
    None when the release has no signed manifest (or it cannot be reached)."""
    if getattr(args, "bin_dir", None) and (pathlib.Path(args.bin_dir) / "manifest.json").is_file():
        d = pathlib.Path(args.bin_dir)
        if not (d / "manifest.sig").is_file():
            raise SigError("signature", f"{d}/manifest.json has no manifest.sig next to it")
        o.say(f"  signed manifest: {d}/manifest.json")
        return (d / "manifest.json").read_bytes(), (d / "manifest.sig").read_text(encoding="ascii", errors="replace")
    got = []
    for name in ("manifest.json", "manifest.sig"):
        url = RELEASE_DL.format(tag="v" + VERSION, name=name)
        try:
            with urllib.request.urlopen(url, timeout=30) as r:
                got.append(r.read(MANIFEST_MAX + 1))
        except Exception as e:
            o.debug(f"signed manifest: {url}: {e}")
            return None
    o.say(f"  signed manifest: {RELEASE_DL.format(tag='v' + VERSION, name='manifest.json')}")
    return got[0], got[1].decode("ascii", errors="replace")


def check_signed_release(args, o, files, max_seq):
    """Verify the installed files against the release's signed manifest (computer side). Returns the manifest or None
    (unsigned / unreachable: a warning, or an abort with --require-signed). Never touches the display."""
    try:
        got = fetch_signed_manifest(args, o)
        if got is None:
            msg = (f"no signed manifest found for v{VERSION} (offline, or the release is not signed yet) — the files "
                   "were checked against this installer's pinned SHA-256 table only")
            if args.require_signed:
                raise Abort(msg.split(" — ")[0] + " and --require-signed was given. Nothing was changed.")
            o.warn(msg)
            return None
        m = verify_manifest(got[0], got[1],
                            max_seq=None if args.allow_downgrade or max_seq is None else max_seq)
        if m["version"] != VERSION:
            raise SigError("format", f"the signed manifest is for v{m['version']}, this installer installs v{VERSION}")
        verify_assets(m, files)
    except SigError as e:
        raise Abort(f"signed release check FAILED ({e.kind}): {e}. Nothing was changed."
                    + (" Use --allow-downgrade to install an older release on purpose." if e.kind == "older" else ""))
    o.ok(f"signed manifest v{m['version']} (seq {m['seq']}, key id {m['_keyid']}): signature valid, "
         f"all {len(files)} files match")
    if max_seq is not None and m["seq"] < max_seq:
        o.warn(f"DOWNGRADE on purpose (--allow-downgrade): seq {max_seq} -> {m['seq']}")
    return m


def cmd_verify_release(o, args):
    """verify-release DIR: a downloaded release (manifest.json + manifest.sig + assets) — no display needed."""
    d = pathlib.Path(args.dir)
    try:
        m = verify_manifest((d / "manifest.json").read_bytes(), (d / "manifest.sig").read_text(encoding="ascii",
                            errors="replace"), max_seq=args.max_seq)
    except OSError as e:
        raise Abort(f"cannot read the manifest: {e}")
    except SigError as e:
        raise Abort(f"REJECTED ({e.kind}): {e}")
    o.ok(f"manifest.sig: valid Ed25519 signature by key id {m['_keyid']}")
    o.ok(f"manifest: v{m['version']} seq {m['seq']} commit {m['commit'][:12]} released {m['released']}")
    missing, bad = [], []
    for e in m["assets"]:
        p = d / e["name"]
        if not p.is_file():
            missing.append(e["name"])
            continue
        b = p.read_bytes()
        if len(b) != e["size"] or sha256b(b) != e["sha256"]:
            bad.append(e["name"])
        else:
            o.ok(f"{e['name']}: {e['size']} bytes, sha256 {e['sha256'][:16]}…")
    for n in bad:
        o.warn(f"{n}: DOES NOT MATCH the signed manifest")
    for n in missing:
        o.say(f"  [-]  {n}: not in {d}")
    if bad or (missing and not args.partial):
        raise Abort(f"{len(bad)} file(s) differ, {len(missing)} missing" +
                    ("" if bad else " (use --partial when you downloaded only some assets)"))
    o.say(f"VERIFIED: {len(m['assets']) - len(missing)} file(s) match the signed manifest of v{m['version']}.")


# ============================================================ release directories (v6.21, docs/ota-plan.md §5)
def rel_name(version, ui_bytes):
    """Release directory name: <version>-<first 8 hex of the UI's SHA-256> (content-addressed: never rewritten)."""
    return f"{version}-{sha256b(ui_bytes)[:8]}"


def manifest(version, files):
    """MANIFEST of a release directory: one 'file <path> <size> <sha256>' line per file. The launcher checks the
    demo_v9 line before every start (size + SHA-256)."""
    return ("merakiplus-release 1\nversion " + version + "\n" +
            "".join(f"file {n} {len(b)} {sha256b(b)}\n" for n, b in files)).encode()


def is_b_family(exe):
    """Meraki+ started for slot B: the launcher path itself or a release it exec()ed."""
    return exe == B_EXE or (exe.startswith(REL_DIR + "/") and exe.endswith("/demo_v9")
                            and "/." not in exe[len(REL_DIR):] and exe.count("/") == REL_DIR.count("/") + 2)


def kv_text(text):
    return dict(l.split("=", 1) for l in text.splitlines() if "=" in l)


def ota_state_text(st):
    keys = ("format", "current", "previous", "pending", "tries", "last")
    d = dict(st)
    d["format"] = "1"
    return "".join(f"{k}={d.get(k, '')}\n" for k in keys) + "".join(f"{k}={v}\n" for k, v in st.items() if k not in keys)


def write_ota_state(dev, state):
    """tmp -> sync -> rename -> sync: a power cut leaves the old or the new state file (the launcher reads it)."""
    body = ota_state_text(state).replace("\n", "\\n")
    t = dev.d(OTA_STATE) + ".tmp.I"
    dev.w(f"mkdir -p {dev.d(OTA_DIR)} && printf '{body}' > {t} && sync && mv -f {t} {dev.d(OTA_STATE)} && sync")


# ============================================================ state inspection
def inspect(dev):
    st = {}
    rc, out = dev.sh(f"[ -f {dev.d(DEMO_SH)} ] && [ -f {dev.d(A_EXE)} ] && [ -f {dev.d(TEST_SH)} ] "
                     f"&& grep -q new_customer {dev.d(DEMO_SH)} && echo FP_OK; uname -m", check=False)
    st["fingerprint"] = "FP_OK" in out
    st["arch"] = out.splitlines()[-1].strip() if out.strip() else "?"
    _, mnt = dev.sh("cat /proc/mounts", check=False)
    st["b_mounted"] = bool(dev.p) or any(len(l.split()) > 1 and l.split()[1] == B_DIR for l in mnt.splitlines())
    st["procs"] = dev.ui_procs()
    st["watchdog"] = dev.watchdog_slot()
    st["flag"] = dev.exists(FLAG)
    st["b_exists"] = dev.exists(B_EXE)
    st["b_md5"] = dev.md5(B_EXE) if st["b_exists"] else None
    st["b_ours"] = dev.is_ours(B_EXE) if st["b_exists"] else False
    st["a_md5"] = dev.md5(A_EXE)
    st["legacy"] = legacy_inspect(dev)

    def where(path):      # current location, or the pre-rename one while a migration is pending
        if dev.exists(path):
            return path
        return legacy_path(path) if st["legacy"]["dir"] and dev.exists(legacy_path(path)) else None
    st["bak_b"] = bool(where(BAK_B))
    st["bak_b_absent"] = bool(where(BAK_B_ABSENT))
    st["bak_sh"] = bool(where(BAK_SH))
    _, g = dev.sh(f"grep -c -F '{GUARD_KEY}' {dev.d(DEMO_SH)} 2>/dev/null; true", check=False)
    st["guard_lines"] = (int(g.strip().splitlines()[-1]) if g.strip() and g.strip().splitlines()[-1].isdigit() else 0) \
        + st["legacy"]["guard"]
    st["switcher_pids"] = dev.pids_named("switcher")
    st["state"] = {}
    sp = where(STATE)
    if sp:
        _, s = dev.sh(f"cat {dev.d(sp)}", check=False)
        st["state"] = dict(l.split("=", 1) for l in s.splitlines() if "=" in l)
    _, df = dev.sh(f"df -k {dev.d(B_DIR)} 2>/dev/null | tail -n 1", check=False)
    try:
        st["b_free_kb"] = int(df.split()[3])
    except (IndexError, ValueError):
        st["b_free_kb"] = None
    _, lo = dev.sh("for f in " + " ".join(dev.d(g) for g in LEFTOVER_GLOBS) +
                   "; do [ -f \"$f\" ] && echo \"L $f\"; done; true", check=False)
    st["leftovers"] = [l[2:][len(dev.p):] for l in lo.splitlines() if l.startswith("L ")]
    pats = " ".join(f"-e '{m}'" for m in (MERAKIPLUS_MARK,) + LEGACY_MARKS)
    _, sb = dev.sh(f"for f in {dev.d(OURDIR)}/*; do [ -f \"$f\" ] && grep -q -F {pats} \"$f\" && echo \"S $f\"; done; true",
                   check=False)
    running = {e for _, e in st["procs"]}
    st["stray"] = [f for f in (l[2:][len(dev.p):] for l in sb.splitlines() if l.startswith("S "))
                   if f != STAGE and f not in running]
    # v6.21: launcher + release directories + the launcher's state and this boot's counters
    st["launcher"] = st["b_exists"] and dev.sh(f"grep -q -F '{LAUNCHER_MARK}' {dev.d(B_EXE)} 2>/dev/null && echo Y || echo N",
                                               check=False)[1].strip().endswith("Y")
    _, rl = dev.sh(f"for d in {dev.d(REL_DIR)}/* {dev.d(REL_DIR)}/.stage-* {dev.d(REL_DIR)}/.damaged-*; do [ -d \"$d\" ] && echo \"R ${{d##*/}}\"; done; true",
                   check=False)
    names = [l[2:].strip() for l in rl.splitlines() if l.startswith("R ")]
    st["rels"] = [n for n in names if not n.startswith(".")]
    st["rel_junk"] = [n for n in names if n.startswith(".")]
    _, o = dev.sh(f"cat {dev.d(OTA_STATE)} 2>/dev/null; true", check=False)
    st["ota"] = kv_text(o) if "format=1" in o else {}
    _, o = dev.sh(f"cat {dev.d(LAUNCH_VOL)} 2>/dev/null; echo; echo BOOTID=$(cat {dev.d(BOOT_ID)} 2>/dev/null)", check=False)
    st["boot_id"] = next((l[7:].strip() for l in o.splitlines() if l.startswith("BOOTID=")), "")
    st["launch"] = {k: v for k, v in kv_text(o).items() if k != "BOOTID"}
    _, o = dev.sh(f"cat {dev.d(HMARK)} 2>/dev/null; true", check=False)
    st["hmark"] = kv_text(o)
    st["vendor"] = vendor_traces(dev, st)
    return st


def demo_sh_base_md5(dev):
    """md5 of /customer/demo.sh without our guard line(s): the vendor part, which only a vendor update changes."""
    return md5b(b"".join(l for l in dev.read(DEMO_SH).splitlines(keepends=True)
                         if GUARD_KEY.encode() not in l and LEGACY_GUARD_KEY.encode() not in l))


def vendor_traces(dev, st):
    """Signs that a vendor (manufacturer) update ran or is about to run (docs/ota-plan.md §7 layer 3).
    Read-only. Only meaningful once Meraki+ was installed (our state file exists)."""
    tr = [f"{p} exists: a downloaded vendor update will be applied at the next boot "
          f"(it can overwrite slot B, demo.sh and the boot flag)" for p in VENDOR_PENDING if dev.exists(p)]
    if not st["state"]:
        return tr
    if st["b_exists"] and not st["b_ours"]:
        tr.append(f"{B_EXE} is no longer Meraki+ (replaced, md5 {st['b_md5']})")
    if dev.exists(SWITCHER) and not st["guard_lines"]:
        tr.append("the gesture is installed but /customer/demo.sh has no guard line any more (demo.sh was replaced)")
    elif st["state"].get("sh_md5") and demo_sh_base_md5(dev) != st["state"]["sh_md5"]:
        tr.append("/customer/demo.sh changed since Meraki+ was installed (vendor update?)")
    a0 = st["state"].get("a_md5")
    if a0 and st["a_md5"] and a0 != st["a_md5"]:
        tr.append("slot A (stock UI) changed since Meraki+ was installed: a vendor update was applied" +
                  ("" if st["flag"] else "; the boot flag is not set (a vendor update into slot A clears it)"))
    return tr


def slot_of(exe):
    if exe.startswith(REL_DIR + "/"):
        return "B, release " + exe[len(REL_DIR) + 1:].split("/")[0]
    return {A_EXE: "A (stock)", B_EXE: "B"}.get(exe, exe)


def print_status(o, st):
    o.say(f"  device fingerprint : {'MerakiTech/Sanbon display (OK)' if st['fingerprint'] else 'NOT RECOGNISED'}"
          f"  arch={st['arch']}")
    o.say(f"  running UI         : " + (", ".join(f"pid {p} = {slot_of(e)}" for p, e in st["procs"]) or "none"))
    o.say(f"  watchdog restarts  : slot {st['watchdog'] or '?'}")
    o.say(f"  boot flag          : {'SET -> next boot starts slot B' if st['flag'] else 'not set -> next boot starts slot A (stock)'}")
    b = "missing" if not st["b_exists"] else ("Meraki+" if st["b_ours"] else "vendor/other") + f" (md5 {st['b_md5']})"
    o.say(f"  slot B binary      : {b}")
    o.say(f"  slot A binary      : md5 {st['a_md5']} (never modified)")
    o.say(f"  demo.sh guard line : {st['guard_lines']}   switcher running: {bool(st['switcher_pids'])}")
    o.say(f"  backups            : slot-B {'yes' if st['bak_b'] else ('was-empty' if st['bak_b_absent'] else 'no')}"
          f", demo.sh {'yes' if st['bak_sh'] else 'no'}")
    if st["launcher"] or st["rels"] or st["ota"]:
        ota = st["ota"]
        o.say(f"  launcher           : {'installed in slot B' if st['launcher'] else 'NOT in slot B'}")
        o.say(f"  releases           : current {ota.get('current') or '-'}, previous {ota.get('previous') or '-'}"
              + (f", TRIAL {ota['pending']} (started {ota.get('tries', '0')}x)" if ota.get("pending") else "")
              + f"   [on flash: {', '.join(st['rels']) or 'none'}]")
        if ota.get("max_seq"):
            o.say(f"  signed releases    : newest accepted seq {ota['max_seq']} (older ones need --allow-downgrade)")
        if ota.get("last", "").startswith("rolled_back:"):
            r = ota["last"].split(":")
            o.say(f"  last rollback      : {r[1]} ({r[2] if len(r) > 2 else '?'}) — the launcher went back automatically")
        ln = st["launch"]
        if ln.get("boot") and ln.get("boot") == st["boot_id"]:
            lvl = {"0": "current", "1": "previous (fallback)", "2": "STOCK UI (fallback)"}.get(ln.get("level", "0"), "?")
            o.say(f"  this boot          : running {lvl}; {ln.get('rel', '?')} started {ln.get('streak', '?')}x in a row")
        hm = st["hmark"]
        if hm.get("boot") == st["boot_id"] and hm.get("boot"):
            o.say(f"  healthy marker     : {hm.get('state', '?')} (pid {hm.get('pid', '?')}, release {hm.get('rel') or '-'}, this boot)")
    if st["legacy"]["dir"] or st["legacy"]["guard"]:
        o.say("  older install      : found in the pre-rename layout — migrated by install / revert")
    if st["vendor"]:
        o.say("  vendor update      : DETECTED")
        for t in st["vendor"]:
            o.say("    - " + t)
        o.say("    -> run ./install.sh again with the options you used before (--persist / --gesture);")
        o.say("       backups and settings are kept. Block the vendor update server in your router to avoid repeats.")
    else:
        o.say("  vendor update      : no traces")
    if st["leftovers"]:
        o.say(f"  interrupted copies : {len(st['leftovers'])} temp file(s) — removed by the next install")
    if st["stray"]:
        o.say(f"  old builds         : {len(st['stray'])} stray Meraki+ build(s) in {OURDIR} (install offers to remove them)")
        for f in st["stray"]:
            o.say("    - " + f)


def require_device(st):
    if not st["fingerprint"]:
        raise Abort("This does not look like a MerakiTech/Sanbon display (no /customer/demo.sh, "
                    "/customer/demo_v9 and /customer/test.sh). Nothing was changed.")
    if not st["arch"].startswith("armv7"):
        raise Abort(f"Unexpected CPU architecture '{st['arch']}' (expected armv7l). Nothing was changed.")
    if not st["b_mounted"]:
        raise Abort("Slot B (/new_customer) is not mounted on this display, so Meraki+ cannot be "
                    "installed there. Nothing was changed.")


# ============================================================ activation (no reboot)
def find_new(dev, match, old, budget):
    t_end = time.time() + budget
    while time.time() < t_end:
        procs = dev.ui_procs()
        new = [p for p, e in procs if match(e) and p not in old]
        if new:
            return new[0], procs
        time.sleep(1)
    return None, dev.ui_procs()


def stop_first_switch(dev, o, target_dir, match, ldp, logname, exe_name):
    """Runtime switch without reboot: SIGSTOP the running UI(s) (the watchdog still sees the
    pid, so it does not race us), start the target, verify it, then kill the old one.
    On failure the old UI is resumed with SIGCONT and the new one is killed."""
    old = [p for p, _ in dev.ui_procs()]
    olds = " ".join(map(str, old))
    dev.changed = True
    if olds:
        dev.sh(f"for p in {olds}; do kill -STOP $p; done")
    dev.sh(f"cd {dev.d(target_dir)} && LD_LIBRARY_PATH={ldp} nohup ./demo_v9 > {dev.d('/tmp/' + logname)} "
           f"2>&1 < /dev/null & sleep 2")
    pid, procs = find_new(dev, match, old, 8)
    if pid is None:
        if olds:
            dev.sh(f"for p in {olds}; do kill -CONT $p; done", check=False)
        stray = [p for p, e in procs if match(e) and p not in old]
        if stray:
            dev.sh("kill -9 " + " ".join(map(str, stray)), check=False)
        raise Abort(f"{exe_name} did not start; the previous UI was resumed. "
                    f"See /tmp/{logname} on the display.")
    if olds:
        dev.sh(f"kill -9 {olds}; sleep 1", check=False)
    return pid


def live_exe(dev, pid):
    e = dev.sh(f"readlink {dev.d('/proc')}/{pid}/exe", check=False)[1].strip()
    if e.endswith(" (deleted)"):
        e = e[:-len(" (deleted)")]
    return e[len(dev.p):] if dev.p and e.startswith(dev.p) else e


def ui_can_reload(dev, pid):
    """v6.21+: the UI execs the launcher itself when idle (same PID, no signal, never during an activity)."""
    return dev.sh(f"grep -q -F '{RELOAD_MARK}' {dev.d('/proc')}/{pid}/exe 2>/dev/null && echo Y || echo N",
                  check=False)[1].strip().endswith("Y")


def activate_meraki(dev, o, st, want_md5, want_exe, reload_wait):
    """Make the new release the running UI. Returns its pid, or None when it is deferred (machine busy)."""
    procs = st["procs"]
    if len(procs) > 1:
        raise Abort(f"Several UIs are running at once ({procs}) — unusual state; not touching it. "
                    "Power-cycle the machine and run install again.")
    if procs and procs[0][1] == want_exe and dev.md5(f"/proc/{procs[0][0]}/exe") == want_md5:
        o.ok(f"Meraki+ {VERSION} is already the running UI (pid {procs[0][0]})")
        return procs[0][0]
    reloadable = bool(procs) and is_b_family(procs[0][1]) and ui_can_reload(dev, procs[0][0])
    if dev.dry:
        how = ("exec-reload by the running Meraki+ when idle (same PID)" if reloadable else
               "watchdog exec-reload (kill -TERM, the watchdog restarts the launcher)" if st["watchdog"] == "B" and procs
               else "stop-first runtime switch (SIGSTOP the running UI, start the launcher, verify, kill the old UI)")
        o.say(f"  [dry-run] would activate via {how}")
        return None
    if reloadable:
        pid = procs[0][0]
        o.say(f"  exec-reload: the running Meraki+ (pid {pid}) restarts itself into the new release as soon as the machine")
        o.say("  is idle on HOME (same PID — the watchdog never sees a gap; never during an activity)")
        dev.w(f"touch {dev.d(RELOAD)}")
        t_end = time.time() + reload_wait
        while time.time() < t_end:
            e = live_exe(dev, pid)
            if e == want_exe:
                return pid
            if not e:
                break
            time.sleep(1)
        if live_exe(dev, pid):
            o.warn(f"Meraki+ did not restart within {reload_wait} s — the machine is busy or not on HOME.")
            o.say("  The new version starts by itself when the machine is idle on HOME, or at the next power-on.")
            return None
        o.warn("the running Meraki+ ended instead of reloading — waiting for the watchdog")
        pid, _ = find_new(dev, is_b_family, [pid], 15)
        if pid is None:
            pid = stop_first_switch(dev, o, B_DIR, is_b_family, B_LDP, "merakiplus_boot.log", B_EXE)
        return pid
    if st["watchdog"] == "B" and procs:
        old = procs[0][0]
        o.say(f"  watchdog exec-reload: kill -TERM {old}; the vendor watchdog restarts {B_EXE} (the launcher)")
        dev.changed = True
        dev.sh(f"kill -TERM {old}")
        pid, _ = find_new(dev, is_b_family, [old], 15)
        if pid is None:
            o.warn("watchdog did not restart slot B within 15 s — starting it directly")
            pid = stop_first_switch(dev, o, B_DIR, is_b_family, B_LDP, "merakiplus_boot.log", B_EXE)
    else:
        o.say("  stop-first runtime switch: pause the running UI, start Meraki+ (launcher), verify, then stop the old UI")
        pid = stop_first_switch(dev, o, B_DIR, is_b_family, B_LDP, "merakiplus_boot.log", B_EXE)
    return pid


def health_fail(why, ip, trial_note=""):
    raise Abort(f"HEALTH CHECK FAILED: {why}\n"
                + (f"  {trial_note}\n" if trial_note else "") +
                f"  Go back to the stock UI now:\n"
                f"      ./revert.sh --ip {ip}\n"
                f"  and please open an issue with the log file.")


def verify_running(dev, o, pid, want_md5, want_exe, ip, trial_note=""):
    exe = live_exe(dev, pid)
    if not exe:
        health_fail(f"Meraki+ (pid {pid}) stopped right after it was started", ip, trial_note)
    if exe != want_exe:
        raise Abort(f"pid {pid} runs {exe}, not {want_exe} (the launcher chose another release — see "
                    f"./install.sh status and {OTA_LOG})")
    live = dev.md5(f"/proc/{pid}/exe")
    if live != want_md5:
        raise Abort(f"The running binary has md5 {live}, expected {want_md5}")
    o.ok(f"running UI pid {pid}: exe {want_exe}, live md5 {live}")


def ls_e_age(line, now_line):
    """Age in seconds from busybox `ls -lue` (atime, ctime(3) format) and `date` in the same local format."""
    pat = r"\w{3} (\w{3}) +(\d{1,2}) (\d\d:\d\d:\d\d) (\d{4})"
    a, b = re.search(pat, line), re.search(pat, now_line)
    if not (a and b):
        return None
    t = [datetime.datetime.strptime(" ".join(m.groups()), "%b %d %H:%M:%S %Y") for m in (a, b)]
    return int((t[1] - t[0]).total_seconds())


def controller_link(dev, pid):
    """(holds UART_DEV open, seconds since the last byte from the controller or None if not measurable).
    The kernel's tty layer sets the device node's access time on every successful read (8 s granularity),
    so the age of that atime tells whether the controller is talking — without touching the UI or the link."""
    u = dev.d(UART_DEV)
    _, out = dev.sh(f"echo FD=$(for f in {dev.d('/proc')}/{pid}/fd/*; do readlink $f; done 2>/dev/null | grep -c ttyS1); "
                    f"echo NOW=$(date +%s); echo AT=$(stat -c %X {u} 2>/dev/null); "
                    f"echo \"LS $(ls -lue {u} 2>/dev/null)\"; echo \"DT $(date '+%a %b %e %H:%M:%S %Y')\"", check=False)
    kv = {l.split("=", 1)[0]: l.split("=", 1)[1].strip() for l in out.splitlines() if re.match(r"^(FD|NOW|AT)=", l)}
    fd_open = kv.get("FD", "0").isdigit() and int(kv.get("FD", "0")) > 0
    if kv.get("NOW", "").isdigit() and kv.get("AT", "").isdigit():
        return fd_open, int(kv["NOW"]) - int(kv["AT"])
    ls = next((l[3:] for l in out.splitlines() if l.startswith("LS ")), "")
    dt = next((l[3:] for l in out.splitlines() if l.startswith("DT ")), "")
    return fd_open, ls_e_age(ls, dt)


def health_check(dev, o, pid, seconds, ip, trial_note=""):
    """Phase 1b 'healthy' (docs/ota-plan.md §5.4): the UI writes /tmp/merakiplus_healthy (boot_id + its PID) once it has
    run 60 s with the controller answering for 30 s. The same process must stay alive until then (no crash, no
    restart). Without the marker, the controller link is measured from outside (1a: tty atime) to say why."""
    def fail(why):
        health_fail(why, ip, trial_note)
    limit = seconds + (30 if seconds >= 20 else 3)
    o.say(f"  health check: waiting up to {limit} s for Meraki+ (pid {pid}) to report healthy "
          f"(running, controller answering) …")
    boot = dev.sh(f"cat {dev.d(BOOT_ID)} 2>/dev/null; true", check=False)[1].strip()
    t0, step = time.time(), (5 if seconds >= 20 else 1)
    while True:
        el = time.time() - t0
        procs = dev.ui_procs()
        if pid not in [p for p, _ in procs]:
            fail(f"Meraki+ (pid {pid}) stopped after {int(el)} s" +
                 (f"; now running: {', '.join(f'pid {p} = {slot_of(e)}' for p, e in procs)}" if procs else ""))
        hm = kv_text(dev.sh(f"cat {dev.d(HMARK)} 2>/dev/null; true", check=False)[1])
        if hm.get("pid") == str(pid) and hm.get("state") == "healthy" and (hm.get("boot") == boot or not boot):
            break
        if el >= limit:
            fd_open, age = controller_link(dev, pid)
            if not fd_open:
                fail(f"Meraki+ does not hold the controller link ({UART_DEV}) open")
            if age is not None and age > LINK_FRESH_S:
                fail(f"no data from the coffee machine's controller for {age} s (link {UART_DEV} is open but silent)")
            fail(f"Meraki+ did not report healthy within {limit} s (is the coffee machine switched on?)")
        time.sleep(min(step, max(0.2, limit - el)))
    o.ok(f"same process for {int(el)} s (no crash, no restart); Meraki+ reports healthy (boot {boot[:8] or '?'}, pid {pid})")
    fd_open, age = controller_link(dev, pid)
    if age is not None and fd_open:
        o.ok(f"controller link live: last data from the controller {max(age, 0)} s ago")
    _, lg = dev.sh(f"grep 'net-vagt startet' {dev.d('/tmp/merakiplus_net.log')} 2>/dev/null | tail -n 1", check=False)
    if ("V" + VERSION) in lg:
        o.ok(f"Meraki+ log confirms version V{VERSION}")


# ============================================================ install
def cmd_install(dev, o, args):
    o.step("1/8 Checking the display (read-only)")
    st = inspect(dev)
    print_status(o, st)
    require_device(st)
    if not st["procs"]:
        o.warn("no UI process running right now (the watchdog usually restarts one within a second)")

    o.step("2/8 Getting and verifying the Meraki+ files")
    merakiplus = get_binary("merakiplus", args, o)
    check_arm_elf(merakiplus, "merakiplus")
    want = BIN_MD5["merakiplus"]
    snd = get_binary("merakiplus_snd", args, o)                  # v6.18: sound helper + our own sounds
    check_arm_elf(snd, "merakiplus_snd")
    boot = get_binary("merakiplus_boot", args, o)                # v6.21: the launcher (/new_customer/demo_v9)
    check_arm_elf(boot, "merakiplus_boot")
    sounds = {n: get_binary(n, args, o, sub="audio") for n in AUDIO_MD5}
    extra = {}
    has_switcher = dev.exists(SWITCHER)
    if args.gesture or has_switcher:                             # an installed switcher must understand release paths
        extra["switcher"] = get_binary("switcher", args, o)
        check_arm_elf(extra["switcher"], "switcher")
        extra["grab_shim.so"] = get_binary("grab_shim.so", args, o)
        check_arm_elf(extra["grab_shim.so"], "grab_shim.so", shared=True)
    # OTA phase 2: the release's signed manifest (Ed25519, offline key) must list every file we install
    max_seq = int(st["ota"]["max_seq"]) if st["ota"].get("max_seq", "").isdigit() else None
    signed = check_signed_release(args, o, {"merakiplus": merakiplus, "merakiplus_snd": snd, "merakiplus_boot": boot,
                                            **sounds, **extra}, max_seq)
    rel = rel_name(VERSION, merakiplus)
    rel_exe = f"{REL_DIR}/{rel}/demo_v9"
    files = [("demo_v9", merakiplus), ("merakiplus_snd", snd)] + [("audio/" + n, b) for n, b in sounds.items()]
    man = manifest(VERSION, files)
    # free space: the new release + the launcher (+ its upload) + a migrated copy of the running build + margin
    need_kb = (sum(len(b) for _, b in files) * 2 + len(boot) * 2 + sum(map(len, extra.values()))) // 1024 + 512
    if st["b_free_kb"] is not None and st["b_free_kb"] < need_kb:
        raise Abort(f"Only {st['b_free_kb']} KB free on /new_customer — need about {need_kb} KB. Nothing was changed.")

    o.say("")
    o.say("  Meraki+ will replace the display's touch UI. The coffee machine's own controller is not")
    o.say("  changed. Make sure the machine is IDLE: not brewing, steaming, grinding or dispensing water.")
    if not dev.dry and not confirm("  Machine is idle and you accept the risks described in the README?", args.yes):
        raise Abort("Cancelled by user. Nothing was changed.")

    if legacy_migrate(dev, o, st, rewrite_guard=True) and not dev.dry:
        st = inspect(dev)
    if st["leftovers"] or st["rel_junk"]:
        dev.w("rm -f " + " ".join(dev.d(f) for f in st["leftovers"]) + "; " +
              "".join(f"rm -rf {dev.d(REL_DIR)}/{n}; " for n in st["rel_junk"]) + "sync")
        o.ok(f"removed {len(st['leftovers']) + len(st['rel_junk'])} temp file(s)/dir(s) left by an interrupted earlier run")
    if st["stray"]:
        if not args.yes and not dev.dry and sys.stdin.isatty() and \
                confirm(f"  Remove {len(st['stray'])} old Meraki+ build(s) from {OURDIR} (not used by anything)?", False):
            dev.w("rm -f " + " ".join(dev.d(f) for f in st["stray"]) + "; sync")
            o.ok("old builds removed")
        else:
            o.say(f"  {len(st['stray'])} old Meraki+ build(s) in {OURDIR} kept (see status; safe to delete by hand)")
    o.step("3/8 Backups (only taken once; never overwritten)")
    dev.w(f"mkdir -p {dev.d(BOOTDIR)}")
    if st["bak_b"] or st["bak_b_absent"]:
        o.ok("slot-B backup already exists — kept as is")
    elif not st["b_exists"]:
        dev.w(f"touch {dev.d(BAK_B_ABSENT)} && sync")
        o.ok("slot B was empty — recorded (revert will leave it empty again)")
    elif st["b_ours"]:
        o.warn("slot B already holds a Meraki+ build and there is no vendor backup "
               "(older manual install). Revert will then leave slot B empty and boot stock A.")
    else:
        dev.atomic_copy(B_EXE, BAK_B, st["b_md5"])
        o.ok(f"slot-B vendor binary -> {BAK_B} (md5 {st['b_md5']})")
    if st["bak_b"] and st["b_exists"] and not st["b_ours"] and dev.exists(BAK_B) and dev.md5(BAK_B) != st["b_md5"]:
        newer = f"{BAK_B}.{st['b_md5'][:8]}"              # a vendor update replaced slot B: keep that one too
        if not dev.exists(newer):
            dev.atomic_copy(B_EXE, newer, st["b_md5"])
            o.ok(f"slot B held a different vendor program (vendor update) -> extra backup {newer}")
    if st["bak_sh"]:
        o.ok("demo.sh backup already exists — kept as is")
    elif st["guard_lines"]:
        o.warn("demo.sh already contains a Meraki+ guard line but no backup exists; revert will strip the line")
    else:
        dev.atomic_copy(DEMO_SH, BAK_SH, dev.md5(DEMO_SH), mode="644")
        o.ok(f"/customer/demo.sh -> {BAK_SH}")
    state = dict(st["state"])
    if not state:
        state["orig_flag"] = "1" if st["flag"] else "0"
        state["orig_b"] = "absent" if not st["b_exists"] else ("ours" if st["b_ours"] else "vendor")
        state["first_install"] = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    state["a_md5"] = st["a_md5"] or ""                   # baselines for vendor-update detection (status):
    state["sh_md5"] = demo_sh_base_md5(dev)              # slot A and demo.sh as they are now
    if state != st["state"]:
        lines = "".join(f"{k}={v}\\n" for k, v in state.items())
        dev.w(f"printf '{lines}' > {dev.d(STATE)}.tmp && sync && mv -f {dev.d(STATE)}.tmp {dev.d(STATE)} && sync")
        o.ok("boot state recorded (used by revert and by vendor-update detection)")


    o.step(f"4/8 Release directory {REL_DIR}/{rel}")
    ota = dict(st["ota"])
    if not ota:
        old = migrate_flat(dev, o, st, rel)
        ota = {"current": old, "previous": "", "pending": "", "tries": "0", "last": ""}
    install_release(dev, o, rel, files, man)

    o.step("5/8 The launcher (/new_customer/demo_v9) and the release state")
    new_ota = dict(ota)
    if ota.get("current") == rel and not ota.get("pending"):
        o.ok(f"{rel} is already the committed release")
    else:
        new_ota.update(pending=rel, tries="0")                    # a trial: the launcher rolls it back if it never gets healthy
        if new_ota.get("current") == rel:
            new_ota["current"] = ota.get("previous", "")          # (re-trial of a build that is current: keep a real fallback)
    if signed:                                                    # anti-downgrade record (docs/signing.md)
        new_ota["max_seq"] = str(signed["seq"] if args.allow_downgrade else max(signed["seq"], max_seq or 0))
    if new_ota != st["ota"]:
        dev.w(f"rm -f {dev.d(OTA_HEALTHY)}")
        write_ota_state(dev, new_ota)
        o.ok(f"state: current {new_ota.get('current') or '-'}, previous {new_ota.get('previous') or '-'}, "
             f"trial {new_ota.get('pending') or '-'}")
    install_launcher(dev, o, st, boot)
    if dev.exists(STAGE):                                         # the v6.15..v6.20 staged copy is not used any more
        dev.w(f"rm -f {dev.d(STAGE)} && sync")
        o.ok(f"{STAGE} removed (replaced by the release directory)")

    o.step("6/8 Optional parts")
    if extra:
        install_switcher(dev, o, st, extra)
    if args.gesture:
        install_guard(dev, o)
    else:
        o.say("  --gesture not given: /customer/demo.sh is left untouched")
    if args.persist:
        if st["flag"]:
            o.ok("boot flag already set: Meraki+ starts at every boot")
        else:
            dev.w(f"touch {dev.d(FLAG)} && sync")
            o.ok("boot flag set: Meraki+ starts at every boot (undo: ./revert.sh, or BOOT-UI in Meraki+ settings)")
    elif st["flag"]:
        o.warn("the boot flag was ALREADY set before this install, so the machine boots slot B = Meraki+")
    else:
        o.say("  --persist not given: after a power cycle the machine starts the stock UI again")

    o.step("7/8 Activating Meraki+ now (no reboot)")
    if args.no_activate:
        o.say("  --no-activate given: the new version starts at the next power-on (or the next start of the launcher)")
        pid = None
    else:
        pid = activate_meraki(dev, o, inspect(dev) if not dev.dry else st, want, rel_exe, args.reload_wait)

    o.step("8/8 Verifying")
    prev = new_ota.get("current") or "the stock UI"
    trial_note = (f"Meraki+ {VERSION} stays on TRIAL: if it never becomes healthy, the launcher goes back to {prev} "
                  f"by itself after 3 failed starts." if new_ota.get("pending") == rel else "")
    if not dev.dry:
        if dev.md5(B_EXE) != BIN_MD5["merakiplus_boot"]:
            raise Abort("slot B does not hold the launcher (md5 changed unexpectedly)")
        for n, b in files:
            if dev.md5(f"{REL_DIR}/{rel}/{n}") != md5b(b):
                raise Abort(f"{REL_DIR}/{rel}/{n} has the wrong md5 — run ./revert.sh")
        if dev.md5(A_EXE) != st["a_md5"]:
            raise Abort("slot A binary changed — this must never happen; please open an issue")
        o.ok("slot A untouched; launcher and release directory verified")
    if pid and not dev.dry:
        verify_running(dev, o, pid, want, rel_exe, args.ip, trial_note)
        health_check(dev, o, pid, args.health_seconds, args.ip, trial_note)
        commit_release(dev, o, rel)
    o.say("")
    o.say(f"Done. Meraki+ {VERSION} is " + ("installed (dry-run: nothing was changed)" if dev.dry else
          "healthy and running" if pid else
          "installed as a trial; it starts by itself (idle HOME, or the next power-on)") + ".")
    o.say("  Check on the screen: the gear on HOME -> SYSTEM shows 'MERAKI+ V" + VERSION + "' under its title.")
    o.say(f"  Undo everything:     ./revert.sh --ip {args.ip}")


def install_release(dev, o, name, files, man, src=None):
    """A complete release directory, all or nothing: everything goes into rel/.stage-<name> (each file uploaded or
    copied, md5-verified, synced), the MANIFEST last, then `sync` and ONE rename to rel/<name>. A power cut leaves
    either no rel/<name> (the stage is removed by the next install) or the complete one. Existing directories are
    content-addressed and never rewritten; a damaged one is moved aside."""
    target, stage = f"{REL_DIR}/{name}", f"{REL_DIR}/.stage-{name}"
    if dev.exists(target):
        bad = [n for n, b in files if dev.md5(f"{target}/{n}") != md5b(b)] + \
              (["MANIFEST"] if dev.md5(f"{target}/MANIFEST") != md5b(man) else [])
        if not bad:
            o.ok(f"{target} already complete (md5 verified)")
            return
        o.warn(f"{target} is damaged ({', '.join(bad)}) — moved aside and installed again")
        dev.w(f"mv {dev.d(target)} {dev.d(REL_DIR)}/.damaged-{name}-{random.randrange(1 << 30)} && sync")
    dev.w(f"rm -rf {dev.d(stage)}; mkdir -p {dev.d(stage)}/audio")
    for n, b in files:
        mode = "644" if n.startswith("audio/") or n == "MANIFEST" else "755"
        if src and n in src:
            dev.w(f"cp {dev.d(src[n])} {dev.d(stage)}/{n} && chmod {mode} {dev.d(stage)}/{n}")
            if not dev.dry and dev.md5(f"{stage}/{n}") != md5b(b):
                raise Abort(f"copy of {src[n]} into {stage} has the wrong md5; nothing was switched")
        else:
            dev.push(b, f"{stage}/{n}", mode=mode)
    dev.push(man, f"{stage}/MANIFEST", mode="644")
    dev.w(f"sync && mv {dev.d(stage)} {dev.d(target)} && sync")
    o.ok(f"{target}: {len(files)} files + MANIFEST, verified, renamed into place in one step")


def migrate_flat(dev, o, st, new_rel):
    """First install with release directories: the Meraki+ build that slot B runs today (v6.17..v6.20 layout, the UI
    itself as /new_customer/demo_v9) becomes a release directory too, so it is the rollback target ('current' until the
    new version has proven itself). Copied on the display; the old files stay where they are."""
    if not (st["b_exists"] and st["b_ours"]) or st["launcher"]:
        return ""
    data = dev.read(B_EXE) if not dev.dry else b""
    m = re.search(rb"@\(#\)merakiplus V([0-9][0-9.]*) ", data)
    if dev.dry:
        o.say(f"  [dry-run] would copy the Meraki+ build in slot B into {REL_DIR}/<its version> (rollback target)")
        return ""
    if not m:
        o.warn("slot B holds a Meraki+ build without a version marker — not kept as a rollback target")
        return ""
    ver = m.group(1).decode()
    name = rel_name(ver, data)
    if name == new_rel:
        return ""
    files, src = [("demo_v9", data)], {"demo_v9": B_EXE}
    if dev.exists(SND_HELPER):
        files.append(("merakiplus_snd", dev.read(SND_HELPER)))
        src["merakiplus_snd"] = SND_HELPER
    for n in AUDIO_MD5:
        if dev.exists(f"{AUDIO_DIR}/{n}"):
            files.append(("audio/" + n, dev.read(f"{AUDIO_DIR}/{n}")))
            src["audio/" + n] = f"{AUDIO_DIR}/{n}"
    o.say(f"  migrating the running layout: Meraki+ V{ver} from {B_EXE} -> {REL_DIR}/{name} (rollback target)")
    install_release(dev, o, name, files, manifest(ver, files), src)
    return name


def install_launcher(dev, o, st, boot):
    """Upload the launcher next to slot B's program, run its selftest ON THE DISPLAY, then rename it over
    /new_customer/demo_v9 (after a sync). Releases and the state file are in place before this step, so every
    power cut leaves either the old program or the launcher with something to start."""
    want = BIN_MD5["merakiplus_boot"]
    if st["launcher"] and st["b_md5"] == want:
        o.ok(f"{B_EXE} already is the launcher (md5 {want})")
        return
    dev.push(boot, LAUNCHER_NEW, mode="755")
    if not dev.dry:
        host = os.environ.get("MERAKIPLUS_TEST_LAUNCHER_HOST") if dev.p else None    # mock display: run the host build
        if dev.p and not host:
            o.debug("mock display without MERAKIPLUS_TEST_LAUNCHER_HOST: on-display selftest skipped")
        else:
            exe = host or dev.d(LAUNCHER_NEW)
            _, out = dev.sh(f"rm -rf {dev.d(OTA_DIR)}/selftest; {exe} --selftest {dev.d(OTA_DIR)}/selftest 2>&1; true",
                            check=False)
            if not re.search(r"selftest: (\d+)/\1 OK", out):
                dev.sh(f"rm -f {dev.d(LAUNCHER_NEW)}", check=False)
                raise Abort(f"the launcher's selftest failed on the display — nothing was switched:\n{out[-400:]}")
            o.ok("launcher selftest on the display: " + out.strip().splitlines()[-1])
    dev.w(f"mv -f {dev.d(LAUNCHER_NEW)} {dev.d(B_EXE)} && sync")
    if not dev.dry and dev.md5(B_EXE) != want:
        raise Abort(f"{B_EXE} has the wrong md5 after the rename — run ./revert.sh")
    o.ok(f"{B_EXE} is now the launcher (md5 {want}); the previous program in slot B is in the backups")


def install_switcher(dev, o, st, extra):
    """Switcher + shim up to date; a running switcher of an older build is restarted (v6.21: it must recognise a
    release started by the launcher as Meraki+), with the same LD_PRELOAD the demo.sh guard line gives it."""
    changed = False
    for name, dest in (("switcher", SWITCHER), ("grab_shim.so", SHIM)):
        if dev.md5(dest) == BIN_MD5[name]:
            o.ok(f"{dest} up to date")
        else:
            dev.push(extra[name], dest, mode="755")
            o.ok(f"{dest} uploaded and verified")
            changed |= name == "switcher"
    pids = st["switcher_pids"]
    if pids and (changed or dev.md5(f"/proc/{pids[0]}/exe") != BIN_MD5["switcher"]):
        dev.w("kill " + " ".join(map(str, pids)) + " 2>/dev/null; sleep 1; true")
        left = [p for p in dev.pids_named("switcher") if p in pids]
        if left:
            dev.w("kill -9 " + " ".join(map(str, left)) + " 2>/dev/null; true")
        pre = f"LD_PRELOAD={dev.d(SHIM)} " if dev.exists(SHIM) else ""
        dev.w(f"cd / && {pre}nohup {dev.d(SWITCHER)} > /dev/null 2>&1 < /dev/null & sleep 1")
        o.ok("gesture switcher restarted with the new build" if dev.dry or dev.pids_named("switcher")
             else "gesture switcher stopped; it starts again at the next power-on")


def commit_release(dev, o, rel):
    """Healthy: current=<rel>, previous=<old current>, trial cleared (the launcher would do the same at its next start).
    Then only what current/previous name is kept in rel/."""
    ota = kv_text(dev.sh(f"cat {dev.d(OTA_STATE)} 2>/dev/null; true", check=False)[1])
    if ota.get("pending") == rel:
        if ota.get("current") != rel:
            ota["previous"] = ota.get("current", "")
        ota.update(current=rel, pending="", tries="0", last="ok:" + rel)
        write_ota_state(dev, ota)
        o.ok(f"committed: current {rel}, previous {ota.get('previous') or '-'}")
    keep = {ota.get("current", ""), ota.get("previous", ""), ota.get("pending", "")} - {""}
    _, rl = dev.sh(f"for d in {dev.d(REL_DIR)}/*; do [ -d \"$d\" ] && echo \"R ${{d##*/}}\"; done; true", check=False)
    old = [l[2:].strip() for l in rl.splitlines() if l.startswith("R ") and l[2:].strip() not in keep]
    if old:
        dev.w("".join(f"rm -rf {dev.d(REL_DIR)}/{n}; " for n in old) + "sync")
        o.ok(f"removed {len(old)} older release(s) not needed for a rollback: {', '.join(old)}")


def install_guard(dev, o):
    content = dev.read(DEMO_SH).decode("utf-8", "surrogateescape")
    lines = content.splitlines(keepends=True)
    ours = [l for l in lines if GUARD_KEY in l or LEGACY_GUARD_KEY in l]
    if len(ours) == 1 and ours[0].rstrip("\n") == GUARD_LINE:
        o.ok("demo.sh guard line already present")
        return
    base = [l for l in lines if GUARD_KEY not in l and LEGACY_GUARD_KEY not in l]
    anchors = [i for i, l in enumerate(base) if l.rstrip("\n") == ANCHOR]
    insm = [i for i, l in enumerate(base) if l.strip() == INSMOD_MARKER]
    if not anchors or not insm or anchors[-1] < insm[-1] or \
            "starting from B" not in "".join(base[anchors[-1]:anchors[-1] + 4]):
        raise Abort("demo.sh does not have the expected layout (slot branch after '# kernel_mod_list'). "
                    "Your firmware differs from the one Meraki+ was developed on — skipping --gesture "
                    "is safe; demo.sh was NOT changed.")
    base.insert(anchors[-1], GUARD_LINE + "\n")
    new = "".join(base).encode("utf-8", "surrogateescape")
    dev.push(new, DEMO_SH, mode="755")
    if not dev.dry:
        back = dev.read(DEMO_SH)
        if back != new:
            raise Abort("demo.sh read-back differs from what was written — run ./revert.sh now")
    o.ok("demo.sh: one guard line added (starts the gesture switcher at boot)")
    o.say("  note: the gesture becomes active from the next power-on (the switcher and the touch-grab")
    o.say("        shim for the stock UI are started by demo.sh at boot)")


# ============================================================ revert
def cmd_revert(dev, o, args):
    o.step("1/6 Checking the display (read-only)")
    st = inspect(dev)
    print_status(o, st)
    require_device(st)
    nothing = (not st["b_ours"] and not st["guard_lines"] and not st["switcher_pids"]
               and not any(is_b_family(e) for _, e in st["procs"]) and not st["state"]
               and not st["rels"] and not st["ota"] and not st["launcher"])
    if nothing and not args.purge:
        o.say("\nMeraki+ is not installed here — nothing to revert.")
        return
    if not dev.dry and not confirm("  Revert this display to the stock UI now?", args.yes):
        raise Abort("Cancelled by user. Nothing was changed.")
    if legacy_migrate(dev, o, st, rewrite_guard=False) and not dev.dry:
        st = dict(inspect(dev), a_md5=st["a_md5"])

    def at(path):         # --dry-run: a pending migration has not moved the file yet
        return legacy_path(path) if dev.dry and st["legacy"]["dir"] and not dev.exists(path) else path

    o.step("2/6 Switching the running UI back to stock (no reboot)")
    running_ours = [p for p, e in st["procs"] if (e == B_EXE and st["b_ours"]) or (is_b_family(e) and e != B_EXE)]
    if running_ours:
        if dev.dry:
            o.say("  [dry-run] would stop Meraki+ and start the stock UI from slot A (stop-first)")
        else:
            pid = stop_first_switch(dev, o, A_DIR, lambda e: e == A_EXE, A_LDP, "a_relaunch.log", A_EXE)
            o.ok(f"stock UI running again (pid {pid}, {A_EXE})")
    else:
        o.ok("Meraki+ is not the running UI")

    o.step("3/6 Restoring /customer/demo.sh")
    if st["guard_lines"]:
        cur = dev.read(DEMO_SH)
        stripped = b"".join(l for l in cur.splitlines(keepends=True)
                            if GUARD_KEY.encode() not in l and LEGACY_GUARD_KEY.encode() not in l)
        bak_md5 = dev.md5(at(BAK_SH)) if st["bak_sh"] else None
        if bak_md5 and bak_md5 == md5b(stripped):
            dev.atomic_copy(BAK_SH, DEMO_SH, bak_md5)
            o.ok(f"demo.sh restored from backup (md5 {bak_md5})")
        else:
            if bak_md5:
                o.warn("demo.sh changed since the backup (e.g. a vendor update) — removing only our line")
            dev.push(stripped, DEMO_SH, mode="755")
            o.ok("guard line removed from demo.sh")
    else:
        o.ok("demo.sh has no Meraki+ line")

    o.step("4/6 Removing the gesture switcher")
    if st["switcher_pids"]:
        dev.w("kill " + " ".join(map(str, st["switcher_pids"])) + " 2>/dev/null; true")
    dev.w(f"rm -f {dev.d(SWITCHER)} {dev.d(SHIM)} {dev.d(BOOTDIR + '/tinj_hold')} {dev.d(BOOTDIR + '/fbpage')}; sync")
    o.ok("switcher stopped and removed (if it was there)")

    o.step("5/6 Restoring slot B and the boot flag")
    restored_vendor = False
    if st["bak_b"]:
        bmd5 = dev.md5(at(BAK_B))
        if st["b_md5"] == bmd5:
            o.ok("slot B already holds the vendor binary")
        else:
            dev.atomic_copy(BAK_B, B_EXE, bmd5)
            o.ok(f"slot B vendor binary restored (md5 {bmd5})")
        restored_vendor = True
    elif st["b_ours"]:
        dev.w(f"rm -f {dev.d(B_EXE)} && sync")
        o.ok("Meraki+ removed from slot B (slot B was empty / had no vendor backup)")
    orig_flag = st["state"].get("orig_flag") == "1"
    if orig_flag and restored_vendor:
        if not st["flag"]:
            dev.w(f"touch {dev.d(FLAG)} && sync")
        o.ok("boot flag kept as it was before Meraki+ (vendor slot B)")
    elif st["flag"]:
        dev.w(f"rm -f {dev.d(FLAG)} && sync")
        o.ok("boot flag removed: next boot starts the stock UI (slot A)")
    else:
        o.ok("boot flag not set: next boot starts the stock UI (slot A)")
    if args.purge:
        dev.w(f"rm -rf {dev.d(OURDIR)} {dev.d(LEGACY_DIR)} && sync")
        o.ok(f"{OURDIR} removed (backups, language file and settings backups included)")
    else:
        dev.w(f"rm -f {dev.d(STAGE)} {dev.d(STATE)} {dev.d(SND_HELPER)} {dev.d(LAUNCHER_NEW)}; "
              f"rm -rf {dev.d(AUDIO_DIR)} {dev.d(REL_DIR)} {dev.d(OTA_DIR)}; sync")
        o.say(f"  backups kept in {BOOTDIR} (remove with --purge); the launcher's releases ({REL_DIR}), its state and "
              f"the sound player were removed")
    dev.w(f"rm -f {dev.d(HMARK)} {dev.d(RELOAD)} {dev.d(LAUNCH_VOL)}")

    o.step("6/6 Verifying")
    if dev.dry:
        o.say("\nDry-run finished: nothing was changed.")
        return
    end = inspect(dev)
    print_status(o, end)
    if end["a_md5"] != st["a_md5"]:
        raise Abort("slot A changed — this must never happen; please open an issue")
    if end["guard_lines"] or end["switcher_pids"]:
        raise Abort("demo.sh / switcher not fully removed — see the status above")
    if any((e == B_EXE and end["b_ours"]) or (is_b_family(e) and e != B_EXE) for _, e in end["procs"]):
        raise Abort("Meraki+ is still running")
    o.say("\nDone. The display runs the stock UI; Meraki+ is removed.")


# ============================================================ scan / status
def local_ipv4():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))            # no packet is sent; just picks the outgoing interface
        return s.getsockname()[0]
    finally:
        s.close()


def cmd_scan(o, args):
    net = ipaddress.ip_network(args.subnet, strict=False) if args.subnet else \
        ipaddress.ip_network(local_ipv4() + "/24", strict=False)
    o.say(f"Scanning {net} for an open telnet port (23) …")

    def probe(ip):
        try:
            socket.create_connection((str(ip), 23), timeout=0.6).close()
            return str(ip)
        except OSError:
            return None
    with ThreadPoolExecutor(64) as ex:
        hits = [h for h in ex.map(probe, net.hosts()) if h]
    found = []
    for ip in hits:
        dev = Device(ip, 23, Out())
        try:
            dev.connect()
            st_fp = dev.sh(f"[ -f {A_EXE} ] && [ -f {DEMO_SH} ] && [ -d {B_DIR} ] && echo FP_OK", timeout=8,
                           check=False)[1]
            if "FP_OK" in st_fp:
                found.append(ip)
                o.say(f"  {ip}: MerakiTech/Sanbon display")
            else:
                o.say(f"  {ip}: telnet, but not a MerakiTech display")
        except Abort:
            o.say(f"  {ip}: telnet, no usable shell")
        finally:
            dev.close()
    if not found:
        raise Abort("No display found. Is the machine switched on and on the same Wi-Fi/LAN?")
    return found


def resolve_ip(o, args):
    if args.ip:
        return args.ip
    if args.scan:
        found = cmd_scan(o, args)
        if len(found) > 1:
            raise Abort("More than one display found — pass --ip explicitly.")
        o.say(f"Using {found[0]}")
        return found[0]
    raise Abort("Give the display's address with --ip ADDR (or use --scan).")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="merakiplus", description="Meraki+ installer for MerakiTech Gen1/Gen2 displays")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--ip", help="display IP address")
        p.add_argument("--scan", action="store_true", help="find the display on the local /24 network")
        p.add_argument("--subnet", help="network to scan instead of the local /24, e.g. 192.168.0.0/24")
        p.add_argument("--port", type=int, default=23, help=argparse.SUPPRESS)
        p.add_argument("--dry-run", action="store_true", help="show what would happen; change nothing")
        p.add_argument("--yes", "-y", action="store_true", help="do not ask for confirmation")
        p.add_argument("--verbose", "-v", action="store_true", help="print every device command")
        p.add_argument("--log", help="log file (default: logs/merakiplus-<time>.log)")
        p.add_argument("--test-prefix", help=argparse.SUPPRESS)      # mock device (tests/)
    p = sub.add_parser("install", help="install / update Meraki+ in slot B and start it")
    common(p)
    p.add_argument("--slot", default="B", choices=["B"], help="target slot (only B is supported)")
    p.add_argument("--persist", action="store_true", help="also set the boot flag: start Meraki+ at every boot")
    p.add_argument("--gesture", action="store_true",
                   help="also install the center-hold gesture switcher (adds ONE line to /customer/demo.sh)")
    p.add_argument("--no-activate", action="store_true", help="install only; do not start Meraki+ now")
    p.add_argument("--ref", default=os.environ.get("MERAKIPLUS_REF", "main"), help="git ref to download bin/ from")
    p.add_argument("--bin-dir", help="use binaries from this directory (sha256 + md5 checked; a signed manifest.json + "
                                     "manifest.sig in it is used instead of downloading them)")
    p.add_argument("--require-signed", action="store_true",
                   help="refuse to install unless the release's signed manifest verifies (docs/signing.md)")
    p.add_argument("--allow-downgrade", action="store_true",
                   help="allow a signed release older than the newest one this display accepted")
    p.add_argument("--health-seconds", type=int, default=HEALTH_S, help=argparse.SUPPRESS)   # tests
    p.add_argument("--reload-wait", type=int, default=45, help=argparse.SUPPRESS)              # tests
    p = sub.add_parser("revert", help="restore the stock UI and remove Meraki+")
    common(p)
    p.add_argument("--purge", action="store_true", help="also delete /new_customer/merakiplus (backups, language, settings backups)")
    p = sub.add_parser("status", help="read-only overview")
    common(p)
    p = sub.add_parser("scan", help="find displays on the local network")
    p.add_argument("--subnet")
    p = sub.add_parser("verify-release", help="verify a downloaded release (manifest.json + manifest.sig + assets)")
    p.add_argument("dir", help="directory with manifest.json, manifest.sig and the downloaded assets")
    p.add_argument("--max-seq", type=int, help="refuse a release older than this seq (anti-downgrade)")
    p.add_argument("--partial", action="store_true", help="only some assets were downloaded (missing ones are OK)")
    args = ap.parse_args(argv)

    logfile = None
    if args.cmd in ("install", "revert"):
        logfile = args.log or str(repo_root() / "logs" / datetime.datetime.now().strftime("merakiplus-%Y%m%d-%H%M%S.log"))
        pathlib.Path(logfile).parent.mkdir(parents=True, exist_ok=True)
    o = Out(logfile, getattr(args, "verbose", False))
    dev = None
    try:
        if args.cmd == "scan":
            cmd_scan(o, args)
            return 0
        if args.cmd == "verify-release":
            cmd_verify_release(o, args)
            return 0
        ip = resolve_ip(o, args)
        args.ip = ip
        dev = Device(ip, args.port, o, dry=args.dry_run, prefix=args.test_prefix or "")
        dev.connect()
        if args.cmd == "status":
            print_status(o, inspect(dev))
        elif args.cmd == "install":
            if args.dry_run:
                o.say("DRY-RUN: reads only, nothing on the display will be changed.")
            cmd_install(dev, o, args)
        elif args.cmd == "revert":
            if args.dry_run:
                o.say("DRY-RUN: reads only, nothing on the display will be changed.")
            cmd_revert(dev, o, args)
        if logfile:
            o.say(f"Log: {logfile}")
        return 0
    except Abort as e:
        o.say(f"\nSTOPPED: {e}")
        if dev is not None and dev.changed:
            o.say(f"Slot A (the stock UI) is never modified. To get back to a clean stock state: ./revert.sh --ip {args.ip}")
        elif dev is not None:
            o.say("Nothing on the display was changed.")
        if logfile:
            o.say(f"Log: {logfile}")
        return 1
    except KeyboardInterrupt:
        o.say("\nInterrupted. Every step is atomic; run ./install.sh again or ./revert.sh to get a clean state.")
        return 130
    finally:
        if dev:
            dev.close()


if __name__ == "__main__":
    sys.exit(main())
