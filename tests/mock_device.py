#!/usr/bin/env python3
"""mock_device.py — a fake MerakiTech display for testing tools/merakiplus.py without hardware.

It serves a "telnet" shell (plain TCP, no login) on 127.0.0.1 backed by bash. Device paths are
relocated under a sandbox root via merakiplus' hidden --test-prefix option, and the few things a
sandbox cannot do for real are emulated with shell functions:

  * processes: <root>/proc/<pid>/{exe,cmdline,exe_path,image}. `readlink .../exe` reports the
    launched path, or "<path> (deleted)" once that file was replaced (inode semantics).
  * kill -STOP/-CONT/-TERM/-9 act on fake pids; when no UI is left the vendor watchdog is
    emulated (it restarts the slot recorded in <root>/watchdog_slot).
  * nohup ./demo_v9 spawns a fake UI process; nc -l -p PORT is a tiny Python listener.
  * uname -m says armv7l.
  * sync / mv / chmod / cp are logged to <root>/events in order (sync-before-rename tests).
  * every spawned UI holds <root>/dev/ttyS1 open (fd/3); `stat -c %X` on it reports "now" (a live
    controller link) unless <root>/uart_dead exists (silent link, 5 min old) or <root>/no_stat exists
    (stat unavailable). <root>/no_uart_fd: new UIs do not open the link. <root>/crash_new_ui: a UI
    started from slot B dies 5 s after it started (the watchdog emulation then restarts a UI). <root>/no_sha256: no sha256sum on the display.
    <root>/df_kb: `df` reports that many free KB.
  * v6.21 launcher: spawning /new_customer/demo_v9 when it is the launcher starts what the launcher would start —
    asked from the real host build (MERAKIPLUS_TEST_LAUNCHER_HOST, `--dry`), else pending > current > stock. A UI from a
    release directory writes the healthy marker (/tmp/merakiplus_healthy) after 1 s (not with crash_new_ui / uart_dead /
    no_uart_fd / no_healthy) and, when its binary supports it, exec-reloads (same pid) on /tmp/merakiplus_reload
    unless <root>/ui_busy exists. nohup <path>/switcher starts a fake switcher process.
  * <root>/snap_on: every `sync` copies /new_customer into <root>/../snaps/NNNN (power-cut replay of the installer).

Everything here is synthetic — no vendor files are used. demo.sh below only reproduces the
lines the installer anchors on.
"""
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import threading

DEMO_SH = """#!/bin/sh
# SYNTHETIC test fixture — not the vendor script. Only the structure the installer relies on.
MOUNT_SUCCESS=0
ubiattach -m 6 && mount -t ubifs ubi0:new_customer /new_customer && MOUNT_SUCCESS=1
if [ -f /customer/temp.tar.gz ]; then
    if [ "$MOUNT_SUCCESS" -eq 1 ]; then
        if [ ! -f /customer/new ]; then
            touch /customer/new
        fi
    fi
fi
if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then
\techo "ota bookkeeping"
fi
insmod /config/modules/4.9.84/fake_touch.ko
# kernel_mod_list

if [ "$MOUNT_SUCCESS" -eq 1 ] && [ -f /customer/new ]; then
    echo "===> starting from B"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/new_customer/libs:/config/wifi
    cd /new_customer
    ./demo_v9 &
    sleep 3
    /customer/test.sh new_customer &
else
    echo "===> starting from A"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/customer/libs:/config/wifi
    cd /customer
    ./demo_v9 &
    sleep 3
    ./test.sh customer &
fi
"""

