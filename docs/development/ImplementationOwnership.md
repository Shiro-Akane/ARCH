# Implementation ownership

Audience: contributors. This index describes maintained responsibilities,
design constraints and verification; user-facing references describe the API.

Use alongside the [acceptance checklist](CudaReleaseStandard.md).
Paths are repository-relative. This is a navigation/ownership index,
not another implementation or a declaration that validation passed.

## Authority map

For general directory navigation, please start with the [source guide](../../src/README.md).
The [CUDA runtime index](../../src/cuda/runtime/README.md) categorizes host control, hydrodynamics, burning, AMR, and diffusion, placing thin built-in bindings under `burn/routes`. The public backend and storage entry paths remain unchanged. Additionally, the [maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md) documents path mappings and measured build checks. Please note that EOS declarations remain strictly separated from complete selected implementations; therefore, changes to common policy headers may trigger rebuilds across several routes without implying a leaked EOS dependency.

Coordinate layout, measures, geometric sources and stress share the four
headers in [curvilinear](../../src/numerics/curvilinear/README.md).
`Reconstruction.h` and `InvariantDomainFlux.h` own selected methods and face
assembly. Runtime and the common scheduler bind stage storage, domain boundary
work and publication; the numerical-state and EOS owners validate physical states.
Current verification, source identities and costs are maintained in the
[release qualification index](../releases/README.md).

| Responsibility | Single mathematical/control authority | Allowed adapters / consumers |
|---|---|---|
| Policy names, factory routes, capabilities | `src/driver/dispatch/PolicyDescriptor.h`, `BackendCapabilities.h` | Resolve configuration names and backend support once; burn, diffusion and gravity factories consume resolved policy IDs. CPU dispatch TUs and generated CUDA wrappers bind implementations without a second string-selection path. |
| Compiled CUDA image compatibility | `cmake/cuda/CudaCodeImages.cmake` resolves the configured image set; `RuntimeProbe.cpp::cuda_image_compatible` checks ordinary SASS/PTX eligibility | Image metadata is private to the probe translation unit. Descriptors retain genuine feature requirements, not a local GPU-model floor. Existing startup resolver owns strict CUDA rejection / pre-construction Auto fallback. |
| AMR selected fields and indicator | `src/amr/refinement/RefinementIndicatorMath.h` | `AmrTree.h`, `cuda/amr/RefinementIndicators.cu` |
| AMR conservative transfer | `src/amr/transfer/RegridTransferMath.h` | Shared conservative prolongation/restriction: native axisymmetric Mphi uses W; other fields use V. Host migration/ghost gathers and CUDA kernels supply measures, readers and scratch storage; Runtime owns topology/storage publication; transfer operators retain their original admissibility and conservation contracts. |
| AMR coarse/fine ghost reconstruction and restriction | `src/amr/transfer/LimitedLinearProlongation.h`, `ConservativeRestriction.h` | `GhostExchange.h` and CUDA `CoarseFineExchangeKernels.cuh` bind samples to the same scalar/species helpers. Ghost reconstruction and full regrid-family migration retain their distinct admissibility and conservation contracts. |
| Mixed-level 2D ghost corners | `src/amr/exchange/ExchangePlan.h::make_same_level_corner_completion_plan`, `GhostExchange.h` | After coarse-fine face fills, complete only same-level neighbor corners using the existing phased copy executor. Logical and Host-lowered plans remain cached; ordinary Device boundaries and staged migration consume the same recipe. No physics or active-cell update belongs here. |
| AMR singular-coordinate chart ghosts | `src/amr/exchange/CoordinateSeamPlan.h`, `CoordinateSeamMath.h` | The topology plan owns physical donor lookup and basis signs; Host and `src/cuda/runtime/amr/CudaBackendCoordinateSeam.cpp` apply the same admissibility and interpolation math. The CUDA adapter binds device slots and uploads only stencil metadata, outside the gravity source. |
| AMR flux registration and reflux | `src/amr/flux/AmrFluxMath.h` | Host `FluxRegister.h` and CUDA `AmrFluxSurfaceKernels.cuh` use the same orientation, stage-weight and conserved-species corrections; native axisymmetric Mphi registration/reflux uses T/W, ordinary fields A/V; route plans and storage traversal are separate. |
| Topology/Morton and transaction plans | `src/amr/topology/Morton.h`, `AmrTree.h`, `RegridExecutionPlan.h` | Host control and atomic plan/publication lifecycle; CUDA adapters own D2D migration/storage, not a second tree or Morton implementation. |
| Physical volume/area/length | `src/numerics/curvilinear/GridMetrics.h`, `GridGeometryView.h` | Driver CFL, GPU metric cache, hydro/diffusion/AMR |
| Hydro CFL and cell divergence | `src/driver/DriverUtils.h::{evaluate_cfl_cell_dt,finalize_cfl_dt}`, `src/numerics/integrator/HydroCellUpdate.h::accumulate_cell_divergence` | Host traversal and CUDA `HydroStateKernels.cuh` share the cell formulas; physical spacing/areas come from GridMetrics or its device cache. |
| Hydro/RKL stages and state publication | `src/driver/schedule/StageScheduler.h`, `src/numerics/diffusion/DiffFunction.cpp`, `DiffusionAMRStages.h` | Common scheduling owns hydro weights and slot/ghost/reflux ordering; DiffFunction owns RKL coefficients and DiffusionAMRStages the shared cell updates. Host and CUDA executors bind those descriptors and helpers. Host AMR, single-block and CUDA consume unscaled L(U); physical dt is applied with stage coefficients once. Raw boundary-flux capture and real-dt reflux retain their separate budgets. |
| Shared run orchestration and resources | `src/driver/Driver.h`, `DriverRuntime.h/.cpp`, `DriverBoundary.cpp`, `DriverRegrid.cpp`, `DriverIO.h/.cpp` | One loop and one runtime own active identities, ledger, clock and backend. Boundary completion is distinct from Host materialization; regrid retains transactional publication/rollback. IO consumes the existing serializers. Implementations are linked with `arch_solver_dispatch`. |
| Split-stage execution binding | `src/driver/stages/DriverStages.h` | EOS-dependent CFL, Hydro, RKL and a single Burn-half executor share runtime scratch. First/Second Burn retain distinct reduction identities; `DriverBurn` and numerical modules keep cell mathematics. |
| Domain Hydro preparation and gravity identity | `StageScheduler.h::HydroStagePreparation`, `src/grid/ScalarFieldView.h`, `src/physics/gravity/GravitySolveTypes.h` | Prepare once per actual RK stage after input validation and before block execution. Borrow scalar storage; identify every density input and publish only completed metadata. The gravity Runtime supplies the optional field service; this scheduling/data boundary owns no Poisson/MG mathematics. |
| Gaussian problem envelope | `simulation/GaussianPulse/Gaussian.cpp` | One physical Cartesian distance from `Grid::GetPhysicalCoords`; species and optional pressure/native-velocity amplitudes share the envelope before either backend runs. No coordinate conversion or backend branch in the problem. |
| Independent thin-shell/polar measures (tests only) | `validation/amr/geometry_reference.py`, data and shared test in `tests/math/geometry/CurvilinearMetricCases.h` | CPU metric and CUDA geometry tests; 70/90-digit defining integrals, not sampled production output |
| Physical-state bounds validity | `src/numerics/state/StateAdmissibility.h::valid_bounds` | Shared state acceptance checks finite bounds, nonnegative floors and max>=min before thermodynamic completion; no default or implicit repair. |

