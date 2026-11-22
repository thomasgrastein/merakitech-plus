#!/bin/sh
# launcher_watchdog.sh — the launcher (src/merakiplus_boot.c) under a model of the vendor watchdog.
#
#   sh tests/launcher_watchdog.sh <launcher binary> [work dir]
#
# POSIX sh only, so the same script runs on the CI host (host build) and in an ARMv7 container with the
# real static ARM build (tests/test_launcher.py --arm). Everything is synthetic: fake UIs are small shell
# scripts in rel/<name>/demo_v9, the "stock UI" is a stub at customer/demo_v9; MERAKIPLUS_BOOT_ROOT puts
# every path (/new_customer, /customer, /tmp, /proc) under the work dir.
#
# The vendor watchdog (/customer/test.sh new_customer) restarts /new_customer/demo_v9 ~1 s after no
# demo_v9 process is left. The model: start the launcher, wait until "its" process ends (a crash) or the
# UI reports that it runs (healthy / stock), then start again. Uptime advances 2 s per restart; a power
# cycle = new boot_id + empty /tmp.
set -u
L=$1
W=${2:-$(mktemp -d)}
mkdir -p "$W"; W=$(cd "$W" && pwd -P)
R=$W/root
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "  [PASS] $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  [FAIL] $1  -- $2"; }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "got [$2] want [$3]"; fi; }
sha()  { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

fresh() {   # empty display: slot A stub, no releases
  rm -rf "$R"; mkdir -p "$R/new_customer/merakiplus/rel" "$R/new_customer/merakiplus/ota" "$R/customer" "$R/tmp" "$R/proc/sys/kernel/random"
  echo "boot-0001" > "$R/proc/sys/kernel/random/boot_id"; echo "100.00 90.00" > "$R/proc/uptime"
  cat > "$R/customer/demo_v9" <<EOF
#!/bin/sh
echo "stock cwd=\$PWD ld=\$LD_LIBRARY_PATH rel=\${MERAKIPLUS_REL:-} trial=\${MERAKIPLUS_TRIAL:-}" >> "$R/runs"
exit 0
EOF
  chmod 755 "$R/customer/demo_v9"; : > "$R/runs"
}
# rel <name> <behaviour>: crash | healthy | hthencrash (healthy, then crashes) | hang | clean (exits on purpose)
rel() {
  d=$R/new_customer/merakiplus/rel/$1; mkdir -p "$d"
  cat > "$d/demo_v9" <<EOF
#!/bin/sh
# synthetic UI "$1" ($2)
echo "$1 trial=\${MERAKIPLUS_TRIAL:-0} relname=\${MERAKIPLUS_RELNAME:-} cwd=\$PWD fb=\${MERAKIPLUS_FALLBACK:-0} note=\${MERAKIPLUS_NOTE:-}" >> "$R/runs"
mark() { printf 'boot=%s\npid=%s\nrel=%s\n' "\$(cat $R/proc/sys/kernel/random/boot_id)" "\$\$" "\$MERAKIPLUS_RELNAME" > "$R/tmp/merakiplus_healthy"
         [ "\${MERAKIPLUS_TRIAL:-}" = 1 ] && printf 'boot=x\npid=%s\nrel=%s\n' "\$\$" "\$MERAKIPLUS_RELNAME" > "$R/new_customer/merakiplus/ota/healthy"; true; }
case "$2" in
  crash) exit 1;;
  healthy|clean) mark; exit 0;;
  hthencrash) mark; exit 1;;
  hang) exec sleep 30;;
