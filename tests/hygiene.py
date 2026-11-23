#!/usr/bin/env python3
"""hygiene.py — repository checks that must hold for a public repo (run in CI):

  * bin/MD5SUMS matches the files in bin/, and tools/merakiplus.py BIN_MD5 matches bin/MD5SUMS;
    BIN_SHA256 / AUDIO_SHA256 cover the same files as BIN_MD5 / AUDIO_MD5 and match them
  * no vendor firmware/binaries: the only ELF/Mach-O files tracked are our builds in bin/
  * no personal paths, private IP addresses (other than documented placeholders), MAC addresses
  * sim/fixtures/config.json carries placeholder Wi-Fi credentials only
  * audio/ holds exactly our own generated sounds (tools/merakiplus.py AUDIO_MD5; tools/gen_audio.py --check runs in
    ./build.sh) and no audio/video file of any other origin is tracked (no vendor .mp3/.mp4 — docs/audio.md)
  * the pre-rename identifier (tools/merakiplus.py LEGACY_ID) appears nowhere — not in file names,
    text or binaries — except inside the "legacy-compat" block of tools/merakiplus.py
  * bin/merakiplus_snd NEEDs libpthread.so.0 and its source dlopen()s libpthread.so.0 RTLD_GLOBAL before libmi_common.so
    (v6.18.1: the display's libmi_common.so uses sem_post & co. without NEEDing libpthread — docs/audio.md §3)
  * no quoted ARM instructions (disassembly listings or fragments) in Markdown/text docs: stock firmware behaviour is
    described in prose or pseudocode with flash/RAM addresses only
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import merakiplus  # noqa: E402

fails = []


def fail(msg):
    fails.append(msg)
    print("  [FAIL] " + msg)


def tracked():
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
        files = [ROOT / f for f in out.decode().split("\0") if f]
        if files:
            return files
    except Exception:
        pass
    skip = {".git", "build", "logs", "__pycache__", "reports"}
    return [p for p in ROOT.rglob("*") if p.is_file() and not (set(p.relative_to(ROOT).parts) & skip)]


files = tracked()

# 1. checksums
sums = {}
for line in (ROOT / "bin/MD5SUMS").read_text().splitlines():
    h, name = line.split(None, 1)
    sums[name.strip()] = h
for name, h in sums.items():
    p = ROOT / "bin" / name
    if not p.is_file():
        fail(f"bin/MD5SUMS lists missing file {name}")
    elif hashlib.md5(p.read_bytes()).hexdigest() != h:
        fail(f"bin/{name} does not match bin/MD5SUMS")
for p in (ROOT / "bin").iterdir():
    if p.name not in sums and p.name not in ("MD5SUMS", "README.md"):
        fail(f"bin/{p.name} is not listed in bin/MD5SUMS")
for name, h in merakiplus.BIN_MD5.items():
    if sums.get(name) != h:
        fail(f"tools/merakiplus.py BIN_MD5[{name}] != bin/MD5SUMS")
# OTA phase 1a: the installer pins SHA-256 for exactly the files it pins md5 for, and both match the files
for tab, ref, sub in ((merakiplus.BIN_SHA256, merakiplus.BIN_MD5, "bin"), (merakiplus.AUDIO_SHA256, merakiplus.AUDIO_MD5, "audio")):
    if set(tab) != set(ref):
        fail(f"tools/merakiplus.py SHA-256 table for {sub}/ lists {sorted(tab)}, md5 table {sorted(ref)}")
    for name, h in tab.items():
        p = ROOT / sub / name
        got = hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None
        if got != h:
            fail(f"tools/merakiplus.py SHA-256 of {sub}/{name} is {h}, the file has {got}")

# 1b. v6.18: audio/ = exactly AUDIO_MD5 (our own sounds); no other audio/video file anywhere in the repository
adir = ROOT / "audio"
have = {p.name for p in adir.iterdir()} if adir.is_dir() else set()
if have != set(merakiplus.AUDIO_MD5):
    fail(f"audio/ holds {sorted(have)}, expected exactly {sorted(merakiplus.AUDIO_MD5)}")
for name, h in merakiplus.AUDIO_MD5.items():
    p = adir / name
    if p.is_file() and hashlib.md5(p.read_bytes()).hexdigest() != h:
        fail(f"audio/{name} does not match AUDIO_MD5 in tools/merakiplus.py")
MEDIA = re.compile(r"\.(mp3|mp4|aac|m4a|ogg|flac|wav)$", re.I)
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    if MEDIA.search(rel) and not (rel.startswith("audio/") and p.name in merakiplus.AUDIO_MD5):
        fail(f"media file outside our own audio set (vendor audio must never be committed): {rel}")

# 2. binaries only in bin/, no vendor file names anywhere
VENDOR = re.compile(r"(^|/)(demo_v9[^/]*|MEM03_[^/]*\.bin|config\.bin|upgrade\.sh|test\.sh|demo\.sh|[^/]*\.dis)$")
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    head = p.read_bytes()[:4] if p.is_file() else b""
    if head in (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe") and not rel.startswith("bin/"):
        fail(f"binary outside bin/: {rel}")
    if VENDOR.search(rel):
        fail(f"vendor-looking file name: {rel}")

# 3. personal data in text files
ALLOWED_IPS = {"192.168.1.50", "192.168.1.100", "192.168.0.0", "10.0.0.0"}
PRIV = re.compile(r"\b(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")
MAC = re.compile(r"\b(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\b")
HOME = re.compile(r"/(?:Users|home)/[A-Za-z][\w.-]+/")
# a config-style password / ssid value that is not one of the documented placeholders
CRED = re.compile(r"""["'`](password|ssid)["'`]?\s*[:=]\s*\\?["'`]([^"'`\\<]{3,})""")
CRED_OK = {"ExampleAP", "notarealpw", "TestNet"}
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    if rel.startswith("bin/") or p.suffix in (".png", ".gz"):
        continue
    try:
        t = p.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    for m in PRIV.finditer(t):
        if m.group(0) not in ALLOWED_IPS:
            fail(f"{rel}: private IP address {m.group(0)}")
    for m in MAC.finditer(t):
        fail(f"{rel}: MAC-like address {m.group(0)}")
    for m in CRED.finditer(t):
        if m.group(2).strip() not in CRED_OK and not m.group(2).startswith(("%s", "IKKE")):
            fail(f"{rel}: possible credential {m.group(1)}=…")
    for m in HOME.finditer(t):
        if rel != "tests/hygiene.py":
            fail(f"{rel}: personal path {m.group(0)}")
for p in (ROOT / "bin").iterdir():
    if HOME.search(p.read_bytes().decode("latin-1")):
        fail(f"bin/{p.name} embeds a home-directory path")
    elf = p.read_bytes()
    if elf[:4] == b"\x7fELF" and (b"Homebrew clang" in elf or b"Homebrew LLD" in elf):
        fail(f"bin/{p.name} was built with Homebrew's zig (its .comment differs from the official toolchain "
             "CI uses) — rebuild with the official zig 0.17.1 tarball (bin/README.md)")

# 4. fixture credentials are placeholders
cfg = json.loads((ROOT / "sim/fixtures/config.json").read_text())
if (cfg.get("ssid"), cfg.get("password")) != ("ExampleAP", "notarealpw"):
    fail("sim/fixtures/config.json must keep the placeholder Wi-Fi credentials")

# 5. the pre-rename identifier lives only in the legacy-compat block of tools/merakiplus.py
OLD = merakiplus.LEGACY_ID.encode()
BEGIN, END = b"# BEGIN legacy-compat", b"# END legacy-compat"
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    if OLD in rel.lower().encode():
        fail(f"file name contains the pre-rename identifier: {rel}")
    if not p.is_file():
        continue
    data = p.read_bytes()
    if rel == "tools/merakiplus.py":
        if data.count(BEGIN) != 1 or data.count(END) != 1 or data.index(BEGIN) > data.index(END):
            fail("tools/merakiplus.py: exactly one legacy-compat BEGIN/END block expected")
            continue
        data = data[:data.index(BEGIN)] + data[data.index(END):]
    n = data.lower().count(OLD)
    if n:
        fail(f"{rel}: {n} occurrence(s) of the pre-rename identifier outside the legacy-compat block")

# 6. sound helper: libpthread must be in the global scope before libmi_common.so is loaded (v6.18.1, docs/audio.md §3)
def elf32_needed(d):
    import struct
    shoff, = struct.unpack_from("<I", d, 32)
    es, en = struct.unpack_from("<HH", d, 46)
    secs = [struct.unpack_from("<10I", d, shoff + i * es) for i in range(en)]
    dyn = next(s for s in secs if s[1] == 6)                       # SHT_DYNAMIC
    strt = secs[dyn[6]]
    out = []
    for i in range(dyn[5] // 8):
        tag, val = struct.unpack_from("<II", d, dyn[4] + i * 8)
        if tag == 0:
            break
        if tag == 1:                                                   # DT_NEEDED
            o = strt[4] + val
            out.append(d[o:d.index(b"\0", o)].decode())
    return out
need = elf32_needed((ROOT / "bin/merakiplus_snd").read_bytes())
if "libpthread.so.0" not in need:
    fail(f"bin/merakiplus_snd: DT_NEEDED {need} lacks libpthread.so.0 (libmi_common.so needs sem_post & co.)")
src = (ROOT / "src/merakiplus_snd.c").read_text(encoding="utf-8")
ip, ic = src.find('dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL)'), src.find('dlopen("libmi_common.so"')
if ip < 0 or ic < 0 or ip > ic or src.count("pthread_global();") < 2:
    fail("src/merakiplus_snd.c: libpthread.so.0 must be dlopen()ed RTLD_GLOBAL before libmi_common.so (mi_load + mp3_play)")

# 7. no quoted vendor disassembly in docs: an ARM mnemonic followed by a register / immediate / memory operand
ASM = re.compile(r"(?<![\w.-])(?:ldr|str)(?:b|h|sb|sh|d)?(?:\.w)?\s+(?:r\d|\[)"
                 r"|(?<![\w.-])(?:orrs?|eors?|ands|bics?|cmp|cmn|tst|teq|movs?|movw|movt|mvns?|adds?|subs?|rsbs?"
                 r"|lsls?|lsrs?|asrs?|mul|push|pop|ldm\w*|stm\w*)(?:\.w)?\s+(?:r\d|sp|lr|pc|ip|sb|#|\{)"
                 r"|(?<![\w.-])(?:bl|blx|bx|b(?:eq|ne|hs|lo|hi|ls|ge|lt|gt|le|cs|cc|mi|pl))\s+(?:0x)?[0-9a-f]{3,}\b"
                 r"|\[(?:r\d{1,2}|sb|fp|ip|sp|pc)\s*[,+]", re.I)
for p in files:
    rel = p.relative_to(ROOT).as_posix()
    if p.suffix not in (".md", ".txt"):
        continue
    for i, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
        m = ASM.search(line)
        if m:
            fail(f"{rel}:{i}: looks like quoted ARM disassembly ({m.group(0)!r}) — describe it in prose/pseudocode")

# 8. signing keys (docs/signing.md): the same public keys in keys/*.pub, tools/merakiplus.py TRUSTED_KEYS and
#    src/merakiplus_upd_keys.h; never a private key anywhere in the tree
sys.path.insert(0, str(ROOT / "tools"))
import merakiplus as _mp  # noqa: E402
_pubs = {}
for p in sorted((ROOT / "keys").glob("*.pub")):
    try:
        kid, pk = _mp.signify_blob(p.read_text(encoding="ascii"), 42)
        if hashlib.sha256(pk).digest()[:8].hex() != kid:
            fail(f"{p.name}: key id is not SHA-256(public key)[:8]")
        _pubs[kid] = pk.hex()
    except Exception as e:  # noqa: BLE001
        fail(f"keys/{p.name}: not a signify Ed25519 public key ({e})")
_hdr = dict(re.findall(r'\{"([0-9a-f]{16})", "([0-9a-f]{64})"\}', (ROOT / "src/merakiplus_upd_keys.h").read_text()))
if not _pubs or _pubs != _mp.TRUSTED_KEYS or _hdr != _mp.TRUSTED_KEYS:
    fail(f"signing keys differ: keys/ {sorted(_pubs)}, TRUSTED_KEYS {sorted(_mp.TRUSTED_KEYS)}, "
         f"src/merakiplus_upd_keys.h {sorted(_hdr)}")
for p in files:
    if p.suffix not in (".png", ".gz", ".wav") and not p.relative_to(ROOT).as_posix().startswith("bin/") and \
            re.search(rb"-----BEGIN[ ][A-Z ]*PRIVATE[ ]KEY-----|untrusted[ ]comment:[^\n]*secret[ ]key", p.read_bytes()):
        fail(f"{p.relative_to(ROOT)}: contains a private key")

print("HYGIENE: " + ("OK" if not fails else f"{len(fails)} problem(s)"))
sys.exit(1 if fails else 0)
