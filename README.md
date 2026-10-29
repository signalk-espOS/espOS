# espOS

espOS is a firmware base for ESP32 boards that talk to Signal K. It does the
parts every such device needs: joining the boat's WiFi, a settings page in
the browser, finding the Signal K server and getting approved, sending and
receiving data, and installing signed updates over the air. You write the
part that reads your sensor or drives your display.

Despite the name, espOS is not an operating system. The ESP32 already runs
FreeRTOS, which comes with Espressif's ESP-IDF. espOS is a set of ESP-IDF
components that your firmware links in, much like SensESP, but built on plain
ESP-IDF 6 instead of Arduino and PlatformIO.

## Why it exists

I wanted ESP32 devices on Signal K that a boat owner can use without being a
developer. So espOS comes with three things around it:

* a [web flasher](https://signalk-espos.github.io/signalk-espos-manager/flash/)
  that puts firmware on a new board from the browser,
* a [registry](https://github.com/signalk-espOS/registry) that lists the
  firmware ready to install, sorted by what you want the board to do,
* the [signalk-espos-manager](https://github.com/signalk-espOS/signalk-espos-manager)
  plugin, which finds the devices on your boat and keeps them up to date over
  the air.

signalk-espOS is a community project, not part of Signal K. It is written
with AI assistance; I review and merge every change and run the devices on my
own boat.

Dirk Wahrheit

## If you own a boat

You don't need to build anything. Install **signalk-espos-manager** from the
Signal K Appstore, open its **New board** page, plug in a board and pick what
it should do. The manager's [README](https://github.com/signalk-espOS/signalk-espos-manager#readme)
walks through it.

## If you write firmware

You need ESP-IDF 6.0, the release named in [`.idf-version`](.idf-version),
and a board with an ESP32, ESP32-S3, ESP32-C3, ESP32-C5, ESP32-C6 or
ESP32-P4.

```sh
. $IDF_PATH/export.sh
git clone https://github.com/signalk-espOS/espOS.git
cd espOS/components/espos_core/examples/minimal
idf.py set-target esp32c6            # or your chip
idf.py build flash monitor
```

On a small or shared machine, build with `../../../../scripts/build.sh build`
instead of `idf.py build`. It caps the job count so the machine stays usable.

The serial monitor tells you what to do next: join the `espOS-xxxx` WiFi
network, pick your boat's network at <http://192.168.4.1>, then approve the
device in Signal K under **Security → Access Requests**.
[Getting started](docs/getting-started.md) goes through it step by step.

The examples sign their images with a development key made on the first
build. That is fine on the bench. Before a device goes on a boat, read
[OTA and signing](docs/ota.md): a device only takes updates signed with the
key it was flashed with.

Your application starts espOS with one call and then does its own work:

```c
#include "espos.h"

void app_main(void)
{
    ESP_ERROR_CHECK(espos_start(NULL));
    /* read your sensor, then espos_sk_publish_number(...) */
}
```

The components are also published on the [ESP Component
Registry](https://components.espressif.com/components?q=signalk-espos) as
`signalk-espos/espos_*`, so a firmware can use them without cloning this
repository. [Development](docs/development.md#using-the-components-from-the-registry)
lists the three files such a project needs.

The documentation is at
**[signalk-espos.github.io/espOS](https://signalk-espos.github.io/espOS/)**.
Coming from SensESP? Start with [Migrating from SensESP](docs/migration-from-sensesp.md).

## Components

Each part is its own ESP-IDF component, so a firmware only pulls in what it
uses. `espos_start()` brings up the core, plus Signal K, updates and the
Bluetooth gateway when they are in the build. The application starts the rest,
such as NMEA 2000, when its board has the hardware.

| Component | What it does | Docs |
|---|---|---|
| `espos_core` | `espos_start()`, which brings everything up | [concepts](docs/concepts.md) |
| `espos_config` | Settings stored on the device, with a generated settings page | [config](docs/config.md) |
| `espos_httpd` | The device's web server, REST API and web UI | [rest-api](docs/rest-api.md), [ui](docs/ui.md) |
| `espos_net` | Network status, mDNS and the device id, whatever the link | [net](docs/net.md) |
| `espos_wifi` | WiFi and the setup portal | [wifi](docs/wifi.md) |
| `espos_eth` | Ethernet over the chip's own EMAC | [net](docs/net.md) |
| `espos_log` | The device log, readable over the network | [rest-api](docs/rest-api.md) |
| `espos_health` | Warnings and alarms about the device itself | [health](docs/health.md) |
| `espos_event` | Events other components post: network up, server found, … | [concepts](docs/concepts.md) |
| `espos_time` | The device clock, so data sent late keeps its real time | [time](docs/time.md) |
| `espos_sk` | Signal K: finding the server, the access token, sending and receiving data | [signalk](docs/signalk.md) |
| `espos_ota` | Signed updates with rollback | [ota](docs/ota.md) |
| `espos_flow`, `espos_sk_flow` | Wiring sensors to Signal K in a few lines, SensESP style | [flow](docs/flow.md) |
| `espos_sensors` | ADC, GPIO, pulse counter, PWM, I2C, 1-Wire inputs | [sensors](docs/sensors.md) |
| `espos_formulas` | Marine maths: tank curves, dew point, densities | [transforms](docs/transforms.md) |
| `espos_devices` | Ready-made devices such as a tank level or engine RPM sender | [devices](docs/devices.md) |
| `espos_power` | Deep sleep for battery devices | [power](docs/power.md) |
| `espos_prov` | WiFi setup from a phone over Bluetooth | [provisioning](docs/provisioning.md) |
| `espos_ble` | Bluetooth gateway for Signal K's BLE provider API | [ble](docs/ble.md) |
| `espos_n2k` | NMEA 2000 over the CAN controller | [n2k](docs/n2k.md) |
| `espos_nmea0183` | NMEA 0183 from a GPS, compass or sounder | [nmea0183](docs/nmea0183.md) |
| `espos_sdlog` | Logging to a microSD card that survives a power cut | [sdlog](docs/sdlog.md) |
| `espos_usb_storage` | A microSD card shared with a chartplotter over USB | [usb-storage](docs/usb-storage.md) |
| `espos_audio`, `espos_voice` | A voice satellite with wake word | [voice](docs/voice.md) |
| `espos_tone` | Short event tunes (RTTTL) for a buzzer: boot, relay, button chirps | [tone](docs/tone.md) |

Board-specific code, such as a display driver or pin map, stays in the
firmware that knows the board.

## Status

espOS is before 1.0, so a minor release can still change the API. The core is
in daily use on ESP32-P4 panels with signalk-server 2.31. CI runs the host
tests and builds for every supported chip on each change. What comes next is
in the [roadmap](docs/roadmap.md), and the decisions so far are in
[decisions](docs/decisions.md).

## How the project works

* **Anyone can publish firmware.** A project needs one entry in the
  registry and no credentials. A maintainer reviews new entries.
* **Fixes go into espOS**, so every firmware built on it gets them.
* **Updates are signed.** A device only accepts firmware signed with the key
  it was first flashed with, so a project keeps one key for all its releases.

Contributions are welcome; [CONTRIBUTING.md](CONTRIBUTING.md) says how.

## License

Apache License 2.0 ([LICENSE](LICENSE)). Contributions need a
`Signed-off-by` line (Developer Certificate of Origin). Third-party code keeps
its own license; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
