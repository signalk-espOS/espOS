// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// How api.ts reaches the device. The demo build (`npm run build:demo`)
// replaces this module with src/demo/transport.ts, which answers from the
// mock in the page instead, so none of the demo reaches a device's bundle.
export const request = (url: string, init: RequestInit): Promise<Response> => fetch(url, init);
export const openEvents = (url: string, init: EventSourceInit): EventSource => new EventSource(url, init);
