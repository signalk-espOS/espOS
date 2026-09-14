# espos_httpd

HTTP server, REST API, SSE, the web UI from a LittleFS partition.

```sh
idf.py add-dependency "signalk-espos/espos_httpd^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_httpd` works with the same version of every other.

**Documentation:** [docs/rest-api.md](https://github.com/signalk-espOS/espOS/blob/main/docs/rest-api.md)

**Examples:**

* [`app_endpoint_and_page`](https://github.com/signalk-espOS/espOS/tree/main/components/espos_httpd/examples/app_endpoint_and_page)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_httpd` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
