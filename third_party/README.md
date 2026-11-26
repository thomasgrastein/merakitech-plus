# third_party/ — source code by others (third-party notices)

Only permissively licensed **source code** is kept here; it is compiled into our own binaries by `./build.sh`.
No vendor firmware, libraries or binaries are included in this repository.

## bearssl/ — BearSSL (TLS for the on-device updater)

| | |
|---|---|
| What | BearSSL, a small TLS library by Thomas Pornin — the TLS 1.2 client of `merakiplus_upd` (src/merakiplus_upd_net.c, OTA phase 3) |
| Upstream | https://www.bearssl.org/ (git: `https://www.bearssl.org/git/BearSSL`) |
| Version | git master, commit `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f` (2026-04-06) |
| License | MIT — [bearssl/LICENSE.txt](bearssl/LICENSE.txt) (copyright notice kept unchanged) |
| Files | `inc/` and `src/` (incl. the T0 sources `*.t0` of the generated handshake code), `LICENSE.txt`, `README.txt` — **unmodified**; upstream's tests, tools, samples and build files are not included |
| Used how | `build.sh` compiles every `src/**/*.c` file (sorted, so the link is reproducible) into `merakiplus_upd`; the linker keeps only what the updater's TLS profile uses (TLS 1.2, ECDHE + AES-GCM / ChaCha20-Poly1305, RSA + ECDSA certificate checks) |

**Enforced:** `tests/hygiene.py` computes the git tree hashes of `src/` and `inc/` (and the blob hashes of `LICENSE.txt`,
`README.txt`) from the files on disk and fails unless they equal upstream's at this commit
(`git -C BearSSL rev-parse 7bea48e:src 7bea48e:inc` → `9893f48d…`, `c2ce436e…`). `.gitattributes` marks this directory
`linguist-vendored`, so GitHub folds it in pull-request diffs. Its license notice (MIT) is in `THIRD-PARTY-NOTICES`
(a release asset, and built into `merakiplus_upd`: `merakiplus_upd notices`).

To update: replace `inc/` and `src/` with a newer upstream commit (no local changes), update the commit above and the
hashes in `tests/hygiene.py` (`BEARSSL_COMMIT`, `BEARSSL_LOCK`), run
`./build.sh`, `python3 tests/test_ota_fetch.py` (incl. `--arm`) and a live `merakiplus_upd_host check`, and rebuild `bin/`
with the official zig.

The Ed25519 and SHA-256 code of the updater is not here: it comes from the Zig standard library
(src/merakiplus_crypto.zig), i.e. from the toolchain that builds every Meraki+ binary.
