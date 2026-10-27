# Migrating from SensESP

This page is for people with a SensESP project who want the same device on
espOS. Most of what you know carries over: Signal K paths and SI units,
metadata for paths of your own, approving the device on the server, and the
shape of a sensor program (read, calibrate, publish). Three things change,
and the table further down maps SensESP's classes onto espOS one by one.

The tutorials start with [Your first sensor](tutorials/first-sensor.md), and
[Concepts](concepts.md) explains how espOS fits together.

## What changes

### The toolchain: ESP-IDF instead of PlatformIO and Arduino

SensESP builds with PlatformIO on the Arduino core. espOS is plain ESP-IDF
6.0, the release named in `.idf-version`. You get CMake projects, Kconfig and
`idf.py`, and you use the ESP-IDF drivers directly:

| Arduino | ESP-IDF |
|---|---|
| `analogReadMilliVolts()` | `esp_adc` |
| `digitalRead()` | `esp_driver_gpio` |
| `Wire` | `i2c_master` |
| an interrupt counter | `esp_driver_pcnt` |

`String` and ArduinoJson are gone; cJSON is there when you need JSON. The
espOS C API returns `esp_err_t`. Your own code can be C or C++. There is no
`setup()` and `loop()`: the entry point is `app_main()`, and a task that runs
forever has its own loop.

