// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// Type-aware linting for the TypeScript sources: the rules worth having here --
// no floating promises, no unsafe `any` reaching a DOM call, no value stringified
// into "[object Object]" -- all need the type checker, and a lint that cannot see
// types mostly restates what tsc already refuses.
//
// The .mjs files (the API mock and the gzip step) are linted without type
// information: they are Node scripts outside the app's tsconfig, and pulling a few
// hundred lines of deliberately loose mock code into it would buy nothing.
import js from "@eslint/js";
import globals from "globals";
import reactHooks from "eslint-plugin-react-hooks";
import tseslint from "typescript-eslint";

export default tseslint.config(
  { ignores: ["dist/**", "node_modules/**"] },
  js.configs.recommended,
  {
    files: ["**/*.{ts,tsx}"],
    extends: [...tseslint.configs.recommendedTypeChecked],
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
    rules: {
      // tsc already resolves every name, and with DOM in `lib` it knows the ones
      // this rule does not. Leaving it on reports `URL` and friends as undefined.
      "no-undef": "off",
      /* `onClick={async () => ...}` is how an async handler is written in Preact,
       * and the framework ignores the returned promise by design. Left on, this
       * rule fires on every such handler and says nothing a reader can act on --
       * its own documentation names JSX attributes as the case for this option.
       * The part that matters, a promise passed where a synchronous function is
       * genuinely required, stays on. */
      "@typescript-eslint/no-misused-promises": [
        "error",
        { checksVoidReturn: { attributes: false } },
      ],
    },
  },
  {
    // The hooks rules, which are the reason to lint a hooks UI at all: a stale
    // closure over a dependency nobody listed is invisible on review and shows up
    // as a panel that silently stops updating. src/app.tsx already carried a
    // disable comment for exhaustive-deps, written for a rule that was never
    // actually running.
    files: ["**/*.{ts,tsx}"],
    plugins: { "react-hooks": reactHooks },
    rules: {
      "react-hooks/rules-of-hooks": "error",
      "react-hooks/exhaustive-deps": "warn",
    },
  },
  {
    files: ["**/*.mjs"],
    extends: [tseslint.configs.disableTypeChecked],
    languageOptions: { globals: globals.node },
  },
);
