// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// The espOS API mock itself, with no transport: it takes a request and returns
// a response, and hands server-sent events to whoever subscribes. Two
// transports drive it -- mock/server.ts (node:http, for `npm run dev`) and
// src/demo/transport.ts (in the browser, for the read-only demo bundle) -- so
// the device the UI is developed against and the one a demo visitor clicks
// through are the same code and cannot drift apart.
//
// It implements docs/rest-api.md well enough to exercise every page without a
// device: config with schema validation of the basics, a simulated WiFi state
// machine, SignalK discovery/token flow, a log ring, SSE, and the REST
// authentication (Bearer or the espos_sid cookie, once httpd.api_key is set).
//
// Only what Node and a browser both provide is used here: timers,
// structuredClone, crypto.randomUUID, URL. No node:* import and no DOM.

// Config values are scalars: every schema property has a scalar type, and
// validate() refuses anything else before it is stored.
type Scalar = string | number | boolean | null;
type ConfigDoc = Record<string, Record<string, Scalar>>;
type Json = Scalar | Json[] | { [key: string]: Json };
type JsonObject = { [key: string]: Json };

interface SchemaProperty {
  type?: string;
  default?: Scalar;
  minimum?: number;
  maximum?: number;
  maxLength?: number;
  enum?: Scalar[];
  pattern?: string;
  "x-espos-secret"?: boolean;
  "x-espos-restartRequired"?: boolean;
}
export interface Schema {
  properties: Record<string, { properties: Record<string, SchemaProperty> }>;
}

/** Who the mock pretends to be. The defaults are the development device. */
export interface Profile {
  app: string;
  version: string;
  hostname: string;
  chip: string;
  chipRevision: number;
  cores: number;
  cpuMhz: number;
  board: string;
  mac: string;
  features: string[];
  flashBytes: number;
  flashPhysicalBytes: number;
  ramInternalBytes: number;
  ramPsramBytes: number;
  /** Seconds the device has been up when the mock starts. */
  uptimeS: number;
  /** Which optional endpoints answer, and so which optional pages appear. */
  ble: boolean;
  flow: boolean;
  /** Networks a scan finds; the first is the one the device joins. */
  networks: { ssid: string; bssid: string; rssi: number; channel: number; auth: string }[];
  /** SignalK servers discovery finds; the first is the one it uses. */
  servers: { host: string; port: number; self: string; name: string; roles: string; swname: string; swvers: string }[];
  /** The device's own address once WiFi is up. */
  ip: string;
  gateway: string;
  clientId: string;
  vesselContext: string;
}

export const DEV_PROFILE: Profile = {
  app: "espos",
  version: "0.5.0-mock",
  hostname: "espos-1a2b",
  chip: "esp32c6",
  chipRevision: 1,
  cores: 1,
  cpuMhz: 160,
  board: "Espressif ESP32-C6-DevKitC-1",
  mac: "60:55:f9:00:1a:2b",
  features: ["wifi", "ble", "802.15.4", "embedded-flash"],
  flashBytes: 4 * 1024 * 1024,
  flashPhysicalBytes: 8 * 1024 * 1024,
  ramInternalBytes: 512 * 1024,
  ramPsramBytes: 0,
  uptimeS: 0,
  ble: true,
  flow: true,
  networks: [
    { ssid: "Marina-Guest", bssid: "de:ad:be:ef:00:02", rssi: -71, channel: 11, auth: "wpa2" },
    { ssid: "Boat", bssid: "de:ad:be:ef:00:01", rssi: -48, channel: 6, auth: "wpa2/wpa3" },
    { ssid: "Harbour Cafe", bssid: "de:ad:be:ef:00:07", rssi: -84, channel: 1, auth: "open" },
  ],
  servers: [
    { host: "192.168.1.10", port: 80, self: "urn:mrn:signalk:uuid:0e6d1a1a-1111-4111-8111-000000000099", name: "boat", roles: "master,main", swname: "signalk-server", swvers: "2.31.1" },
    { host: "192.168.1.11", port: 3000, self: "urn:mrn:signalk:uuid:0e6d1a1a-2222-4222-8222-000000000042", name: "nav-pc", roles: "master,main", swname: "signalk-server", swvers: "2.30.0" },
  ],
  ip: "192.168.1.42",
  gateway: "192.168.1.1",
  clientId: "9cf791de-aa92-4830-a958-0388a42ef72b",
  vesselContext: "vessels.urn:mrn:signalk:uuid:3d0b5a1e-6f2c-4b8e-9d71-5c4a2e8f1b07",
};

export interface MockOptions {
  schema: Schema;
  profile?: Profile;
  /**
   * Refuse every write with 403 and start already settled: joined to WiFi,
   * approved by SignalK, stream up. What a demo visitor gets -- something to
   * look at, nothing to change.
   */
  readOnly?: boolean;
  /** Where log lines go besides the ring (the dev server prints them). */
  print?: (line: string) => void;
}

/** A request as the mock sees it: path below /api/v1, query included. */
export interface MockRequest {
  method: string;
  path: string;
  /** Lower-case names. */
  headers: Record<string, string | undefined>;
  body: string;
}
export interface MockResponse {
  status: number;
  headers: Record<string, string>;
  body: string;
}
export interface MockEvent { event: string; data: unknown }

export interface Mock {
  handle(req: MockRequest): Promise<MockResponse>;
  /**
   * Open an event stream: the snapshot a fresh GET /events starts with, then
   * every event as it happens. Returns the function that closes it. Refused
   * (null) when the request would get a 401 or 403 on the device.
   */
  events(headers: Record<string, string | undefined>, listener: (e: MockEvent) => void): (() => void) | null;
  /** Stop every timer, so a test or a closed page leaves nothing running. */
  close(): void;
}

export const READ_ONLY_MESSAGE = "Demo: view only";

