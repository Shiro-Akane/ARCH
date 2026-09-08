# ARCH Verification and Validation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

This directory is the single entry point for quantitative verification records
covering hydrodynamics, diffusion, external gravity, burning, AMR, EOS, restart
and generated networks on CPU and CUDA. Each record identifies its tested
sources, binaries and data.

Start with the module for the physics you plan to use. Its summary explains the
test problem, the reference answer and the allowed error before linking the full
records. A convergence check asks whether error decreases as the mesh or timestep
is refined; a conservation check accounts for physical sources and boundary
transport. CPU/CUDA agreement checks backend consistency, while the independent
references check the numerical answer itself. Neither comparison substitutes
for the other.

The CPU/CUDA release profile has passed its numerical, application, regression,
device-safety, sustained-run and resource checks. The results below preserve
the tested source, binary and scientific-data identities. The
[source delivery review](backend/results/final-acceptance-20260907/release-73a9cf50/)
collects the final evidence inventory and source-asset review.

The [source organization and build review](backend/results/maintenance-freeze-20260908/README.md)
records runtime directory organization, documentation and source equivalence,
with separate build checks. The scientific results below retain their original
tested identities.

The current dispatch cleanup preserves the numerical implementations and
registered routes. Its optimized incremental CUDA build and all 289 Python
controls pass, together with all 98 configured Release tests, ten application
smoke lanes and all four strict normal/instrumented restart suites. The
[active plan](../docs/development/CudaReleaseStandard.md#active-execution-contract)
records the completed technical checks for the owner-authorized integration.
The [maintenance record](backend/results/maintenance-freeze-20260908/README.md)
keeps these current checks separate from the scientific measurements below.

The [matched local AMR timing](backend/results/maintenance-freeze-20260908/README.md#matched-local-amr-timing)
passes field, conservation and dynamic-topology checks at both measured Sedov
sizes. CUDA takes 3.30 and 3.43 times the eight-thread CPU end-to-end time on
this machine. Functional agreement does not imply a speedup; compare the
backends on your workload when choosing where to run it.

Timmes author contact and redistribution confirmation remain pending, as recorded
in the [third-party notices](../THIRD_PARTY_NOTICES.md). This administrative
release item is separate from the completed technical tests.

## Directory contract

Each module owns one subdirectory containing a curated README, machine-readable metrics,
immutable baseline parameter files under `inputs/`, and optional `figures/`. Each
validation `.par` file names its owning record and must change together with the
relevant metrics and acceptance decision. Reusable teaching and example inputs
remain under `simulation/`; runtime data remain under `EOS_toolkit/`. Do not create
a parallel validation tree. Raw logs, hardware metadata and historical records
belong under each module's `results/`, separate from the user-facing summary.
Generated HDF5 checkpoints and plot outputs remain in the local result directory
and are excluded from new Git additions. Keep the JSON/CSV results, parameters,
logs and artifact hashes in Git; distribute a full binary-data archive separately
when needed. Existing tracked reference data and EOS inputs are unaffected.

<a id="completed-results-on-the-release-candidate"></a>

## Accepted CPU/CUDA results

| Area | Passing CPU/CUDA checks | Record |
| --- | --- | --- |
| Smooth hydro | PCM/MUSCL/PPM spatial convergence at 64/128/256 cells; independent Euler/RK2/RK3 time accuracy; 1,016-step periodic advection | [hydro](hydro/README.md) |
| Riemann and strong shocks | Sod and planar Sedov analytic profiles, three-resolution convergence and shock positions | [hydro](hydro/README.md#sod-shock-tube) |
| Species diffusion | RKL1/RKL2 applications at three resolutions; second-order RKL2 spatial convergence | [diffusion](diffusion/README.md) |
| External gravity | RK2/RK3 analytic source balance, including AMR and RKL2 species-diffusion coupling | [gravity](gravity/README.md) |
| Built-in burning | Six aprox13 application runs, twelve Host network/ODE temporal controls and separate CUDA policy checks | [burn](burn/README.md) |
| Dynamic AMR | Ten Cartesian and 24 curved cases; conservative transfer and thermal/viscous/species coupling, plus complete runtime refine/coarsen cycles in Cartesian 3-D and cylindrical/spherical 2-D | [AMR](amr/README.md), [3-D lifecycle](amr/results/dynamic-3d-final-20260907/release-919/evidence.json), [curved 2-D lifecycles](amr/results/dynamic-curved-final-20260907/release-923/evidence.json) |
| Three-dimensional AMR instrumentation | Memcheck and racecheck each pass seven CUDA executions covering complete eight-child refine/coarsen/refine transitions; clean reports with original field and conservation checks | [memcheck](amr/results/dynamic-3d-final-20260907/memcheck-914/evidence.json), [racecheck](amr/results/dynamic-3d-final-20260907/racecheck-915/evidence.json) |
| Coupled curved AMR instrumentation | Memcheck and racecheck each pass two spherical three-dimensional thermal/viscous/species CUDA runs on mixed-level meshes; clean reports with original five-stage RKL2, field and physical-volume conservation checks | [memcheck](amr/results/curved-native-20260907/memcheck-904/backend-validation-evidence.json), [racecheck](amr/results/curved-native-20260907/racecheck-906/backend-validation-evidence.json) |
| EOS | Twelve normalized-table application cases and 24 physical endpoints, plus independent Helmholtz and manufactured thermodynamic controls | [EOS](eos/README.md) |
| HDF5/restart | Four backend directions from intermediate and terminal checkpoints; native composition, controller and output continuity | [restart](restart/README.md) |
| Burn/AMR restart instrumentation | Memcheck and racecheck each pass six actual CUDA runs and all nine strict comparisons; complete clean reports with native fields, controller and output-phase checks | [restart safety records](restart/README.md#completed-checks-on-the-release-candidate) |
| Generated networks and sparse burn | Six actual generated/weak application routes, three-ODE KLU/cuDSS trajectories and independent weak references | [network](network/README.md) |
| Built-in NSE | Sixteen application cases and 32 physical endpoints, with independent equilibrium and source-energy checks | [burn](burn/README.md) |
| Geometry | Twelve high-precision metric references; thermal/species/viscous spatial convergence and radial-origin stability | [AMR](amr/README.md#independent-geometry-and-diffusion-checks) |
| Sustained execution | 500 hydro and 100 diffusion AMR steps; twelve cycles per restart chain; 72 exact native burn-state replays and seven fixed-time burn comparisons | [AMR](amr/README.md#sustained-regridding-and-continuation), [restart](restart/README.md#sustained-native-restoration) |
| Core compilation | Optimized cold ARCH build, no-op, compact-route incremental build and identical-work serial/parallel comparison; two heavy jobs and four total jobs | [build measurements](backend/results/cold-core-first-law-20260907/release-909/README.md) |
| Bounded memory capacity | Regrid migration, a 16,384-row cuDSS matrix, generated burning and AMR applications; all observed dynamic allocations released | [capacity measurements](backend/results/device-memory-first-law-20260907/README.md) |
| Focused backend instrumentation | Memcheck and racecheck each pass 23 complete routes, including weak thermodynamics and real generated cuDSS burning; clean memory and race reports | [instrumentation records](backend/results/final-first-law-20260907/README.md) |

The [uniform application matrix](backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
contains 22 cases, 180 CPU/CUDA executions and 90 comparisons. The
[generated-network matrix](network/results/runtime-native-20260907/release-875/backend-validation-evidence.json)
adds six cases, 48 executions and 24 comparisons, including actual CPU KLU and
GPU cuDSS selection for the 32-equation audit31 network. Module summaries link
the independent scientific checks and preserve their original budgets.

The [complete Release regression](backend/results/final-first-law-20260907/release-regression-895/evidence.json)
passes 98 tests, and the [CPU-only regression](backend/results/final-first-law-20260907/cpu-regression-897/evidence.json)
passes 31. The [complete Debug regression](backend/results/final-first-law-20260907/debug-regression-910/evidence.json)
also passes all 98 tests without skips. Focused memcheck and racecheck each pass
all 23 routes. Their sparse tests cover three ODEs, both storage sizes and four
subdivisions with the original numerical budgets. Ordinary scientific runs and
memcheck retain the full `1e-10 s` interval; racecheck uses a separate `1e-12 s`
observation interval. The shorter instrumentation run does not replace the
full scientific trajectory. Host/device capacity and optimized core-build
measurements are linked above.

## Error conventions

Verification compares the implementation with analytic, manufactured, or
independently converged references. Validation against experimental or
published physical data will be labeled separately.

For cell-volume-weighted field error,

\[
L_1(q)=\frac{\sum_i V_i\lvert q_i-q_i^{ref}\rvert}{\sum_i V_i},
\qquad
L_2(q)=\sqrt{\frac{\sum_i V_i(q_i-q_i^{ref})^2}{\sum_i V_i}}.
\]

The observed rate between resolutions \(N\) and \(2N\) is
\(p=\log_2(E_N/E_{2N})\). Each record states any different norm or
normalization. Machine-readable results are retained as CSV or JSON; a committed data
processing script is optional when the formula, sampled outputs, commands, and
metrics are sufficient to reproduce the decision.

## Record requirements

Each case has one effective result set. Rerun in a temporary working directory,
then update the existing result, summary and dependent index together after
checking their identities. Do not accumulate V1/V2 copies or relabel an earlier
run as a new execution. Keep required reference data and failure controls under
their stated test ownership; superseded working outputs do not belong in the
published validation directory.

Each completed record states:

1. the property and modules tested, equations, dimension, and geometry;
2. immutable `.par` inputs and checksums for external EOS/reference assets;
3. configure, build, and run commands;
4. commit, compiler/flags, OpenMP count, backend, and relevant hardware;
5. reference provenance and sampling rules;
6. L1/L2 errors or the module-specific residuals and invariants;
7. an acceptance tolerance and explicit pass/fail result;
8. retained CSV/JSON metrics and, where useful, a static figure;
9. any safeguard activation or known implementation limitation.

AMR comparisons must place reference and numerical fields on a documented
common mesh and use physical cell volumes. CPU/CUDA comparisons use the same
case source, parameters, reference and metric definitions. Physical acceptance
compares solutions at the same prescribed time; exact checkpoint restoration
has its own state and metadata checks.

<a id="final-candidate-verification"></a>

## Recheck the acceptance records

The completed campaigns cover the following areas on one frozen source and
scientific-data set, with identified Debug/Release binaries, dependencies and
comparators. Their [delivery audit](backend/results/final-acceptance-20260907/release-73a9cf50/)
records the combined evidence and reviewed source assets. Each retained result
identifies the source actually tested; the current maintenance checks are
recorded separately from those scientific runs. A passing test report does not
itself publish the source or confirm third-party redistribution permission.

| Area | Reference or check | Required measurements |
| --- | --- | --- |
| Hydro/Riemann | analytic 1D Sod solution | density, velocity, pressure, energy L1/L2; feature positions; conservation |
| Strong shock | planar Sedov similarity solution | profile L1/L2, shock position, energy, symmetry |
| Hydro time integration | smooth semi-discrete reference | error versus time step for Euler, SSPRK2, SSPRK3 |
| Geometry and diffusion | analytic and manufactured solutions | physical-volume metrics, source balance and thermal/species/viscous convergence with RKL1/RKL2 |
| Dynamic AMR | refinement, coarsening and interface crossing | common-mesh L1/L2, topology, conservation and curved-coordinate face consistency |
| EOS | Ideal, Helmholtz and normalized Tabular3D/Tabular4D references | domains, derivatives, inversions, errors and coupled hydro/burn behavior |
| Burning and NSE | independent trajectories and equilibrium states | built-in/generated/weak composition and energy, tolerance refinement, equilibrium residuals and transitions |
| HDF5/restart | uninterrupted and resumed dynamic-AMR runs | native fields, topology, controller/output metadata and compatibility/error checks |
| CPU/CUDA policies | identical physics and accepted backend selections | field/state L1/L2, invariants, DenseLU/KLU/cuDSS selection and explicit rejection |
| Reliability and capacity | full execution paths and sustained workloads | memory/race checks, repeated regrid/restart, host RAM, swap and device allocation peaks |
| Build reproducibility | optimized cold and incremental core builds | identified sources and assets, compile time, safe concurrency and memory pressure |

## Follow-up workloads and table formats

ARCH's tabular EOS supports the normalized 3D/4D HDF5 layouts in the
[tabular data contract](../src/physics/eos/TabularEOS.md). Converting native
nuclear-matter tables, such as Shen, is a separate future extension: each table
family needs documented units, energy zero, component scope and valid domains.
The historical source-table assessment is linked from [EOS validation](eos/README.md).

Complete large-network trajectories and scaling measurements are scheduled for
machines sized for those workloads. They remain separate from the release
profile's representative generated and weak networks; retained results and
reproduction inputs are in [network validation](network/README.md).

Use [CASE_TEMPLATE.md](CASE_TEMPLATE.md) for new records.
