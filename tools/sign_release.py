#!/usr/bin/env python3
"""sign_release.py — signed release manifests for Meraki+ (docs/signing.md, docs/ota-plan.md phase 2).

Runs on the MAINTAINER's own computer. CI never has the private key: it publishes the release and an unsigned
manifest.unsigned.json; signing is a separate, offline step.

    tools/sign_release.py keygen   [--key PATH]                      new Ed25519 key, passphrase-encrypted (OpenSSL 3)
    tools/sign_release.py manifest --dir DIST --version V --commit SHA --released YYYY-MM-DD --assets NAME... \\
                                   [--seq N] --out FILE                (CI: unsigned, without --seq)
    tools/sign_release.py sign vX.Y [--key PATH] [--zig-dir DIR] [--upload] [--resign] [--yes]

`sign` = download every asset of the GitHub release -> rebuild the tagged source in a temporary git worktree with
the OFFICIAL zig 0.17.1 (own cache) -> every reproducible binary must equal the fresh build, the committed bin/ at
the tag and the downloaded asset byte for byte; the rest (WAVs, grab_shim.so, the macOS SIM, MD5SUMS, SHA256SUMS)
must equal the tagged repository files / recomputed sums -> manifest.json with seq = highest signed seq + 1 ->
Ed25519 signature (OpenSSL asks for the passphrase on the terminal) -> verify with the keys pinned in
tools/merakiplus.py -> with --upload: upload manifest.json + manifest.sig to the release and verify the download.

Needs: git, gh (logged in), openssl >= 3.0, the official zig 0.17.1. Standard library only.
"""
import argparse
import ast
import base64
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import merakiplus as mp  # noqa: E402

DEFAULT_KEY = pathlib.Path.home() / ".merakiplus" / "signing_ed25519.pem"
ZIG_VERSION = "0.17.1"
# release directory (switches atomically, docs/ota-plan.md §5.2): name inside rel/<x>/ <- release asset, mode
RELEASE_FILES = [("demo_v9", "merakiplus", "755"), ("merakiplus_snd", "merakiplus_snd", "755"), ("merakiplus_mqtt", "merakiplus_mqtt", "755")] + \
                [("audio/" + n, n, "644") for n in sorted(mp.AUDIO_MD5)]
# fixed-path platform components (updated separately, §5.6): component <- release asset
PLATFORM_FILES = [("grab_shim.so", "grab_shim.so"), ("launcher", "merakiplus_boot"), ("switcher", "switcher")]
SUMS = ("MD5SUMS", "SHA256SUMS")
COMMITTED_ONLY = ("grab_shim.so", "merakiplus_sim-macos-arm64")    # not bit-reproducible: equal to the committed bin/


class Fail(Exception):
    pass


def sh(cmd, **kw):
    kw.setdefault("check", True)
    return subprocess.run(cmd, **kw)


