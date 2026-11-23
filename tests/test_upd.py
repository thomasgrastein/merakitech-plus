#!/usr/bin/env python3
"""Signed release manifests (docs/signing.md, OTA phase 2): both verifiers — the installer's pure-Python one
(tools/merakiplus.py) and the device verifier (src/merakiplus_upd.c, build/merakiplus_upd_host) — plus the
maintainer tool (tools/sign_release.py) and the release workflow's manifest step.

    python3 tests/test_upd.py          host (needs ./build.sh host, zig, openssl >= 3)
    python3 tests/test_upd.py --arm    also the static ARM build in the arm32v7 container (local; docker)

Covered: RFC 8032 vectors, sign/verify round trip with a throwaway TEST key, tampered manifest / signature /
asset, wrong key, forged key id, downgrade (seq < max_seq), missing signature, strict JSON + canonical form
(duplicate keys, floats, escapes, whitespace, key order, size limit), Python/C parity on every case, release-dir
check, the release.yml manifest command (dry run), the real signed manifest fixture (tests/fixtures/), and that the
production builds trust only the pinned keys.
"""
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import sigtest as st        # noqa: E402
from sigtest import mp, sr  # noqa: E402

FAILS = []
N = [0]
PROD = ROOT / "build" / "merakiplus_upd_host"
EXIT = {"signature": 3, "format": 4, "older": 5, "asset": 6}


def check(name, cond, detail=""):
    N[0] += 1
    print(("  [PASS] " if cond else "  [FAIL] ") + name + ("" if cond else f"  -- {detail}"))
    if not cond:
        FAILS.append(name)


def zig_obj(target_args, out):
    subprocess.run(["zig", "build-obj", str(ROOT / "src/merakiplus_crypto.zig"), "-O", "ReleaseSmall", "-fsingle-threaded",
                    *target_args, f"-femit-bin={out}"], check=True)


def build_test_upd(key, work, arm=False):
    """A verifier that ALSO trusts the throwaway test key (compile-time -D; never made by build.sh)."""
    sfx = "arm" if arm else "host"
    obj = work / f"crypto_{sfx}.o"
    zig_obj(["-target", "arm-linux-musleabihf", "-mcpu", "cortex_a7", "-fstrip"] if arm else [], obj)
    out = work / f"upd_test_{sfx}"
    tgt = ["-target", "arm-linux-musleabihf", "-mcpu=cortex_a7", "-static", "-s"] if arm else []
    subprocess.run(["zig", "cc", *tgt, "-O2", "-Wall", "-Wextra", "-Werror",
                    f'-DMERAKIPLUS_UPD_TEST_KEYID="{key.id}"', f'-DMERAKIPLUS_UPD_TEST_PK="{key.pk.hex()}"',
                    "-o", str(out), str(ROOT / "src/merakiplus_upd.c"), str(obj)], check=True)
    return out


def py_verify(man, sig, keys, max_seq=None, assets_dir=None):
    """Python verifier -> (kind or 'ok', seq)."""
    try:
        m = mp.verify_manifest(pathlib.Path(man).read_bytes(), pathlib.Path(sig).read_text(errors="replace"), keys,
                               max_seq)
        if assets_dir:
            mp.verify_assets(m, {e["name"]: (pathlib.Path(assets_dir) / e["name"]).read_bytes() for e in m["assets"]})
        return "ok", m["seq"]
    except mp.SigError as e:
        return e.kind, None
    except FileNotFoundError:
        return "signature" if not pathlib.Path(sig).exists() else "asset", None


def main():
    arm = "--arm" in sys.argv
    if not PROD.is_file():
        sys.exit("build/merakiplus_upd_host missing — run ./build.sh host first")
    work = pathlib.Path(tempfile.mkdtemp(prefix="merakiplus-upd-")).resolve()
    try:
        run(work, arm)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"UPD TESTS: {N[0] - len(FAILS)}/{N[0]} passed" + (f"; FAILED: {FAILS}" if FAILS else ""))
    sys.exit(1 if FAILS else 0)


