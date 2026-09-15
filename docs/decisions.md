# Decisions and additions vs. the plan

espOS was built against a written plan that lives outside this repository
(the section numbers below refer to it). Every deviation from that plan and
every addition to it is recorded here so nothing is silently applied
(plan §7). Moved from the README unchanged when the milestone table there
was retired.

* **ESP-IDF 6.0.2** instead of the plan's original 5.x (owner decision,
  2026-08-18). Consequence: IDF 6 removed the bundled cJSON, so
  `espressif/cjson` (Espressif-maintained, exact-pinned) is the single
  registry dependency.
* Beyond the M1 bullet list, and marked M1 in [rest-api.md](rest-api.md):
  `GET /api/v1/system/info`, `POST /api/v1/system/reboot`, `?ns=` filter on
  `GET /config`, `ETag`/`304` on the schema, `restart_required` in the PUT
  response, and the `Content-Type: application/json` CSRF guard on
  state-changing requests. Each is small and needed by the M1 acceptance
  test or the M5 UI; drop any of them if unwanted before the API is frozen.
* ESP32-P4 pulls `espressif/esp_hosted` + `espressif/esp_wifi_remote`
  (P4-only) because the chip has no radio; approved 2026-08-18.
* M2 ships the SoftAP captive portal; BLE provisioning followed in
  `espos_prov` — but built on protocomm directly, not on
  `espressif/network_provisioning` (see the 2026-09-15 entry below).
* M3 adds `espressif/mdns` (registry, exact-pinned) for discovery, approved
  2026-08-18. Discovery of `_signalk-ws._tcp` is folded into the
  `_signalk-http._tcp` browse (every server advertises both with the same
  TXT records; the ws endpoint comes from `GET /signalk`).
* Delta buffering during offline periods (listed under M2) landed with the
  delta pipeline in M4 — there was nothing to buffer before that.
* M5 adds `joltwallet/littlefs` (registry, exact-pinned) — the plan names
  LittleFS for the UI bundle and this is the ESP-IDF component for it — and
  the UI's build-time npm dependencies (`preact`, `vite`,
  `@preact/preset-vite`, `typescript`; nothing at runtime but preact).
  Beyond the M5 bullets: `PUT /api/v1/logs/level` (runtime log level) and
  `GET /api/v1/system/coredump/raw` (download for `espcoredump.py`), both
  small and needed to make logs/crashes actually useful from a browser.
