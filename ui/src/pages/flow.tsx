// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// The data-flow graph: what is wired, and whether the loop is keeping up.
//
// GET /api/v1/flow has existed since the endpoint landed and nothing rendered
// it, so the two numbers that say a graph is in trouble were reachable only
// with curl. Both are invisible from outside and neither raises an alarm:
//
//   edges_used near edges_max  the edge pool is static, so the next
//                              connect_to() fails rather than growing
//   dropped climbing           something posts faster than the loop consumes,
//                              and the mailbox is shedding values
//
// Hence the layout: saturation first, wiring second. A node list is
// reassuring; a full edge pool is the thing you needed to know.
import { get, type FlowStatus } from "../api";
import { Badge, Row, Msg, useAsync } from "../app";

/** Percent full of the edge pool. Guards edges_max 0, which a device without
 *  espos_flow’s Kconfig would report. */
function pct(used: number, max: number): number {
  return max > 0 ? Math.min(100, Math.round((used / max) * 100)) : 0;
}

export function FlowPage() {
  const flow = useAsync(() => get<FlowStatus>("/flow"));
  const f = flow.data;

  const used = f?.loop.edges_used ?? 0;
  const max = f?.loop.edges_max ?? 0;
  const full = pct(used, max);
  // 80% is not a cliff, it is where a firmware still has room to be told. The
  // pool is sized by CONFIG_ESPOS_FLOW_MAX_EDGES, so the fix is a Kconfig
  // value rather than anything on this page.
  const tight = max > 0 && full >= 80;

  return (
    <>
      <h1>Flow</h1>
      <Msg text={flow.error} />
      {!f && !flow.error && <p class="muted">Loading…</p>}
      {f && (
        <div class="grid">
          <section class="card">
            <h2>
              Loop{" "}
              {f.running ? <Badge kind="ok">running</Badge> : <Badge kind="bad">stopped</Badge>}
            </h2>
            {!f.running && (
              <Msg kind="warn" text="The flow loop is not running: espos_flow_start() was never called, or it stopped. Nothing in the graph is being evaluated." />
            )}
            <Row k="Edge pool">
              {used} / {max || "—"}
              {max > 0 && <span class="muted"> · {full}% used</span>}
            </Row>
            {tight && (
              <Msg kind="warn" text={`The edge pool is ${full}% full. It is allocated once at build time, so the next connect_to() fails rather than growing it — raise CONFIG_ESPOS_FLOW_MAX_EDGES.`} />
            )}
            <Row k="Posts">
              {f.loop.posts}
              {f.loop.dropped > 0 && <> · <span class="bad">{f.loop.dropped} dropped</span></>}
            </Row>
            {f.loop.dropped > 0 && (
              <Msg kind="warn" text="Values are being shed: something posts into the loop faster than it consumes. Those readings are gone, not delayed." />
            )}
            <Row k="Queue peak">{f.loop.queue_peak}</Row>
            <Row k="Timers">
              {f.loop.timers_live} live <span class="muted">· {f.loop.timers_fired} fired</span>
            </Row>
          </section>

          <section class="card">
            <h2>
              Nodes{" "}
              {f.nodes === null
                ? <Badge kind="muted">no graph</Badge>
                : <Badge kind={f.nodes.length ? "ok" : "warn"}>{f.nodes.length}</Badge>}
            </h2>
            {f.nodes === null ? (
              <p class="muted">
                This firmware drives the loop from C — timers and mailboxes, no
                typed graph. The counters above still apply.
              </p>
            ) : f.nodes.length === 0 ? (
              <Msg kind="warn" text="A graph exists but has no nodes, so nothing is wired. make<>() was never called, or the Graph went out of scope." />
            ) : (
              <>
                <table>
                  <thead><tr><th>#</th><th>Id</th><th>Title</th></tr></thead>
                  <tbody>
                    {f.nodes.map((n, i) => (
                      <tr key={n.id}>
                        <td class="muted">{i + 1}</td>
                        <td class="mono">{n.id}</td>
                        <td>{n.title || <span class="muted">—</span>}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
                <p class="muted">
                  In adoption order — the order <code>make&lt;&gt;()</code> was
                  called, which is the order the firmware's own source reads in.
                </p>
              </>
            )}
          </section>
        </div>
      )}
      <p>
        <button onClick={flow.reload} disabled={flow.loading}>
          {flow.loading ? "Refreshing…" : "Refresh"}
        </button>
      </p>
    </>
  );
}
