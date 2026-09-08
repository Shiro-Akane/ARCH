# Optimized core-build reference

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
authoritative.

The [five-phase record](evidence.json) passes the cold, no-op, incremental and
paired-concurrency checks for candidate source
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`.
It qualifies this build workload; overall release and runtime-capacity acceptance
remain separate.

## Scope and configuration

The measured target is `ARCH`, including the built-in networks, generated
`audit31` and `weak_urca` routes, CPU KLU, CUDA cuDSS and required dependencies.
Tests were configured but their executables were not part of the measured
target. Before the cold phase, none of the 280 configured compile outputs
existed; this is a cold-state check, not a claim that all 280 belong to `ARCH`.

Both the reference and separate measurement tree use Ninja, Release, OpenMP,
CUDA architecture 86 and a GCC 12 host compiler. Compiler caching is disabled.
The heavy-job pool has depth 2 throughout; the total job limit is 4 except for
the serial control, which uses 1. Complete normalized `ARCH` commands and heavy
pool assignments match the reference before and after every phase. The
original `-O3` and optimized LTO link are retained.

## Measurements

Times include the build command and memory guard. All memory columns are KiB.
Owned RSS is the sampled total for the guarded process tree, not a minimum RAM
requirement. Available memory and swap are Linux/WSL system observations.

| Phase | Total jobs | Elapsed (s) | Peak owned RSS | Minimum available RAM | Initial → peak swap |
|---|---:|---:|---:|---:|---:|
| Cold `ARCH` | 4 | 2213.445 | 3763288 | 3439116 | 125264 → 202600 |
| No changes | 4 | 1.012 | 5760 | 7125000 | 184680 → 184680 |
| Ideal/iso7 route + relink | 4 | 27.323 | 1093016 | 6515332 | 184424 → 184424 |
| Ideal/iso7 + Helm/iso7, serial + relink | 1 | 45.320 | 1035936 | 6538696 | 184424 → 184424 |
| Same pair, parallel + relink | 4 | 31.312 | 1018000 | 6491080 | 184424 → 184424 |

All five phases completed without a guard stop. The cold build took about
36.9 minutes, with 3.589 GiB peak owned RSS and 75.5 MiB observed swap growth.
Each command's measured time and maximum RSS are retained in the JSON record.

The incremental measurement recompiles one compact generated dispatch route;
it does not estimate rebuilding all consumers of a common mathematical header.
The two-route comparison performs the same work at both job limits, including
the unchanged optimized final link. Its elapsed time falls by 30.9%; this is
evidence for the measured pair, not a speedup guarantee for other translation
units or a proof that no other concurrency setting could be faster.

The two-heavy/four-total configuration is a measured mid-range reference.
Choose lower limits with less available memory, or measure higher limits on
larger systems. Runtime mesh/network capacity is qualified separately.

## Reproduction

Prepare `audit31` and `weak_urca` using the [network setup](../../../../network/README.md#reproduce-the-records),
then configure a reference build and a fresh, separate measurement tree with
the same compiler, optimization, providers, packages and heavy-job pool.
Use an isolated `ARCH_RUNTIME_OUTPUT_DIRECTORY` for each tree. Build the
reference `ARCH` first; do not build any target in the measurement tree.
The [recipe](../replay.py) rejects existing compile outputs or unequal commands.

For per-command RSS collection, both trees must use the same CMake project
include file containing the following instrumentation. It wraps compiler
commands without changing their numerical flags:

```cmake
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
    "/usr/bin/time -f 'ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C'")
```

Pass its absolute path as `CMAKE_PROJECT_INCLUDE` when configuring both trees.
With the resulting directories named `build/core-reference` and
`build/core-cold`, run from the repository root:

```bash
CCACHE_DISABLE=1 OMP_NUM_THREADS=4 python3 -B \
  validation/backend/results/cold-core-first-law-20260907/replay.py \
  --reference-build build/core-reference --build-dir build/core-cold \
  --incremental-source build/core-cold/generated/cuda_dense_burn/ideal_iso7.cu \
  --comparison-source build/core-cold/generated/cuda_dense_burn/helm_iso7.cu \
  --output-dir validation/backend/results/cold-core-first-law-20260907/my-run \
  --jobs 4 --min-available-mib 1536 --max-swap-growth-mib 256
```

The recipe invokes the existing memory/pressure guard for every phase and
changes only the timestamps of the selected build-generated routes. Use a new
output directory and avoid overlapping another heavy build or GPU campaign.
The [recorded commands](preflight/measurement/normalized-commands.txt) and
[evidence](evidence.json) preserve the measured configuration and dependencies.