| Coordinate layout and measures | `src/numerics/curvilinear/GridGeometryView.h`, `GridMetrics.h` | Cartesian/spherical 1D–3D and cylindrical 1D–3D internal roles, V/A/W/T and lengths. Native axisymmetric geometry and Host transport use the existing owners; optional gravity retains its separate owner; public Cyl2 CPU/CUDA configuration/API/IO uses the new chart identity; Device traversals consume the same mathematics. Physical wall policy remains with boundary/topology owners, not GridMetrics. Spatial derivative count does not determine the physical velocity/AMR indicator count. Grid storage/topology, complete EOS implementations and resource owners remain outside geometry. |
| Physical Hydro wall selection | `BCHandler::hydro_physical_faces`, `HostHydroBoundaryAuthority`, Driver topology preparation | BCHandler derives selected reflecting root faces from authentic Tree/Block identity, excluding coordinate joins. Host keeps its frame/clock checks; native CUDA topology requires an explicit prepared view before state allocation and carries it through three resident slots and regrid. Shared wall flux/source formulas consume the view; no second boundary policy or RZ owner. |

| Selected physical reconstruction | `src/numerics/reconstruction/Reconstruction.h` | Common PCM/MUSCL/PPM traces and their existing limiter contracts. Host and Device bind the same mathematics; patch-stage storage remains with the caller. |
| Geometric momentum source | `src/numerics/curvilinear/GeometricSources.h` | CPU time helpers, `cuda/hydro/kernels/HydroSourceKernels.cuh` |
| Roe-Glaister thermodynamic average | `src/numerics/flux/FluxFunctions.h::calc_glaister_state` | HLL/HLLC/Roe on both backends use the same symmetric fixed-composition pressure secants, derivative limits and enthalpy-consistent acoustic relation. Existing face species output supplies temporary averaged-composition storage; no new allocation or EOS implementation. |
| Face traversal, thermodynamics and conservative assembly | `src/numerics/flux/FluxSweep.h`, `InvariantDomainFlux.h` | Shared selected reconstruction, point flux and conservative limiter. Ordinary mean P/c preparation is immutable-stage work; HLL/HLLC retain Roe–Glaister or Davis bounds. |
| Stationary wall law and selected wall assembly | `src/numerics/flux/StationarySlipWallFlux.h`; `InvariantDomainFlux.h::{selected_wall_point_blend,assemble_wall_point}` | The low leaf owns reflection, exact gamma-law or actual general-EOS pressure traction. The common flux owner owns high/baseline assembly, shared factor and publication checks; there is no inverse Wall-to-Invariant include. Runtime/boundary authority stays upstream. |
| Curvilinear viscous stress and work | `src/numerics/curvilinear/CurvilinearViscousStress.h`, `diffusion/NewtonianViscousStress.h` | One Stokes constitutive law with polar/3D cylindrical frame connections, face traction, geometric terms, work and stability bounds. EOS, coefficients, BC and RKL acceptance keep their owners. |
| HLLC star-region flux | `src/numerics/flux/FluxHLLC.h::calc_star_flux` | One Rankine-Hugoniot state and contact pressure give mass/energy fluxes factored by contact speed. CPU/CUDA use the same expression; the prior subtractive star-state assembly is retired except for its unchanged guarded degenerate limit. |
| Velocity derivatives | `src/physics/diagnostics/VelocityDiagnostics.h` | CPU/device refinement and diffusion |
| External gravity | `src/physics/gravity/GravitySource.h` | CPU `ExternalGravity.h`, CUDA source kernel; same per-stage update |
| Uniform Poisson operator and MG (P2) | `src/numerics/elliptic/CartesianPoisson.h/.cpp`, `src/numerics/multigrid/MGTransfer.h`, `HostMultigrid.h/.cpp` | Backend-neutral stencil/transfer leaves; Host workspace owns levels, V-cycle and bounded existing dense LU. No AMR, EOS, Driver or fluid packing. CUDA leaf compilation is separate from device execution qualification. |
| Standalone density-to-gravity adapter (P2) | `src/physics/gravity/UniformGravity.h/.cpp` | Borrowed Host scalar view, CGS source, periodic mean accounting, compatible face/cell acceleration. No production field publication or self capability enabled. |
| ODE algorithms and continuations | `src/numerics/burnsolver/ode/ode_be-nr.h`, `ode_bd.h`, `ode_ros4.h`, `OdeContinuation.h` | CPU executor and CUDA `SparseOdeBatch.cuh` invoke the same begin/advance/linear-response continuations. The unconsumed `SparseBeNrBatch.cuh` alias header was retired in P1.5. |
| NSE equilibrium, arithmetic and fixed-point certification | `src/physics/nse/nse_solver.h::{NSESolver,nse_detail}` | The existing owner contains conditioned Saha factors, centered compensated moments and shared log/exp/capped-step arithmetic. It reconstructs chemical potentials and checks every Saha log residual plus mass/charge at unchanged tolerances. Certified inputs remain exact fixed points; energy contraction keeps its separate authority. No energy cutoff, EOS substitution or backend physics branch. |
| Generated NSE data and eligibility | `tools/network/NseMetadata.py`, emitted package metadata, `cmake/CustomNetworks.cmake` | Nuclear masses/spins/binding and constant conventions come from the generating pynucastro installation. Exact stoichiometric rank and recognized detailed-balance pairs certify the supported ground-state, unscreened, weak-free model. The shared NSE solver consumes those data; no per-network solver or backend macro family. |
| NSE Auto request | `PolicyDescriptor.h::resolve_nse_request`, existing `RuntimeParams.h` parser | Resolve the host capability request before construction and pass the effective boolean through the unchanged `BurnConfigView`. True and auto share the same configured temperature/density gates and failure-to-ODE path. |
| Composition-derived nuclear energy | `src/physics/network/NuclearEnergy.h` | The existing compensated mass contraction moved intact from `odeFunction.h`, whose `OdeMath` names remain aliases. Generated NSE and accepted ODE increments use that one authority; Timmes NSE retains its original binding-data convention. |
| ROS4 rejected-trial handling | `ode_ros4.h::finish_trial` | Factor and stage-solve failures share quarter-step retry, NSE reset, stall termination and no-commit behavior. Host/CUDA controls in `tests/math/microphysics/BurnThermalCases.h` cover finite provider rejection, actual nonfinite DenseLU rejection and max-substep ordering. |
| Accepted-substep compensated state | `odeFunction.h::OdeMath::AcceptedState`, consuming `core/CompensatedSum.h` | BE_NR/BD/ROS4 share initialization, trial copies and projection-aware commit; rejected trials do not mutate accepted sums. O(NEQ) payload is included in typed workspace sizing |
| Self-heating Jacobian assembly | `src/numerics/burnsolver/coupling/odeFunction.h::assemble_burn_jacobian` | BE_NR/BD/ROS4; EOS cv gradient uses the view's analytic provider or samples that same view, including the denominator chain rule |
| Full burn RHS and thermal closure | `src/numerics/burnsolver/coupling/odeFunction.h::eval_burn_rhs` | All three ODE assemblies, BD midpoint and ROS4 stages; no copied cv division |
| Fixed-density burn first law and EOS derivative adapters | `src/numerics/burnsolver/coupling/BurnThermodynamics.h` | One composition-energy contraction and common numerical fallback; Ideal/Helm/Tabular views supply analytic derivatives. The common Jacobian contracts network columns and the EOS Hessian. No backend branch, extra ODE variable or changed DenseLU boundary. |
| Accepted burn energy | `odeFunction.h::{composition_increment_energy,integrated_burn_increment_energy,record_accepted_energy}` | One compensated network-energy contraction and optional signed weak quadrature consume raw accepted BE_NR/BD/ROS4 increments before rounded state addition/projection. Endpoint helpers delegate to that same contraction. BurnOdeReport carries the scalar result; CPU erasure, dense CUDA and sparse continuations preserve it. Rejected trials contribute nothing; NSE supplies its accepted projection energy. No extra ODE equation. |
| Burn state extent and matrix-size selection | Typed ODE `NEQ`, generated layout metadata, `BurnLimits::equation_count` / `uses_compact_matrix` | CPU erased handle exports the exact extent; CPU/CUDA Auto, dense workspaces and CSR thermal/source coupling count all equations, not only species |
| Single-cell burn preparation/handoff | `src/driver/stages/DriverBurnPolicy.h` | CPU `DriverBurn.h` grid iteration and CUDA cell executors commit the accepted source integral to conserved energy/ENUC/limiter. The final EOS query still owns domain/error validation; subtracting independently reconstructed EOS energies is not a source integral. Invalid handoffs do not commit. |
| EOS view declarations and aliases | `src/physics/eos/eos.h` | CUDA launch declarations; actual owners include complete selected EOS headers |
| Diffusion argument/result records | `src/numerics/diffusion/DiffusionTypes.h` | `DiffFlux.h` owns the sole operators; CUDA config/launch declarations import only the shared records |
| Covariant viscous gradient and divergence | `DiffFlux.h::{ViscousBasisRotation,viscous_basis_rotation,evaluate_geometric_diffusion_cell}` | CPU `FluidState::get` and CUDA `DiffusionStateReader` only supply neighbouring state. Face momentum/work fluxes share the connection; scalar thermal/species math is unchanged. |
| Diffusion face thermodynamics and stability | `DiffFlux.h::{evaluate_diffusion_face_properties,evaluate_diffusion_dt_candidate,viscous_source_stability_rate}`, existing `GridMetrics::InverseRadiusVolumeAverage` | Fluxes and dt share actual face coefficients and rho/cv capacities. CPU/CUDA load current neighbours through their readers; the driver completes backend-local ghosts first. The source reuses the hydro volume measure. The origin-only `raw_geometric_diffusion_dt` was replaced, not retained as another route. |
| Independent viscous spatial acceptance (tests only) | `tests/math/geometry/ViscousGeometryCases.h`, `validation/amr/geometry_reference.py` | Cartesian-vector/radial analytic derivatives and one 30-profile input traversal; CPU/CUDA tests supply only their executor. Archive reuses shared provenance/logging and checks complete refinement coverage. |
| Independent Gaussian initialization / thermal activity (tests only) | `validation/amr/gaussian_reference.py` | Native-coordinate initial-state oracle and same-time/topology on/off thermal control. Uses the existing runtime parameter/lane/provenance helpers, not a second process or checkpoint-comparison infrastructure. |
| CUDA allocation/error boundary | `src/cuda/common/DeviceAllocation.h` | Runtime store and sparse pools share the existing RAII owner; no complete runtime-layout include |
| CUDA EOS failure transport | `src/cuda/common/DeviceEosStatus.h`, `src/cuda/hydro/policies/CheckedHydroEos.cuh` | The backend binds a launch-owned sticky error latch and delegates queries to the original EOS unchanged; it supplies no interpolation, thermal recovery or pressure-floor formula. |
| Runtime metadata transfer accounting | `CudaBackendResources.cpp::enqueue_cuda_metadata_upload` | Same-level/coarse-fine exchange, boundary/flux construction, indicator selection and reflux rebinding. Counts the actual successful enqueue bytes; existing field-region and sparse-provider counters retain their own responsibilities. |
| Whole-regrid timing and transfers | `Driver.h::perform_regrid` wraps the existing `execute_regrid` transaction | One CPU/CUDA wall-time traversal and counter delta, including existing completion/retirement fences; no extra device synchronization. Separate `_regrid.tsv` records overlap nested backend trace operations and must not be added to those totals. |
| Dense/sparse factorization | `src/numerics/linalg/`, `cuda/microphysics/linalg/CuDssSparseSolver.cpp` | KLU CPU / cuDSS CUDA are intentionally separate; native nonsymmetric BTF/COLAMD, original-matrix residual rejection invalidates native analysis through the existing request message; opaque native errors fail closed |
| Sparse structure and value indexing | `src/numerics/linalg/CsrPattern.h`, `CsrMatrixView.h` | Host-only symbolic pattern construction and a backend-neutral duck-typed value view; generated structural writes and the shared burn Jacobian supply entries. The CUDA provider owns its numerical buffers and factorization handles. |
| Sparse equation/unknown equilibration | `src/numerics/linalg/LinearEquilibration.h` | One contiguous/permuted scale/division authority; `cuda/microphysics/linalg/SparseEquilibration.cu` only lowers traversal/launch, `CuDssSparseSolver.cpp` owns device workspaces and immutable column-slot metadata. Original CSR/RHS residual acceptance unchanged |
| Original-system sparse residual | `src/numerics/linalg/SparseResidual.h` | One backward-error gate for `CsrMatrixView` and the provider. Compensated arithmetic improves only the correction RHS; KLU/cuDSS storage, factors and completion remain backend-owned. |
| Multi-block completion contract | `src/driver/runtime/ComputeBackend.h` | Shared input/duplicate validation and ordered fallback. CUDA batches bind views and complete all requested blocks before publication; the shared driver owns stage order and reduction keys. |
| Reusable exchange capacity | `src/amr/exchange/ExchangePlan.h`, `src/cuda/common/DeviceAllocation.h` | Host scratch and device capacity have different storage owners. Reuse does not preserve stale topology views or remove the completion boundary before replacement. |
| EOS free-energy math | `src/physics/eos/tabular/TabularFreeEnergy.h`, EOS views | One cached Horner-basis tensor and constant-background-subtracted compensated contraction serve Host/CUDA. Optional native F/P/S derivative constraints enter the existing field builder; higher derivatives and validity dependencies use the same five-point stencil contract. Host table owners and CUDA owners/error latch supply data/lifetime only; independent polynomial/constant-background controls do not call production bases for expected values. |
| Tabular source metadata and dispatch | `TabularSource.h`, `eosdispatch.cpp` | The lightweight source/component contract supports content-based normalized, EOSDriver-total and baryon-ASCII inspection before rank dispatch. Existing Tabular3D/Tabular4D owners consume it; no new runtime EOS variant or backend macro. Source, interpretation and required auxiliary-table identities enter the existing checkpoint fingerprint. Equilibrium nuclear binding cannot be combined with independent kinetic burn/NSE. |
| Declared tabular coupling eligibility | `EOSDispatcher::validate_coupling`, implemented in `eosdispatch.cpp` | `SolverDispatch` supplies the resolved flux requirement and source metadata before backend construction. Equilibrium sources reject kinetic burn/NSE; declared or equilibrium sources reject composition-only-gamma fluxes and automatic conductivity without electron diagnostics. Positive constant thermal diffusivity remains allowed, and undeclared ordinary tables retain their established behavior. |
| Baryon source-format decoding | `TabularBaryonSource.{h,cpp}` | One finite-temperature 16-column family reader serves EOS2/EOS4 header aliases, preserves native axes/raw samples and converts units/reference conventions. `source::baryon_ascii16` owns fixed mass/reference constants and interpretation metadata for reader and fingerprint consumers. Rounded coordinate mismatches produce source-mask holes, not moved nodes or locally fitted masses. No component physics, interpolation or device ownership belongs here. |
| Missing tabular component assembly | `TabularCompletion.{h,cpp}` | Host import adds only declared missing electron/positron and photon F/derivative terms through the existing Helm component API. Fixed source-to-provider mass conversion, derivative-mask closure and one table-wide energy reference serve existing 3D/4D owners. The original baryon ASCII reader feeds the 3D owner. No EOS-name tuning, duplicate component formula or runtime completion kernel. |
| Strict free-energy temperature inversion | `TabularInversion.h`, consuming the thermal polynomial from `TabularFreeEnergy.h` | Both strict 3D/4D views use one derivative-hierarchy root isolator on the original Hermite potential. Domain, invalid-mask gaps, nonphysical roots, nonunique branches and nonfinite arithmetic fail closed; no sampled surrogate, extrapolation or ideal-gas fallback. Existing CUDA error transport only carries that shared failure. |
| Tabular coordinates and encoded fields | `TabularInterpolation.h`, consuming `Tabular3DEOS.h` | Native EOSDriver P/E interpolation supplies its analytic derivatives and Newtonian acoustic closure; inversion searches the same function and rejects ambiguous branches. This representation is distinct from single-potential completion. Existing CUDA owners upload axes, fields and validity data. Normalized tables without explicit component declarations retain their existing interpretation. |
| Tabular thermal composition derivatives | `Tabular3DEOS.h`, `Tabular4DEOS.h` differentiate their existing interpolation patches; `eos_Utils.h::composition_derivative_query` | One species chain rule in linear composition coordinates, including the Hessian action. 4D maps Abar/Zbar analytically to sum(X/A), Ye; 3D supports selected Xi and Ye. Host/device share formulas; strict/native queries reject unsupported domains rather than taking the ordinary table fallback. Independent manufactured controls remain under the EOS tests. |
| Helm table coordinates and conditioned Hermite contraction | `HelmEos` materializes the documented grid once; `BasicHelmEosView::{locate_axis,hermite_row,interpolate_ele_pos}` | Host/CUDA share identical grid values and the same constant-background-subtracted tensor polynomial. Existing Helm device owner only uploads/owns the two axes. `CompensatedSum` remains the sole accumulator. Independent monomial and actual-table tests remain separate mathematical oracles. |
| Electron/positron and photon thermodynamics | `BasicHelmEosView::{electron_positron_component,photon_component}` | Component queries reuse the maintained electron free-energy interpolation and photon formula, with explicit domains and no ion/Coulomb double count. The host tabular assembler maps a declared fixed baryon mass to the electron provider's number-density convention; photons use the original density. CUDA views expose the same mathematical API, not a second completion implementation. |
| Timmes screening pair factors | `src/physics/network/timmes_common/ScreeningTimmes.h` | Fixed charge/mass factors may constant-fold at literal call sites; state factors and weak/intermediate/strong branches remain shared for scalar/Dual and CPU/CUDA evaluation. |
| Network reaction math | built-in network headers or device-callable generated `.math.h` | CPU adapter + CUDA instantiation; no second generated math body |
| Generated package assembly and route binding | `tools/network/GenerateNetwork.py`, `PortableAdapter.py`, `PortableCxx.py` | The adapter moves numerical definitions into one `.math.h`; the emitted `.cpp` includes it. CMake network discovery and dense/sparse route templates bind registered types, not copied reaction or ODE formulas. Weak data/view specialization uses the separate owners below. |
| Generated complete-RHS temperature difference | `src/numerics/burnsolver/coupling/NetworkDerivative.h` | One generated `CompleteRhs` functor for Host/CUDA; built-in AD derivatives unchanged |
| Generated weak-table coordinate derivatives and corrections | `tools/network/WeakTables.py`, emitted into the same generated table/math headers | Existing `timmes::Dual` differentiates upstream bilinear coordinates; weak rho*Ye and signed loss gradients consume those results; Dense/sparse executors bind that same generated body |
| Immutable weak-table storage and borrowed views | `tools/network/WeakStorage.py`, `src/physics/network/WeakTableView.h` | Moves original data tokens into one Host store; `cuda/microphysics/network/device_network_owner.h` owns the upload, dense backend slot / sparse pool retain it across grid storage changes; no interpolation formulas in the adapter |
| Finite sum/product compensation | `src/core/CompensatedSum.h` | Timmes sums and generated nuclear-energy dot product; explicit FMA is intentional, implicit contraction remains disabled |
| Generated energy mass/conversion data | Emitted `generated/actual_network.H::network::mion` and `fundamental_constants.H::C::enuc_conv2` | One derived `energy_mion` accessor removes a conserved baryon mass offset for RHS/Jacobian/accepted energy; no independent MeV mass reconversion |
| Physical constants and units | `src/physics/constant/PhysicalConstants.h` | Disciplinary SI/CODATA 2022 constants, shared derived values and formula-local aliases; data boundaries in adjacent README |
| Heavy mathematical call annotation | `src/core/ArchPortability.h` (`ARCH_HEAVY_INLINE`) | Helm/Tabular leaves; removes duplicate Tabular3/4 local macros |
| Fixed field CGS unit labels | `src/data/FieldUnits.h` | Configuration API and Plotfile producer share known labels. PlotFieldMetadata.h describes actual plot calculations; HDF serialization consumes producer declarations and never infers fields by name. Unknown ENTR/extra-field units retain reasons. |
| Restart schema, native composition and scientific identity | `src/io/hdf5/HDF5Writer.{h,cpp}`, `src/io/chk/ChkIO.cpp`, `CheckpointCompatibility.cpp` | ARCH checkpoints require scientific identity, ENUC, controller state and native X for active species; X/rhoX consistency and physical compatibility are checked before restoring AMR state. No missing-field reconstruction or CUDA-specific checkpoint format. Common Driver/StateResidency controls materialization and output phase. |
| OS run and executing-image identity | `src/core/files/RunIdentity.{h,cpp}`, `FileFingerprint.{h,cpp}` | One compiled UUID formatter with Linux/Windows entropy providers; one streaming SHA authority. Windows opens the mapped main image with read sharing only and rechecks its mapped name. HDF5 consumes the platform source label; no mathematical or format fork. |
| Studio desktop startup and portable packaging | `studio/desktop/{main.cjs,arguments.mjs,node-runtime.mjs,windows-tools.mjs,package.mjs}`, `cmake/studio` | Main owns native/explicit WSL selection and private tool setup; existing Host owners consume one approved context. One native Linux/Windows packaging flow derives Electron from the locked dependency and bundles standalone Node with its license. No separate Windows reader/writer or scientific implementation. |
| Studio process ownership | `studio/host/hostProcess.ts`, `src/api/resources/WindowsProcess.cpp` | Existing Host runners retain workflow states. Linux uses process groups, including cleanup after leader exit; Windows creates targets in a private Job before resume and waits on a verified parent handle. Run Stop requires controlled Job drain; `windowsRun.ts` supplies only Run-specific terminal/bootstrap and lease arguments. Preview resource policy belongs to WorkerLimits; this process primitive does not translate it. |
| Studio Build provenance and OS publication | `studio/host/{cmakeEvidence,ninjaDependencies,linkDependencies,toolRuntimeEvidence,buildManifest}.ts`, `src/core/files/WindowsBuildPublish.cpp` | Shared BuildRunner and content owner; fixed native GNU/Ninja inputs, ELF v1 / explicit PE v2 evidence, and private pinned-directory publication by source handle. Unknown loader selection stays unknown; BuildRunner alone derives optional observed-input consistency and owns Preview admission. No scientific I/O or second Windows workflow. |
| Preview platform resource policy | `src/api/resources/WorkerLimits.{h,cpp}`, `src/api/session/PreviewSession.cpp`, `studio/src/host/workflowValidation.ts` | Linux retains RLIMIT semantics; Windows owns a nested resource Job for process commit and renewable Job user CPU. One pure metadata predicate serves registry/session consumers. Capability producers share the existing session owner; no supervisor protocol or scientific algorithm is added. |
| Studio config/source/Plotfile OS boundaries | Existing `source.ts`, `atomicConfig.ts`, `plotfileMetadata.ts` and isolated/project adapters | A3–A6 approved exceptions retain original parsers, fingerprints, source evidence and byte limits. `windowsFilePublish.ts` transports opaque native stages, config bytes and read leases; no second HDF/config owner or HTTP tool selection. Only exact Linux/Windows mapped-main-image source labels are accepted. |
| Shared Windows directory identity | `src/core/files/WindowsDirectoryPins.h` | File publication/read leases and Run output leases share root-to-leaf non-reparse pins. Process handles and Job ownership remain with the process owner; config permission preservation and content commits remain with the file/content owners. |
| Validation provenance | `tools/validation_provenance.py` | Matrix, restart, smoke and release aggregators |
| Focused test artifact identity | `validation_provenance.py::capture_focused` and the shared artifact observation helper | Weak / real sparse network runners, configured source/package/library hashes and edit/replace guards; not an ARCH release certificate |
| Runtime checkpoint comparison | `tests/host/io/test_cuda_single_level_validation.cpp` | Same comparator for CPU/CUDA reports; explicit physical-time / step-diagnostic modes must preserve strict restart mode and never change checkpoint data. The manifest/qualifier must require fixed-time acceptance when step timing is diagnostic. |
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
| Stationary slip-wall point flux and selected baseline | `src/numerics/flux/StationarySlipWallFlux.h` | Explicit gamma-law capability selects the original exact reflected-wave pressure; other EOS use actual point pressure as a numerical boundary law. Host and Device traversals share actual high/mean-low data, original two-bar admission and one factor. Required invalid baselines reject; this owner neither authenticates wall identity nor changes CFL, geometry measures or resource lifetimes. |
| Header-layer audit | `tools/audit_architecture.py::audit_header_dependencies` | Reuses one source snapshot; local-cycle and indirect catalogue regression checks |
| Complete Tooling execution and completion (tests only) | `tests/tooling/__main__.py`, `tools/check_ci_results.py::check_tooling_result` | One discovery/no-skip entry for local CTest and hosted Tooling. Sparse-transcript controls live in `tests/tooling/network`; numerical/API and scientific oracles retain their owners. |
| Current documentation links | `tools/check_documentation.py` | Git-visible current Markdown destinations/anchors; dated development, archives and frozen results retain their basis. Unit counterexamples remain in the existing Tooling architecture area; no CTest per page. |
| Required CI LFS assets | `.github/actions/lfs-asset/{action.yml,identity.py}` | One fixed-asset cache/identity flow for the CPU Helm table and Studio Sod reader fixture. The helper never downloads; the action narrows authenticated checkout on a miss, then verifies size/SHA on both paths. Preserve the existing Helm cache key and no local automatic fetch. |
| Independent NSE/EOS acceptance data (tests only) | `validation/network/nse_reference.py`, `validation/eos/helm_reference.py` | Small immutable `tests/fixtures/{Nse,Helm}Reference.h` data; high-precision Saha roots / monomial endpoint fits do not call production math |
| Built-in independent time-integration acceptance (tests only) | `tests/fixtures/burn/BurnTimeReference.h`, `validation/burn/time_reference.py` | Existing `test_burn_mainline_reference.cpp` serves the shared RHS read-only and tests current integrators. DOP853/Radau are independent in TIME, not reaction data. Historical `BurnMainlineReference.h` numbers/checker stay intact; CUDA short-step parity is separate |
| Independent Urca trajectories and evidence (tests only) | `validation/network/weak_reference.py`, `run_weak_validation.py` | Original Suzuki data, independent SciPy interpolation / DOP853 and Radau; archive reuses `validation_provenance` and `run_arch_with_logs`, not another hashing/process infrastructure |
| CUDA diffusion/RKL parity and EOS failure acceptance (tests only) | `tests/cuda/numerics/test_diffusion_rkl_parity.cu`, `test_diffusion_rkl_tabular.cu`, `DiffusionRklTestSupport.cuh` | One executable/CTest and failure counter. Core owns frozen references, common assertions and main; Tabular owns both 3D/4D failure/recovery instantiations; private support owns shared device fixtures once. Shared diffusion header edits rebuild both TUs; no production math or independent oracle duplication. |
| Independent hydro temporal acceptance (tests only) | `validation/hydro/time_reference.py` | Exact Fourier exponential of the PCM contact's upwind semi-discrete operator; actual CPU/CUDA ARCH runs use the existing runtime validators. Separates time error from spatial error without calling a production integrator for expected values. |
| Independent Roe-family flux acceptance (tests only) | `validation/hydro/roe_reference.py`, `tests/fixtures/hydro/RoeFluxReference.h`, `tests/math/RoeThermodynamicCases.h` | A 70/90-digit Euler eigenbasis solve, conservative jump identity and Rankine-Hugoniot relations define immutable ideal-gas fixtures. The existing hydro leaf test checks Host/Device reflection, cyclic rotation and thermodynamic identities; unchanged limiter, reconstruction and SW/VL snapshots remain intact. |
| Independent planar Sedov acceptance (tests only) | `validation/hydro/sedov_reference.py` | Kamm's closed-form similarity profile, converged energy integral and shock-split finite-volume quadrature. Published profile values, independent Euler-similarity DOP853/Radau normalization and swept mass check the oracle; ordinary runtime/provenance helpers own CPU/CUDA execution. No production formula or process infrastructure is duplicated. |
| Real generated sparse trajectory harness (tests only) | `tests/cuda/generated/test_generated_sparse_burn.cpp`, `GeneratedSparseBurnFactory.h` and `test_generated_sparse_burn_factory.cu` in that directory | Host owns inputs/CPU/reference comparisons; thin CUDA TU only binds the selected network to the production factory. Shared IdealGas, ODE and DriverBurn; no copied reaction, EOS or ODE bodies |

