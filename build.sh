#!/bin/sh
# build.sh — builds every Meraki+ binary from source with zig (no other toolchain needed).
#
#   ./build.sh            device + tools + host sim + self-tests  ->  build/
#   ./build.sh host       only what CI needs (host sim, switcher_host, self-tests)
#
# Device binaries are static ARMv7 (arm-linux-musleabihf, Cortex-A7) and stripped (-s): the
# unstripped debug info would otherwise embed the absolute build path of whoever built them.
set -eu
cd "$(dirname "$0")"
MODE=${1:-all}
command -v zig >/dev/null 2>&1 || { echo "build.sh: zig not found (https://ziglang.org/download/ — tested with 0.17.1)" >&2; exit 1; }
# bin/ is byte-identical to CI only with the OFFICIAL zig release: distribution builds (e.g. Homebrew's) put their own
# name into the ELF .comment ("Homebrew clang/LLD"), and a shared zig cache can even hand their objects to the official zig.
case "$(zig cc --version 2>/dev/null | head -n 1)" in
  *Homebrew*) echo "build.sh: WARNING: this is Homebrew's zig — fine for development, but builds for bin/ must use the official" >&2
              echo "          zig 0.17.1 tarball with its own ZIG_GLOBAL_CACHE_DIR, or tests/check_bins.py fails in CI (bin/README.md)" >&2 ;;
esac

mkdir -p build
ARM="-target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -s"
# -fno-unwind-tables: otherwise zig leaves an UND __aeabi_unwind_cpp_pr1 in the .so
# (exidx personality) which glibc's ld.so cannot resolve -> LD_PRELOAD failure in stock.
ARMSO="-target arm-linux-musleabihf -mcpu=cortex_a7 -shared -fPIC -nostdlib -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions -O2 -s"
WERR="-Wall -Wextra -Werror"
# the updater's TLS library (third_party/bearssl, MIT, unmodified; third_party/README.md). A fixed, locale-independent
# file order keeps the link — and so the binary — reproducible.
BEARSSL="-Ithird_party/bearssl/inc -Ithird_party/bearssl/src"
BEARSSL_SRC=$(find third_party/bearssl/src -name '*.c' | LC_ALL=C sort)
UPD_SRC="src/merakiplus_upd.c src/merakiplus_upd_net.c"
say(){ printf '== %s\n' "$*"; }

say "host: merakiplus_sim (SIM build, used by sim/run_e2e.py)"
zig cc -DMERAKIPLUS_SIM -O2 -o build/merakiplus_sim src/merakiplus.c -lm
say "host: switcher_selftest + switcher_host + grab_shim_selftest"
zig cc -DSWITCHER_SELFTEST -O2 $WERR -o build/switcher_selftest boot/switcher.c
zig cc -O2 $WERR -o build/switcher_host boot/switcher.c
zig cc -DGRAB_SHIM_SELFTEST -DSHIM_HOST -O2 $WERR -o build/grab_shim_selftest boot/grab_shim.c
say "host: merakiplus_boot_host (the launcher, for tests/test_launcher.py and the installer's mock display)"
zig cc -O2 $WERR -o build/merakiplus_boot_host src/merakiplus_boot.c
say "host: merakiplus_snd_selftest (sound helper against a mock MI_AO) + audio/ check"
zig cc -DSND_SELFTEST -O2 $WERR -o build/merakiplus_snd_selftest src/merakiplus_snd.c
say "host: merakiplus_upd_host (the updater: signed manifests + HTTPS fetch, docs/signing.md; Ed25519/SHA-256 = Zig std, TLS = BearSSL)"
zig build-obj src/merakiplus_crypto.zig -O ReleaseSmall -fsingle-threaded -femit-bin=build/merakiplus_crypto_host.o
zig cc -O2 $WERR $BEARSSL -c src/merakiplus_upd.c -o build/merakiplus_upd_host.o
zig cc -O2 $WERR $BEARSSL -c src/merakiplus_upd_net.c -o build/merakiplus_upd_net_host.o
zig cc -O2 $BEARSSL -o build/merakiplus_upd_host build/merakiplus_upd_host.o build/merakiplus_upd_net_host.o build/merakiplus_crypto_host.o $BEARSSL_SRC
say "host: merakiplus_upd_test (TEST build for tests/test_ota_fetch.py + sim/: also trusts a test CA/key from the environment; never shipped)"
zig cc -O2 $WERR $BEARSSL -DMERAKIPLUS_UPD_TEST -c src/merakiplus_upd.c -o build/merakiplus_upd_test.o
zig cc -O2 $WERR $BEARSSL -DMERAKIPLUS_UPD_TEST -c src/merakiplus_upd_net.c -o build/merakiplus_upd_net_test.o
zig cc -O2 $BEARSSL -o build/merakiplus_upd_test build/merakiplus_upd_test.o build/merakiplus_upd_net_test.o build/merakiplus_crypto_host.o $BEARSSL_SRC

