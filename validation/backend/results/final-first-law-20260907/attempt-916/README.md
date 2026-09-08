# Memcheck attempt 916: interrupted GPU memory monitoring

Attempt 916 remains failed and incomplete. The requested GPU memory query
returned nonzero after three completed routes, and the memory guard ended the
campaign with `stop_reason=monitor_error`. This is a monitoring failure, not a
scientific or sanitizer verdict for the interrupted workload.

The [original guard log](../../../../../build/memcheck-final-916.log) records
10.027 seconds of execution, minimum available RAM of 6,747,692 KiB, peak owned
RSS of 500,476 KiB, and unchanged swap at 242,944 KiB. GPU telemetry has ten
samples and `complete=False`; system-pressure telemetry is also incomplete.
The recorded `guard_stopped=False` does not make this a successful run: the
monitor itself failed, rather than reaching a configured pressure threshold.

## Retained route evidence

| Route | Retained result |
|---|---|
| [Regrid transaction](../memcheck-916/cuda_regrid_transaction/sanitizer.log) | Completed; 0 errors, 0 leaked bytes in 0 allocations |
| [Regrid migration](../memcheck-916/cuda_regrid_migration/sanitizer.log) | Completed; 0 errors, 0 leaked bytes in 0 allocations |
| [AMR composition](../memcheck-916/cuda_amr_composition/sanitizer.log) | Completed; 0 errors, 0 leaked bytes in 0 allocations |
| [AMR exchange](../memcheck-916/cuda_amr_exchange/sanitizer.log) | Header only; no completion summary or final verdict |

The other 19 routes were not started, and no full `memcheck-916/evidence.json`
was produced. The dependent racecheck-903 command was not started because it
followed memcheck through a shell `&&` chain. Clean results from three routes
do not substitute for completing the original 23-route inventory.

## Diagnosis boundary

The log's error comes from the nonzero-return branch in
`GpuMemoryObservation.sample` in the unchanged
[shared guard](../../../../../tools/run_memory_guarded.py). Its message records
only stripped stderr, which contained no text. The numeric query return code
and stdout were not retained. These records therefore do not identify the
precise driver, utility or platform cause; a later successful query cannot
recover that missing information. GPU telemetry remains required for the
replacement run.

[diagnosis.json](diagnosis.json) records the guard metrics, source branch used
for this interpretation, original file hashes, clean and incomplete routes,
and the planned successor. It does not turn incomplete observations into a
pass or infer unobserved sanitizer results.

## Preserved recipe and successor

The [archived recipe](run_sanitizers.py) is byte-identical to the recipe used
by 916, confirmed with `cmp` and SHA-256:
`a4c505dcf3e1507d5bb7888a90f1e7663d78040f11719993f228dc9fa2e32be8`.
Its relative `ROOT` expression belongs to the original directory, so this
copy is evidence, not an executable entry point. Use the
[current entry point](../run_sanitizers.py) for a new campaign.

The assigned successor is memcheck-929 with the same frozen recipe, all 23
original physical inputs and the same sanitizer and memory-guard settings.
It must complete in a fresh directory with GPU telemetry enabled. The
separate racecheck-903 campaign follows only after successful memcheck.
No production code, maintained guard, physical input or error budget was
changed as part of this archive; 916 retains its original failed status.
