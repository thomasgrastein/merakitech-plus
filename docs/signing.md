# Signed releases — manifest, key custody, signing runbook

Status: **implemented 2026-11-23** (OTA phase 2, see [ota-plan.md](ota-plan.md#phase-2--implemented-2026-11-23)).
**v6.23 is the first signed release** (seq 1). Releases before v6.23 are not signed.

Every Meraki+ release can carry two extra assets:

| Asset | What | Made by |
|---|---|---|
| `manifest.json` | every release asset with size + SHA-256, a monotonic `seq`, the tag and commit | the maintainer, offline |
| `manifest.sig` | Ed25519 signature over the exact bytes of `manifest.json` | the maintainer, offline |
| `manifest.unsigned.json` | the same manifest without `seq`, computed by CI — for comparison only, **never trusted** | CI (from v6.24) |

`MD5SUMS` and `SHA256SUMS` stay as before (older scripts, manual checks). They are listed in the signed manifest,
so a verified manifest also vouches for them.

**CI cannot sign.** The private key never leaves the maintainer's computer; it is not in GitHub, not in CI secrets,
not in this repository. Until the maintainer has signed a release, it is simply unsigned.

---

## 1. Checking a download yourself

```sh
gh release download v6.23 -R thomasgrastein/merakitech-plus -D v6.23     # or download the assets in a browser
python3 tools/merakiplus.py verify-release v6.23                         # --partial if you fetched only some files
```

`verify-release` checks the signature against the public key pinned in `tools/merakiplus.py`, the manifest format,
and the size + SHA-256 of every asset in the directory. `./install.sh` does the same automatically (§4).

The public key is [`keys/merakiplus-release.pub`](../keys/merakiplus-release.pub):

| Key | Key id | Ed25519 public key (hex) |
|---|---|---|
| primary (2026-11-23) | `26f673cde8584ba0` | `f16f17c0b8576b95931c0593b924c81748ad10668dfac9fecfa8e7404aa0bc1d` |

The files use the same format as OpenBSD `signify` (`untrusted comment:` line + base64 of `"Ed"` ‖ 8-byte key
number ‖ key or signature); the key number is the first 8 bytes of SHA-256(public key).

---

## 2. The manifest (format 1)

Canonical JSON: sorted keys, one-space indent, `", "`/`": "` separators, ASCII only, one final newline — exactly
`json.dumps(obj, sort_keys=True, indent=1, ensure_ascii=True, separators=(",", ": ")) + "\n"`. Integers only (no
floats), no string escapes at all (every string is printable ASCII without `"` or `\`), at most 64 KiB. Both
verifiers reject anything that is not byte-identical to its canonical form, duplicate keys included, so the two
implementations can never read the same signed bytes differently.

```json
{
 "assets": [ {"name": "MD5SUMS", "sha256": "…", "size": 556}, … every release asset, sorted by name … ],
 "base_url": "https://github.com/thomasgrastein/merakitech-plus/releases/download/v6.23/",
 "channel": "stable",
 "commit": "<40-hex commit of the tag>",
 "format": 1,
 "platform": [ {"name": "grab_shim.so", "src": "grab_shim.so", "sha256": "…", "size": 852},
               {"name": "launcher", "src": "merakiplus_boot", …}, {"name": "switcher", "src": "switcher", …} ],
 "product": "merakiplus",
 "release": [ {"mode": "755", "name": "demo_v9", "src": "merakiplus", "sha256": "…", "size": 320680},
              {"mode": "755", "name": "merakiplus_snd", …}, {"mode": "644", "name": "audio/start.wav", …}, … ],
 "released": "2026-11-23",
 "seq": 1,
 "tag": "v6.23",
 "version": "6.23"
}
```

| Field | Rule |
|---|---|
| `format` | `1`. An incompatible change bumps it; old verifiers then refuse the manifest (safe). |
| `seq` | 1 … 2³¹−1, strictly increasing over signed releases in version order. v6.23 = 1. Absent in `manifest.unsigned.json`. |
| `version`, `tag` | `tag` = `v` + `version`; `version` = `N.N` or `N.N.N`. |
| `base_url` | must be this repository's download URL for exactly this tag (no mixing of releases). |
| `assets` | every release asset except the `manifest*` files: unique safe file names, size, SHA-256. |
| `release` | what a release directory `rel/<x>/` contains (switches atomically, [ota-plan.md §5.2](ota-plan.md#52-layout)): name inside the directory, source asset, mode. Must match its asset's size + SHA-256. |
| `platform` | fixed-path components updated separately (launcher, gesture switcher + shim). Same rule. |
| other keys | ignored (forward compatible), but still covered by the signature. |

Anti-downgrade: the display stores the highest accepted `seq` as `max_seq=` in `ota/state` (the launcher keeps
unknown keys). A release with `seq < max_seq` is refused (**OLDER VERSION REFUSED**); `seq == max_seq` is a
re-install and fine. `--allow-downgrade` installs an older release on purpose and resets `max_seq` to it.
Rolling back to the `previous` release that is already on the display is not a downgrade (local, verified when it
was installed).

---

## 3. Key custody (maintainer)

Decision [ota-plan.md §13.2](ota-plan.md#13-decisions-resolved-by-the-maintainer): passphrase-protected file, no
hardware token.

| What | Where | Notes |
|---|---|---|
| Private key | `~/.merakiplus/signing_ed25519.pem` on the maintainer's Mac | PKCS#8, **scrypt (N = 2¹⁴, r = 8, p = 1) + AES-256-CBC** (OpenSSL 3). Mode 600, directory 700. Outside the repository and any synced work directory. The plaintext key never touched a disk (generated in a pipe). |
| Public key | `keys/merakiplus-release.pub`, `TRUSTED_KEYS` in `tools/merakiplus.py`, `src/merakiplus_upd_keys.h` | `tests/hygiene.py` fails if the three differ, and if anything in the tree looks like a private key. |
| Warm backup | a dedicated folder in the maintainer's iCloud Drive | **The encrypted key file only** (+ `.pub`). Survives the loss of the Mac. |
| Cold backup (recommended) | an offline USB stick in a drawer | Copy the same encrypted `.pem` (and the `.pub`). Check it once a year. |
| Passphrase | a password manager, or on paper kept offline | **Never next to the key**: not in iCloud Drive, not in the repository, not in notes, chats, logs or shell history. Key file and passphrase are only dangerous together. |

Backup steps (once, and after any key change):

```sh
cp -p ~/.merakiplus/signing_ed25519.pem ~/.merakiplus/signing_ed25519.pub /Volumes/<usb>/merakiplus-signing/
openssl pkey -in /Volumes/<usb>/merakiplus-signing/signing_ed25519.pem -noout   # asks the passphrase; no output = OK
```

### Lost or compromised key

- **Lost Mac, key file intact elsewhere:** restore the `.pem` from a backup into `~/.merakiplus/` (mode 600). Nothing
  else changes.
- **Passphrase forgotten / every copy lost:** the key is gone. Generate a new key (`tools/sign_release.py keygen`),
  pin its public key in the three places, release a new version. Installers from that commit on trust the new key;
  displays (phase 3) learn it from a release installed by `install.sh`.
- **Key (and passphrase) possibly stolen:** generate a new key, **remove** the old key id from the three places,
  release, and publish an advisory (SECURITY.md). Releases signed with the old key are then refused by new installers.
  Until displays verify on their own (phase 3), the companion installer of the new commit is the gate.
- **Rotation without an incident:** add the new key next to the old one, release (signed with the old key), then sign
  the next releases with the new key, then remove the old key in a later release. The verifiers accept a list of keys.

The plan's separate *cold backup key* ([ota-plan.md §6.1](ota-plan.md#61-signatures)) is not generated: the
maintainer chose backup copies of the one encrypted key; rotation as above covers the rest.

---

## 4. Who verifies what

| Where | Code | When | What happens on failure |
|---|---|---|---|
| Companion installer (computer) | `tools/merakiplus.py` (pure-Python RFC 8032 Ed25519, standard library only — the standalone `install.sh` fetches just this one file) | every `install`: the signed manifest of `v<VERSION>` from the `--bin-dir` directory if it holds one, else from the GitHub release; every file to be installed must be listed with the same size + SHA-256 | abort **before anything on the display changes** |
| Release workflow (CI) | `tools/merakiplus.py verify-release` | a re-run for an already signed release | the job fails; assets under a signed manifest are never replaced |
| Maintainer | `tools/sign_release.py` | after signing, before and after the upload | not uploaded / investigate |
| Display (phase 3) | `src/merakiplus_upd.c` + `src/merakiplus_crypto.zig` (Zig standard library Ed25519 + SHA-256), static ARMv7, ~96 KB | every on-device download and every install that stages it | nothing is staged |

The installer also records `max_seq` on the display and refuses older signed releases (§2).

**No signed manifest** (offline computer, a development build, a release not signed yet): the installer warns and
falls back to its pinned SHA-256 table, as before. `--require-signed` turns that warning into an abort.

The device verifier is **built and tested but not shipped yet** (no release contains it; nothing on the display runs
it). It ships with phase 3 or with the first release that stages it into `rel/<x>/` — that is a shipped-binary change
and needs a release decision plus one hardware check. Its interface:

```
merakiplus_upd verify manifest.json manifest.sig [--max-seq N] [--version V] [--assets DIR] [--release-dir DIR]
  -> "ok version=6.23 seq=1 key=26f673cde8584ba0 files=13"            exit 0
  -> "fail <signature|format|older|asset|io> <reason>"                 exit 3|4|5|6|7
merakiplus_upd sha256 FILE…     merakiplus_upd keys     merakiplus_upd --selftest     merakiplus_upd --version
```

The signature is checked before the manifest is parsed. The build is reproducible (same bytes from different
directories and caches with the official zig 0.17.1).

---

## 5. Signing runbook (maintainer)

Prerequisites on the maintainer's computer: a clone of this repository, `gh` logged in, OpenSSL ≥ 3
(`brew install openssl@3`), and the **official** zig 0.17.1 tarball unpacked somewhere (Homebrew's zig differs in the
ELF `.comment`, see [bin/README.md](../bin/README.md)).

1. CI publishes the release as usual (push of a newer `VERSION` to `main`, a tag, or *Run workflow*). Wait until the
   release workflow is green.
2. Sign:

   ```sh
   git pull
   ./build.sh host                       # also builds build/merakiplus_upd_host, used as a second verifier
   python3 tools/sign_release.py sign v6.24 --zig-dir ~/zig-aarch64-macos-0.17.1 --upload
   ```

   The tool, in this order, stops at the first problem:
   1. downloads every asset of the release;
   2. re-verifies every earlier signed manifest and takes `seq = highest + 1` (the version must be newer than the
      last signed one; an already signed release needs `--resign`, which keeps its `seq`);
   3. rebuilds the **tagged** source in a temporary `git worktree` with the official zig and its own cache;
   4. compares byte for byte: every reproducible binary (the list in `tests/check_bins.py`) = fresh build = committed
      `bin/` at the tag = downloaded asset; `grab_shim.so` and the macOS SIM (not reproducible by design) = committed
      `bin/` at the tag; the WAVs = `audio/` at the tag (and `tools/gen_audio.py --check` ran in the build);
      `MD5SUMS` / `SHA256SUMS` = recomputed from the downloads. Any unknown asset stops it;
   5. writes `manifest.json`; if CI published `manifest.unsigned.json`, the assets / release / platform sections must
      be identical;
   6. signs with OpenSSL (**it asks for the passphrase on the terminal**; type it, nothing is stored);
   7. verifies the signature with the keys pinned in `tools/merakiplus.py` (not with the key file) and with the device
      verifier's host build;
   8. with `--upload`: asks once more, uploads `manifest.json` + `manifest.sig`, downloads the release again and runs
      `verify-release` on it.

   Without `--upload` the files stay in `build/sign-<tag>/out/` for a manual `gh release upload`.
3. Done. Optionally keep the signed pair as a test fixture: `tests/fixtures/release-<tag>/` (the tests verify every
   fixture with the production verifiers).

Never sign when step 4 reports a mismatch: it means the published binaries are not what the tagged source builds to
(wrong toolchain at release time, a manual upload, or a compromise). Investigate first.

New key (only for a rotation or after a loss): `python3 tools/sign_release.py keygen` → asks the passphrase twice,
writes `~/.merakiplus/signing_ed25519.pem` (+ `.pub`), refuses to overwrite a key or to write inside the repository,
proves the key signs. Then pin the public key (§3) and make the backups.

---

## 6. Threat model

| Attack | Result once the display verifies (phase 3) | Today (companion installer verifies) |
|---|---|---|
| Compromised CI / leaked `GITHUB_TOKEN` / malicious workflow change | can publish binaries and a manifest, but cannot sign → refused | refused by an installer from a trusted checkout |
| Hijacked GitHub account | same as above: no key on GitHub | same; but the attacker can also change `tools/merakiplus.py` on `main` (incl. its keys), so a *freshly downloaded* installer is not a trust anchor — an installer from an earlier, known-good clone is |
| Tampered download / CDN / TLS-intercepting proxy | hash or signature mismatch → refused | refused |
| Malicious pull request merged into a release | the maintainer's local rebuild compares the binaries with the tagged source, not the intent of the code: review stays essential | same |
| Older, validly signed release with known bugs | `seq < max_seq` → OLDER VERSION REFUSED | refused (`max_seq` on the display) unless `--allow-downgrade` |
| Stolen Mac or iCloud account | the key file alone is useless without the passphrase (scrypt + AES-256) | same |
| Stolen key **and** passphrase | can sign → rotate the key (§3) | same |
| Anyone on the machine's LAN | the vendor's root telnet is open — signatures do not change that; isolate the machine (README *Security and OTA*) | same |

What the signature proves: these exact files were approved by the holder of the key after a local rebuild of the
tagged source. It does not prove the code is good.

---

## 7. Tests

- `tests/test_upd.py` — RFC 8032 vectors (incl. a non-canonical S), sign/verify round trip with a throwaway test key,
  tampered manifest / signature / asset, wrong key, forged key id, missing / empty / malformed signature, downgrade,
  duplicate keys, floats, escapes, whitespace, key order, size limit, path traversal, section/asset mismatch, the
  Python and C verifiers agreeing on every case, `--release-dir`, `keygen` (encrypted, mode 600, refuses overwrite /
  a path in the repository, wrong passphrase), the release workflow's manifest command (dry run on a CI-like `dist/`),
  the published v6.23 manifest with the production verifiers, and that production builds trust only the pinned key.
  `--arm` runs the same cases with the static ARM build in an arm32v7 container.
- `tests/test_installer.py`, scenario *2: signed release manifest* — verification before any change on the display,
  tampered / wrong key / missing signature / a file not in the manifest refused, `max_seq` recorded, re-install,
  downgrade refused and `--allow-downgrade`, unsigned fallback, `--require-signed`, revert.
- `./build.sh` runs `merakiplus_upd_host --selftest`; `tests/hygiene.py` checks the pinned keys.
