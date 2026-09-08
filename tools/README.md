# Developer and validation tools

[中文](README.zh-CN.md) · [Verify your checkout](../tests/README.md)

These tools are included with the source. The top-level Python entry points
share a small set of execution, comparison and identity helpers; they remain
together because they import those same helpers. Network generation is grouped
under [network/](network/README.md). Scientific reference methods and inputs
belong to [Validation](../validation/README.md), not a second runner here.

## Start without a GPU

Use Python 3.10 or newer and the Git/CMake tools in the build setup. From the
repository root:

```bash
python3 tools/audit_architecture.py .
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

[audit_architecture.py](audit_architecture.py) checks shared implementation and
include boundaries; the Python suite tests tool behavior with controlled inputs.
Neither command runs a simulation or establishes physical accuracy. Full
resource-guard coverage requires Linux `/proc`, pidfd and child-subreaper
support, including Python's `os.pidfd_open` interface.

For a manifest/protocol check without compiled solvers:

```bash
python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json --unit-test
python3 tools/validate_cuda_amr_restart.py --unit-test
```

These modes check the checking protocol only. They do not produce a CPU/CUDA
trajectory or replace field comparisons.

## Check CI test reports

[check_ci_results.py](check_ci_results.py) verifies the CPU CI profile's CTest
inventory and, when provided, its JUnit report. It rejects missing coverage
anchors, duplicate or omitted results, failures and skips. It reads CTest's
pass/fail decisions rather than recalculating numerical tolerances. The
[workflow guide](../.github/workflows/README.md) describes its use; this report
check does not generate scientific Validation evidence.

## Run the application and compare results

Use a testing-enabled build containing ARCH and
`arch_cuda_single_level_validation`. CUDA tests require a usable GPU; sparse
CUDA burning additionally needs cuDSS and compiled support for the selected
network/EOS combination. A generated package is needed only when the case uses
a custom network; built-in networks can also use the sparse provider.
See [test setup](../tests/README.md#add-cuda-and-its-sparse-provider).

- [smoke_cuda_amr_runtime.py](smoke_cuda_amr_runtime.py): short complete-program
  runs; start with the [smoke guide](../tests/smoke/README.md).
- [validate_backend_results.py](validate_backend_results.py): execute declared
  CPU/CUDA cases and compare actual fields, topology and conservation.
- [validate_cuda_amr_restart.py](validate_cuda_amr_restart.py): strict restore
  and continuation using the shared ARCH checkpoint contract.
- [qualify_cuda_amr_evidence.py](qualify_cuda_amr_evidence.py): review recorded
  matrices against their manifests, source/build identities and final artifacts.

ARCH and the comparator must come from the same build. Choose an absent or empty
output directory under a local build area or temporary directory; runners retain
logs and do not erase an earlier run. Writing or “publishing” evidence here means
writing local JSON, not uploading it. The effective Validation record is updated
by the maintainer only after reviewing the actual result.

## Build and memory measurements

[run_memory_guarded.py](run_memory_guarded.py) supervises its owned command tree.
The [build guide](../docs/guides/Build.md#parallel-compilation-and-memory)
explains memory/swap limits and optional Linux PSI pressure checks. GPU telemetry
is collected only when requested; it is not a device-memory reservation.

[summarize_cuda_compile_memory.py](summarize_cuda_compile_memory.py) reads recorded
compiler measurements. [validation_device_memory.py](validation_device_memory.py)
reads process-local allocation events from profiler data. These readers do not
compile code, execute a solver or infer performance from configured limits.

## Shared helpers and optional dependencies

[validation_provenance.py](validation_provenance.py) owns source/build/input
identities and replacement checks. The backend runner's `run_arch_with_logs`
owns process logs. [validation_sanitizer.py](validation_sanitizer.py) owns
Compute Sanitizer invocation and report checks. Extend these owners for reusable
behavior instead of creating another process supervisor or evidence schema.

The top-level tools use the Python standard library; HDF5 checkpoint reading is
delegated to the C++ comparator. Independent references may need NumPy, SciPy,
mpmath or h5py as documented by their Validation module. Compute Sanitizer and
Nsight are needed only for their respective instrumentation workflows.

pynucastro is needed for [network generation](network/README.md).
`GenerateNetwork.py --check` executes the supplied recipe and constructs the
network in memory without writing a package. Review recipes before running
them, retain their upstream data provenance, and rerun CMake configuration after
generating a selected package.
