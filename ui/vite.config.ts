// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { defineConfig, type Plugin } from "vite";
import preact from "@preact/preset-vite";
import checker from "vite-plugin-checker";
import { startMock } from "./mock/server.ts";
import { PROFILES } from "./src/demo/profiles.ts";

// `npm run dev` talks to ESPOS_API (a device or the host harness) when set,
// otherwise to the in-process mock so no ESP32 is needed to work on the UI.
const target = process.env["ESPOS_API"] ?? "http://127.0.0.1:8484";

function mockPlugin(): Plugin {
  return {
    name: "espos-mock",
    apply: "serve",
    configureServer() {
      if (!process.env["ESPOS_API"]) startMock(8484);
    },
  };
}

/* Vite marks the module script and the stylesheet `crossorigin`, which makes
 * the browser fetch them in CORS mode. The device serves no
 * Access-Control-Allow-Origin -- it has no reason to, the bundle is same
 * origin -- so a browser that takes that attribute at its word blocks the
 * script and renders nothing at all.
 *
 * A normal browser does not care, because same-origin CORS requests succeed
 * without the header. iOS's captive-portal webview does: on the setup portal
 * the page loaded, the module was blocked, and the result was a black page
 * with no console error to explain it. Strip the attribute; it buys nothing
 * for a bundle served by the device it talks to. */
function stripCrossorigin() {
  return {
    name: "espos-strip-crossorigin",
    enforce: "post" as const,
    transformIndexHtml(html: string) {
      return html.replace(/\s+crossorigin(=("|')[^"']*\2)?/g, "");
    },
  };
}

const src = (p: string) => fileURLToPath(new URL(`./src/${p}`, import.meta.url));

/* `--mode demo` (npm run build:demo): the same UI, answered by the read-only
 * mock in the page instead of a device, for a page that shows what a device
 * looks like with none behind it. The two modules that reach outside the page
 * -- the transport and the router -- are swapped for their demo versions here
 * rather than switched on a flag inside them, so not a byte of the demo ends
 * up in the bundle a device serves. */
function demoPlugin(): Plugin {
  const swap: Record<string, string> = {
    [src("transport.ts")]: src("demo/transport.ts"),
    [src("router.ts")]: src("demo/router.ts"),
  };
  return {
    name: "espos-demo",
    enforce: "pre",
    // The demo falls back to an empty schema at run time; at build time a
    // missing one is a mistake, made by running vite without build:demo.
    buildStart() {
      for (const id of Object.keys(PROFILES)) {
        if (!existsSync(fileURLToPath(new URL(`./demo-gen/${id}.schema.json`, import.meta.url)))) {
          this.error(`demo-gen/${id}.schema.json is missing: build with npm run build:demo`);
        }
      }
    },
    async resolveId(source, importer, options) {
      // Only the two specifiers that can name a swapped module are resolved
      // again; everything else is left to Vite.
      if (!importer || importer.includes("/src/demo/") || !/\/(transport|router)$/.test(source)) return null;
      const resolved = await this.resolve(source, importer, { ...options, skipSelf: true });
      return resolved ? (swap[resolved.id] ?? null) : null;
    },
    // "pre", so the entry is swapped before Vite reads the scripts out of
    // the page; later, the build has already bundled src/main.tsx.
    transformIndexHtml: {
      order: "pre",
      handler: (html: string) => html.replace('src="/src/main.tsx"', 'src="/src/demo/main.tsx"'),
    },
  };
}

export default defineConfig(({ mode }) => ({
  // Relative, because the demo is published under a path chosen by whoever
  // serves it; the device bundle is served from its root.
  base: mode === "demo" ? "./" : "/",
  plugins: [
    preact(),
    /* Type errors in the dev overlay, which `npm run dev` otherwise never
     * showed: Vite strips types without checking them, so a mistake stayed
     * invisible until someone ran `npm run build`.
     *
     * Dev only, and the app only (tsconfig.json): the Node-side files under
     * tsconfig.node.json are left to `npm run typecheck`. `npm run build` runs
     * that first, and it is the better gate -- it fails in seconds, before Vite
     * spends time bundling something that cannot ship. Letting the plugin check
     * the build as well would run tsc again for no extra safety. */
    checker({ typescript: true, enableBuild: false }),
    mockPlugin(),
    stripCrossorigin(),
    ...(mode === "demo" ? [demoPlugin()] : []),
  ],
  build: {
    outDir: mode === "demo" ? "dist-demo" : "dist",
    emptyOutDir: true,
    sourcemap: false,
    target: "es2020",
    cssCodeSplit: false,
  },
  server: {
    port: 5173,
    proxy: { "/api": { target, changeOrigin: true, ws: false } },
  },
  preview: {
    port: 4173,
    proxy: { "/api": { target, changeOrigin: true, ws: false } },
  },
}));
