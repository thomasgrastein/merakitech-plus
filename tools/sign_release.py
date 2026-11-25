#!/usr/bin/env python3
"""sign_release.py — signed release manifests for Meraki+ (docs/signing.md, docs/release-process.md).

Runs on the MAINTAINER's own computer. CI never has the private key: it builds a DRAFT release with an unsigned
manifest.unsigned.json; signing — and publishing — is a separate, offline step.

    tools/sign_release.py keygen   [--key PATH]                      new Ed25519 key, passphrase-encrypted (OpenSSL 3)
    tools/sign_release.py pin-keys [--key PATH]                      pin the key id OUTSIDE the repository
    tools/sign_release.py manifest --dir DIST --version V --commit SHA --released YYYY-MM-DD --assets NAME... \\
                                   [--seq N] --out FILE                (CI: unsigned, without --seq)
    tools/sign_release.py sign vX.Y [--key PATH] [--zig-dir DIR] [--upload [--no-publish]] [--resign] [--yes]

`sign` = download every asset of the (draft) GitHub release -> resolve its commit (the draft's target must be on
origin/main) -> REVIEW: commits, diffstat and trust-anchor changes since the last signed release, and the trusted keys at
that commit against the pin in ~/.merakiplus/trusted-key-pin; you confirm by typing the commit SHA -> rebuild that commit
in a temporary git worktree with the OFFICIAL zig 0.17.1 (own cache) -> every reproducible binary must equal the fresh
build, the committed bin/ and the downloaded asset byte for byte; the rest (WAVs, the macOS SIM, MD5SUMS, SHA256SUMS)
must equal the repository files / recomputed sums -> manifest.json with seq = highest signed seq + 1 -> Ed25519
signature (OpenSSL asks for the passphrase on the terminal) -> verify with the keys pinned in tools/merakiplus.py ->
with --upload: upload manifest.json + manifest.sig to the draft, verify the download, then PUBLISH the draft (creates
the tag; with immutable releases it is final) and verify the published release once more.

Needs: git, gh (logged in), openssl >= 3.0, the official zig 0.17.1. Standard library only. Run it from a checkout you
trust (docs/signing.md §5): it executes the release's build.sh in a worktree on this computer.
"""
import argparse
import ast
import base64
import hashlib
import os
import pathlib
import re
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
RELEASE_FILES = [("demo_v9", "merakiplus", "755"), ("merakiplus_snd", "merakiplus_snd", "755")] + \
                [("audio/" + n, n, "644") for n in sorted(mp.AUDIO_MD5)]
# fixed-path platform components (updated separately, §5.6): component <- release asset
PLATFORM_FILES = [("grab_shim.so", "grab_shim.so"), ("launcher", "merakiplus_boot"), ("switcher", "switcher")]
SUMS = ("MD5SUMS", "SHA256SUMS")
# not bit-reproducible: equal to the committed bin/. grab_shim.so is reproducible (trimmed) from the tags whose
# tests/check_bins.py lists it; for older tags it is compared with the committed file only.
COMMITTED_ONLY = ("grab_shim.so", "merakiplus_sim-macos-arm64")
# Trust anchors: paths that decide what users install or which signatures they trust. .github/CODEOWNERS owns each of
# them (tests/hygiene.py checks it), ci.yml warns when a PR changes one, and `sign` highlights them in its review.
TRUST_ANCHORS = (".github/", "install.sh", "revert.sh", "build.sh", "tools/merakiplus.py", "tools/sign_release.py",
                 "keys/", "src/merakiplus_upd_keys.h", "tests/hygiene.py", "tests/check_bins.py", "bin/")
# The maintainer's own record of the release key(s), kept OUTSIDE the repository: a change to the pinned keys in a
# release (a key added or replaced by a merged PR) is refused at signing time unless --accept-key-change is given.
KEY_PIN = pathlib.Path.home() / ".merakiplus" / "trusted-key-pin"


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
    if a.pin:
        print(f"key pin (outside the repository): {write_key_pin({key_id(pk).hex(): pk.hex()}, a.pin)}")
    else:
        print(f"Then record the key id outside the repository: tools/sign_release.py pin-keys  (-> {KEY_PIN})")


def cmd_manifest(a):
    data = build_manifest(a.dir, a.version, a.commit, a.released, a.assets, a.seq)
    pathlib.Path(a.out).write_bytes(data)
    print(f"{a.out}: {len(data)} bytes, {len(a.assets)} assets, " + (f"seq {a.seq}" if a.seq else "unsigned"))


