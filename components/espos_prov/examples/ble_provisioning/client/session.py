# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""protocomm security2 session + AES-GCM transport, over one GATT write/read.

Wire shape, read out of ESP-IDF rather than assumed:
  * every session frame is a SessionData protobuf with sec_ver=SecScheme2
    and a Sec2Payload inside (session.proto / sec2.proto);
  * the handshake is Cmd0 -> Resp0 (device_pubkey, device_salt) then
    Cmd1 -> Resp1 (device_proof, device_nonce);
  * after that, every payload on the config endpoint is AES-256-GCM with
    key = K[:32] and a 12-byte IV = device_nonce, whose low 4 bytes are a
    big-endian counter incremented per message (sec2_gcm_iv_counter_increment).
  * protocomm maps one request onto a GATT WRITE followed by a READ of the
    same characteristic (transport_simple_ble_write stashes the response).
"""
import struct
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import session_pb2, sec2_pb2, constants_pb2
import srp6a

# espos_prov passes its SERVICE NAME to esp_srp_gen_salt_verifier() as the SRP
# username (espos_prov.c: esp_srp_gen_salt_verifier(s.service_name, ...)), so a
# client must use the advertised name -- "ESPOS_<id>" by default -- not a fixed
# string. Read it from GET /api/v1/prov, or take it from the advertisement.


class Sec2Session:
    def __init__(self, username: bytes, pop: bytes):
        self.cli = srp6a.Client(username, pop)
        self.username = username
        self.key = None
        self.iv = None

    # ---- handshake ----------------------------------------------------
    def cmd0(self) -> bytes:
        d = session_pb2.SessionData()
        d.sec_ver = session_pb2.SecScheme2
        d.sec2.msg = sec2_pb2.S2Session_Command0
        d.sec2.sc0.client_username = self.username
        d.sec2.sc0.client_pubkey = self.cli.A_b
        return d.SerializeToString()

    def cmd1(self, resp0_bytes: bytes) -> bytes:
        r = session_pb2.SessionData.FromString(resp0_bytes)
        if r.sec2.sr0.status != constants_pb2.Success:
            raise RuntimeError(f"device rejected Cmd0: status={r.sec2.sr0.status}")
        M1, _K = self.cli.process_challenge(r.sec2.sr0.device_salt,
                                            r.sec2.sr0.device_pubkey)
        d = session_pb2.SessionData()
        d.sec_ver = session_pb2.SecScheme2
        d.sec2.msg = sec2_pb2.S2Session_Command1
        d.sec2.sc1.client_proof = M1
        return d.SerializeToString()

    def finish(self, resp1_bytes: bytes) -> None:
        r = session_pb2.SessionData.FromString(resp1_bytes)
        if r.sec2.sr1.status != constants_pb2.Success:
            raise RuntimeError(f"device rejected our proof: status={r.sec2.sr1.status}")
        if not self.cli.verify_device_proof(r.sec2.sr1.device_proof):
            raise RuntimeError("device proof did not verify -- wrong PoP, or a MITM")
        self.key = self.cli.K[:32]        # AES-256 from the 64-byte K
        self.iv = bytearray(r.sec2.sr1.device_nonce)
        if len(self.iv) != 12:
            raise RuntimeError(f"device_nonce is {len(self.iv)} bytes, expected 12")

    # ---- encrypted endpoint -------------------------------------------
    def _bump_iv(self) -> None:
        ctr = struct.unpack(">I", bytes(self.iv[8:12]))[0]
        self.iv[8:12] = struct.pack(">I", (ctr + 1) & 0xFFFFFFFF)

    def encrypt(self, plaintext: bytes) -> bytes:
        if self.key is None:
            raise RuntimeError("session not established")
        out = AESGCM(self.key).encrypt(bytes(self.iv), plaintext, None)
        self._bump_iv()
        return out

    def decrypt(self, ciphertext: bytes) -> bytes:
        out = AESGCM(self.key).decrypt(bytes(self.iv), ciphertext, None)
        self._bump_iv()
        return out