esac
EOF
  chmod 755 "$d/demo_v9"
  printf 'merakiplus-release 1\nversion %s\nfile demo_v9 %s %s\n' "$1" "$(wc -c < "$d/demo_v9" | tr -d ' ')" "$(sha "$d/demo_v9")" > "$d/MANIFEST"
}
state() { printf 'format=1\ncurrent=%s\nprevious=%s\npending=%s\ntries=%s\nlast=\n' "$1" "$2" "$3" "${4:-0}" > "$R/new_customer/merakiplus/ota/state"; }
st()    { grep "^$1=" "$R/new_customer/merakiplus/ota/state" | cut -d= -f2-; }
powercycle() { n=$(cat "$R/proc/sys/kernel/random/boot_id"); echo "${n}x" > "$R/proc/sys/kernel/random/boot_id"; rm -rf "$R/tmp"; mkdir "$R/tmp"; }
tick() { u=$(cut -d. -f1 "$R/proc/uptime"); echo "$((u+2)).00 1.00" > "$R/proc/uptime"; }
# watchdog <max starts> [env...]: restart until a UI keeps running (healthy/clean/stock) or max starts
watchdog() {
  max=$1; shift; i=0
  while [ $i -lt "$max" ]; do
    i=$((i+1)); tick
    env MERAKIPLUS_BOOT_ROOT="$R" LD_LIBRARY_PATH=/lib:/new_customer/libs:/config/wifi "$@" "$L" > /dev/null 2>> "$R/launcher.err" &
    pid=$!; n=0
    while kill -0 $pid 2>/dev/null && [ $n -lt 20 ]; do sleep 0.1; n=$((n+1)); done
    if kill -0 $pid 2>/dev/null; then kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null; rc=137; else wait $pid; rc=$?; fi
    last=$(tail -n 1 "$R/runs" | cut -d' ' -f1)
    [ "$rc" = 0 ] && break                    # healthy UI / stock: it keeps running (the model stops here)
  done
}
seq_() { cut -d' ' -f1 "$R/runs" | tr '\n' ' ' | sed 's/ $//'; }

echo "== launcher watchdog model: $L"

echo "-- first install crashes before Wi-Fi (no previous release): 3 trial starts, then the stock UI"
fresh; rel 6.21-a crash; state "" "" 6.21-a 0
watchdog 10
check "3 trial starts of the new release, then stock" "$(seq_)" "6.21-a 6.21-a 6.21-a stock"
check "trial flag passed to the trial build" "$(head -n 1 "$R/runs" | cut -d' ' -f2)" "trial=1"
check "rollback recorded in the state file" "$(st last)" "rolled_back:6.21-a:crashloop"
check "pending cleared, tries reset" "$(st pending)/$(st tries)" "/0"
check "stock started like a slot-A boot: cwd /customer, slot-A libraries, no Meraki+ env" "$(grep '^stock' "$R/runs" | head -n 1)" \
      "stock cwd=$R/customer ld=/lib:/customer/libs:/config/wifi rel= trial="
check "the launcher never touched slot A or a boot flag" "$(ls "$R/customer" | tr '\n' ' ')" "demo_v9 "

echo "-- 3-strike chain: new -> previous -> stock, all crashing"
fresh; rel 6.22-n crash; rel 6.21-b crash; rel 6.20-a crash; state 6.21-b 6.20-a 6.22-n 0
watchdog 20
check "new x3 (trial), current x3, previous x3, then stock" "$(seq_)" "6.22-n 6.22-n 6.22-n 6.21-b 6.21-b 6.21-b 6.20-a 6.20-a 6.20-a stock"
check "only the trial is rolled back persistently; current/previous stay" "$(st current)/$(st previous)/$(st pending)" "6.21-b/6.20-a/"
check "the UI after the rollback is told which release failed (one-time notice)" "$(sed -n 4p "$R/runs" | cut -d' ' -f5-)" "fb=0 note=6.22-n"
check "previous after a crash storm: FALLBACK + the failed release" "$(sed -n 7p "$R/runs" | cut -d' ' -f5-)" "fb=1 note=6.21-b"
check "... the notice only once, FALLBACK on every start of this level" "$(sed -n 8p "$R/runs" | cut -d' ' -f5-)" "fb=1 note="
check "the release dir is the UI's cwd and its name is passed" "$(sed -n 1p "$R/runs" | cut -d' ' -f3-4)" "relname=6.22-n cwd=$R/new_customer/merakiplus/rel/6.22-n"
s0=$(cat "$R/new_customer/merakiplus/ota/state")
echo "-- power cycle after the chain: the per-boot fallback starts from the top again; no state write in steady state"
powercycle; : > "$R/runs"; watchdog 20
check "current x3, previous x3, stock (again, this boot)" "$(seq_)" "6.21-b 6.21-b 6.21-b 6.20-a 6.20-a 6.20-a stock"
check "state file unchanged by steady-state starts" "$(cat "$R/new_customer/merakiplus/ota/state")" "$s0"
check "no temp files left in ota/" "$(ls -a "$R/new_customer/merakiplus/ota" | grep -c tmp)" "0"
echo "-- the gesture (explicit start) tries Meraki+ again in the same boot"
: > "$R/runs"; watchdog 1 MERAKIPLUS_LAUNCH=gesture
check "gesture start: current again despite this boot's stock fallback" "$(seq_)" "6.21-b"

