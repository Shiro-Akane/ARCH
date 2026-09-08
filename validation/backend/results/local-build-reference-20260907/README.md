# Local heavy-compile concurrency reference

Contributor measurement, 2026-09-07 JST. This is the focused comparison used
to choose the current mid-range build configuration, not a complete cold-build
or release-capacity certificate. The shared Helm correction was made after
these measurements; final-candidate build/resource qualification remains open.

The reference machine is WSL2 on an i7-10700-class CPU with 16 GB host RAM
and an RTX 3060 Ti 8 GB GPU. WSL exposed 7,892 MiB RAM and 2,048 MiB swap.
Each run compiled the same two large aprox21 dense/sparse Tabular4D routes
from the actual compilation database into fresh temporary outputs. Both runs
bypassed ccache, preserved the compiler's optimization/precision/image flags,
and had no other heavy validation workload running. Exact compiler arguments
and both successful exit codes are retained in the logs.

| Heavy jobs | Guarded elapsed | Peak summed owned RSS | Minimum available RAM | Additional swap |
|---|---:|---:|---:|---:|
| 1 | 287.928 s | 1,951,348 KiB | 4,659,456 KiB | 0 KiB |
| 2 | 159.564 s | 3,882,728 KiB | 3,095,800 KiB | 0 KiB |

Two heavy jobs reduce this group's elapsed time by 44.6%. The selected local
Ninja configuration is `ARCH_CUDA_HEAVY_COMPILE_JOBS=2` with a total
`--parallel 4` ceiling for heavy and lighter jobs together. The isolated
comparison establishes the heavy-pool choice; it does not independently
establish the optimal total-job ceiling or predict a full clean-build time.
Those require the complete-build measurements in the active release plan.
Keep the memory-conservative default on smaller environments. Larger machines
can raise the two existing limits without changing any numerical implementation.

The common guard reserved 1,536 MiB of available RAM, allowed 256 MiB of
additional swap, and watched sustained Linux PSI full stalls. A few swapped
pages do not trigger a stop. The pressure limits were 20% memory or 50% I/O
full stalls for ten consecutive seconds. These describe WSL-visible stalls,
not Windows disk utilization. Only descendants of the guarded command may be
stopped; unrelated applications are not terminated.

## Retained records and reproduction

- `jobs1.log` and `jobs2.log`: original per-command inputs, results and guard
  telemetry. The baseline swap allocation was 57,880 KiB in both runs.
- `replay.py`: the original local two-command measurement helper. It does not
  modify build objects or provide another process/memory supervisor.

Run this helper under `tools/run_memory_guarded.py`, using an already configured
build containing the two named routes. Substitute the chosen build directory
and run once with each `--jobs` value; do not run the controls concurrently.

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  python3 validation/backend/results/local-build-reference-20260907/replay.py \
    --build-dir build-cuda --jobs 1
```

These historical logs did not capture an immutable complete source identity.
Do not retroactively assign them the current tree's fingerprint. A new run
measures its actual configured sources; final acceptance uses recorded source,
build and dependency identities together with isolated whole-build telemetry.
