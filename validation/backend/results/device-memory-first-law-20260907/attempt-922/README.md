# Capacity attempt 922: unresolved observed-image cardinality

Attempt 922 remains failed. The first regrid workload completed its four- and
41-species transactions, and the allocator trace shows all explicit device
storage released. Identity classification then raised:

```text
ValueError: CUDA PID 254728 lacks a unique observed executable image
```

The original code used that message for both zero observations and multiple
images. It did not retain the observer's in-memory capture on failure.
Consequently, this record cannot establish which condition occurred. A missed
short-lived process, startup fork/exec images, or another observed image change
remain hypotheses, not diagnosed causes. The classifier has not been relaxed.

The [archived recipe](replay.py) was copied byte-for-byte with `apply_patch`
and checked using `cmp` before modification. Its SHA-256 is
`222973323737304fbf189d89f040a47aac84305344aa8a7e95935b425ae3b928`.
As with the earlier archives, this copy's relative `ROOT` expression belongs
to its original directory; use the [current recipe](../replay.py) for execution.

[diagnosis.json](diagnosis.json) preserves the original hashes and selected
CUDA summary. The original Nsight export names PID 254728 with the full,
correct regrid executable path, so this failure is not itself another short
name comparison. CUDA allocation events span 93,655,745 ns. This event interval
does not establish the process lifetime or recover missing `/proc` samples.

The memory guard completed without stopping the job: 7.668 seconds elapsed,
6,327,484 KiB minimum available RAM, and 190,056 to 190,576 KiB swap usage.
The mathematical PASS markers and balanced dynamic storage do not qualify
the uncompleted capacity campaign.

## Minimal diagnostic change for successor 925

Before lifetime or process-identity classification, the current recipe now
writes `process-identity-audit.json` into the workload lane. It contains:

- The exact invocation, recipe hash and selected raw allocation summary.
- Only actually selected CUDA PIDs' executable images, stat identities,
  start times, sample counts and explicit image counts, including zero counts.
- The workload-specific frozen expected artifact and local SQLite hash.
- The classification status and exact exception type/message on failure.

No environment, arguments from `/proc`, or unselected descendant observations
are written. The record explicitly sets `release_qualified=false`; a process
identity audit is not a complete capacity result. Successful campaigns also
hash this audit in their normal record. The strict observer, classifier,
workloads and scientific inputs remain unchanged.

The bounded checks passed: 23 process-identity tests and the existing 19
allocation-lifetime tests. New controls distinguish zero from multiple images
and verify that unselected processes and extra metadata are not exported.
Only the CPU-only observer selftest ran; this diagnostic preparation did not
launch CUDA, Nsight or a build.

Successor 925 must supply the missing live observations. If it fails again,
inspect its first lane's `process-identity-audit.json` before changing the
identity policy. A short-name prefix or blanket multiple-image exception is
not an acceptable repair.
