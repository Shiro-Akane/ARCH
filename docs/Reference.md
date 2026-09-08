# ARCH Research and API Reference

Chinese translation: [Reference.zh-CN.md](Reference.zh-CN.md). The English file
is the authoritative source text; interface and behavior changes must be made
here first.

This searchable reference follows the declarations and dispatch paths in the
current source tree. The student workflow is in
[`docs/guides/SimulationCase.md`](guides/SimulationCase.md).

## Contents

1. [Authority and stability](#authority-and-stability)
2. [Capability matrix](#capability-matrix)
3. [Runtime architecture](#runtime-architecture)
4. [Accuracy contract and engineering compromises](#accuracy-contract-and-engineering-compromises)
5. [Build, registration, and command line](#build-registration-and-command-line)
6. [Parameter parsing](#parameter-parsing)
7. [Parameter reference](#parameter-reference)
8. [AMR and plot variable vocabulary](#amr-and-plot-variable-vocabulary)
9. [Stable case API](#stable-case-api)
10. [Source-extension interfaces](#source-extension-interfaces)
11. [HDF5 and restart format](#hdf5-and-restart-format)
12. [Known limitations](#known-limitations)
13. [Source map](#source-map)

## Authority and stability

ARCH exposes two interface levels:

- **Stable case API**: the source surface used by files under
  `simulation/<Case>/`. It consists of `UserInterface.h`, `GlobalDefs.h`, the
  `Setup`/`Init` contract, `SpeciesManager`, and the public `ProblemHelper`
  functions. A case includes exactly those two ARCH headers; concrete EOS,
  dispatch, AMR, and driver headers are not part of this surface.
- **Source-extension API**: template or virtual contracts used to add numerical
  and physical policies inside this repository. They follow in-tree source
  compatibility.

Stability labels in this document mean:

| Label | Meaning |
| --- | --- |
| Stable | Maintained for case authors; incompatible changes require migration notes. |
| Source extension | In-tree source compatibility. |
| Internal | Driver/AMR implementation detail outside the case surface. |
| Experimental | Implemented, but validation or interface stabilization is incomplete. |
| Reserved | Parsed or named for future work. |

ARCH builds one executable with internal, functionally split object targets.
Its extension contracts operate at source level.

Scientific provenance is recorded separately from interface stability. The
reaction networks, NSE formulation, and Helmholtz EOS trace to Frank Timmes;
the stellar-conductivity mathematics traces immediately to AMReX-Astro
Microphysics. File-level boundaries and retained terms are listed in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). Other modules have no
external provenance claim unless their file header or that notice says so.

## Capability matrix

### Execution and mesh

| Feature | Accepted value or interface | Current status | Notes |
| --- | --- | --- | --- |
| Host execution | `compute_backend = cpu` | supported | OpenMP is configured at build time. |
| CUDA execution | `compute_backend = cuda/auto` | supported | Built with `ARCH_ENABLE_CUDA=ON`; explicit CUDA is fail-closed and `auto` may fall back only before construction. |
| Dimension | positive `nblockx1`; zero trailing block counts | supported | `nblockx2=0,nblockx3=0` is 1D; `nblockx3=0` is 2D. |
| Geometry | `cartesian`, `cylindrical`, `spherical` | supported on CPU and CUDA | Names are case-insensitive and stored canonically. Both backends share physical cell volumes, face areas, CFL lengths, diffusion spacing and geometric source terms. |
| AMR | `lrefinemax >= 0` | supported on CPU and CUDA | Fixed 16-cell block extent per active dimension. Topology/Morton decisions remain on the Host; indicators, conservative migration, ghosts, and reflux execute on the device using shared numerical leaves. |
| Self gravity | `gravity_type = self` | unavailable | The capability gate rejects it before policy construction. |
| Jeans field | `JENS` | reserved | Parser warns and disables it. |

CUDA implements Cartesian/cylindrical/spherical 1D/2D/3D hydro, the registered
flux/reconstruction/time-integrator matrix, Ideal/Helmholtz/Tabular3D/Tabular4D
EOS, and RKL1/RKL2 diffusion through the common geometry definitions. Two-dimensional
spherical grids use ARCH's polar `(r,phi)` convention. Runtime
species scratch removes the 30-species storage ceiling for passive transport
and AMR; DenseLU independently remains limited to 31 total ODE equations. Dynamic
AMR uses Host topology plans and device numerical indicators/migration, staged
device-store transactions, multiblock exchange, and compact Hydro/RKL reflux.
Restart uses the shared Host checkpoint schema, and output uses the shared
host writer after explicit materialization.

The four built-in burn networks support DenseLU and optional cuDSS solving and
retain NSE. Version-4 generated packages declaring `device_callable_math=true`
use the same math on CPU and CUDA. Recognized embedded weak tables have separate
read-only storage on each backend. Version-3 packages and packages not converted
for device execution remain CPU-only. cuDSS requires its optional library to be
linked; KLU is CPU-only, and incompatible explicit backend/solver choices are
rejected. External gravity uses
one per-stage source operator on both backends. The
[backend guide](CudaBackendStatus.md) explains execution responsibilities;
the [validation index](../validation/README.md) records numerical checks,
application results and release acceptance.

### Numerical policies

| Category | Runtime values | Status and exact behavior |
| --- | --- | --- |
| Flux | `SW`, `VL`, `Roe`, `HLL`, `HLLC` | dispatched |
| Reconstruction | `pcm`, `donor_cell`; `muscl`, `plm`; `ppm` | dispatched aliases shown; `weno5` appears only in ghost-count code |
| MUSCL limiter | `minmod`, `superbee`, `vanleer`, `mc` | dispatched; unknown values, including `none`, fall back to MinMod |
| Hydro time | `Euler`, `RK1`; `RK2`, `SSPRK2`; `RK3`, `SSPRK3` | Euler, SSPRK2, SSPRK3 |
| Diffusion time | `RKL2` (default), `RKL1` | RKL2 is second order for the isolated diffusion operator; RKL1 is the optional first-order variant |
| EOS | `ideal`, `tabular`, `helmholtz` | dispatched on CPU and CUDA |
| Gravity | `none`, `external` | shared CPU/CUDA stage source; unknown strings and `self` are rejected before construction |
| Network | `aprox13`, `aprox19`, `aprox21`, `iso7`; `custom:<id>` | built-ins plus generated custom packages discovered by CMake |
| Burn ODE | `BE_NR`, `ROS4`, `BD` | all dispatched and covered by the one-zone CPU regression |
| Linear solve | `Auto`, `DenseLU`, `SparseKLU`, `cuDSS` | Case-insensitive; aliases `dense_lu`, `sparse_klu`, and `cu_dss` are accepted. `Auto` selects DenseLU for up to 31 total ODE equations, including temperature and any auxiliary energy states. Larger systems use SparseKLU on CPU or cuDSS on CUDA. SparseKLU is CPU-only, cuDSS is CUDA-only, and incompatible explicit pairs are rejected before backend construction without solver substitution. Missing solver libraries or registered CUDA network code also cause rejection. |

Policy names are ASCII case-insensitive, but accepted aliases and fallback
behavior still vary by dispatcher.

## Runtime architecture

The executable follows this lifecycle:

```text
main(argc, argv)
  -> RuntimeParams::Load(.par)
  -> ProblemRegistry::Create(problem name)
  -> case.Setup(config, species)
  -> DispatchSolver
       -> resolve registered execution plan and runtime requirements
       -> probe build/device and query CPU/CUDA capability gates
       -> resolve backend (the only `auto` fallback boundary)
       -> allocate AMRControl/MemoryPool
       -> initialize or restart leaf state
       -> dispatch EOS, burn handle, gravity, time integrator, flux, reconstruction
       -> run_simulation
            -> regrid and IO scheduling
            -> choose hydro/diffusion/burn time restrictions
            -> B(dt/2) D(dt/2) H(dt) D(dt/2) B(dt/2)
            -> advance time
```

The multidimensional hydro RHS accumulates every active-direction face
divergence before an RK stage update. Geometric and external-gravity sources
share the hydro stage evaluation.

AMR owns topology, block memory, ghost exchange, and flux registers. Hydro and
multi-block diffusion register coarse/fine fluxes and apply reflux after their
respective composite updates.

## Accuracy contract and engineering compromises

Order reporting separates component methods from the coupled calculation.

### Temporal composition

The current driver uses the symmetric composition

```text
B(dt/2) -> D(dt/2) -> H(dt) -> D(dt/2) -> B(dt/2)
```

where `B`, `D`, and `H` denote burning, diffusion, and hydro including gravity
and geometric sources. The composition gives:

- symmetric Strang composition limits the coupled problem to at most second
  order, even when the hydro substep uses SSPRK3;
- the default RKL2 substep is second order for isolated diffusion; the complete
  split method remains at most second order (optional RKL1 is first order);
- the adaptive stiff burn solver and any NSE projection must be verified
  separately before a second-order coupled convergence claim is made;
- regridding events, solution-dependent limiters, shocks, and safeguard activation
  can prevent the asymptotic order from appearing.

### Spatial reconstruction and AMR

- PCM is nominally first order.
- MUSCL/PLM is nominally second order in smooth regions before limiter activation.
- PPM provides nominally third-order reconstruction; coupled temporal order
  follows the Strang composition.
- In the uniform-grid smooth-advection baseline, PPM's final-pair L1 rate is
  3.993 and passes the 2.7 acceptance threshold. This smooth-contact result is
  not a general fourth-order claim.
- At a 2:1 coarse/fine interface, ARCH uses second-order MUSCL-MinMod from
  `AMRInterfaceReconstruction.h` for the face reconstruction.
- Shock convergence uses norms and shock-problem rates; discontinuities reduce
  local order.

### Safeguards that alter conservation

`perform_stage_update` applies robustness repairs after a hydro stage:

- density below `sml_rho` is reset, momenta are zeroed, and energy is rebuilt;
- velocity magnitude is capped by a hard-coded `1e10` ceiling;
- specific internal energy is clamped to `[min_eint, max_eint]`;
- negative species fractions are clipped and all fractions are renormalized;
- if the species sum is nearly zero, a uniform composition is installed.

These engineering safeguards modify state outside conservative flux updates.
Runs record their repair contribution alongside conservation and L1/L2 metrics.

The production PPM path reconstructs density, velocity, pressure, and species,
then calls the selected EOS to rebuild total energy. It floors density and
pressure, bounds species to `[0,1]`, and renormalizes interface compositions.

### Build reproducibility and compile-time compromise

Release compilation uses optimization and `-march=native`, but the shared
build contract explicitly disables fast-math and contraction for supported
GNU/Clang host compilers (`-fno-fast-math -ffp-contract=off`) and NVIDIA CUDA
(`--fmad=false --ftz=false --prec-div=true --prec-sqrt=true`). Host link options
also prevent fast-math startup state. This preserves the intended compensated
sums and frozen arithmetic policy, not cross-machine bitwise reproducibility.
Validation records include compiler, flags, OpenMP thread count, hardware, and
numerical tolerances.

The CPU dispatch translation units use `-O1` and
`-fno-inline-functions-called-once` to limit compiler memory for the
integrator, flux, reconstruction and EOS combinations. Release LTO remains
enabled when the toolchain supports it. Burner policies are type-erased behind
`BurnerHandle` before the full flux matrix, limiting repeated network/ODE
instantiation across hydro routes. CUDA splits follow the same functional
boundaries, with separate backend compile pools.

## Build, registration, and command line

### Build contract

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 1
```

CMake fetches HighFive during configuration and links HDF5 C++/HL libraries.
KLU is enabled by default: CMake first looks for an installed KLU package and,
if absent, fetches pinned SuiteSparse v7.13.0 and builds only KLU, BTF, AMD,
COLAMD, and SuiteSparse_config. EOS `.dat`/`.h5` assets may require Git LFS.

CUDA sparse burning additionally uses optional cuDSS discovery with
`ARCH_ENABLE_CUDSS=ON` (default) and `CUDSS_ROOT` for an installed prefix; a
user-local installation is supported. The provider requires the reviewed 0.8
API and a matching runtime version. It is advertised only after its registered
network/EOS routes and library are linked. Missing cuDSS does not prevent a
dense-only CUDA build, but sparse requests are rejected. See the
[README build example](../README.md#build) for isolated executable output and
`tools/run_memory_guarded.py`. Start with `--parallel 1` when available memory
is uncertain, then choose the heavy-pool and total-job limits from measurements.
The guard allows bounded swap growth and stops sustained memory or I/O stalls.
Simulation capacity is measured separately for the selected workload.

`ARCH_CUDSS_IR_STEPS` is a nonnegative integer CMake setting (default `2`)
for cuDSS's device-side iterative-refinement passes. `0` disables them for
diagnostics. It changes only the linear provider, not the ODE tolerances or
the Auto cutoff: at most 31 total equations use DenseLU. Thirty isotopes plus
temperature and a weak-energy integral already form a 32-equation system.
The provider keeps cuDSS `IR_TOL=0` and applies ARCH's unchanged original-matrix
componentwise residual gate after the solve; a successful library status alone
does not prove an accurate correction. This setting is private to the Host
provider compilation and does not reinstantiate CUDA network/EOS kernels.
See the [cuDSS refinement contract](https://docs.nvidia.com/cuda/cudss/types.html#cudssconfigparam-t).

Relevant cache controls are `ARCH_ENABLE_KLU` (default `ON`),
`ARCH_FETCH_SUITESPARSE` (default `ON`), `ARCH_CUSTOM_NETWORK_ROOT` (the
generated-package root), and `ARCH_CUSTOM_NETWORKS` (an optional semicolon list
of custom IDs to compile). `BUILD_TESTING=ON` registers the maintained
ideal-gas tabular EOS and 161-equation KLU regressions. Restart, AMR, real-table,
and generated-network audit evidence is retained together under `validation/`
without adding per-audit test targets.

Sources are found with CMake `GLOB_RECURSE` over `src/core`, `src/physics`,
`src/numerics`, `src/io`, and `simulation`, excluding the generated custom
network subtree handled by its registry. The glob uses `CONFIGURE_DEPENDS`, so
the build regenerates when a `.cpp` is added; an explicit CMake configure is
also valid.

### Case registration

The maintained registration macro is:

```cpp
REGISTER_PROBLEM_CLASS("RuntimeName", CaseClass);
```

It registers a factory during static initialization. Duplicate names overwrite
the existing entry. The case class must be default
constructible. A free-function compatibility macro also exists:

```cpp
REGISTER_PROBLEM("RuntimeName", setup_function, init_function);
```

### Command line

```text
./bin/ARCH <ProblemType> <ParFile>
```

The executable accepts these two positional arguments. List, dry-run,
parameter-override, schema-dump, and validation subcommands are unavailable.

## Parameter parsing

`ConfigParser` reads the first `=` on each non-empty line, removes text after
`#`, trims whitespace, and stores the last value for a duplicate key. Keys are
case-sensitive.

Important behavior:

- integer and double core values use `std::stoi`/`std::stod`;
- Boolean values accept only `true` or `false`, case-insensitively; numeric
  `0`/`1` and `on`/`off` are rejected;
- geometry, boundary, gravity, and compute-backend tokens are normalized to
  ASCII lowercase once during parameter loading;
- unknown keys are retained in `SimConfig::custom_params` or
  `custom_string_params`; spelling validation is unavailable;
- `SimConfig::Get<T>` returns its caller-supplied default when a custom key is
  absent;
- the lightweight `pi` expression parser serves domain bounds and external
  gravity components, with forms such as `pi`, `-pi`, `2*pi`, `pi*2`, and
  `pi/2`;
- paths are interpreted relative to the process working directory;
- quotes are stripped from `eos_table_path` by EOS dispatch, but general strings
  retain parser text.

Fallback policy is inconsistent by design today:

| Invalid choice | Current behavior |
| --- | --- |
| flux | warning, HLLC |
| reconstruction | warning, PCM |
| MUSCL limiter | warning, MinMod |
| hydro integrator | warning, SSPRK2 |
| gravity | exception during resolved-requirement construction |
| EOS, network, ODE, linear solver | exception |
| diffusion integrator | fatal/exception depending path |

Always inspect the startup “Strategy” line and treat fallback warnings as a
failed configuration in research workflows.

## Parameter reference

Defaults below are the values used by `RuntimeParams::Load`, which takes
precedence over default member initializers in `GlobalDefs.h`.

### Grid and geometry

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `geometry` | string | `cartesian` | `cartesian`, `cylindrical`, `spherical` |
| `nblockx1` | int | `1` | positive root blocks |
| `nblockx2` | int | `1` | `<=0` removes axis 2 |
| `nblockx3` | int | `1` | `<=0` removes axis 3; axis 3 requires active axis 2 |
| `max_blocks` | int | `2000` | strict AMR memory-pool capacity |
| `x1_min/max` | expression | `0/1` | active axis must have max > min |
| `x2_min/max` | expression | `0/1` | angular restrictions depend on geometry/dimension |
| `x3_min/max` | expression | `0/1` | angular restrictions depend on geometry/dimension |
| `x1l_boundary_type` | string | `outflow` | `outflow`, `reflect`, `periodic` |
| `x1r_boundary_type` | string | `outflow` | same |
| `x2l_boundary_type` | string | `outflow` | same |
| `x2r_boundary_type` | string | `outflow` | same |
| `x3l_boundary_type` | string | `outflow` | same |
| `x3r_boundary_type` | string | `outflow` | same |

Logical coordinate meanings are:

| Geometry | 1D | 2D | 3D |
| --- | --- | --- | --- |
| Cartesian | x | x, y | x, y, z |
| Spherical | r | r, phi | r, theta, phi |
| Cylindrical | r | r, phi | r, z, phi |

For converted `PointCoords`, the origin is fixed at `(0,0,0)`.

Every `bool` parameter accepts `true` or `false` case-insensitively (for example,
`TRUE`, `False`, and `tRuE`). Numeric `0/1`, `on/off`, `yes/no`, partial matches,
and any other spelling are rejected with the parameter name in the error.

### Hydro numerics and execution

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `solver` | string | `SW` | `SW`, `VL`, `Roe`, `HLL`, `HLLC` |
| `reconstruct` | string | `pcm` | `pcm`, `donor_cell`, `muscl`, `plm`, `ppm` |
| `limiter` | string | `minmod` | MUSCL only: `minmod`, `superbee`, `vanleer`, `mc` |
| `time_integrator` | string | `RK2` | `Euler/RK1`, `RK2/SSPRK2`, `RK3/SSPRK3` |
| `timeintegrator` | string | — | legacy fallback key when canonical key is absent |
| `cfl` | double | `0.8` | explicit hydro CFL; range unchecked at load time |
| `EntropyFix` | bool | `true` | enables entropy-fix smoothing |
| `EntropyFixCoefficient` | double | `0.1` | used when entropy fix is enabled |
| `sml_rho` | double | `1e-12` | density repair threshold |
| `min_eint` | double | `1e-10` | positive specific internal-energy floor |
| `max_eint` | double | `1e21` | specific internal-energy ceiling |
| `compute_backend` | string | `cpu` | `cpu`, `cuda`, or `auto`; explicit CUDA is fail-closed and `auto` may fall back only before construction |
| `cuda_device` | int | `0` | CUDA runtime device ordinal used by probing, construction, and lifecycle operations |

### AMR

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `lrefinemin` | int | `0` | stored; current hierarchy behavior should be verified before relying on a nonzero minimum |
| `lrefinemax` | int | `0` | maximum refinement level; zero disables refinement |
| `regrid_interval` | int | `2` | must be positive |
| `refine_var` | string list | `DENS` | comma or `+`; canonical fields or registered species |
| `refine_threshold` | double | `0.8` | Lohner indicator, `[0,1]` |
| `derefine_threshold` | double | `0.2` | must be `>=0` and less than refine threshold |

`refine_var` is validated even when `lrefinemax = 0`.

### EOS and gravity

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `eos_type` | string | `ideal` | `ideal`, `tabular`, `helmholtz` |
| `eos_table_path` | string | empty | required for tabular/Helmholtz |
| `gamma` | double | `1.4` | ideal-gas fallback/reference gamma |
| `gravity_type` | string | `none` | `none`, `external`; `self` is rejected by the capability gate before construction |
| `gravity_g_x/y/z` | expression | `0` | used for external gravity |
| `gravity_G` | expression | `6.6743e-8` | parsed only for unsupported self gravity |

For `eos_type = tabular`, the HDF5 file declares `table_rank = 3` or `4` and
dispatch selects the matching policy automatically. New tables should store
specific Helmholtz free energy; the normalized datasets, derivative identities,
legacy direct-table path, and measured guard-node/endpoint spacing rules are specified
in the local [Tabular EOS HDF5 interface](../src/physics/eos/TabularEOS.md).
Shen/LS/HS/CompOSE/EOSDriver files require a family-specific converter; none is
currently bundled. Binary compatibility is defined by the normalized schema,
not by an upstream filename or HDF5 container. The acquired Shen EOS4 and
EOSDriver HShen assets are assessed, but not accepted as directly loadable, in
the [EOS validation record](../validation/eos/README.md).

The maintained Helmholtz validation asset is the `helm_table.dat` member of the
`helmholtz.tar.xz` archive downloaded from the
[Timmes EOS page](https://cococubed.com/code_pages/eos.shtml). It is materialized
at `EOS_toolkit/tables/helmholtz/helm_table.dat` through Git LFS and has
size 60,242,514 bytes and
SHA-256
`c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1`.
The original table member is the validation authority. The loader uses the
fixed 541×201 Timmes layout and requires all four data blocks; the burn baseline
also requires the exact checksum above.

### Burning, network, and ODE

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `use_burn` | bool | `false` | enables the burn module |
| `network_name` | string | `aprox19` | built-ins above or any compiled `custom:<id>`; generated networks require `use_nse = false` |
| `nuclearTempMin` | double | `1e9` | K; burn activation threshold |
| `nuclearDensMin` | double | `1e-10` | g/cm3; burn activation threshold |
| `smallt` | double | `1e5` | K; burn state floor |
| `smallx` | double | `1e-20` | composition floor |
| `enucDtFactor` | double | `1e30` | energy-release time-step limiter; huge default is effectively off |
| `use_nse` | bool | `true` | enables thresholded NSE projection for the four built-in networks only |
| `nseTempThreshold` | double | `4.5e9` | K |
| `nseDensThreshold` | double | `1e6` | g/cm3 |
| `enforce_mass_conservation` | bool | `true` | parsed and stored; no active burn path currently consumes this switch |
| `burn_verbose_level` | int | `0` | parsed and stored; no active burn path currently consumes this level |
| `ode_solver` | string | `BE_NR` | `BE_NR`, `ROS4`, or `BD` |
| `linear_solver` | string | `Auto` | Case-insensitive `Auto`, `DenseLU`, `SparseKLU`, or `cuDSS` (`dense_lu`, `sparse_klu`, `cu_dss` aliases accepted); see backend-dependent materialization below |
| `ode_rtol` | double | `1e-4` | relative ODE tolerance |
| `ode_atol` | double | `1e-8` | absolute ODE tolerance |
| `ode_max_newton_iter` | int | `50` | Newton limit where used |
| `ode_max_substeps` | int | `10000` | adaptive substep limit |
| `ode_dt_safe_fac` | double | `0.9` | adaptive controller safety factor |
| `ode_dt_fac_max` | double | `2.0` | growth factor |
| `ode_dt_fac_min` | double | `0.1` | shrink factor |
| `ode_initial_dt_frac` | double | `1e-3` | initial internal substep fraction |
| `ode_use_numerical_jac` | bool | `false` | stored; verify solver-specific use before relying on it |
| `ode_freeze_jacobian` | bool | `false` | stored; verify solver-specific use before relying on it |
| `dt_init` | custom double | `1e-16` | first macro step when burn is enabled |
| `dt_min` | custom double | `1e-20` | abort threshold for macro step |
| `tstep_change_factor` | custom double | `1.2` | maximum macro-step growth after first step |

`ROS4` uses a matched four-stage, fourth-order, L-stable tableau. Each internal
step evaluates one Jacobian, factors `I - gamma*dt*J` once, and reuses the
factors for all stages. Its aprox13/Helmholtz one-zone result passes the current
BE_NR cross-solver tolerance. Production studies still report a
substep/tolerance convergence series and compare species and energy histories,
especially when extending the network or EOS coupling.

The current correctness revision separates BE_NR's nonlinear convergence from
time accuracy: the Newton correction must fit one tenth of the ODE error scale,
then a backward-Euler/trapezoidal endpoint difference estimates the second-order
local error. The accepted solution remains first-order backward Euler. Tightening
`ode_rtol`/`ode_atol` now controls that local estimate, not merely the linear/Newton
solve; it is not a promised global relative-error bound. All three ODEs share
the fixed-density first-law RHS and Jacobian assembly, including composition-
dependent EOS energy and heat-capacity derivatives. The equations and energy
handoff are described in the [network technical note](physics/TimmesNetworks.md#4-temperature-equation-jacobian-and-lhs-conventions).
Independent time/energy checks are recorded in [burn validation](../validation/burn/README.md).

The last three are custom-map controls.
Network-specific initial fractions such as `xc12` are consumed by the selected
network setup implementation.

### Diffusion

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `use_diffusion` | bool | `false` | enables the diffusion module |
| `diff_integrator` | string | `RKL2` | `RKL1` or `RKL2` |
| `diff_cfl` | double | `0.8` | fraction used in RKL stage/step selection |
| `diff_max_stages` | int | `256` | caps the STS polynomial and macro step |
| `use_thermal_diff` | bool | `false` | thermal conduction |
| `use_viscous_diff` | bool | `false` | momentum diffusion |
| `use_species_diff` | bool | `false` | composition diffusion |
| `nu_visc` | double | `0` | constant non-Helm kinematic viscosity |
| `alpha_therm` | double | `0` | constant non-Helm thermal diffusivity |
| `D_spec` | double | `0` | constant non-Helm species diffusivity |

With Helmholtz diffusion, omit all three constant override keys to select
`diffusionCoe` transport. Presence of an override key is rejected, including a
zero value.

### Time, output, and restart

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `tmax` | double | `0.1` | target physical time |
| `max_steps` | int | `-1` | positive value enables step stop |
| `out_dir` | string | `data` | created before logging |
| `base_name` | string | `arch` | output filename prefix |
| `plt_dt` | double | `-1` | positive physical-time interval |
| `plt_dstep` | int | `-1` | positive step interval |
| `chk_dt` | double | `-1` | positive physical-time interval |
| `chk_dstep` | int | `-1` | positive step interval |
| `plt_variables` | string list | `ALL` | comma or `+`, canonical fields/species |
| `restart` | bool | `false` | enables checkpoint restart |
| `restart_file` | string | empty | must be non-empty when `restart = true` |

At step zero, ARCH writes an initial PLT and CHK. Reaching target time forces
final output; a `max_steps` stop follows the configured output schedule.

## AMR and plot variable vocabulary

Canonical names are case-insensitive after tokenization. Rejected short aliases
include `rho`, `p`, `u`, `v`, `w`, and `eng`.

| Name | Meaning | AMR | PLT | Availability |
| --- | --- | --- | --- | --- |
| `DENS` | mass density | yes | yes | all models |
| `PRES` | pressure from active EOS | yes | yes | all EOS |
| `TEMP` | temperature from active EOS | yes | yes | all EOS |
| `VELX/Y/Z` | velocity along active logical directions | yes | yes | active dimensions only |
| `ENER` | total energy density | yes | yes | all models |
| `VORT` | metric-aware curl magnitude | yes | yes | implemented geometries |
| `DIVV` | metric-aware velocity divergence | yes | yes | implemented geometries |
| `ENTR` | local `p/rho^Gamma1` proxy | yes | yes | finite positive EOS state |
| `ENUC` | signed nuclear specific-energy source rate | yes | yes | burning enabled |
| `JENS` | Jeans criterion | reserved | reserved | disabled |
| `SPECIES` | every registered species | yes | yes | registered composition |
| registered name | one species/tracer | yes | yes | case-insensitive lookup |

`CONSERVED` selects `DENS`, active velocities, and `ENER`. `ALL` selects every
available PLT field and registered species.

`ENTR` is the local proxy `p/rho^Gamma1`, with
`Gamma1 = rho*c_s^2/p` from the active EOS. It is the usual constant-gamma
ideal-gas invariant and a refinement proxy for general EOS policies.
It is not an absolute entropy returned by the EOS and must not be used to move
a general state along an isentrope. Fixed-composition isentropic state
construction uses the differential EOS identity documented under the EOS
policy interface.

## Stable case API

### Include surface and class contract

For `simulation/<Case>/<Case>.cpp`:

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
```

These are the only ARCH headers a case may include. C++ standard-library
headers remain unrestricted. EOS operations exposed to a case go through
`ProblemHelper`, so selecting a different runtime EOS never changes case
includes or introduces a concrete EOS policy type.

The relative path assumes the maintained two-level case layout. A case class is
wrapped by `TypedProblemGenerator<T>` and must be default constructible:

```cpp
void Setup(SimConfig &config, SpeciesManager &specs);
void Init(const PointCoords &point, PrimitiveData &out) const;
```

`Setup` runs once before allocation. `Init` populates the allocated root blocks,
including ghost storage, through an OpenMP loop. Initial and later fine blocks
are created by conservative AMR transfer; `Init` is not called again to
overwrite them. Keep it deterministic and thread-safe.

### `SimConfig`

```cpp
template <typename T>
T SimConfig::Get(const std::string &key, T default_value) const;

double SimConfig::GetCustomParam(
    const std::string &key, double default_value) const;
```

`T` is practically a numeric type convertible from stored `double`, or
`std::string`. Core values already copied into `grid`, `numerics`, `execution`,
`physics`, `amr`, and `io` should be read from those typed structures.

### `SpeciesManager`

Maintained case-facing operations are:

```cpp
int add_species(std::string name, double A, double Z, double gamma, double Cv);
int GetSpeciesID(const std::string &name) const;
int count() const;
std::string get_name(int id) const;
double get_A(int id) const;
double get_Z(int id) const;
double get_gamma_ref(int id) const;
double get_Cv_ref(int id) const;
```

IDs are registration-order indices. `GetSpeciesID` is case-insensitive and
returns `-1` when absent. Property getters use unchecked IDs.

### `PointCoords`

```cpp
struct PointCoords {
    double x, y, z;
    double r, theta, phi;
    double r_cy, phi_cy, z_cy;
};
```

`Grid::GetPhysicalCoords` populates every representation. In 2D spherical and
cylindrical geometry, the second logical coordinate is planar azimuth `phi`.

### `PrimitiveData`

```cpp
struct PrimitiveData {
    double rho, u, v, w, p;
    double temperature;
    bool has_temperature;
    std::vector<double> mass_fractions;
    void SetMassFraction(int id, double value);
    void SetTemperature(double value);
};
```

The framework resets the values before each `Init` call and pre-sizes the
composition vector to the registered species count. `SetMassFraction` can resize
for a larger ID and requires `id >= 0`. Conservative momenta and energy are
derived after `Init` through the selected EOS. `SetTemperature` selects
temperature-based internal-energy initialization and takes precedence over
pressure for energy construction; otherwise `rho` and `p` define the energy.
The framework resets `has_temperature` before every `Init` call.

### `ProblemHelper`

Public functions are:

```cpp
void ProblemHelper::SetupNetworkAndFractions(
    SimConfig &config,
    SpeciesManager &specs,
    std::vector<double> &default_X);

double ProblemHelper::GetPressureFromRhoT(
    const SimConfig &config,
    const SpeciesManager &specs,
    double rho,
    double temperature,
    const double *mass_fractions);

double ProblemHelper::GetRootCellWidth(
    const SimConfig &config,
    int logical_axis);

struct ProblemHelper::IsentropicState {
    double rho;
    double temperature;
    double pressure;
    double sound_speed;
};

ProblemHelper::IsentropicState
ProblemHelper::GetIsentropicStateAtPressureFactor(
    const SimConfig &config,
    const SpeciesManager &specs,
    double reference_rho,
    double reference_temperature,
    const double *mass_fractions,
    double pressure_factor);
```

Call network setup before manually registering other species. It calls a
network’s `RegisterSpecies` when the manager is empty. Pressure conversion
performs runtime EOS dispatch and belongs in `Setup`.

`GetRootCellWidth` returns the physical width of one active root-level cell on
the one-based logical axis `1`, `2`, or `3`. It uses the configured root-block
count and the compiled active block extent. In the current layout, each block
has `16` active cells per direction plus four guard cells on each side
(`16 + 8` stored cells along x); the eight guard cells are not part of the
physical domain width. Use this function when a case needs cell-average
initialization instead of including the internal `AmrDefines.h` header.

The isentropic helper keeps `mass_fractions` fixed, interprets
`pressure_factor` as `P_target/P_reference`, and returns the matched density,
temperature, pressure, and sound speed. It uses the active EOS and the common
EOS-policy path described below; it does not assume an ideal gas. The operation
is intentionally local and rejects a solution farther than `0.25` in
`ln(rho)` from the reference. Both EOS helpers are setup-time operations, not
per-cell `Init` or timestep-loop calls.

`ProblemHelper::detail::PopulateState` is an internal initialization bridge.

## Source-extension interfaces

All interfaces below require source changes, registration changes, a complete
rebuild, and new validation evidence.

### Flux policy — Source extension

Each runtime flux is a template on a reconstruction policy and supplies:

```cpp
static std::string name();
static constexpr int NG;

template <typename EosType>
static void compute_fluxes(
    const FluidState &state,
    const EosType &eos,
    const Grid &grid,
    std::vector<FluidVector> &flux_out,
    std::vector<double> &species_flux_out,
    int direction,
    double entropy_fix_coefficient = 0.0);
```

Register a new template in `DispatchImpl::select_flux`. Flux arrays use face
indices compatible with `TimeIntegration::accumulate_divergence` and AMR flux
registration; species flux is flattened as `species * total_size + face_index`.

### Reconstruction policy — Source extension

The concrete policies provide:

```cpp
static std::string name();
static constexpr int NG;
static std::pair<FluidVector, FluidVector>
run(const FluidState &state, int left_cell_index, int stride = 1);
static void run_species(
    const FluidState &state, int left_cell_index, int species_count,
    double *X_left, double *X_right, int stride = 1);
```

Register aliases in `DispatchImpl::select_reconstruction`, update
`determine_required_ng`, keep `NG <= amr::MAX_NG`, and define a physically valid
coarse/fine policy. Current wide-stencil policies are locally replaced by
MUSCL-MinMod at coarse/fine faces.

### Hydro integrator and solver boundary — Source extension/Internal

The type-erased block operator is `Numerics::IHydroSolver`:

```cpp
virtual void evaluate_patch(
    amr::AMRControl*, int block_id,
    const FluidState&, const Grid&, double dt,
    std::vector<FluidVector>&, std::vector<double>&,
    const Physical::Gravity::IGravityPolicy*,
    const NumericsConfig&, double flux_weight,
    void *execution_stream) const = 0;

virtual void update_patch(
    const FluidState &old_state, const FluidState &current_state,
    FluidState &new_state,
    const std::vector<FluidVector>&, const std::vector<double>&,
    const Grid&, double old_weight, double flux_weight,
    const NumericsConfig&, void *execution_stream) const = 0;
```

An integrator exposes a static templated `solve(AMRControl&, dt, BCPolicy&,
gravity, hydro, NumericsConfig)` and is registered by adding an isolated dispatch
translation unit plus the selection in `SolverDispatch.cpp`. Reflux stage weights
must match the RK quadrature and flux registration.

### EOS policy — Source extension

Project-owned physical constants have one CPU/CUDA definition in
[`PhysicalConstants.h`](../src/physics/constant/PhysicalConstants.h), organized
by discipline with explicit units. The current set uses SI definitions and
CODATA 2022, not a different constant profile per EOS. Pi delegates to C++20
`<numbers>` and derived radiation/Gaussian-charge quantities reuse the shared
fundamentals. See the [constants and data boundaries](../src/physics/constant/README.md)
before adding values: external tables and deferred reaction-network data are
not regenerated by changing this header. Arbitrary code units are not
automatically converted. Historical raw-bit results using other constants
require independent requalification for the current release.

EOS dispatch uses static duck typing. `src/physics/eos/eos.h` lists the expected
surface. Methods exercised across hydro, initialization, burn, diffusion, and
diagnostics include:

```cpp
double get_gamma(const double *X) const;
double get_eta(double rho, double T, const double *X) const;
double get_pressure(const FluidVector &U, const double *X) const;
double get_temperature(double rho, double e, const double *X) const;
double get_pressure_from_rho_e(double rho, double e, const double *X) const;
double get_eint_from_T(double rho, double T, const double *X) const;
double get_sound_speed(const FluidVector &U, double p, const double *X) const;
double get_total_energy_primitive(
    double rho, double u, double v, double w, double p, const double *X) const;
double get_pressure_from_rho_T(double rho, double T, const double *X) const;
double get_cv(double rho, double T, const double *X) const;
double get_dp_drho_e(double rho, double e, const double *X) const;
double get_dp_de_rho(double rho, double e, const double *X) const;
void evaluate_state(eos_state_t &state) const;
const SpeciesManager *get_species_manager() const;
```

`evaluate_state` is the canonical thermodynamic-state contract. For every
valid `(rho,T,X)` input it must fill finite `P`, `E`, `cv`, `sound_speed`,
`dp_drho`, and `dp_dT`; pressure, specific internal energy, `cv`, and sound
speed must be positive. `dp_drho` means `(dP/drho)_e`, while `dp_dT` means
`(dP/dT)_rho`. Free-energy tabular policies derive these quantities from one
interpolated Helmholtz potential. The legacy direct policy uses supplied
derivative datasets or table-bounded local differences rather than returning
zero. See the
[normalized HDF5 contract](../src/physics/eos/TabularEOS.md).

All policies receive the same fixed-composition isentrope algorithm from
`eos_utils::get_isentropic_state_at_pressure_factor` in `eos_Utils.h`. It
integrates

```text
d ln(T) / d ln(rho) |_s,X = (dP/dT)_rho,X / (rho cv)
```

with RK4 and solves for the target pressure in `ln(rho)` using
`Gamma1=rho*c_s^2/P`. A new EOS implements `evaluate_state`; it must not copy or
special-case the isentrope solver. This shared utility is a source-extension
interface and is not included directly by simulation cases. Case code reaches
it only through `ProblemHelper` and the two stable public headers.

Check the exact overload set against every EOS implementation. `eos.h` documents
the duck-typed surface. Register new types in `EOSDispatcher::dispatch_eos`.

### Gravity policy — Source extension

Derive from `Physical::Gravity::IGravityPolicy`:

```cpp
virtual void update_field(
    const FluidState&, const Grid&, void *execution_stream = nullptr) const = 0;

virtual void add_sources_on_patch(
    std::vector<FluidVector> &dU,
    const FluidState&, const Grid&, double dt,
    void *execution_stream = nullptr) const = 0;
```

Register construction in `make_gravity`. External gravity is a constant logical
vector evaluated inside every hydro RK stage. Self gravity requires a separate
field solver.

### Network, ODE, and linear solver — Source extension

Network types define compile-time sizes and species metadata such as
`NUM_SPECIES`, `ODE_NEQ`, `SPECIES_NAMES`, `AION`, `ZION`, plus the static
`eval_rhs`, `eval_jacobian`, temperature-derivative, registration, and initial
fraction functions consumed by the burn solvers and `ProblemHelper`.

An ODE wrapper has the template form

```cpp
template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_NEW {
    template <typename EOSType>
    static bool integrate(
        double *X_ODE, double rho, double dt_target,
        const EOSType &eos, const BurnConfig &config, double &dt_recommended);
};
```

The four Timmes-derived built-ins remain registered directly. Custom
pynucastro networks use a user-maintained recipe based on
`examples/network/CustomNetworkRecipe.py` and the generator
`tools/network/GenerateNetwork.py`. Each valid lowercase `NETWORK_ID` creates
an isolated package under `src/physics/network/custom/<id>/`. IDs beginning
with `aprox` or `iso` are reserved. Existing-ID replacement requires
`--replace` and preserves the previous package under `.backup/`. CMake
discovers any number of coexisting packages through its generated registry.
A run selects exactly one with `network_name = custom:<id>`.

The documented generator and validation recipes use pynucastro 2.12.0. See
the [network setup](../validation/network/README.md#reproduce-the-records) for
the matching Python environment and build commands.

The adapter converts pynucastro molar RHS/Jacobian entries to ARCH mass-fraction
form, carries nuclear/weak-neutrino energy into the ODE RHS, and namespaces the
generated SimpleCxx headers. Recognized weak tables include the rho*Ye
composition chain rule and signed energy-source gradient. Custom networks set
`SUPPORTS_NSE=false`; the complete-RHS temperature Jacobian uses the shared
fourth-order difference policy with a precision-derived step and boundary stencil.
Generator version 3 removes only compile-time literal-zero Jacobian calls;
runtime numerical zeros remain structural entries for safe KLU refactorization.
The default `NUCLEI` path retains disconnected requested nuclei as inert
species and rejects duplicates. CMake validates each manifest and rejects
packages that predate these version-3 safeguards. Networks that integrate signed
weak losses add a source-integral state using the same BE_NR/BD/ROS4 stages, error control and
rollback; accepted energy includes both nuclear energy and that integral.
Controlled Urca trajectories pass independent reference checks with both
constant-cv and Helmholtz closure. Representative generated-network Helmholtz
applications also pass on both backends; their workload scope, original error
budgets and reproduction commands are in [network validation](../validation/network/README.md).
AMR and restart have their own [application acceptance records](../validation/amr/README.md).

Generator version 4 adds one portable math header used by both the
ordinary C++ adapter and CUDA instantiations. It preserves the original
reaction expressions and their nuclear-data convention. Energy weights derive
from the emitted masses/conversion, with a conserved-baryon mass offset to
condition their dot product. Recognized small immutable metadata is
device-callable without dereferencing Host globals. Explicit views give both
backends access to embedded weak tables: CPU functions borrow host data, while
CUDA storage managers upload and retain device data across grid-storage changes. Interpolation and
reaction/ODE math are maintained once.
Per-package include guards and scoped screening macros allow packages to
coexist. The symbolic Jacobian structure comes from declared writes (after
literal-zero pruning), not from sampling numerical nonzeros; unrecognized
write indices are rejected. CMake enables CUDA execution only for manifests
with generator version at least 4 and `device_callable_math=true`. Version-3
packages and layouts not converted for device execution stay CPU-only;
unrecognized weak layouts are rejected before writing the final package.
Portable generation does not remove the
scientific qualification or custom NSE limitations above,
and focused math/solver smoke checks do not qualify an integrated trajectory.

Matrix and solver policies are independent template parameters. `DenseWrap` is
the dedicated fixed-size backend for at most 31 total ODE equations
(`BurnLimits::MAX_ODE_NEQ`); `SparseWrap` retains a CSC symbolic pattern and
uses KLU analyze/factor/refactor/solve. Parsing leaves `linear_solver = Auto`
unchanged; each backend resolves it during capability checks. At or below 31
total equations both backends select DenseLU; above 31, CPU selects SparseKLU
and CUDA selects cuDSS. Explicit SparseKLU is CPU-only and
explicit cuDSS is CUDA-only. An explicit CPU+cuDSS or CUDA+SparseKLU pair is
rejected during capability resolution, before backend construction; with
`compute_backend = auto`, an explicit solver can therefore select only its
compatible backend. Explicit DenseLU rejects a larger system. Count all species,
temperature and auxiliary source states: the isotope limit is 30 without a
source integral, 29 with one, not an unconditional species-only cutoff.

SparseKLU requires a KLU-enabled build. CUDA cuDSS requires the optional 0.8
library to be linked and the network to be registered for device execution.
The CUDA executor reuses the
shared BE_NR/ROS4/BD ODE continuations; only memory, batched execution, and
library-specific sparse operations are backend-specific. The CUDA route uses a
declared CSR pattern, bounded per-lane workspace and memory-budgeted factor
storage. It does not use a dense `N*N` entry-to-slot table. CPU `SparseWrap`
continues to store CSC values and its existing `N*N` integer lookup. Memory
requirements depend on sparse fill-in and the active workload. For very large
networks, model reliability depends on the isotope set, reaction data and their
range of applicability; current coverage is recorded in
[network validation](../validation/network/README.md).

After generating or replacing a package, rerun CMake. Use `--check` before
writing, and use `-DARCH_CUSTOM_NETWORKS="id1;id2"` to restrict expensive builds.
The generic source scan excludes the custom subtree, so only selected adapter
sources are compiled. The generator itself needs pynucastro only at generation
time; ARCH has no runtime Python dependency. Multi-size generated-network
compatibility evidence is centralized in
[validation/network](../validation/network/README.md).

### Diffusion — Source extension/Experimental

Single-block integrators provide:

```cpp
static void integrate(
    FluidState&, const auto &eos, const Grid&, const SimConfig&,
    double dt, double dt_forward_euler, const auto &boundary_handler);
```

Composite AMR entry points are:

```cpp
Numerics::Diffusion::advance_amr_rkl1(...);
Numerics::Diffusion::advance_amr_rkl2(...);
```

RKL stage mathematics is exposed through `DiffFunction::RKLOrder`,
`compute_stages`, `usable_max_stages`, `stable_step`, and `get_rkl_coeffs`.
`DiffFlux` owns thermal, viscous, and species fluxes. Multi-block stages require
boundary application, ghost exchange, coarse/fine flux registration, reflux,
and post-reflux synchronization.

### AMR — Internal/source extension

`AMRControl` owns `MemoryPool`, `AmrTree`, `GhostExchange`, and `FluxRegister`.
Direct case access is unsupported. `amr::BLOCK_NX/NY/NZ` are 16,
`amr::MAX_NG` is 4, and x storage is padded to 32. Changing these constants affects
allocation, IO shape, reconstruction reach, and the active CUDA device layout.

## HDF5 and restart format

### Plot file

File attributes:

```text
time        double
dim         int
geometry    string
```

Datasets:

```text
Grid/x, Grid/y, Grid/z       physical Cartesian cell-centre coordinates
Grid/level                   AMR leaf level per block
Grid/morton                  Morton code per block
Data/<requested field>       [block, z?, y?, x] interior-cell arrays
```

Registered species use their registered names. The historical visual renderer
searches for `X_` prefixes, creating a composition auto-selection mismatch.

`HDF5Writer` logs PLT write failures and continues the simulation.

### Checkpoint file version 4

Attributes include `checkpoint_version`, `time`, `step`, `chk_index`,
`plt_index`, `dim`, `geometry`, `num_species`, `cells_per_block`, `dt_old`,
`dt_burn`, `resume_after_regrid`, `eos_type`, `ideal_gamma`, `burn_enabled`,
`active_network`, `nse_enabled`, `eos_table_path`, and `eos_table_sha256`.
Checkpoint `eos_type` records the resolved canonical policy (`ideal`,
`helmholtz`, `tabular3d`, or `tabular4d`), so automatic table-rank dispatch is
part of restart identity rather than the raw `tabular` configuration spelling.
`active_network` is `none` when burning is disabled. The timestep values restore growth,
the burn limit carried into the next macro step, and loop phase without
repeating a completed regrid or step-based output event. The table path is
audit metadata; compatibility uses the SHA-256 content identity, so an
unchanged table may move between installations. The table loader fingerprints
the file before and after loading, binds cached owners to that digest, and the
immutable identity passed to every checkpoint therefore describes the bytes
actually resident in the EOS owner rather than a later path lookup.

Datasets:

```text
Blocks/level
Blocks/logical_x1, logical_x2, logical_x3
Data/rho, Data/mom_u, Data/mom_v, Data/mom_w, Data/eng, Data/enuc_rate
Data/rhoX, Data/X    [species, block, interior cell]
Species/name, Species/A, Species/Z, Species/gamma, Species/Cv
```

`Data/X` preserves the native mass fractions used by both backends. The reader
checks that they reproduce the stored `rhoX`; restoring them directly avoids
roundoff from multiplying and then dividing by density. Version-3 files remain
readable with their scientific identity and ENUC state, but reconstruct mass
fractions from `rhoX`. New files preserve both representations.

Restart compatibility checks dimension, geometry, cells per block, EOS policy,
ideal-gas gamma where applicable, reaction-network identity, EOS-table content,
burn and NSE enablement, and every ordered species name and thermodynamic
property. `ENUC` is persisted
because it is restart-relevant when it drives dynamic refinement. Structural
or present-provenance mismatches throw before hierarchy publication. Version-1
and version-2 files remain readable, but are reported as legacy/unverified
because they have no ordered scientific identity or `ENUC`; version 1 also has
no controller state, so hydro recomputes its CFL step and burning starts
conservatively from `dt_init`. Step-zero and already-final restarts do not
duplicate initial/final files. CPU/CUDA both read and write this same Host
schema; the backend name is intentionally not part of compatibility.

## Known limitations

- Verification results apply to the tested workloads and configurations
  described in the [validation index](../validation/README.md), which also
  records combined acceptance status.
- Generated CUDA networks require a device-callable version-4 package.
  Regenerate older CPU-only packages; unlowered layouts are not supported on
  CUDA. Neither backend implements self-gravity, the Jeans indicator or
  custom-network NSE.
- Runtime selection is string based, and several policy surfaces are compile-time
  or duck-typed contracts rather than a stable public ABI.
- State repair, interface clamping, and fallback defaults can alter strict
  conservation or hide malformed numerical selections; production runs must
  inspect their resolved configuration and diagnostics.
- Unit metadata and complete build/run provenance (parameter file, compiler,
  solver settings, boundaries, and commit) remain external to HDF5. Version 3
  embeds the restart-critical EOS/table/network/species identity, but release
  flags still prevent a cross-machine bitwise-reproducibility guarantee.
- Case builds assume the `simulation/<Case>/` layout, and plot-write failures are
  reported without aborting the simulation.
- The historical AMR visual archive is qualitative and retains an input/renderer
  species-name mismatch pending archive regeneration.
- Sedov deposits a normalized continuous finite-radius profile at cell centres,
  so its discrete injected energy remains resolution dependent.

## Source map

Use the [source guide](../src/README.md) for module-level navigation and the
[runtime index](../src/cuda/runtime/README.md) for CUDA's functional groups.
The table below points to the shared interfaces rather than duplicating the
directory inventories.

| Area | Primary files |
| --- | --- |
| application entry | `src/main.cpp` |
| parameter loading | `src/io/ConfigParser.h`, `src/core/RuntimeParams.h` |
| case registration/public facade | `src/core/UserInterface.h`, `ProblemRegistry.h`, `ProblemHelper.h/.cpp` |
| case adapter | `src/interface/GenericProblem.h`, `ProblemGenerator.h` |
| case-facing data | `src/data/UserTypes.h`, `GlobalDefs.h`, `physics/species/Species.h` |
| conserved storage | `src/data/FluidState.h` |
| coordinates/metrics | `src/grid/Grid.h`, `GridMetrics.h` |
| AMR | `src/amr/` |
| runtime composition | `src/driver/SolverDispatch.cpp`, `driver/dispatch/`, `Driver.h` |
| hydro time stepping | `src/numerics/integrator/` |
| flux/reconstruction | `src/numerics/flux/`, `src/numerics/reconstruction/` |
| diffusion | `src/numerics/diffusion/` |
| burn/ODE/linalg | `src/numerics/burnsolver/`, `src/numerics/linalg/` |
| EOS/gravity/network/NSE | `src/physics/` |
| third-party provenance and retained terms | `THIRD_PARTY_NOTICES.md`, `LICENSES/` |
| plot/checkpoint | `src/io/plot/`, `src/io/chk/`, `src/io/hdf5/` |
| benchmarks | `simulation/Sod/`, `simulation/Sedov/` |
| verification and validation records | `validation/` |