## Before adding a helper or file

1. Identify its mathematical/control responsibility in the map above.
2. Search its symbol and equivalent operation in that directory and direct
   consumers. Similar names are not proof of equivalent units/conventions.
3. Prefer reusing or moving the authority and update its current consumers
   directly. Do not retain compatibility aliases or wrappers solely for old
   callers. Never copy a formula under another name.
4. Keep truly independent test oracles independent. Do not make expected values
   call the implementation being tested merely to eliminate apparent duplication.
5. Record moved symbols, remaining aliases, retired implementations and tests
   below. A generated TU must only bind types and call shared bodies.

The tabular split follows these rules: `TabularSource.h` is a lightweight
inspection/interface boundary; `TabularBaryonSource` decodes a source family;
`TabularCompletion` assembles missing components on the host; and
`TabularInversion` is common host/device mathematics. Small file size alone is
not a reason to merge these responsibilities or pull EOS mathematics into a
declaration header. The existing `eosdispatch` remains the selection authority.
Source constants/interpretation and the five-point stencil are shared contracts,
not independently maintained copies in these files.

## Directory and file placement

Before creating or relocating a unit, check its responsibility against this map,
its direct callers, includes/imports and actual CMake or Studio build entry.
Choose the existing owner with the same mathematics, data lifetime or control
boundary. A new directory needs a coherent responsibility; file count, file
length and visual symmetry are not sufficient reasons. Keep related work
together. The user has authorized retiring old case/API/CLI paths and facilities
without backward compatibility; update current consumers, builds, tests and
documentation together. Unsupported inputs or restart artifacts must fail
explicitly. Physical definitions, numerical acceptance and historical evidence
provenance remain required; the existing interface map describes the current
implementation rather than a promise to retain legacy formats or entry points.

