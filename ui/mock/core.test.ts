// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// The demo's promise: a read-only mock answers what a device would and
// changes nothing. `npm test` (node --test, no dependencies).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createMock, DEV_PROFILE, READ_ONLY_MESSAGE, type Schema } from "./core.ts";

const schema = JSON.parse(readFileSync(new URL("./schema.json", import.meta.url), "utf8")) as Schema;
const json = { "content-type": "application/json" };

void test("a read-only mock refuses every write before any handler runs", async () => {
  const mock = createMock({ schema, readOnly: true });
  try {
    for (const [method, path] of [["POST", "/system/reboot"], ["POST", "/system/factory-reset"], ["PUT", "/config"],
      ["POST", "/wifi/scan"], ["POST", "/ota"], ["POST", "/sk/forget"], ["DELETE", "/system/coredump"],
      ["POST", "/auth/login"], ["PUT", "/logs/level"]] as const) {
      const r = await mock.handle({ method, path, headers: json, body: '{"wifi":{"ssid0":null}}' });
      assert.equal(r.status, 403, `${method} ${path}`);
      assert.equal((JSON.parse(r.body) as { message: string }).message, READ_ONLY_MESSAGE);
    }
    const cfg = await mock.handle({ method: "GET", path: "/config?ns=wifi", headers: {}, body: "" });
    assert.equal((JSON.parse(cfg.body) as { wifi: { ssid0: string } }).wifi.ssid0, DEV_PROFILE.networks[0]!.ssid);
  } finally {
    mock.close();
  }
});

void test("a read-only mock starts settled: joined, approved, streaming, no crash stored", async () => {
  const mock = createMock({ schema, readOnly: true });
  try {
    const get = async (path: string) => {
      const r = await mock.handle({ method: "GET", path, headers: {}, body: "" });
      return { status: r.status, body: r.body ? JSON.parse(r.body) as Record<string, unknown> : null };
    };
    assert.equal((await get("/wifi/status")).body?.["state"], "connected");
    const sk = (await get("/sk/status")).body as { token: { state: string }; ws: { connected: boolean } };
    assert.equal(sk.token.state, "approved");
    assert.equal(sk.ws.connected, true);
    assert.equal((await get("/system/coredump")).status, 404);
    assert.equal((await get("/auth/status")).body?.["required"], false);
  } finally {
    mock.close();
  }
});

void test("a profile decides which optional pages answer", async () => {
  const off = createMock({ schema, readOnly: true, profile: { ...DEV_PROFILE, ble: false, flow: false } });
  const on = createMock({ schema, readOnly: true });
  try {
    for (const path of ["/ble/status", "/flow"]) {
      assert.equal((await off.handle({ method: "GET", path, headers: {}, body: "" })).status, 404, path);
      assert.equal((await on.handle({ method: "GET", path, headers: {}, body: "" })).status, 200, path);
    }
  } finally {
    off.close();
    on.close();
  }
});

void test("the development mock still writes", async () => {
  const mock = createMock({ schema });
  try {
    const r = await mock.handle({ method: "PUT", path: "/config", headers: json, body: '{"wifi":{"ssid1":"Other"}}' });
    assert.equal(r.status, 200);
    assert.deepEqual((JSON.parse(r.body) as { changed: string[] }).changed, ["wifi.ssid1"]);
  } finally {
    mock.close();
  }
});
