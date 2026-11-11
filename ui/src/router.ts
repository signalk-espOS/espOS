// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// Where the page is. On a device the path is the route: the device answers
// every unknown path with the bundle, so /wifi survives a reload. The demo
// build replaces this module with src/demo/router.ts, which keeps the route
// in the hash, because the server it is published on answers /wifi with 404.
export const currentPath = (): string => location.pathname;
export const hrefFor = (path: string): string => path;
export function navigate(path: string): void {
  history.pushState(null, "", path);
  dispatchEvent(new PopStateEvent("popstate"));
}
/** Call `cb` whenever the route changes; returns the unsubscribe. */
export function onPathChange(cb: () => void): () => void {
  addEventListener("popstate", cb);
  return () => removeEventListener("popstate", cb);
}
