# Implementation ownership

Audience: contributors. This index describes maintained responsibilities,
design constraints and verification; user-facing references describe the API.

Companion to [CudaReleaseStandard.md](CudaReleaseStandard.md), established
2026-09-06. Paths are repository-relative. This is a navigation/ownership index,
not another implementation or a declaration that validation passed.

## Authority map

For general directory navigation, please start with the [source guide](../../src/README.md).
The [CUDA runtime index](../../src/cuda/runtime/README.md) categorizes host control, hydrodynamics, burning, AMR, and diffusion, placing thin built-in bindings under `burn/routes`. The public backend and storage entry paths remain unchanged. Additionally, the [maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md) documents path mappings and measured build checks. Please note that EOS declarations remain strictly separated from complete selected implementations; therefore, changes to common policy headers may trigger rebuilds across several routes without implying a leaked EOS dependency.

| Responsibility | Single mathematical/control authority | Allowed adapters / consumers |
|---|---|---|
| Policy names, factory routes, capabilities | `src/driver/dispatch/PolicyDescriptor.h`, `BackendCapabilities.h` | Resolve configuration names and backend support once; burn, diffusion and gravity factories consume resolved policy IDs. CPU dispatch TUs and generated CUDA wrappers bind implementations without a second string-selection path. |
| Compiled CUDA image compatibility | `cmake/CudaCodeImages.cmake` resolves the configured image set; `RuntimeProbe.cpp::cuda_image_compatible` checks ordinary SASS/PTX eligibility | Image metadata is private to the probe translation unit. Descriptors retain genuine feature requirements, not a local GPU-model floor. Existing startup resolver owns strict CUDA rejection / pre-construction Auto fallback. |
| AMR selected fields and indicator | `src/amr/RefinementIndicatorMath.h` | `AmrTree.h`, `cuda/amr/RefinementIndicators.cu` |
| AMR conservative transfer | `src/amr/RegridTransferMath.h` | CPU prolong/restrict, CUDA migration kernels |
| AMR coarse/fine ghost reconstruction and restriction | `src/amr/LimitedLinearProlongation.h`, `ConservativeRestriction.h` | `GhostExchange.h` and CUDA `CoarseFineExchangeKernels.cuh` bind samples to the same scalar/species helpers. Ghost reconstruction and full regrid-family migration retain their distinct admissibility and conservation contracts. |
| AMR flux registration and reflux | `src/amr/AmrFluxMath.h` | Host `FluxRegister.h` and CUDA `AmrFluxSurfaceKernels.cuh` use the same orientation, stage-weight and conserved-species corrections; route plans and storage traversal are separate. |
| Topology/Morton and transaction plans | `src/amr/Morton.h`, `AmrTree.h`, `RegridExecutionPlan.h` | Host control and atomic plan/publication lifecycle; CUDA adapters own D2D migration/storage, not a second tree or Morton implementation. |
| Physical volume/area/length | `src/grid/GridMetrics.h`, `GridGeometryView.h` | Driver CFL, GPU metric cache, hydro/diffusion/AMR |
| Hydro CFL and cell divergence | `src/driver/DriverUtils.h::{evaluate_cfl_cell_dt,finalize_cfl_dt}`, `src/numerics/integrator/TimeIntegratorHelper.h::accumulate_cell_divergence` | Host traversal and CUDA `HydroStateKernels.cuh` share the cell formulas; physical spacing/areas come from GridMetrics or its device cache. |
| Hydro/RKL stages and state publication | `src/driver/StageScheduler.h`, `src/numerics/diffusion/DiffFunction.cpp`, `DiffusionAMRStages.h` | Common scheduling owns hydro weights and slot/ghost/reflux ordering; DiffFunction owns RKL coefficients and DiffusionAMRStages the shared cell updates. Host and CUDA executors bind those descriptors and helpers. |
| Gaussian problem envelope | `simulation/GaussianPulse/Gaussian.cpp` | One physical Cartesian distance from `Grid::GetPhysicalCoords`; species and optional pressure/native-velocity amplitudes share the envelope before either backend runs. No coordinate conversion or backend branch in the problem. |
| Independent thin-shell/polar measures (tests only) | `validation/amr/geometry_reference.py`, data and shared test in `tests/math/CurvilinearMetricCases.h` | CPU metric and CUDA geometry tests; 70/90-digit defining integrals, not sampled production output |
| Geometric momentum source | `src/numerics/integrator/GeometricSources.h` | CPU time helpers, `cuda/hydro/HydroSourceKernels.cuh` |
| Roe-Glaister thermodynamic average | `src/numerics/flux/FluxFunctions.h::calc_glaister_state` | HLL/HLLC/Roe on both backends use the same symmetric fixed-composition pressure secants, derivative limits and enthalpy-consistent acoustic relation. Existing face species output supplies temporary averaged-composition storage; no new allocation or EOS implementation. |
| HLLC star-region flux | `src/numerics/flux/FluxHLLC.h::calc_star_flux` | One Rankine-Hugoniot state and contact pressure give mass/energy fluxes factored by contact speed. CPU/CUDA use the same expression; the prior subtractive star-state assembly is retired except for its unchanged guarded degenerate limit. |
| Velocity derivatives | `src/physics/diagnostics/VelocityDiagnostics.h` | CPU/device refinement and diffusion |
| External gravity | `src/physics/gravity/ExternalGravitySource.h` | CPU `ExternalGravity.h`, CUDA source kernel; same per-stage update |
| ODE algorithms and continuations | `src/numerics/burnsolver/ode_be-nr.h`, `ode_bd.h`, `ode_ros4.h`, `OdeContinuation.h` | CPU executor and CUDA `SparseOdeBatch.cuh` invoke the same begin/advance/linear-response continuations. `SparseBeNrBatch.cuh` retains aliases only, not another ODE implementation. |
| NSE fixed-point certification | `src/physics/nse/nse_solver.h::{input_is_equilibrium,log_mass_fraction}` | The existing solver reconstructs chemical potentials from populated species and checks every Saha log residual plus mass/charge at the unchanged tolerances. A certified input is preserved exactly; perturbed inputs use the existing safeguarded solver. No energy cutoff or backend branch. |
| ROS4 rejected-trial handling | `ode_ros4.h::finish_trial` | Factor and stage-solve failures share quarter-step retry, NSE reset, stall termination and no-commit behavior. Host/CUDA controls in `tests/math/BurnThermalCases.h` cover finite provider rejection, actual nonfinite DenseLU rejection and max-substep ordering. |
| Accepted-substep compensated state | `odeFunction.h::OdeMath::AcceptedState`, consuming `core/CompensatedSum.h` | BE_NR/BD/ROS4 share initialization, trial copies and projection-aware commit; rejected trials do not mutate accepted sums. O(NEQ) payload is included in typed workspace sizing |
| Self-heating Jacobian assembly | `src/numerics/burnsolver/odeFunction.h::assemble_burn_jacobian` | BE_NR/BD/ROS4; EOS cv gradient uses the view's analytic provider or samples that same view, including the denominator chain rule |
| Full burn RHS and thermal closure | `src/numerics/burnsolver/odeFunction.h::eval_burn_rhs` | All three ODE assemblies, BD midpoint and ROS4 stages; no copied cv division |
| Fixed-density burn first law and EOS derivative adapters | `src/numerics/burnsolver/BurnThermodynamics.h` | One composition-energy contraction and common numerical fallback; Ideal/Helm/Tabular views supply analytic derivatives. The common Jacobian contracts network columns and the EOS Hessian. No backend branch, extra ODE variable or changed DenseLU boundary. |
| Accepted burn energy | `odeFunction.h::{composition_increment_energy,integrated_burn_increment_energy,record_accepted_energy}` | One compensated network-energy contraction and optional signed weak quadrature consume raw accepted BE_NR/BD/ROS4 increments before rounded state addition/projection. Endpoint helpers delegate to that same contraction. BurnOdeReport carries the scalar result; CPU erasure, dense CUDA and sparse continuations preserve it. Rejected trials contribute nothing; NSE supplies its accepted projection energy. No extra ODE equation. |
| Burn state extent and matrix-size selection | Typed ODE `NEQ`, generated layout metadata, `BurnLimits::equation_count` / `uses_compact_matrix` | CPU erased handle exports the exact extent; CPU/CUDA Auto, dense workspaces and CSR thermal/source coupling count all equations, not only species |
| Single-cell burn preparation/handoff | `src/driver/DriverBurnPolicy.h` | CPU `DriverBurn.h` grid iteration and CUDA cell executors commit the accepted source integral to conserved energy/ENUC/limiter. The final EOS query still owns domain/error validation; subtracting independently reconstructed EOS energies is not a source integral. Invalid handoffs do not commit. |
| EOS view declarations and aliases | `src/physics/eos/eos.h` | CUDA launch declarations; actual owners include complete selected EOS headers |
| Diffusion argument/result records | `src/numerics/diffusion/DiffusionTypes.h` | `DiffFlux.h` owns the sole operators; CUDA config/launch declarations import only the shared records |
| Covariant viscous gradient and divergence | `DiffFlux.h::{ViscousBasisRotation,viscous_basis_rotation,evaluate_geometric_diffusion_cell}` | CPU `FluidState::get` and CUDA `DiffusionStateReader` only supply neighbouring state. Face momentum/work fluxes share the connection; scalar thermal/species math is unchanged. |
| Diffusion face thermodynamics and stability | `DiffFlux.h::{evaluate_diffusion_face_properties,evaluate_diffusion_dt_candidate,viscous_source_stability_rate}`, existing `GridMetrics::InverseRadiusVolumeAverage` | Fluxes and dt share actual face coefficients and rho/cv capacities. CPU/CUDA load current neighbours through their readers; the driver completes backend-local ghosts first. The source reuses the hydro volume measure. The origin-only `raw_geometric_diffusion_dt` was replaced, not retained as another route. |
| Independent viscous spatial acceptance (tests only) | `tests/math/ViscousGeometryCases.h`, `validation/amr/geometry_reference.py` | Cartesian-vector/radial analytic derivatives and one 30-profile input traversal; CPU/CUDA tests supply only their executor. Archive reuses shared provenance/logging and checks complete refinement coverage. |
| Independent Gaussian initialization / thermal activity (tests only) | `validation/amr/gaussian_reference.py` | Native-coordinate initial-state oracle and same-time/topology on/off thermal control. Uses the existing runtime parameter/lane/provenance helpers, not a second process or checkpoint-comparison infrastructure. |
| CUDA allocation/error boundary | `src/cuda/common/DeviceAllocation.h` | Runtime store and sparse pools share the existing RAII owner; no complete runtime-layout include |
| CUDA EOS failure transport | `src/cuda/common/DeviceEosStatus.h`, `src/cuda/hydro/CheckedHydroEos.cuh` | The backend binds a launch-owned sticky error latch and delegates queries to the original EOS unchanged; it supplies no interpolation, thermal recovery or pressure-floor formula. |
| Runtime metadata transfer accounting | `CudaBackendResources.cpp::enqueue_cuda_metadata_upload` | Same-level/coarse-fine exchange, boundary/flux construction, indicator selection and reflux rebinding. Counts the actual successful enqueue bytes; existing field-region and sparse-provider counters retain their own responsibilities. |
| Whole-regrid timing and transfers | `Driver.h::perform_regrid` wraps the existing `execute_regrid` transaction | One CPU/CUDA wall-time traversal and counter delta, including existing completion/retirement fences; no extra device synchronization. Separate `_regrid.tsv` records overlap nested backend trace operations and must not be added to those totals. |
| Dense/sparse factorization | `src/numerics/linalg/`, `cuda/microphysics/CuDssSparseSolver.cpp` | KLU CPU / cuDSS CUDA are intentionally separate; native nonsymmetric BTF/COLAMD, original-matrix residual rejection invalidates native analysis through the existing request message; opaque native errors fail closed |
| Sparse structure and value indexing | `src/numerics/linalg/CsrPattern.h`, `CsrMatrixView.h` | Host-only symbolic pattern construction and a backend-neutral duck-typed value view; generated structural writes and the shared burn Jacobian supply entries. The CUDA provider owns its numerical buffers and factorization handles. |
| Sparse equation/unknown equilibration | `src/numerics/linalg/LinearEquilibration.h` | One contiguous/permuted scale/division authority; `cuda/microphysics/SparseEquilibration.cu` only lowers traversal/launch, `CuDssSparseSolver.cpp` owns device workspaces and immutable column-slot metadata. Original CSR/RHS residual acceptance unchanged |
| EOS free-energy math | `src/physics/eos/TabularFreeEnergy.h`, EOS views | One cached Horner-basis tensor and constant-background-subtracted compensated contraction serve Host/CUDA. Tabular temperature inversion reuses a single energy/cv query per iteration. Host table owners and CUDA owners/error latch supply data/lifetime only; independent full-degree polynomial/constant-background controls do not call production bases for expected values. |
| Tabular thermal composition derivatives | `Tabular3DEOS.h`, `Tabular4DEOS.h` differentiate their existing interpolation patches; `eos_Utils.h::composition_derivative_query` | One species chain rule in linear composition coordinates, including the Hessian action. 4D maps Abar/Zbar analytically to sum(X/A), Ye; 3D supports selected Xi and Ye. Host/device share formulas and declared table-domain fallback. Independent manufactured controls live in the existing EOS test. |
| Helm table coordinates and conditioned Hermite contraction | `HelmEos` materializes the documented grid once; `BasicHelmEosView::{locate_axis,hermite_row,interpolate_ele_pos}` | Host/CUDA share identical grid values and the same constant-background-subtracted tensor polynomial. Existing Helm device owner only uploads/owns the two axes. `CompensatedSum` remains the sole accumulator. Independent monomial and actual-table tests remain separate mathematical oracles. |
| Network reaction math | built-in network headers or device-callable generated `.math.h` | CPU adapter + CUDA instantiation; no second generated math body |
| Generated package assembly and route binding | `tools/network/GenerateNetwork.py`, `PortableAdapter.py`, `PortableCxx.py` | The adapter moves numerical definitions into one `.math.h`; the emitted `.cpp` includes it. CMake network discovery and dense/sparse route templates bind registered types, not copied reaction or ODE formulas. Weak data/view specialization uses the separate owners below. |
| Generated complete-RHS temperature difference | `src/numerics/burnsolver/NetworkDerivative.h` | One generated `CompleteRhs` functor for Host/CUDA; built-in AD derivatives unchanged |
| Generated weak-table coordinate derivatives and corrections | `tools/network/WeakTables.py`, emitted into the same generated table/math headers | Existing `timmes::Dual` differentiates upstream bilinear coordinates; weak rho*Ye and signed loss gradients consume those results; Dense/sparse executors bind that same generated body |
| Immutable weak-table storage and borrowed views | `tools/network/WeakStorage.py`, `src/physics/network/WeakTableView.h` | Moves original data tokens into one Host store; `cuda/microphysics/device_network_owner.h` owns the upload, dense backend slot / sparse pool retain it across grid storage changes; no interpolation formulas in the adapter |
| Finite sum/product compensation | `src/core/CompensatedSum.h` | Timmes sums and generated nuclear-energy dot product; explicit FMA is intentional, implicit contraction remains disabled |
| Generated energy mass/conversion data | Emitted `generated/actual_network.H::network::mion` and `fundamental_constants.H::C::enuc_conv2` | One derived `energy_mion` accessor removes a conserved baryon mass offset for RHS/Jacobian/accepted energy; no independent MeV mass reconversion |
| Physical constants and units | `src/physics/constant/PhysicalConstants.h` | Disciplinary SI/CODATA 2022 constants, shared derived values and formula-local aliases; data boundaries in adjacent README |
| Heavy mathematical call annotation | `src/core/ArchPortability.h` (`ARCH_HEAVY_INLINE`) | Helm/Tabular leaves; removes duplicate Tabular3/4 local macros |
| Restart schema, native composition and scientific identity | `src/io/hdf5/HDF5Writer.{h,cpp}`, `src/io/chk/ChkIO.cpp`, `CheckpointCompatibility.cpp` | ARCH checkpoints require scientific identity, ENUC, controller state and native X for active species; X/rhoX consistency and physical compatibility are checked before restoring AMR state. No missing-field reconstruction or CUDA-specific checkpoint format. Common Driver/StateResidency controls materialization and output phase. |
| Validation provenance | `tools/validation_provenance.py` | Matrix, restart, smoke and release aggregators |
| Focused test artifact identity | `validation_provenance.py::capture_focused` and the shared artifact observation helper | Weak / real sparse network runners, configured source/package/library hashes and edit/replace guards; not an ARCH release certificate |
| Runtime checkpoint comparison | `tests/cuda/test_cuda_single_level_validation.cpp` | Same comparator for CPU/CUDA reports; explicit physical-time / step-diagnostic modes must preserve strict restart mode and never change checkpoint data. The manifest/qualifier must require fixed-time acceptance when step timing is diagnostic. |
| Fixed-time and step-based restart execution | `tools/validate_cuda_amr_restart.py::run_lane` | One parameter renderer, subprocess boundary, actual checkpoint/phase selection and source-identity check. Invalid/past targets fail before execution; saved terminal time must equal the prescribed time. Exact native IO/device replay and same-backend continuation are distinct from forward-step diagnostics. |
| Runtime CUDA sanitizer boundary | `tools/validation_sanitizer.py`, invoked by `validate_backend_results.py::run_arch_with_logs` | Matrix and restart validators instrument the actual CUDA application only; the ordinary CPU references, physics/checkpoint gates and provenance remain unchanged. One report parser requires unique process initialization and complete clean error/leak/race summaries. Archived historical recipes are immutable. |
| Sparse instrumentation observation (result policy only) | `validation/backend/results/final-first-law-20260907/run_sanitizers.py::{sparse_profile,sanitizer_commands}` | The explicit race-only interval leaves ordinary/memcheck science unchanged. Coverage delegates to `validation/network/run_sparse_validation.py::parse_transcript`; the final index reuses that profile/parser and requires a unique clean report in every actual execution lane. No second sparse solver, transcript parser or sanitizer boundary. |
| Checkpoint metadata inspection | `tools/validate_backend_results.py::checkpoint_metadata` | Smoke alias, formal restart, evidence recheck; no second HDF reader |
| Memory-protected subprocess ownership and resource observation | `tools/run_memory_guarded.py` | One owned-descendant cleanup path for memory/swap limits, optional sustained Linux PSI stalls and GPU counters. PSI measures system stalls, not Windows disk utilization; nvidia-smi is whole-device telemetry. |
| CUDA allocator-request peak evidence | `tools/validation_device_memory.py` reads Nsight's recorded allocation/free events | Existing profiler and memory guard own collection/execution. Per-process/device peaks include overlapping old/new/scratch requests; pool reserves overlap requests, managed residency and driver overhead are not inferred. No production allocator interception or hardware branch. Raw profiler files remain local because they contain unrelated environment metadata. |
| Profiled executable association (result policy only) | `validation/backend/results/device-memory-first-law-20260907/replay.py` | Reuses the shared reader's exact CUDA PID join, `OwnedDescendants` / `process_identity`, artifact observations and the ordinary runner. Only an explicitly pinned profiler launcher may precede the unique workload image. The observer does not launch another solver, parse allocation events or read process environments; incomplete and failed captures remain diagnostic records. |
| Runtime resolved-policy acceptance | `tools/validate_backend_results.py::validate_plan_values` | Live matrix runner and `qualify_cuda_amr_evidence.py` recheck the same declared policy expectations; provider expectations may differ by backend, math/input budgets may not |
| Complete runtime evidence coverage | `tools/qualify_cuda_amr_evidence.py::runtime_matrix_contract` | Full-runtime requires the canonical Cartesian/curved/uniform/generated manifests and both restart reports. It reuses existing case, provider, physics-budget and identity checks. |
| Compiler evidence attribution | `tools/summarize_cuda_compile_memory.py` | GNU time per-command maxima or historical uniquely attributed process-group samples, explicitly labeled; the same memory guard owns safety. Ambiguous multi-TU process groups fail closed |
| Header-layer audit | `tools/audit_architecture.py::audit_header_dependencies` | Reuses one source snapshot; local-cycle and indirect catalogue regression checks |
| Independent NSE/EOS acceptance data (tests only) | `validation/network/nse_reference.py`, `validation/eos/helm_reference.py` | Small immutable `tests/fixtures/{Nse,Helm}Reference.h` data; high-precision Saha roots / monomial endpoint fits do not call production math |
| Built-in independent time-integration acceptance (tests only) | `tests/fixtures/BurnTimeReference.h`, `validation/burn/time_reference.py` | Existing `test_burn_mainline_reference.cpp` serves the shared RHS read-only and tests current integrators. DOP853/Radau are independent in TIME, not reaction data. Historical `BurnMainlineReference.h` numbers/checker stay intact; CUDA short-step parity is separate |
| Independent Urca trajectories and evidence (tests only) | `validation/network/weak_reference.py`, `run_weak_validation.py` | Original Suzuki data, independent SciPy interpolation / DOP853 and Radau; archive reuses `validation_provenance` and `run_arch_with_logs`, not another hashing/process infrastructure |
| Independent hydro temporal acceptance (tests only) | `validation/hydro/time_reference.py` | Exact Fourier exponential of the PCM contact's upwind semi-discrete operator; actual CPU/CUDA ARCH runs use the existing runtime validators. Separates time error from spatial error without calling a production integrator for expected values. |
| Independent Roe-family flux acceptance (tests only) | `validation/hydro/roe_reference.py`, `tests/fixtures/RoeFluxReference.h`, `tests/math/RoeThermodynamicCases.h` | A 70/90-digit Euler eigenbasis solve, conservative jump identity and Rankine-Hugoniot relations define immutable ideal-gas fixtures. The existing hydro leaf test checks Host/Device reflection, cyclic rotation and thermodynamic identities; unchanged limiter, reconstruction and SW/VL snapshots remain intact. |
| Independent planar Sedov acceptance (tests only) | `validation/hydro/sedov_reference.py` | Kamm's closed-form similarity profile, converged energy integral and shock-split finite-volume quadrature. Published profile values, independent Euler-similarity DOP853/Radau normalization and swept mass check the oracle; ordinary runtime/provenance helpers own CPU/CUDA execution. No production formula or process infrastructure is duplicated. |
| Real generated sparse trajectory harness (tests only) | `tests/cuda/test_generated_sparse_burn.cpp`, `GeneratedSparseBurnFactory.h` and `test_generated_sparse_burn_factory.cu` in that directory | Host owns inputs/CPU/reference comparisons; thin CUDA TU only binds the selected network to the production factory. Shared IdealGas, ODE and DriverBurn; no copied reaction, EOS or ODE bodies |

