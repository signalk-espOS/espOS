# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Provision an espOS device over BLE -- the client half of espos_prov.

Usage:
  provision.py --pop <PoP> --name <service name> [--ssid S --psk P] [--probe]

The service name doubles as the SRP username (espos_prov.c passes it to
esp_srp_gen_salt_verifier), so it must match what the device advertises.
Both it and the PoP are served by GET /api/v1/prov on a device that still
has a network.

UUIDs: protocomm keeps one 128-bit UUID per endpoint, built from the
service UUID with bytes 12-13 replaced by the endpoint's 16-bit id
(protocomm_ble.c:831-833). espos_prov's service UUID is kServiceUuid,
stored little-endian, so on the air it reads back reversed.
"""
import argparse, asyncio, json, sys

from bleak import BleakScanner, BleakClient
import session as sess

SVC_LE = [0x1c,0x4b,0x8f,0x2a,0x6d,0x11,0x47,0x9e,
          0xa3,0x5c,0x70,0xe8,0x92,0x0d,0x53,0xb6]


def _uuid(le_bytes) -> str:
    h = bytes(reversed(le_bytes)).hex()
    return f"{h[0:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"


def _char(u16: int) -> str:
    b = list(SVC_LE)
    b[12] = u16 & 0xFF
    b[13] = (u16 >> 8) & 0xFF
    return _uuid(b)


SERVICE_UUID = _uuid(SVC_LE)
CH_SESSION   = _char(0xFF51)
CH_CONFIG    = _char(0xFF52)


async def _exchange(client: BleakClient, char: str, payload: bytes) -> bytes:
    """protocomm maps one request onto a WRITE then a READ of the same
    characteristic: the write handler stashes the response for the read."""
    await client.write_gatt_char(char, payload, response=True)
    return bytes(await client.read_gatt_char(char))


async def run(args) -> int:
    print(f"  service    {SERVICE_UUID}")
    print(f"  session    {CH_SESSION}")
    print(f"  config     {CH_CONFIG}")
    print(f"\n  scanning for {args.name!r} ...")

    # The name must MATCH, not merely be one of two ways to match: espos_prov
    # passes the service name to esp_srp_gen_salt_verifier() as the SRP
    # username, so connecting to some other espOS device that happens to
    # advertise the same service would fail in the handshake with a bad-proof
    # error rather than here with "not found". The UUID stays as a second
    # condition, to skip anything that merely shares the name.
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: ((ad.local_name or d.name) == args.name
                       and SERVICE_UUID.lower() in [u.lower() for u in ad.service_uuids]),
        timeout=args.scan_timeout)
    if dev is None:
        print("  NOT FOUND -- is a provisioning window open?")
        return 1
    print(f"  found {dev.name} [{dev.address}]")
    if args.probe:
        return 0

    async with BleakClient(dev) as client:
        # Negotiate a real MTU before writing: the default 23 leaves a 20-byte
        # ATT payload, and protocomm's frames are far larger. BlueZ exposes
        # this only through the private _acquire_mtu().
        try:
            if getattr(client, "mtu_size", 23) <= 23 and hasattr(client, "_acquire_mtu"):
                await client._acquire_mtu()
        except Exception as e:
            print(f"  (MTU negotiation failed, continuing: {e})")
        print(f"  connected, mtu={getattr(client, 'mtu_size', '?')}")
        chars = {c.uuid.lower() for s in client.services for c in s.characteristics}
        for want, label in ((CH_SESSION, "prov-session"), (CH_CONFIG, "espos-config")):
            print(f"    {label:13s} present: {want.lower() in chars}")

        s = sess.Sec2Session(args.name.encode(), args.pop.encode())

        print("\n  handshake: Cmd0 ->")
        resp0 = await _exchange(client, CH_SESSION, s.cmd0())
        print(f"    Resp0 {len(resp0)} bytes")

        print("  handshake: Cmd1 (client proof) ->")
        resp1 = await _exchange(client, CH_SESSION, s.cmd1(resp0))
        print(f"    Resp1 {len(resp1)} bytes")

        s.finish(resp1)
        print("  SESSION ESTABLISHED -- device proof verified, AES-256-GCM keyed")

        if not args.ssid:
            print("\n  no --ssid given: handshake only, nothing written")
            return 0

        doc = json.dumps({"ssid": args.ssid, "psk": args.psk or ""}).encode()
        print(f"\n  writing credentials for {args.ssid!r} ...")
        reply = await _exchange(client, CH_CONFIG, s.encrypt(doc))
        answer = s.decrypt(reply)
        print(f"    device replied: {answer.decode(errors='replace')}")
        return 0 if b'"ok":true' in answer else 2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pop", required=True)
    ap.add_argument("--name", required=True, help="advertised name = SRP username")
    ap.add_argument("--ssid")
    ap.add_argument("--psk")
    ap.add_argument("--probe", action="store_true", help="scan only, do not connect")
    ap.add_argument("--scan-timeout", type=float, default=20.0)
    args = ap.parse_args()
    try:
        return asyncio.run(run(args))
    except Exception as e:
        print(f"  FAILED: {type(e).__name__}: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
