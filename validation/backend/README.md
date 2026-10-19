# Cross-backend validation

[cases.json](cases.json) defines the canonical uniform-grid CPU/CUDA application
matrix: problems, owned inputs, execution checkpoints, expected policy choices
and scientific comparison budgets. The shared
[backend runner](../../tools/validate_backend_results.py) executes that matrix;
the owning physics modules describe its reference methods.

Selection is scoped, never partial. With no `--case` the runner validates the
whole matrix, and while explicit `--case` runs keep the per-case checks, every
touched `qualification_group` must be selected in full. A partial or
complete-plus-partial group is rejected before any output-root, provenance,
runtime-input-hash, execution or evidence side effect, listing the missing case
IDs. Complete selected groups still pass the unchanged frozen convergence gate,
deciding completeness from the original manifest rather than the requested
subset. Results scoped to ungrouped cases carry no group summaries and are
refused by the full-matrix qualifier.

Related coverage is kept with its module:

- [AMR](../amr/README.md): Cartesian and curvilinear meshes, runtime topology
  changes, conservation and diffusion coupling.
- [Network](../network/README.md): generated packages, weak rates and sparse solves.
- [Restart](../restart/README.md): native restore and continuation across backends.

## Configuration and application integration

For cancellation-sensitive nuclear-mass balances, call
`read_conservation_metrics(..., preserve_total_precision=True)`. The validator
prints native long-double totals as round-trip decimal numbers; the reader
preserves their integer/float tokens and requires the explicit encoding marker.
Metadata keeps its ordinary types. Old narrowed totals are rejected for this
scientific mode. The [current CPU receipt](results/release-readiness/REL3GeometryGravitySeparation-20261014.json)
records the serialization defect, saved-output requalification under the
unchanged `1e-12` law, all six enabled-transport endpoints and four active-ENUC
continuations. It does not qualify a new CUDA run.

The [Core/Studio configuration audit](results/studio-config-contract-audit-20261001/README.md)
records the inspected main and locally available Studio checkpoints, exact-input
identity checks, and the current client migration gap. It is CPU contract evidence,
not a simulation, GPU, or performance qualification. The
[delivery plan](../../docs/histroy/studio/StudioConfigurationHandoff.zh-CN.md)
defines the subsequent interface and platform acceptance work. Its
[Jeans/RZ and second-platform guide](../../docs/histroy/science/JeansRZPlatformHandoff.zh-CN.md)
scopes implementation and the approved long-duration subset. Process new raw
outputs locally before submitting evidence: upload concise metrics, timing tables,
figures and diagnostic extracts, not HDF5, plotfiles, checkpoints or full arrays.
Keep the originals on the producing machine with an identifiable local index.

The [Plotfile candidate probe](plotfile_contract.py) checks a mapped JSON header,
FP64 field/block shapes and one native Cartesian 1D/2D cell by hyperslab. Its
[joint validation guide](../../docs/development/PlotfileValidationContract.zh-CN.md)
defines the adjustable test adapter, low-dimensional measure normalization,
identity expectations and result codes. `candidate` means local consistency;
it does not qualify production publication, provenance, complete AMR coverage
or physical accuracy. Legacy files retain explicit missing semantics. See the
[processed record](results/plotfile-contract/README.md) for tiny counterexamples
and source-pinned real-output readback. The checks use existing NumPy/h5py and
tooling discovery, without new CI lanes or a production writer change.

## Optimization and coupled checks

The [release plan](../../ARCH_RELEASE_PLAN.md) owns current qualification work.
Its [CPU follow-up record](results/release-readiness/REL3CpuScienceFollowup-20261014.json)
links the original receipt and adds six burn–diffusion combinations, sixteen
built-in NSE applications, four short fresh-process continuation protocols,
and bounded Cellular/RZ diagnostics. Accepted numerical scopes, retained
failures and still-open long/device gates are recorded separately.

The [HPC-CUDA evidence index](results/hpc-cuda-optimization/README.md) summarizes
the completed optimization campaign and links its original source-pinned data.
Historical server cleanup scripts and experimental providers are archived there,
not part of the maintained solver or routine test suite.

[run_microphysics_validation.py](run_microphysics_validation.py) selects existing
scientific gates. [verify_runtime_matrix.py](verify_runtime_matrix.py) delegates
runtime, AMR, restart and instrumentation checks to the shared validators.
[verify_microphysics_coupling.py](verify_microphysics_coupling.py) combines burn,
physical transport, Hydro and ENUC-driven AMR; its optional
`--restart --active-enuc-factor VALUE --terminal-time TIME` requires an actually
binding ENUC timestep limit and runtime regridding. Adaptive CPU/CUDA runs meet
at the prescribed physical time with unchanged field budgets; same-backend
restart retains strict controller-state and output-history comparisons. The
endpoint must follow the three-step source run. The console's `dt_burn` is the
executed Strang half-step, so the ENUC witness reads checkpoint controller state.

Endpoint nuclear-energy checks use the selected network's frozen energy
weights and conversion. The aprox13 mass convention retains mass/charge drift
and records its difference from binding energy; immutable data access needs
only the existing tooling dependencies. Independent NSE root calculations
load their optional reference solvers separately. The
[processed endpoint record](results/release-closure-20261009/endpoint-nuclear-energy-summary.json)
identifies the diagnostic scope and original budgets.

[probe_cuda_calls.cpp](probe_cuda_calls.cpp) is an optional, test-only preload
observer. By default it reports host CUDA API latency, which may include waiting
for earlier work and must not be interpreted as PCIe or kernel time. Setting
`ARCH_CUDA_OBSERVER_EVENTS=1` also measures each launch with CUDA events and a
completion wait. This changes overlap and launch pacing: use it to attribute
cost, never as a formal speedup sample. Older runtimes without `cudaFuncGetName`
report executable-relative stub addresses; resolve them against the exact timed
binary's symbol table and retain that binary identity.

Install NumPy and h5py for these Python checks. Use a new output directory for
each run. Short integration checks do not repeat the formal scientific or
timing campaign.

`results/` preserves identified application, regression, sanitizer, build,
resource and final-review records, including earlier failed attempts. Start
from the [central Validation index](../README.md) for the combined status and
the selected evidence; directory names alone do not establish acceptance.
Maintenance checks retain their own source identity and do not relabel an
earlier scientific run.

The current [ARCH–FLASH work and coupling audit](../gravity/flash/O5OptimizationReport.zh-CN.md)
separates route registration, active material channels, coupled agreement and
independent accuracy. Its four-module runs do not certify the full policy
Cartesian product. Historical Helmholtz "full transport" timing rows retain
their source scope; the current stellar closure supplies thermal conduction
only. Tabular ODE/NSE candidate recovery now has paired host/device error-contract
checks, with strict required failures retained; see the
[O6 acceptance record](../gravity/flash/O6AcceptanceReport.zh-CN.md). It does not
qualify missing material or weak-process closures.