## Before adding a helper or file

1. Identify its mathematical/control responsibility in the map above.
2. Search its symbol and equivalent operation in that directory and direct
   consumers. Similar names are not proof of equivalent units/conventions.
3. Prefer reusing or moving the authority. A compatibility alias/delegating
   wrapper is acceptable; a copied formula with another name is not.
4. Keep truly independent test oracles independent. Do not make expected values
   call the implementation being tested merely to eliminate apparent duplication.
5. Record moved symbols, remaining aliases, retired implementations and tests
   below. A generated TU must only bind types and call shared bodies.

## Current interface and compatibility review

Updated 2026-09-08. These decisions define the maintained interfaces. Verification
records identify the source and inputs they tested; this index is updated in
place when responsibilities change.

| Area | Maintained contract |
| --- | --- |
| Checkpoint input and restoration | One ARCH checkpoint reader and writer. Missing identity, ENUC, native composition or controller state is an error. Keep the existing payload, field-consistency checks and physical-identity checks; fresh simulations initialize their own `RunState`. |
| Solver selection | Remove unused `NumericsConfig::riemann_solver` and `ProblemGenerator::GetSolverName`, whose adapters returned empty strings. Keep the active policy selection path. |
| Factory entry points | Remove config/string overloads in `BurnDispatch`, `DiffDispatch` and `GravityDispatch`. Resolve names and backend eligibility once, then pass IDs to the concrete factories. Tests follow the same path. |
| Hydro launch boundary | `launch_resolved_run` visits the registered route once; `launch_run` requires the plan, requirements, backend, startup state and checkpoint identity by reference. No config-only selector chain remains. |
| Coordinate display | Two-dimensional curved-grid startup labels use the shared polar angle name `phi`. This changes presentation only; physical coordinates and metric operators remain unchanged. |

