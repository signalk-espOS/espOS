# espos_sensors

Sensor drivers on the IDF 6 APIs -- ADC, GPIO, PCNT, LEDC, I2C, 1-Wire -- as espos::flow nodes.

```sh
idf.py add-dependency "signalk-espos/espos_sensors^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_sensors` works with the same version of every other.

**Documentation:** [docs/sensors.md](https://github.com/signalk-espOS/espOS/blob/main/docs/sensors.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_sensors` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
