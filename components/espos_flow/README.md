# espos_flow

The data-flow runtime: one loop task, a timer wheel, a mailbox, and a typed producer/consumer graph.

```sh
idf.py add-dependency "signalk-espos/espos_flow^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_flow` works with the same version of every other.

**Documentation:** [docs/flow.md](https://github.com/signalk-espOS/espOS/blob/main/docs/flow.md)

**Examples:**

* [`sensor_graph`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_flow/examples/sensor_graph)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_flow` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
