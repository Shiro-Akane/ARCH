# Shared developer and validation tools

Reusable execution, comparison and evidence helpers live here. Problem inputs,
scientific reference methods and run-specific recipes stay in
[Validation](../validation/README.md); network generation has its own
[module index](network/README.md).

## Entry points

- [run_memory_guarded.py](run_memory_guarded.py): run a command while observing
  its process tree and system memory pressure.
- [summarize_cuda_compile_memory.py](summarize_cuda_compile_memory.py): summarize
  per-command measurements or historical compiler samples. See the measured
  [core-build reference](../validation/backend/results/cold-core-first-law-20260907/release-909/README.md).
- [audit_combination_v2.py](audit_combination_v2.py): check shared-implementation,
  backend and include-boundary rules against the source tree.
- [smoke_cuda_amr_runtime.py](smoke_cuda_amr_runtime.py): short application runs
  driven by the [smoke manifest](../tests/smoke/README.md).
- [validate_backend_results.py](validate_backend_results.py): execute canonical
  CPU/CUDA cases and compare their outputs.
- [validate_cuda_amr_restart.py](validate_cuda_amr_restart.py): checkpoint restore
  and continuation checks.
- [qualify_cuda_amr_evidence.py](qualify_cuda_amr_evidence.py): qualify recorded
  matrices against declared final artifacts and required coverage.

## Common helpers

[validation_provenance.py](validation_provenance.py) owns source/build/input
identities, replacement checks and evidence publication. Process execution and
log capture use `run_arch_with_logs` in the shared backend runner.
[validation_sanitizer.py](validation_sanitizer.py) owns CUDA instrumentation and
report checks.
[validation_device_memory.py](validation_device_memory.py) summarizes process-local
allocation events; allocation observations are distinct from total physical VRAM.

Use each command's `--help` and the owning Validation module's reproduction
instructions. Extend these shared helpers for reusable behavior instead of
creating another runner or evidence format inside a result directory.
