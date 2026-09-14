# espos_sk_flow

Signal K nodes for the espos::flow graph: publish, subscribe, and inbound PUT control.

```sh
idf.py add-dependency "signalk-espos/espos_sk_flow^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_sk_flow` works with the same version of every other.

**Documentation:** [docs/signalk.md](https://github.com/signalk-espOS/espOS/blob/main/docs/signalk.md)

**Examples:**

* [`dusk_relay`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk_flow/examples/dusk_relay)
* [`smart_switch`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk_flow/examples/smart_switch)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_sk_flow` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
