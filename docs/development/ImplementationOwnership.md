# Implementation ownership and refactor index

Audience: contributors. This index and its raw change ledger are not public
feature documentation; the user-facing references describe the resulting API.

Companion to [CudaReleaseStandard.md](CudaReleaseStandard.md), established
2026-09-06. Paths are repository-relative. This is a navigation/ownership index,
not another implementation or a declaration that validation passed.

## Authority map

Directory navigation starts at the [source guide](../../src/README.md).
The [CUDA runtime index](../../src/cuda/runtime/README.md) groups host control,
hydro, burn, AMR and diffusion, with thin built-in bindings under `burn/routes`.
The public backend/store entry paths remain unchanged. The post-acceptance
maintenance record preserves the old/new path mapping; historical entries below
retain the filenames and evidence context in which they were written.

Configured dependency check (core build-555): Ninja's actual dependency records
for the compact aprox13/Ideal and sparse aprox21/Ideal translation units include
`IdealGas.h` and `PolicyDescriptor.h`, but neither `HelmEos.h` nor the complete
`CudaBackendInternal.h`. This rebuild changed the shared policy descriptor as
well as Helm; do not attribute its Ideal-route recompilation to an EOS include
leak. Keep the existing EOS declaration boundary intact.

| Responsibility | Single mathematical/control authority | Allowed adapters / consumers |
|---|---|---|
| Policy names, factory routes, capabilities | `src/driver/dispatch/PolicyDescriptor.h`, `BackendCapabilities.h` | CPU dispatch TUs, generated CUDA route wrappers |
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
| Network reaction math | built-in network headers or generated v4 `.math.h` | CPU adapter + CUDA instantiation; no second generated math body |
| Generated package assembly and route binding | `tools/network/GenerateNetwork.py`, `PortableAdapter.py`, `PortableCxx.py` | The adapter moves numerical definitions into one `.math.h`; the emitted `.cpp` includes it. CMake network discovery and dense/sparse route templates bind registered types, not copied reaction or ODE formulas. Weak data/view specialization uses the separate owners below. |
| Generated complete-RHS temperature difference | `src/numerics/burnsolver/NetworkDerivative.h` | One generated `CompleteRhs` functor for Host/CUDA; built-in AD derivatives unchanged |
| Generated weak-table coordinate derivatives and corrections | `tools/network/WeakTables.py`, emitted into the same generated table/math headers | Existing `timmes::Dual` differentiates upstream bilinear coordinates; weak rho*Ye and signed loss gradients consume those results; Dense/sparse executors bind that same generated body |
| Immutable weak-table storage and borrowed views | `tools/network/WeakStorage.py`, `src/physics/network/WeakTableView.h` | Moves original data tokens into one Host store; `cuda/microphysics/device_network_owner.h` owns the upload, dense backend slot / sparse pool retain it across grid storage changes; no interpolation formulas in the adapter |
| Finite sum/product compensation | `src/core/CompensatedSum.h` | Timmes sums and generated nuclear-energy dot product; explicit FMA is intentional, implicit contraction remains disabled |
| Generated energy mass/conversion data | Emitted `generated/actual_network.H::network::mion` and `fundamental_constants.H::C::enuc_conv2` | One derived `energy_mion` accessor removes a conserved baryon mass offset for RHS/Jacobian/accepted energy; no independent MeV mass reconversion |
| Physical constants and units | `src/physics/constant/PhysicalConstants.h` | Disciplinary SI/CODATA 2022 constants, shared derived values and formula-local aliases; data boundaries in adjacent README |
| Heavy mathematical call annotation | `src/core/ArchPortability.h` (`ARCH_HEAVY_INLINE`) | Helm/Tabular leaves; removes duplicate Tabular3/4 local macros |
| Restart schema, native composition and scientific identity | `src/io/hdf5/HDF5Writer.{h,cpp}`, `src/io/chk/ChkIO.cpp` | Schema 4 stores native X and conserved rhoX in the same shared payload, verifies their consistency, and restores X without a lossy multiply/divide round trip. Schemas 1–3 retain the existing rhoX reconstruction. Common Driver/StateResidency controls materialization and output phase; no CUDA-specific checkpoint format. |
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
| Header-layer audit | `tools/audit_combination_v2.py::audit_header_dependencies` | Reuses one source snapshot; local-cycle and indirect catalogue regression checks |
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

## Change ledger

