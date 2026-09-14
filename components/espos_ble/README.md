# espos_ble

BLE gateway: bridges Bluetooth Low Energy devices to signalk-server's BLE provider API.

```sh
idf.py add-dependency "signalk-espos/espos_ble^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_ble` works with the same version of every other.

**Documentation:** [docs/ble.md](https://github.com/signalk-espOS/espOS/blob/main/docs/ble.md)

**Examples:**

* [`ble_gateway`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_ble/examples/ble_gateway)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_ble` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
