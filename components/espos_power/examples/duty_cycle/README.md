# duty_cycle

A device on a battery: wake, publish two readings, flush the SignalK stream,
deep-sleep for `power.interval_s`, again. The readings are
`sensors.dutyCycle.wakeCount` (timer wakes since the last power-on) and
`sensors.dutyCycle.lastAwake` (how long the previous wake took, in seconds), so
the cycle can be watched from the server's Data Browser.

Built in CI for the ESP32-C6 (WiFi) and the ESP32-P4 (the Waveshare PoE board,
Ethernet, WiFi off in `sdkconfig.defaults.esp32p4`).

## Turning it on

The cycle is **off** until you set it:

```sh
curl -X PUT http://<device>/api/v1/config -H 'Content-Type: application/json' \
     -d '{"power": {"mode": "cycle", "interval_s": 60}}'
```

A freshly flashed device stays awake until then, so it can learn its network
and its SignalK server first. `GET /api/v1/power` shows what the cycle is
waiting for (`why`), the wake number and how long the last wake took.

## Staying reachable

A sleeping device is reachable only while it is awake. Two rules keep a way
in ([docs/power.md](../../../../docs/power.md)):

- After every power-on, update or crash it stays awake for `power.window_s`
  (default 5 minutes) before the cycle starts. **Cutting the power reopens the
  window** — that is how you reach a device to change its settings or update
  it.
- It never sleeps while an update is still unconfirmed: every wake is a boot,
  and sleeping before the new image confirmed itself would roll it back.

## On the ESP32-P4 PoE board

Never connect USB while the board is powered over Ethernet. Update it over the
network during its awake window instead (`POST /api/v1/ota`).

Some PoE switches cut the power of a device that draws very little. If that
happens, every wake is a power-on: `wake_count` stays 0 and the reset reason
reads `poweron` instead of `deepsleep`.
