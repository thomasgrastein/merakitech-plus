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
not in this repository.

**Draft → sign → publish** (from the first release after the security hardening; [release-process.md](release-process.md)):
CI only builds a *draft* release, which is invisible to the public and has no git tag yet. The maintainer signs the
draft and publishes it in the same step (§5), so no unsigned release is ever public. With immutable releases enabled
on the repository, a published release — its assets and its tag — cannot be changed or deleted afterwards. (v6.23 and
v6.24 were published first and signed 17-52 minutes later under the old flow.)

**Signed by default** (installer, same point in time): `./install.sh` refuses files that are not covered by a valid
signed manifest (§4). `--allow-unsigned` is the explicit escape hatch for development builds.

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
| Public key | `keys/merakiplus-release.pub`, `TRUSTED_KEYS` in `tools/merakiplus.py`, `src/merakiplus_upd_keys.h` | `tests/hygiene.py` fails if the three differ, and if anything in the tree looks like a private key. All three are trust anchors owned in `.github/CODEOWNERS`. |
| Key pin | `~/.merakiplus/trusted-key-pin` on the maintainer's Mac — **outside the repository** | `<key id> <public key hex>` per line, written by `tools/sign_release.py pin-keys` from the *private* key (not from repository files). `sign` compares the three key copies **at the commit being signed** with it and refuses any difference (a key added or replaced by a merged PR) unless `--accept-key-change`. A PR cannot change this file. |
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
| Companion installer (computer) | `tools/merakiplus.py` (pure-Python RFC 8032 Ed25519, standard library only — the standalone `install.sh` fetches just this one file, from the latest release tag) | every `install`: the signed manifest of `v<VERSION>` from the `--bin-dir` directory if it holds one, else from the GitHub release; every file to be installed must be listed with the same size + SHA-256 | abort **before anything on the display changes** (signed by default) |
| Release workflow (CI) | — | never changes an existing release (draft or published) | the run reports it and stops |
| Maintainer | `tools/sign_release.py` | after signing, before and after the upload | not uploaded / investigate |
| Display (phase 3) | `src/merakiplus_upd.c` + `src/merakiplus_crypto.zig` (Zig standard library Ed25519 + SHA-256), static ARMv7, ~96 KB | every on-device download and every install that stages it | nothing is staged |

The installer also records `max_seq` on the display and refuses older signed releases (§2).

**Signed by default.** No signed manifest (offline computer, a development build, a draft that is not published yet),
or files that differ from the signed manifest of `v<VERSION>` (a development checkout between releases): the
installer **stops before anything on the display changes**. `--allow-unsigned` is the explicit escape hatch: it
installs such files with a loud `UNSIGNED install` warning, checked against the pinned SHA-256 table only. It is
refused on a display that already accepted a signed release (`max_seq` recorded) unless `--allow-downgrade` is given
as well, and it never excuses a bad signature, an unknown key or an older `seq`. `--require-signed` is still accepted
(it is the default now). Downloads come from the release tag `v<VERSION>`; `--ref`/`MERAKIPLUS_REF` accept only release
tags (`vX.Y[.Z]`) and full 40-character commit SHAs — never a branch.

**Bootstrap — the one thing a signature cannot fix.** The installer verifies files with the key *it* carries. A
first-time user who downloads the installer has to trust where it came from: GitHub and this repository (TLS). The
standalone `install.sh` therefore fetches the installer from the **latest published release tag**, not from a branch,
and a git clone should be checked out at a release tag (README). The key id is listed here and in the release notes;
cross-check it with a copy you obtained earlier (an older clone keeps its own pins and key and is not affected by a
later change to the repository).

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

## 5. Signing runbook (maintainer) — draft → sign → publish

Prerequisites on the maintainer's computer: a clone of this repository, `gh` logged in, OpenSSL ≥ 3
(`brew install openssl@3`), and the **official** zig 0.17.1 tarball unpacked somewhere (Homebrew's zig differs in the
ELF `.comment`, see [bin/README.md](../bin/README.md)). Once: `python3 tools/sign_release.py pin-keys` (asks the key
passphrase; writes `~/.merakiplus/trusted-key-pin` from the private key — compare the key id with §1).

Run the signing tool from a checkout you trust, **not** from a freshly pulled `main` you have not read: it imports the
verifier from that checkout and runs the release commit's `build.sh` in a worktree on this computer, next to the key.
A dedicated clone that you update only after reading the diff (or a separate macOS user without access to
`~/.merakiplus/` for the rebuild) keeps a hostile merge away from the key.

1. The release PR (version bump) is merged into `main` (release-process.md). `release.yml` builds and tests it and
   creates a **draft** release `vX.Y` with the unsigned assets and `manifest.unsigned.json`. Wait until it is green.
