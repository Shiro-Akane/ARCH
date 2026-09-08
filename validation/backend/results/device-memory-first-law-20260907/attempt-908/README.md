# Capacity attempt 908: allocation-lifetime diagnosis

Attempt 908 remains an incomplete capacity campaign. Its first workload,
`arch_cuda_regrid_transaction`, completed both storage configurations and
printed the required PASS markers. The original recipe then rejected the
allocation summary, so the sparse-capacity, sparse-trajectory and application
workloads did not start. This diagnosis does not retrospectively qualify that
campaign. The successor output is reserved as `release-921`; it has not been
run as part of this diagnosis.

## Preserved evidence

The archived [original recipe](replay.py) is byte-identical to the recipe used
for 908, verified with `cmp`. Its SHA-256 is
`07f9552a9bb17b9e0caf3b3a869d71893c1f4cabede5d896a829a73de6e719b4`.
This is an evidence copy, not an executable entry point: moving it one level
deeper changes its relative `ROOT` calculation. Execute the [current recipe](../replay.py).

[diagnosis.json](diagnosis.json) records the selected-process summary, the
reclassification and exact hashes of the original recipe, SQLite export,
stdout and stderr. The raw Nsight export remains in the ignored build tree;
it is not a delivery asset because it can contain unrelated environment data.
Only CUDA memory events, CUDA runtime API results and the selected CUDA
process identity were read for this diagnosis.

| Observation | Result |
|---|---:|
| Successful `cudaMalloc` / `cudaFree` calls | 1,321 / 1,321 |
| Explicit device allocation / release bytes | 1,253,400 / 1,253,400 |
| Explicit device bytes still live in the trace | 0 |
| Static allocation events without release events | 2, totalling 272 B |
| Peak explicit device requests | 531,212 B |
| Peak requests including static symbols | 531,484 B |

The two static events name `__cudart_sin_cos_coeffs` (128 B) and
`__cudart_i2opi_d` (144 B). Both carry
`CUDA_MEMOPR_MEMORY_KIND_DEVICE_STATIC`, `contextId=0` and `correlationId=0`.
All 2,642 explicit allocation/release events have a corresponding successful
runtime API record. No runtime API has a nonzero return value. The final
`cudaFree`, `cudaStreamSynchronize` and `cudaStreamDestroy` calls succeeded.
The trace therefore includes application-owned dynamic cleanup; it does not
directly demonstrate the later destruction of the static symbols.

## Why static storage is separate

The [CUDA 12.3 programming guide](https://docs.nvidia.com/cuda/archive/12.3.0/cuda-c-programming-guide/index.html#device)
assigns device and constant variables the lifetime of their CUDA context.
The [CUDA 12.3 runtime API](https://docs.nvidia.com/cuda/archive/12.3.0/cuda-runtime-api/group__CUDART__MEMORY.html)
defines `cudaFree` for pointers obtained from explicit allocation APIs, not
for arbitrary module symbols. CUPTI distinguishes
[`DEVICE_STATIC` and `MANAGED_STATIC`](https://docs.nvidia.com/cupti/api/group__CUPTI__ACTIVITY__API.html).
The installed CUDA 12.3 headers provide the same definitions:

- `/usr/local/cuda-12.3/targets/x86_64-linux/include/cupti_activity.h:1108`
  and `:1113`: the two static memory kinds.
- `/usr/local/cuda-12.3/targets/x86_64-linux/include/cuda_runtime_api.h:5344`:
  allocations accepted by `cudaFree`.
- The same runtime header at `:295`: `cudaDeviceReset` destroys the current
  process's device resources. No reset has been added to ARCH to alter the trace.

The shared reader remains unchanged. Its `all_allocations_released=false`
accurately describes the observed events, including the static entries.
Only the result recipe's acceptance policy changes: every explicit
`device`, `array`, `managed`, `pageable` and `pinned` allocation must close,
and a real `device` or `array` allocation peak must be present. Static
residuals are retained separately without claiming an observed release.
Their sizes and symbol names do not appear in the policy.

## Reproduction and controls

The reusable interface is `check_allocation_lifetimes(summary)` in the current
recipe. It consumes the unchanged shared reader's result, returns a separate
classification, and raises `ValueError` on inadequate coverage or invalid
lifetime totals. Each new workload record stores this result under
`allocation_lifetimes` beside the original `allocation_summary`.

Run the pure-data controls without launching CUDA:

```bash
python3 -B validation/backend/results/device-memory-first-law-20260907/test_allocation_lifetimes.py -v
```

All 19 tests passed on 2026-09-07. These include all five explicit allocation
kinds leaking, a leak in another process, static-only traces, managed/host-only
traces, unknown kinds, negative or non-integer totals, contradictory release
counts, retention of static residuals, JSON replay and input immutability.
The existing shared reader's five tests also passed unchanged. An AST
comparison confirms that workload commands, input controls, artifacts,
interpreters and the Nsight invocation are unchanged from attempt 908.

The selected-event query used for the counts is reproducible without reading
environment tables:

```sql
SELECT k.name AS kind, o.name AS operation, COUNT(*) AS count,
       SUM(e.bytes) AS bytes, MIN(e.start) AS first_ns, MAX(e.start) AS last_ns
FROM CUDA_GPU_MEMORY_USAGE_EVENTS AS e
JOIN ENUM_CUDA_MEM_KIND AS k ON k.id = e.memKind
JOIN ENUM_CUDA_DEV_MEM_EVENT_OPER AS o ON o.id = e.memoryOperationType
GROUP BY k.name, o.name;
```

The fresh complete capacity campaign is still required. These measurements
describe allocator requests, not physical VRAM residency, driver overhead or
a proof that unrecorded events were released.