The removed factory entry points duplicated configuration interpretation and
construction selection, not reaction equations, ODE methods, diffusion operators
or gravity source mathematics. Their resolved-ID implementations remain the
single concrete factories.

The following boundaries serve distinct inputs or execution responsibilities
and remain supported:

- Parameter aliases, including `timeintegrator`, and free-function
  `REGISTER_PROBLEM` select the same maintained implementations.
- EOS initialization wrappers support case setup. Direct-field and free-energy
  tables are different thermodynamic representations; their mathematics and
  rank inference remain in the shared EOS implementation.
- Generated packages with `generator_version=3` remain CPU-eligible. CUDA
  requires `generator_version >= 4` and device-callable math; these are package
  interface requirements, not separate physical models.
- DenseLU/KLU/cuDSS selection retains the 31-total-equation dense boundary and
  explicit rejection of incompatible backend/solver combinations.

The full contracts are in the [Reference](../Reference.md),
[EOS format guide](../../src/physics/eos/TabularEOS.md) and
[generated-network guide](../../src/physics/network/custom/README.md).

### Verification scope

The [scientific acceptance index](../../validation/backend/results/final-acceptance-20260907/release-73a9cf50/index-final-930/README.md)
records 41 required passes and two owner-deferred large-network workloads for
its identified source. The
[maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md)
separates the current interface checks from the referenced full scientific and
source-organization runs. The current optimized CUDA build, all 98 configured
Release tests, ten development-smoke lanes and all four strict
normal/instrumented restart suites pass, including final source and artifact
identity checks. The [matched local AMR timing](../../validation/backend/results/maintenance-freeze-20260908/README.md#matched-local-amr-timing)
passes the existing field, conservation, workload and identity checks at both
sizes. CUDA is slower on these measured Sedov workloads. This is an execution
performance follow-up, not a reason to fork the shared mathematics or change
the accepted scientific budgets; the owner accepts functional-parity integration.

The configured 98-test Release inventory includes the interface controls
`checkpoint_compatibility`, `resolved_execution_plan`,
`shared_stage_scheduler`, `boundary_plan`, `amr_operation_plans` and
`state_residency`. They check the changed input and dispatch contracts within
the current CUDA-enabled build with KLU and cuDSS available.

The checkpoint controls cover complete ARCH state restoration and rejection of
unsupported formats, missing state, invalid shapes, inconsistent composition and
incompatible physical identities before AMR restoration. The dispatch controls
exercise the shared resolver and ID factories, including disabled modules and
rejected backend/solver combinations. Fresh-start initialization, active parser
aliases and the supported registration interfaces remain intact.

Commands, observed identities and resource measurements belong to the existing
[maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md).
The 289 Python tooling and architecture controls pass with
zero skips, including seven configure-helper controls. Fixed provenance
protocol inputs now belong to the [test fixtures](../../tests/fixtures/validation_provenance/README.md),
not the current scientific result set. These are actual separate checks, not
results inferred from C++ regression.

This review does not repeat the full independent scientific or complete device-safety
profile. Publication actions and third-party redistribution permission remain separate.

## Completed responsibilities and design constraints

The authority map covers the implemented AMR, geometry, burn, EOS, generated
network, sparse-solver, constant and IO responsibilities. Their accepted
scientific, runtime, safety and resource measurements are indexed by the
[Validation overview](../../validation/README.md), with explicit source
identities. Current interface checks are recorded above. Git retains the change
history; this document describes the implementation that contributors maintain.

### AMR, geometry and hydro

CPU control owns topology, Morton ordering, capacity planning and atomic
publication of a regrid transaction. CUDA owns device storage and field
migration. Ghost reconstruction, whole-family migration and reflux share their
respective mathematical helpers across backends, but do not share an algorithm
merely because all three transfer data.

Physical volumes, areas and lengths come from `GridMetrics`, including
thin-shell/polar conditioning and volume-averaged inverse radius at the origin.
Hydro CFL, geometric sources and covariant viscous operators consume these same
measures. The two-dimensional spherical polar convention remains explicit;
independent test integrals and continuum derivatives are references, not another
production geometry library.

The Roe-family thermodynamic average freezes composition at
`Xhat = wL*XL + wR*XR`, where `wL = sqrt(rhoL)/(sqrt(rhoL)+sqrt(rhoR))`.
Weighted opposite pressure paths define symmetric density and energy secants
`chi` and `kappa`, satisfying the fixed-mixture pressure jump. The conservative
jump gives `c_hat^2 = chi + kappa*(Hhat-ehat-|vhat|^2/2)/sqrt(rhoL*rhoR)`,
recovering the ideal-gas Roe relation. Near unresolved energy/density intervals,
the same EOS supplies derivative limits. This is not an exact multicomponent
Roe matrix for arbitrary composition-dependent jumps.

HLLC uses Rankine–Hugoniot star pressure and contact-speed-factored transport,
so a stationary contact does not acquire a subtractive roundoff energy flux.
Its explicit degenerate limits remain part of the shared formula. Reflection,
rotation and independent flux references test these properties separately from
full Sedov similarity or AMR convergence; matching two backends alone is not a
scientific reference.

### Burn, EOS and generated networks

The fixed-density thermal equation follows the first law, including composition
energy derivatives. All three ODE methods use the same closure/Jacobian and
accepted-increment energy contraction. Compensated state accumulation preserves
small heating on a large background; rejected trials commit neither state nor
energy. ENUC is the accepted source integral divided by the actual interval,
not the difference of independently reconstructed large EOS energies.

Weak reactions add a signed source quadrature to the existing ODE stages.
Temperature remains at the physical species-count index; optional auxiliary
equations count toward workspace extents and the 31-equation dense limit.
Generated weak derivatives include the table's density/charge dependence and
loss gradient. One emitted mathematical body is consumed through explicit
network views with backend-specific immutable storage owners.

NSE preserves an input only after the shared Saha and conservation residuals
certify it as an equilibrium. It does not use an energy cutoff or backend rule.
EOS interpolation and derivatives likewise have one mathematical owner; table
coordinates and immutable data are materialized once and supplied to each
backend. Independent time-integrated, thermodynamic and equilibrium references
have different obligations from strict CPU/CUDA parity or negative-test
snapshots; keep those criteria distinct.

Generator-source edits do not rewrite installed generated packages. Regenerate
the declared recipe explicitly and bind its identity to subsequent measurements.
Do not add a second Jacobian, energy conversion or lookup table to shorten a
backend build.

### Sparse execution, headers and measurements

DenseLU uses shared row-scaled pivot selection. CPU KLU and CUDA cuDSS own their
native factorization resources; equation/unknown equilibration, CSR structure
and ODE continuation contracts remain shared. cuDSS uses its nonsymmetric
BTF/COLAMD route. Opaque provider errors fail explicitly; only the shared
original-matrix residual can request numerical rejection and a retry.

Thin declarations avoid importing complete EOS/network catalogues into generic
launch or ODE headers. A lexical include audit checks project dependencies and
cycles; actual compiler dependencies still establish configured includes.
Preserve intentional small-helper inlining and heavy mathematical call
boundaries rather than trading runtime performance for speculative build gains.

Compiler command RSS, owned-process RSS, requested device allocations and
whole-device VRAM are different measurements. Regrid traces overlap nested
backend operations and cannot be added as independent totals. The shared
memory guard and provenance helpers own collection and execution; result
recipes should not create another allocator, process supervisor or mathematical
checker.

## Remaining maintenance decisions

- Small `minmod` and min/max families in `RegridTransferMath.h` and
  `LimitedLinearProlongation.h` remain possible scalar consolidation work.
  Both already serve CPU/CUDA. Review finite/NaN behavior and caller guarantees
  before merging a primitive; conservative migration and coarse/fine ghost
  reconstruction have different admissibility contracts.
- The built-in `molar_rhs_jacobian_frozen_screening` functions and
  `TimmesJacobian.inc` fragments are not consumed by production
  `TimmesNetworkSupport::eval_jacobian`, which uses shared automatic
  differentiation. Decide whether to remove these unused fragments or qualify
  a replacement independently. They must not become a CUDA-only derivative path.
- Complete audit150/audit200 trajectories and scaling remain the owner-approved
  larger-system follow-up in the [release standard](CudaReleaseStandard.md).
  Their absence is not an unclosed local required gate. Custom-network NSE and
  native nuclear-table converters are separate capability extensions.

## Constants boundaries

`src/physics/constant/PhysicalConstants.h` owns disciplinary SI/CODATA 2022
definitions and shared derived values. Pi delegates to `<numbers>`; radiation
and Gaussian charge-squared are derived from their fundamentals. Runtime
defaults stay in typed configuration, while algorithm coefficients and
tolerances stay beside their sole numerical owner. The air-like Cv fallback is
an IdealGas model parameter, not a universal physical constant.

Generated network data, reaction fits and Timmes data constants retain their
declared conventions. They are not duplicated backend definitions and must not
be silently rescaled. No automatic conversion between arbitrary ideal-gas code
units and cgs is implied. See the
[constant boundaries](../../src/physics/constant/README.md) and independent
[EOS](../../validation/eos/README.md) / [NSE](../../validation/network/README.md)
references when changing a value.

## Resumption checklist

Read the current [release standard](CudaReleaseStandard.md), this authority map
and the [maintenance verification record](../../validation/backend/results/maintenance-freeze-20260908/README.md).
Inspect the worktree and active build/resource state before starting another
build. Change the existing owner, update its current documentation and run the
affected checks. Keep one effective result per maintained validation entry;
retain actual source/input identities and required scientific fixtures rather
than relabeling measurements from another build.
