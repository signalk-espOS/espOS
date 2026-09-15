# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""SRP-6a client matching ESP-IDF's protocomm security2 (esp_srp.c).

Verified against the implementation, not guessed:
  * group: RFC 5054 3072-bit, g = 5
  * hash:  SHA-512 throughout (SHA512_HASH_SZ everywhere in esp_srp.c)
  * k, u:  calculate_padded_hash() zero-LEFT-PADS the shorter operand to
           len(N) -- k = H(PAD(N) | PAD(g)), u = H(PAD(A) | PAD(B))
  * x:     H(salt | H(I | ":" | P))   -- TWO stages, with a literal colon.
           esp_srp.h's prose says "x = H(s | I | P)", which is NOT what
           calculate_x() computes; its prose also says SHA1 where the code
           uses SHA-512. The code is the specification here.
  * K:     SHA512(S) over the RAW big-endian bytes of S -- esp_mpi_to_bin(),
           NOT padded to len(N). This differs from k/u and is the single
           easiest thing to get wrong.
  * M1:    H[H(N) XOR H(g) | H(I) | salt | A | B | K]   -- the SALT, not S.
  * M2:    H(A | M1 | K)
"""
import hashlib, os

# RFC 5054 3072-bit group (same bytes as N_3072 in esp_srp.c).
N_HEX = (
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74"
    "020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F1437"
    "4FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF05"
    "98DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB"
    "9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF695581718"
    "3995497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A33"
    "A85521ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6BF12FFA06D98A0864"
    "D87602733EC86A64521F2B18177B200CBBE117577A615D6C770988C0BAD946E2"
    "08E24FA074E5AB3143DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF"
)
N = int(N_HEX, 16)
g = 5
NLEN = (N.bit_length() + 7) // 8          # 384


def _H(*chunks: bytes) -> bytes:
    h = hashlib.sha512()
    for c in chunks:
        h.update(c)
    return h.digest()


def _int(b: bytes) -> int:
    return int.from_bytes(b, "big")


def _bytes(i: int, length: int | None = None) -> bytes:
    """Big-endian, minimal length by default -- esp_mpi_to_bin() semantics."""
    if length is None:
        length = max(1, (i.bit_length() + 7) // 8)
    return i.to_bytes(length, "big")


def _pad(b: bytes) -> bytes:
    return b.rjust(NLEN, b"\x00")


def _padded_hash(a: bytes, b: bytes) -> int:
    """calculate_padded_hash(): pad the SHORTER of the two up to len(N)."""
    return _int(_H(_pad(a) if len(a) < NLEN else a,
                   _pad(b) if len(b) < NLEN else b))


K_MULT = _padded_hash(_bytes(N, NLEN), _bytes(g))   # k = H(PAD(N) | PAD(g))
# (_padded_hash pads the shorter operand, so g is padded here already.)


class Client:
    """Drives the client half of protocomm security2."""

    def __init__(self, username: bytes, password: bytes, a: int | None = None):
        self.I = username
        self.P = password
        self.a = a if a is not None else _int(os.urandom(32))
        self.A = pow(g, self.a, N)
        self.A_b = _bytes(self.A, NLEN)

    def process_challenge(self, salt: bytes, B_b: bytes):
        """Given device_salt and device_pubkey, return (M1, K)."""
        B = _int(B_b)
        if B % N == 0:
            raise ValueError("device public key is zero mod N")

        u = _padded_hash(self.A_b, B_b)
        if u == 0:
            raise ValueError("u is zero")

        # x = H(salt | H(I | ":" | P))  -- TWO stages, with a literal colon.
        # esp_srp.c calculate_x(), not the "x = H(s | I | P)" in esp_srp.h,
        # whose prose also says SHA1 where the code uses SHA-512.
        x = _int(_H(salt, _H(self.I, b":", self.P)))
        v = pow(g, x, N)

        # S = (B - k*v) ^ (a + u*x)  mod N
        base = (B - K_MULT * v) % N
        S = pow(base, self.a + u * x, N)

        self.S_b = _bytes(S)                        # RAW, not padded
        self.K = _H(self.S_b)                       # K = SHA512(S)

        # H(N) is over the unpadded N (already len(N)); H(g) is over PAD(g)
        # zero-left-padded to len(N). And M1 hashes the SALT, not S.
        hN = _H(_bytes(N, NLEN))
        hg = _H(_pad(_bytes(g)))
        hxor = bytes(p ^ q for p, q in zip(hN, hg))
        hI = _H(self.I)
        self.salt = salt
        self.B_b = B_b
        self.M1 = _H(hxor, hI, salt, self.A_b, B_b, self.K)
        return self.M1, self.K

    def verify_device_proof(self, M2: bytes) -> bool:
        return _H(self.A_b, self.M1, self.K) == M2


class Server:
    """The device half -- only used to self-test the client's maths."""

    def __init__(self, username: bytes, password: bytes, salt: bytes | None = None,
                 b: int | None = None):
        self.I = username
        self.salt = salt if salt is not None else os.urandom(16)
        x = _int(_H(self.salt, _H(username, b":", password)))
        self.v = pow(g, x, N)
        self.b = b if b is not None else _int(os.urandom(32))
        self.B = (K_MULT * self.v + pow(g, self.b, N)) % N
        self.B_b = _bytes(self.B, NLEN)

    def session(self, A_b: bytes):
        A = _int(A_b)
        u = _padded_hash(A_b, self.B_b)
        S = pow(A * pow(self.v, u, N), self.b, N)
        S_b = _bytes(S)
        K = _H(S_b)
        hN = _H(_bytes(N, NLEN))
        hg = _H(_pad(_bytes(g)))
        hxor = bytes(p ^ q for p, q in zip(hN, hg))
        M1 = _H(hxor, _H(self.I), self.salt, A_b, self.B_b, K)
        return M1, K, S_b

    def device_proof(self, A_b: bytes, M1: bytes, K: bytes) -> bytes:
        return _H(A_b, M1, K)