def run(work, arm):
    print("== Ed25519 (RFC 8032 §7.1 vectors) in the Python verifier ==")
    vec = [("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
           ("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
           ("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
            "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a")]
    for i, (pk, msg, sig) in enumerate(vec, 1):
        pk, msg, sig = bytes.fromhex(pk), bytes.fromhex(msg), bytes.fromhex(sig)
        check(f"vector {i} verifies", mp.ed25519_verify(pk, msg, sig))
        check(f"vector {i}: other message rejected", not mp.ed25519_verify(pk, msg + b"\0", sig))
        check(f"vector {i}: flipped signature bit rejected", not mp.ed25519_verify(pk, msg, sig[:40] + bytes([sig[40] ^ 1]) + sig[41:]))
        s_plus_l = (int.from_bytes(sig[32:], "little") + mp._L).to_bytes(32, "little")
        check(f"vector {i}: non-canonical S (S + L) rejected", not mp.ed25519_verify(pk, msg, sig[:32] + s_plus_l))

    print("== device verifier: production build ==")
    r = subprocess.run([str(PROD), "--selftest"], capture_output=True, text=True)
    check("merakiplus_upd_host --selftest", r.returncode == 0, r.stdout[-300:])
    r = subprocess.run([str(PROD), "keys"], capture_output=True, text=True)
    built_in = dict(l.split() for l in r.stdout.splitlines())
    check("production build trusts exactly the pinned keys (TRUSTED_KEYS)", built_in == mp.TRUSTED_KEYS and built_in,
          f"{built_in} vs {mp.TRUSTED_KEYS}")
    pub = (ROOT / "keys" / "merakiplus-release.pub").read_text()
    kid, pk = mp.signify_blob(pub, 42)
    check("keys/merakiplus-release.pub is a pinned key with a matching key id",
          mp.TRUSTED_KEYS.get(kid) == pk.hex() and sr.key_id(pk).hex() == kid)

    key, other = st.TestKey(work, "test"), st.TestKey(work, "other")
    upd = build_test_upd(key, work)
    rel = work / "rel"
    data, assets = st.signed_release(rel, key, seq=5)
    man, sig = rel / "manifest.json", rel / "manifest.sig"

    print("== manifest: canonical form + format ==")
    again = sr.build_manifest(rel, mp.VERSION, "0" * 40, "2026-11-23", list(reversed(assets)), 5)
    check("manifest bytes are stable (same input, any asset order)", again == data)
    m = json.loads(data)
    check("manifest == canonical re-serialization", mp.manifest_bytes(m) == data)
    check("every asset listed with size + sha256", {e["name"] for e in m["assets"]} == set(assets)
          and all(re.fullmatch(r"[0-9a-f]{64}", e["sha256"]) for e in m["assets"]))
    check("release section = UI, sound helper, WAVs (like the installer's release directory)",
          [e["name"] for e in m["release"]] == ["demo_v9", "merakiplus_snd"] + ["audio/" + n for n in st.AUDIO_ASSETS])
    check("platform section = launcher, switcher, shim", sorted(e["src"] for e in m["platform"]) ==
          ["grab_shim.so", "merakiplus_boot", "switcher"])
    check("base_url pins the tag", m["base_url"].endswith(f"/releases/download/v{mp.VERSION}/"))

    cases = []   # (name, manifest bytes, sig text or None, extra C args, expected kind or 'ok')

    def case(name, mdata, sigt, expect, args=(), assets_dir=None):
        cases.append((name, mdata, sigt, list(args), expect, assets_dir))

    case("valid signature", data, key.sign(data), "ok")
    case("valid + every asset hashed", data, key.sign(data), "ok", ["--assets", str(rel)], rel)
    tampered = data.replace(m["assets"][0]["sha256"].encode(), b"0" * 64, 1)
    case("tampered manifest (asset hash replaced)", tampered, key.sign(data), "signature")
    case("tampered manifest (seq raised)", data.replace(b'"seq": 5', b'"seq": 9'), key.sign(data), "signature")
    bad_sig = key.sign(data).splitlines()
    raw = bytearray(__import__("base64").b64decode(bad_sig[1]))
    raw[40] ^= 1
    case("tampered signature", data, bad_sig[0] + "\n" + __import__("base64").b64encode(bytes(raw)).decode() + "\n",
         "signature")
    case("wrong key (unknown key id)", data, other.sign(data), "signature")
    forged = other.sign(data).splitlines()
    fraw = bytearray(__import__("base64").b64decode(forged[1]))
    fraw[2:10] = key.keynum                          # claims the trusted key id, signed by another key
    case("wrong key (forged trusted key id)", data,
         forged[0] + "\n" + __import__("base64").b64encode(bytes(fraw)).decode() + "\n", "signature")
    case("missing signature", data, None, "signature")
    case("empty signature file", data, "", "signature")
    case("signature without comment line", data, key.sign(data).splitlines()[1] + "\n", "signature")
    case("downgrade: seq 5 < max_seq 6", data, key.sign(data), "older", ["--max-seq", "6"])
    case("same seq (re-install) accepted", data, key.sign(data), "ok", ["--max-seq", "5"])
    case("newer than max_seq accepted", data, key.sign(data), "ok", ["--max-seq", "4"])

    def signed_variant(name, mdata, expect):
        case(name, mdata, key.sign(mdata), expect)
    txt = data.decode()
    signed_variant("duplicate key (signed)", txt.replace('"format": 1,', '"format": 1,\n "format": 1,').encode(), "format")
    signed_variant("float (signed)", txt.replace('"format": 1', '"format": 1.0').encode(), "format")
    signed_variant("escape in a string (signed)", txt.replace('"channel": "stable"', '"channel": "st\\u0061ble"').encode(), "format")
    signed_variant("non-canonical whitespace (signed)", txt.replace('"format": 1', '"format":  1').encode(), "format")
    signed_variant("compact JSON (signed)", json.dumps(m, sort_keys=True).encode(), "format")
    unsorted = "{" + txt[1:].replace('\n "assets"', '\n "zz": 1,\n "assets"', 1)
    signed_variant("unsorted keys (signed)", unsorted.encode(), "format")
    signed_variant("no trailing newline (signed)", data[:-1], "format")
    signed_variant("format 2 (signed)", mp.manifest_bytes(dict(m, format=2)), "format")
    signed_variant("seq 0 (signed)", mp.manifest_bytes(dict(m, seq=0)), "format")
    signed_variant("seq missing (unsigned manifest, signed)", mp.manifest_bytes({k: v for k, v in m.items() if k != "seq"}), "format")
    signed_variant("other repository in base_url (signed)",
                   mp.manifest_bytes(dict(m, base_url=m["base_url"].replace("merakitech-plus", "evil"))), "format")
    signed_variant("tag != version (signed)", mp.manifest_bytes(dict(m, tag="v9.99")), "format")
    signed_variant("path traversal in release (signed)",
                   mp.manifest_bytes(dict(m, release=[dict(m["release"][0], name="../demo_v9")] + m["release"][1:])), "format")
    signed_variant("release entry not matching its asset (signed)",
                   mp.manifest_bytes(dict(m, release=[dict(m["release"][0], size=1)] + m["release"][1:])), "format")
    signed_variant("duplicate asset (signed)", mp.manifest_bytes(dict(m, assets=m["assets"] + m["assets"][:1])), "format")
    signed_variant("unknown extra field (forward compatible, signed)", mp.manifest_bytes(dict(m, zz_future="x")), "ok")
    big = mp.manifest_bytes(dict(m, zz_pad="x" * mp.MANIFEST_MAX))
    signed_variant("larger than 64 KiB (signed)", big, "format")
    # asset tampering after a valid signature
    bad_dir = work / "bad_assets"
    shutil.copytree(rel, bad_dir)
    (bad_dir / "merakiplus_snd").write_bytes((bad_dir / "merakiplus_snd").read_bytes() + b"\0")
    case("tampered asset (one byte appended)", data, key.sign(data), "asset", ["--assets", str(bad_dir)], bad_dir)
    gone = work / "gone_assets"
    shutil.copytree(rel, gone)
    (gone / "start.wav").unlink()
    case("missing asset", data, key.sign(data), "asset", ["--assets", str(gone)], gone)

    print("== Python and device verifier on every case (they must agree) ==")
    cdir = work / "cases"
    cdir.mkdir()
    script = ["#!/bin/sh", 'B="$1"; D="$(dirname "$0")"; P=0; F=0']
    for i, (name, mdata, sigt, args, expect, adir) in enumerate(cases):
        (cdir / f"{i}.json").write_bytes(mdata)
        if sigt is not None:
            (cdir / f"{i}.sig").write_text(sigt)
        got, _ = py_verify(cdir / f"{i}.json", cdir / f"{i}.sig", key.trusted(),
                           int(args[args.index("--max-seq") + 1]) if "--max-seq" in args else None, adir)
        check(f"python: {name} -> {expect}", got == expect, got)
        r = subprocess.run([str(upd), "verify", str(cdir / f"{i}.json"), str(cdir / f"{i}.sig"), *args],
                           capture_output=True, text=True)
        want_rc = 0 if expect == "ok" else EXIT[expect]
        check(f"device: {name} -> {expect}", r.returncode == want_rc, f"rc {r.returncode}: {r.stdout.strip()}")
        cargs = " ".join(shlex.quote(a.replace(str(work), "$W")) if str(work) not in a else '"$W' + a[len(str(work)):] + '"'
                         for a in args)
        script.append(f'"$B" verify "$D/{i}.json" "$D/{i}.sig" {cargs} >/dev/null 2>&1; rc=$?; '
                      f'if [ $rc -eq {want_rc} ]; then P=$((P+1)); else F=$((F+1)); echo "FAIL {i} rc=$rc"; fi')
    r = subprocess.run([str(upd), "verify", str(man), str(sig)], capture_output=True, text=True)
    check("device: success line reports version, seq, key id",
          r.stdout.strip() == f"ok version={mp.VERSION} seq=5 key={key.id} files=0", r.stdout)
    r = subprocess.run([str(PROD), "verify", str(man), str(sig)], capture_output=True, text=True)
    check("production device build rejects a test-key signature", r.returncode == 3 and "unknown key" in r.stdout, r.stdout)
    check("production Python verifier rejects a test-key signature", py_verify(man, sig, None)[0] == "signature")
    r = subprocess.run([str(upd), "verify", str(man), str(sig), "--version", "0.1"], capture_output=True, text=True)
    check("device: --version mismatch rejected", r.returncode == 4, r.stdout)

    print("== installed release directory (--release-dir) ==")
    rd = work / "reldir"
    (rd / "audio").mkdir(parents=True)
    for e in m["release"]:
        shutil.copy(rel / e["src"], rd / e["name"])
    r = subprocess.run([str(upd), "verify", str(man), str(sig), "--release-dir", str(rd)], capture_output=True, text=True)
    check("release directory matches the signed manifest", r.returncode == 0 and "files=5" in r.stdout, r.stdout)
    (rd / "demo_v9").write_bytes(b"\x7fELF not ours")
    r = subprocess.run([str(upd), "verify", str(man), str(sig), "--release-dir", str(rd)], capture_output=True, text=True)
    check("replaced demo_v9 in the release directory rejected", r.returncode == 6, r.stdout)
    r = subprocess.run([str(upd), "sha256", str(rel / "start.wav")], capture_output=True, text=True)
    check("sha256 subcommand == hashlib", r.stdout.split()[0] == mp.AUDIO_SHA256["start.wav"], r.stdout)

    print("== installer: verify-release ==")
    saved = dict(mp.TRUSTED_KEYS)
    mp.TRUSTED_KEYS.clear()
    mp.TRUSTED_KEYS.update(key.trusted())
    try:
        quiet = lambda *a: mp.main(list(a))
        import contextlib
        import io
        with contextlib.redirect_stdout(io.StringIO()):
            check("verify-release on a complete signed release", quiet("verify-release", str(rel)) == 0)
            check("verify-release: tampered asset fails", quiet("verify-release", str(bad_dir)) == 1)
            check("verify-release: missing asset fails", quiet("verify-release", str(gone)) == 1)
            check("verify-release --partial tolerates a missing asset", quiet("verify-release", str(gone), "--partial") == 0)
            check("verify-release --max-seq 6 refuses seq 5", quiet("verify-release", str(rel), "--max-seq", "6") == 1)
    finally:
        mp.TRUSTED_KEYS.clear()
        mp.TRUSTED_KEYS.update(saved)

    print("== tools/sign_release.py ==")
    data2 = sr.build_manifest(rel, mp.VERSION, "0" * 40, "2026-11-23", assets, None)
    check("unsigned manifest (no seq) passes the unsigned check", mp.check_manifest(data2, signed=False)["version"] == mp.VERSION)
    try:
        mp.check_manifest(data2, signed=True)
        check("unsigned manifest is refused where a signed one is required", False)
    except mp.SigError:
        check("unsigned manifest is refused where a signed one is required", True)
    try:
        sr.build_manifest(rel, mp.VERSION, "0" * 40, "2026-11-23", [a for a in assets if a != "merakiplus_boot"], 1)
        check("a missing launcher asset stops the manifest builder", False)
    except sr.Fail:
        check("a missing launcher asset stops the manifest builder", True)
    enc = work / "enc"
    enc.mkdir()
    pw = enc / "pw"
    pw.write_text("test-passphrase-only\n")
    r = subprocess.run([sys.executable, str(ROOT / "tools/sign_release.py"), "keygen", "--key", str(enc / "k.pem"),
                        "--passin-file", str(pw)], capture_output=True, text=True)
    kp = (enc / "k.pem")
    check("keygen writes an encrypted (scrypt PKCS#8) key, mode 600, and a .pub",
          r.returncode == 0 and kp.read_text().startswith("-----BEGIN " + "ENCRYPTED PRIVATE KEY") and
          (kp.stat().st_mode & 0o777) == 0o600 and (enc / "k.pub").is_file(), r.stdout + r.stderr)
    r2 = subprocess.run([sys.executable, str(ROOT / "tools/sign_release.py"), "keygen", "--key", str(kp),
                         "--passin-file", str(pw)], capture_output=True, text=True)
    check("keygen refuses to overwrite a key", r2.returncode == 1 and "refusing" in r2.stderr, r2.stderr)
    r3 = subprocess.run([sys.executable, str(ROOT / "tools/sign_release.py"), "keygen", "--key",
                         str(ROOT / "build" / "never.pem"), "--passin-file", str(pw)], capture_output=True, text=True)
    check("keygen refuses a key inside the repository", r3.returncode == 1 and not (ROOT / "build/never.pem").exists())
    if r.returncode == 0:
        epk = mp.signify_blob((enc / "k.pub").read_text(), 42)[1]
        s = sr.sign_bytes(kp, data, pw)
        check("encrypted key signs with the passphrase", mp.ed25519_verify(epk, data, s))
        wrong = enc / "wrong"
        wrong.write_text("not-the-passphrase\n")
        try:
            sr.sign_bytes(kp, data, wrong)
            check("encrypted key refuses a wrong passphrase", False)
        except sr.Fail as e:
            check("encrypted key refuses a wrong passphrase", "passphrase" in str(e), str(e))

    print("== release.yml: the unsigned-manifest step (dry run) ==")
    wf = (ROOT / ".github/workflows/release.yml").read_text()
    files = re.search(r'^\s*files="([^"]+)"', wf, re.M).group(1).split()
    pub_assets = [pathlib.Path(a).name for a in re.search(r'^\s*assets="([^"]+)"', wf, re.M).group(1).split()]
    step = re.search(r"^\s*(python3 tools/sign_release\.py manifest .+)$", wf, re.M)
    check("release.yml builds manifest.unsigned.json with sign_release.py", step is not None)
    check("release.yml publishes manifest.unsigned.json, never manifest.json/.sig (CI cannot sign)",
          "manifest.unsigned.json" in pub_assets and "manifest.json" not in pub_assets and "manifest.sig" not in pub_assets)
    check("published assets = files + MD5SUMS + SHA256SUMS + manifest.unsigned.json",
          sorted(pub_assets) == sorted(files + ["MD5SUMS", "SHA256SUMS", "manifest.unsigned.json"]), pub_assets)
    check("every published binary/sound is one sign_release.py knows how to verify",
          all(f in sr.COMMITTED_ONLY or f in mp.AUDIO_MD5 or f in st.BIN_ASSETS for f in files))
    if step:
        dist = work / "dist"
        st.make_dist(dist)
        cmd = step.group(1).replace("$VERSION", mp.VERSION).replace('"$COMMIT"', "0" * 40).replace('"$RELEASED"', "2026-11-23")
        cmd = cmd.replace("$files", " ".join(files)).replace("tools/sign_release.py", str(ROOT / "tools/sign_release.py"))
        r = subprocess.run(cmd, shell=True, cwd=work, capture_output=True, text=True,
                           env=dict(os.environ, PYTHONPATH=""), executable="/bin/sh")
        ok = r.returncode == 0 and (dist / "manifest.unsigned.json").is_file()
        check("workflow manifest command runs on a CI-like dist/", ok, r.stdout + r.stderr)
        if ok:
            u = mp.check_manifest((dist / "manifest.unsigned.json").read_bytes(), signed=False)
            check("unsigned manifest lists exactly the published assets (minus itself)",
                  sorted(e["name"] for e in u["assets"]) == sorted(files + ["MD5SUMS", "SHA256SUMS"]))

    print("== real signed manifest (tests/fixtures/) ==")
    fx = sorted((HERE / "fixtures").glob("release-v*/manifest.json"))
    check("at least one published signed manifest is kept as a fixture", bool(fx))
    for f in fx:
        s = f.with_name("manifest.sig")
        kind, seq = py_verify(f, s, None)
        check(f"python (pinned keys): {f.parent.name} verifies", kind == "ok", kind)
        r = subprocess.run([str(PROD), "verify", str(f), str(s)], capture_output=True, text=True)
        check(f"device production build: {f.parent.name} verifies", r.returncode == 0, r.stdout)
        t = work / "fx.json"
        t.write_bytes(f.read_bytes().replace(b'"size": ', b'"size": 1', 1))
        r = subprocess.run([str(PROD), "verify", str(t), str(s)], capture_output=True, text=True)
        check(f"device production build: tampered {f.parent.name} rejected", r.returncode == 3, r.stdout)

    if arm:
        print("== static ARM build in the arm32v7 container (same cases) ==")
        aupd = build_test_upd(key, work, arm=True)
        shutil.copy(aupd, cdir / "upd")
        (cdir / "run.sh").write_text("\n".join(script + ['echo "ARM cases: $P passed, $F failed"', '[ $F -eq 0 ]']) + "\n")
        for p in [work, *work.rglob("*")]:
            os.chmod(p, 0o777 if p.is_dir() or p.name in ("upd", "run.sh") else 0o644)
        r = subprocess.run(["docker", "run", "--rm", "--network", "none", "--platform", "linux/arm/v7", "--user", "1000:1000",
                            "-v", f"{work}:/w", "-e", "W=/w", "arm32v7/debian:bullseye-slim",
                            "sh", "-c", "getconf LONG_BIT; /w/cases/upd --selftest | tail -n 1; sh /w/cases/run.sh /w/cases/upd"],
                           capture_output=True, text=True)
        print("    " + r.stdout.strip().replace("\n", "\n    "))
        check("ARM: 32-bit container", r.stdout.startswith("32"), r.stdout + r.stderr)
        check("ARM: selftest passes", "passed" in r.stdout and "FAIL" not in r.stdout.split("ARM cases")[0])
        check(f"ARM: all {len(cases)} cases agree with the host", r.returncode == 0, r.stdout[-500:] + r.stderr[-300:])


if __name__ == "__main__":
    main()