FUNCS = r'''
R="$MOCK_ROOT"
uname() { [ "$1" = "-m" ] && echo armv7l || command uname "$@"; }
command -v md5sum >/dev/null 2>&1 || md5sum() {
  rc=0; for f in "$@"; do if [ -e "$f" ]; then echo "$(md5 -q "$f")  $f"; else echo "md5sum: $f: No such file" >&2; rc=1; fi; done; return $rc; }
mock_next_pid() { n=$(cat "$R/pidcounter"); n=$((n+1)); echo $n > "$R/pidcounter"; echo $n; }
mock_launcher_target() {  # what the launcher (/new_customer/demo_v9) would exec right now
  if [ -n "${MERAKIPLUS_TEST_LAUNCHER_HOST:-}" ]; then
    t=$(MERAKIPLUS_BOOT_ROOT="$R" "$MERAKIPLUS_TEST_LAUNCHER_HOST" --dry 2>/dev/null | /usr/bin/sed -n 's/^exec \([^ ]*\) .*/\1/p')
    [ -n "$t" ] && { echo "$R$t"; return 0; }
  fi
  st="$R/new_customer/merakiplus/ota/state"
  for k in pending current; do c=$(/usr/bin/grep "^$k=" "$st" 2>/dev/null | /usr/bin/cut -d= -f2-)
    [ -n "$c" ] && [ -f "$R/new_customer/merakiplus/rel/$c/demo_v9" ] && { echo "$R/new_customer/merakiplus/rel/$c/demo_v9"; return 0; }; done
  echo "$R/customer/demo_v9"; }
mock_mark() {  # $1 pid, $2 exe: a release writes its healthy marker after 1 s
  [ -f "$R/crash_new_ui" ] || [ -f "$R/uart_dead" ] || [ -f "$R/no_uart_fd" ] || [ -f "$R/no_healthy" ] && return 0
  rn=${2%/demo_v9}; rn=${rn##*/}
  ( /bin/sleep 1; [ -d "$R/proc/$1" ] && printf 'boot=%s\npid=%s\nrel=%s\nup=61\nstate=healthy\n' "$(/bin/cat "$R/proc/sys/kernel/random/boot_id")" "$1" "$rn" > "$R/tmp/merakiplus_healthy" ) >/dev/null 2>&1 &
  return 0; }
mock_reload() {  # $1 pid: the UI execs the launcher (same pid)
  t=$(mock_launcher_target); /bin/cp "$t" "$R/proc/$1/image"; printf '%s' "$t" > "$R/proc/$1/exe_path"
  echo "reload $1 $t" >> "$R/events"; case "$t" in "$R/new_customer/merakiplus/rel/"*) mock_mark "$1" "$t";; esac; }
mock_spawn() {  # $1 = absolute exe path, $2 = argv0
  x="$1"
  if [ "$x" = "$R/new_customer/demo_v9" ] && /usr/bin/grep -q -F '@(#)merakiplus-launcher ' "$x" 2>/dev/null; then x=$(mock_launcher_target); fi
  p=$(mock_next_pid); mkdir -p "$R/proc/$p/fd"; /bin/cp "$x" "$R/proc/$p/image"
  printf '%s' "$x" > "$R/proc/$p/exe_path"; ln -s "$R/proc/$p/image" "$R/proc/$p/exe"
  [ -f "$R/no_uart_fd" ] || ln -s "$R/dev/ttyS1" "$R/proc/$p/fd/3"
  printf '%s\000' "$2" > "$R/proc/$p/cmdline"; echo "spawn $p $x" >> "$R/events"
  case "$x" in "$R/new_customer/"*) [ -f "$R/crash_new_ui" ] && { (sleep 5; kill -9 $p) >/dev/null 2>&1 & };; esac
  case "$x" in "$R/new_customer/merakiplus/rel/"*) mock_mark $p "$x"
    if /usr/bin/grep -q -F /tmp/merakiplus_reload "$x"; then
      ( i=0; while [ -d "$R/proc/$p" ] && [ $i -lt 400 ]; do i=$((i+1))
          if [ -f "$R/tmp/merakiplus_reload" ] && [ ! -f "$R/ui_busy" ]; then /bin/rm -f "$R/tmp/merakiplus_reload"; mock_reload $p; fi
          /bin/sleep 0.5; done ) >/dev/null 2>&1 &
    fi;; esac; return 0; }
mock_spawn_daemon() {  # $1 = absolute exe path (e.g. the switcher)
  p=$(mock_next_pid); mkdir -p "$R/proc/$p/fd"; /bin/cp "$1" "$R/proc/$p/image"; printf '%s' "$1" > "$R/proc/$p/exe_path"
  ln -s "$R/proc/$p/image" "$R/proc/$p/exe"; printf '%s\000' "${1##*/}" > "$R/proc/$p/cmdline"; echo "spawn $p $1" >> "$R/events"; return 0; }
sync() { echo "sync" >> "$R/events"; command sync
  if [ -f "$R/snap_on" ]; then n=$(/bin/ls "$R/../snaps" | /usr/bin/wc -l | /usr/bin/tr -d ' '); /bin/cp -Rp "$R/new_customer" "$R/../snaps/$(printf %04d $n)"; fi; }
mv() { echo "mv $*" >> "$R/events"; command mv "$@"; }
chmod() { echo "chmod $*" >> "$R/events"; command chmod "$@"; }
cp() { echo "cp $*" >> "$R/events"; command cp "$@"; }
stat() {
  if [ "$1" = "-c" ] && [ "$2" = "%X" ]; then
    [ -f "$R/no_stat" ] && return 1
    case "$3" in */dev/ttyS1) if [ -f "$R/uart_dead" ]; then echo $(( $(date +%s) - 300 )); else date +%s; fi; return 0;; esac
  fi
  command stat "$@"; }
df() { if [ -f "$R/df_kb" ]; then echo "Filesystem 1K-blocks Used Available Use% Mounted on"
  echo "ubi0:new_customer 111704 1 $(cat "$R/df_kb") 99% /new_customer"; else command df "$@"; fi; }
sha256sum() { [ -f "$R/no_sha256" ] && { echo "sha256sum: applet not found" >&2; return 127; }; command sha256sum "$@" 2>/dev/null || shasum -a 256 "$@"; }
mock_ui_left() { for d in "$R"/proc/[0-9]*; do [ -f "$d/exe_path" ] && case "$(cat $d/exe_path)" in */demo_v9) return 0;; esac; done; return 1; }
readlink() {
  case "$1" in
    "$R"/proc/*/exe)
      d="${1%/exe}"
      if [ -f "$d/exe_path" ]; then p=$(cat "$d/exe_path")
        if [ -f "$p" ] && cmp -s "$p" "$d/image"; then echo "$p"; else echo "$p (deleted)"; fi; return 0; fi;;
  esac
  command readlink "$@"; }
kill() {
  sig=TERM; case "$1" in -*) sig="${1#-}"; shift;; esac
  for p in "$@"; do
    if [ -d "$R/proc/$p" ]; then
      echo "kill -$sig $p" >> "$R/events"
      case "$sig" in
        STOP) touch "$R/proc/$p/stopped";; CONT) rm -f "$R/proc/$p/stopped";;
        *) rm -rf "$R/proc/$p"
           if ! mock_ui_left; then ws=$(cat "$R/watchdog_slot" 2>/dev/null)
             [ "$ws" = B ] && [ -f "$R/new_customer/demo_v9" ] && mock_spawn "$R/new_customer/demo_v9" ./demo_v9
             [ "$ws" = A ] && mock_spawn "$R/customer/demo_v9" ./demo_v9
           fi;;
      esac
    else command kill -s "$sig" "$p" 2>/dev/null || command kill "$p" 2>/dev/null; fi
  done; return 0; }
nc() {  # only "nc -l -p PORT" is used by the installer
  python3 -c '
import socket,sys
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("127.0.0.1",int(sys.argv[1]))); s.listen(1)
c,_=s.accept()
while True:
    b=c.recv(65536)
    if not b: break
    sys.stdout.buffer.write(b)
sys.stdout.flush(); c.close()' "$3"; }
nohup() {
  case "$1" in
    ./demo_v9) mock_spawn "$PWD/demo_v9" ./demo_v9;;
    nc) shift; nc "$@";;
    */switcher) mock_spawn_daemon "$1";;
    *) command nohup "$@";;
  esac; }
'''