if [ "$MODE" != host ]; then
  say "device (ARMv7 static): merakiplus, merakiplus_sim_arm"
  zig cc $ARM -o build/merakiplus src/merakiplus.c
  zig cc $ARM -DMERAKIPLUS_SIM -o build/merakiplus_sim_arm src/merakiplus.c
  say "device (ARMv7 static): merakiplus_boot (the launcher, installed as /new_customer/demo_v9)"
  zig cc $ARM $WERR -o build/merakiplus_boot src/merakiplus_boot.c
  say "device (ARMv7 static): merakiplus_upd (the updater, shipped in every release directory — docs/ota-plan.md phase 3)"
  zig build-obj src/merakiplus_crypto.zig -target arm-linux-musleabihf -mcpu cortex_a7 -O ReleaseSmall -fstrip -fsingle-threaded \
    -femit-bin=build/merakiplus_crypto_arm.o
  zig cc $ARM $WERR $BEARSSL -c src/merakiplus_upd.c -o build/merakiplus_upd_arm.o
  zig cc $ARM $WERR $BEARSSL -c src/merakiplus_upd_net.c -o build/merakiplus_upd_net_arm.o
  zig cc $ARM $BEARSSL -o build/merakiplus_upd build/merakiplus_upd_arm.o build/merakiplus_upd_net_arm.o build/merakiplus_crypto_arm.o $BEARSSL_SRC
  say "device (ARMv7 static): switcher, tinj_hold, keyinj, tinj, fbpage, evprobe"
  zig cc $ARM $WERR -o build/switcher boot/switcher.c
  say "device (ARMv7, glibc 2.28 like the display, dynamic): merakiplus_snd (dlopens the display's libmi_sys.so + libmi_ao.so)"
  zig cc -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 -O2 -s -fno-PIE -no-pie $WERR -o build/merakiplus_snd src/merakiplus_snd.c -lpthread -ldl
  zig cc $ARM $WERR -o build/tinj_hold boot/tinj_hold.c
  zig cc $ARM -o build/keyinj src/keyinj.c
  zig cc $ARM -o build/tinj src/tinj.c
  zig cc $ARM -o build/fbpage src/fbpage.c
  zig cc $ARM -o build/evprobe boot/evprobe.c
  say "device: grab_shim.so (freestanding LD_PRELOAD shim, no DT_NEEDED)"
  zig cc $ARMSO $WERR -o build/grab_shim.so boot/grab_shim.c
  python3 - build/grab_shim.so <<'PY'
