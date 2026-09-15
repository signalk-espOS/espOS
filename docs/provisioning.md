# BLE provisioning (`espos_prov`)

Hand a device its WiFi credentials from a phone over Bluetooth, with no
access point and no captive portal. Optional and off by default
(`CONFIG_ESPOS_PROV`, about 23 KB of flash); it needs Bluedroid, and the
application must already have brought the BLE stack up — `espos_ble` does
this, including the ESP32-P4's hosted co-processor path.

```c
ESP_ERROR_CHECK(espos_start(NULL));
espos_prov_start(NULL);   /* advertises as ESPOS_<id>, random PoP, 10 min */
```

Credentials arrive on the provisioning task; `espos_prov_start()` returns as
soon as the device is advertising.

## What it does not do, and why that matters

Espressif's `network_provisioning` manager drives the station itself —
`esp_wifi_set_config()`, `esp_wifi_connect()`, `esp_wifi_start()` and its own
retry logic. espOS already has a WiFi state machine that owns exactly those
calls, with a priority list of networks, exponential backoff, reason-code
handling and the SoftAP portal, host-tested against them
([wifi.md](wifi.md)). Two owners of one radio is a fault that only shows up
in the field.

So `espos_prov` uses **protocomm as a BLE transport only**. Credentials
arriving over GATT are written into the `wifi` config namespace — the same
keys the web UI writes — and the state machine picks them up through its
config-change callback and connects exactly as it always does. Nothing in
this component calls `esp_wifi_*`.

Two consequences follow, both deliberate:

* **Espressif's "ESP BLE Provisioning" phone app will not talk to this
  device.** That app speaks the manager's protobuf schema; the endpoint here
  is plain JSON, which any BLE tool — or a web-Bluetooth page — can write.
* Because the manager never attempts the connection, it cannot report "wrong
  password" from its own connect path. `espos_prov` answers from
  `espos_wifi_get_status()` instead, which is the same truth by another
  route.

The alternative was to adopt the manager and give it the station, which
would mean reworking or retiring espOS's WiFi state machine rather than
swapping a component. Recorded in [decisions.md](decisions.md).

## It cannot share a firmware with `espos_ble`

**Verified on hardware, 2026-09-15: a build containing both `espos_prov` and
`espos_ble` starts the gateway and then fails to start provisioning.**

protocomm's BLE transport brings the Bluetooth stack up itself --
`simple_ble_start()` calls `esp_bt_controller_init()` and
`esp_bluedroid_init_with_cfg()` unconditionally, with no check for a stack
that is already running. `espos_ble` has already initialised Bluedroid for
the gateway by then, so the second initialisation is refused:

```
I (4543) espos_ble: scanning suspended (BLE provisioning)
E (4544) BT_LOG: Bluedroid already initialised
E (4544) simple_ble: simple_ble_start init bluetooth failed 259
E (4547) protocomm_ble: simple_ble_start failed w/ error code 0x103
E (4553) espos_prov: protocomm_ble_start: ESP_ERR_INVALID_STATE
I (4558) espos_ble: scanning resumed
```

`espos_prov_start()` returns `ESP_ERR_INVALID_STATE`. It unwinds correctly --
the scanner is resumed and nothing leaks -- so the gateway keeps working and
only provisioning is missing. `GET /api/v1/prov` is absent too, because the
endpoint is registered at the end of a successful start.

Until this is fixed, use `espos_prov` **only in a firmware without
`espos_ble`**. A device that needs both has to provision over the SoftAP
portal ([wifi.md](wifi.md)).

Fixing it means one of: teaching protocomm to skip initialisation when the
stack is up (an upstream change), or giving `espos_prov` its own transport
that does not go through `simple_ble`. Neither is a small change, and
neither has been made.

**Do not call `espos_prov_start()` inside `ESP_ERROR_CHECK()`.** It returns
errors a device can survive, and on a board with no serial console an abort
becomes an OTA rollback with the reason lost -- which is how this was found.
Log it and carry on.

## Verified end to end on an ESP32-C5

2026-09-15, on an ESP32-C5 (rev v1.0, native BLE radio, no `espos_ble` in the
build). A Python client on the same LAN ran the whole flow:

```
handshake: Cmd0 ->  Resp0 416 bytes
handshake: Cmd1 ->  Resp1 89 bytes
SESSION ESTABLISHED -- device proof verified, AES-256-GCM keyed
writing credentials for 'ProvTestNet' ...
  device replied: {"ok":true}
```

and the device acted on them, which is the part that matters:

```
I (297535) espos_wifi: connecting to 'ProvTestNet'
I (300595) espos_prov: provisioning stopped
```

Credentials reached the `wifi` namespace, the state machine picked them up
through its config-change callback, and the window closed itself about three
seconds after the write -- each as documented above.

### It needs BLE 4.2 advertising, which is protocomm's limit, not the chip's

protocomm's `simple_ble` advertises only through the BLE **4.2 legacy** API
(`esp_ble_gap_start_advertising`, `esp_ble_gap_config_adv_data`), which
Bluedroid compiles under `BLE_42_ADV_EN`. A radio configured for BLE 5.0
extended advertising does not build those symbols, and the link fails:

```
undefined reference to `esp_ble_gap_start_advertising'
undefined reference to `esp_ble_gap_config_adv_data'
```

So a build with `espos_prov` needs

```
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
CONFIG_BT_BLE_50_FEATURES_SUPPORTED=n
```

