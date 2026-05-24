"""Generate initial seed inputs for the ShadowChannels fuzzer.

Each input is:
    [0]       channel hash byte
    [1..4]    from  (little-endian uint32)
    [5..8]    id    (little-endian uint32)
    [9..]     encrypted payload (<= 256 bytes)

Seeds deliberately include:
  - every known preset-derived wire hash (so the fuzzer immediately gets
    past the hash-match gate on entry and stresses the decrypt + pb_decode
    path),
  - a zero-length ciphertext (should be rejected before decrypt),
  - a couple of obviously-garbage hashes that hit no preset (exercises
    the "no match, drop" fall-through),
  - a maximum-length ciphertext (exercises scratch-buffer bounds).

Hash values here match the on-device shadow table computed from
`xor(name) ^ xor(defaultpsk)` in src/mesh/ShadowChannels.cpp. If the
preset display names or the default PSK ever change upstream, these
numbers need regenerating; the device logs the table at boot under
"ShadowCh :   <name>   hash=0x..".
"""

import os
import struct


def emit(name: str, hash_byte: int, nfrom: int, nid: int, payload: bytes) -> None:
    out = bytes([hash_byte]) + struct.pack("<II", nfrom, nid) + payload
    with open(f"seed_{name}", "wb") as f:
        f.write(out)


# ---- header-only (empty payload): forces the size==0 reject path -----
emit("empty", 0x77, 0xa8c85935, 0xdeadbeef, b"")

# ---- every preset-derived hash (as observed on device) ---------------
# Exact values verified in the device boot log; see ShadowChannels.cpp.
preset_hashes = {
    "LongFast":     0x08,
    "LongSlow":     0x0f,
    "VeryLongSlow": 0x37,
    "MediumSlow":   0x18,
    "MediumFast":   0x1f,
    "ShortSlow":    0x77,
    "ShortFast":    0x70,
    "LongMod":      0x6e,
    "ShortTurbo":   0x0e,
    "LongTurbo":    0x76,
    "Default":      0x49,
}
for name, h in preset_hashes.items():
    # 64-byte pseudo-random cipher: plausible broadcast NodeInfo size.
    body = bytes((i ^ h) & 0xff for i in range(64))
    emit(f"match_{name.lower()}", h, 0xa8c85935 + h, 0x10000 + h, body)

# ---- hash that matches no preset entry -------------------------------
emit("nomatch_0xff", 0xff, 0x4b06e19e, 0x01, b"\x00" * 32)
emit("nomatch_0xa5", 0xa5, 0x4b06e19e, 0x02, b"\xff" * 48)

# ---- maximum-length ciphertext with a matching hash ------------------
emit("match_shortslow_max", 0x77, 0xa8c85935, 0xaabbccdd, bytes(range(256)))

# ---- degenerate all-zero ciphertext with a matching hash -------------
# AES-CTR of all-zero plaintext produces the keystream; this seed lets
# the fuzzer flip a few bytes and see whether pb_decode rejects cleanly.
emit("match_longfast_zero", 0x08, 0x00000001, 0x00000001, b"\x00" * 80)

# ---- same hash, distinct (from,id) pairs: the CTR nonce is derived ---
# from (from,id), so this forces the fuzzer to explore different
# keystreams early rather than hammering one nonce.
emit("match_shortslow_alt1", 0x77, 0x12345678, 0x87654321, b"\xa5" * 64)
emit("match_shortslow_alt2", 0x77, 0x00000000, 0xffffffff, b"\x5a" * 64)
