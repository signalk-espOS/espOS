# ble_provisioning

A device that gets its WiFi credentials from a phone or a laptop over BLE, with
no access point and no captive portal. The whole firmware is `espos_start()`:
when no network is configured it advertises, credentials arrive over GATT and
land in the `wifi` config namespace, and espOS's own WiFi state machine picks
them up and connects. Details: [docs/provisioning.md](../../../../docs/provisioning.md).

## Espressif's phone app will not talk to this device

This is the first thing to know, because the obvious next step is to install
**ESP BLE Provisioning** from the app store and it will not work. That app
speaks `network_provisioning`'s protobuf schema; `espos_prov` uses protocomm as
a BLE *transport only* and its configuration endpoint is plain JSON, because
espOS's WiFi state machine already owns `esp_wifi_connect()` and two owners of
one radio is a fault that only shows up in the field.

So the client is the Python script in [`client/`](client), which is also the
readable specification of the wire protocol — the handshake maths in
`srp6a.py` in particular, which is where ESP-IDF's own header comments are
wrong (they say SHA1 where `esp_srp.c` uses SHA-512).

**The setup portal does the same job without any of this.** espOS raises a
WiFi access point with a config page on an unprovisioned device already. This
example is for the cases the portal cannot serve: a sealed enclosure, a fleet
provisioned from a script, or a device whose radio should never come up as an
access point.

## It cannot share a firmware with `espos_ble`

Bluedroid keeps exactly one GAP callback — `esp_ble_gap_register_callback()`
is a setter, not a subscribe — and protocomm takes it. A BLE gateway scanning
at the same time keeps running and silently receives nothing, with no error
anywhere. `espos_prov` therefore suspends the scanner while it advertises, and
this example does not build `espos_ble` at all.

## BLE 4.2, and that is protocomm's limit rather than the chip's

`sdkconfig.defaults` asks for legacy advertising:

```
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
CONFIG_BT_BLE_50_FEATURES_SUPPORTED=n
```

protocomm's `simple_ble` advertises only through the 4.2 API, which Bluedroid
compiles under `BLE_42_ADV_EN`; configure the radio for 5.0 extended
advertising and the link fails with undefined references to
`esp_ble_gap_start_advertising`. The two are mutually exclusive. The ESP32-C5
this was verified on declares `SOC_BLE_50_SUPPORTED` — a 5.0 radio being asked
to speak 4.2. Upstream has since added BLE 5 support to protocomm, so these two
lines go away when that reaches a released IDF.

## It does not fit a 4 MB board

Bluedroid is large: this firmware links to about 2.2 MB, which does not fit the
default 4 MB table's 1728K app slots, so `CMakeLists.txt` passes
`partitions/16mb.csv`. An 8 MB board works too — swap that for
`partitions/8mb.csv`, whose 3 MB slots are still ample. A device keeps the
table it was flashed with, so changing this later turns the next update into a
USB reflash rather than an OTA.

## Running it

Flash the device. With no WiFi configured it advertises as `ESPOS_<id>` and
logs its proof of possession — generated once at random and kept, so it is the
same on every boot until it is cleared:

```
I (2317) espos_prov: provisioning over BLE as "ESPOS_ca6a", pop "7K4M9QRT2WXY"
I (2319) espos_prov: advertising for 600 s
```

Then, from a machine with a Bluetooth adapter:

```sh
cd client
python3 -m venv venv && ./venv/bin/pip install -r requirements.txt

# The protobuf modules are generated from YOUR ESP-IDF, not shipped here, so
# the schema always matches the IDF the device was built with. Run this from
# an environment where . $IDF_PATH/export.sh has been sourced.
./venv/bin/python -m grpc_tools.protoc \
    -I "$IDF_PATH/components/protocomm/proto" --python_out=. \
    "$IDF_PATH"/components/protocomm/proto/session.proto \
    "$IDF_PATH"/components/protocomm/proto/sec0.proto \
    "$IDF_PATH"/components/protocomm/proto/sec1.proto \
    "$IDF_PATH"/components/protocomm/proto/sec2.proto \
    "$IDF_PATH"/components/protocomm/proto/constants.proto

./venv/bin/python provision.py --name ESPOS_ca6a --pop 7K4M9QRT2WXY \
    --ssid MyBoat --psk hunter2
```

A successful run looks like this, and the last line is the part that matters —
the device acting on the credentials, not merely accepting them:

```
  found ESPOS_ca6a [XX:XX:XX:XX:XX:XX]
  connected, mtu=517
  handshake: Cmd0 ->  Resp0 416 bytes
  handshake: Cmd1 (client proof) ->  Resp1 89 bytes
  SESSION ESTABLISHED -- device proof verified, AES-256-GCM keyed
  writing credentials for 'MyBoat' ...
    device replied: {"ok":true}
```

```
I (297535) espos_wifi: connecting to 'MyBoat'
I (300595) espos_prov: provisioning stopped
```

`--probe` scans without connecting, which separates "the device is not
advertising" from "the handshake failed".

## The window closes on its own

Ten minutes by default (`CONFIG_ESPOS_PROV_TIMEOUT_S`), and a few seconds after
credentials arrive, because a device left advertising is a device anyone in the
marina can try to provision. Reboot to reopen it.

Note also that `espos_start()` only advertises when **no** network is
configured. Once this device is provisioned it comes up as an ordinary espOS
device with the radio free; to provision it again, clear the stored networks.
On a device that still has a network, `GET /api/v1/prov` reports the state, the
advertised name and the PoP — which is how you read a device-generated PoP that
nobody wrote down.

## Verified

End to end on an **ESP32-C5** (rev v1.0, native BLE radio, no `espos_ble` in
the build) on 2026-09-15: handshake, credential write, and `espos_wifi`
connecting with what arrived. The same firmware builds for any target with
Bluedroid; the ESP32-P4 would additionally need its C6 co-processor's HCI
route, which is what the [`ble_gateway`](../../../espos_ble/examples/ble_gateway)
example sets up.
