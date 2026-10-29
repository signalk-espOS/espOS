# Updates over the air (`espos_ota`)

`espos_ota` installs new firmware over the network. It only takes images
signed with the key the device trusts, and it goes back to the old firmware
if the new one doesn't come up properly.

## What happens

1. **Finding an update.** Either someone gives the device a URL
   (`POST /api/v1/ota {"url": …}`, or **From a URL** on the OTA page), or
   the device reads a **manifest** (`ota.manifest_url` and `ota.channel`).
   While `ota.auto_check` is on, it checks the manifest once the network is
   up and at least 20 s have passed since boot
   (`CONFIG_ESPOS_OTA_BOOT_CHECK_DELAY_S`), then every `ota.check_h` hours.
   A newer build for the device's target, channel and app shows up as
   *available*. With `ota.auto_install` it installs at
   once; otherwise someone presses **Install**.
2. **Download and check.** `esp_https_ota` writes the image into the other
   app slot. Before writing, it checks the header: the image must be an
   ESP-IDF app with the same `project_name` as the running one, so an
   `espos` device won't take a `myotherapp` image. At the end it checks the
   image's SHA-256 and its **RSA-3072 signature**
   (`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`) against the public key
   built into the running firmware. Anything else is rejected with
   `image rejected: bad signature or corrupt`, and nothing changes.
3. **Restart.** About 1.5 s after the status says `ready`, the device
   restarts into the new slot.
4. **Keep it or go back.** The new image starts as `pending_verify`
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). It marks itself good as soon
   as the network is up, on WiFi or Ethernet. A firmware with WiFi that has
   no network configured, or WiFi turned off, marks itself good after 60 s
   instead (`CONFIG_ESPOS_OTA_NO_STA_GRACE_S`, which must stay below
   `ota.confirm_tmo_s`).
   * If it crashes before that, the bootloader starts the old slot at the
     next reset.
   * If it runs but can't reach the network within `ota.confirm_tmo_s`
     (600 s by default), it marks itself bad and restarts into the old slot.
   * `POST /api/v1/ota/confirm` keeps it by hand, and
     `POST /api/v1/ota/rollback` goes back on purpose.

   The status shows `running.rolled_back` when the other slot holds an image
   that failed.

Plain `http://` sources are allowed (`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`),
because the signature protects the image, not the connection. `https://`
uses the ESP-IDF certificate bundle, and `ota.allow_insecure` skips the
certificate check for a boat server with a self-signed certificate.

## Signing key

Don't confuse it with `httpd.api_key`. That is the password for the REST API
([REST API](rest-api.md)), which signalk-espos-manager calls the "fleet API
key". It guards the web API and the web UI login ([Web UI](ui.md)), and you
can change it any time. The signing key decides which firmware a device will
install, and losing it means reflashing every device over USB. With hardware
Secure Boot the keys in eFuse decide instead (see below), and then even a USB
flash has to be signed with one of them.

Who holds which key:

* A developer's own build is signed with a key made in their checkout, so
  only devices they flashed from that checkout accept their updates.
* A published release is signed by the project's owner, with a key kept as a
  CI secret.
* The firmware registry neither signs nor checks images. It records a
  project's key fingerprint (`signingKeyId`) when the project gives one.
  When both that and the device's `running.key_fp` are known, a fleet manager
  can tell before downloading that a device without hardware Secure Boot
  would refuse the update. If either is missing, it can't.

`sdkconfig.d/espos.defaults` turns on *Require signed app images* with the
RSA scheme, and every build is signed with `secure_boot_signing_key.pem` in
the project folder. That file is **git-ignored and never committed**. When
it is missing, `espos_project_prologue()` makes a *development* key
(`espsecure generate_signing_key --version 2 --scheme rsa3072`) and warns
about it, so a fresh checkout builds. But a device flashed with a
development-key build only accepts updates signed with that same key.

**Every project folder gets its own key**, because the key lives next to the
project and is made when it is missing. espOS's own tree is one project, each
example under `components/*/examples/` is another, and so is every firmware
built on espOS. Build the same application in two folders and you get two
keys, and a device flashed from one refuses images from the other:

```
E (21719384) esp_image: Secure boot signature verification failed
W (21719545) esp_image: image valid, signature bad
E (21719546) esp_ota_ops: New image failed verification
E (21719547) espos_ota: install failed: image rejected: bad signature or
                        corrupt (ESP_ERR_OTA_VALIDATE_FAILED)
```

The device is doing its job: it refuses an image it wasn't told to trust.
But "signature bad" doesn't say which key it wanted. To find out, check the
image against the key you signed with:

