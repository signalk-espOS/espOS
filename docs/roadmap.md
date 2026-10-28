# Roadmap

espOS is being built as the ESP-IDF successor to SensESP for Signal K
devices. The work was cut into tranches, and each tranche had a gate that was
checked before the next one started. Dates are intentions, not promises.

| Tranche | Theme | State |
|---|---|---|
| T0 | Foundation: Apache-2.0, registry manifests, contributor tooling, CI gates | done (0.7) |
| T1 | Builds and boots the same for everyone: `espos_start()`, inherited sdkconfig, health policy, HTTP client, mDNS responder, ABI rules | done (0.7) |
| T2 | A stranger reaches a live value: examples, template project, documentation site, SensESP migration guide, tutorials | done (0.7) |
| T3 | Platform spine: `espos_net` (Ethernet, static IP), `espos_time` (SNTP, delta timestamps), TLS trust store (TOFU or private CA, `sk.scheme auto`), REST authentication | done |
| T4 | Data flow: `espos_flow` (producer, consumer and transform graph with a C++ facade), settings registered at runtime, Signal K nodes with inbound PUT handlers, sensors on IDF 6 drivers, marine formulas | done |
| T5 | Devices, registry, fleet: device classes (`espos_devices`), registry publishing, the reusable release workflow, the manager plugin that updates the boat | done (0.14) |
| T6 | Hardware-gated: BLE provisioning, Ethernet boards, deep-sleep duty cycle, Web Serial flasher, Thread spike | mostly done, see below |

## T6 in detail

* **BLE provisioning** (`espos_prov`): done and verified on an ESP32-C5.
* **Ethernet** (`espos_eth`): done for the internal EMAC with an RMII PHY,
  such as the Waveshare ESP32-P4 PoE board. SPI Ethernet chips like the
  W5500 are not supported yet, so the Ethernet port of the Waveshare
  ESP32-S3 8-relay board in the registry goes unused for now.
* **Deep-sleep duty cycle** (`espos_power`): done.
* **Web Serial flasher**: done. It lives in signalk-espos-manager and is
  hosted on the
  [project pages](https://signalk-espos.github.io/signalk-espos-manager/flash/).
* **Thread spike**: open. `espos_net` already ranks a Thread route after
  Ethernet and WiFi (`ESPOS_NET_IF_THREAD`), but no build brings up
  OpenThread yet. It needs a C5, C6 or H2 on the bench.

What still gates T6 is hardware: most registry boards are built by CI but
have not been run on a bench yet.

## What comes next

Open work is tracked as
[issues](https://github.com/signalk-espOS/espOS/issues). What "done" meant
for each tranche, and the decisions behind the order, are in
[decisions.md](decisions.md). The changelog has the detail for each release.
Anything not on this page is open for discussion in the repository's
Discussions.
