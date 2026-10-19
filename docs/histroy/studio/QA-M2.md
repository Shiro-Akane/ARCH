# M1 / M2 verification

Date: 2026-09-13. Scope: frontend only.

- M1 state tests: edits dirty/stale, invalid/recovery, async revision guard, generation success/failure, idle run.
- M2 provider tests: three distinct finite 512x512 Float32 fields, exact extrema and metadata, hotspot position/radius/amplitude, cancellation and numerical overflow errors.
- npm test: 6/6 passing; lint, typecheck and production build passing.
- Browser acceptance after user permission: generating/current, dirty/stale, invalid input disables Preview, hotspot movement and width/amplitude changes, heatmap and Viridis color bar, zoom/pan/Fit.
- Desktop 1280x720 and mobile 390x844 visual checks pass after fixing renderer container height; no horizontal overflow.
- No browser errors. Upstream THREE.Clock deprecation and >500 kB production chunk warning remain.
- Only Density displayed. No M3 field-switching, M4 selected point, M5 Save/Revert, real data or solver operations.
