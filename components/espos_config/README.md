# espos_config

NVS config store, JSON-Schema descriptors, REST-backed settings.

```sh
idf.py add-dependency "signalk-espos/espos_config^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_config` works with the same version of every other.

**Documentation:** [docs/config.md](https://github.com/signalk-espOS/espOS/blob/main/docs/config.md)

**Examples:**

* [`custom_settings`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_config/examples/custom_settings)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_config` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
