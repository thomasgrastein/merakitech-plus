"""sigtest.py — throwaway Ed25519 TEST keys and signed test releases (tests/test_upd.py, tests/test_installer.py).

Test keys are generated per run with OpenSSL (unencrypted, in a temp dir) and are never trusted by a shipped build:
the production verifiers only know the keys pinned in tools/merakiplus.py / src/merakiplus_upd_keys.h.
"""
import hashlib
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import merakiplus as mp     # noqa: E402
import sign_release as sr   # noqa: E402

# the release assets of a published version (.github/workflows/release.yml `files` + the two sum files)
BIN_ASSETS = ["merakiplus", "merakiplus_snd", "merakiplus_boot", "switcher", "grab_shim.so", "merakiplus_sim_arm",
              "merakiplus_sim-linux-x86_64", "merakiplus_sim-macos-arm64"]
AUDIO_ASSETS = sorted(mp.AUDIO_MD5)


class TestKey:
    def __init__(self, d, name):
        self.path = pathlib.Path(d) / f"{name}.pem"
        subprocess.run([sr.openssl(), "genpkey", "-algorithm", "ed25519", "-out", str(self.path)], check=True,
                       capture_output=True)
        self.pk = sr.public_key(self.path)
        self.keynum = sr.key_id(self.pk)
        self.id = self.keynum.hex()

    def trusted(self):
        return {self.id: self.pk.hex()}

    def sign(self, data):
        return sr.sig_text(self.keynum, sr.sign_bytes(self.path, data))


def make_dist(d, tweak=None):
    """A release directory like CI's dist/: bin/ + audio/ files, MD5SUMS, SHA256SUMS. Returns the asset names."""
    d = pathlib.Path(d)
    d.mkdir(parents=True, exist_ok=True)
    for n in BIN_ASSETS:
        if (ROOT / "bin" / n).is_file():
            shutil.copy(ROOT / "bin" / n, d / n)
        elif (ROOT / "build" / n).is_file():          # the Linux SIM is only committed in bin/ (CI builds it)
            shutil.copy(ROOT / "build" / n, d / n)
    for n in AUDIO_ASSETS:
        shutil.copy(ROOT / "audio" / n, d / n)
    files = [n for n in BIN_ASSETS + AUDIO_ASSETS if (d / n).is_file()]
    if tweak:
        tweak(d)
    (d / "MD5SUMS").write_text("".join(f"{hashlib.md5((d / n).read_bytes()).hexdigest()}  {n}\n" for n in files))
    (d / "SHA256SUMS").write_text("".join(f"{hashlib.sha256((d / n).read_bytes()).hexdigest()}  {n}\n" for n in files))
    return files + ["MD5SUMS", "SHA256SUMS"]


def signed_release(d, key, seq=5, version=None, commit="0" * 40, released="2026-11-23"):
    """dist + manifest.json + manifest.sig signed with the TEST key. Returns (manifest bytes, asset names)."""
    assets = make_dist(d)
    data = sr.build_manifest(d, version or mp.VERSION, commit, released, assets, seq)
    (pathlib.Path(d) / "manifest.json").write_bytes(data)
    (pathlib.Path(d) / "manifest.sig").write_text(key.sign(data))
    return data, assets
