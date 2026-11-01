# Cross-backend validation

[cases.json](cases.json) defines the uniform-grid CPU/CUDA application matrix:
problems, input files, sampled checkpoints, expected solver choices and
scientific comparison budgets. The [validation index](../README.md) links the
reference methods and results for each physics module.

## Run an application matrix

Build ARCH and its checkpoint validator with the test targets enabled, then
run the shared validator from the source root:

```sh
python tools/validate_backend_results.py \
  --manifest validation/backend/cases.json \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda \
  --output-root /tmp/arch-backend-validation
```

Use the corresponding `.exe` paths and a native output directory on Windows.
The Python checks require NumPy and h5py. The build must support both CPU and
CUDA execution for a paired matrix; optional network/EOS routes require their
matching data and libraries.

Without `--case`, the runner selects the whole manifest. Repeating
`--case CASE_ID` selects cases, but every selected `qualification_group` must
be complete. This keeps convergence decisions tied to the full resolution
sequence. The runner lists missing cases and rejects an incomplete group before
creating outputs. Ungrouped case results contain only their per-case checks.

Additional matrices are maintained with their modules:

- [AMR](../amr/README.md): mesh changes, conservation and diffusion coupling.
- [Network](../network/README.md): generated packages, weak rates and sparse solves.
- [Restart](../restart/README.md): restoration and continuation across backends.

## Coupled physics and restart

[run_microphysics_validation.py](run_microphysics_validation.py) selects the
microphysics checks. [verify_runtime_matrix.py](verify_runtime_matrix.py)
invokes the shared application, AMR, restart and instrumentation validators.
[verify_microphysics_coupling.py](verify_microphysics_coupling.py) combines
burning, physical transport, hydrodynamics and ENUC-driven AMR. Its options
`--restart --active-enuc-factor VALUE --terminal-time TIME` require an active
ENUC timestep limit and actual runtime regridding. The endpoint must follow
the three-step source run.

CPU/CUDA trajectories are compared at the prescribed physical time with the
stated field budgets. Same-backend restart also checks controller state and
output history. The console's `dt_burn` is the executed Strang half-step;
the ENUC timestep check therefore reads saved controller state.

Nuclear-energy checks use the selected network's energy weights and conversion.
For aprox13, the mass convention is tracked alongside mass/charge drift and its
difference from binding energy. The [endpoint energy results](results/release-closure-20261009/endpoint-nuclear-energy-summary.json)
give the diagnostic definitions and error budgets. Independent NSE calculations
load their reference solvers separately.

For cancellation-sensitive nuclear-mass balances, use
`read_conservation_metrics(..., preserve_total_precision=True)`. Native
long-double totals are encoded as round-trip decimal values with an explicit
encoding marker. The reader preserves numeric tokens for these totals while
keeping ordinary metadata types. Narrowed totals cannot support this mode.
The [precision comparison](results/release-readiness/REL3GeometryGravitySeparation-20261014.json)
checks saved outputs against the `1e-12` energy law, six enabled-transport
endpoints and four active-ENUC continuations.

The [CPU/CUDA integration record](results/release-readiness/REL4CudaDeviceAndIntegration-20261019.json)
contains the corresponding source and executable identities. Platform and
library configurations are listed in [platform support](../../docs/releases/PlatformSupport.md).

## Plotfile inspection

[plotfile_contract.py](plotfile_contract.py) checks a mapped JSON header,
FP64 field/block shapes and one native Cartesian 1D/2D cell by hyperslab.
Its `candidate` result describes this local consistency check. Complete AMR
coverage, provenance and physical accuracy have separate checks in the
[Plotfile query contract](../../studio/docs/contracts/PLOTFILE_AUDIT_API.md).
[Examples and readback results](results/plotfile-contract/README.md) include
small counterexamples and source-identified application outputs.

## Measure runtime costs

Use complete runs of the same physical input, endpoint, AMR settings and output
schedule. The [GPU measurements](results/hpc-cuda-optimization/README.md) and
[ARCH/FLASH comparison](../gravity/flash/README.md) show the hardware, source
identities, sampling methods and costs for their workloads. The current
Helmholtz stellar closure supplies thermal conduction; older full-transport
measurements use the transport model recorded with those results.

[probe_cuda_calls.cpp](probe_cuda_calls.cpp) is an optional preload observer.
By default it reports host CUDA API latency, including any wait for earlier
work. This quantity differs from transfer or kernel time. Setting
`ARCH_CUDA_OBSERVER_EVENTS=1` adds CUDA-event timing and a completion wait for
each launch, changing overlap and launch pacing. Use that mode to locate costs;
measure speedups in ordinary runs without the observer. On runtimes without
`cudaFuncGetName`, resolve executable-relative stub addresses against the
symbol table of the measured binary.

Store compact metrics, timing tables and diagnostic extracts with their source
and input identities. Large HDF5 files, checkpoints and complete traces are
indexed according to [data availability](../archive/README.md).
