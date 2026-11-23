//! merakiplus_crypto.zig — Ed25519 verification and SHA-256 for src/merakiplus_upd.c (OTA phase 2, docs/signing.md).
//!
//! Thin C ABI over the Zig standard library's audited implementations (std.crypto), so the repository carries no
//! third-party crypto code. Built with `zig build-obj` and linked into the C program by build.sh.
const std = @import("std");
const Ed25519 = std.crypto.sign.Ed25519;
const Sha256 = std.crypto.hash.sha2.Sha256;

/// 0 = valid signature; -1 = unusable public key; -2 = signature does not verify.
export fn mp_ed25519_verify(sig: *const [64]u8, msg: [*]const u8, len: usize, pk: *const [32]u8) c_int {
    const key = Ed25519.PublicKey.fromBytes(pk.*) catch return -1;
    Ed25519.Signature.fromBytes(sig.*).verify(msg[0..len], key) catch return -2;
    return 0;
}

/// Streaming SHA-256 over one module-level context (the tool is single-threaded and hashes one file at a time).
var ctx: Sha256 = undefined;
export fn mp_sha256_init() void {
    ctx = Sha256.init(.{});
}
export fn mp_sha256_update(p: [*]const u8, n: usize) void {
    ctx.update(p[0..n]);
}
export fn mp_sha256_final(out: *[32]u8) void {
    ctx.final(out);
}
