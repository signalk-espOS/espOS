# espos_health

Device conditions (warn/alarm) and the sinks that consume them.

```sh
idf.py add-dependency "signalk-espos/espos_health^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_health` works with the same version of every other.

**Documentation:** [docs/health.md](https://github.com/signalk-espOS/espOS/blob/main/docs/health.md)

**Examples:**

* [`health_and_led`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_health/examples/health_and_led)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_health` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
