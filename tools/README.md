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
python3 tools/check_documentation.py .
python3 -B tests/tooling
```

[audit_architecture.py](audit_architecture.py) checks shared implementation and
include boundaries; the Python suite tests tool behavior with controlled inputs.
These entries do not run a simulation or establish physical accuracy. Full
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

[check_documentation.py](check_documentation.py) checks current Markdown destinations
and heading anchors; historical/frozen evidence remains on its recorded basis.
The complete Tooling entry rejects empty, failed or skipped discovery.

## Check CI test reports

[check_ci_results.py](check_ci_results.py) verifies the CPU CI profile's CTest
inventory and, when provided, its JUnit report. It rejects missing coverage
anchors, duplicate or omitted results, failures and skips. It reads CTest's
pass/fail decisions rather than recalculating numerical tolerances. The
[workflow guide](../.github/workflows/README.md) describes its use; this report
check does not generate scientific Validation evidence. The same owner reports
conservative `native`/`studio` applicability using `--scope`, `--base` and `--head`;
`NOT_APPLICABLE` never represents an executed test pass.

## Run the application and compare results

Use a testing-enabled build containing ARCH and
`arch_cuda_single_level_validation`. CUDA tests require a usable GPU; sparse
CUDA burning additionally needs cuDSS and compiled support for the selected
network/EOS combination. A generated package is needed only when the case uses
a custom network; built-in networks can also use the sparse provider.
See [test setup](../tests/README.md#cuda-and-optional-providers).

- [smoke_cuda_amr_runtime.py](smoke_cuda_amr_runtime.py): short complete-program
  runs; start with the [smoke guide](../tests/smoke/README.md).
- [validate_backend_results.py](validate_backend_results.py): execute declared
  CPU/CUDA cases and compare actual fields, topology and conservation.
- [validate_cuda_amr_restart.py](validate_cuda_amr_restart.py): strict restore
  and continuation using the shared ARCH checkpoint contract. An explicit
  `--terminal-time` compares adaptive cross-backend trajectories at a common
  physical time; same-backend restart still requires strict reproducibility.
- [qualify_cuda_amr_evidence.py](qualify_cuda_amr_evidence.py): review recorded
  matrices against their manifests, source/build identities and final artifacts.

For a fixed level-zero grid, ARCH skips empty regrid transactions. The backend
runner identifies this from the actual run parameters and marks the regrid
measurement as not applicable; it does not invent a timing or a zero counter.
Dynamic AMR still requires valid transaction measurements. Field, conservation
and scientific-reference checks apply in both cases.

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

Most tools use the Python standard library; the main backend comparison delegates
HDF5 field comparison to the C++ comparator. Coupled microphysics checks also
use NumPy and h5py. Independent references may need SciPy,
mpmath or h5py as documented by their Validation module. Compute Sanitizer and
Nsight are needed only for their respective instrumentation workflows.

pynucastro is needed for [network generation](network/README.md).
`GenerateNetwork.py --check` executes the supplied recipe and constructs the
network in memory without writing a package. Review recipes before running
them, retain their upstream data provenance, and rerun CMake configuration after
generating a selected package.