| Responsibility | Existing location and boundary |
| --- | --- |
| Shared reconstruction, flux, integration or state mathematics | `src/numerics/{reconstruction,flux,integrator,state}`; CUDA policies bind these implementations |
| EOS view/closure, source decoding or table completion | `src/physics/eos` and its `tabular` / `sources` owners; CUDA allocation/upload stays in `src/cuda/microphysics/eos/owners` |
| Reaction network/NSE physics and ODE coupling | `src/physics/{network,nse}` and `src/numerics/burnsolver/{ode,coupling}`; preserve upstream provenance and shared energy authority |
| AMR topology, transfer, reflux or refinement | Existing `src/amr/{topology,transfer,flux,refinement}` owner; device traversal/migration stays in `src/cuda/{amr,runtime/amr}` |
| Device kernels and execution adapters | The matching `src/cuda/hydro`, `microphysics`, `diffusion` or `runtime` area; no separate copy of shared formulas |
| Stage binding, order or state lifecycle | `src/driver/stages`, `schedule` or `runtime`, according to which contract it owns |
| API implementation or request/response contract | Matching `src/api/{configuration,preview,session,inspection,protocol,resources}` owner; existing public declaration paths remain stable |
| Persistent data and serialization | `src/io/{hdf5,chk,plot}`; driver materialization/order remains in `src/driver/io` |
| New user problem | `simulation/<case>` through `include/UserInterface.h` and `include/GlobalDefs.h`; registry and preparation remain in Core |
| Generated network or CUDA binding | Generator/manifest owner in `tools/network` or `cmake`; emit selected packages to their configured package root and CUDA route TUs to the build tree |
| Test or independent oracle | `tests/{host,cuda,api,tooling}/<area>`; shared fixtures/math under `tests/{fixtures,math}`; scientific references/runners under `validation/<area>` |
| Studio service, renderer contract or desktop packaging | `studio/host`, `studio/src` (including `studio/src/host`) or `studio/desktop`; these have distinct process responsibilities |