```sh
espsecure verify-signature --version 2 --keyfile <key>.pem <image>.bin
```

Without the key file, `GET /api/v1/ota/status` names the key too.
`running.key_fp` is the first 16 hex characters of the SHA-256 digest of the
public key in the running image's first signature block. It is `null` when
the image has no signature, and also when signed updates are off, the scheme
isn't RSA or ECDSA v2, or an original ESP32 is below revision 3. An image
signed with more than one key, during a key change, reports the first. It is the same digest `espsecure` prints, so you can
compare it with an image or a key without a device:

```sh
espsecure signature-info-v2 <image>.bin          # "Public key digest for block 0: …"
espsecure digest-sbv2-public-key --keyfile <key>.pem -o digest.bin
od -An -tx1 digest.bin | tr -d ' \n' | cut -c1-16    # the digest file is raw bytes
```

The registry's `signingKeyId` uses the same fingerprint, so a fleet manager
can see before sending an update that it will be refused. That holds for
signed apps without hardware Secure Boot, where the running image's first
signature block is the key updates are checked against. With hardware
Secure Boot the keys in eFuse decide, and `key_fp` only names the key the
running image was signed with.

The fix is to name the key instead of letting each folder make one up. That
is what `SIGNING_KEY` below is for.

For anything you ship:

```sh
espsecure generate_signing_key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
# keep it outside the repo (password manager, CI secret) and copy it in for release builds
```

Or leave it where it is and name it:
`espos_project_prologue(... SIGNING_KEY /path/to/key.pem)`. The prologue
passes that path to ESP-IDF's signing step (`CONFIG_SECURE_BOOT_SIGNING_KEY`,
through a generated defaults file) and never makes a key there. If the named
key is missing, the build stops. Defaults only reach a fresh `sdkconfig`, so
if an existing one names a different key, the configure step stops too and
explains why; otherwise it would keep signing with the old key.

Every device that should take your updates must have been flashed over USB
at least once with a build carrying your key's public half. To change keys,
ship one update signed with the *old* key that already contains the *new*
public key. The original ESP32 needs chip revision 3 or later for the RSA
scheme (`sdkconfig.d/espos.defaults.esp32` sets `ESP32_REV_MIN_3`).

This is *signed apps without Secure Boot*. It protects against tampering
over the network, not against someone with the USB port. Hardware Secure
Boot and flash encryption are decisions for the release overlay
([Security](security.md)).

### Swapping the key

ESP-IDF only re-signs when the unsigned binary changes. So if you replace
`secure_boot_signing_key.pem` and rebuild, the build **keeps the old
signature**: no source changed, nothing re-links, and the log looks normal.
The root `CMakeLists.txt` takes a fingerprint of the key and forces a re-link
when it changes, so espOS handles this. If you copy that block into your own
project, copy the fingerprint part with it. Before flashing anything you
can't recover over the air, check it:

```sh
espsecure verify-signature --version 2 --keyfile <key>.pem build/<app>.bin
```

### Releasing, and forking

Every project built on espOS inherits signing, so this applies to them and
to anyone who forks one.

Ordinary builds need nothing. The development key is made automatically, so
pushes and pull requests build on a fork unchanged. **Publishing a release
is where you decide**, because that file is what other people install.

- **You run devices that take updates over the air.** Make a key, keep it,
  and give it to CI as a secret:

  ```sh
  espsecure generate-signing-key --version 2 --scheme rsa3072 signing_key.pem
  gh secret set <APP>_SIGNING_KEY_PEM < signing_key.pem
  ```

  If you lose that file, no device already flashed can ever take another
  update. It is the one thing worth backing up.

- **You just want a binary people can flash.** Release without a key, and
  say so in the release. Set the repository *variable*
  `ESPOS_ALLOW_UNSIGNED_RELEASE=true`, which `build-firmware.yml` and
  `release-firmware.yml` read. Your own workflow may use its own name, as the
  cockpit's `COCKPIT_ALLOW_UNSIGNED_RELEASE` does. The build then uses a
  throwaway key, and the release notes say USB only. Devices flashed from it
  refuse every update over the air, including later releases from the same
  fork.

When neither is set, a release workflow should **fail**, not quietly fall
back to a throwaway key. That fallback makes a release that installs fine
and then refuses every future update, and you only find out on the device,
long after CI went green.

There is deliberately **no committed default key**. A private key in a
public repository is a key everyone has, and a device trusting it would take
firmware signed by anyone. And if the safe path were opt-in, most forks
would ship that shared key without noticing.

