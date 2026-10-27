# Security notes

This page says what protects an espOS device, what doesn't, and what
travels over the network. In short:

* Once you set an API key, the web page and the REST API need it.
* The device only installs firmware with a valid signature, wherever the
  image came from.
* Everything travels over plain http on the boat's network, so anyone who
  can capture that traffic can read the key.

## Who this keeps out

An espOS device sits on the boat's network next to the Signal K server, the
chartplotter and everyone's phone, and now and then on a marina's shared
WiFi. Its REST API can change any setting, reboot or factory-reset the
device, drop and re-request the Signal K token, and do whatever the
firmware's own endpoints allow. The API key is meant to keep out:

* **accidents**: a guest's phone that found `espos-1a2b.local`, a script
  pointed at the wrong address, a browser tab left open on the marina WiFi;
* **a hostile web page** in your own browser that tries to use your browser
  to reach the device (CSRF);
* **a curious neighbour** on a shared network who can send requests but
  isn't capturing traffic.

It does not protect against someone **capturing traffic** on the network
(the key crosses it in clear, see [Transport](#transport)), someone with
**the USB port** (a development build has no Secure Boot and no flash
encryption), or the Signal K server, which the device trusts by design.

## REST authentication

Every endpoint is protected unless it is one of the few public ones
([REST API](rest-api.md) marks each). One setting, **`httpd.api_key`**,
decides what protected means:

| `httpd.api_key` | Effect |
|---|---|
| empty (the default) | The API is **open** to anyone on the network. A device from before authentication existed keeps working after an update, and first setup needs no key. |
| set (8 to 64 characters) | Every protected request needs a credential, or it gets `401`. |

There are two kinds of credential, both checked in one place before any
handler runs:

* **`Authorization: Bearer <key>`**, for programs: the HMI designer, the
  manager plugin, `curl`. The device keeps nothing for these.
* **A session cookie**, for browsers. `POST /api/v1/auth/login {"key": …}`
  answers with the cookie `espos_sid` (`HttpOnly; SameSite=Strict; Path=/`),
  valid for `httpd.session_ttl_s`, a day by default. `fetch()` and
  `EventSource` send it by themselves, and the web UI shows its login page
  when the device asks for one. The device keeps sessions in a small table in
  RAM (`CONFIG_ESPOS_HTTPD_MAX_SESSIONS`, 4). One login too many pushes out
  the session idle longest, a reboot forgets them all, and changing the key
  ends every session, so everyone logs in again with the new key.

A browser sends the cookie with every request a page makes, including a
hostile page. So a cookie-authenticated request that **changes something**
(`PUT`, `POST`, `DELETE`) must also come from the device's own page: the
host in `Origin` (or `Referer`) must equal `Host`, or the answer is
`403 forbidden`. That comes on top of the older rule that such requests carry
`Content-Type: application/json`, which a browser can't send to another site
without a CORS preflight the device never allows. Bearer requests skip the
origin check, because a browser never adds that header by itself.

Guessing is slowed down, not blocked. After **five wrong keys within 60 s**,
every key check (login and Bearer, even with the right key) answers
`429 too_many_attempts` with `Retry-After` for **30 s**. The counter is
shared by all clients, because the device can't cheaply tell them apart.
Sessions that are already logged in aren't key checks and keep working. Keys
and session ids are compared in constant time.

Applications get this for free. An endpoint registered with
`espos_httpd_register()` is protected. One that must stay reachable
registers with `espos_httpd_register_ex(uri, ESPOS_HTTPD_PUBLIC)`, and a
public handler can still ask `espos_httpd_request_authenticated()`. The
public endpoints are the web UI files, `GET /api/v1/system/ping` (app,
version, and whether a key is needed), the captive-portal probe URLs, and
`/api/v1/auth/*`.

### The setup portal, and the way back in

While **no key is set**, a request that arrives on the device's own setup
network needs no authentication. That is what the setup network is for:
`http://192.168.4.1` on a new or factory-reset device is where you enter the
first `httpd.api_key`. The device checks the local address the request came
in on, so a remote client can't fake it, and it never applies to requests
over the station WiFi or Ethernet.

Once a key is set, the setup network is treated like any other: a Bearer key
or a login, the origin rule, the throttle. Being on it proves nothing about
who is near the device. `wifi.portal_psk` is empty by default, so the setup
network is open, and the device raises it unattended:
`wifi.portal_after_s` after the WiFi link drops, or at once when no network
is configured. (It doesn't come up while another link such as Ethernet
carries the network, see [WiFi](wifi.md#portal-softap-provisioning), but
that only shrinks the exposure.) Earlier releases trusted the setup network
always; [Decisions](decisions.md) records why that changed.

**If you lose the key and have no cable**, switch the device **off and on
three times**. Let it come up each time, and switch it off again within 20 s
of each boot. After the third time the setup network needs no key for 10
minutes: join `espOS-xxxx`, open the Config page and set a new key.
`GET /api/v1/auth/status` shows the seconds left as `recovery_s`, and the
log says:

```
W espos_auth: 3 power cycles: the setup access point is exempt from the API key for 600 s — set a new one
```

> **On a device that is online over Ethernet, unplug the cable first.** The
> recovery window only applies to the setup network; it doesn't raise one,
> and there is none while Ethernet carries the network. Unplugging brings it
> back: at once if no WiFi network is configured, otherwise once
> `portal_after_s` has passed since the WiFi link dropped (not since the
> unplug). Ethernet stays protected the whole time, so you can't recover a
> key over the cable. The easy order is to unplug first, then power-cycle.
> Setting `wifi.portal_online = true` while you still have the key avoids
> all this. Firmware with a real presence button can also call
> `espos_wifi_portal_open(true)` and `espos_httpd_auth_recovery_open()`
> together.

The details:

* Only switching the **power** off counts. A watchdog reboot, a crash or a
  software restart doesn't, so a device stuck in a reboot loop can't open
  itself up.
* The count is kept in NVS, because RTC memory doesn't survive a power cut.
  It is only written while a key is set, so a device without a key pays no
  flash writes for it.
* A boot that lasts longer than `CONFIG_ESPOS_HTTPD_RECOVERY_CLEAR_S` resets
  the count, so only quick cycles add up.
* The window lives in RAM, so a reboot ends it.
* The numbers are `CONFIG_ESPOS_HTTPD_RECOVERY_CYCLES`,
  `CONFIG_ESPOS_HTTPD_RECOVERY_CLEAR_S` and
  `CONFIG_ESPOS_HTTPD_RECOVERY_WINDOW_S`.
  `CONFIG_ESPOS_HTTPD_PORTAL_RECOVERY=n` removes this path completely, for a
  device whose power switch strangers can reach. Keep a cable for that one.

Firmware that can prove someone is physically there, with a recessed button,
a jumper or a key switch, should call `espos_httpd_auth_recovery_open(seconds)`
from that event instead. Never call it from a timer or a network request:
that gives the key away. The window never relaxes anything on the station
side, and the guessing lockout doesn't apply to it, since a lockout is one of
the things it is there to get you out of.

A factory reset (`POST /api/v1/system/factory-reset`, which needs the key)
also gets you back in, because it leaves the device with no key. It also
deletes the settings, the WiFi networks and the Signal K token. There is no
other way in: no default key, and no reset URL on the station side.

**While you're at it, set `wifi.portal_psk`.** An open setup network is a
nuisance even when nothing behind it is reachable, and a device that keeps
it up all the time advertises it all the time.

### For products: `CONFIG_ESPOS_HTTPD_AUTH_REQUIRED`

An open default suits a device someone flashes at home. A product that must
never ship open builds with `CONFIG_ESPOS_HTTPD_AUTH_REQUIRED=y`. Then, while
`httpd.api_key` is empty, protected endpoints answer `403 auth_unconfigured`,
except on the setup network, where the first key gets set. The web UI
explains this when it sees the 403. That exception is the no-key one above,
so it closes as soon as a key exists.

### What it does and doesn't stop

It stops the guest's phone, the misdirected script, the tab on the marina
WiFi, a hostile page in your browser, and casual guessing.

It doesn't stop anyone who can **capture traffic** between a client and the
device. Over plain http the key crosses the network in clear with every
Bearer request and every login, just like the Signal K token does between
the device and the server. On a boat's own network that is an accepted
trade-off; the same network carries NMEA with no authentication at all. On a
shared marina network, treat the key as seen and change it when you are back
on your own network.

## Transport

Traffic to the Signal K server is plain `http` and `ws` unless the device
talks to the server over TLS (`sk.scheme`, `auto` by default,
[Signal K](signalk.md)). So the access token travels in an `Authorization`
header over an unencrypted connection, and anyone who can capture traffic on
the boat's network can reuse it against the server, with whatever access the
token has.

The device's own web server is http only, on purpose. A TLS server costs RAM
the smaller chips don't have, and a self-signed certificate on a boat
network gives the browser nothing to check it against.
`CONFIG_ESPOS_HTTPD_TLS` is **reserved** (off, and it does nothing) so that a
later implementation has a stable name. Until then the API key protects
against use, not against eavesdropping. Firmware updates don't depend on the
transport either way: the device checks every image's signature, so an image
fetched over plain http can't be swapped (see below).

## Secrets on the device

The API key, the WiFi passwords and the Signal K token are stored in NVS and
marked `secret`. The API never returns them (they read back as
`"********"`), and the web UI only shows whether they are set.

For production builds, turn on flash encryption. ESP-IDF then sets
`CONFIG_NVS_ENCRYPTION=y` and encrypts the `nvs` partition with keys from
the `nvs_keys` partition, which every bundled table in
`components/espos_core/partitions/` has, or with the HMAC peripheral on
chips that have one. The release overlay `sdkconfig.d/release.defaults`
does this; select it with `espos_project_prologue(... PROFILE release)` or
`-DESPOS_PROFILE=release`:

```
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
CONFIG_NVS_ENCRYPTION=y
```

Development boards stay unencrypted, so `idf.py flash` keeps working without
burning eFuses. The host tests always run unencrypted, because `nvs_flash`
on the linux target doesn't support encryption.

## Request handling

* Authentication is checked before a handler runs, for every endpoint, so a
  refused request never reaches the handler's parsing.
* Request bodies are capped at `CONFIG_ESPOS_HTTPD_MAX_BODY` (16 KiB by
  default); anything larger gets `413`.
* All JSON input is checked against the settings descriptor before anything
  is written.
* Settings are checked again when they are read, so bad or stale values in
  NVS can't reach the application out of range.
* Static file paths refuse `..` and `//`. Anything under `/api/` that isn't
  registered gets a JSON `404`, never a file.

## Firmware updates

Every app image is signed (RSA-3072, `SECURE_SIGNED_APPS_NO_SECURE_BOOT`).
The running firmware checks the signature of any update against its built-in
public key before it installs it. So a device only takes firmware from
whoever holds `secure_boot_signing_key.pem`, whether the image came over
`http://` or `https://`, and whoever asked for the update
(`POST /api/v1/ota` needs the key like everything else).

This doesn't stop someone with the USB port, because there is no hardware
Secure Boot and no flash encryption by default; both are options in the
release overlay. Rollback is the bootloader's `APP_ROLLBACK_ENABLE` plus
`espos_ota`'s rule that a new image confirms itself once it reaches the
network ([OTA](ota.md)). Keep the signing key out of the repository. It is
git-ignored, and without one the build makes a development key and warns
about it loudly.
