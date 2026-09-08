# Sanitizer recipe before explicit sparse racecheck profiles

[run_sanitizers.py](run_sanitizers.py) is a byte-for-byte archive of the recipe
before the racecheck-only observation option was added. Its SHA-256 is
`f52f31ac9fe366f6f1cd3dc58564269a24e28a1dee3323e6cccb080a5c0a71f2`.
It retains the existing per-route timeout option. The separate `attempt-902`
archive and its earlier failure records have not been changed.

The [current recipe](../../run_sanitizers.py) still uses the original `1e-10`
sparse interval by default, for both memcheck and racecheck. An explicit
`--tool racecheck --sparse-race-interval 1e-12` selects a shorter instrumentation
observation, without changing the separately measured scientific trajectories.
Memcheck rejects this option, even when the supplied value equals the default.
Malformed, nonfinite and nonpositive intervals are rejected before execution.

All 23 routes remain present. The other 22 commands are unchanged, as are sparse
density, temperature, heat capacity, tolerance, composition, all three ODEs,
two storage sizes and four subdivisions. Error and leak/hazard checks still use
the shared sanitizer boundary. The sparse route also passes its complete
transcript through `validation/network/run_sparse_validation.py::parse_transcript`,
which checks coverage, positive kernel work, pool metrics, measurable evolution
and the original parity budgets. A shorter interval receives no automatic pass.

The evidence records `sparse_profile` at the top level and `sparse_summary` in
the `audit31_sparse_cells` record. The profile includes purpose, selection mode,
the exact argument tokens, numerical controls, methods, storage sizes, steps
and composition. The existing acceptance index can reuse `sparse_profile`,
`sanitizer_commands` and the imported `sparse_validation` parser from the current
recipe; this archive adds no second verifier.

The archive is for review, not direct execution from its deeper directory.
CPU-only command, parser and failure-path tests run from the repository root:

```bash
python3 -B validation/backend/results/final-first-law-20260907/test_run_sanitizers.py
```

Actual instrumentation must be dispatched separately, serially under the
existing resource guard. Keep the same Python version for the runner and the
final index when comparing the parser's diagnostic timing sums.