def sha256f(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


# ------------------------------------------------------------------ manifest
def build_manifest(dist, version, commit, released, assets, seq=None):
    """The canonical manifest (bytes) for the given asset files in `dist`. seq=None -> unsigned (CI)."""
    dist = pathlib.Path(dist)
    tab = {}
    for n in sorted(set(assets)):
        if n.startswith("manifest"):
            continue
        b = (dist / n).read_bytes()
        tab[n] = {"name": n, "sha256": hashlib.sha256(b).hexdigest(), "size": len(b)}
    for _, src, *_ in RELEASE_FILES + PLATFORM_FILES:
        if src not in tab:
            raise Fail(f"asset {src} is missing (needed by the release/platform section)")
    m = {
        "assets": [tab[n] for n in sorted(tab)],
        "base_url": mp.RELEASE_DL.format(tag="v" + version, name=""),
        "channel": "stable",
        "commit": commit,
        "format": 1,
        "platform": [dict(name=n, src=s, size=tab[s]["size"], sha256=tab[s]["sha256"]) for n, s in PLATFORM_FILES],
        "product": "merakiplus",
        "release": [dict(name=n, src=s, mode=md, size=tab[s]["size"], sha256=tab[s]["sha256"])
                    for n, s, md in RELEASE_FILES],
        "released": released,
        "tag": "v" + version,
        "version": version,
    }
    if seq is not None:
        m["seq"] = seq
    data = mp.manifest_bytes(m)
    mp.check_manifest(data, signed=seq is not None)          # never emit what the verifiers would refuse
    return data


# ------------------------------------------------------------------ keys / signatures (OpenSSL 3 does the private-key work)
def openssl():
    exe = shutil.which("openssl")
    out = sh([exe or "openssl", "version"], capture_output=True, text=True).stdout if exe else ""
    if not out.startswith("OpenSSL 3") and not out.startswith("OpenSSL 4"):
        raise Fail(f"OpenSSL >= 3 is required for Ed25519 (found: {out.strip() or 'none'}; macOS: brew install openssl@3)")
    return exe


def passin(passin_file):
    """-passin argument: a file (first line) for scripted use, else OpenSSL prompts on the terminal itself."""
    return ["-passin", "file:" + str(passin_file)] if passin_file else []


def key_id(pk):
    """signify key number: the first 8 bytes of SHA-256(public key) (deterministic, so it never needs storing)."""
    return hashlib.sha256(pk).digest()[:8]


def pub_text(pk, comment="Meraki+ release signing key (Ed25519, signify format)"):
    return f"untrusted comment: {comment}\n" + base64.b64encode(b"Ed" + key_id(pk) + pk).decode() + "\n"


def sig_text(keynum, sig):
    return (f"untrusted comment: verify with keys/merakiplus-release.pub (key id {keynum.hex()})\n"
            + base64.b64encode(b"Ed" + keynum + sig).decode() + "\n")


def public_key(key, passin_file=None):
    der = sh([openssl(), "pkey", "-in", str(key)] + passin(passin_file) + ["-pubout", "-outform", "DER"],
             capture_output=True).stdout
    if len(der) != 44:
        raise Fail("not an Ed25519 key")
    return der[-32:]


def sign_bytes(key, data, passin_file=None):
    """Ed25519 signature (64 bytes) of `data` with the (encrypted) PKCS#8 key file."""
    with tempfile.TemporaryDirectory() as t:
        m, s = pathlib.Path(t) / "m", pathlib.Path(t) / "s"
        m.write_bytes(data)
        # OpenSSL prompts for the passphrase on the terminal (/dev/tty), so stderr can be captured for errors
        r = subprocess.run([openssl(), "pkeyutl", "-sign", "-rawin", "-inkey", str(key)] + passin(passin_file) +
                           ["-in", str(m), "-out", str(s)], stderr=subprocess.PIPE, text=True)
        if r.returncode:
            why = [ln for ln in r.stderr.splitlines() if "wrong password" in ln or "bad decrypt" in ln]
            raise Fail("openssl could not sign" + (" — wrong passphrase?" if why else f": {r.stderr.strip()[-200:]}"))
        sig = s.read_bytes() if s.exists() else b""
    if len(sig) != 64:
        raise Fail("openssl returned no Ed25519 signature")
    return sig


def cmd_keygen(a):
    key = pathlib.Path(a.key).expanduser()
    if key.exists():
        raise Fail(f"{key} exists — refusing to overwrite a signing key")
    if str(key.resolve()).startswith(str(ROOT)):
        raise Fail("the private key must not live inside the repository")
    key.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(key.parent, 0o700)
    exe = openssl()
    # the plaintext key only ever exists in a pipe; on disk it is scrypt + AES-256-CBC encrypted PKCS#8
    raw = sh([exe, "genpkey", "-algorithm", "ed25519"], capture_output=True).stdout
    pk = sh([exe, "pkey", "-pubout", "-outform", "DER"], input=raw, capture_output=True).stdout[-32:]
    old = os.umask(0o077)
    try:
        sh([exe, "pkcs8", "-topk8", "-v2", "aes-256-cbc", "-scrypt", "-out", str(key)] +
           (["-passout", "file:" + str(a.passin_file)] if a.passin_file else []), input=raw)
    finally:
        os.umask(old)
        raw = None
    os.chmod(key, 0o600)
    if b"ENCRYPTED PRIVATE KEY" not in key.read_bytes():
        key.unlink()
        raise Fail("the key file was not encrypted — removed")
    print("checking the new key (enter the passphrase once more)" if not a.passin_file else "checking the new key")
    probe = b"merakiplus keygen probe"
    if public_key(key, a.passin_file) != pk or not mp.ed25519_verify(pk, probe, sign_bytes(key, probe, a.passin_file)):
        raise Fail("the new key does not sign correctly")
    pub = key.with_suffix(".pub")
    pub.write_text(pub_text(pk))
    print(f"private key (encrypted): {key}  (mode 600)")
    print(f"public key             : {pub}")
    print(f"key id                 : {key_id(pk).hex()}")
    print(f"public key (hex)       : {pk.hex()}")
    print("Pin it: keys/merakiplus-release.pub, TRUSTED_KEYS in tools/merakiplus.py, src/merakiplus_upd_keys.h "
          "(tests/hygiene.py checks they agree). Make the offline backup now (docs/signing.md).")


def cmd_manifest(a):
    data = build_manifest(a.dir, a.version, a.commit, a.released, a.assets, a.seq)
    pathlib.Path(a.out).write_bytes(data)
    print(f"{a.out}: {len(data)} bytes, {len(a.assets)} assets, " + (f"seq {a.seq}" if a.seq else "unsigned"))


# ------------------------------------------------------------------ sign a published release
def gh_json(*args):
    import json
    return json.loads(sh(["gh", *args, "--repo", mp.REPO], capture_output=True, text=True).stdout)


def official_zig(zig_dir):
    env = dict(os.environ)
    if zig_dir:
        env["PATH"] = str(pathlib.Path(zig_dir).expanduser()) + os.pathsep + env["PATH"]
    zig = shutil.which("zig", path=env["PATH"])
    if not zig:
        raise Fail("zig not found (pass --zig-dir with the official 0.17.1 tarball)")
    ver = sh([zig, "version"], capture_output=True, text=True, env=env).stdout.strip()
    cc = sh([zig, "cc", "--version"], capture_output=True, text=True, env=env).stdout.splitlines()[0]
    if ver != ZIG_VERSION or "Homebrew" in cc:
        raise Fail(f"{zig} is {ver} ({cc}) — the comparison needs the OFFICIAL zig {ZIG_VERSION} tarball "
                   "(ziglang.org); pass --zig-dir")
    return env


def rebuild_and_compare(tag, dl, assets, zig_env, work):
    """Fresh build of the tagged source == committed bin/ at the tag == downloaded asset (byte for byte)."""
    src = work / "src"
    sh(["git", "-C", str(ROOT), "worktree", "add", "--detach", str(src), tag], capture_output=True)
    try:
        env = dict(zig_env, ZIG_GLOBAL_CACHE_DIR=str(work / "zc"), ZIG_LOCAL_CACHE_DIR=str(work / "zcl"))
        print(f"== rebuilding {tag} in {src} (official zig {ZIG_VERSION}, own cache) ...")
        log = work / "build.log"
        with open(log, "w") as lf:
            r = subprocess.run(["./build.sh"], cwd=src, env=env, stdout=lf, stderr=subprocess.STDOUT)
            if r.returncode == 0:
                r = subprocess.run(["zig", "cc", "-target", "x86_64-linux-musl", "-DMERAKIPLUS_SIM", "-O2", "-static",
                                    "-s", "src/merakiplus.c", "-lm", "-o", "build/merakiplus_sim-linux-x86_64"],
                                   cwd=src, env=env, stdout=lf, stderr=subprocess.STDOUT)
        if r.returncode:
            raise Fail(f"rebuild failed — see {log}")
        tree = ast.parse((src / "tests" / "check_bins.py").read_text())
        repro = next(ast.literal_eval(n.value) for n in ast.walk(tree)
                     if isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") == "REPRODUCIBLE")
        report = []
        for n in sorted(assets):
            got = sha256f(dl / n)
            if n in SUMS:
                continue
            if n in repro:
                fresh, committed = sha256f(src / "build" / n), sha256f(src / "bin" / n)
                ok = fresh == committed == got
                report.append((n, "rebuilt == bin/@tag == asset", ok))
            elif n in COMMITTED_ONLY:
                ok = sha256f(src / "bin" / n) == got
                report.append((n, "bin/@tag == asset (not reproducible by design)", ok))
            elif n in mp.AUDIO_MD5:
                ok = sha256f(src / "audio" / n) == got            # build.sh also ran tools/gen_audio.py --check
                report.append((n, "audio/@tag == asset (generator-checked)", ok))
            else:
                raise Fail(f"unexpected release asset {n} — teach sign_release.py what it is before signing")
        for n in SUMS:                                            # recomputed sums over the downloaded assets
            algo = hashlib.md5 if n == "MD5SUMS" else hashlib.sha256
            lines = {}
            for ln in (dl / n).read_text().splitlines():
                h, f = ln.split(None, 1)
                lines[f.strip().lstrip("*")] = h
            want = {f: algo((dl / f).read_bytes()).hexdigest() for f in assets if f not in SUMS and not f.startswith("manifest")}
            report.append((n, "matches the downloaded assets", lines == want))
        for n, how, ok in report:
            print(f"  [{'OK' if ok else 'FAIL'}] {n}: {how}")
        if not all(ok for _, _, ok in report):
            raise Fail("the published release does not match a local rebuild of its tag — NOT signing")
        return sh(["git", "-C", str(src), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip(), \
            sh(["git", "-C", str(src), "log", "-1", "--format=%cd", "--date=format-local:%Y-%m-%d"],
               capture_output=True, text=True, env=dict(os.environ, TZ="UTC")).stdout.strip()
    finally:
        sh(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(src)], check=False, capture_output=True)


def signed_history(skip_tag):
    """(highest seq, its version) over every release that carries a valid signed manifest (signatures re-verified)."""
    best = (0, None)
    for r in gh_json("release", "list", "--limit", "200", "--json", "tagName"):
        tag = r["tagName"]
        names = [x["name"] for x in gh_json("release", "view", tag, "--json", "assets")["assets"]]
        if "manifest.json" not in names:
            continue
        with tempfile.TemporaryDirectory() as t:
            sh(["gh", "release", "download", tag, "--repo", mp.REPO, "--dir", t, "-p", "manifest.json", "-p",
                "manifest.sig"], capture_output=True)
            m = mp.verify_manifest((pathlib.Path(t) / "manifest.json").read_bytes(),
                                   (pathlib.Path(t) / "manifest.sig").read_text())
        print(f"  signed: {tag} seq {m['seq']}")
        if tag == skip_tag:
            continue
        if m["seq"] > best[0]:
            best = (m["seq"], m["version"])
    return best


def cmd_sign(a):
    tag = a.tag if a.tag.startswith("v") else "v" + a.tag
    version = tag[1:]
    key = pathlib.Path(a.key).expanduser()
    if not key.is_file():
        raise Fail(f"no signing key at {key} (tools/sign_release.py keygen, or --key)")
    if not mp.TRUSTED_KEYS:
        raise Fail("tools/merakiplus.py pins no TRUSTED_KEYS")
    openssl()
    zig_env = official_zig(a.zig_dir)
    work = pathlib.Path(a.work or ROOT / "build" / f"sign-{tag}").resolve()
    if work.exists():
        shutil.rmtree(work)
    dl, out = work / "dl", work / "out"
    dl.mkdir(parents=True)
    out.mkdir()
    sh(["git", "-C", str(ROOT), "fetch", "--tags", "--quiet", "origin"], check=False)

    print(f"== downloading the assets of {tag}")
    names = [x["name"] for x in gh_json("release", "view", tag, "--json", "assets")["assets"]]
    sh(["gh", "release", "download", tag, "--repo", mp.REPO, "--dir", str(dl)])
    assets = [n for n in names if not n.startswith("manifest")]
    print(f"   {len(assets)} assets: {', '.join(sorted(assets))}")

    print("== signed releases so far")
    prev_seq, prev_ver = signed_history(tag)
    resign_seq = None
    if "manifest.json" in names:
        old = mp.verify_manifest((dl / "manifest.json").read_bytes(), (dl / "manifest.sig").read_text())
        if not a.resign:
            raise Fail(f"{tag} is already signed (seq {old['seq']}); use --resign to replace it (keeps its seq)")
        resign_seq = old["seq"]
    vkey = lambda v: tuple(int(x) for x in v.split("."))
    if prev_ver and resign_seq is None and vkey(version) <= vkey(prev_ver):
        raise Fail(f"{tag} is not newer than the last signed release v{prev_ver} — seq must follow the version order")
    seq = resign_seq or prev_seq + 1

    commit, released = rebuild_and_compare(tag, dl, assets, zig_env, work)
    data = build_manifest(dl, version, commit, released, assets, seq)
    if "manifest.unsigned.json" in names:                      # cross-check with what CI computed
        ci = mp.check_manifest((dl / "manifest.unsigned.json").read_bytes(), signed=False)
        mine = mp.check_manifest(data, signed=True)
        for k in ("assets", "release", "platform", "version", "tag"):
            if ci[k] != mine[k]:
                raise Fail(f"CI's manifest.unsigned.json differs in '{k}' — NOT signing")
        print("  [OK] equals CI's manifest.unsigned.json (assets, release, platform)")
    (out / "manifest.json").write_bytes(data)
    print(f"== manifest.json: {tag} seq {seq} commit {commit[:12]} released {released} ({len(data)} bytes)")

    print("== signing (OpenSSL asks for the key passphrase)" if not a.passin_file else "== signing")
    sig = sign_bytes(key, data, a.passin_file)
    kid = next((k for k, pk in mp.TRUSTED_KEYS.items() if mp.ed25519_verify(bytes.fromhex(pk), data, sig)), None)
    if kid is None:
        raise Fail(f"{key} is not one of the keys pinned in tools/merakiplus.py — NOT publishing that signature")
    (out / "manifest.sig").write_text(sig_text(bytes.fromhex(kid), sig))
    m = mp.verify_manifest(data, (out / "manifest.sig").read_text())          # the pinned keys, not the key file
    mp.verify_assets(m, {n: (dl / n).read_bytes() for n in assets})
    print(f"  [OK] signature verifies with the pinned key {m['_keyid']}; all {len(assets)} assets match")
    upd = ROOT / "build" / "merakiplus_upd_host"
    if upd.is_file():
        r = sh([str(upd), "verify", str(out / "manifest.json"), str(out / "manifest.sig"), "--assets", str(dl)],
               capture_output=True, text=True, check=False)
        print(f"  [{'OK' if r.returncode == 0 else 'FAIL'}] device verifier (host build): {r.stdout.strip()}")
        if r.returncode:
            raise Fail("the device verifier rejects the manifest")
    print(f"   -> {out}/manifest.json, manifest.sig")

    if not a.upload:
        print(f"Not uploaded (no --upload). Upload later: gh release upload {tag} {out}/manifest.json {out}/manifest.sig")
        return
    if not a.yes and input(f"Upload manifest.json + manifest.sig to the published release {tag}? [y/N] ").lower() != "y":
        raise Fail("not uploaded")
    sh(["gh", "release", "upload", tag, "--repo", mp.REPO, str(out / "manifest.json"), str(out / "manifest.sig")]
       + (["--clobber"] if resign_seq else []))
    chk = work / "check"
    chk.mkdir()
    sh(["gh", "release", "download", tag, "--repo", mp.REPO, "--dir", str(chk)])
    rc = mp.main(["verify-release", str(chk)])
    if rc:
        raise Fail("the uploaded release does not verify — investigate before anything else")
    print(f"SIGNED: {tag} seq {seq} is published with manifest.json + manifest.sig")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="sign_release.py", description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("keygen", help="create the passphrase-encrypted Ed25519 signing key")
    p.add_argument("--key", default=str(DEFAULT_KEY))
    p.add_argument("--passin-file", help=argparse.SUPPRESS)      # scripted use: passphrase from a file (first line)
    p = sub.add_parser("manifest", help="write a canonical manifest for a directory of release assets")
    p.add_argument("--dir", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--released", required=True)
    p.add_argument("--assets", nargs="+", required=True)
    p.add_argument("--seq", type=int)
    p.add_argument("--out", required=True)
    p = sub.add_parser("sign", help="rebuild-verify, sign and (optionally) upload a published release")
    p.add_argument("tag")
    p.add_argument("--key", default=str(DEFAULT_KEY))
    p.add_argument("--passin-file", help=argparse.SUPPRESS)
    p.add_argument("--zig-dir", help="directory of the official zig 0.17.1 (prepended to PATH)")
    p.add_argument("--work", help="work directory (default build/sign-<tag>)")
    p.add_argument("--upload", action="store_true", help="upload manifest.json + manifest.sig to the release")
    p.add_argument("--resign", action="store_true", help="replace an existing signed manifest (keeps its seq)")
    p.add_argument("--yes", "-y", action="store_true")
    a = ap.parse_args(argv)
    try:
        {"keygen": cmd_keygen, "manifest": cmd_manifest, "sign": cmd_sign}[a.cmd](a)
    except (Fail, mp.Abort) as e:
        print(f"sign_release: {e}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        print(f"sign_release: command failed: {' '.join(map(str, e.cmd[:3]))} … (exit {e.returncode})", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