For an accepted move, update direct consumers, target/source lists, local links
and the ownership map together. Preserve independent test oracles and historical
evidence. Use the [comment guidance](CommentAndDocumentationStyle.md) to explain
the resulting responsibility and data flow. A path change alone does not justify
a forwarding layer, a second process runner, or a new abstraction.

The [P1 layout record](layout/README.zh-CN.md) and its `moves.json` retain their
2026-09-22 identity. They are historical migration evidence, not a current file
count or authorization gate. The [stage 1 inspection](archive/modernization/ModernizationR1-Stage1Completion-20261012.zh-CN.md)
reconciles later multigrid work and the intentionally retired sparse BE-NR
header without rewriting that record.

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

- Registered option names and free-function `REGISTER_PROBLEM` select the
  maintained implementations. P1.5 retired the `timeintegrator` parameter key;
  text inputs use `time_integrator`.
- EOS initialization wrappers support case setup. Native EOSDriver and strict
  free-energy tables retain their source representations. Normalized HDF5
  requires explicit rank/model/component declarations; direct normalized tables
  and metadata inference were retired in P1.5.
- Generated packages with `generator_version=3` remain CPU-eligible. CUDA
  requires `generator_version >= 4` and device-callable math; these are package
  interface requirements, not separate physical models.
- DenseLU/KLU/cuDSS selection retains the 31-total-equation dense boundary and
  explicit rejection of incompatible backend/solver combinations.