## Manifest format (`schema: 1`)

```json
{
  "schema": 1,
  "app": "espos",
  "builds": [
    {"version": "0.6.1", "target": "esp32p4", "channel": "stable",
     "url": "espos-esp32p4-0.6.1.bin", "size": 1314816,
     "sha256": "…hex…", "notes": "M6", "date": "2026-08-18"},
    {"version": "0.7.0-beta.1", "target": "esp32p4", "channel": "beta",
     "url": "https://fw.example.org/espos/espos-esp32p4-0.7.0-beta.1.bin"}
  ]
}
```

* `schema` must be 1 when present; any other number rejects the manifest.
* `app` is optional, at the top or per build. When present it must equal
  the device's project name.
* Each build needs `version`, `target` and `url`. `target` is the ESP-IDF
  target name, compared exactly with the device's own (CI builds `esp32
  esp32s3 esp32c3 esp32c5 esp32c6 esp32p4`). `url` is absolute, or relative
  to the manifest's URL. A build without all three as strings is skipped,
  not reported.
* `channel` defaults to `stable`. `size`, `sha256`, `notes` and `date` are
  for information only; `sha256` isn't what protects the image, the
  signature is.
* The device picks the **highest version** among the builds that match its
  target, channel and app, and offers it when it is strictly newer than the
  running version. Versions compare as dotted numbers, and a `-prerelease`
  suffix sorts below the release (`1.2.10 > 1.2.9`, `1.0.0-beta.1 < 1.0.0`).
  Anything that doesn't parse compares as plain text. The running version is
  `version.txt` in the project folder (`PROJECT_VER`).
* The content type doesn't matter, but the manifest must be 16 KiB or less.

A boat-side update service only has to serve this file and the images;
static hosting is enough, and nothing in espOS ties it to one host. This is
the "firmware distribution service" the original plan left for later.

## Settings (`ota` namespace)

| Key | Default |
|---|---|
| `manifest_src` | `url` |
| `manifest_url` | |
| `manifest_path` | `/plugins/signalk-espos-updates/manifest.json` |
| `channel` | `stable` |
| `auto_check` | on |
| `check_h` | 24 |
| `auto_install` | off |
| `allow_insecure` | off |
| `confirm_tmo_s` | 600 |

### Finding updates through the Signal K server

With `manifest_src = "signalk"`, the device builds the manifest URL from the
server it is already connected to, by adding `manifest_path` to the server's
address.

That way **nothing has to be typed in per device**. A new device that finds
its server also finds its updates, and a server that moves doesn't leave
devices pointing at a URL that no longer works. It needs a plugin on the
server that serves a manifest at that path.

Two failures are reported as they are:

* **No server selected yet.** Normal at boot, before discovery has run.
  While `ota.auto_check` is on, the check runs again as soon as a server is
  selected.
* **This firmware has no Signal K client.** The setting asks for something
  the build can't do. It does *not* fall back to `manifest_url`, because
  quietly checking somewhere other than where you asked is worse than saying
  so.

`espos_sk` is an optional dependency of `espos_ota` for this. A firmware can
do updates without Signal K at all, and requiring it for one convenience
would be the wrong trade.

## API

`GET /api/v1/ota/status`, `POST /api/v1/ota/check`,
`POST /api/v1/ota {"url"?}`, `POST /api/v1/ota/confirm`,
`POST /api/v1/ota/rollback`, and the SSE event `ota`; see
[REST API](rest-api.md).

## Testing

* **Host:** `test/host/espos_ota_test` covers picking a build from a
  manifest, comparing versions and resolving URLs.
  `run_test.py OtaTests OtaRollbackTimeoutTests` runs on the simulator: a
  manifest check, an install with progress, rejected images (wrong project,
  bad signature, not an image, 404), a busy device, confirming once
  connected, rollback after the timeout, and rollback by hand.
* **Device** (done on the ESP32-P4 panel, 2026-08-18): flash 0.6.0 over USB.
  Serve `build/espos.bin` from a 0.6.1 build and a manifest with
  `python3 -m http.server`, and set `ota.manifest_url`. Check, see it
  available, install, and the device restarts into `ota_1` and logs
  `new image confirmed (network up)`. `build/espos-unsigned.bin` from the
  same build gives `image rejected`. A build made with
  `idf.py -DESPOS_BROKEN_BUILD=1 build` (it aborts in `app_main`) installs,
  starts, aborts, and the bootloader starts the old slot; the status then
  shows `rolled_back: true, other_version: 0.6.2`.