A project is a short root `CMakeLists.txt` plus `main/`
([Development](development.md#building-a-firmware-on-espos)). There is no
`platformio.ini`. The chip, flash size and partitions come from the espOS
prologue, and pins are constants in your code.

### Two ways to wire a device

**A graph, like SensESP.** `espos_flow` gives you producers, transforms and
outputs, joined with `connect_to()` or `>>`. `espos_sensors` adds inputs such
as `Analog` and `PulseCounter`, and `espos_sk_flow` adds the Signal K end:
`sk::Output`, `sk::Listener`, `sk::PutRequest` and `sk::PutHandler`. This is
the closest match to SensESP ([Data flow](flow.md)).

**Plain C.** Your task reads the sensor, does the arithmetic and calls
`espos_sk_publish_number()`. espOS batches, buffers while offline and
reconnects behind that call ([Signal K](signalk.md)).

The graph is built on the C calls, so both produce the same device. Use the
one that reads better to you, or mix them.

### Settings: declared once, not owned by objects

In SensESP each configurable object has a `config_path`, saves itself, and
`ConfigItem()` gives it a card in the web UI. In espOS a setting lives in one
place:

* **In a graph,** a transform with parameters puts them on the settings page
  itself. `Linear::register_config()` is the `ConfigItem(linear)` of espOS.
* **In plain C,** a JSON descriptor per namespace, added in `CMakeLists.txt`
  with `espos_config_add_descriptor(config/<ns>.json)`. The build makes the
  key constants (`ESPOS_CFG_<NS>_<KEY>`) and the settings page. Code reads a
  value with `espos_config_get_*()` and follows changes with
  `espos_config_subscribe()` ([Configuration](config.md)).

Title, description and `restart_required` (SensESP's
`set_requires_restart()`) live in the descriptor. There is no sort order;
namespaces appear by name.

## Symbol by symbol

| SensESP | espOS graph (C++) | espOS plain C |
|---|---|---|
| `SensESPAppBuilder` … `get_app()` | `espos_start(nullptr)`, then `Graph::start()` | `espos_start(NULL)`; options in `espos_start_opts_t` |
| `set_hostname("x")` | the `net.hostname` setting | same |
| `set_wifi_client(ssid, psk)` | never in code: the setup portal, or an NVS image ([WiFi](wifi.md)) | same |
| `set_sk_server(host, port)` | `sk.server_host` and `sk.server_port`; left empty, the server is found over mDNS | same |
| `SetupLogging()` | nothing: `espos_start()` sets up logging; change the level with `PUT /api/v1/logs/level` | same |
| `onRepeat()` / `onDelay()` | `Ticker`, `Poll<T>` | `espos_flow_every()` / `espos_flow_after()`, or a FreeRTOS task |
| `RepeatSensor<T>` | `Poll<T>` | a loop: read, compute, `espos_sk_publish_*()` |
| `AnalogInput` | `sensors::Analog` (volts, calibrated) | `esp_adc` oneshot ([example](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/analog_input)) |
| `DigitalInputCounter` / `Change` / `State` | `PulseCounter` / `GpioChange` / `GpioState` | `esp_driver_pcnt` / a GPIO interrupt and a queue / `gpio_get_level()` |
| `DigitalOutput` | `GpioOutput` | `gpio_set_level()` |
| `Linear` / `MovingAverage` / `Frequency` | `Linear` / `MovingAverage` / `Frequency` | a multiply-add / a ring buffer / counts over time |
| `Hysteresis` / `FloatThreshold` / `LambdaTransform` | `Hysteresis` / `Threshold` / `Lambda` | an `if` |
| `CurveInterpolator` / `Median` / `Debounce` / `Throttle` | `Curve` / `Median` / `Debounce` / `Throttle` | your own C |
| `Join` / `Zip` | `Join` | your own C |
| `SKOutputFloat` and friends | `sk::Output<T>` | `espos_sk_publish_number()` / `_bool()` / `_string()` / `_json()` |
| `SKMetadata` | `sk::Meta`, passed to `sk::Output` for a path of your own | `espos_sk_declare_meta()` |
| `SKValueListener<T>` | `sk::Listener<T>` | `espos_sk_subscribe()` |
| `SKPutRequest<T>` | `sk::PutRequest<T>` | `espos_sk_put()` |
| `SKPutRequestListener<T>` | `sk::PutHandler<T>` | `espos_sk_put_handler_register()` |
| `ConfigItem(obj)` | `register_config()` on the node | a key in the namespace's descriptor |
| `SystemStatusLed` | an `espos_health` sink driving a GPIO ([example](https://github.com/signalk-espOS/espOS/tree/main/components/espos_health/examples/health_and_led)) | same |
| `SensESPMinimalAppBuilder` | `espos_init()` without `espos_start_network()` | same |
| `StatusPageItem` / `UIButton` | your own endpoints with `espos_httpd_register()` and a page tab ([Web UI](ui.md)) | same |
| `enable_system_info_sensors()` | built in: `espos.<hostname>.*` every `sk.health_s` | same |
| `enable_ota(password)` | built in: signed images with rollback ([OTA](ota.md)); the signature replaces the password | same |
| `enable_wifi_watchdog()` | built in: `espos_health` ([Device health](health.md)) | same |
| `set_admin_user()` | the `httpd.api_key` setting ([Security](security.md)) | same |

## A worked migration: `analog_input`

SensESP's example reads a light sensor every 500 ms, calibrates it with a
`Linear` you can change in the web UI, and sends the result to Signal K.
Without comments it reads:

```c++
#include "sensesp/sensors/sensor.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/transforms/linear.h"
#include "sensesp_app_builder.h"
using namespace sensesp;

void setup() {
  SetupLogging();
  SensESPAppBuilder builder;
  sensesp_app = builder.get_app();
  auto* analog_input = new RepeatSensor<float>(500, []() {
    return analogReadMilliVolts(34) / 1000.;
  });
  auto* cal = new Linear(1.7007, -0.1650, "/indoor_illuminance/linear");
  ConfigItem(cal)->set_title("Input Calibration")->set_description("Analog input value adjustment.");
  analog_input->connect_to(cal)->connect_to(new SKOutputFloat(
      "environment.indoor.illuminance", "", new SKMetadata("ratio", "Indoor light")));
}
void loop() { event_loop()->tick(); }
```

### As an espOS graph

```c++
#include "espos.h"
#include "espos_flow/flow.hpp"
#include "espos_flow/transforms.hpp"
#include "espos_sensors/sensors.hpp"
#include "espos_sk_flow/sk.hpp"

using namespace espos::flow;
using namespace espos::sensors;

static Graph g;

extern "C" void app_main()
{
    ESP_ERROR_CHECK(espos_start(nullptr));

    auto& light = g.make<Analog>("light", 4, 500);      // GPIO 4, every 500 ms, in volts
    auto& cal   = g.make<Linear<float>>("cal", 1.7007f, -0.165f);
    auto& out   = g.make<espos::sk::Output<float>>("environment.inside.illuminance");

    cal.register_config("Input Calibration");           // ConfigItem(cal)
    light >> cal >> out;

    light.start();
    ESP_ERROR_CHECK(g.start());
}
```

The [`sensor_graph`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_flow/examples/sensor_graph)
example builds the same chain for every chip, with a stand-in reading instead
of the ADC.

### In plain C

The plain C version is `components/espos_sk/examples/analog_input`, cut down
here to the parts that match. The real example also handles the original
ESP32's calibration scheme, averages 16 samples and logs. The `Linear`'s two
numbers become settings in a descriptor, `main/config/app.json`. The example
ships `1.0` and `0.0`; a port keeps SensESP's numbers:

```json
{"namespace": "app", "version": 1, "title": "Analog input",
 "keys": [
  {"name": "multiplier", "type": "float", "default": 1.7007, "title": "Multiplier"},
  {"name": "offset", "type": "float", "default": -0.165, "title": "Offset", "description": "Analog input value adjustment."},
  {"name": "period_ms", "type": "int", "default": 500, "min": 100, "max": 60000, "unit": "ms", "title": "Sample period"}]}
```

It is registered in `main/CMakeLists.txt`, next to the ESP-IDF ADC driver:

```cmake
idf_component_register(SRCS main.c PRIV_REQUIRES espos_core espos_config espos_sk esp_adc)
espos_config_add_descriptor(config/app.json)
```

`main/main.c` is the whole program. `RepeatSensor` becomes the loop, `Linear`
one multiply-add and `SKOutputFloat` one publish call:

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "espos.h"
#include "espos_cfg_keys.h"
#include "espos_config.h"
#include "espos_sk.h"

#define ADC_GPIO 4 /* ADC1 on the ESP32-C6 DevKitC (GPIO 0..6); the example uses 20 on the ESP32-P4 */
#define SK_PATH  "environment.inside.illuminance" /* a spec path: the server owns its meta */

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static float s_multiplier = 1.7007f, s_offset = -0.165f; /* word-sized: written on the HTTP task, read by the loop */
static int32_t s_period_ms = 500;

/* Initial load and the change callback (runs on the writer's task, usually an HTTP handler). */
static void load_cfg(const char *ns, const char *key, void *arg)
{
    (void)ns; (void)key; (void)arg;
    espos_config_get_float(ESPOS_CFG_NS_APP, ESPOS_CFG_APP_MULTIPLIER, &s_multiplier);
    espos_config_get_float(ESPOS_CFG_NS_APP, ESPOS_CFG_APP_OFFSET, &s_offset);
    espos_config_get_i32(ESPOS_CFG_NS_APP, ESPOS_CFG_APP_PERIOD_MS, &s_period_ms);
}

static void adc_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ADC_GPIO, &unit, &s_chan));
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unit };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &s_adc));
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &ccfg));
    adc_cali_curve_fitting_config_t cal = { .unit_id = unit, .chan = s_chan, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cal, &s_cali)); /* eFuse data: what analogReadMilliVolts() used */
}