The full contracts are in the [Reference](../Reference.md),
[EOS format guide](../../src/physics/eos/TabularEOS.md) and
[generated-network guide](../../src/physics/network/custom/README.md).

### Verification scope

The active component/raw-source extension on `CUDA_complete_v1` requires its
own final-source complete CPU Release inventory, full Python/tooling controls
and focused CUDA strict-3D/4D mathematics and owner/lifetime checks. A full CUDA
backend rebuild is not part of that bounded gate. The preceding extension's
33 CPU, 307 Python and three focused CUDA passes are not final results for
this later source. Current measurements belong to the existing module summaries
and the [release checkpoint](CudaReleaseStandard.md#progressevidence-rules),
not to a new evidence hierarchy. Passing format or owner tests does not certify
the complete raw-table domain, nonuniform flow or restart continuation.

The following recorded verification describes earlier identified sources:

The [scientific acceptance index](../../validation/backend/results/final-acceptance-20260907/release-73a9cf50/index-final-930/README.md)
records 41 required passes and two owner-deferred large-network workloads for
its identified source. The
[maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md)
separates its interface checks from the referenced full scientific and
source-organization runs. That optimized CUDA build, all 98 configured
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

### Native tables and component completion

Content-based family recognition and explicit component declarations feed the
existing rank/owner dispatch. A total table never receives another electron or
photon contribution. An undeclared normalized table retains its established
meaning; arbitrary field names or an EOS model name are not enough to infer
missing physics. Finite-temperature EOS2/EOS4 share one baryon reader, while
EOSDriver total tables retain their native P/E representation. CompOSE and
zero-temperature/zero-proton companion formats require separate supported
format contracts before they can be advertised.

For baryon ASCII, align source F to the documented E reference, seed
`F_ln(rho)=P/rho` and `F_ln(T)=-T*S`, and add missing components to those same
quantities before building the existing Hermite fields. Entropy preserves
small thermal information that subtracting independently rounded F/E can lose.
The resulting runtime E, derivatives and acoustic state come from that one
potential. Published E remains an independent source-fidelity diagnostic;
finite source F/E/S disagreement is not corrected by fitting a model-specific
constant or weakening a physical acceptance budget.

The fixed source baryon mass gives `rho_helm=rho/(m_b*N_A)` for the electron
provider. Convert its specific free energy and entropy by the same constant
ratio `rho_helm/rho`; pressure is unchanged. Photon queries use the original
rho. Do not use the rounded nodewise nB/rho ratio to define a density-dependent
mass convention. One immutable table-wide energy reference remains outside
the derivative fields, preserving pressure, entropy and heat capacity.

Source-coordinate inconsistencies and component support limits remain holes
on the native axes. The same five-point stencil used for derivative fields
defines their full validity dependencies. Strict queries reject affected cells
and nonphysical states; shared all-root inversion never bridges invalid cells
or chooses an arbitrary valid branch. CPU and CUDA consume the same potential,
axes, mask and energy reference. Host ingestion and device storage are distinct
owners, not distinct thermodynamic models. Independent source fidelity, exact
manufactured closure, backend parity and application behavior must be reported
as separate checks.

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
- Production audit150/audit200 correctness and application timing are recorded
  in the [HPC-CUDA summary](../../validation/backend/results/hpc-cuda-optimization/README.md).
  Further scaling and experimental sparse-provider performance remain separate
  larger-system work, not a repeated local build requirement. Native EOSDriver,
  baryon-source/component completion and qualified generated-NSE extensions
  have their own focused controls; those do not inherit historical release
  qualification. Other native-table families and excitation/weak/screened NSE
  remain separate extensions.

## Constants boundaries

`src/physics/constant/PhysicalConstants.h` owns disciplinary SI/CODATA 2022
definitions and shared derived values. Pi delegates to `<numbers>`; radiation
and Gaussian charge-squared are derived from their fundamentals. Runtime
defaults stay in typed configuration, while algorithm coefficients and
tolerances stay beside their sole numerical owner. The air-like Cv fallback is
an IdealGas model parameter, not a universal physical constant.

Generated network data, reaction fits and Timmes data constants retain their
declared conventions. They are not duplicated backend definitions and must not
be silently rescaled. The EOS2/EOS4 source contract deliberately retains its
fixed rounded baryon mass and printed reference conventions in
`TabularBaryonSource.h::source::baryon_ascii16`; these are not replacements for
the central fundamental constants. Reader and scientific fingerprint consumers
use that one declaration. No automatic conversion between arbitrary ideal-gas code
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

## Source layout and public case headers

The P1 [layout map and include convention](layout/README.zh-CN.md) records the
functional subdirectories. Case sources consume `<UserInterface.h>` and
`<GlobalDefs.h>` through `include/`; internal definitions retain one owner.
Internal includes are relative to `src/`, test helpers to `tests/`. All physics
inputs and outputs follow the existing CGS contract, including IdealGas.

## Composite self gravity (P3–P7)

Geometry and gravity are separate physical responsibilities. `GridMetrics`,
`GeometricSources` and `HydroGeometryBinding` own measures, chart selection and
geometric source terms; none selects an external/self field. The gravity-owned
`HydroSourceBinding.h` owns Host source discovery, actual patch claims, source
work and source receipt completion. Hydro preserves the execution order; AMR's
optional Energy receipt protocol owns no gravitational formula or concrete
frame. The driver still owns field preparation and lifetime. Disabling gravity
leaves the geometric path operational, including EOS/boundary and conservation
checks. Retired per-chart kernels, controllers and memoization remain absent.
Axisymmetric isolated gravity extends the existing `GravityBoundary` tree with
full-ring moments and shared Newton near-leaf math; neither Grid nor Hydro
owns this physical boundary. Its CPU evidence is indexed by the release plan.

The common `TimeIntegratorHelper` deduces its caller-owned source adapter;
`HydroSolverImpl` includes and constructs the concrete binding. Diffusion imports
neither that binding nor a concrete gravity solver. `IHydroSolver` carries only
borrowed declarations, not the configuration loader. `FluidState` forward-declares
its existing transient boundary attachments; their actual users include complete
boundary definitions directly. This narrows headers without moving per-block
ownership, stage aliases, rollback or regrid. The maintained architecture audit
checks these transitive boundaries while permitting the existing lightweight
gravity views/interfaces and the actual composition owners.

| Owner | Consumers and boundary |
| --- | --- |
| `numerics/elliptic/CompositePoisson` | Dyadic leaf geometry, shared coarse/fine subfaces, quadratic interface gradients, volume norms; no fluid state or AMR tree. |
| `numerics/multigrid/CompositeMultigrid` | Shared CPU/CUDA hierarchy and MG/FGMRES flow through `CompositeExecution`; P2 builds a cached coarse inverse. |
| `physics/gravity/GravityBoundary`, `GravityExecution` | Shared isolated boundary moments, axisymmetric full-ring Newton quadrature and physical work descriptors; no CUDA kernel math copy. Native CPU/Device prescribed-field and isolated short checks share this owner; full algorithm/model scientific qualification remains separate. |
| `cuda/runtime/gravity` | Backend allocation, stream/kernel/reduction adapters and generation-stamped patch publication. |
| `amr/elliptic/EllipticMeshAdapter` | Native active-cell order, handles, grid metrics and padded scalar views; no numerical solve. |
| `physics/gravity/self/SelfGravity` | CGS density source, periodic background, potential/force ownership, publication/failure, momentum and face-flux work. Serial preparation, concurrent read-only patch consumption. |
| `driver/stages/GravityStage` | Actual slot/version/epoch leases, one solve per RK input, explicit Current preparation for outputs/CFL, stage invalidation and diagnostics. |
| `io` | Borrowed extra plot fields and v6 gravity checkpoint identity. No implicit Poisson solve. |

`arch_gravity_cpu` owns compiled CPU solver sources once. The Host policy header
uses a private workspace and forward declarations to avoid propagating MG templates
into the hydro dispatch matrix. The same control and math execute through Host or CUDA loop providers.
The target name records compilation by the Host compiler, not a Host field fallback:
CUDA runs keep iteration vectors resident and use the existing stream/allocation owners.

## User boundary ownership

| Responsibility | Authority | Consumers |
| --- | --- | --- |
| Typed callback data / portable registration | `physics/boundary/BoundaryTypes.h`, `UserBoundary.h` | Public two-header case interface, Driver, restart |
| Origin/pole identity | `grid/CoordinateBoundary.h` | Physical ghosts, AMR coordinate seam, composite Poisson |
| EOS ghost and transport-flux rules | `physics/boundary/PhysicalBoundary.h`, `BoundaryFlux.h` | Physical point-law resolution, diffusion capture and shared flux selection. Publication and stage completion remain with boundary manager and Runtime; optional physics owns its own fields. |
| Actual surface observation and quadrature | `BoundaryDiagnostics.h`, `DriverBoundaryDiagnostics.cpp` | Host/CUDA actual face kernels, Driver IO |
| Potential side policy / field exchange | `gravity/self/GravityUserBoundary.*`, `GravityBoundaryDiagnostics.h` | Existing composite operator / gravity execution |
| CUDA storage and gather/scatter | `cuda/runtime/boundary/CudaBackendBoundary.cu` | Surface-only callback snapshots and observer planes |
| Region transfer layout / completion | `CudaBackendResources.cpp::copy_host_device_region`, `ComputeBackend.h::transfer_state_regions` | Disjoint Interior/Ghost cuboids, per-plane species stride and the existing residency/fence transaction |
| Completed user ghost reuse | `PhysicalBoundaryHandler::configure_stage`, `DriverRuntime::complete_device_boundary` | Exact time/purpose snapshot plus existing field version and AMR epoch; failure never publishes a reusable stamp |
| Host surface evaluation | `PhysicalBoundaryHandler::apply_device`, existing `state::HostFailure` | Independent sample outputs and face-control rows may use OpenMP; immutable inputs, fixed scatter order, and joined failure rejection before publication. CUDA calls remain outside the worker loop. |

Regularity is identified geometrically, not by dropping small physical areas.
Source registration, callback names and scientific inputs participate in format-7
restart identity. The O7 configuration/API owner retains required-input semantics;
O8 extends existing options/applicability without creating a parallel config model.
Legacy and mixed-flux gradient descriptors call the same `composite_face_gradient`;
the once-per-level selector removes unused branches without changing its arithmetic.
