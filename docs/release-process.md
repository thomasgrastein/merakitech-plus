# Release process

How a change becomes a published (and signed) Meraki+ release. The contribution rules themselves are in
[CONTRIBUTING.md](../CONTRIBUTING.md#how-changes-reach-main).

```
branch ── pull request ── merge-gate green ── maintainer merges ── release.yml publishes ── maintainer signs
                          (+ maintainer approval      (version bump      (unsigned assets,        (offline, seq N,
                           for community PRs)          on main)           manifest.unsigned.json)  manifest.json + .sig)
```

## 1. Pull request

Every change to `main` is a pull request. The required check is **`merge-gate`**
([`.github/workflows/ci.yml`](../.github/workflows/ci.yml)): the quick gate for every PR, plus the full E2E suite
(English + Danish) when `src/`, `sim/`, `boot/`, `build.sh` or `ci.yml` change. `main` must be merged into the
branch (or the branch rebased) whenever `main` has moved on — branch protection requires it to be up to date.

Community PRs also need one approving review from a maintainer. The owner (an administrator) may merge without a
second review; see [CONTRIBUTING.md](../CONTRIBUTING.md#maintainer-and-maintainer-automation).

## 2. The release PR

A release is an ordinary PR that additionally:

- bumps `VERSION` in `tools/merakiplus.py` **and** `MERAKIPLUS_VERSION` in `src/merakiplus.c` (they must agree;
  `release.yml` refuses a mismatch),
- moves the `Unreleased` entries in `CHANGELOG.md` into a `## [X.Y] — date` section (the release notes are taken
  from it by `tools/release_notes.py`),
- for device changes, commits the rebuilt `bin/` with updated checksums (official zig 0.17.1, see CONTRIBUTING.md).

**Merging it is the maintainer's release approval.** Nothing is published before the merge, and a PR that bumps
the version must not be merged by anyone else.

## 3. Publishing (automatic)

On the push to `main` that the merge creates, [`release.yml`](../.github/workflows/release.yml):

1. checks that `VERSION` is stable and newer than the latest published release (otherwise it does nothing),
2. re-runs the build + test gate and the `bin/` drift check,
3. creates the release `vX.Y` with the committed `bin/` builds, the sounds, `MD5SUMS`, `SHA256SUMS` and
   `manifest.unsigned.json`.

A re-run for the same version updates that release instead of creating a duplicate, and never replaces assets
under an existing signed manifest.

## 4. Signing (maintainer, offline)

CI has no signing key. The maintainer rebuilds the tag, compares it byte for byte with the published assets, signs
the manifest with the next sequence number and uploads `manifest.json` + `manifest.sig` — the runbook is in
[docs/signing.md](signing.md). Until then the release is published but unsigned; the installer reports that.