def make_root(root, scenario="stock_a"):
    """scenario: stock_a  (stock running from A, B holds a vendor copy, no flag)
                 vendor_b (vendor UI running from B after a vendor update, flag set)
                 empty_b  (stock A, slot B has no binary)"""
    root = pathlib.Path(root)
    if root.exists():
        shutil.rmtree(root)
    for d in ("customer/libs", "new_customer/libs", "tmp", "proc", "config/wifi", "dev"):
        (root / d).mkdir(parents=True)
    (root / "dev/ttyS1").write_bytes(b"")
    (root / "proc/sys/kernel/random").mkdir(parents=True)
    (root / "proc/sys/kernel/random/boot_id").write_text("5d1e1f0a-0000-4000-8000-00000000b001\n")
    (root / "customer/demo.sh").write_text(DEMO_SH)
    (root / "customer/demo_v9").write_bytes(b"\x7fELF MOCK VENDOR UI slot A\n" * 50)
    (root / "customer/test.sh").write_text("#!/bin/sh\n# synthetic watchdog stand-in\n")
    (root / "customer/config.json").write_text('{\n\t"brewTemper":\t870\n}\n')
    if scenario in ("stock_a", "vendor_b"):
        tag = b"slot A" if scenario == "stock_a" else b"slot B (newer vendor build)"
        (root / "new_customer/demo_v9").write_bytes(b"\x7fELF MOCK VENDOR UI " + tag + b"\n" * 50)
    if scenario == "vendor_b":
        (root / "customer/new").touch()
    (root / "pidcounter").write_text("700\n")
    (root / "events").write_text("")
    slot = "B" if scenario == "vendor_b" else "A"
    (root / "watchdog_slot").write_text(slot + "\n")
    # watchdog process (test.sh <slot>) and the running UI
    wd = root / "proc/650"
    wd.mkdir()
    os.symlink("/bin/sh", wd / "exe")
    arg = b"/customer/test.sh\0new_customer\0" if slot == "B" else b"./test.sh\0customer\0"
    (wd / "cmdline").write_bytes(b"/bin/sh\0" + arg)
    ui = root / ("new_customer/demo_v9" if slot == "B" else "customer/demo_v9")
    p = root / "proc/600"
    p.mkdir()
    shutil.copy(ui, p / "image")
    (p / "exe_path").write_text(str(ui))
    os.symlink(p / "image", p / "exe")
    (p / "cmdline").write_bytes(b"./demo_v9\0")
    return root