void app_main(void)
{
    ESP_ERROR_CHECK(espos_start(NULL));                 /* WiFi, portal, token, stream, UI, OTA */
    load_cfg(NULL, NULL, NULL);
    ESP_ERROR_CHECK(espos_config_subscribe(load_cfg, NULL));
    adc_init();
    for (;; vTaskDelay(pdMS_TO_TICKS(s_period_ms))) {   /* RepeatSensor<float>(500, …) */
        int raw, mv;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, s_chan, &raw));
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            espos_sk_publish_number(SK_PATH, mv / 1000.0f * s_multiplier + s_offset); /* Linear → SKOutputFloat */
        }
    }
}
```

One change is on purpose. SensESP's `environment.indoor.illuminance` is
not in the Signal K specification, which is why its example had to send
`SKMetadata("ratio", …)`. The espOS versions publish the specification path
`environment.inside.illuminance`, whose units the server already knows. To
keep a path of your own, declare its metadata: `sk::Meta` in the graph, or
one call before the loop in C:

```c
espos_sk_declare_meta(SK_PATH, "{\"units\":\"ratio\",\"displayName\":\"Indoor light\"}", s_period_ms);
```

WiFi, hostname and server are settings, entered once in the setup portal or
the web page. The calibration is changed on the **Config** page, or with
`PUT /api/v1/config {"app":{"multiplier":2}}`, and reaches the loop through
`load_cfg`. None of this code deals with WebSockets, tokens or reconnects.

## What has no equivalent yet

* **Arduino driver libraries** (Adafruit, SparkFun and so on) don't compile
  on ESP-IDF. Look on the ESP Component Registry first
  (`idf.py add-dependency "<namespace>/<name>"`). Otherwise port the driver:
  an I²C breakout is usually a few register reads. With ESP-IDF 6's
  `i2c_master`, call `i2c_new_master_bus()` once,
  `i2c_master_bus_add_device()` per address, then
  `i2c_master_transmit_receive()` to read a register and
  `i2c_master_transmit()` to write one. Keep the library's formulas and drop
  the rest. Run the bus from your own task, never from an espOS callback.
* **A permanent access point.** SensESP keeps its own WiFi network up next
  to the boat's. espOS starts the setup network only when no network is set
  up, or after `wifi.portal_after_s` without a connection, and stops it once
  the device joins ([WiFi](wifi.md)).

## What espOS adds

* **Signed updates with rollback.** Every image is signed and checked on the
  device. A new image has to confirm itself once it is on the network, or
  the device goes back to the previous one. A `manifest.json` on any web
  server works as an update channel ([OTA](ota.md),
  [tutorial](tutorials/ota-from-a-manifest.md)), and the
  [manager plugin](https://github.com/signalk-espOS/signalk-espos-manager)
  does it for you from the Signal K server.
* **A sturdier Signal K login.** The token is tied to the server's identity,
  so a server that changes address keeps working. A pending request survives
  a reboot, and a denied one can be requested again ([Signal K](signalk.md)).
* **WiFi errors with a reason**, such as "wrong password" or "network not in
  range", up to four networks in priority order, and a watchdog for the
  ESP32-P4's radio chip ([WiFi](wifi.md)).
* **Device health:** warnings and alarms, low-memory checks and a watchdog
  that never restarts the device just because the network is gone
  ([Device health](health.md)).
* **Host tests** for the state machines, parsers and REST server, run on a
  PC without hardware ([Development](development.md)).
* **One tree for six chips:** ESP32, S3, C3, C5, C6 and P4.
* **Ethernet** on chips with a built-in Ethernet MAC, such as the ESP32-P4
  ([Networking](net.md)).
* Smaller things: the device log and core dump over the network
  ([tutorial](tutorials/logs-and-core-dumps.md)), live updates in the web
  page instead of polling, config export and import, and the `_espos._tcp`
  mDNS record the manager uses to find devices.
