// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// espOS API mock for UI development: mock/core.ts served over node:http,
// with SSE. Zero dependencies.
//
//   node mock/server.ts [port]       (vite dev starts it automatically)
//
// The schema is regenerated from the real descriptors when python3 is
// available (components/espos_config/tools/espos_gen_config.py); otherwise mock/schema.json is used.
import http, { type IncomingMessage, type ServerResponse } from "node:http";
import { spawnSync } from "node:child_process";
import { readFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createMock, type Schema } from "./core.ts";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..", "..");

/** The descriptors the development device builds; paths from the espOS root. */
export const DEV_DESCRIPTORS = ["main/config/app.json", "components/espos_httpd/config/httpd.json",
  "components/espos_wifi/config/wifi.json", "components/espos_sk/config/sk.json",
  "components/espos_ota/config/ota.json"];

/** The schema espos_gen_config.py makes of these descriptors, or null without python3. */
export function generateSchema(descriptors: string[]): Schema | null {
  try {
    const gen = path.join(root, "components", "espos_config", "tools", "espos_gen_config.py");
    const out = mkdtempSync(path.join(tmpdir(), "espos-mock-"));
    const r = spawnSync("python3", [gen, "--schema-out", path.join(out, "s.json"), "--c-out", path.join(out, "c.c"),
      "--h-out", path.join(out, "h.h"), ...descriptors.map((p) => path.resolve(root, p))], { stdio: "ignore" });
    if (r.status === 0) return JSON.parse(readFileSync(path.join(out, "s.json"), "utf8")) as Schema;
  } catch { /* fall through */ }
  return null;
}

function loadSchema(): Schema {
  return generateSchema(DEV_DESCRIPTORS) ?? JSON.parse(readFileSync(path.join(here, "schema.json"), "utf8")) as Schema;
}

const readBody = (req: IncomingMessage) => new Promise<string>((resolve) => { let b = ""; req.on("data", (c: Buffer) => (b += c.toString())); req.on("end", () => resolve(b)); });

function headersOf(req: IncomingMessage): Record<string, string | undefined> {
  const out: Record<string, string | undefined> = {};
  for (const [k, v] of Object.entries(req.headers)) out[k] = Array.isArray(v) ? v.join(", ") : v;
  return out;
}

export function startMock(port = 8484): http.Server {
  const mock = createMock({ schema: loadSchema(), print: (line) => console.log("  [mock] " + line) });

  async function handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const url = new URL(req.url ?? "", "http://x");
    const send = (status: number, body: unknown) => {
      res.writeHead(status, { "Content-Type": "application/json", "Cache-Control": "no-store" });
      res.end(JSON.stringify(body));
    };
    if (!url.pathname.startsWith("/api/v1/")) return send(404, { error: "not_found", message: "no such resource" });
    const r = url.pathname.slice("/api/v1".length) + url.search;
    const headers = headersOf(req);
    const method = req.method ?? "";
    if (url.pathname === "/api/v1/events" && method === "GET") {
      let open = false;
      const close = mock.events(headers, (e) => {
        if (!open) {
          res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-cache", Connection: "keep-alive" });
          res.write("retry: 3000\n\n");
          open = true;
        }
        res.write(`event: ${e.event}\ndata: ${JSON.stringify(e.data)}\n\n`);
      });
      if (!close) {
        // The same refusal a plain request gets, so the browser closes the stream.
        const refused = await mock.handle({ method, path: "/events", headers, body: "" });
        res.writeHead(refused.status, refused.headers);
        res.end(refused.body);
        return;
      }
      const ping = setInterval(() => res.write(": ping\n\n"), 15000);
      req.on("close", () => { close(); clearInterval(ping); });
      return;
    }
    const out = await mock.handle({ method, path: r, headers, body: await readBody(req) });
    res.writeHead(out.status, out.headers);
    res.end(out.body);
  }

  // node:http ignores what a listener returns, so the promise is dropped on
  // purpose; mock.handle() catches everything itself.
  const server = http.createServer((req, res) => { void handle(req, res); });
  server.listen(port, "127.0.0.1", () => console.log(`  [mock] espOS API mock on http://127.0.0.1:${port}/api/v1`));
  return server;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === path.resolve(process.argv[1])) {
  startMock(Number(process.argv[2] ?? 8484));
}
