/* merakiplus_upd_keys.h — the Ed25519 public keys that sign Meraki+ release manifests (docs/signing.md).
 * Same keys as keys/merakiplus-release.pub and TRUSTED_KEYS in tools/merakiplus.py (tests/hygiene.py checks).
 * {key id = hex of the 8-byte signify key number, public key = 32 bytes hex} */
#define MERAKIPLUS_TRUSTED_KEYS \
    {"26f673cde8584ba0", "f16f17c0b8576b95931c0593b924c81748ad10668dfac9fecfa8e7404aa0bc1d"},   /* primary, generated 2026-11-23 */
