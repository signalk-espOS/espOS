# Getting started

This page takes you from a fresh clone to a device that sends a value to
Signal K. Installing ESP-IDF and the first build take the most time. The
espOS part is a few commands and two clicks in the server.

## What you need

* **ESP-IDF 6.0.x.** espOS is tested on the release named in
  `.idf-version`. Another 6.0.x release builds with a warning, and anything
  outside 6.0 is refused ([Troubleshooting](troubleshooting.md#build)).
  Install it with the [Espressif Installation Manager](https://docs.espressif.com/projects/idf-im-ui/en/latest/)
  or the [classic guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html).
  Then export it in every shell you build from. The installer prints the
  exact line, which looks like this:

  ```sh
  . $IDF_PATH/export.sh
  ```

* **A board** with an ESP32, ESP32-S3, ESP32-C3, ESP32-C5, ESP32-C6 or
  ESP32-P4 ([Hardware](hardware.md)). The commands below say `esp32c6`; use
  your chip instead.
* **A Signal K server** 2.x on the same network, with security on, which is
  the default. espOS is tested against 2.31. Without security the device
  sends data without a token and you can skip step 4.
* **git.** You don't need Node, because the web UI is committed already, and
  ESP-IDF brings its own Python.

## 1. Build the minimal example

```sh
git clone https://github.com/signalk-espOS/espOS.git
cd espOS/components/espos_core/examples/minimal
idf.py set-target esp32c6
idf.py build flash monitor
```

Every example is a complete ESP-IDF project, so `idf.py` works inside its
folder as usual. The first build prints two lines you can ignore. One says a
development signing key was made, since every image is signed
([OTA](ota.md#signing-key)). The other names the partition table,
`…/partitions/4mb.csv`.

If `flash` cannot find the board, add `-p /dev/ttyACM0` (or `/dev/ttyUSB0`).
On a small or shared machine, build with `../../../../scripts/build.sh build`
instead of `idf.py build`. It caps the job count so the machine stays usable
([Development](development.md)).

## 2. Read the monitor

After ESP-IDF's own boot lines, the device tells you what it is doing:

```text
I (612) espos: espOS 0.7.0 on esp32c6 — app minimal 0.7.0-12-gabc1234
I (702) espos_wifi: no network configured: join "espOS-1a2b" and open http://192.168.4.1
I (710) minimal: publishing environment.inside.temperature every second
```

The first line names the espOS version, the chip, and your application's
version from `git describe`. The second says there is no WiFi set up yet, so
the setup network is already on. The third comes from the application. It
publishes straight away; espOS holds the values until it reaches the server
and then sends them in order.

## 3. Pick your WiFi

On a phone or laptop, join the open WiFi network **`espOS-1a2b`**. The four
characters come from the device's MAC address, so every device has its own.
Most phones open a sign-in page by themselves. If yours doesn't, open
`http://192.168.4.1`. Pick your network, enter its password and save.

The setup network goes away as soon as the device joins yours, so your phone
drops it. That is expected. The monitor carries on:

```text
I (9310) espos_wifi: connected to "Boat" as 192.168.1.23 — web UI: http://espos-1a2b.local
I (9420) espos_sk: found signalk-server "boat" at 192.168.1.10:3000 (discovered urn:mrn:signalk:uuid:…)
I (9650) espos_sk: access requested — approve it in the server UI: Security → Access Requests
```

The device found the server over mDNS and asked it for access. Its own web
page is now at `http://espos-1a2b.local`, with status, WiFi, Signal K,
settings, logs and updates.

## 4. Approve the device in Signal K

In the server's admin UI, open **Security → Access Requests**. The request
shows the application name and hostname, here `minimal espos-1a2b`. Give it
**read/write**, which it needs to send data, and approve it. The device
checks back and prints:

```text
I (17040) espos_sk: approved, streaming
```

The device keeps the token across reboots and new firmware. A factory reset
or `POST /api/v1/sk/forget` deletes it. If you denied it by mistake, use
**Request again** on the device's Signal K page
([Troubleshooting](troubleshooting.md#signal-k)).

## 5. Find the value

In the server's **Data Browser**, look under `vessels.self` for
`environment.inside.temperature`. It reads `293.65`, refreshed every second.
Signal K stores SI units, so that is kelvin, or 20.5 °C. The source column
reads `espos.<hostname>`, so you can tell devices apart.

The example's `main.c` is 40 lines, and the constant is where your sensor
reading would go. Every espOS device also publishes its own health every 10
seconds under `espos.<hostname>.*`: uptime, free memory, WiFi signal and
reconnects ([Signal K](signalk.md#delta-stream-m4)).

## Set a password before the boat leaves the marina

The web page and REST API are open at first, so setting up needs no
password. Once the device works, set `httpd.api_key` on the **Config** page.
**Generate** makes a 20-character key and shows it once. After that the page
asks for it and scripts send `Authorization: Bearer <key>`. The setup network
never asks, so you cannot lock yourself out ([Security](security.md)).

## Next steps

* **Add a setting of your own.** One entry in a JSON file becomes a stored
  value and a field on the settings page:
  [Add a setting](tutorials/add-a-setting.md), example `custom_settings`.
* **Read a real sensor** and publish it with `espos_sk_publish_number()`:
  [Your first sensor](tutorials/first-sensor.md), example `analog_input`.
* **Browse the other examples**, one idea each: [Examples](examples.md).
* **Learn how it fits together** before you write your first callback:
  [Concepts](concepts.md) covers the start order and which task calls you.
* **Coming from SensESP?** [Migrating from SensESP](migration-from-sensesp.md).
