# espos_nmea0183

NMEA 0183 for espOS: sentences off a UART, checked and decoded into Signal K units, and published to Signal K — a GPS, a compass or a depth sounder on any espOS device.

```sh
idf.py add-dependency "signalk-espos/espos_nmea0183^0.19.1"  # x-release-please-version
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_nmea0183` works with the same version of every other.

**Documentation:** [docs/nmea0183.md](https://github.com/signalk-espOS/espOS/blob/main/docs/nmea0183.md)

**Examples:**

* [`nmea0183_gps`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_nmea0183/examples/nmea0183_gps)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_nmea0183` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