The two are mutually exclusive. This is **not** a hardware limitation -- the
ESP32-C5 declares `SOC_BLE_50_SUPPORTED` and its radio is BLE 5.0 -- it is
Espressif's provisioning code not having been updated for extended
advertising. A firmware that needs extended advertising for something else
cannot also use `espos_prov` until that changes upstream.

### The portal does the same job without any of this

`espos_wifi` already brings up a SoftAP portal when no network is configured,
and it writes the same `wifi` keys:

```
I (1435) espos_wifi: no network configured: join "espOS-9174" and open http://192.168.4.1
```

BLE provisioning is an alternative to that, not a prerequisite: it is worth
having when joining a temporary access point is awkward -- a phone app, a
sealed enclosure, a fleet being set up in one pass -- and worth skipping
otherwise.

## The scanner stops while this runs

Bluedroid keeps exactly one GAP callback — `esp_ble_gap_register_callback()`
is a setter, not a subscribe — and protocomm's `simple_ble` registers its
own. A BLE gateway left scanning would keep reporting "scanning" and receive
nothing at all, with no error anywhere.

`espos_prov` therefore suspends the scanner for the duration and resumes it
when provisioning ends, reclaiming the GAP callback first. A device being
provisioned has no network to publish to yet, so nothing is lost.

## The wire

One service, two characteristics, all UUIDs fixed — a client that has talked
to one espOS device expects the same layout on the next.

| Endpoint | UUID | Purpose |
|---|---|---|
| `prov-session` | `0xFF51` | protocomm Security 2 handshake |
| `espos-config` | `0xFF52` | the configuration write |

Security is protocomm **Security 2** (SRP6a): the salt and verifier are
derived at boot from the proof of possession. Espressif calls deriving them
on the device the development pattern and prefers salt and verifier embedded
at manufacture; for a self-built marine device the PoP is not a secret worth
a provisioning server.

### What to write to `espos-config`

The shorthand, which is the common case:

```json
{"ssid": "MyBoat", "psk": "secret"}
```

That writes `ssid0`/`psk0` — network 0, the same slot the setup portal
writes, so provisioning and the portal cannot disagree about which network a
freshly configured device tries first — and sets `sta_enabled` to true,
because provisioning implies the station should come up even on a device
someone had disabled it on.

Or the general form, any document `espos_config_import_json` accepts, so a
phone can provision more than the radio in one write:

```json
{"wifi": {"ssid0": "MyBoat", "psk0": "secret"},
 "sk":   {"host": "192.168.0.148"},
 "net":  {"hostname": "masthead"}}
```

Unknown keys are **rejected**, not ignored: over BLE there is no second
chance to notice a typo, and a device that reports success while dropping
half the document is worse than one that says no.

### Replies

Always answered, on every path — a client left waiting on a silent
characteristic cannot tell a rejected password from a crashed device.

| Reply | Meaning |
|---|---|
| `{"ok":true}` | written; the state machine takes it from here |
| `{"ok":false,"error":"not_json"}` | the body did not parse |
| `{"ok":false,"error":"bad_request"}` | empty body |
| `{"ok":false,"error":"rejected"}` | parsed, but the config was refused (unknown key, bad value) |

## The window closes on its own

A device left advertising is a device anyone in range can try to configure.

* `CONFIG_ESPOS_PROV_TIMEOUT_S` (default 600) stops advertising after ten
  minutes; `espos_prov_cfg_t.timeout_s` overrides it, 0 means never.
* Credentials accepted closes it about three seconds later — the job is
  done, and the delay is only so the reply reaches the phone.

## Proof of possession

`prov.pop`, at most 23 characters. Empty makes the device generate a random
one on first use and keep it, from an alphabet with no `0`/`O`/`1`/`I` so it
can be read aloud.

It is deliberately **not** derived from the MAC or the device id: the
advertised name already carries the id, so a derived PoP would travel over
the air beside the thing it is meant to protect. It is persisted rather than
regenerated per boot, because whoever is standing at the device has read it
from the log or the status endpoint. A PoP printed on the enclosure at
manufacture is better still, and is what `espos_prov_cfg_t.pop` is for.

Setting `pop` to an over-long value fails the start rather than truncating:
a truncated PoP would leave the device expecting a different secret from the
one the caller set, and the mismatch would surface only as an unexplained
authentication failure from the phone.

## Status

`GET /api/v1/prov`:

```json
{"active": true, "got_credentials": false,
 "service_name": "ESPOS_ca6a", "pop": "7K4M9QRT2WXY",
 "scheme": "espos-ble-prov-1"}
```

Reachable only over the network, which a device being provisioned does not
have yet — that is not a contradiction. It is for the other cases: a device
already on WiFi advertising for re-provisioning, and the setup portal, which
serves it over its own access point. The PoP is in there because a
device-generated PoP is useless if nobody can read it.

`scheme` is not Espressif's QR schema: that one names their transport and
their app, and this device speaks neither.

## Configuration

| Key | Default | What it is |
|---|---|---|
| `prov.pop` | `""` (generated) | the passphrase a phone must present; clearing it picks a new one |

| Kconfig | Default | What it is |
|---|---|---|
| `ESPOS_PROV` | off | build the component at all |
| `ESPOS_PROV_TIMEOUT_S` | 600 | stop advertising after this many seconds; 0 = never |

## API

| Function | |
|---|---|
| `espos_prov_start(cfg)` | start advertising; `NULL` takes every default. `ESP_ERR_INVALID_STATE` if already running |
| `espos_prov_stop()` | tear down the GATT server and resume scanning |
| `espos_prov_is_active()` | advertising now |
| `espos_prov_got_credentials()` | a configuration write has been accepted this session |
