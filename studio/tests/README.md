# Studio and Host verification

Run from `studio/` using Linux Node 24+:

```bash
npm ci
npm run lint
npm run build
npm test
```

`npm run build` includes TypeScript checking; use `npm run typecheck` alone when production assets are not needed.

Host tests also perform small real builds and verify GNU subprocess/specs and the selected mold linker. Install the Linux CMake, Ninja, C++ and mold tools described in the [environment guide](../../docs/guides/StudioEnvironment.zh-CN.md); the fixtures deliberately use `/usr/bin/cmake`, `/usr/bin/ninja`, `/usr/bin/c++` and `ld.mold`. Missing tools are test prerequisites to fix, not reasons to skip identity assertions.

The complete `npm test` includes `host-*.test.ts`; `npm run test:host` is a scoped subset and must not be counted again. Tests cover configuration/workspace, Preview/cancellation, renderer, Plotfile queries, controlled build and owned run/restart lifecycle. Browser/HTTP tests do not prove native desktop interaction or physical accuracy.

CI runs this complete suite, lint and the typed production build for changes affecting Studio, Core interfaces or build contracts. Independent numerical, scientific-validation and documentation changes can omit the Node lane after a conservative path check. Missing revision information, unknown paths or a failed diff select the complete lane. The existing Studio job records its scope; `NOT_APPLICABLE` reports a scope decision rather than a passing test run. See the [workflow guide](../../.github/workflows/README.md).

`npm test` uses `run-platform.mjs` to enumerate the same root `*.test.ts` inventory, with TAP output and file concurrency of one. On Linux it reports the five declared native-only files as `NOT_APPLICABLE` on stderr and runs every shared/mixed file, including `host-windows-run.test.ts`. Unknown/new files are selected by default; an empty inventory or stale declared filename fails. Keep native-only files free of shared assertions. Mixed files register their OS-specific cases only on the applicable OS; assertion bodies and missing-tool checks remain unchanged. An inapplicable case is never a passing test.

CI still invokes `npm --silent test` and validates the complete selected TAP report with the unchanged strict zero-skip gate. Do not use name patterns, fabricated empty-file passes or an allowed-skip count to obtain acceptance. Native Windows process/helper tests require the trusted tools documented in the [platform guide](../../docs/guides/WindowsStudioPlatform.md); the Linux full-suite result does not qualify those cases. Native focused groups retain their separate W0–W3 evidence in the [archive](../../docs/histroy/build/WindowsNativeQualification-20261026.zh-CN.md).

`fixtures/` holds bounded regression inputs, including a pinned 14 KiB historical CPU Sod plotfile used by reader tests. CI materializes only that required LFS fixture through the shared exact-pointer cache action, then verifies its actual size/SHA. A pointer left in place fails verification before Node execution. Workstation use reuses the matching local copy without automatic fetch. Its provenance is in [fixtures/README.md](fixtures/README.md); it is not a production sample or a new scientific run. `tools/compareRestartCheckpoints.mjs` compares explicitly selected checkpoint files; `tools/import_cellular.py` is a historical one-run sample importer, not a production reader. Newly generated raw scientific files remain in the operator's local output directory.

Core tests live in [tests/](../../tests/README.md); scientific references and runner evidence in [validation/](../../validation/README.md). Production sources are `../src`, `../host`, and `../desktop`. Linux packages contain production assets, Host imports and required runtime source; they exclude this test tree, scientific validation and historical reports.
