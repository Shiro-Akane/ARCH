# Studio and Host verification

Run from `studio/` using Linux Node 24+:

```bash
npm ci
npm run lint
npm run build
npm test
```

`npm run build` includes TypeScript checking; use `npm run typecheck` alone when production assets are not needed.

The complete `npm test` includes `host-*.test.ts`; `npm run test:host` is a scoped subset and must not be counted again. Tests cover configuration/workspace, Preview/cancellation, renderer, Plotfile queries, controlled build and owned run/restart lifecycle. Browser/HTTP tests do not prove native desktop interaction or physical accuracy.

CI runs this complete suite, lint and the typed production build for changes affecting Studio, Core interfaces or build contracts. Independent numerical, scientific-validation and documentation changes can omit the Node lane after a conservative path check. Missing revision information, unknown paths or a failed diff select the complete lane. The existing Studio job records its scope; `NOT_APPLICABLE` reports a scope decision rather than a passing test run. See the [workflow guide](../../.github/workflows/README.md).

CI uses `node --test --test-concurrency=1 --test-reporter=tap tests/*.test.ts` to keep independent Host toolchain probes from competing on a small runner. Use the same command locally when CPU resources are shared. Every file and assertion remains selected; file concurrency changes resource scheduling only.

`fixtures/` holds bounded regression inputs, including a pinned 14 KiB historical CPU Sod plotfile used by reader tests. Its provenance is in [fixtures/README.md](fixtures/README.md); it is not a production sample or a new scientific run. `tools/compareRestartCheckpoints.mjs` compares explicitly selected checkpoint files; `tools/import_cellular.py` is a historical one-run sample importer, not a production reader. Newly generated raw scientific files remain in the operator's local output directory.

Core tests live in [tests/](../../tests/README.md); scientific references and runner evidence in [validation/](../../validation/README.md). Production sources are `../src`, `../host`, and `../desktop`. Linux packages contain production assets, Host imports and required runtime source; they exclude this test tree, scientific validation and historical reports.