class MockServer:
    def __init__(self, root):
        self.root = str(pathlib.Path(root).resolve())
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.port = self.srv.getsockname()[1]
        self.funcs = pathlib.Path(self.root) / ".mockfuncs.sh"
        self.funcs.write_text(FUNCS)
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            threading.Thread(target=self._session, args=(c,), daemon=True).start()

    def _session(self, c):
        env = dict(os.environ, MOCK_ROOT=self.root)
        sh = subprocess.Popen(["bash", "--noprofile", "--norc"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=self.root)
        sh.stdin.write(f". '{self.funcs}'\n".encode())
        sh.stdin.flush()
        c.sendall(b"\r\nMock display (synthetic)\r\n# ")

        def pump_out():
            for chunk in iter(lambda: sh.stdout.read1(65536), b""):
                try:
                    c.sendall(chunk.replace(b"\n", b"\r\n"))
                except OSError:
                    break
        threading.Thread(target=pump_out, daemon=True).start()
        try:
            while True:
                b = c.recv(65536)
                if not b:
                    break
                sh.stdin.write(b.replace(b"\r\n", b"\n").replace(b"\r", b"\n"))
                sh.stdin.flush()
        except OSError:
            pass
        finally:
            try:
                sh.stdin.close()
            except OSError:
                pass
            sh.terminate()
            c.close()

    def close(self):
        self.srv.close()


if __name__ == "__main__":
    r = make_root(sys.argv[1] if len(sys.argv) > 1 else "/tmp/merakiplus-mock",
                  sys.argv[2] if len(sys.argv) > 2 else "stock_a")
    s = MockServer(r)
    print(f"mock display on 127.0.0.1:{s.port}  root={r}  (use --port {s.port} --test-prefix {r})")
    threading.Event().wait()