# ------------------------------------------------------------------ trust anchors, key pin, review
def trust_anchor_hits(paths):
    """the paths (repository-relative, '/'-separated) that are trust anchors"""
    return [p for p in paths if any(p == a or (a.endswith("/") and p.startswith(a)) for a in TRUST_ANCHORS)]


def read_key_pin(path=None):
    """{key id: public key hex} from the pin file, or None when there is none. Format: one `<keyid> <pubhex>` per line."""
    path = pathlib.Path(path or KEY_PIN)
    if not path.is_file():
        return None
    pins = {}
    for n, ln in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        ln = ln.split("#", 1)[0].strip()
        if not ln:
            continue
        f = ln.split()
        if len(f) != 2 or not re.fullmatch(r"[0-9a-f]{16}", f[0]) or not re.fullmatch(r"[0-9a-f]{64}", f[1]):
            raise Fail(f"{path}:{n}: expected '<16-hex key id> <64-hex public key>'")
        pins[f[0]] = f[1]
    return pins


def write_key_pin(keys, path=None):
    path = pathlib.Path(path or KEY_PIN)
    if str(path.resolve()).startswith(str(ROOT)):
        raise Fail("the key pin must live outside the repository (a PR could change it otherwise)")
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.write_text("# Meraki+ release signing keys this maintainer trusts (docs/signing.md). Outside the repository on "
                    "purpose.\n" + "".join(f"{k} {v}\n" for k, v in sorted(keys.items())), encoding="ascii")
    os.chmod(path, 0o600)
    return path


