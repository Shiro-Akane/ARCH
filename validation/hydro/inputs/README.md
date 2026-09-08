# Hydro input sets

- `pcm_n*.par`, `muscl_n*.par` and `ppm_n*.par` compare reconstruction of the
  periodic entropy wave. Representative inputs are [pcm_n64.par](pcm_n64.par),
  [muscl_n64.par](muscl_n64.par) and [ppm_n64.par](ppm_n64.par).
- `sod_ppm_n*.par`, starting with [sod_ppm_n64.par](sod_ppm_n64.par), compare the
  one-dimensional shock tube against its reference solution.

[Hydro validation](../README.md) owns scientific methods, convergence groupings
and budgets. The [backend matrix](../../backend/cases.json) supplies execution
checkpoints and comparisons; Sedov and temporal campaigns link their own
recorded recipes from the module summary.

Initializers live in [simulation/](../../../simulation/README.md). Preserve these
canonical input files and record any case overrides in the execution evidence.