echo "-- trial succeeds: healthy marker -> commit at the next start"
fresh; rel 6.22-n healthy; rel 6.21-b healthy; state 6.21-b "" 6.22-n 0
watchdog 5
check "trial build started once and healthy" "$(seq_)" "6.22-n"
check "tries=1 persisted before the exec" "$(st tries)/$(st pending)" "1/6.22-n"
: > "$R/runs"; watchdog 1
check "next start commits it: current=new, previous=old" "$(st current)/$(st previous)/$(st pending)/$(st tries)/$(st last)" "6.22-n/6.21-b//0/ok:6.22-n"
check "... and runs it as a normal (non-trial) start" "$(head -n 1 "$R/runs" | cut -d' ' -f1-2)" "6.22-n trial=0"
s0=$(cat "$R/new_customer/merakiplus/ota/state"); powercycle; : > "$R/runs"; watchdog 1
check "steady-state start after a power cycle: no state write" "$(cat "$R/new_customer/merakiplus/ota/state")" "$s0"

echo "-- a trial that was healthy once is committed even after a power cut (marker on flash)"
fresh; rel 6.22-n healthy; rel 6.21-b healthy; state 6.21-b "" 6.22-n 0
watchdog 1; powercycle; watchdog 1; powercycle; watchdog 1
check "committed on the first start after the cut" "$(st current)/$(st pending)" "6.22-n/"

echo "-- healthy, then crashing: never 3 in a row, but the 7th start within 10 min is a crash storm"
fresh; rel 6.21-h hthencrash; rel 6.20-p healthy; state 6.21-h 6.20-p "" 0
watchdog 12
check "6 starts of the crashing build, then previous" "$(seq_)" "6.21-h 6.21-h 6.21-h 6.21-h 6.21-h 6.21-h 6.20-p"

echo "-- hang (never healthy, killed): 3 starts, then previous"
fresh; rel 6.21-g hang; rel 6.20-p healthy; state 6.21-g 6.20-p "" 0
watchdog 8
check "hung build x3, then previous" "$(seq_)" "6.21-g 6.21-g 6.21-g 6.20-p"

echo "-- damaged current (MANIFEST does not match the binary): previous at once"
fresh; rel 6.21-d healthy; rel 6.20-p healthy; state 6.21-d 6.20-p "" 0
printf 'x' >> "$R/new_customer/merakiplus/rel/6.21-d/demo_v9"
watchdog 3
check "previous runs, the damaged build is never executed" "$(seq_)" "6.20-p"
check "logged" "$(grep -c 'unusable' "$R/new_customer/merakiplus/ota/launch.log")" "1"

echo "-- damaged trial build: rolled back before it runs"
fresh; rel 6.22-n healthy; rel 6.21-b healthy; state 6.21-b "" 6.22-n 0
rm "$R/new_customer/merakiplus/rel/6.22-n/MANIFEST"
watchdog 3
check "current runs" "$(seq_)" "6.21-b"
check "rollback reason recorded" "$(st last)" "rolled_back:6.22-n:damaged"

echo "-- clean exit (SWITCH TO ORIGINAL / reload) does not count as a failed start"
fresh; rel 6.21-c clean; rel 6.20-p healthy; state 6.21-c 6.20-p "" 0
watchdog 1; watchdog 1; watchdog 1; watchdog 1
check "4 deliberate exits, still the current build" "$(seq_)" "6.21-c 6.21-c 6.21-c 6.21-c"

echo "-- no state file: the newest complete release; none at all: stock"
fresh; rel 6.9-x healthy; rel 6.21-y healthy; rel 6.20-z healthy
watchdog 1
check "6.21 chosen over 6.20 and 6.9 (numeric order)" "$(seq_)" "6.21-y"
fresh; watchdog 1
check "empty display: stock" "$(seq_)" "stock"

echo "-- a stage directory (.stage-*) or a bad name is never started"
fresh; rel 6.21-b crash; mv "$R/new_customer/merakiplus/rel/6.21-b" "$R/new_customer/merakiplus/rel/.stage-6.21-b"; state "" "" .stage-6.21-b 0
watchdog 3
check "rolled back as damaged, stock" "$(seq_)/$(st last)" "stock/rolled_back:.stage-6.21-b:damaged"

echo "LAUNCHER WATCHDOG MODEL: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
