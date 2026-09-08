# Current-constant focused evidence — internal acceptance record

This bundle is contributor/auditor evidence, not the public capability guide.
The [summary](../../README.md) describes the user-visible scope.

## Observed result

[focused-tests.json](focused-tests.json) records five passing tests:
`physical_constants`, `sparse_ode_continuation`, `cuda_burn_thermal_math`,
`eos_host_device_parity`, `network_nse_device`. The CUDA tests actually executed;
skips, absent tests and failures are rejected by the recording harness. Source,
configuration, selected unit binaries and declared inputs were observed before
and after the run and required to remain unchanged.

This is an observation of the recorded Debug unit artifacts, not an attestation
that the full ARCH application was rebuilt from the captured dirty worktree.
The configured custom-network list is provenance, not a claim of weak/large
trajectory coverage. `release_qualified` is deliberately false.

## Independent reference contract

- NSE: independent direct Saha roots at 70/90 decimal digits, using current
  project constants and the unchanged built-in nuclear data/conversion contract.
  The four-network abundance/energy criterion is 2e-12 relative. Existing
  reaction snapshots and 2e-12 Host/Device comparisons are unchanged. Rejected
  states still require exact output sentinels and zero returned energy.
- Helmholtz: independent monomial endpoint-fit table reconstruction at 60/80
  digits, exact inverse-energy quantities and a pressure-coordinate DOP853
  isentrope. Data live in `tests/fixtures/HelmReference.h` and are generated only
  by the independent validation calculation, never production output.
- Reference reconciliation is explicit: independently rounded cv/xne and
  analytic fallback values use small arithmetic windows; inverse energy uses
  the existing 8e-15 inversion criterion. Path sound speed uses the existing
  general 4e-10 independently differenced-state criterion, replacing the former
  1.2e-10 same-implementation path snapshot margin. The initial path comparison
  failed at 1.58e-10. Do not describe this as retaining every historical margin.
- Actual EOS Host/Device comparison budgets are unchanged. The independent
  reference criterion and backend parity criterion are different obligations.

Reproduce reference calculations with `validation/eos/helm_reference.py` and
`validation/network/nse_reference.py`. Neither updates fixtures automatically.
The exact table and reference-source hashes are in the JSON record. The
historical old-constant source fails the independent NSE reference; this
negative diagnostic is recorded in the internal development ledger.

## Open gates

Final application and Debug/Release qualification; full EOS domains and coupled
physics; complete burn trajectories and energy handoff; restart, sanitizer and
capacity. These focused results do not close those gates.