Bounded final ownership review (2026-09-07 16:04 UTC): checked the tracked diff,
new-file inventory and direct symbol consumers against frozen source
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`.
The map now explicitly covers AMR ghost/regrid/reflux boundaries, topology,
hydro CFL/divergence and stage/RKL helpers, sparse structure/execution,
generated package assembly, EOS error transport, constants and native IO.
The inspected production paths reuse their shared mathematical owners; CUDA
differences are storage, launches, failure transport and provider bindings.
Targeted physical-constant literal searches found no second project-owned
numeric definition outside the declared network/data boundaries.

The ownership-mapping item is complete for this round. This bounded review is
not an exhaustive clone scan or a scientific/sanitizer/capacity pass. The two
cleanup candidates below remain recorded: unused Timmes hand-written Jacobians
and small scalar helpers duplicated between the shared ghost and regrid math.
No source, input, numerical budget, running recipe or acceptance result was
changed by this review; no compilation or GPU execution was performed.

Policy-resolution reference reconciliation (2026-09-07): complete Release-869
passes 97/98 tests; its only failure retains pre-repair Roe/HLL/HLLC scalar
fingerprints. The existing independent Euler/RH oracle now also evaluates the
original policy-test states and entropy coefficient 0.9 at 70/90 digits. All
prior reference cases remain unchanged. The existing Host policy traversal
supplies exact Host/Device witnesses for these three slots, each also checked
against the independent fixture at the existing hydro science budget. Invalid
historical fingerprints and substituted flux bindings are negative controls;
unrelated fixed snapshots remain exact. No production formula changes. Actual
updated policy-test compilation/device execution is required before resuming
candidate qualification.

Shared energy-secant conditioning repair (2026-09-07): the compiled negative
probe retained under `validation/hydro/results/sedov-first-law-20260907/` finds
17.763% ideal Roe acoustic error for nextafter-separated high internal energies
with a finite velocity jump. An absolute 1e-10 interval guard divided pressure
roundoff by energy roundoff. `calc_glaister_state` now combines the existing
near-zero floor with sqrt(binary64 epsilon) times both endpoint energy scales,
and uses its existing weighted EOS derivative limit below that resolution.
No new EOS/backend implementation or case branch is introduced. Thirty-six
shared nextafter controls cover stationary and expanding/compressing states at
four energy scales. Positive compiled probe max error is 1.961e-16; complete
shared identities and CPU hydro leaf PASS at unchanged budgets and fixtures.
Final CUDA and Helm/Tabular application checks remain mandatory because the
nonlinear-EOS derivative limit is a local approximation, not an exact secant.

Coupled-gravity verification support (2026-09-07): the existing checkpoint
comparator's constant-acceleration reference is cellwise and valid on Cartesian
AMR. Dispatch it after ordinary shape checks but before the level-zero guard
used by spatial-profile references. Explicitly require Cartesian geometry in
that same gravity oracle. No new reference formula, production gravity code or
relaxed budget is added; the supplemental Gaussian-species AMR manifest reuses
the existing runtime validator and this independent source-law check.

HLLC finite-precision contact repair (2026-09-07): stage replay-846 reproduces
Sedov-839 exactly through the first four steps and at the terminal energy
asymmetry. The first loss of reflection occurs at the central stationary contact
in stage 2 of step 1: subtractive `F_K+S_K*(U*_K-U_K)` creates a 4.263e-14 energy
flux where the Rankine-Hugoniot mass/energy transport is identically zero.
PPM amplifies this seed; this is not evidence of a PPM interpolation defect.
The existing star-state construction now directly returns flux using
`p*=p_K+rho_K*(S_K-u_K)*(S*-u_K)`, `F_rho=rho* S*`,
`F_mn=F_rho S*+p*`, `F_mt=F_rho u_t`, `F_E=S*(E*+p*)`.
No speed threshold, symmetry detector, backend branch or PPM downgrade is added.
The two existing degenerate limits remain explicit. Stage replay-848 reaches
t=0.1 with exactly zero reflection error and central mass/energy flux throughout;
actual application and independent-reference regression remain required. New
shared leaf controls cover stationary expanding/compressing contacts at three
scales in every direction; existing nonzero independent flux fixtures stay fixed.
Negative source/output and both replay binaries are retained under the Sedov
evidence directory / build. Diagnostic attempt-845 used an incorrect harness
keyword and did not execute physics; corrected -846 records the negative case.

Shared Roe thermodynamic repair (2026-09-07): Sedov preflight-812 passes backend
parity but fails reflection symmetry; PCM first-step isolation-813 reproduces
the defect. The old directed pressure rectangle and left-only composition give
different acoustic speeds when a face is reversed. The old arithmetic pressure
also fails the ideal-gas Roe identity. Negative leaf-815 is retained together
with its source and pre-change flux/test snapshots. Weighted opposite pressure
paths at one Roe-weighted composition preserve the fixed-mixture pressure jump;
the sound speed follows the conservative energy/enthalpy jump relation. Focused
CPU identities-817 PASS at the unchanged 3e-14 leaf budget. CUDA identities,
independent flux references and actual Sedov remain required, not inferred.

Roe rotation negative-819 additionally isolates a face-direction defect: the
entropy-fix spectral radius used x velocity even for y/z faces. It now uses the
already selected normal velocity in the same shared function. Independent
reference-820 and fixture check-821 PASS: 70/90-digit values agree beyond 55
digits and the ideal Euler conservative jump residual is below 2.5e-91 at 90
digits. The three invalidated flux snapshots and the linear PCM/HLL reference
now consume these fixtures; they are not sampled from repaired ARCH output.
CPU hydro leaf build-823 and run-824 PASS, including original unrelated bit
snapshots. Rotation error is zero, reflection 2.244e-16, ideal acoustic error
1.976e-16 and pressure-jump error zero. Build-822 omitted the generated include
directory and is retained as a diagnostic command failure, not a source defect.
The full CUDA test/application has not yet run with this change.

CUDA leaf-831 passes the new thermodynamic identities and primitive execution
checks, then rejects the old 60-route fingerprint table for its changed Roe,
HLL and HLLC rows. Keep that failure and all old hashes. The independent oracle
now also derives the four limited polynomial face stencils and all twelve
Roe-family flux vectors; fixture verification-833 PASS at 70/90 digits. The
route test retains exact limiter/reconstruction/ID and unaffected SW/VL hashes,
but checks the affected physical fluxes and SSP convex stage combinations
against those independent values at its existing 3e-14 budget. This is not a
refresh from Host or Device production output. CUDA replay is pending.

CUDA hydro leaf replay-837 PASS, including the independent physical values,
reflection/rotation controls and all sixty routes. The enlarged test result
record contains FluidVector values, so its historical expected table is const,
not constexpr; diagnostic build-835 caught that test-only type issue before
build-836. No production type or formula changed for this fix. Complete Release
build-806 and hydro catch-up-836 PASS; no-op-838 confirms a consistent artifact.
Application similarity and complete release-profile acceptance remain separate.

Thermodynamic derivation: let `wL=sqrt(rhoL)/(sqrt(rhoL)+sqrt(rhoR))`,
`wR=1-wL`, `rhohat=sqrt(rhoL*rhoR)` and freeze composition at
`Xhat=wL*XL+wR*XR`. For `p_ab=p(rho_a,e_b,Xhat)`, define
`chi=wL*(p_RL-p_LL)/drho+wR*(p_RR-p_LR)/drho` and
`kappa=wL*(p_RR-p_RL)/de+wR*(p_LR-p_LL)/de`, using the corresponding
weighted EOS derivatives when an interval vanishes. Hence
`chi*drho+kappa*de=p_RR-p_LL` and swapping endpoints leaves both coefficients
unchanged. The conservative jump relation
`d(rho*e)=ehat*drho+rhohat*de` gives
`c_hat^2=chi+kappa*(Hhat-ehat-|vhat|^2/2)/rhohat`.
For an ideal gas at fixed composition, `chi=(gamma-1)*ehat` and
`kappa=(gamma-1)*rhohat`, recovering the exact ideal Roe acoustic relation.
The frozen-mixture pressure jump is not a claim of an exact multicomponent Roe
matrix across arbitrary composition-dependent EOS jumps. Existing physical
endpoint pressures and species upwinding remain responsible for those fluxes.

Sedov evidence completion (2026-09-07): the remaining matrix still required a
strong-shock similarity comparison, beyond existing AMR symmetry/conservation.
The new independent planar oracle passes published profile values at their
printed precision, shock jumps, swept mass, quadrature refinement, and two
independent Euler-similarity integrations (PASS-808). Negative-807 and its recipe
are preserved: the printed alpha=.538548 differs from the converged integral
.5387427923675265. DOP853/Radau normalization agrees with that integral within
4.1e-14; the historical printed discrepancy remains explicit, not fitted to
ARCH or handled by widening application budgets. Application runs are pending.
The point-deposition limit uses two deposition cells and three grid resolutions,
checks the actual initial energy, and compares conserved cell averages at the
same physical time. This is a planar uniform strong-shock reference; existing
Cartesian/curved AMR evidence retains its separate scope.

NSE fixed-point repair (2026-09-07): actual application-771 and shared-leaf
replay-801 show that recomputing a converged equilibrium from a cold guess
produces composition/charge roundoff and an artificial repeated source. The
same-input device replay agrees with the host's second projection; the first
projection's small state difference is amplified by the 5e-17 burn interval.
One residual-certified input check now preserves an already converged root.
It reuses the original 1e-12 Saha/conservation targets, rejects nonzero inactive
species and uncertifiable zero active abundances, and shares the Saha log
formula with ordinary Newton evaluation. Replay-803 retains the original first
projection and gives exact zero source for all subsequent Host/Device repeats.
The existing four-network test now also requires fixed-point identity and
responses to composition, temperature and density perturbations; build-804 and
regression-805 PASS, retaining the original independent references and negative
controls. Full application/regression/sanitizer/resource gates remain open.

Actual NSE diagnostic-829 PASS: sixteen cases / thirty-two CPU/CUDA endpoints,
all four built-ins, all three enabled ODEs and disabled controls. Maximum
independent relative energy closure is 2.26964e-16; maximum absolute charge
drift is 5.08854e-13, within the original 1e-12 budgets. The previously failed
aprox21 enabled cases now pass their original field/source comparisons. This
early burn-only run used build-806's linked ARCH while other test targets were
still building and hydro catch-up remained pending; retain it as transitional
diagnostic evidence, then rerun on the consistent final artifact. It took
119.324 s, peak owned RSS 395,068 KiB, no swap growth. Whole-device peak 7518
MiB includes a 7011 MiB baseline and is not per-application allocation.

Accepted-energy/tabular audit (2026-09-07): EOS-783 rejects a 3.49558% CPU/CUDA
ENUC difference despite nearly identical nuclear increments. Shared handoff
subtracted large reconstructed EOS energies and amplified their roundoff by a
tiny burn interval. All three shared ODE continuations now report accepted
increment energy with one scalar compensated accumulator; optional signed weak
quadrature is counted once and rejected trials are excluded. Host erasure and
both device executors carry the same result. NSE reuses its existing projection
energy. The final EOS failure/atomic-commit boundary is retained. Tabular basis
reuse and conditioning stay within the existing math owner, with no copied EOS
or backend-specific formula. Focused EOS polynomial/derivative controls PASS-794;
all twelve normalized EOS application cases PASS-795 at the original budgets,
including prescribed physical times. Independent endpoint conservation and
nuclear-energy balance PASS-796. Strict restart, weak/NSE, complete regression,
sanitizer and resource qualification on this candidate remain pending.
Focused CPU/CUDA thermal, CPU continuation/handoff and normalized-table checks
PASS-790. Negative-788 catches spurious rejection of exact constant sub-ULP
species transfers; all three closure checks now use the same raw increment
contraction as their accepted-energy report. No closure budget changed. The
full coupled application, strict restart and final-identity suites remain open.

Native restart composition audit (2026-09-07): sustained-740 and same-CPU
diagnostic-741 fail the strict long burn restart gate. An actual C12 output
proves `rho * X / rho` differs from native X by one ULP. The sole shared
checkpoint writer/reader now preserves X as well as rhoX in schema 4; existing
schema 1–3 reading remains available. Focused tests-744 pass exact witness
round-trip and corrupt/missing composition rejection; application rebuild-745
passes. Short four-direction restart-757/758 and sustained-native-763 now pass:
72 actual native IO/device snapshot comparisons are exact, and all required
continued burn solutions pass at the same prescribed physical time. The later
alternating forward-step clock differences are diagnosed separately from
restoration; the original failed recipes and strict restore budget are retained.
The extra species-sized host IO buffer and dataset must be included in the
remaining capacity measurements. Physical and strict temporal budgets remain.

Weak scalar-entry lifetime audit (2026-09-07): optimized CUDA PTX-677 aliases
the still-live test input with the discarded total-energy output of the scalar
source wrapper. Original ordering FAIL-674, reversed ordering PASS-676; neither
is a scientific correction. `WeakStorage.py` now annotates this full-RHS wrapper
with existing `ARCH_HEAVY_INLINE`, keeping scratch within a heavy entry. Original
failing ordering PASS-680 with no extra input probes. The production burn RHS
already consumes the combined species/total/source evaluator directly and does
not duplicate that evaluation. Final source-preservation/trajectory/sanitizer
regressions remain required. No copied network math or hardware-name branch.
The final weak/Helm trajectory target links the canonical backend archive for
EOS ownership. Its earlier direct three-OBJECT import failed the architecture
audit and is removed; no broader object-ownership exception is added.

First-law audit (2026-09-07): the independent e=T^2(1+X_B)/2,
A'=-A, B'=A, q=A control FAILS before repair (negative-626: q=0.5 but
de/dt=0.75). The earlier composition-cv convergence oracle integrated q/cv,
not the first-law equation; it is being replaced by the independently derived
energy-conserving trajectory. Shared closure/Jacobian repair and analytic Helm
pressure/composition derivatives are IN PROGRESS, not a passing release gate.
The former cv-gradient implementation moves to BurnThermodynamics.h; no second
copy remains in odeFunction.h. Existing immutable table/network data are unchanged.
Focused first-law/Jacobian/fourth-order convergence PASS on CPU/CUDA-629.
Helm's exact analytic derivatives and Tabular analytic composition derivatives
are under independent checks. Tabular polynomial third-temperature derivatives
are computed only when requested, using the existing biquintic coefficients.
No generated-network, sparse-provider or backend-specific physics is added.

Construction test follow-up (2026-09-06): one test-local
`require_metadata_only_construction` checks initial and replacement backends,
preserving the no-FluidState-upload contract while allowing exact boundary
metadata bytes and counting the metric kernel. Full regression-613 passes all
97 tests. The focused sanitizer replay under its result archive obtains actual
commands from CTest and reuses the shared provenance/process helpers; it adds
no mathematical body or second memory supervisor. Both 18-route campaigns pass.

Metadata accounting repair (2026-09-06): exact-byte exchange and staged-resource
regressions reject the old counters (negative-604, all three fail). One existing
backend resource translation unit now owns enqueue-and-account for its metadata;
block metric kernels and flux-construction fences are also included. No numerical
formula, transfer extent, launch policy or workspace capacity changes. The Host
multiblock test declares its direct CUDA ABI-header dependency through CUDA::cudart.
The full 24-case curved application matrix-601 passed before this accounting edit;
final artifact refresh remains separate.

Curved ghost restriction repair (2026-09-06): CUDA's transfer descriptor was
missing the physical-volume weights already used by Host ghost restriction.
Its Host lowering now computes those immutable measures with the existing
`GridMetrics::CellVolume` through the existing geometry adapter, validates
them, and uploads them with the cell indices. Device gather uses the same
`restriction_math::{weighted_conserved_value,weighted_species_density,
restricted_average,restricted_mass_fraction}` leaves as CPU. Cartesian unit
weights preserve previous arithmetic. No second metric formula or state
download is introduced. The existing whole-backend exchange regression now
covers all three geometries/dimensions and Current/Next/Scratch slots; the old
backend fails the added cylindrical control (negative-596).
Regression responsibility repair (2026-09-06): the reduction tests compare
the current candidate stream with a serial minimum, while independent geometric
operator/convergence tests remain the physics oracle. Retired cell-only
diffusion bit snapshots are not replaced with new observed snapshots. The CUDA
reduction test has no exact H100/sm_90 requirement; successful real kernels
exercise the configured image. The generic sparse factory checks custom
registry bindings, owner bounds and incompatible-provider rejection. Custom
physical trajectories use the existing explicit-input sparse/weak runners;
one arbitrary iso7-style state is not a universal custom-network oracle.
Builtin iso7 active/reuse/no-commit controls remain in that factory test.

Helm weak-state conditioning (2026-09-06 19:04 UTC): diagnostic-538 exposes
large-background cancellation in temperature derivatives; 540 reduces weak
round-trip energy noise to one binary64 spacing. Actual-table regression-542
then exposes a one-ULP Host/Device density-node difference, isolated directly
in pressure/grid trace-545. The owner now materializes nodes once and uploads
the same values; both electron free energy and eta consume one axis locator.
No table data, thermodynamic definitions or burn-energy budgets change.
Release focused EOS/polynomial test-547 PASS, including 36 monomials at nine
points per backend (max scaled error 2.273e-13) and EXACT zero derivatives for
a 2^60 constant background. Existing independent references and original
Host/Device budgets pass unchanged. Updated allocation/move/error and final
application gates remain pending; do not call this a release certificate.

Radial-origin follow-up (2026-09-06 17:15 UTC): null-field probe-495 and
actual-matrix spectral probe-498 expose shared origin imbalance and unstable
Cartesian timestep estimates. The common source now consumes the existing
volume-averaged inverse radius. Curved dt uses FV measures and bounds on the
same basis rotation; Cartesian math is unchanged. Both retained executors pass
origin/contraction and thermal/species/viscous convergence controls-502. This
is focused spatial/frozen-coefficient evidence, not full coupled qualification.
Core build-490 also passed with heavy pool 2 / total jobs 4; see the release
ledger for actual time/memory and the cold/incremental distinction.

Curved viscosity / build safety (2026-09-06 16:30 UTC): removed the diagonal-only
curved momentum model in favor of one basis-rotation authority for face and
cell terms of div(mu grad(v)). The existing two-dimensional spherical polar
specialization is retained. Independent 90-point spatial matrices pass on CPU
and CUDA in Release, alongside original geometry/parity controls; memcheck has
zero errors. Full coupled application runs remain open. The geometry reference
tool now archives those existing executables through the shared evidence tools,
without another process/provenance implementation.

The existing memory guard adds opt-in full-stall PSI/swap-rate observation,
with configurable limits and sustained-duration controls. Small productive
swap is allowed; missing requested telemetry fails through owned cleanup.
Matching GCC C/C++ major versions are checked before expensive Release LTO
builds. The local toolchain is aligned at GCC 12; LTO/runtime optimization stays
enabled. Two-heavy / four-total compilation is a measured candidate in progress,
not a new universal default or a completed resource qualification.
Build-578 exposed a PID snapshot/reaping race: on kernels with the historical
TGID check, `pidfd_open` can report EINVAL for a disappearing process. The same
ownership tracker rechecks the PID/start-time identity and skips only stale
snapshots, never a live process it cannot pin. Other failures remain fatal;
monitor failures are explicitly recorded before owned-tree cleanup. Targeted
tests cover disappeared/reused/live/zombie identities and unrelated errors.

Release-profile inputs / compatibility (2026-09-06): existing curved manifest
now covers 1D/2D/3D RKL annuli/wedges without a new physical implementation;
generated runtime manifest uses existing BurnOneZone, shared Helmholtz and
factory-selected providers. Both live and archived plan checks consume one
optional policy contract. GPU memory telemetry lives in the existing memory
guard, fails on missing/changed counters and uses its existing descendant
cleanup on failure. No CUDA/CPU production input changed during cold build-477.
Runtime, resource and final-identity qualification remain OPEN; see the sole
release ledger, including the compute-capability/build-image audit item.

Compiler measurements (2026-09-06): the existing summarizer now accepts GNU time
records from optional local CMake project-include instrumentation, including
Host/Device files and failed invocations. It distinguishes command maximum RSS
from sampled process-group RSS sums and refuses historical groups spanning more
than one source instead of attributing all their memory to the last source.
Six fast parser/negative controls pass. No production compiler flags or process
supervision are duplicated; cold core build-477 is running separately.

Runtime priority (2026-09-06): owner requests maximum CPU/GPU speed within the
memory envelope. Small immutable lookup outlining is WITHDRAWN: weak science /
factory control-461 takes 902.241 s, current-backend/inlineable-lookup control-467
takes 92.246 s, with identical 100.035x BE_NR error improvement and all original
scientific/parity/closure budgets passing. This Debug diagnostic is not a Release
or application speedup claim. `PortableCxx::_lookup` again permits Host/Device
inlining; it retains one body and exact data tokens, with no size/name exception.
Heavy RHS/EOS boundaries and recognized value-rate-storage reuse remain intact.
Regenerated packages pass math/provider/route controls-473, full weak science /
factory-474 (94.256 s) and audit31 multi-step/storage matrix-475 (181.484 s).
Move to optimized core measurements; full application qualification remains open.

Burn acceptance reconciliation (2026-09-06 13:42 UTC): historical approximate
main endpoints are not exact ODE solutions after independently proven shared
Jacobian/time-controller corrections. Preserve them as historical data and
negative checker controls; add independently time-integrated immutable endpoints
with the existing physical Validation budgets. This explicitly changes reference
semantics; it is NOT an unchanged bit-snapshot contract or a CPU/GPU tolerance
increase. The read-only RHS query mode shares the existing C++ test, avoiding a
second math library/instantiation target. The Python reviewer reuses the existing
independent Helm monomial-fit model and common artifact provenance. Formal build
and tests pending; actual runtime/device release qualification remains open.

Owner-approved priority change (2026-09-06 13:19 UTC): large generated workloads
move off the local release critical path to explicit external-machine follow-up.
No math, generator, sparse route or test is removed. Keep representative generated
and weak scientific/runtime qualification locally. Close built-in burn acceptance
before further optional refactors, then core builds and complete release-profile
Validation. This changes workload qualification scope, not numerical thresholds.
The sole active scope/deferral contract remains `CudaReleaseStandard.md`.

Resource/compile follow-up (2026-09-06): cuDSS owns explicit metadata/scalar
transfer, preparation-kernel and fence counts, consumed cumulatively by the
existing sparse executor instead of reconstructing vendor work from assumptions.
Vendor-internal traffic/kernels remain opaque. Sparse owners expose initial
pattern/table uploads separately; Dense initialization accounts the same table
owner. EOS initialization uses its actual variant, not the combined immutable
owner diagnostic. New compiled accounting controls are pending. The trajectory
test now separates Host diagnostics from selected-network device instantiation
through one test ABI. `PortableCxx` narrows only a recognized value-only rate
buffer, and bounds dynamic lookup calls without changing literal values or
expressions. Generator contracts 33 PASS, actual separate packages generated;
numerical and compile-cost qualification remains pending.

Sparse conditioning follow-up (2026-09-06): actual audit150 fails unscaled-417,
passes row-only-419/422, but independent integrated-energy provider control
fails row-only-421/426. Two-sided equation/unknown scaling passes both unchanged
controls-424/425. GPU-resident implementation passes provider-428, actual short
all-method audit150-429 and expanded error/recovery/zero/unit/input-preservation
contracts-431. Shared scale selection/division is `LinearEquilibration.h`; rows
and columns use the same body via an immutable index permutation. Zero equations
remain singular, scaling does not erase representable coefficients, and invalid
forcing fails closed. Separate kernel/launch ABI avoids network instantiation:
build-427 is 4.069 s, not a cold-core measurement. New owned storage is included
in provider peak estimates; transfer/kernel counters and final capacity are open.

Thin-shell/polar conditioning (2026-09-06): `GridMetrics.h` now factors radial
power differences and gives spherical volume/radial area one angular-integral
authority. The angle-addition form avoids cancellation at both poles without
altering 2-D polar semantics. Independent old FAIL-393 / repaired PASS-395,
Host metric/equilibrium/CFL PASS-397 and runtime-input CUDA PASS-404; immutable
references rechecked at 70/90 digits. No duplicate backend metric or source
formula was introduced. Full operator convergence remains a separate gate.

The existing CUDA geometry test now owns a single metric-cache population and
diffusion-kernel launch helper, reused by its backend parity and independent
manufactured species-diffusion series. The analytic continuum Laplacian is
test-only, respecting the same documented coordinate conventions without
calling either discrete operator for its expected value. Host probe-410 shows
second-order local consistency; real CUDA target build-411 is pending. No
additional production diffusion implementation or translation unit was added.

Focused evidence consolidation (2026-09-06): actual audit31 full method/storage
matrix PASS-386 (186.835 s, peak 247,728 KiB, zero swap growth). Current weak
science/factory archive PASS-389 (109.582 s, peak 441,268 KiB, zero swap growth).
The common provenance module now owns focused build-local artifact capture and
the observation helper shared with full-program capture, including replacement
guards and configured build-type selection. `run_weak_validation.py` consumes it;
`run_sparse_validation.py` reuses that same identity and process logger and checks
all method/storage/step records. Five sparse transcript negative-control tests
pass; real archive and final qualification remain pending. No new hashing,
subprocess or physics implementation was added.

BE_NR increment follow-up (2026-09-06): the same exact fixed-substep source
negative witness FAILS-381; BE_NR also loses the small cumulative heating.
It now reuses `OdeMath::AcceptedState` and solves the algebraically identical
Newton equation for the relative increment, with residual dt*f-increment rather
than a cancelling difference of large temperatures. Newton/time-error budgets,
physical projections and energy handoff are unchanged. Host exact source range
PASS-382. All three methods now consume one accepted-state authority; full
controls and actual network trajectories require rebuilding. BD/ROS4 real
middle trajectories already passed-378/379 before this BE-only change.

Accepted-substep follow-up (2026-09-06): multi-step ROS4 trace-371 has matching
requests/order/rejects and no failed original-matrix residual; first two energy
handoffs are identical, the third differs by three energy ULPs. Independent
16-substep constant-source witness FAIL-372: small representable total heating
can disappear entirely when each accepted substep rounds back to its large
background. `odeFunction.h::AcceptedState` now owns the one compensated accepted
state/projection contract for BD/ROS4; both continue to use their existing stages
and error rules. Native linear providers and energy handoff are unchanged.
Source-sized continuation storage grows by two doubles per equation, explicitly
accounted through existing sizeof-based pools. Host ROS4 range PASS-373;
multisubstep Host/CUDA, retry/rollback and real-network qualification are pending.

ROS4 offset follow-up (2026-09-06): audit31 step-3 ENUC FAIL-358. The original
two-point thermal witness passes-359, but a full dyadic source range reveals a
two-ULP error at a large offset-360. `ode_ros4.h::stage_state` is the single affine
stage/candidate-state helper and consumes `core/CompensatedSum.h`, not another
summation implementation. The ROS4 tableau, error weights, RHS and admissibility
checks are unchanged; unused stages are never read for zero coefficients. The
expanded independent test authority is shared across BD/ROS4 and Host/CUDA.
Host source range PASS-361; full controls and actual trajectories remain pending.

BD increment representation (2026-09-06): real audit31 middle-duration ENUC
FAIL-345 is followed through the shared BD continuation-351: no request/order/
reject divergence, final temperatures differ by five ULPs, then energy
differencing amplifies the discrepancy. An independent constant thermal source
has an exact dyadic solution: the previous absolute-state midpoint/extrapolation
misses it by two ULPs, a 50% error in the tiny heating increment at T=2^30-352.
`ode_bd.h` now accumulates/extrapolates macro-step-relative increments; physical
RHS inputs and actual stage clamps still use materialized states. The redundant
correction array is reused, so continuation storage does not grow. There is no
backend-specific mathematical body or tolerance change. The exact Host witness
now has zero ULP error-353. Shared signed heating/cooling Host/CUDA tests and the
real network trajectory remain pending; historical fixtures are unchanged.

Real-network trace increment (2026-09-06): nonsymmetric cuDSS BTF/COLAMD is
selected after the same compiled audit31 trajectory fails with nested-dissection
and matching, then passes unchanged with BTF-330; tiny trace residuals, not just
an ENUC subtraction floor, caused extra GPU retries. Matching is incompatible
with BTF and is disabled. Independent provider/mixed-unit controls PASS-329,
weak factory PASS-331, all manufactured sparse ODE/singular-retry controls
PASS-333. Production rebuild and longer trajectories remain pending. Native
`CUDSS_DATA_INFO` is now named `device_info`: its opaque resource/execution errors
must not masquerade as stiffness. `CuDssResult::require_success` is the one Host
error boundary; only the common original-matrix residual generates adaptive
numerical rejection. The previous matching increment below is historical.

cuDSS conditioning/retry increment (2026-09-06): native matching/scaling repairs
the real weak first linear correction without changing ODE/physics or residual
budgets. Device-side rejection now piggybacks on the existing request integer,
including terminal completion; the provider invalidates factors and reanalyzes
matching/scaling on retry. No added matrix/state download or scheduling fence.
Focused build-293 PASS (32.180 s, peak 439,392 KiB, zero swap growth); actual
GPU provider and all three manufactured sparse ODEs PASS-294/296, including
singular retry, pool reuse and no-commit. The extended independent abundance /
temperature / integrated-source linear control passes-296. Full production
large-network, resource accounting and final application qualification remain.

Production weak factory increment (2026-09-06): the Host-only
`CudaBurnArguments` adds a persistent owner slot without importing network
catalogues into launch declarations. Typed Dense and sparse bindings pass the
same immutable `Network` value into shared ODE continuations. Real Urca factory
controls PASS-257 on actual CUDA for BE_NR/BD/ROS4, DenseLU and cuDSS, two/three
cell buffer generations, missing/foreign-owner rejection, full cell energy,
composition and ENUC handoff. Independent science, real AMR/application runs,
construction-transfer accounting and final identity qualification remain open.
The new non-default-stream/invalid-upload controls are not yet included in that
pass; generated registry/delegate builds are next. Focused build-256 takes
15.018 s / peak 346,820 KiB, not a cold-core build metric. Attempts 251/252/254/255
were standalone harness configuration/link failures, not scientific passes.

| Date | Change / authority | Evidence | Remaining work |
|---|---|---|---|
| 2026-09-06 | Release standard and this index established | Read-only review of current source and prior logs | All open release gates remain open |
| 2026-09-06 | Central constants; Helm/Tabular/NSE/conductivity/ideal fallback/gravity default/pi aliases | `release-foundations-build-01.log`, `physical_constants` PASS | Final program regression pending; network constants unchanged |
| 2026-09-06 | Composition uncertainty in the shared indicator; runtime species count on both views | `refinement_indicator_math` PASS (zero, signed noise, trace, unit rescaling, every species) | Real GPU and end-to-end topology/parity pending |
| 2026-09-06 | Shared physical spacing replaces duplicated diffusion spacing; physical hydro CFL on both paths; corrected spherical angular areas and volume-averaged source radius | `curvilinear_metrics` PASS (analytic area/length/CFL/rest balance) | Multidimensional convergence and final GPU qualification pending |
| 2026-09-06 | External-gravity per-cell math extracted; geometric kernel renamed to `HydroSourceKernels.cuh` and accepts gravity POD | New GPU source oracle added; build in `release-foundations-build-02.log` | Production integrator/AMR/restart gravity runs pending |
| 2026-09-06 | `file_identity` moved out of runtime-input closure for reuse; actual registered network package + configured sparse library content hashes | `tests/test_validation_provenance.py`: 30 PASS | Final linked-dependency/version qualification and full release profile pending |
| 2026-09-06 | Formal restart distinguishes process step 3 / selected checkpoint step 2; adds terminal-step-3 four-way resumes and actual restore witness | Provenance 31, smoke runner 12, timeout logging 3 Python contracts PASS | Final binaries must rerun all eight routes per problem |
| 2026-09-06 | Shared heavy-call annotation for generated RHS/Jacobian/rate stages and Jacobian sink; generator content fingerprint | Portable generator 14 contracts PASS | Actual regenerated large-network compilation/parity pending |
| 2026-09-06 | Reused memory guard adds elapsed time and sampled owned-process RSS; no duplicate process supervisor | Memory guard 7 contracts PASS | Sampled RSS is diagnostic, system headroom remains safety authority |
| 2026-09-06 | Generic ODE/DriverBurn no longer include the network catalogue; generated CUDA routes include only their selected network through the existing CMake inventory | Inspection found the umbrella defeating per-network split | Full rebuild and dependency audit pending; CPU factory remains catalogue owner |
| 2026-09-06 | Smoke SHA-256 implementation replaced with the existing validation alias | One hashing authority in `validation_provenance.py` | Smoke unit regression pending |
| 2026-09-06 | CPU indicator now passes the lightweight geometry view into shared Host/Device math | Four final CPU foundation/ODE contracts PASS, `release-foundations-build-09.log` | End-to-end AMR topology still pending |
| 2026-09-06 | Generated Jacobian row call boundaries preserve original expressions/write order in one header | 15 portable-generator contracts PASS | Actual audit200 compiler capacity/parity pending |
| 2026-09-06 | Existing qualifier adds three-matrix full-runtime profile; explicitly not a complete release certificate | 32 provenance/qualification contracts PASS | Independent physics, sanitizer, capacity aggregation still open |
| 2026-09-06 | Removed `CudaBurnNetworkTypes.h`; generated routes already bind selected types, resource sizing now uses the actual compact matrix ABI and runtime species count | No remaining consumers of the removed map; tracked file recoverable through Git | Full rebuild/workspace regression pending; no numerical code removed |
| 2026-09-06 | Shared NSE entry call boundaries stop force-expansion into each ODE path | Guarded aprox19 Ideal/Helm compile 98.359 s, peak owned RSS 3,271,612 KiB | Actual full-core and physics regression pending; NSE equations unchanged |
| 2026-09-06 | SuiteSparse fetch no longer inherits ARCH's directory link contract; provenance hashes final-link sparse artifacts | KLU+cuDSS CMake generation PASS; 33 provenance contracts PASS | Final archives must finish building before evidence can be captured |
| 2026-09-06 | Three identical accepted composition-energy loops moved to existing `OdeMath`; exact summation order retained | `sparse_ode_continuation` PASS including independent dyadic energy/reverse/zero oracle; build-26 | Does not implement weak-loss quadrature |
| 2026-09-06 | Burn cell policy separated from CPU grid iteration; matrix workspace moved to existing continuation contract; EOS aliases moved to existing interface | Six direct Host header syntax checks PASS; architecture and CPU foundations PASS | Final CUDA/core rebuild pending |
| 2026-09-06 | Sparse typed owner now includes shared `DeviceAllocation.h`, not `CudaBackendInternal.h`; per-EOS owners include their own implementation | Lexical layer audit PASS; existing CUDA compile probe now asserts EOS types remain incomplete through launch ABIs | Final owner/lifetime/runtime tests pending |
| 2026-09-06 | Restart runner selects by the shared metadata reader; comparator validates the exact extra terminal CHK/PLT against both actual source files | Root tooling 114 and architecture 90 contracts PASS; real smooth restart all eight routes PASS, log-42 | ENUC restart and full-runtime profile pending; no runtime output counter reset |
| 2026-09-06 | Diffusion records/mapping moved verbatim to shared `DiffusionTypes.h`; retired the empty diagnostic adapter and its audit exemption | CUDA incomplete-Grid/EOS probe PASS, log-53; direct Host header checks logs-47/50/55 | Full core rebuild log-54 pending; removed tracked adapter is recoverable in Git |
| 2026-09-06 | Raw device grid binding is Host duck-typed; AMR flux pointer ABI forward-declares its shared plan records | Narrow declarations no longer import Grid, flux operators or integrator bodies; architecture 92 contracts PASS | Actual plan/storage owners still explicitly include their complete definitions |
| 2026-09-06 | Timmes generic support imports `GlobalDefs.h`, not parsing/dense/sparse implementations | Exact config record and duck matrix methods remain; dependency regression covers all three forbidden imports | Full core rebuild log-54 pending |
| 2026-09-06 | Generated RHS isotope balances gain call boundaries alongside Jacobian rows, preserving expressions/order in one file | Portable generator 16 contracts PASS; new packages in a separate external root | Row-split variant not yet compiled; do not inherit earlier audit200 pass |
| 2026-09-06 | Actual AMR flux kernel explicitly imports the shared plan it dereferences; pointer ABI remains narrow | Build-54 correctly exposed missing include; build-57 resumes | Final core and runtime checks pending |
| 2026-09-06 | Existing numeric-dump driver compares row-split 150/200 packages to the preserved prior variant | Strict Host builds log-58; 46,506/82,006 finite values bitwise identical | No claim of row-split CUDA compile/runtime or independent trajectory qualification |
| 2026-09-06 | Final header owner/ABI split validated in the actual core, not only lexical checks | Build-57, nine CPU/GPU tests-60, Cartesian/curved and both restart suites-62 PASS | Cold-build/concurrency and full release qualification remain open |
| 2026-09-06 | Generated network setup imports the `SimConfig` authority directly; audit also catches missing project-namespace headers | Root tooling 116 and architecture 94 PASS; regenerated audit31 Host build/parity-69/70 and CUDA build/math-72/73 PASS, no parsing include in compiler dependencies | Separate package only; registered core package unchanged; actual large-network gates remain open |
| 2026-09-06 | Terminal parity reuses checkpoint metadata rather than an optional scientific-oracle record for time/step identity | Root tooling 117 PASS including time/NaN/parameter-drift controls; real uniform-gravity RK2/RK3 parity-75 and independent analytic check-76 PASS | Final profile integration and coupled AMR/restart/curved/long-run qualification remain open; no copied runtime reader |
| 2026-09-06 | Extra generated RHS-row transform withdrawn; retain upstream RHS body with existing heavy-call boundary | Actual audit200 row experiment compiled and passed math-65/77 but did not establish compile/RSS benefit; historical artifacts retained; final audit31 Host/CUDA build/parity-80/82 PASS | Jacobian boundaries and header fixes remain; audit150 derivative and large production trajectories remain open |
| 2026-09-06 | Shared DenseLU compares candidate pivots against original row maxima and rejects nonfinite matrices; no backend-dependent arithmetic | BD stage trace-89 isolates first amplified correction, matrix-91 captures its actual system; scaled trace-94 has identical CPU/GPU minima for 20 burn halves; frozen-main CPU and continuation test-93 PASS | Independent mixed-unit oracle and real-program rebuild/regression pending; no change to BD, energy handoff or validation budgets |
| 2026-09-06 | Independent mixed-unit compact LU witness shared only by tests, not used as the numerical implementation | Host/CUDA regression-97 PASS; original HEAD provider fails the same oracle in negative-control-96 | Core application and all built-in policy routes rebuilding in log-98; no final release claim |
| 2026-09-06 | BD conditioning repair validated in the real driver, reduction and checkpoint path | Core build-98 PASS, original two-block BD checkpoints 1/2/5/10 PASS-101 (dt_burn relative difference zero), all 16 built-in policy contracts PASS-102 | Final Debug/Release release-identity reruns still required |
| 2026-09-06 | Generated nuclear masses/conversion extracted from upstream emitted energy data; portable energy_weight delegates to the same mion lookup | Source inspection of installed SimpleCxx `_mion` and runtime probe-100 demonstrate the prior convention mismatch; generator contracts 18 PASS | Regenerated package checks and temperature-difference stability pending; existing registered packages are unchanged |
| 2026-09-06 | Generated temperature derivatives use the new shared fourth-order complete-RHS policy; upstream nuclear-energy dot product uses compensated products in the existing summation authority | Host/CUDA analytic polynomial/sine convergence, boundary/domain/input preservation and exact product-cancellation controls PASS-106/107/111; regenerated audit31 full math PASS-112/113; 120 root tooling tests PASS | Audit150 compiling-115, audit200 and production trajectories pending; weak tables/loss quadrature not implemented by this increment |
| 2026-09-06 | Three thermal Jacobian assemblies consolidated; correct derivative of enuc/cv including cv composition/temperature dependence | Analytic cv=T probe-116 shows first-order ROS4 temperature; correct-J-only control-118 and common fix-122 recover fourth order | Host/CUDA independent composition-dependent case, unchanged main references and real-program regressions pending; finite EOS sampling cost must be measured |
| 2026-09-06 | Full burn RHS also deduplicated; EOS gradient supports a duck-typed analytic provider, with IdealGas reusing its linear mixture coefficients | Host/CUDA thermal order/chain-rule cases PASS-124; DOP853 and Radau references of the actual built-in shared RHS agree at roundoff-130 | Original main fixture fails at aprox19 BE_NR after the mathematical correction; no fixture or budget changed; independent control review and final rebuild still required |
| 2026-09-06 | Generated energy gauge removes a common conserved baryon rest mass before compensated accumulation; stoichiometric validation fails closed | Baryon diagnostic-131 reduces audit150 derivative disagreement from 8.98e-10 to 2.21e-12; exact-rational reaction-Q and invalid-gauge tests pass; 121 tooling tests PASS; regenerated audit31 actual Host/CUDA math PASS-141 | Actual audit150 compilation-143 and audit200/full trajectories pending; preserved prior assets unchanged |
| 2026-09-06 | BE_NR separates Newton convergence from a backward-Euler/trapezoidal local time-error estimate; PI exponent matches its dt^2 estimate | Host/CUDA analytic tolerance control PASS-147, errors improve 8.52x for 100x tighter local tolerance; original HEAD BE_NR fails identical control-150 with unchanged error and one step | Real-network trajectories, unchanged historical baselines, sparse continuation and final runtime qualification pending; no acceptance-budget relaxation |

2026-09-06 constants follow-up: replaced the relocation-only profiles with one
disciplinary SI/CODATA 2022 set and updated all eligible consumers. Removed
unused Helm aliases, moved the air-like Cv model default back to IdealGas,
derived radiation constants and Gaussian e^2 from their sole fundamentals.
Definitions/high-precision derived-value contracts and CPU sparse continuation
PASS-159; existing declaration probe compiles the device constant references;
an actual launch of that same kernel returns all 11 values bit-identically to
Host (build-161 / run-163). Tooling 121 PASS-162. These focused results do not
close the affected EOS/NSE/burn scientific or historical-snapshot gates. The
shared core/EOS/NSE rebuild is running-158. Audit150 actual Host/CUDA math
PASS-155, unchanged 2e-10 budget. Audit200 build-156 hit the memory-headroom
guard while the core also compiled (zero swap growth, no compiler/numerical
pass); retry separately after the core. No further heavy overlap on this WSL.
The architecture audit exposed a missing exact Helm-resource consumer record
for the already added BD controller test, not a duplicated production owner.
Added that precise entry and reused the positive/foreign-source/object tests;
94 architecture contracts PASS-167. Current-constants built-in DOP853/Radau
review completed-165 without overwriting the pre-change library; final EOS/NSE
snapshot reconciliation and application qualification remain open.

2026-09-06 independent-oracle increment: four NSE networks agree between 70/90
decimal digits (log-170); current Host NSE passes the 2e-12 independent budget,
maximum abundance error 2.04e-13 / energy error 4.27e-14 (log-174). The unmodified
HEAD NSE/constants fail the same oracle (negative-control-179); no production
solver was used to create these new reference values. Helm monomial endpoint
fits agree at 60/80 digits for weak/strong Coulomb and radiation-dominated points,
with the unchanged table hash (log-175); Host diagnostic-177 agrees within
4.43e-14 pressure, 7.78e-16 energy and 3.34e-15 cv. Exact inverse quantities and
an independent pressure-coordinate DOP853 entropy path are recorded-180/181.
These are test oracles, not a second maintained production backend. Existing
CUDA tests still contain the earlier constants' snapshots until build-158
finishes; integrate/reconcile them before calling these scientific gates closed.

Independent NSE references are now consumed by the existing full test. Actual
GPU PASS-185 includes unchanged reaction snapshots, 2e-12 Host/Device comparisons,
and exact invalid-output preservation. EOS reference test-184 passed the new
central state/inverse/path density/temperature/pressure checks but its path-cs
comparison failed (1.58e-10 versus the earlier 1.2e-10 same-implementation
snapshot margin). The new high-precision/decimal-grid reference is not that
old iteration snapshot: it now uses the already established independent-state
sound-speed budget 4e-10 for the same 1e-4 differenced quantity. This is an
explicit reference-contract change, not an unchanged-budget claim or an
empirically enlarged GPU parity budget. Cv/xne/fallback reference bits likewise
use small independently rounded arithmetic windows; inverse energy reuses the
existing 8e-15 inversion contract. The actual `compare()` Host/Device checks are
unchanged. Obsolete unused Helm raw literals/helper were removed from the test
and remain recoverable in Git. Updated EOS build-186 and actual five-test suites
187/198 PASS; the latter is archived under the EOS results tree with unchanged
before/after identities. This does not qualify the ARCH application.

Generated scalar API follow-up: the CPU-only base adapter now owns `aion` and
`energy_weight`; the portable adapter replaces their bodies once. Real weak
package compile negative-193 / positive-194 and zero-duration all-ODE API-195
pass, tooling 122 PASS-191. Audit200 build-188 and actual math-199 now PASS;
large production trajectories remain open. Weak-table diagnostic-197 confirms
that frozen-rate abundance derivatives miss rho*Ye dependence, and the energy
gradient omits weak losses. Next, signed loss quadrature must travel through
the existing ODE stages with exact state-layout/dispatch accounting, before
enabling a generated weak network or qualifying its trajectories.

Shared energy quadrature increment: all three existing ODEs now carry an
optional passive signed-energy component through their own stages, error
estimates and rollback; temperature remains at NUM_SPECIES. The common Jacobian
includes its full row and does not treat this integral as an EOS composition
input. Analytic cooling with and without reaction, time refinement, exact
Jacobian and failed-solve no-commit controls PASS on Host and actual CUDA
(206/207/208); the previous implementation fails the same new contract-201.
Runtime packing, exact matrix-size dispatch, CSR coupling, real weak derivatives
and owners are still open; generated weak packages are not enabled by this test.
The new `tools/network/WeakTables.py` changes only scalar typing of the existing
upstream table coordinate expression and reuses `timmes::Dual`; its natural
coordinate gradient adapter passes analytic power-law/clamp/domain controls and
real-table difference refinement-212. Thirty-six original RHS/energy sample
records stay bitwise identical-214. The emitted weak rho*Ye correction and loss
gradient share one visitor; the real Urca finest-difference Jacobian discrepancy
falls from 1.0 to 3.69e-9 (197/217). CPU weak numerical bodies now live in one
Host-only `.math.h` instead of a vector-specific second Jacobian; include-guard
and balanced-loop transforms are shared with the existing portable generator.
No new AD or interpolation implementation is introduced. This is not yet
GPU table ownership or full generated weak trajectory qualification.

Runtime layout increment: CPU driver allocations now use the selected ODE's
actual extent; sparse device lanes clear all auxiliary state on reuse. Registry
layout metadata has no reaction-body include and is checked against the actual
generated type. Both factories and dense workspace sizing use the complete
31/32-equation boundary. The common ODE helper declares thermal/source CSR
coupling at the physical temperature index; generated temperature differences
sample only the N+1 physical state. Existing three ODE continuations, including
signed source, CSR suspension and failed-solve retry, PASS-229 with actual CUDA
thermal controls and CPU driver buffer reuse. The factory test's unconditional
KLU-disabled assumption was fixed to test its actual build capability. These
focused passes do not qualify current core/application or real weak trajectories.

Explicit-view continuation increment PASS-237: the existing three shared ODEs
store a trivially copyable network value across linear suspensions; stateless
networks use an empty value. Host factory binding uses `make_host_burn_network`
and device executors receive their own view explicitly. Two independent rate
bindings pass analytic Host/CUDA trajectory controls. The existing convenience
CUDA ODE helper also had a remaining last-component temperature assumption;
it now uses NUM_SPECIES. No second network evaluator or ODE is added. Weak data
lowering and production owner wiring are the active work, not a capability pass.

### Duplicate candidates still under review

The small `minmod` and min/max helper families in
`amr::regrid_math` (`RegridTransferMath.h`) and `amr::prolongation_math`
(`LimitedLinearProlongation.h`) remain a consolidation candidate. Both already
serve Host and CUDA, so this is not a backend physics fork. Their finite/NaN
branch behavior and caller guarantees require review before sharing a scalar
primitive. Do not merge the full algorithms: cell-family conservative migration
and coarse/fine ghost reconstruction have different admissibility contracts.

`aprox13/19/21::molar_rhs_jacobian_frozen_screening` and their
`TimmesJacobian.inc` fragments are currently not consumed by production
`TimmesNetworkSupport::eval_jacobian` (which uses shared AD). Do not silently
enable them as a compile shortcut: that would change the derivative authority.
Decide between removal of unused code and independently qualified replacement;
never maintain a CUDA-only Jacobian fork.

Header dependency review is part of every split. The lexical audit found no
current local include cycles; it rejects new cycles even with header guards,
and rejects indirect `Networks.h` imports from generic burn/ODE headers. It
does not pretend to resolve external/generated headers or conditional compilation;
actual compiler dependency records and builds remain necessary. Missing explicit
relative and existing-project-namespace source includes are rejected too; bare
generated names and unrelated SDK prefixes remain the configured compiler's
responsibility. Architecture contracts: 94 PASS.

The Timmes and generated setup parsing includes have now been pruned as recorded
above. Existing external generated packages retain their old content until
deliberately regenerated; source-generator changes do not rewrite those packages.
Recheck all direct consumers before pruning; do not edit numerical
headers halfway through a qualification build or substitute an umbrella include
to mask a missing direct dependency.

## Constants boundaries

Centralize physical constants, conversions and mathematical named constants;
document units and provenance. Runtime parameter defaults remain in typed
configuration; algorithm coefficients/tolerances stay beside their sole numerical
authority and are indexed here, not mixed into a universal global namespace.
The owner's subsequent 2026-09-06 instruction supersedes the earlier preservation
of per-consumer profiles: Helm, tabular fallbacks, NSE and transport use the same
current SI/CODATA 2022 definitions, grouped by discipline. Pi delegates to
`<numbers>`; radiation and Gaussian charge-squared are derived once. No legacy
profile is retained. The 718 J/(kg K) air-like Cv fallback belongs to IdealGas,
not to universal constants. Network generated data, reaction fits and Timmes
constants remain explicitly deferred data contracts, not backend duplicates.
No automatic conversion between arbitrary ideal-gas code units and cgs is added.
Frozen numeric snapshots affected by this authorized update require independent
current-constant oracles in the existing Validation; never self-refresh them.

## Historical coupled-burn investigation

These notes preserve the earlier diagnosis and design constraints. Current
ownership is in the map above; current gate status is in
`CudaReleaseStandard.md`. Subsequent shared derivative/energy and weak-storage
repairs have passed focused scientific and factory checks, so the old pending
design statements below are not a second active task list.

The new uniform BD failure is a controller witness, not evidence of a different
final energy formula: final energy/ENUC match exactly, but the minimum limiter
also includes earlier Strang half-steps. Next isolate the per-half-step energy
handoff and its reduction origin under the unchanged input. Do not reset/clamp
the persisted limiter because it is inactive for this particular tiny timestep,
and do not substitute a looser comparison budget for this investigation.

2026-09-06 02:31 UTC update: first-divergence tracing now identifies the dense
provider's absolute pivot comparison as the amplification site. The actual
captured matrix places a temperature coefficient of about 1.49 ahead of the
composition identity diagonal. Although the matrix is not normwise ill
conditioned, the RHS components have different physical scales; subtracting
thermal-size intermediates loses relative accuracy in the tiny abundance
correction. The shared `DenseWrap.h` now uses original-row-scaled pivoting.
An independent analytic 2x2 subsystem witness is owned by
`tests/math/DenseLuCases.h` and used by Host/CUDA tests; it also exercises row
unit changes, factor reuse and singular/nonfinite rejection. Temporary detailed
stage-trace instrumentation was removed after preserving diagnostic logs; the
focused `test_burn_controller_parity.cu` remains a production-policy regression,
not another ODE implementation. The original full application BD case now
passes (log-101); final release-identity reruns remain open.

The row-split audit150 also exposes an energy-temperature derivative comparison
failure (logs-66/67), although the other checked arrays pass. The current adapter
uses centered finite differences of the complete screened RHS/energy. Upstream
rate-derivative records alone are not a complete replacement: the generated
screening implementation does not fill its temperature derivative. Any numerical
improvement must preserve that dependence, cover weak-loss derivatives when
enabled, and be checked against an independent temperature-refinement reference.
Do not create a CUDA-only derivative or widen the 2e-10 comparison budget.

Generated weak-table support also exposes an existing interface limitation:
`BurnerHandle` requires a stateless burner and ODE math calls static network
entries. Porting real table storage needs an explicit shared network data/view
contract, analogous to EOS views, with Host/CUDA owners behind the existing
factory. Do not introduce process-global device pointers or a CUDA-only ODE/
interpolator. Keep builtin stateless networks as a zero-storage case of that
contract. The view/owner design and its lifetime tests are still pending.

Weak energy cannot be recovered from isotope mass differences alone. Its
composition/temperature derivatives and accepted-step loss integral must use
the same rate/interpolation authority and the appropriate ODE stage weights.
Changing the accepted energy contract requires independent conservation/loss
and time-refinement tests, not merely matching two implementations at one state.

## Resumption checklist

Read the release ledger, `git status --short`, and only the affected entries
above. Inspect live build processes/memory before starting another build. Reuse
named logs/test commands from the ledger; verify evidence identity rather than
rerunning unrelated searches. Update paths when moving files.
