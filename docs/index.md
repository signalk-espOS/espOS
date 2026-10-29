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

These pages are for people writing firmware. If you want to put a ready-made
firmware on a board, you don't need them: install
[signalk-espos-manager](https://github.com/signalk-espOS/signalk-espos-manager)
from the Signal K Appstore and use its **New board** page, or the
[web flasher](https://signalk-espos.github.io/signalk-espos-manager/flash/).

signalk-espOS is a community project, not part of Signal K. It is written
with AI assistance; Dirk Wahrheit reviews and merges every change and runs the
devices on his own boat. The source is at
[github.com/signalk-espOS/espOS](https://github.com/signalk-espOS/espOS)
under the Apache-2.0 license.

## Try it

You need ESP-IDF 6.0, the release named in `.idf-version`, a board with an
ESP32, -S3, -C3, -C5, -C6 or -P4, and a Signal K server on the same network.

```sh
. $IDF_PATH/export.sh
git clone https://github.com/signalk-espOS/espOS.git
cd espOS/components/espos_core/examples/minimal
idf.py set-target esp32c6            # or your chip
idf.py build flash monitor
```

The serial monitor tells you what to do next: join the `espOS-xxxx` WiFi
network, pick your boat's network at `http://192.168.4.1`, then approve the
device in Signal K under **Security → Access Requests**. After that the
device's own page is at `http://<hostname>.local` and its value shows in the
server's Data Browser. [Getting started](getting-started.md) goes through it
step by step.

Your application starts espOS with one call and then does its own work:

```c
#include "espos.h"

void app_main(void)
{
    ESP_ERROR_CHECK(espos_start(NULL));
    /* read your sensor, then espos_sk_publish_number(...) */
}
```

## Where to go next

* **New to espOS:** [Getting started](getting-started.md), then
  [Concepts](concepts.md), the [examples](examples.md) and the
  [tutorials](tutorials/first-sensor.md).
* **Coming from SensESP:** [Migrating from SensESP](migration-from-sensesp.md),
  and [Hardware](hardware.md) for the boards you already have.
* **Something is wrong:** [Troubleshooting](troubleshooting.md), sorted by
  symptom.
* **Looking something up:** the [REST API](rest-api.md) and the
  [C API](api-c.md).
* **Changing espOS itself:** [Development](development.md), the
  [decisions](decisions.md) so far and the [changelog](changelog.md).

## Components

Each part is its own ESP-IDF component, so a firmware only pulls in what it
uses. `espos_start()` brings up the core, plus Signal K, updates and the
Bluetooth gateway when they are in the build. The application starts the rest,
such as NMEA 2000, when its board has the hardware.

| Component | What it does | Docs |
|---|---|---|
| `espos_core` | `espos_start()`, which brings everything up | [concepts](concepts.md) |
| `espos_config` | Settings stored on the device, with a generated settings page | [config](config.md) |
| `espos_httpd` | The device's web server, REST API and web UI | [rest-api](rest-api.md), [ui](ui.md) |
| `espos_net` | Network status, mDNS and the device id, whatever the link | [net](net.md) |
| `espos_wifi` | WiFi and the setup portal | [wifi](wifi.md) |
| `espos_eth` | Ethernet over the chip's own EMAC | [net](net.md) |
| `espos_log` | The device log, readable over the network | [rest-api](rest-api.md) |
| `espos_health` | Warnings and alarms about the device itself | [health](health.md) |
| `espos_event` | Events other components post: network up, server found, … | [concepts](concepts.md) |
| `espos_time` | The device clock, so data sent late keeps its real time | [time](time.md) |
| `espos_sk` | Signal K: finding the server, the access token, sending and receiving data | [signalk](signalk.md) |
| `espos_ota` | Signed updates with rollback | [ota](ota.md) |
| `espos_flow`, `espos_sk_flow` | Wiring sensors to Signal K in a few lines, SensESP style | [flow](flow.md) |
| `espos_sensors` | ADC, GPIO, pulse counter, PWM, I2C, 1-Wire inputs | [sensors](sensors.md) |
| `espos_formulas` | Marine maths: tank curves, dew point, densities | [transforms](transforms.md) |
| `espos_devices` | Ready-made devices such as a tank level or engine RPM sender | [devices](devices.md) |
| `espos_power` | Deep sleep for battery devices | [power](power.md) |
| `espos_prov` | WiFi setup from a phone over Bluetooth | [provisioning](provisioning.md) |
| `espos_ble` | Bluetooth gateway for Signal K's BLE provider API | [ble](ble.md) |
| `espos_n2k` | NMEA 2000 over the CAN controller | [n2k](n2k.md) |
| `espos_nmea0183` | NMEA 0183 from a GPS, compass or sounder | [nmea0183](nmea0183.md) |
| `espos_sdlog` | Logging to a microSD card that survives a power cut | [sdlog](sdlog.md) |
| `espos_usb_storage` | A microSD card shared with a chartplotter over USB | [usb-storage](usb-storage.md) |
| `espos_audio`, `espos_voice` | A voice satellite with wake word | [voice](voice.md) |
| `espos_tone` | Short event tunes (RTTTL) for a buzzer: boot, relay, button chirps | [tone](tone.md) |

Board-specific code, such as a display driver or pin map, stays in the
firmware that knows the board.
