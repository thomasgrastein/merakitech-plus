# Release process

How a change becomes a published, signed Meraki+ release. The contribution rules themselves are in
[CONTRIBUTING.md](../CONTRIBUTING.md#how-changes-reach-main); the signing runbook is in [signing.md](signing.md#5-signing-runbook-maintainer--draft--sign--publish).

```
branch ── pull request ── merge-gate green ── owner merges ── release.yml: DRAFT ── maintainer: review + sign ── PUBLISH
                          (+ code-owner review   (release PR:     (build + test gate,   (offline: diff since last   (tag created,
                           for community PRs)     version bump)    unsigned assets,      signed tag, key pin,        immutable)
                                                                   not public, no tag)   rebuild, seq N)
```

No unsigned release is ever public, and the installer refuses unsigned files by default — so a merge into `main` is
not, on its own, something users install. What users install is a **signed release tag**.

## 1. Pull request

Every change to `main` is a pull request. The required check is **`merge-gate`**
([`.github/workflows/ci.yml`](../.github/workflows/ci.yml)): the quick gate for every PR, plus the full E2E suite
(English + Danish) when `src/`, `sim/`, `boot/`, `build.sh` or `ci.yml` change. `main` must be merged into the
branch (or the branch rebased) whenever `main` has moved on — branch protection requires it to be up to date.

A separate weekly workflow, [`live-canary.yml`](../.github/workflows/live-canary.yml), runs the on-device updater
against the real GitHub with only the pinned roots (`tests/live_canary.py`). It is not part of `merge-gate` (it needs
github.com); a failure means a CA, TLS or URL-format change that must be answered with a release while the machines can
still fetch it ([keys/tls/README.md](../keys/tls/README.md)).

Community PRs also need one approving review from a maintainer, and from the code owner when they touch a trust anchor
(`.github/CODEOWNERS`); a new push dismisses an earlier approval. The owner (an administrator) merges their own and
their agents' PRs with `gh pr merge --admin` once `merge-gate` is green; see
[CONTRIBUTING.md](../CONTRIBUTING.md#maintainer-and-maintainer-automation).

## 2. Version discipline

A feature, fix or tooling PR **never claims a release version**. `VERSION` in `tools/merakiplus.py` and
`MERAKIPLUS_VERSION` in `src/merakiplus.c` stay at the last released version, and its `CHANGELOG.md` entry goes under
`## [Unreleased]` — with a `### On the machine` subsection of 1–4 ultrashort bullets (≤ 36 ASCII characters; what
an owner sees on the display before installing; see §3). If such a PR changes a shipped binary, `bin/` is rebuilt with the unchanged version string (so the
drift check stays green); the release PR assigns the version later. Between releases the commits on `main` are
therefore not installable without `--allow-unsigned` — they are development builds.

## 3. The release PR

A release is an ordinary PR that additionally:

- bumps `VERSION` in `tools/merakiplus.py` **and** `MERAKIPLUS_VERSION` in `src/merakiplus.c` (they must agree;
  `release.yml` refuses a mismatch),
- moves the `Unreleased` entries in `CHANGELOG.md` into a `## [X.Y] — date` section (the release notes are taken
  from it by `tools/release_notes.py`) and merges their **`### On the machine`** bullets into the section's own one:
  **1–4 bullets, plain ASCII, at most 36 characters each** — the ultrashort notes the display shows in
  SYSTEM › UPDATES before INSTALL. They are published as the release asset `notes.txt` (covered by the signed
  manifest). `tests/hygiene.py` (CI), `release.yml` (`release_notes.py --device`) and `sign_release.py` refuse a
  release without valid ones,
- for device changes, commits the rebuilt `bin/` with updated checksums (official zig 0.17.1, see CONTRIBUTING.md).

**Merging it is the maintainer's approval to build the release; publishing it is the maintainer's signature.** A PR
that bumps the version must not be merged by anyone else.

## 4. The draft (automatic)

On the push to `main` that the merge creates, [`release.yml`](../.github/workflows/release.yml):

1. checks that `VERSION` is stable and newer than the latest published release (otherwise it does nothing),
2. re-runs the build + test gate and the `bin/` drift check from a **cold** zig cache,
3. creates the **draft** release `vX.Y` with the committed `bin/` builds, the sounds, `MD5SUMS`, `SHA256SUMS` and
   `manifest.unsigned.json`, targeting the merge commit. A draft is not public and creates no tag.

CI never changes an existing release: if a draft or a published release for the version exists, the run says so and
stops. To rebuild a draft, delete it (`gh release delete vX.Y`) and start the workflow on `main` (*Run workflow*). The
workflow runs on `main` only; there is no tag trigger, and a manual run on any other branch is refused.

## 5. Sign and publish (maintainer, offline)

CI has no signing key. On the maintainer's computer, `tools/sign_release.py sign vX.Y --upload`:
shows every commit, the diffstat and every **trust-anchor** change since the last signed release; checks the trusted
keys at that commit against the maintainer's key pin outside the repository; asks for the reviewed commit SHA; rebuilds
the commit and compares it byte for byte with the draft's assets; signs the manifest with the next sequence number;
uploads `manifest.json` + `manifest.sig` to the draft; verifies the download; and **publishes** the draft. Publishing
creates the tag at the signed commit. Runbook: [signing.md §5](signing.md#5-signing-runbook-maintainer--draft--sign--publish).

From that moment `./install.sh` installs it (standalone: the latest published release; from a clone: after
`git checkout vX.Y`). With immutable releases on, the published release — assets, signature and tag — can no longer be
changed or deleted.

## 6. Repository settings (owner)

| Setting | Value | Why |
|---|---|---|
| Branch protection `main` | PR required (1 approval, **dismiss stale approvals**, **code-owner review**), required check `merge-gate` (GitHub Actions app), up to date, no force-push / deletion, admins not enforced | every change is a PR; the owner can still merge alone (`--admin`) |
| Tag ruleset `v*` | creation, update, deletion and non-fast-forward restricted; bypass: repository admins | only the owner (by publishing a signed draft) creates release tags; CI cannot |
| Immutable releases | on | a published release cannot be altered, re-uploaded or stripped of its signature |
| Actions: require SHA pinning | on (after this change is on `main`) | an action reference by tag or branch fails the run |
| Actions workflow token | read-only by default; Actions cannot approve PRs | least privilege |
| Fork PR workflows | require approval for first-time contributors (consider: all external contributors) | read-only token, no secrets either way |
| Dependabot | `github-actions`, weekly (`.github/dependabot.yml`) | keeps the SHA pins current, through normal PRs |
| Secrets | none | the signing key is offline; CI never needs a secret |

## 7. CI hardening

What the workflows guarantee — `tests/hygiene.py` fails if one of the first five regresses:

- **Actions pinned to full commit SHAs** (with the tag as a comment); Dependabot proposes updates.
- **No privileged triggers**: no `pull_request_target`, `workflow_run` or similar; fork PRs run `ci.yml` with a
  read-only token and no secrets.
- **Read-only workflow token**; `contents: write` exists exactly once — on `release.yml`'s draft job, which checks out
  nothing and runs no repository code.
- **No persisted credentials**: every `actions/checkout` sets `persist-credentials: false`.
- **Dependabot** for `github-actions`.
- **Cold builds** where binaries are compared: the `bin/` drift check (`ci.yml` quick gate) and the release build use
  `use-cache: false` for `setup-zig`, so no restored zig cache can influence what is compared or released.
- **No expression injection**: event data reaches `run:` scripts only through `env:`.
- **Trust-anchor changes are loud**: `.github/CODEOWNERS` owns the installer, keys, signing tool, gates, workflows and
  `bin/`; CI annotates a PR that touches one (`tests/hygiene.py --trust-anchors`), and the signing tool flags them
  again before a release is signed. This is advisory: a PR controls its own workflow file, so the real gates are the
  review, the signature and the installer's signed-by-default check.

## 8. Credentials for automation

Coding agents work on the maintainer's behalf through PRs only. Recommended: give them a **fine-grained token limited
to this repository** with *Contents: read/write* and *Pull requests: read/write* only — no *Administration*, no
*Workflows* permission, and not the owner's own `gh` login. Such a token cannot bypass branch protection (not an
admin), cannot create `v*` tags (tag ruleset — publishing a draft creates one), cannot sign (the key is offline, and an
unsigned release is refused by the installer anyway), and cannot push workflow changes. Keep the owner token out of
agent sessions; until such a token is in place, agents running with the owner's login are bound by policy only
("agents never merge, never push to `main`, never publish").
