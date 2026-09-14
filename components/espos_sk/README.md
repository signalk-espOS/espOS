# espos_sk

SignalK: mDNS discovery, access token, delta stream in and out.

```sh
idf.py add-dependency "signalk-espos/espos_sk^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_sk` works with the same version of every other.

**Documentation:** [docs/signalk.md](https://github.com/signalk-espOS/espOS/blob/main/docs/signalk.md)

**Examples:**

* [`analog_input`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/analog_input)
* [`digital_switch`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/digital_switch)
* [`json_and_meta`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/json_and_meta)
* [`listener_relay`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/listener_relay)
* [`pulse_counter`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/pulse_counter)
* [`tls_server`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_sk/examples/tls_server)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_sk` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
