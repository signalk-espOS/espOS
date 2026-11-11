// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// The demo's transport: every request is answered by the mock in this page,
// read-only, so the UI runs with no device and no server behind it. Nothing
// leaves the browser. Stands in for src/transport.ts in `npm run build:demo`.
import { createMock, type MockEvent, type Schema } from "../../mock/core.ts";
import { profileFromQuery } from "./profiles";

// One schema per device, generated from that firmware's config descriptors
// by scripts/demo-schemas.ts before the build. A glob rather than imports, so
// the app typechecks before they exist.
const schemas = import.meta.glob<Schema>("../../demo-gen/*.schema.json", { eager: true, import: "default" });

const { id, profile } = profileFromQuery(location.search);
const schema = schemas[`../../demo-gen/${id}.schema.json`] ?? { properties: {} };
const mock = createMock({ schema, profile, readOnly: true });

export async function request(url: string, init: RequestInit): Promise<Response> {
  const path = url.replace(/^.*?\/api\/v1/, "");
  const headers: Record<string, string | undefined> = {};
  for (const [k, v] of Object.entries((init.headers ?? {}) as Record<string, string>)) headers[k.toLowerCase()] = v;
  const out = await mock.handle({
    method: init.method ?? "GET",
    path,
    headers,
    body: typeof init.body === "string" ? init.body : "",
  });
  // A 204 or 304 may not carry a body; Response refuses one, even empty.
  const empty = out.status === 204 || out.status === 304;
  return new Response(empty ? null : out.body, { status: out.status, headers: out.headers });
}

/** Just enough of EventSource for api.ts: named listeners, onopen, onerror. */
class MockEventSource extends EventTarget {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSED = 2;
  readyState = MockEventSource.CONNECTING;
  onopen: ((e: Event) => void) | null = null;
  onerror: ((e: Event) => void) | null = null;
  private stop: (() => void) | null = null;

  constructor() {
    super();
    // After the caller has attached its listeners, as a real stream would.
    setTimeout(() => {
      if (this.readyState === MockEventSource.CLOSED) return;
      this.stop = mock.events({}, (e: MockEvent) => this.deliver(e));
      this.readyState = MockEventSource.OPEN;
      this.onopen?.(new Event("open"));
    }, 0);
  }
  private deliver(e: MockEvent) {
    // Its own task, like a network event, so a listener never runs inside
    // the mock's state change that raised it.
    setTimeout(() => this.dispatchEvent(new MessageEvent(e.event, { data: JSON.stringify(e.data) })), 0);
  }
  close(): void {
    this.readyState = MockEventSource.CLOSED;
    this.stop?.();
    this.stop = null;
  }
}

export const openEvents = (): EventSource => new MockEventSource() as unknown as EventSource;
