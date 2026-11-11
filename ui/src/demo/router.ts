// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// The demo's router: the route lives in ?page=, beside ?device=. The demo is
// published as static files under some path of someone else's server, which
// answers /wifi with 404, so the page itself never leaves index.html. Not the
// hash: the Config page keeps its namespace there.
const PARAM = "page";

export function currentPath(): string {
  const p = new URLSearchParams(location.search).get(PARAM) ?? "/";
  return p.startsWith("/") ? p : "/";
}
export function hrefFor(path: string): string {
  const q = new URLSearchParams(location.search);
  if (path === "/") q.delete(PARAM); else q.set(PARAM, path);
  const s = q.toString();
  return location.pathname + (s ? `?${s}` : "");
}
export function navigate(path: string): void {
  history.pushState(null, "", hrefFor(path));
  dispatchEvent(new PopStateEvent("popstate"));
}
export function onPathChange(cb: () => void): () => void {
  addEventListener("popstate", cb);
  return () => removeEventListener("popstate", cb);
}
