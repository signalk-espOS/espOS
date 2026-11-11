// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// The devices the demo can pretend to be, picked by ?device=. Each one shows
// the pages its firmware shows: the BLE page only on the gateway, because
// only the gateway builds espos_ble. Addresses come from the documentation
// ranges (192.0.2.0/24, locally administered MACs), so nothing here names a
// real network or a real board.
import { DEV_PROFILE, type Profile } from "../../mock/core.ts";

const network = [
  { ssid: "Demo-Boat", bssid: "02:00:5e:10:00:01", rssi: -52, channel: 6, auth: "wpa2/wpa3" },
  { ssid: "Marina-Guest", bssid: "02:00:5e:10:00:02", rssi: -74, channel: 11, auth: "wpa2" },
];
const server = [
  { host: "192.0.2.10", port: 3000, self: "urn:mrn:signalk:uuid:00000000-0000-4000-8000-000000000d30", name: "demo-boat", roles: "master,main", swname: "signalk-server", swvers: "2.31.1" },
];
const common = {
  ...DEV_PROFILE,
  // Replaced by ?version=, which the espOS Manager sets to what it lists.
  version: "0.0.0-demo",
  flow: false,
  ble: false,
  networks: network,
  servers: server,
  gateway: "192.0.2.1",
  vesselContext: "vessels.urn:mrn:signalk:uuid:00000000-0000-4000-8000-000000000d30",
};

export const PROFILES: Record<string, Profile> = {
  cockpit: {
    ...common,
    app: "cockpit",
    hostname: "espos-cockpit",
    chip: "esp32p4", chipRevision: 100, cores: 2, cpuMhz: 360,
    board: "Waveshare ESP32-P4-WIFI6-Touch-LCD-7B",
    mac: "02:00:5e:20:00:01",
    features: ["wifi", "ble"],
    flashBytes: 16 * 1024 * 1024, flashPhysicalBytes: 32 * 1024 * 1024,
    ramInternalBytes: 768 * 1024, ramPsramBytes: 32 * 1024 * 1024,
    uptimeS: 3 * 86400 + 4 * 3600 + 17 * 60,
    ip: "192.0.2.21",
    clientId: "00000000-0000-4000-8000-0000000c0c01",
  },
  "ble-gateway": {
    ...common,
    app: "ble_gateway",
    hostname: "espos-engine-room",
    chip: "esp32c5", chipRevision: 100, cores: 1, cpuMhz: 240,
    board: "Waveshare ESP32-C5-WIFI6-KIT-N16R8",
    mac: "02:00:5e:20:00:02",
    features: ["wifi", "ble", "802.15.4"],
    flashBytes: 16 * 1024 * 1024, flashPhysicalBytes: 16 * 1024 * 1024,
    ramInternalBytes: 384 * 1024, ramPsramBytes: 8 * 1024 * 1024,
    uptimeS: 11 * 86400 + 2 * 3600 + 41 * 60,
    ble: true,
    ip: "192.0.2.22",
    clientId: "00000000-0000-4000-8000-0000000b1e02",
  },
  "zigbee-coordinator": {
    ...common,
    app: "zigbee_coordinator",
    hostname: "espos-zigbee",
    chip: "esp32c5", chipRevision: 100, cores: 1, cpuMhz: 240,
    board: "Waveshare ESP32-C5-WIFI6-KIT-N16R8",
    mac: "02:00:5e:20:00:03",
    features: ["wifi", "ble", "802.15.4"],
    flashBytes: 16 * 1024 * 1024, flashPhysicalBytes: 16 * 1024 * 1024,
    ramInternalBytes: 384 * 1024, ramPsramBytes: 8 * 1024 * 1024,
    uptimeS: 6 * 86400 + 19 * 3600 + 5 * 60,
    ip: "192.0.2.23",
    clientId: "00000000-0000-4000-8000-000000021b03",
  },
};

export const DEFAULT_DEVICE = "cockpit";

// What a link may set beyond the device: the version the manager shows next
// to it, so both pages agree. Checked, because it comes from the address bar.
const VERSION = /^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]{1,32})?$/;

/** The device ?device= names (the default when it names none), with ?version= applied. */
export function profileFromQuery(search: string): { id: string; profile: Profile } {
  const q = new URLSearchParams(search);
  const asked = q.get("device") ?? "";
  const id = Object.hasOwn(PROFILES, asked) ? asked : DEFAULT_DEVICE;
  const base = PROFILES[id]!;
  const version = q.get("version") ?? "";
  return { id, profile: VERSION.test(version) ? { ...base, version } : base };
}
