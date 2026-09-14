# espos_event

Event bus: espOS lifecycle events (network, SignalK, OTA) on the default esp_event loop.

```sh
idf.py add-dependency "signalk-espos/espos_event^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_event` works with the same version of every other.

**Documentation:** [docs/concepts.md](https://github.com/signalk-espOS/espOS/blob/main/docs/concepts.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_event` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