# grab_shim.so must be self-contained: no DT_NEEDED and no undefined symbols,
# otherwise glibc's ld.so refuses the LD_PRELOAD in the stock process.
import struct, sys
d = open(sys.argv[1], 'rb').read()
e_shoff, = struct.unpack_from('<I', d, 32)
es, en, ex = struct.unpack_from('<HHH', d, 46)
secs = [struct.unpack_from('<10I', d, e_shoff + i * es) for i in range(en)]
st = secs[ex]; strtab = d[st[4]:st[4] + st[5]]
nm = lambda n: strtab[n:strtab.index(b'\0', n)].decode()
by = {nm(s[0]): s for s in secs}
sy = by['.dynsym']
sym = lambda i: struct.unpack_from('<IIIBBH', d, sy[4] + i * sy[9])
und = [i for i in range(sy[5] // sy[9]) if sym(i)[5] == 0 and sym(i)[0]]
need = []; dy = by['.dynamic']
for i in range(dy[5] // dy[9]):
    tag, val = struct.unpack_from('<II', d, dy[4] + i * 8)
    if tag == 1: need.append(val)
    if tag == 0: break
assert not und and not need, f"grab_shim.so has UND/NEEDED: {und} {need}"
print("   grab_shim.so: no DT_NEEDED, no undefined symbols")
# Deterministic trim (bin/README.md): keep only what the loader maps — the file up to the end of the last program
# header's file range — and clear the section-header fields (e_shoff, e_shentsize, e_shnum, e_shstrndx). The result
# is the shipped bin/grab_shim.so byte for byte, so tests/check_bins.py drift-checks it like every other binary.
phoff, = struct.unpack_from('<I', d, 28)
phes, phn = struct.unpack_from('<HH', d, 42)
end = max(struct.unpack_from('<8I', d, phoff + i * phes)[1] + struct.unpack_from('<8I', d, phoff + i * phes)[4]
          for i in range(phn))
assert phoff + phes * phn <= end <= e_shoff, "grab_shim.so: unexpected layout (program headers / sections)"
t = bytearray(d[:end])
struct.pack_into('<I', t, 32, 0)
struct.pack_into('<HHH', t, 46, 0, 0, 0)
open(sys.argv[1], 'wb').write(bytes(t))
print(f"   grab_shim.so: trimmed to the loaded segments ({len(d)} -> {len(t)} bytes)")
PY
fi

say "self-tests (switcher gesture/mode logic, grab shim, sound helper, launcher, manifest verifier)"
rm -rf build/boot_selftest
./build/merakiplus_boot_host --selftest build/boot_selftest > build/merakiplus_boot_selftest.log 2>&1 || { cat build/merakiplus_boot_selftest.log; echo "build.sh: launcher selftest FAILED" >&2; exit 1; }
printf '   merakiplus_boot_host: %s\n' "$(tail -n 1 build/merakiplus_boot_selftest.log)"
./build/merakiplus_upd_host --selftest > build/merakiplus_upd_selftest.log 2>&1 || { cat build/merakiplus_upd_selftest.log; echo "build.sh: updater selftest FAILED" >&2; exit 1; }
printf '   merakiplus_upd_host: %s\n' "$(tail -n 1 build/merakiplus_upd_selftest.log)"
python3 tools/gen_anchors.py --check > build/anchors_check.log 2>&1 || { cat build/anchors_check.log; echo "build.sh: src/merakiplus_upd_anchors.h differs from keys/tls/" >&2; exit 1; }
printf '   anchors: %s\n' "$(tail -n 1 build/anchors_check.log)"
for t in switcher_selftest grab_shim_selftest merakiplus_snd_selftest; do
  a=; [ "$t" = merakiplus_snd_selftest ] && a=audio
  ./build/$t $a > build/$t.log 2>&1 || { cat build/$t.log; echo "build.sh: $t FAILED" >&2; exit 1; }
  printf '   %s: %s\n' "$t" "$(tail -n 1 build/$t.log)"
done
python3 tools/gen_audio.py --check > build/audio_check.log 2>&1 || { cat build/audio_check.log; echo "build.sh: audio/ differs from tools/gen_audio.py" >&2; exit 1; }
printf '   audio: %s\n' "$(tail -n 1 build/audio_check.log)"

say "done -> build/"