2. Sign and publish:

   ```sh
   git fetch origin
   ./build.sh host                       # also builds build/merakiplus_upd_host, used as a second verifier
   python3 tools/sign_release.py sign vX.Y --zig-dir ~/zig-aarch64-macos-0.17.1 --upload
   ```

   The tool, in this order, stops at the first problem:
   1. refuses to run in CI; downloads every asset of the (draft) release;
   2. re-verifies every earlier signed manifest and takes `seq = highest + 1` (the version must be newer than the
      last signed one; an already signed draft needs `--resign`, which keeps its `seq` — a published release is
      immutable and cannot be re-signed);
   3. resolves the commit: the tag's commit if the tag exists, else the draft's target, which must be a full SHA on
      `origin/main` whose `VERSION` equals the tag;
   4. **review**: the commit list and diffstat since the last signed release, with every changed **trust anchor**
      (`.github/`, `install.sh`, `revert.sh`, `build.sh`, `tools/merakiplus.py`, `tools/sign_release.py`, `keys/`,
      `src/merakiplus_upd_keys.h`, `tests/hygiene.py`, `tests/check_bins.py`, `bin/`) flagged — read those diffs
      line by line; then the three key copies at that commit against your key pin (any difference: refused unless
      `--accept-key-change`); then **you type the first 12 characters of the commit** to confirm what you reviewed;
   5. rebuilds that commit in a temporary `git worktree` with the official zig and its own cache;
   6. compares byte for byte: every reproducible binary (the list in `tests/check_bins.py`, which includes the
      deterministically trimmed `grab_shim.so` from this change on) = fresh build = committed `bin/` = downloaded asset;
      the macOS SIM (Mach-O, not reproducible) = committed `bin/`; the WAVs = `audio/` (and `tools/gen_audio.py --check`
      ran in the build); `MD5SUMS` / `SHA256SUMS` = recomputed from the downloads. Any unknown asset stops it;
   7. writes `manifest.json`; CI's `manifest.unsigned.json` must be identical in assets / release / platform / commit;
   8. signs with OpenSSL (**it asks for the passphrase on the terminal**; type it, nothing is stored);
   9. verifies the signature with the keys pinned in `tools/merakiplus.py` (not with the key file) and with the device
      verifier's host build;
   10. with `--upload`: asks, uploads `manifest.json` + `manifest.sig` to the draft, downloads it again and runs
       `verify-release`; then asks once more and **publishes** the draft (`--latest` for a stable version). Publishing
       creates the tag at the signed commit; the tool checks that the tag points there and verifies the published
       release once more. `--no-publish` stops after the upload.

   Without `--upload` the files stay in `build/sign-<tag>/out/` for a manual `gh release upload` + `gh release edit
   <tag> --draft=false`.
3. Done. Optionally keep the signed pair as a test fixture: `tests/fixtures/release-<tag>/` (the tests verify every
   fixture with the production verifiers).

Never sign when step 6 reports a mismatch: it means the release binaries are not what the commit builds to (wrong
toolchain at release time, a manual upload, or a compromise). Investigate first. Rebuilding a draft: delete it
(`gh release delete vX.Y` — a draft has no tag) and run the release workflow on `main` (*Run workflow*).

New key (only for a rotation or after a loss): `python3 tools/sign_release.py keygen` → asks the passphrase twice,
writes `~/.merakiplus/signing_ed25519.pem` (+ `.pub`), refuses to overwrite a key or to write inside the repository,
proves the key signs. Then pin the public key (§3), run `pin-keys` again, and make the backups. A rotation release is
signed with `--accept-key-change` once.

---

## 6. Threat model

| Attack | Result once the display verifies (phase 3) | Today (companion installer verifies) |
|---|---|---|
| Compromised CI / leaked `GITHUB_TOKEN` / malicious workflow change | can publish binaries and a manifest, but cannot sign → refused | CI only has a write token in the job that creates the draft (no repository code runs there); a draft is not public and an unsigned release is refused by the installer (signed by default) |
| Malicious PR merged into `main` (incl. new pins, binaries, or a key added to the three key copies) | not signed unless the maintainer signs it | not installed: users install release tags, and an unsigned or mismatching release is refused. At signing time the tool shows every commit and trust-anchor change since the last signed release and refuses a key change that is not in the maintainer's out-of-repository key pin |
| Hijacked GitHub account | same as above: no key on GitHub | same; but the attacker can also change `tools/merakiplus.py` (incl. its keys) and publish a release, so a *freshly downloaded* installer is not a trust anchor — an installer from an earlier, known-good clone is. Immutable releases stop a published signed release from being altered or stripped of its signature |
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
- `tests/test_upd.py`, *signing hardening* — trust-anchor classification, key pin (missing → warning; added /
  replaced / one-copy key change → refused; `--accept-key-change`; malformed pin; never inside the repository), review
  since the last signed tag (commits, diffstat, trust-anchor warning) on a throwaway git repository, draft commit
  resolution (target on `origin/main`, VERSION match, branch names / unmerged commits / tag mismatch refused),
  commit confirmation, no signing in CI, and the shape of `release.yml` (main only, read-only token, write only in the
  draft job, no asset replacement or deletion, cold zig cache).
- `tests/test_installer.py`, scenario *2: signed release manifest* — signed by default (no manifest → refused),
  tampered / wrong key / missing signature / a file not in the manifest refused (also with `--allow-unsigned`),
  `max_seq` recorded, re-install, downgrade refused and `--allow-downgrade`, unsigned install on a display with
  `max_seq` refused unless `--allow-unsigned --allow-downgrade`, `--require-signed` still accepted, revert. Scenario
  *2b*: development builds with `--allow-unsigned`, `--ref` validation, default ref. *install.sh bootstrap*: the
  standalone installer resolves the latest published release tag and fetches from it, `MERAKIPLUS_REF` validation
  (fake `curl`, no network).
- `./build.sh` runs `merakiplus_upd_host --selftest`; `tests/hygiene.py` checks the pinned keys.