def keys_at(rev, repo=None):
    """The three pinned key sets at a commit: TRUSTED_KEYS (tools/merakiplus.py), src/merakiplus_upd_keys.h, keys/*.pub."""
    repo = str(repo or ROOT)
    show = lambda path: sh(["git", "-C", repo, "show", f"{rev}:{path}"], capture_output=True, text=True).stdout
    tree = ast.parse(show("tools/merakiplus.py"))
    py = next(ast.literal_eval(n.value) for n in ast.walk(tree)
              if isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") == "TRUSTED_KEYS")
    hdr = dict(re.findall(r'\{"([0-9a-f]{16})", "([0-9a-f]{64})"\}', show("src/merakiplus_upd_keys.h")))
    pubs = {}
    names = sh(["git", "-C", repo, "ls-tree", "--name-only", f"{rev}:keys"], capture_output=True, text=True).stdout
    for n in names.split():
        if n.endswith(".pub"):
            kid, pk = mp.signify_blob(show(f"keys/{n}"), 42)
            pubs[kid] = pk.hex()
    return {"tools/merakiplus.py TRUSTED_KEYS": py, "src/merakiplus_upd_keys.h": hdr, "keys/*.pub": pubs}


def check_key_pin(sets, pin_path=None, accept_change=False):
    """Compare the key sets of the release with the maintainer's pin. Missing pin: a warning. Any difference: refused
    unless accept_change (then a loud warning). Returns a list of report lines."""
    pin = read_key_pin(pin_path)
    lines = []
    for where, keys in sets.items():
        lines.append(f"  {where}: {', '.join(sorted(keys)) or '(none)'}")
    if pin is None:
        lines.append(f"  WARNING: no key pin at {pin_path or KEY_PIN} — the keys above are NOT checked against your own "
                     "record. Create it once: tools/sign_release.py pin-keys (from your private key)")
        return lines
    bad = [w for w, k in sets.items() if k != pin]
    if not bad:
        lines.append(f"  [OK] every key set equals your pin ({', '.join(sorted(pin))})")
        return lines
    for w in bad:
        extra, missing = sorted(set(sets[w]) - set(pin)), sorted(set(pin) - set(sets[w]))
        changed = sorted(k for k in set(sets[w]) & set(pin) if sets[w][k] != pin[k])
        lines.append(f"  WARNING: {w} differs from your pin: added {extra or '-'}, removed {missing or '-'}, "
                     f"changed {changed or '-'}")
    if not accept_change:
        for ln in lines:
            print(ln)
        raise Fail("the release's trusted keys differ from your key pin — a merged change added or replaced a signing "
                   "key. NOT signing. If this is your own planned rotation, re-run with --accept-key-change and update "
                   "the pin afterwards (pin-keys)")
    lines.append("  --accept-key-change: signing anyway (planned key rotation)")
    return lines


def review_since(prev_tag, rev, repo=None):
    """Commits, diffstat and trust-anchor changes from the last signed release to `rev`. Returns (report lines,
    changed paths, trust-anchor paths)."""
    repo = str(repo or ROOT)
    git = lambda *a: sh(["git", "-C", repo, *a], capture_output=True, text=True).stdout
    if not prev_tag:
        return (["  (no earlier signed release: review the whole history yourself)"], [], [])
    rng = f"{prev_tag}..{rev}"
    log = git("log", "--oneline", "--no-decorate", rng).strip()
    stat = git("diff", "--stat", rng).strip()
    names = [n for n in git("diff", "--name-only", rng).splitlines() if n]
    hits = trust_anchor_hits(names)
    lines = [f"  commits {rng} ({len(log.splitlines())}):"] + ["    " + ln for ln in log.splitlines()]
    lines += ["  diffstat:"] + ["    " + ln for ln in stat.splitlines()]
    if hits:
        lines += [f"  WARNING: {len(hits)} TRUST-ANCHOR path(s) changed — read these diffs line by line "
                  f"(git diff {rng} -- <path>):"] + ["    ! " + h for h in hits]
    else:
        lines.append("  [OK] no trust-anchor path changed")
    return lines, names, hits


def confirm_commit(commit, given=None):
    """The maintainer confirms the reviewed commit by its SHA (at least 12 characters, typed or --confirm-commit)."""
    got = (given if given is not None else input(f"Type the first 12 characters of the commit you reviewed "
                                                  f"({commit[:4]}…) to continue: ")).strip().lower()
    if len(got) < 12 or not commit.startswith(got):
        raise Fail("commit not confirmed — NOT signing")


def resolve_commit(tag, rel, repo=None):
    """The commit to rebuild and sign: the tag's commit when the tag exists, else the draft's target, which must be a
    full SHA on origin/main."""
    repo = str(repo or ROOT)
    git = lambda *a, **kw: sh(["git", "-C", repo, *a], capture_output=True, text=True, **kw)
    t = git("rev-parse", "-q", "--verify", f"refs/tags/{tag}^{{commit}}", check=False)
    target = rel.get("targetCommitish") or ""
    if t.returncode == 0:
        commit = t.stdout.strip()
        if rel.get("isDraft") and re.fullmatch(r"[0-9a-f]{40}", target) and target != commit:
            raise Fail(f"tag {tag} exists at {commit[:12]} but the draft targets {target[:12]} — resolve that first")
    elif rel.get("isDraft"):
        if not re.fullmatch(r"[0-9a-f]{40}", target):
            raise Fail(f"draft {tag} targets {target!r}, not a commit SHA — recreate the draft from release.yml")
        if git("cat-file", "-e", f"{target}^{{commit}}", check=False).returncode:
            raise Fail(f"draft target {target[:12]} is not in this clone — git fetch origin")
        commit = target
    else:
        raise Fail(f"{tag} is published but there is no tag {tag} in this clone — git fetch --tags origin")
    if git("merge-base", "--is-ancestor", commit, "origin/main", check=False).returncode:
        raise Fail(f"{commit[:12]} is not on origin/main — only commits merged into main are released")
    ver = re.search(r'^VERSION\s*=\s*"([^"]+)"', git("show", f"{commit}:tools/merakiplus.py").stdout, re.M)
    if not ver or "v" + ver.group(1) != tag:
        raise Fail(f"{commit[:12]} has VERSION {ver.group(1) if ver else '?'}, not {tag[1:]}")
    return commit


def cmd_pin_keys(a):
    """Write the key pin from the PRIVATE key you hold (not from repository files)."""
    key = pathlib.Path(a.key).expanduser()
    if not key.is_file():
        raise Fail(f"no signing key at {key}")
    pk = public_key(key, a.passin_file)
    keys = {key_id(pk).hex(): pk.hex()}
    path = write_key_pin(keys, a.pin)
    print(f"key pin written: {path}  ({', '.join(keys)})")
    print("Cross-check the key id with your offline record and with the one published outside the repository "
          "(docs/signing.md §3).")


# ------------------------------------------------------------------ sign a (draft) release
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


def rebuild_and_compare(rev, dl, assets, zig_env, work):
    """Fresh build of the release commit == committed bin/ at it == downloaded asset (byte for byte)."""
    src = work / "src"
    sh(["git", "-C", str(ROOT), "worktree", "add", "--detach", str(src), rev], capture_output=True)
    try:
        env = dict(zig_env, ZIG_GLOBAL_CACHE_DIR=str(work / "zc"), ZIG_LOCAL_CACHE_DIR=str(work / "zcl"))
        print(f"== rebuilding {rev[:12]} in {src} (official zig {ZIG_VERSION}, own cache) ...")
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
                report.append((n, "rebuilt == bin/@commit == asset", ok))
            elif n in COMMITTED_ONLY:
                ok = sha256f(src / "bin" / n) == got
                report.append((n, "bin/@commit == asset (not reproducible by design)", ok))
            elif n in mp.AUDIO_MD5:
                ok = sha256f(src / "audio" / n) == got            # build.sh also ran tools/gen_audio.py --check
                report.append((n, "audio/@commit == asset (generator-checked)", ok))
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
            raise Fail("the release assets do not match a local rebuild of its commit — NOT signing")
        return sh(["git", "-C", str(src), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip(), \
            sh(["git", "-C", str(src), "log", "-1", "--format=%cd", "--date=format-local:%Y-%m-%d"],
               capture_output=True, text=True, env=dict(os.environ, TZ="UTC")).stdout.strip()
    finally:
        sh(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(src)], check=False, capture_output=True)


def signed_history(skip_tag):
    """(highest seq, its version, its tag) over every release that carries a valid signed manifest (signatures
    re-verified)."""
    best = (0, None, None)
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
            best = (m["seq"], m["version"], tag)
    return best


def cmd_sign(a):
    if os.environ.get("GITHUB_ACTIONS"):
        raise Fail("never sign in CI: the signing key stays on the maintainer's computer (docs/signing.md)")
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

    rel = gh_json("release", "view", tag, "--json", "assets,isDraft,isPrerelease,targetCommitish")
    draft = bool(rel.get("isDraft"))
    print(f"== {'DRAFT' if draft else 'published'} release {tag}: downloading its assets")
    names = [x["name"] for x in rel["assets"]]
    sh(["gh", "release", "download", tag, "--repo", mp.REPO, "--dir", str(dl)])
    assets = [n for n in names if not n.startswith("manifest")]
    print(f"   {len(assets)} assets: {', '.join(sorted(assets))}")
    if not draft and "manifest.json" not in names:
        print("   WARNING: this release was published unsigned (the old flow). With immutable releases on, nothing "
              "can be added to it any more; the next release uses draft -> sign -> publish.")

    print("== signed releases so far")
    prev_seq, prev_ver, prev_tag = signed_history(tag)
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

    commit = resolve_commit(tag, rel)
    print(f"== REVIEW: {tag} = commit {commit} (on origin/main), since the last signed release {prev_tag or '-'}")
    lines, _, _ = review_since(prev_tag, commit)
    for ln in lines:
        print(ln)
    print("== trusted keys at that commit vs. your key pin (outside the repository)")
    for ln in check_key_pin(keys_at(commit), a.pin, a.accept_key_change):
        print(ln)
    confirm_commit(commit, a.confirm_commit)

    commit2, released = rebuild_and_compare(commit, dl, assets, zig_env, work)
    if commit2 != commit:
        raise Fail(f"rebuilt {commit2[:12]}, reviewed {commit[:12]} — NOT signing")
    data = build_manifest(dl, version, commit, released, assets, seq)
    if "manifest.unsigned.json" in names:                      # cross-check with what CI computed
        ci = mp.check_manifest((dl / "manifest.unsigned.json").read_bytes(), signed=False)
        mine = mp.check_manifest(data, signed=True)
        for k in ("assets", "release", "platform", "version", "tag", "commit"):
            if ci[k] != mine[k]:
                raise Fail(f"CI's manifest.unsigned.json differs in '{k}' — NOT signing")
        print("  [OK] equals CI's manifest.unsigned.json (assets, release, platform, commit)")
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
        print(f"Not uploaded (no --upload). Upload later: gh release upload {tag} {out}/manifest.json {out}/manifest.sig"
              + (f"; then publish: gh release edit {tag} --draft=false" if draft else ""))
        return
    where = "draft" if draft else "published"
    if not a.yes and input(f"Upload manifest.json + manifest.sig to the {where} release {tag}? [y/N] ").lower() != "y":
        raise Fail("not uploaded")
    sh(["gh", "release", "upload", tag, "--repo", mp.REPO, str(out / "manifest.json"), str(out / "manifest.sig")]
       + (["--clobber"] if resign_seq else []))
    verify_download(tag, work / "check")
    print(f"SIGNED: {tag} seq {seq} carries manifest.json + manifest.sig")
    if not draft:
        return
    if a.no_publish:
        print(f"Not published (--no-publish). Publish later: gh release edit {tag} --draft=false"
              + ("" if rel.get("isPrerelease") else " --latest"))
        return
    if not a.yes and input(f"PUBLISH {tag} now (creates tag {tag} at {commit[:12]}; final with immutable releases)? "
                           "[y/N] ").lower() != "y":
        raise Fail(f"signed but not published — publish later: gh release edit {tag} --draft=false")
    sh(["gh", "release", "edit", tag, "--repo", mp.REPO, "--draft=false"]
       + ([] if rel.get("isPrerelease") else ["--latest"]))
    verify_download(tag, work / "published")
    sh(["git", "-C", str(ROOT), "fetch", "--tags", "--quiet", "origin"], check=False)
    tagged = sh(["git", "-C", str(ROOT), "rev-parse", "-q", "--verify", f"refs/tags/{tag}^{{commit}}"],
                capture_output=True, text=True, check=False).stdout.strip()
    if tagged != commit:
        raise Fail(f"published, but tag {tag} points at {tagged[:12] or '?'}, not the signed {commit[:12]} — investigate")
    print(f"PUBLISHED: {tag} (tag at {commit[:12]}), signed seq {seq}")


def verify_download(tag, d):
    """Download the release again and verify it with the production verifier (pinned keys)."""
    d.mkdir(parents=True)
    sh(["gh", "release", "download", tag, "--repo", mp.REPO, "--dir", str(d)])
    if mp.main(["verify-release", str(d)]):
        raise Fail(f"{tag} as downloaded does not verify — investigate before anything else")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="sign_release.py", description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("keygen", help="create the passphrase-encrypted Ed25519 signing key")
    p.add_argument("--key", default=str(DEFAULT_KEY))
    p.add_argument("--pin", help=f"also write the key pin (normally {KEY_PIN})")
    p.add_argument("--passin-file", help=argparse.SUPPRESS)      # scripted use: passphrase from a file (first line)
    p = sub.add_parser("pin-keys", help="pin the key id of your private key outside the repository")
    p.add_argument("--key", default=str(DEFAULT_KEY))
    p.add_argument("--pin", help=f"pin file (default {KEY_PIN})")
    p.add_argument("--passin-file", help=argparse.SUPPRESS)
    p = sub.add_parser("manifest", help="write a canonical manifest for a directory of release assets")
    p.add_argument("--dir", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--released", required=True)
    p.add_argument("--assets", nargs="+", required=True)
    p.add_argument("--seq", type=int)
    p.add_argument("--out", required=True)
    p = sub.add_parser("sign", help="review, rebuild-verify, sign, upload and publish a draft release")
    p.add_argument("tag")
    p.add_argument("--key", default=str(DEFAULT_KEY))
    p.add_argument("--passin-file", help=argparse.SUPPRESS)
    p.add_argument("--zig-dir", help="directory of the official zig 0.17.1 (prepended to PATH)")
    p.add_argument("--work", help="work directory (default build/sign-<tag>)")
    p.add_argument("--upload", action="store_true",
                   help="upload manifest.json + manifest.sig to the release, then publish the draft")
    p.add_argument("--no-publish", action="store_true", help="with --upload: leave the signed release a draft")
    p.add_argument("--resign", action="store_true", help="replace an existing signed manifest (keeps its seq)")
    p.add_argument("--pin", help=f"key pin file (default {KEY_PIN})")
    p.add_argument("--accept-key-change", action="store_true",
                   help="sign although the release's trusted keys differ from your pin (planned rotation only)")
    p.add_argument("--confirm-commit", help="the reviewed commit SHA (>= 12 characters) instead of typing it")
    p.add_argument("--yes", "-y", action="store_true")
    a = ap.parse_args(argv)
    try:
        {"keygen": cmd_keygen, "pin-keys": cmd_pin_keys, "manifest": cmd_manifest, "sign": cmd_sign}[a.cmd](a)
    except (Fail, mp.Abort) as e:
        print(f"sign_release: {e}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        print(f"sign_release: command failed: {' '.join(map(str, e.cmd[:3]))} … (exit {e.returncode})", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
