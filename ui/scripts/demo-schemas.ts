// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// demo-gen/<device>.schema.json for the demo bundle: the config schema each
// demo device's firmware builds, generated from the same descriptors by the
// same espos_gen_config.py the firmware build runs. Every firmware gets
// espOS's core descriptors (espos_project_prologue() keeps httpd, net, time,
// wifi, sk and ota) plus the optional components it names.
import { mkdirSync, writeFileSync } from "node:fs";
import { generateSchema } from "../mock/server.ts";

const CORE = ["httpd", "net", "time", "wifi", "sk", "ota"].map((c) => `components/espos_${c}/config/${c}.json`);

const DEVICES: Record<string, string[]> = {
  cockpit: [...CORE, "components/espos_n2k/config/n2k.json", "ui/demo/descriptors/cockpit.json"],
  "ble-gateway": [...CORE, "components/espos_ble/config/ble.json"],
  "zigbee-coordinator": CORE,
};

const out = new URL("../demo-gen/", import.meta.url).pathname;
mkdirSync(out, { recursive: true });
for (const [id, descriptors] of Object.entries(DEVICES)) {
  const schema = generateSchema(descriptors);
  // No fallback: a demo device with the development schema would show
  // settings its firmware does not have.
  if (!schema) throw new Error(`demo-schemas: espos_gen_config.py failed for ${id} (is python3 installed?)`);
  writeFileSync(`${out}${id}.schema.json`, JSON.stringify(schema) + "\n");
  console.log(`${id}: ${Object.keys(schema.properties).join(", ")}`);
}
