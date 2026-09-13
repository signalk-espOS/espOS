# ethernet

An espOS device on a cable. The whole firmware is `espos_start()`:
`espos_eth` brings up the chip's internal EMAC and the RMII PHY, DHCP hands
out an address, and `espos_net` routes SignalK, OTA, the web UI and mDNS over
the cable.

Built for the **Waveshare ESP32-P4-WIFI6-POE-ETH**, where one PoE cable is
both power and network, and where IDF's ESP32-P4 EMAC defaults already are
the board's wiring (MDC 31, MDIO 52, RMII clock in on GPIO 50). The two things
that vary per board — the PHY's MDIO address and its reset GPIO — are Kconfig,
defaulting to this board's `1` and `51`. Details: [docs/net.md](../../../../docs/net.md),
"Wired Ethernet".

## WiFi is off

`sdkconfig.defaults` sets `CONFIG_ESPOS_WIFI=n`, which is what makes this an
Ethernet device rather than a WiFi device that happens to have a port: no
station, no setup portal. You configure it over the cable, at its DHCP address
or `<hostname>.local`.

Turn WiFi back on and both transports run; the route still goes over the cable
whenever it has a link, and falls back to the station when it does not.

## Power the board one way at a time

**Do not connect USB to the Waveshare PoE board while it is powered over PoE.**
Flash it over USB with the network cable unplugged, then unplug USB before
connecting PoE. That also means you cannot watch the serial console while the
board is on the network — which is why the example logs where it can be found,
and why the checks below are all over the network.

## Checking it

With the board on PoE and a DHCP server on the network:

1. **Find it.** `avahi-browse -rt _espos._tcp` (or your router's lease table).
2. **Confirm the route is the cable:**
   ```sh
   curl http://<address>/api/v1/net/status
   ```
   `iface` is `"eth"`, `ip` is the leased address, `rssi` is `null`.
3. **Open the web UI** at `http://<address>/`, and its log page: the example
   logs `on the cable as <address>` when the route arrives.

If `/api/v1/net/status` never answers, the device has no address. The two
likely causes fail the same quiet way: a wrong PHY address or reset GPIO (the
start log names both), or no DHCP server on that network.
