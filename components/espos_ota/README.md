# espos_ota

Signed OTA with rollback, from a URL or a version manifest.

```sh
idf.py add-dependency "signalk-espos/espos_ota^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_ota` works with the same version of every other.

**Documentation:** [docs/ota.md](https://github.com/signalk-espOS/espOS/blob/main/docs/ota.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_ota` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
