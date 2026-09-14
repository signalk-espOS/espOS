# espos_log

Log ring served over REST, so a device is debuggable without a serial cable.

```sh
idf.py add-dependency "signalk-espos/espos_log^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_log` works with the same version of every other.

**Documentation:** [docs/rest-api.md](https://github.com/signalk-espOS/espOS/blob/main/docs/rest-api.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_log` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
