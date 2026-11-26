# keys/tls/ — the pinned TLS roots of the on-device updater

`merakiplus_upd check | install` (docs/ota-plan.md §6.2, phase 3) talks HTTPS to exactly three hosts —
`github.com`, `release-assets.githubusercontent.com` and `objects.githubusercontent.com` — and trusts **only** the
root certificates in this directory. The display has no CA bundle of its own (probe P3, 2026-11-26) and the updater never
uses the vendor's libcurl/OpenSSL. `tools/gen_anchors.py` turns these files into `src/merakiplus_upd_anchors.h`
(subject DN + public key per root, the form BearSSL needs); `tests/hygiene.py` and `./build.sh` fail if the two differ.

TLS only protects the connection. **Authenticity comes from the Ed25519 signature** on `manifest.json`
([docs/signing.md](../../docs/signing.md)). A wrong or stale anchor can therefore stop updates, but it cannot make the
display install something unsigned.

## The roots (2026-11-26)

| File | Root | Valid until | SHA-256 fingerprint | Why |
|---|---|---|---|---|
| `sectigo-public-server-root-e46.crt` | Sectigo Public Server Authentication Root E46 | 2046-03-21 | `C9:0F:26:F0:…:38:0C:53:83` | github.com today: leaf ← *Sectigo Public Server Authentication CA DV E36* ← E46 |
| `usertrust-ecc.crt` | USERTrust ECC Certification Authority | 2038-01-18 | `4F:F4:60:D5:…:D2:A9:AD:7A` | github.com sends E46 cross-signed by this root |
| `sectigo-public-server-root-r46.crt` | Sectigo Public Server Authentication Root R46 | 2046-03-21 | `7B:B6:47:A6:…:EC:38:F2:5A:06` | Sectigo's RSA twin (in case GitHub serves an RSA chain) |
| `usertrust-rsa.crt` | USERTrust RSA Certification Authority | 2038-01-18 | `E7:93:C9:B0:…:C3:D4:CB:D2` | cross-signer of R46 |
| `isrg-root-yr.crt` | ISRG Root YR | 2045-09-02 | `E5:7B:7E:6F:…:EB:F4:A8:6F` | release assets today: `*.githubusercontent.com` leaf ← Let's Encrypt *YR1* ← Root YR |
| `isrg-root-x1.crt` | ISRG Root X1 | 2035-06-04 | `96:BC:EC:06:…:BD:DF:08:C6` | the asset host sends Root YR cross-signed by X1 |
| `isrg-root-ye.crt` | ISRG Root YE | 2045-09-02 | `E1:4F:FC:AD:…:AA:5D:56:66` | Let's Encrypt's ECDSA twin of YR |
| `isrg-root-x2.crt` | ISRG Root X2 | 2040-09-17 | `69:72:9B:8E:…:CB:14:70` | Let's Encrypt's older ECDSA root |
| `digicert-global-root-g2.crt` | DigiCert Global Root G2 | 2038-01-15 | `CB:3C:CB:B7:…:5A:B1:CB:5F` | the release-asset CDN's previous chain (fallback if GitHub switches back) |

Full fingerprints: `openssl x509 -in keys/tls/<file> -noout -fingerprint -sha256`.

Provenance: the Sectigo, USERTrust, ISRG X1/X2 and DigiCert roots were exported from the macOS system root store
(`SystemRootCertificates.keychain`); ISRG Root YR / YE from `https://letsencrypt.org/certs/gen-y/`. Each fingerprint
above was compared with that source, and Root YR's public key equals the key of the cross-signed Root YR the live asset
host sent (verified with `openssl s_client -showcerts`, 2026-11-26). All nine are self-signed roots
(`gen_anchors.py` refuses anything else).

The same set is used for all three hosts, with hostname verification on and only TLS 1.2 ECDHE + AEAD cipher suites
(BearSSL supports no TLS 1.3; both hosts offer TLS 1.2 — checked 2026-11-26).

## The clock floor

Certificates are checked against `max(device clock, MP_CLOCK_FLOOR)` (the `--floor` date in the header, today
2026-11-26); a clock more than 20 years past the floor also falls back to it. The display's RTC survives power-off
(probe P6), so normally the real time is used. With a reset clock the check works only until GitHub's then-current
certificates are newer than the floor — refresh the floor together with the anchors.

## Rotation risk

- **GitHub changes its certificate authority** (it moved github.com from DigiCert to Sectigo, and the asset CDN to
  Let's Encrypt, within the last two years). If the new chain ends in a root that is not listed here, CHECK NOW fails
  with *SECURE CONNECTION FAILED* — no update can be fetched on the device, nothing else breaks.
- **A root here expires or is distrusted** (the earliest here: ISRG Root X1 in 2035, DigiCert G2 / USERTrust in 2038).
- **A new host**: if GitHub serves release assets from another host name, it must be added to `HOSTS` in
  `src/merakiplus_upd_net.c` (redirects to any other host are refused before connecting).

**Early warning:** the weekly workflow `.github/workflows/live-canary.yml` (`tests/live_canary.py`) runs the updater
against the real GitHub with only these anchors (check, a full install, the length of every signed asset URL). A red
run is the signal to refresh this set and release it while the machines can still fetch it.

In each case the **computer install** (`./install.sh`) keeps working — it does not use these anchors — and a release
that ships refreshed anchors restores the on-device path. That is why the set covers the twin and cross-signing roots,
not just the two roots in use today.

## Updating the anchors

1. Inspect the live chains: `openssl s_client -connect github.com:443 -servername github.com -showcerts </dev/null`
   (and the same for `release-assets.githubusercontent.com`, plus the host a release download redirects to:
   `curl -sI https://github.com/thomasgrastein/merakitech-plus/releases/latest/download/manifest.json`).
2. Get the self-signed root of each chain from an independent source (the OS root store, the CA's own site) and compare
   its SHA-256 fingerprint with a second source.
3. Put it here as `<name>.crt` (PEM), remove what is no longer needed, then
   `python3 tools/gen_anchors.py --floor <today>` and rebuild (`./build.sh`; bin/ with the official zig).
4. Update the table above. The PR changes trust anchors: it needs the owner's review (CODEOWNERS), and
   `sign_release.py` lists the change in its pre-signing review.
5. Test against the real GitHub before release: `./build.sh host && python3 tests/live_canary.py` (the same as the
   weekly workflow; dispatch `live-canary.yml` on the branch) must pass.
