# Focused CUDA sanitizer campaign

Contributor evidence, 2026-09-07 JST. This campaign checks the actual configured
executables after the curved restriction and backend accounting corrections.
The [recipe](replay.py) obtains commands from CTest, uses the existing provenance
and subprocess helpers, and runs each executable directly under Compute
Sanitizer. CUDA initialization is mandatory. Missing reports, nonzero exits,
warnings, errors or nonzero leaks fail the campaign.

The [memory-check record](memcheck/evidence.json) passes all 18 routes with zero
errors and zero leaked bytes. Coverage includes staged regrid rollback and
migration, composition, nine geometry/dimension exchange combinations, geometric
operators, indicators, hydro/diffusion, EOS errors, NSE, cuDSS and sparse batching,
both representative generated networks, and all three aprox19 ODE routes.
The report fingerprints the executables, source tree, generated packages,
configured libraries, sanitizer executable and replay recipe before/after use.

The [race-check record](racecheck/evidence.json) also passes all 18 routes,
with zero errors and warnings. No cases or matrix sizes were filtered out.
The sparse-batch check includes the existing manufactured 32/151/201-equation
systems and all three shared ODEs; these are not large generated-network
trajectory or scaling claims.

## Resource observations

| Campaign | Guarded elapsed | Peak summed owned RSS | Minimum available RAM | Additional swap | Whole-device peak GPU memory |
|---|---:|---:|---:|---:|---:|
| memcheck | 135.627 s | 568,224 KiB | 6,373,580 KiB | 256 KiB | 3,131 MiB |
| racecheck | 1,141.252 s | 775,580 KiB | 6,151,016 KiB | 768 KiB | 5,279 MiB |

Both guard runs complete without a pressure stop. Device memory includes the
display/driver baseline, not just this test. Racecheck overlaps a brief CPU-only
independent-reference check; neither campaign is a production throughput or
isolated capacity benchmark. Instrumented timing does not describe normal runs.

## Reproduction

Run from the repository root with a new output directory:

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  python3 validation/backend/results/accounted-sanitizer-20260907/replay.py \
    --build-dir build-cuda --output-dir build/sanitizer-memcheck \
    --sanitizer /usr/local/cuda/bin/compute-sanitizer --tool memcheck
```

Use `--tool racecheck` and a different output directory for the same race-check
matrix. Every case retains separate application stdout/stderr and its sanitizer
report. There are no kernel filters, forced blocking launches or altered physics.
These are focused backend checks; full ARCH/restart sanitizer coverage and
sustained resource qualification remain separate release requirements.
