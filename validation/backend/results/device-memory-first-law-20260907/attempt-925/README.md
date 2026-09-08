# Capacity attempt 925: explicit profiler-launcher handoff

Attempt 925 remains failed and incomplete. Its regrid workload passed the
physical and allocator checks, but the then-current identity policy rejected
two executable images for the selected CUDA PID. Unlike 922, this attempt
retained the observations needed to identify the cause.

The [archived recipe](replay.py) is byte-identical to the recipe used by 925,
verified with `cmp` before editing. Its SHA-256 is
`26b3af0b5e52dd6a13f4e1fee7c4b49d15be8266bd4d194dc01ca8fc4dac5a58`.
This archive is not an executable entry point because its relative `ROOT`
expression belongs to its original directory. Use the [current recipe](../replay.py).

## Observed cause

The selected CUDA PID 257377 has one process start time, 1554865 ticks. The
retained [identity audit](../release-925/regrid_transaction/process-identity-audit.json)
contains two images, with no overlap in their sampled time ranges:

| Image | Samples | First monotonic ns | Last monotonic ns |
|---|---:|---:|---:|
| Installed Nsight launcher | 4 | 15548698140597 | 15548857719998 |
| Frozen regrid workload | 17 | 15548910687307 | 15549769817344 |

The first path is
`/opt/nvidia/nsight-systems/2023.3.3/target-linux-x64/nsys-launcher`; the
second is the exact frozen `arch_cuda_regrid_transaction` artifact. The
observed launcher inode/stat tuple matches the installed object. This is
evidence of the profiler's launcher-to-workload exec handoff, not missing
sampling and not permission to accept arbitrary multiple images.

[diagnosis.json](diagnosis.json) records the original audit and artifact
hashes plus the posthoc launcher identity. That launcher was not explicitly
pinned before attempt 925, so the posthoc classification does not retroactively
turn 925 into a qualified capacity campaign.

## Declared instrumentation support for 926

The recipe accepts an optional, explicit instrumentation setting:

```text
--profiler-launcher /opt/nvidia/nsight-systems/2023.3.3/target-linux-x64/nsys-launcher
```

There is no directory search, name-prefix whitelist or automatic second-image
exception. Omitting the option retains the single-workload-image policy.
When configured, the profiler and launcher are pinned by full resolved path,
SHA-256 and inode/stat identities before collection and compared with new
observations after the campaign. These tool identities remain separate from
the ARCH workload inventory.

A selected CUDA PID must have exactly one frozen workload image. At most one
other image is permitted, and it must be the declared launcher object, use
the same PID/start time, and finish all its observed samples strictly before
the first workload sample. Unknown images, different workloads, changed
objects, overlapping samples, a return to the launcher, multiple start times
and additional images still fail. The raw observations and failure audits
remain intact. No ARCH call, physical input, CUDA kernel or numerical policy
changes as a result of this instrumentation configuration.

## Evidence interface and checks

The completed report records `instrumentation.before`,
`instrumentation.after` and `instrumentation.verified_unchanged`. Each tool
entry contains `artifact: {path, sha256}` and `artifact_observation`. A
launcher entry exists only when explicitly configured. The per-workload
identity audit also preserves the declared tool identities before checking.

The index replays the same policy with the recorded launcher:

```python
key = replay.WORKLOAD_ARTIFACTS[record["name"]]
frozen = report["identity"]
tools = report["instrumentation"]
assert tools["verified_unchanged"] is True
assert tools["before"] == tools["after"]
checked = replay.check_process_identities(
    record["allocation_summary"], record["process_identity_evidence"],
    {key: frozen["artifacts"][key]},
    {key: frozen["artifact_observation"][key]},
    launcher=tools["before"].get("launcher"))
assert checked == record["process_identity_evidence"]
```

The index should verify the recorded tool file identities with its normal
artifact checker; the short Nsight process name is not an input to matching.
Successful handoffs retain both raw images and mark `launch_mode` as
`pinned-launcher-exec`; a single workload image is marked `direct`.

All 39 bounded identity tests and the existing 19 lifetime tests passed. New
controls cover a valid handoff, omitted/wrong launchers, changed object
fields, different start times, overlap, return, third images, invalid tool
identities, tool mutation while recording, and deterministic index replay.
An AST comparison confirms that the observer, lifetime policy, diagnostic
selection, scientific task commands and Nsight invocation are unchanged.
Only the CPU-only observer selftest ran during preparation; no GPU, profiler
campaign or build was launched by this diagnostic work.
