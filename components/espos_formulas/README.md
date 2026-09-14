# espos_formulas

Marine arithmetic for Signal K devices: SI unit conversions, curve interpolation, dew point, heat index, resistive senders, battery state of charge.

```sh
idf.py add-dependency "signalk-espos/espos_formulas^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_formulas` works with the same version of every other.

**Documentation:** [docs/transforms.md](https://github.com/signalk-espOS/espOS/blob/main/docs/transforms.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_formulas` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
