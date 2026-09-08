# CUDA allocator capacity validation

[中文](README.zh-CN.md)

Campaign 926 passed all four workloads on the frozen release source. It measured
CUDA allocation requests while keeping the normal regrid, sparse-solver, burn
and CPU/GPU checkpoint checks active. Every observed dynamic allocation was
released at the end of its selected process trace. The executable, profiler and
launcher identities were verified before and after collection.

The [complete evidence](release-926/evidence.json) is a focused capacity result;
the [final acceptance index](../final-acceptance-20260907/) combines it with the
other release gates. This result alone does not mark the release as qualified.

## Workloads and measured allocation peaks

| Workload | Checks retained during profiling | Peak dynamic device requests | Largest individual allocation |
|---|---|---:|---:|
| Regrid transaction | 4 and 41 species; survivor, refine, restrict, rollback and stale-host handling | 531,212 B (0.507 MiB) | 52,480 B |
| cuDSS sparse capacity | 16,384 rows, 49,150 nonzeros; solve, reuse, refactor and negative contracts | 190,708,688 B (181.874 MiB) | 78,640,800 B |
| Generated audit31 burn | Three ODE methods, storage sizes 2 and 3, CPU KLU/GPU cuDSS parity | 9,401,860 B (8.966 MiB) | 8,388,608 B |
| AMR application | 3D RK3 hydro at steps 1/2; five-stage RKL2 diffusion at steps 1/2/5 | 300,773,640 B (286.840 MiB) | 5,406,720 B |

The AMR row reports the largest peak among five separate CUDA processes; their
peaks are not summed as though they were simultaneous. MiB means 1,048,576 bytes.
The sparse matrix is a measured capacity case, not a maximum supported matrix
size: factorization memory also depends on sparsity and fill-in.

The burn trajectory uses **64 subdivisions of the same total physical interval
of 1e-10**, with the original density, temperature, composition and tolerances.
Its largest field and limiter errors were 2.099e-14 and 2.102e-14, respectively,
within the existing 2e-10 and 2e-8 budgets. All five AMR CPU/GPU checkpoint
comparisons passed; their CUDA traces contained no unfinished transfers or stale
ghost publications. See the [burn evidence](release-926/sparse-validation/evidence.json)
and [AMR evidence](release-926/amr-validation/backend-validation-evidence.json).

## What the memory result means

These are process-local allocator requests, not physical VRAM residency. The
profiler also recorded small static device objects belonging to loaded CUDA
modules: 272 B for regrid, 305 B for sparse capacity, 577 B for audit31, and
816–1,088 B per AMR process. They are distinct from dynamic buffers. Their
teardown was not recorded, so the original `all_allocations_released=false`
remains in the evidence, with static residuals listed separately; no release
event is invented. All observed dynamic device buffers ended at zero live bytes.

The memory guard completed without stopping the campaign. Minimum available
Linux RAM was 5,354,144 KiB, peak owned-process RSS was 1,903,980 KiB, and swap
grew from 197,664 to 200,480 KiB. Whole-device GPU usage rose from 1,527 to
2,306 MiB. This separate device-wide observation includes the desktop, context
and driver allocations; it must not be equated with the table above. The
instrumented campaign took 293.606 seconds; this is not a runtime benchmark.
The original guard output is retained locally in
[capacity-final-926.log](../../../../build/capacity-final-926.log).

## Identity and reproducibility

The stored classification was independently replayed from all four records.
Each selected CUDA PID was matched to its observed full executable path and
file object, then to the frozen workload SHA-256. A short Nsight process name
is retained as reported but is not used to guess an executable. Regrid and
sparse capacity included the explicitly pinned Nsight launcher's exec handoff;
the remaining processes were observed directly in their workload image.
The launcher exception only permits that exact object strictly before the
unique workload image. Unknown, changed, overlapping or ambiguous images fail.

The scientific task definitions, executable inventory and Nsight invocation
are unchanged from archived attempts 908, 921, 922 and 925. Only result-side
allocation-lifetime classification, executable observation and declared
instrumentation handling were repaired. The shared memory reader and ARCH
physics/backend implementation were not changed for these repairs.

The captured invocation below uses a fresh output directory for reproduction.
Use the corresponding Python, Nsight and launcher paths on the machine running
the test. The guard settings are the recorded campaign settings, not application
parameters or a hardware-specific numerical configuration.

```bash
env OMP_NUM_THREADS=4 python3 -B tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 \
  --pressure-guard --gpu-memory-device 0 \
  --nvidia-smi /usr/lib/wsl/lib/nvidia-smi \
  --log build/capacity-replay.log -- \
  python3 -B validation/backend/results/device-memory-first-law-20260907/replay.py \
  --build-dir build/release-core-throughput-cmake \
  --output-dir validation/backend/results/device-memory-first-law-20260907/release-replay \
  --scientific-python /home/shiroakane/miniconda3/envs/p311/bin/python \
  --profiler /usr/local/bin/nsys \
  --profiler-launcher /opt/nvidia/nsight-systems/2023.3.3/target-linux-x64/nsys-launcher
```

Run only one GPU validation campaign at a time. The output directory must not
already contain results. Raw Nsight databases stay in the ignored build tree
because they can contain unrelated environment metadata; the retained JSON
contains only the selected CUDA processes and relevant allocation summaries.

## Audit trail

Earlier attempts remain failed and incomplete; the successful successor does
not retroactively qualify them. Each archive retains the original recipe and
diagnosis: [908: static lifetime classification](attempt-908/README.md),
[921: truncated process name](attempt-921/README.md),
[922: missing failure capture](attempt-922/README.md), and
[925: observed profiler-launcher handoff](attempt-925/README.md).

The result-side policy controls passed 39 process-identity tests and 19
allocation-lifetime tests before campaign 926. These include rejection of
dynamic leaks, missing dynamic coverage, unknown kinds, wrong executable
objects, PID reuse, and invalid launcher ordering.

| Frozen item | SHA-256 |
|---|---|
| Source worktree scope | `73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171` |
| [Recipe](replay.py) | `36437cb84a410b72bc2b8a33b325317d8384980515095ee9ddd9dbb0a4ff4936` |
| [Campaign evidence](release-926/evidence.json) | `5458ae9f5a26bd7beebbea5d2a271f20acb8c425aadb32ed730eced8af124e8c` |