export function createMock(opts: MockOptions): Mock {
  const schema = opts.schema;
  const profile = opts.profile ?? DEV_PROFILE;
  const readOnly = opts.readOnly ?? false;
  const etag = "mock" + Math.abs(hash(JSON.stringify(schema))).toString(16).slice(0, 12);
  // Every timer the mock starts goes through these, so close() can stop them
  // all, and a stopped one leaves the set. Once closed they start nothing: a
  // listener may close the mock from inside a handler that schedules more.
  type Timer = ReturnType<typeof setTimeout>;
  const timers = new Set<Timer>();
  let closed = false;
  const later = (fn: () => void, ms: number): Timer | undefined => {
    if (closed) return undefined;
    const t = setTimeout(() => { timers.delete(t); fn(); }, ms); timers.add(t); return t;
  };
  const every = (fn: () => void, ms: number): Timer | undefined => {
    if (closed) return undefined;
    const t = setInterval(fn, ms); timers.add(t); return t;
  };
  // clearTimeout and clearInterval share one id pool in Node and browsers alike.
  const stop = (t: Timer | undefined) => { if (t === undefined) return; clearInterval(t); timers.delete(t); };

  // ---- state
  const defaults: ConfigDoc = {};
  for (const [ns, o] of Object.entries(schema.properties)) {
    const d: Record<string, Scalar> = (defaults[ns] = {});
    for (const [k, p] of Object.entries(o.properties)) d[k] = p.default ?? null;
  }
  const joined = profile.networks[0]?.ssid ?? "";
  const stored: ConfigDoc = readOnly
    ? { wifi: { ssid0: joined, psk0: "demo-psk" } }
    : { wifi: { ssid0: "Marina-Guest", psk0: "hunter22" } };
  const effective = (): ConfigDoc => {
    const out: ConfigDoc = {};
    for (const ns of Object.keys(defaults)) out[ns] = { ...defaults[ns], ...(stored[ns] ?? {}) };
    return out;
  };
  const cfg = (ns: string): Record<string, Scalar> => effective()[ns] ?? {};
  const isSecret = (ns: string, k: string) => !!schema.properties[ns]?.properties[k]?.["x-espos-secret"];
  const redacted = (ns: string | null): ConfigDoc => {
    const doc = effective();
    const pick: ConfigDoc = ns ? { [ns]: doc[ns] ?? {} } : doc;
    for (const [n, o] of Object.entries(pick)) for (const k of Object.keys(o)) if (isSecret(n, k)) o[k] = o[k] ? "********" : "";
    return pick;
  };

  const boot = Date.now() - profile.uptimeS * 1000;
  const logs: { seq: number; line: string }[] = [];
  let logSeq = 1;
  const listeners = new Set<(e: MockEvent) => void>();
  const emit = (event: string, data: unknown) => {
    // Cloned once per event, so a listener holding on to it never sees the
    // mock's own state move underneath.
    const e = { event, data: structuredClone(data) };
    for (const l of listeners) l(e);
  };
  let logsDirty = false;
  const logAndMark = (lvl: string, tag: string, msg: string) => {
    const line = `${lvl} (${Date.now() - boot}) ${tag}: ${msg}`;
    logs.push({ seq: logSeq++, line });
    while (logs.length > 400) logs.shift();
    opts.print?.(line);
    logsDirty = true;
  };
  every(() => { if (logsDirty) { logsDirty = false; emit("logs", { next: logSeq }); } }, 500);

  interface WifiStatus {
    state: string; sta_enabled: boolean; hostname: string; reason: { code: number; text: string };
    connect_count: number; disconnect_count: number; attempt: number;
    portal: { active: boolean; ssid: string; force: string; ip: string; clients: number };
    ssid?: Scalar | undefined; network_index?: number; backoff_ms?: number;
    ip?: string; gateway?: string; netmask?: string; rssi?: number; channel?: number; bssid?: string;
  }
  const portalSsid = "espOS-" + profile.hostname.replace(/^espos-/, "");
  const wifi: WifiStatus = {
    state: "unconfigured", sta_enabled: true, hostname: profile.hostname, reason: { code: 0, text: "" },
    connect_count: 0, disconnect_count: 0, attempt: 0,
    portal: { active: true, ssid: portalSsid, force: "auto", ip: "192.168.4.1", clients: 0 },
  };
  let wifiTimer: Timer | undefined;
  const wifiEmit = () => emit("wifi", wifiStatus());
  const wifiStatus = () => ({ ...wifi });
  const scanResults = profile.networks;
  const wifiJoin = (ssid: Scalar | undefined) => {
    const r = scanResults.find((x) => x.ssid === ssid)!;
    Object.assign(wifi, { state: "connected", ip: profile.ip, gateway: profile.gateway, netmask: "255.255.255.0",
      rssi: r.rssi, channel: r.channel, bssid: r.bssid, connect_count: wifi.connect_count + 1, reason: { code: 0, text: "" } });
    delete wifi.backoff_ms; wifi.portal.active = false;
  };
  function wifiEval() {
    stop(wifiTimer);
    const c = cfg("wifi");
    const nets = [0, 1, 2, 3].map((i) => c[`ssid${i}`]).filter(Boolean);
    if (!c["sta_enabled"]) {
      Object.assign(wifi, { state: "disabled", reason: { code: 1005, text: "station disabled by configuration" } });
      wifi.portal.active = true; delete wifi.ip; return wifiEmit();
    }
    if (!nets.length) {
      Object.assign(wifi, { state: "unconfigured", reason: { code: 0, text: "" } });
      wifi.portal.active = true; delete wifi.ip; return wifiEmit();
    }
    const idx = nets.findIndex((s) => scanResults.some((r) => r.ssid === s));
    Object.assign(wifi, { state: "connecting", ssid: nets[Math.max(idx, 0)], network_index: Math.max(idx, 0), attempt: wifi.attempt + 1 });
    delete wifi.ip; wifiEmit();
    logAndMark("I", "espos_wifi", `connecting to ${String(wifi.ssid)}`);
    wifiTimer = later(() => {
      if (idx < 0) {
        Object.assign(wifi, { state: "backoff", reason: { code: 201, text: "no AP with that SSID found (NO_AP_FOUND)" }, backoff_ms: 8000, disconnect_count: wifi.disconnect_count + 1 });
        wifi.portal.active = true; wifiEmit();
        logAndMark("W", "espos_wifi", `disconnected: NO_AP_FOUND (201), retry in 8 s`);
        wifiTimer = later(wifiEval, 8000);
        return;
      }
      Object.assign(wifi, { state: "obtaining_ip" }); wifiEmit();
      wifiTimer = later(() => {
        wifiJoin(wifi.ssid);
        wifiEmit();
        logAndMark("I", "espos_wifi", `connected: ${String(wifi.ssid)} ip=${String(wifi.ip)} rssi=${String(wifi.rssi)}`);
        skDiscover();
      }, 1200);
    }, 1500);
  }
  let scan: { scanning: boolean; age_s: number | null; results: typeof scanResults } = { scanning: false, age_s: null, results: [] };
  let scanAt = 0;
  const scanDoc = () => ({ ...scan, age_s: scanAt ? Math.round((Date.now() - scanAt) / 1000) : null });

  // ---- SignalK
  const servers = profile.servers;
  interface SkServer {
    source: string; host?: Scalar | undefined; port?: Scalar | undefined; self?: Scalar | undefined;
    name?: Scalar | undefined; swname?: Scalar | undefined; swvers?: Scalar | undefined;
    by_self?: boolean;
  }
  interface SkToken {
    state: string; has_token: boolean; busy: boolean; last_http_status: number; last_error: string;
    counts: { requests: number; approved: number; denied: number; unauthorized: number };
    pending_s?: number; pending_href?: string; approved_s?: number;
  }
  const sk = {
    token: { state: "no_server", has_token: false, busy: false, last_http_status: 0, last_error: "", counts: { requests: 0, approved: 0, denied: 0, unauthorized: 0 } } as SkToken,
    server: { source: "none" } as SkServer, client_id: profile.clientId, vessel_context: profile.vesselContext, description: `espOS ${profile.hostname}`, permissions: "readwrite",
    discovery: { enabled: true, count: 0, last_s: null as number | null },
    ws: { enabled: true, connected: false, reconnects: 0, sent: 0, send_errors: 0, pending: 0, buffered: 0, buffered_bytes: 0, dropped: 0, last_error: "", meta: { declared: 3, reconciled: 0 }, in: { subs: 2, frames: 0, received: 0 }, put: { pending: 0, ok: 0, failed: 0 } },
  };
  let discovered: ((typeof servers)[number] & { seen_s: number })[] = [];
  let discoverAt = 0;
  let pendingAt = 0;
  const skEmit = () => emit("sk", skStatus());
  const skStatus = () => {
    const s = structuredClone(sk);
    s.discovery.last_s = discoverAt ? Math.round((Date.now() - discoverAt) / 1000) : null;
    if (s.token.state === "pending") s.token.pending_s = Math.round((Date.now() - pendingAt) / 1000);
    return s;
  };
  const skSelect = (pick: SkServer) => {
    sk.server = { host: pick.host, port: pick.port, self: pick.self ?? "", source: pick.source, name: pick.name ?? pick.host, swname: pick.swname ?? "", swvers: pick.swvers ?? "", by_self: false };
  };
  const skFound = () => {
    discovered = servers.map((s) => ({ ...s, seen_s: 0 }));
    discoverAt = Date.now();
    sk.discovery.count = discovered.length;
  };
  function skDiscover() {
    if (wifi.state !== "connected") return;
    skFound();
    logAndMark("I", "espos_sk", `discovery: ${discovered.length} server(s)`);
    emit("sk_servers", serversDoc());
    if (sk.server.source === "none") {
      const c = cfg("sk");
      skSelect(c["server_host"] ? { host: c["server_host"], port: c["server_port"], self: c["server_self"] || "", source: "manual", name: c["server_host"] }
        : { ...discovered[0], source: "discovered" });
      skRequest();
    }
  }
  // Selected by address, as on the device: twins share a self.
  const serversDoc = () => ({ servers: discovered.map((s) => ({ ...s, selected: s.host === sk.server.host && s.port === sk.server.port })), last_s: discoverAt ? Math.round((Date.now() - discoverAt) / 1000) : null });
  // An sk config change re-runs the choice; the device publishes the list
  // whenever the server in use moves, so a pick shows at once.
  function skReselect() {
    const c = cfg("sk");
    const pick: SkServer | undefined = c["server_host"]
      ? { ...discovered.find((s) => s.host === c["server_host"] && s.port === c["server_port"]), host: c["server_host"], port: c["server_port"], source: "manual" }
      : c["server_self"] ? (() => { const d = discovered.find((s) => s.self === c["server_self"]); return d && { ...d, source: "discovered" }; })()
      : undefined;
    if (!pick || (pick.host === sk.server.host && pick.port === sk.server.port && pick.source === sk.server.source)) return;
    skSelect(pick);
    skEmit();
    emit("sk_servers", serversDoc());
  }
  let approveTimer: Timer | undefined;
  function skRequest() {
    if (sk.token.has_token) return;
    Object.assign(sk.token, { state: "requesting", busy: true }); skEmit();
    later(() => {
      Object.assign(sk.token, { state: "pending", busy: false, last_http_status: 202, pending_href: "/signalk/v1/access/requests/" + sk.client_id });
      sk.token.counts.requests++; pendingAt = Date.now(); skEmit();
      logAndMark("I", "espos_sk", "access request PENDING — approve it in the SignalK admin UI");
      // the "admin" approves after a while unless the UI pastes a token first
      stop(approveTimer);
      approveTimer = later(() => {
        if (sk.token.state !== "pending") return;
        Object.assign(sk.token, { state: "approved", has_token: true, approved_s: 0, last_http_status: 200 });
        delete sk.token.pending_href; sk.token.counts.approved++; skEmit();
        logAndMark("I", "espos_sk", "access APPROVED, token stored");
        wsConnect();
      }, 12000);
    }, 800);
  }
  let wsTimer: Timer | undefined;
  function wsConnect() {
    stop(wsTimer);
    if (!sk.token.has_token || !cfg("sk")["ws_enabled"] || wifi.state !== "connected") {
      if (sk.ws.connected) { sk.ws.connected = false; emit("sk_ws", sk.ws); }
      return;
    }
    Object.assign(sk.ws, { connected: true, connected_s: 0, reconnects: sk.ws.reconnects + 1, last_error: "" });
    sk.ws.meta.reconciled = sk.ws.meta.declared;
    emit("sk_ws", sk.ws); skEmit();
    logAndMark("I", "espos_skws", `stream connected to ${String(sk.server.host)}:${String(sk.server.port)}`);
    // connected_s exists from the Object.assign above on. It stays out of the
    // literal so /sk/status keeps omitting it until the stream first connects.
    const ws = sk.ws as typeof sk.ws & { connected_s: number };
    // Emitted, not only counted, so the SignalK page moves while it is open.
    wsTimer = every(() => { sk.ws.sent += 2; ws.connected_s += 5; sk.ws.in.frames += 9; sk.ws.in.received += 12; emit("sk_ws", sk.ws); }, 5000);
  }
  function skForget() {
    Object.assign(sk.token, { state: "no_server", has_token: false, busy: false, last_http_status: 0, last_error: "" });
    delete sk.token.approved_s; delete sk.token.pending_href;
    stop(wsTimer); sk.ws.connected = false; emit("sk_ws", sk.ws);
    logAndMark("W", "espos_sk", "token forgotten");
    skEmit();
    if (sk.server.source !== "none") skRequest();
  }

  // ---- OTA
  const ota = {
    state: "idle", last_error: "",
    running: { version: profile.version, project: profile.app, target: profile.chip, slot: "ota_0", image_state: "valid", pending_verify: false, confirmed: true, other_slot: "ota_1",
      // Empty on a demo device, as after a USB flash: any version there would
      // be read against the one the visitor was shown, as a rollback or a downgrade.
      other_version: readOnly ? "" : "0.4.9", rolled_back: false, built: "Aug 18 2026 12:00:00", idf: "v6.0.2", key_fp: "b3381b48b9cc9941" },
    manifest: { url: "" as Scalar | undefined, channel: "stable" as Scalar | undefined, auto_check: true as Scalar | undefined, auto_install: false as Scalar | undefined,
      last_check_s: null as number | null, next_check_s: null as number | null },
    progress: { received: 0, total: 0 },
    available: null as { version: string; url: string; size: number; sha256: string; notes: string; newer: boolean } | null,
  };
  let checkAt = 0;
  const otaStatus = () => { const c = cfg("ota"); const last = checkAt ? Math.round((Date.now() - checkAt) / 1000) : null; ota.manifest.url = c["manifest_url"]; ota.manifest.channel = c["channel"]; ota.manifest.auto_check = c["auto_check"]; ota.manifest.auto_install = c["auto_install"]; ota.manifest.last_check_s = last; ota.manifest.next_check_s = last !== null && c["auto_check"] ? Number(c["check_h"]) * 3600 - last : null; return ota; };
  const otaEmit = () => emit("ota", otaStatus());
  function otaCheck() {
    const c = cfg("ota");
    if (!c["manifest_url"]) { Object.assign(ota, { state: "failed", last_error: "no manifest URL configured" }); return otaEmit(); }
    Object.assign(ota, { state: "checking", last_error: "" }); otaEmit();
    later(() => {
      checkAt = Date.now();
      ota.available = { version: "0.5.1", url: new URL("espos-esp32c6-0.5.1.bin", String(c["manifest_url"])).href, size: 1180000, sha256: "", notes: "mock: bug fixes", newer: true };
      ota.state = "available"; otaEmit();
      logAndMark("I", "espos_ota", "manifest: 0.5.1 available (running 0.5.0-mock)");
    }, 900);
  }
  function otaInstall(url: string) {
    if (url.includes("unsigned")) {
      Object.assign(ota, { state: "downloading", progress: { received: 0, total: 900000 } }); otaEmit();
      later(() => { Object.assign(ota, { state: "failed", last_error: "image rejected: bad signature or corrupt (ESP_ERR_OTA_VALIDATE_FAILED)" }); otaEmit(); }, 2500);
      return;
    }
    Object.assign(ota, { state: "downloading", last_error: "", progress: { received: 0, total: 1180000 } }); otaEmit();
    const t = every(() => {
      ota.progress.received = Math.min(ota.progress.total, ota.progress.received + 120000); otaEmit();
      if (ota.progress.received >= ota.progress.total) {
        stop(t);
        ota.state = "ready"; otaEmit();
        logAndMark("W", "espos_ota", "update installed, rebooting");
        later(() => { Object.assign(ota, { state: "idle", available: null, progress: { received: 0, total: 0 } }); ota.running.version = "0.5.1"; ota.running.slot = "ota_1"; ota.running.other_slot = "ota_0"; ota.running.other_version = "0.5.0-mock"; ota.running.pending_verify = true; ota.running.confirmed = false; ota.running.image_state = "pending_verify"; otaEmit();
          later(() => { ota.running.pending_verify = false; ota.running.confirmed = true; ota.running.image_state = "valid"; otaEmit(); logAndMark("I", "espos_ota", "new image confirmed (network up), rollback cancelled"); }, 5000); }, 2000);
      }
    }, 400);
  }

  // ---- authentication (docs/rest-api.md "Authentication", docs/security.md)
  // Open until httpd.api_key is set; then Bearer or the espos_sid cookie from
  // POST /auth/login. The Origin check on cookie writes accepts a localhost
  // origin of any port on top of the exact match: the Vite dev server proxies
  // the UI (origin localhost:5173) to this mock (host 127.0.0.1:8484), so the
  // strict rule the device applies would refuse every save in development.
  const auth = { sessions: new Map<string, { expires: number }>(), fails: 0, failFirst: 0, lockedUntil: 0 };
  const apiKey = () => String(effective()["httpd"]?.["api_key"] ?? "");
  const authRequired = () => apiKey() !== "";
  type Headers = Record<string, string | undefined>;
  const cookies = (h: Headers) => Object.fromEntries((h["cookie"] ?? "").split(";").map((c) => c.trim().split("=", 2)).filter((kv): kv is [string, string] => kv.length === 2));
  // A session id is never empty, so a missing cookie finds no session either way.
  const sessionId = (h: Headers) => cookies(h)["espos_sid"] ?? "";
  const throttled = () => Date.now() < auth.lockedUntil;
  function checkKey(k: string) {
    if (throttled()) return "throttled";
    if (k === apiKey()) { auth.fails = 0; return "ok"; }
    const now = Date.now();
    if (!auth.fails || now - auth.failFirst >= 60000) { auth.fails = 0; auth.failFirst = now; }
    if (++auth.fails >= 5) { auth.lockedUntil = now + 30000; auth.fails = 0; logAndMark("W", "espos_auth", "too many failed keys: refusing key checks for 30 s"); }
    return "bad";
  }
  const originOk = (h: Headers) => {
    const o = h["origin"] ?? h["referer"];
    if (!o) return false;
    try { const a = new URL(o).host; return a === (h["host"] ?? "") || /^(localhost|127\.0\.0\.1)(:\d+)?$/.test(a); } catch { return false; }
  };

  // ---- HTTP
  const json = (status: number, body: unknown, headers: Record<string, string> = {}): MockResponse =>
    ({ status, headers: { "Content-Type": "application/json", "Cache-Control": "no-store", ...headers }, body: JSON.stringify(body) });
  const err = (status: number, code: string, message: string, extra: Record<string, unknown> = {}, headers: Record<string, string> = {}) =>
    json(status, { error: code, message, ...extra }, headers);
  const throttle429 = () => err(429, "too_many_attempts", "too many failed keys; wait before trying again", {},
    { "Retry-After": String(Math.max(1, Math.ceil((auth.lockedUntil - Date.now()) / 1000))) });
  // The method the request authenticated with ("none" when it did not), or
  // the refusal to send (never for a public endpoint).
  function authenticate(method: string, h: Headers, isPublic: boolean): "none" | "bearer" | "cookie" | MockResponse {
    const stateChanging = !["GET", "HEAD", "OPTIONS"].includes(method);
    let how: "none" | "bearer" | "cookie" = "none", verdict = "allow";
    const bearer = h["authorization"];
    if (!authRequired()) verdict = "allow";
    else if (bearer && /^bearer /i.test(bearer)) {
      const r = checkKey(bearer.slice(7).trim());
      if (r === "ok") how = "bearer"; else verdict = r === "throttled" ? "throttled" : "unauthorized";
    } else {
      const s = auth.sessions.get(sessionId(h));
      if (s && s.expires > Date.now()) { how = "cookie"; if (stateChanging && !originOk(h)) verdict = "origin"; }
      else verdict = "unauthorized";
    }
    if (isPublic || verdict === "allow") return how;
    if (verdict === "unauthorized") return err(401, "unauthorized", "authentication required: Authorization: Bearer <key>, or log in at /api/v1/auth/login", {}, { "WWW-Authenticate": 'Bearer realm="espOS"' });
    if (verdict === "origin") return err(403, "forbidden", "cross-site request: Origin does not match Host");
    return throttle429();
  }
  // Health conditions. `drills` holds whatever POST /health/test has raised; a key
  // stays in the map once seen, at "normal", which is what the device does -- a
  // condition keeps its table slot for the life of the boot.
  type HealthState = "normal" | "warn" | "alarm";
  interface Condition { key: string; state: HealthState; message: string; reboot_on_alarm: boolean }
  const healthStates: readonly string[] = ["normal", "warn", "alarm"];
  const isHealthState = (s: string): s is HealthState => healthStates.includes(s);
  const drills = new Map<string, Condition>();
  // Deadlines live apart from the drill objects so they never appear in a response.
  // The device clears a drill on the policy tick once its ttl is up; a mock that
  // kept one raised for ever would teach the UI that drills are permanent.
  const drillDeadlines = new Map<string, number>();
  const expireDrills = () => {
    const now = Date.now();
    for (const [k, due] of drillDeadlines) {
      if (now >= due) {
        const c = drills.get(k);
        if (c) c.state = "normal";
        drillDeadlines.delete(k);
      }
    }
  };
  const healthBody = () => {
    expireDrills();
    const builtins: Condition[] = [
      { key: "lowMemory", state: "normal", message: "", reboot_on_alarm: true },
      // A warning a demo visitor would read as a fault on a healthy device.
      readOnly ? { key: "memoryTrough", state: "normal", message: "", reboot_on_alarm: false }
        : { key: "memoryTrough", state: "warn",
          message: "internal RAM low-water mark below 10 KB since boot", reboot_on_alarm: false },
      { key: "netDown", state: "normal", message: "", reboot_on_alarm: false },
    ];
    const conditions = [...builtins, ...drills.values()];
    const rank: Record<HealthState, number> = { normal: 0, warn: 1, alarm: 2 };
    const worst = conditions.reduce<HealthState>((w, c) => (rank[c.state] > rank[w] ? c.state : w), "normal");
    const fatal = conditions.find((c) => c.state === "alarm" && c.reboot_on_alarm);
    return { worst, fatal: fatal ? fatal.key : null, conditions };
  };
  // What the request body parses to, or null when it does not parse. JSON.parse
  // can only produce Json, which is what lets the handlers narrow it.
  const parseBody = (text: string, empty?: string): Json => { try { return JSON.parse(text || (empty ?? "")) as Json; } catch { return null; } };
  // A member of a parsed body. An array has none of the keys the API uses.
  const field = (o: Json, k: string): Json | undefined => (o && typeof o === "object" && !Array.isArray(o) ? o[k] : undefined);
  const needJson = (h: Headers) => /^application\/json/.test(h["content-type"] ?? "")
    ? null : err(415, "unsupported_media_type", "Content-Type: application/json required");

  function validate(ns: string, k: string, v: Json) {
    const p = schema.properties[ns]?.properties[k];
    if (!p) return `unknown key ${ns}.${k}`;
    if (v === null) return null;
    if (p.type === "boolean" && typeof v !== "boolean") return "expected boolean";
    if ((p.type === "integer" || p.type === "number") && typeof v !== "number") return "expected number";
    if (p.type === "integer" && !Number.isInteger(v)) return "expected integer";
    if (p.type === "string" && typeof v !== "string") return "expected string";
    if (typeof v === "number" && ((p.minimum !== undefined && v < p.minimum) || (p.maximum !== undefined && v > p.maximum))) return `out of range [${p.minimum},${p.maximum}]`;
    if (typeof v === "string" && p.maxLength !== undefined && v.length > p.maxLength) return `longer than ${p.maxLength}`;
    if (p.enum && !p.enum.some((e) => e === v)) return `not one of ${p.enum.join(", ")}`;
    // The generator puts a pattern on string keys only, and a non-string was refused above.
    if (p.pattern && typeof v === "string" && !new RegExp(p.pattern).test(v)) return `does not match ${p.pattern}`;
    return null;
  }

  function route(req: MockRequest): MockResponse {
    const url = new URL(req.path, "http://x");
    const r = url.pathname;
    const m = req.method;
    const h = req.headers;
    const isPublic = r.startsWith("/auth/") || r === "/system/ping";
    const authed = authenticate(m, h, isPublic);
    if (typeof authed !== "string") return authed;
    // Before any handler, so no write can reach the state below: the demo is
    // something to look at. 403 is what a device answers a request it will
    // not carry out, and the UI shows the message like any other refusal.
    if (readOnly && !["GET", "HEAD", "OPTIONS"].includes(m)) return err(403, "read_only", READ_ONLY_MESSAGE);
    // ---- auth
    if (r === "/auth/status" && m === "GET") return json(200, { required: authRequired(), configured: authRequired(), authenticated: authed !== "none", method: authed, recovery_s: 0 });
    if (r === "/auth/login" && m === "POST") {
      const bad = needJson(h); if (bad) return bad;
      if (!authRequired()) return err(409, "auth_open", "no API key is configured; the API is open");
      const doc = parseBody(req.body);
      const key = field(doc, "key");
      if (!doc || typeof key !== "string") return err(400, "validation", "expected {\"key\": \"...\"}");
      const rr = checkKey(key);
      if (rr === "throttled") return throttle429();
      if (rr === "bad") { logAndMark("W", "espos_auth", "login refused: wrong key"); return err(401, "unauthorized", "wrong key"); }
      const sid = crypto.randomUUID().replace(/-/g, "");
      const ttl = Number(cfg("httpd")["session_ttl_s"] ?? 86400);
      if (auth.sessions.size >= 4) auth.sessions.delete(auth.sessions.keys().next().value ?? "");   // oldest first, like the device's LRU
      auth.sessions.set(sid, { expires: Date.now() + ttl * 1000 });
      logAndMark("I", "espos_auth", `login: session opened (${ttl} s)`);
      return { status: 204, headers: { "Set-Cookie": `espos_sid=${sid}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${ttl}`, "Cache-Control": "no-store" }, body: "" };
    }
    if (r === "/auth/logout" && m === "POST") {
      const bad = needJson(h); if (bad) return bad;
      if (auth.sessions.delete(sessionId(h))) logAndMark("I", "espos_auth", "logout: session closed");
      return { status: 204, headers: { "Set-Cookie": "espos_sid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0", "Cache-Control": "no-store" }, body: "" };
    }
    if (r === "/system/ping" && m === "GET") return json(200, { app: profile.app, version: profile.version, auth: authRequired() });
    // ---- system
    if (r === "/system/info" && m === "GET") {
      return json(200, { app: profile.app, version: profile.version, idf_version: "v6.0.2", chip: profile.chip, chip_revision: profile.chipRevision, cores: profile.cores,
        uptime_s: Math.round((Date.now() - boot) / 1000), free_heap: 214000 + Math.round(Math.random() * 3000), min_free_heap: 190000,
        // The internal pair: live, then low-water. Deliberately unrelated to the
        // whole-heap pair above -- the two ask different capability sets and each
        // sums the regions matching it, so nothing guarantees an ordering between
        // them, and a mock that made them equal would teach the UI a rule ESP-IDF
        // does not honour even on this no-PSRAM C6. WITHIN the pair the ordering
        // IS guaranteed (a region's minimum cannot exceed its current free size),
        // so free_internal stays above min_internal_free here.
        free_internal: 181000 + Math.round(Math.random() * 2000), min_internal_free: 176000,
        reset_reason: readOnly ? "power_on" : "software", config_storage_reset: false, schema_etag: etag, ui_storage: true, ui: "embedded",
        // A board that declared itself, so the Board row is exercised; the
        // development C6 has no PSRAM, which is the "no PSRAM" branch rather
        // than a missing one. Both are states a happy-path mock would never show.
        hardware: { mac: profile.mac, cpu_mhz: profile.cpuMhz, flash_bytes: profile.flashBytes,
          flash_physical_bytes: profile.flashPhysicalBytes, flash_partitioned_bytes: profile.flashBytes - 0x10000,
          ram_internal_bytes: profile.ramInternalBytes, ram_psram_bytes: profile.ramPsramBytes,
          features: profile.features,
          // No hardware.coprocessor on any profile. Nothing in ui/src reads
          // the field today; when something does, a profile that has one
          // (a P4) needs the coprocessor object AND the matching health
          // condition as ONE change, rather than a field bolted onto a
          // device whose health says otherwise.
          board: profile.board } });
    }
    if (r === "/system/reboot" && m === "POST") { const bad = needJson(h); if (bad) return bad; logAndMark("W", "espos_httpd", "restarting"); return json(202, { status: "rebooting" }); }
    if (r === "/system/factory-reset" && m === "POST") { const bad = needJson(h); if (bad) return bad; for (const k of Object.keys(stored)) delete stored[k]; wifiEval(); return json(202, { status: "factory_reset", rebooting: true }); }
    if (r === "/system/coredump" && m === "GET") {
      // A device a visitor is shown has not crashed.
      if (readOnly) return err(404, "not_found", "no core dump stored");
      return json(200, { present: true, size: 18344, valid: true, task: "espos_skws", pc: "0x4200a5c2", app_elf_sha256: "3f9c1e2b7d0a44f1", version: 1,
        mcause: 7, mtval: "0x00000010", ra: "0x42009f80", sp: "0x3fc9e2d0", stackdump_bytes: 1024 });
    }
    if (r === "/system/coredump" && m === "DELETE") return json(200, { status: "erased" });
    // ---- config
    if (r === "/config/schema" && m === "GET") {
      if (h["if-none-match"] === `"${etag}"`) return { status: 304, headers: {}, body: "" };
      return { status: 200, headers: { "Content-Type": "application/schema+json", ETag: `"${etag}"` }, body: JSON.stringify(schema) };
    }
    if (r === "/config" && m === "GET") {
      const ns = url.searchParams.get("ns");
      if (ns && !schema.properties[ns]) return err(404, "unknown_namespace", `no namespace ${ns}`);
      return json(200, redacted(ns));
    }
    if (r === "/config" && m === "PUT") {
      const bad = needJson(h); if (bad) return bad;
      let doc: Json; try { doc = JSON.parse(req.body) as Json; } catch { return err(400, "validation", "malformed JSON", { path: "" }); }
      if (!doc || typeof doc !== "object" || Array.isArray(doc)) return err(400, "validation", "expected object of namespaces", { path: "" });
      for (const [ns, o] of Object.entries(doc)) {
        if (!schema.properties[ns]) return err(400, "validation", `unknown namespace ${ns}`, { path: ns });
        if (!o || typeof o !== "object") return err(400, "validation", "expected object", { path: ns });
        for (const [k, v] of Object.entries(o)) { const e = validate(ns, k, v); if (e) return err(400, "validation", e, { path: `${ns}.${k}` }); }
      }
      const before = effective();
      const changed = [];
      let restart = false;
      // The loop above has checked every namespace is an object and every
      // value passes validate(), which admits scalars only.
      for (const [ns, o] of Object.entries(doc)) for (const [k, v] of Object.entries(o as JsonObject)) {
        if (isSecret(ns, k) && v === "********") continue;
        const dst = (stored[ns] ??= {});
        if (v === null) delete dst[k]; else dst[k] = v as Scalar;
        if (JSON.stringify(effective()[ns]?.[k]) !== JSON.stringify(before[ns]?.[k])) {
          changed.push(`${ns}.${k}`);
          if (schema.properties[ns]?.properties[k]?.["x-espos-restartRequired"]) restart = true;
          emit("config", { ns, key: k });
          logAndMark("I", "espos_config", `changed ${ns}.${k}`);
        }
      }
      if (changed.includes("httpd.api_key")) { auth.sessions.clear(); logAndMark(authRequired() ? "I" : "W", "espos_auth", authRequired() ? "API key set: protected endpoints need Bearer or a login" : "no API key set: the REST API is open to the network"); }
      if (changed.some((c) => c.startsWith("wifi."))) wifiEval();
      if (changed.some((c) => c.startsWith("sk."))) skReselect();
      if (changed.some((c) => c.startsWith("sk."))) { if (!cfg("sk")["ws_enabled"]) wsConnect(); else if (sk.token.has_token) wsConnect(); }
      return json(200, { changed, restart_required: restart });
    }
    // ---- wifi
    if (r === "/wifi/status" && m === "GET") return json(200, wifiStatus());
    if (r === "/wifi/scan" && m === "POST") {
      const bad = needJson(h); if (bad) return bad;
      if (scan.scanning) return err(409, "busy", "scan in progress");
      scan = { ...scan, scanning: true };
      later(() => { scanAt = Date.now(); scan = { scanning: false, age_s: 0, results: scanResults }; emit("wifi_scan", scanDoc()); logAndMark("I", "espos_wifi", `scan done: ${scanResults.length} networks`); }, 1500);
      return json(202, { status: "scanning" });
    }
    if (r === "/wifi/scan" && m === "GET") return json(200, scanDoc());
    // ---- health. The device's conditions, and the drill endpoint that lets a
    // hardware test exercise a buzzer/LED without causing a real fault. A
    // deliberately mixed set: one fatal alarm, one non-fatal warning and one
    // condition sitting at normal, because a key keeps its slot once raised.
    if (r === "/health" && m === "GET") return json(200, healthBody());
    if (r === "/health/test" && m === "POST") {
      const bad = needJson(h); if (bad) return bad;
      const b = parseBody(req.body, "{}");
      if (!b || typeof b !== "object") return err(400, "bad_request", "not JSON");
      const key = field(b, "key"), state = field(b, "state"), message = field(b, "message"), ttl = field(b, "ttl_s");
      if (typeof key !== "string" || typeof state !== "string") {
        return err(400, "validation", "key and state are required strings");
      }
      if (!key.startsWith("test.") || key === "test.") {
        return err(400, "validation", 'key must start with "test." and name something');
      }
      if (!isHealthState(state)) {
        return err(400, "validation", "state must be normal, warn or alarm");
      }
      if (message !== undefined && message !== null && typeof message !== "string") {
        return err(400, "validation", "message must be a string");
      }
      if (key.length >= 24 || (message ?? "").length >= 96) {
        return err(400, "validation", "key must be under 24 bytes and message under 96");
      }
      if (ttl !== undefined && ttl !== null) {
        if (typeof ttl !== "number") return err(400, "validation", "ttl_s must be a number");
        if (ttl < 1 || ttl > 300) return err(400, "validation", "ttl_s must be 1..300");
      }
      // The table filling is the one failure a well-formed request still meets,
      // and the UI has to tell the difference between that and a server fault.
      if (key === "test.noslot") {
        return err(507, "no_slot",
          "the condition table is full; reuse a test key or raise CONFIG_ESPOS_HEALTH_MAX_CONDITIONS");
      }
      // One drill at a time: raising a second clears the first.
      for (const c of drills.values()) c.state = "normal";
      drillDeadlines.clear();
      drills.set(key, { key, state, message: message ?? "", reboot_on_alarm: false });
      if (state !== "normal") {
        drillDeadlines.set(key, Date.now() + (ttl ?? 45) * 1000);
      }
      return json(200, healthBody());
    }
    // ---- flow
    // A graph that is doing something slightly wrong, because a page that
    // has only ever been seen in its happy state is a page whose warnings
    // have never been read: the edge pool is deliberately near full and a
    // few posts have been dropped.
    if (r === "/flow" && m === "GET" && profile.flow) return json(200, {
      running: true,
      loop: { posts: 18240, dropped: 3, timers_fired: 9120, timers_live: 4,
        queue_peak: 11, edges_used: 78, edges_max: 96 },
      nodes: [
        { id: "light", title: "Analog(GPIO4)" },
        { id: "cal", title: "Linear(1.7007, -0.165)" },
        { id: "avg", title: "MovingAverage(10)" },
        { id: "out", title: "sk::Output<float>(environment.inside.illuminance)" },
      ],
    });
    // ---- ble (docs/rest-api.md, GET /ble/status)
    if (r === "/ble/status" && m === "GET" && profile.ble) return json(200, bleStatus());
    // ---- signalk
    if (r === "/sk/status" && m === "GET") return json(200, skStatus());
    if (r === "/sk/servers" && m === "GET") return json(200, serversDoc());
    if (r === "/sk/discover" && m === "POST") { const bad = needJson(h); if (bad) return bad; later(skDiscover, 600); return json(202, { status: "discovering" }); }
    if (r === "/sk/request" && m === "POST") { const bad = needJson(h); if (bad) return bad; skRequest(); return json(202, { status: "requesting" }); }
    if (r === "/sk/forget" && m === "POST") { const bad = needJson(h); if (bad) return bad; skForget(); return json(202, { status: "forgotten" }); }
    if (r === "/sk/token" && m === "POST") {
      const bad = needJson(h); if (bad) return bad;
      const doc = parseBody(req.body);
      const token = field(doc, "token");
      if (!doc || typeof token !== "string" || !token) return err(400, "validation", "expected {\"token\": \"...\"}");
      Object.assign(sk.token, { state: "verifying", busy: true }); skEmit();
      later(() => { Object.assign(sk.token, { state: "approved", has_token: true, busy: false, approved_s: 0, last_http_status: 200 }); skEmit(); wsConnect(); }, 900);
      return json(202, { status: "verifying" });
    }
    if (r === "/sk/publish" && m === "POST") { const bad = needJson(h); if (bad) return bad; sk.ws.pending++; return json(202, { status: "queued" }); }
    if (r === "/sk/put" && m === "POST") { const bad = needJson(h); if (bad) return bad; if (!sk.ws.connected) return err(503, "not_connected", "stream not connected"); sk.ws.put.ok++; return json(202, { status: "sent" }); }
    if (r === "/sk/put" && m === "GET") return json(200, sk.ws.put.ok ? { request_id: "9cf791de-0000-4000-8000-000000000001", state: "COMPLETED", status_code: 200, message: "" } : null);
    // ---- ota
    if (r === "/ota/status" && m === "GET") return json(200, otaStatus());
    if (r === "/ota/check" && m === "POST") { const bad = needJson(h); if (bad) return bad; if (["checking", "downloading", "ready"].includes(ota.state)) return err(409, "busy", "an update or check is in progress"); otaCheck(); return json(202, { status: "checking" }); }
    if (r === "/ota" && m === "POST") {
      const bad = needJson(h); if (bad) return bad;
      if (["checking", "downloading", "ready"].includes(ota.state)) return err(409, "busy", "an update or check is in progress");
      const doc = parseBody(req.body, "{}");
      if (!doc || typeof doc !== "object") return err(400, "validation", "expected a JSON object");
      const target = field(doc, "url");
      if (target) { if (typeof target !== "string" || !/^https?:\/\//.test(target)) return err(400, "validation", "expected an http(s) URL"); otaInstall(target); }
      else if (ota.available) otaInstall(ota.available.url);
      else return err(404, "not_found", "no update known; check first");
      return json(202, { status: "installing" });
    }
    if (r === "/ota/confirm" && m === "POST") { const bad = needJson(h); if (bad) return bad; ota.running.pending_verify = false; ota.running.confirmed = true; ota.running.image_state = "valid"; otaEmit(); return json(202, { status: "confirmed" }); }
    if (r === "/ota/rollback" && m === "POST") { const bad = needJson(h); if (bad) return bad; logAndMark("W", "espos_ota", "rollback requested"); Object.assign(ota, { state: "failed", last_error: "rollback requested" }); otaEmit(); return json(202, { status: "rolling_back" }); }
    // ---- logs
    if (r === "/logs" && m === "GET") {
      const after = Number(url.searchParams.get("after") ?? 0);
      const limit = Math.min(Number(url.searchParams.get("limit") ?? 200) || 200, 1000);
      const first = logs[0]?.seq ?? logSeq;
      const gap = after + 1 < first;
      const from = gap ? first : after + 1;
      const lines = logs.filter((l) => l.seq >= from).slice(0, limit);
      return json(200, { first, next: logSeq, dropped: first - 1, size: 16384, used: logs.reduce((a, l) => a + l.line.length + 2, 0), gap, from, lines: lines.map((l) => l.line) });
    }
    if (r === "/logs/level" && m === "PUT") {
      const bad = needJson(h); if (bad) return bad;
      const doc = parseBody(req.body);
      const levels: readonly Json[] = ["none", "error", "warn", "info", "debug", "verbose"];
      const level = field(doc, "level"), tag = field(doc, "tag") ?? "*";
      if (!doc || typeof level !== "string" || !levels.includes(level)) return err(400, "validation", "expected {\"level\": none|error|warn|info|debug|verbose[, \"tag\": \"...\"]}");
      logAndMark("I", "espos_httpd", `log level ${typeof tag === "string" ? tag : JSON.stringify(tag)} = ${level}`);
      return json(200, { tag, level });
    }
    return err(404, "not_found", "no such resource");
  }

  // The BLE counters keep climbing while the radio scans, as they do on a
  // gateway with sensors in range.
  const ble = { scan_hits: 2766, adv_received: 2766, adv_posted: 1840, post_success: 46 };
  const bleStatus = () => ({
    enabled: true, scanning: true, scan_suspended: false, mac: profile.mac.toUpperCase(),
    ...ble, adv_dropped: 0, adv_pending: ble.adv_received - ble.adv_posted > 200 ? 126 : ble.adv_received - ble.adv_posted, post_fail: 0, ws_connected: sk.ws.connected,
    gatt_sessions: 0, gatt_max: 3, gatt_dropped: 0, gatt_pending: 0,
  });
  if (profile.ble) {
    every(() => {
      const seen = 12 + Math.round(Math.random() * 6);
      ble.scan_hits += seen; ble.adv_received += seen; ble.adv_posted += seen; ble.post_success += 1;
      emit("ble", bleStatus());
    }, 3000);
  }

  logAndMark("I", "espos_config", `ready: ${Object.keys(schema.properties).length} namespace(s)`);
  logAndMark("I", "espos_httpd", "listening on :80");
  if (readOnly) {
    // Settled from the first request: a visitor opening the page sees a
    // device at work, not one walking through its first boot.
    wifi.ssid = joined; wifi.network_index = 0; wifi.attempt = 1;
    wifiJoin(joined);
    logAndMark("I", "espos_wifi", `connected: ${joined} ip=${profile.ip} rssi=${String(wifi.rssi)}`);
    skFound();
    const first = discovered[0];
    if (first) {
      skSelect({ ...first, source: "discovered" });
      logAndMark("I", "espos_sk", `discovery: ${discovered.length} server(s)`);
      Object.assign(sk.token, { state: "approved", has_token: true, approved_s: 0, last_http_status: 200 });
      sk.token.counts.requests = 1; sk.token.counts.approved = 1;
      logAndMark("I", "espos_sk", "access APPROVED, token stored");
      wsConnect();
    }
  } else {
    wifiEval();
  }
  every(() => logAndMark("I", "app", `heartbeat ${Math.round((Date.now() - boot) / 1000)}`), 7000);

  return {
    // A promise, so a transport can await it whether or not a handler
    // ever needs to wait.
    handle(req) {
      try {
        return Promise.resolve(route(req));
      } catch (e) {
        return Promise.resolve(err(500, "internal", String(e)));
      }
    },
    events(headers, listener) {
      const authed = authenticate("GET", headers, false);
      if (typeof authed !== "string") return null;
      listener({ event: "wifi", data: wifiStatus() });
      listener({ event: "sk", data: skStatus() });
      listener({ event: "sk_servers", data: serversDoc() });
      listener({ event: "ota", data: structuredClone(otaStatus()) });
      if (profile.ble) listener({ event: "ble", data: bleStatus() });
      listeners.add(listener);
      return () => { listeners.delete(listener); };
    },
    close() {
      closed = true;
      for (const t of [...timers]) stop(t);
      listeners.clear();
    },
  };
}

function hash(s: string) { let h = 0; for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return h; }