* M6 signature verification is *signed apps without Secure Boot*
  (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`, RSA-3072, verified on update by the
  running app's compiled-in public key) rather than hardware Secure Boot:
  it is what the plan asks for ("against a compiled-in public key") without
  burning eFuses. The signing key is generated on first build when missing
  (git-ignored) so a checkout builds; real deployments bring their own
  (ota.md). Plain-http image sources are allowed for the same reason.
  ESP32 (original) builds pin chip rev ≥ 3 for the RSA scheme.
* **2026-09-07: the C ABI is the stable contract.** Plain C headers under
  `components/*/include` — `extern "C"`, `esp_err_t`, fixed-width ints,
  callback + `void *arg`, no IDF header but `esp_err.h` (two frozen
  exceptions), no `CONFIG_` in new headers, `ESPOS_ABI_VERSION` — are what
  every consumer gets; rules and the CI check are in
  [development.md](development.md) "Public API rules". A C++ facade is
  header-only sugar above it (`espos_n2k`/`espos_voice`/`espos_audio` stay
  C++-only contracts until wrapped). Rust not now: no released esp-idf-sys for
  IDF 6, Tier-3 targets, no P4 hosted-WiFi host, no esp-sr/LVGL path — the ABI
  is shaped so `bindgen` yields an `espos-sys` later without a rewrite (plan
  §3.4, revisit triggers there).
- 2026-09-10: the network-facing parsers are fuzzed under ASan and UBSan
  ([test/fuzz](https://github.com/signalk-espOS/espOS/tree/main/test/fuzz), a
  CI job on every PR). This is the other half of the Rust decision above, not
  a separate initiative: choosing C over a language with a borrow checker is
  only defensible if the code a hostile input reaches is exercised by
  something that does not share the author's assumptions. Three harnesses
  cover the frame parser, the OTA manifest and the candump codec — every place
  a byte arrives from a socket and becomes a path, a firmware URL or a CAN
  frame. They found a heap overread and a signed overflow in the first run,
  both reachable from the network, neither visible to review or to unit tests.
- 2026-09-07: `espressif/mdns` is espos_net's dependency (moved from espos_wifi with the T3 network seam) and a public `REQUIRES` (espos_sk browses through it); the responder is brought up from `espos_net_start()` on the caller's task, never from an event handler (mdns 1.11.3 hostname/service calls block on the responder task).
- 2026-09-07: REST authentication is a shared secret as `Authorization: Bearer <key>` plus an optional HttpOnly session cookie, enforced centrally in `espos_httpd` (every endpoint registered through `espos_httpd_register()` is protected unless it opts out); not HTTP Digest, because machine clients (the designer, a fleet plugin, scripts) speak Bearer, the cookie rides along with `EventSource`, and Digest has no logout. Device HTTPS is deliberately not part of it (RAM cost); the key crosses a plain-http LAN like the SignalK token does. Empty key = open, so existing devices keep working; the setup-portal network is exempt as the lockout recovery path.
- 2026-09-15: **BLE provisioning uses protocomm directly, not
  `espressif/network_provisioning`.** That manager drives the station itself
  — `esp_wifi_set_config()`, `esp_wifi_connect()`, `esp_wifi_start()` and its
  own retry logic — and espOS's WiFi state machine already owns exactly those
  calls, with a priority list, backoff, reason codes and the portal, all
  host-tested. Two owners of one radio is a fault that only appears in the
  field, so `espos_prov` uses the same Espressif stack (protocomm, Security 2,
  SRP6a) as a **BLE transport only**: credentials land in the `wifi` config
  namespace, the same keys the web UI writes, and the state machine connects
  as it always does. The cost is accepted deliberately: Espressif's "ESP BLE
  Provisioning" phone app speaks the manager's protobuf schema and will not
  talk to this device, whose endpoint is plain JSON. Adopting the manager
  would mean reworking or retiring espOS's WiFi state machine, not swapping a
  component; reaffirmed by the owner 2026-09-15 (no phone app needed).
  [provisioning.md](provisioning.md).
- 2026-09-15: **`espos_prov` keeps protocomm's BLE transport for now, and the
  way out is our own GATT server rather than NimBLE.** Provisioning was run
  end to end on an ESP32-C5 ([provisioning.md](provisioning.md)) and works,
  with two constraints that both come from `simple_ble`, protocomm's GATT
  boilerplate: it initialises Bluedroid unconditionally (so it cannot share a
  firmware with `espos_ble`), and it advertises only through the BLE 4.2
  legacy API (so a BLE 5.0 radio has to be configured down to 4.2). The
  obvious alternative, `protocomm_nimble`, fixes neither -- it also
  advertises with the legacy API, also calls `nimble_port_init()` itself, and
  `BT_HOST` is a Kconfig *choice*, so a NimBLE `espos_prov` would be
  build-incompatible with the Bluedroid-based `espos_ble` rather than merely
  clashing at runtime. What does work is that `protocomm_req_handle()` is
  public API: `espos_prov` can run its own GATT server on
  `esp_ble_gap_ext_adv_*` and hand writes to protocomm, keeping Security 2,
  SRP6a and the JSON endpoint while dropping only `simple_ble`. That fixes
  both constraints and costs a few hundred lines we would own. Not built:
  every espOS device today provisions through the SoftAP portal, which needs
  none of this. An upstream fix for the double-init is worth proposing
  separately -- it is ~30 lines and helps everyone -- but it would land in a
  later IDF than the `[6.0.0, 6.1.0)` espOS pins, so it is not a plan for
  this year.
