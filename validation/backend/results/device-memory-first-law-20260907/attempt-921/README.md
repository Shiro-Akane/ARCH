# Capacity attempt 921: process-name diagnosis

Attempt 921 remains an incomplete capacity campaign. The regrid and sparse
provider workloads passed their allocation gates. The audit31 workload then
completed all three ODE methods, both storage configurations and 64 subdivisions
of the same total physical interval 1e-10, with explicit device allocations
fully released. The recipe rejected
its profiled process name before the final AMR application workload started.
No observation is being replaced or retrospectively certified by this repair.

The [archived recipe](replay.py) is byte-identical to the recipe used by 921;
`cmp` verified the copy before editing the active recipe. Its SHA-256 is
`abe8c5603d12c1bdabb71f9b6f9f23354b0a48e1e9c9c237f88bf69324f543e1`.
This is an evidence copy, not an executable entry point: its relative `ROOT`
calculation belongs to its original directory. Use the [current recipe](../replay.py).

## What the retained records show

[diagnosis.json](diagnosis.json) contains exact artifact hashes, the selected
CUDA process's allocator summary and the limited identity fields inspected.
The raw SQLite export stays local in the ignored build tree. No environment
values or unrelated process commands were exported for this diagnosis.

The only CUDA allocation process has `globalPid=285365143339008`,
`pid=231872`, and `PROCESSES.name="arch_cuda_gener"`. Its main thread has the
same short name. There is no selected-process output-stream record or
process/module/exec table linking this PID to a full executable path. Capture
metadata records only the outer Python command, with launch PID fields set
to zero. The ordinary sparse evidence records the exact executable and hash,
but has no PID field joining it to the SQLite process.

The installed Nsight 2023.3.3 documentation describes `PROCESSES.name` as a
process name, not as a full executable path:
`/opt/nvidia/nsight-systems/2023.3.3/documentation/UserGuide/index.html:11588`.
The [Nsight analysis guide](https://docs.nvidia.com/nsight-systems/AnalysisGuide/index.html)
also distinguishes global process identifiers from Linux PIDs. The shared
reader's exact SQL join supplies this mapping; the result recipe does not
decode profiler-specific identifier bits. Linux
[`/proc/pid/comm`](https://man7.org/linux/man-pages/man5/proc_pid_comm.5.html)
has a 15-visible-character limit and can be changed by threads. Neither a
short-name prefix nor a matching basename proves executable identity.

## Results-only repair

The current recipe retains the shared reader unchanged, including its original
`executable` field. A separate `ProcessImageObserver` reuses the shared memory
guard's `OwnedDescendants` and `process_identity` helpers around the existing
subprocess runner. It observes only this invocation's pinned descendants,
reading `/proc/pid/exe` and executable stat identities without reading argv or
environment. PID/start-time checks bracket each observation. The shared guard
also supplies bounded descendant cleanup on completion or timeout.

The observer's default polling interval is 0.05 seconds and can be recorded
explicitly with `--identity-poll-seconds`. This is an instrumentation setting,
not an ARCH physics, memory-pool or hardware-specific parameter. Missing a
short-lived CUDA process fails the join; it never creates inferred coverage.
Multiple observed executable images or reused process identities also fail.

After profiling, only PIDs that actually supplied CUDA allocation events are
retained. Each must match exactly one observed executable path and inode/stat
identity from the frozen artifact inventory. The associated SHA-256 is the
already measured hash of that same object, and the existing end-of-campaign
provenance check verifies the inventory again. Large executables are not
rehashed on every polling tick. The permitted artifact is workload-specific:

| Workload | Allowed artifact |
|---|---|
| `regrid_transaction` | `regrid` |
| `sparse_capacity` | `provider` |
| `audit31_trajectory` | `sparse` |
| `amr_application` | `arch` |

The new record field is `process_identity_evidence`. It includes the selected
raw observations, exact resolved artifact identities, original Nsight names,
sampling interval and completion status. Unselected descendants are removed
from this evidence. The index can re-run the same classification without
reading `/proc` or trusting the short name:

```python
key = replay.WORKLOAD_ARTIFACTS[record["name"]]
verified = replay.check_process_identities(
    record["allocation_summary"], record["process_identity_evidence"],
    {key: identity["artifacts"][key]},
    {key: identity["artifact_observation"][key]})
assert verified == record["process_identity_evidence"]
```

## Verification before a new campaign

```bash
python3 -B validation/backend/results/device-memory-first-law-20260907/test_process_identity.py -v
python3 -B validation/backend/results/device-memory-first-law-20260907/test_allocation_lifetimes.py -v
```

The process suite covers exact matching, same-prefix and same-basename wrong
binaries, each stat-field change, deleted files, PID reuse, multiple images,
ambiguous global IDs or artifacts, missing/incomplete observations, invalid
sampling controls, unchanged input data and deterministic index replay. One
bounded test observes an owned CPU-only `sleep` process through the real shared
runner; it does not launch CUDA, Nsight or a build. The existing lifetime
controls remain unchanged. An AST comparison confirms that workload commands,
physical inputs, Nsight invocation, artifacts and interpreters are unchanged.

The next complete capacity campaign must collect fresh live identity evidence.
No such capture exists for 921, so this repair cannot turn 921 into a pass.
