# ARCH Research and API Reference

Chinese translation: [Reference.zh-CN.md](Reference.zh-CN.md). The English file
is the authoritative source text; interface and behavior changes must be made
here first.

This searchable reference follows the declarations and dispatch paths in the
current `main` branch. The student workflow is in
[`simulation/CaseGuide.md`](../simulation/CaseGuide.md).

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
  functions.
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

ARCH builds one executable and one internal object target. Its extension
contracts operate at source level.

Scientific provenance is recorded separately from interface stability. The
reaction networks, NSE formulation, and Helmholtz EOS trace to Frank Timmes;
the stellar-conductivity mathematics traces immediately to AMReX-Astro
Microphysics. File-level boundaries and retained terms are listed in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). Other modules have no
external provenance claim unless their file header or that notice says so.

## Capability matrix

### Execution and mesh

| Feature | Accepted value or interface | Status in `main` | Notes |
| --- | --- | --- | --- |
| Host execution | `compute_backend = cpu` | supported | OpenMP is configured at build time. |
| CUDA execution | `compute_backend = cuda/auto` | reserved | Keys are parsed; `main` executes the CPU backend. |
| Dimension | positive `nblockx1`; zero trailing block counts | supported | `nblockx2=0,nblockx3=0` is 1D; `nblockx3=0` is 2D. |
| Geometry | `cartesian`, `cylindrical`, `spherical` | supported | Strings are effectively case-sensitive in grid logic. |
| AMR | `lrefinemax >= 0` | supported | Fixed 16-cell block extent per active dimension. |
| Self gravity | `gravity_type = self` | unavailable | The gravity factory aborts. |
| Jeans field | `JENS` | reserved | Parser warns and disables it. |

### Numerical policies

| Category | Runtime values | Status and exact behavior |
| --- | --- | --- |
| Flux | `SW`, `VL`, `Roe`, `HLL`, `HLLC` | dispatched |
| Reconstruction | `pcm`, `donor_cell`; `muscl`, `plm`; `ppm` | dispatched aliases shown; `weno5` appears only in ghost-count code |
| MUSCL limiter | `minmod`, `superbee`, `vanleer`, `mc` | dispatched; unknown values, including `none`, fall back to MinMod |
| Hydro time | `Euler`, `RK1`; `RK2`, `SSPRK2`; `RK3`, `SSPRK3` | Euler, SSPRK2, SSPRK3 |
| Diffusion time | `RKL2` (default), `RKL1` | RKL2 is second order for the isolated diffusion operator; RKL1 is the optional first-order variant |
| EOS | `ideal`, `tabular`, `helmholtz` | dispatched on CPU |
| Gravity | `none`, `external` | supported; unknown strings select no gravity |
| Network | `aprox13`, `aprox19`, `aprox21`, `iso7` | dispatched when burning is enabled |
| Burn ODE | `BE_NR`, `ROS4`, `BD` | all dispatched and covered by the one-zone CPU regression |
| Linear solve | `DenseLU` | supported |
| Sparse solve | `SparseKLU` | explicit runtime error; wrapper remains a placeholder |

Use the canonical spellings above; normalization varies by dispatcher.

## Runtime architecture

The executable follows this lifecycle:

```text
main(argc, argv)
  -> RuntimeParams::Load(.par)
  -> ProblemRegistry::Create(problem name)
  -> case.Setup(config, species)
  -> DispatchSolver
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
- specific internal energy is clamped to `[1e-10, max_eint]`;
- negative species fractions are clipped and all fractions are renormalized;
- if the species sum is nearly zero, a uniform composition is installed.

These engineering safeguards modify state outside conservative flux updates.
Runs record their repair contribution alongside conservation and L1/L2 metrics.

The production PPM path reconstructs density, velocity, pressure, and species,
then calls the selected EOS to rebuild total energy. It floors density and
pressure, bounds species to `[0,1]`, and renormalizes interface compositions.

### Build reproducibility and compile-time compromise

Release compilation uses `-O3 -march=native -ffast-math -DNDEBUG`. Validation
records include compiler, flags, OpenMP thread count, hardware, and numerical
tolerances.

The dispatch translation units are compiled at `-O1`, with LTO disabled and
`-fno-inline-functions-called-once`, because the integrator × flux ×
reconstruction × EOS template matrix otherwise consumes several GB per
translation unit. Burner policies are type-erased behind `BurnerHandle` before
the full flux matrix, limiting network/ODE multiplication across hydro
instantiations. Performance and correctness require measurement.

## Build, registration, and command line

### Build contract

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
```

CMake fetches HighFive during configuration and links HDF5 C++/HL libraries.
EOS `.dat`/`.h5` assets may require Git LFS.

Sources are found with CMake `GLOB_RECURSE` over `src/core`, `src/physics`,
`src/numerics`, `src/io`, and `simulation`. Rerun
`cmake -S . -B build ...` after adding a `.cpp`.

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
| gravity | unknown value becomes no gravity |
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

### Hydro numerics and execution

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `solver` | string | `SW` | `SW`, `VL`, `Roe`, `HLL`, `HLLC` |
| `reconstruct` | string | `pcm` | `pcm`, `donor_cell`, `muscl`, `plm`, `ppm` |
| `limiter` | string | `minmod` | MUSCL only: `minmod`, `superbee`, `vanleer`, `mc` |
| `time_integrator` | string | `RK2` | `Euler/RK1`, `RK2/SSPRK2`, `RK3/SSPRK3` |
| `timeintegrator` | string | — | legacy fallback key when canonical key is absent |
| `cfl` | double | `0.8` | explicit hydro CFL; range unchecked at load time |
| `EntropyFix` | string | `On` | exact `Off` or `False` disables; other values enable |
| `EntropyFixCoefficient` | double | `0.1` | used when entropy fix is enabled |
| `sml_rho` | double | `1e-12` | density repair threshold |
| `max_eint` | double | `1e21` | specific internal-energy ceiling |
| `compute_backend` | string | `cpu` | parsed; `cuda/auto` reserved for V2 in current branch |
| `cuda_device` | int | `0` | reserved |

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
| `gravity_type` | string | `none` | `none`, `external`; `self` aborts |
| `gravity_g_x/y/z` | expression | `0` | used for external gravity |
| `gravity_G` | expression | `6.6743e-8` | parsed only for unsupported self gravity |

The maintained Helmholtz validation asset is the `helm_table.dat` member of the
`helmholtz.tar.xz` archive downloaded from the
[Timmes EOS page](https://cococubed.com/code_pages/eos.shtml). It is materialized
at `EOS_toolkit/eos_tabular/helmholtz/helm_table.dat` through Git LFS and has
size 60,242,514 bytes and
SHA-256
`c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1`.
The original table member is the validation authority. The loader uses the
fixed 541×201 Timmes layout and requires all four data blocks; the burn baseline
also requires the exact checksum above.

### Burning, network, and ODE

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `use_burn` | int flag | `0` | nonzero enables |
| `network_name` | string | `aprox19` | `aprox13`, `aprox19`, `aprox21`, `iso7` |
| `nuclearTempMin` | double | `1e9` | K; burn activation threshold |
| `nuclearDensMin` | double | `1e-10` | g/cm3; burn activation threshold |
| `smallt` | double | `1e5` | K; burn state floor |
| `smallx` | double | `1e-20` | composition floor |
| `enucDtFactor` | double | `1e30` | energy-release time-step limiter; huge default is effectively off |
| `use_nse` | int flag | `1` | enables thresholded NSE projection |
| `nseTempThreshold` | double | `4.5e9` | K |
| `nseDensThreshold` | double | `1e6` | g/cm3 |
| `enforce_mass_conservation` | int flag | `1` | renormalizes composition after burn |
| `burn_verbose_level` | int | `0` | burn diagnostic verbosity |
| `ode_solver` | string | `BE_NR` | `BE_NR`, `ROS4`, or `BD` |
| `linear_solver` | string | `DenseLU` | `SparseKLU` throws |
| `ode_rtol` | double | `1e-4` | relative ODE tolerance |
| `ode_atol` | double | `1e-8` | absolute ODE tolerance |
| `ode_max_newton_iter` | int | `50` | Newton limit where used |
| `ode_max_substeps` | int | `10000` | adaptive substep limit |
| `ode_dt_safe_fac` | double | `0.9` | adaptive controller safety factor |
| `ode_dt_fac_max` | double | `2.0` | growth factor |
| `ode_dt_fac_min` | double | `0.1` | shrink factor |
| `ode_initial_dt_frac` | double | `1e-3` | initial internal substep fraction |
| `ode_use_numerical_jac` | int flag | `0` | stored; verify solver-specific use before relying on it |
| `ode_freeze_jacobian` | int flag | `0` | stored; verify solver-specific use before relying on it |
| `dt_init` | custom double | `1e-16` | first macro step when burn is enabled |
| `dt_min` | custom double | `1e-20` | abort threshold for macro step |
| `tstep_change_factor` | custom double | `1.2` | maximum macro-step growth after first step |

`ROS4` uses a matched four-stage, fourth-order, L-stable tableau. Each internal
step evaluates one Jacobian, factors `I - gamma*dt*J` once, and reuses the
factors for all stages. Its aprox13/Helmholtz one-zone result passes the current
BE_NR cross-solver tolerance. Production studies still report a
substep/tolerance convergence series and compare species and energy histories,
especially when extending the network or EOS coupling.

The last three are custom-map controls.
Network-specific initial fractions such as `xc12` are consumed by the selected
network setup implementation.

### Diffusion

| Key | Type | Load default | Contract |
| --- | --- | --- | --- |
| `use_diffusion` | int flag | `0` | nonzero enables |
| `diff_integrator` | string | `RKL2` | `RKL1` or `RKL2` |
| `diff_cfl` | double | `0.8` | fraction used in RKL stage/step selection |
| `diff_max_stages` | int | `256` | caps the STS polynomial and macro step |
| `use_thermal_diff` | int flag | `0` | thermal conduction |
| `use_viscous_diff` | int flag | `0` | momentum diffusion |
| `use_species_diff` | int flag | `0` | composition diffusion |
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
| `restart` | string | `false` | strings containing `true`, `1`, `yes`, or `on` enable |
| `restart_file` | string | empty | required for an actual restart path |

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

## Stable case API

### Include surface and class contract

For `simulation/<Case>/<Case>.cpp`:

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
```

The relative path assumes the maintained two-level case layout. A case class is
wrapped by `TypedProblemGenerator<T>` and must be default constructible:

```cpp
void Setup(SimConfig &config, SpeciesManager &specs);
void Init(const PointCoords &point, PrimitiveData &out) const;
```

`Setup` runs once before allocation. `Init` runs through an OpenMP population
loop for allocated block cells, including ghost storage, and can run again
during initial AMR construction. Keep it deterministic, thread-safe, and
independent of call count.

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
```

Call network setup before manually registering other species. It calls a
network’s `RegisterSpecies` when the manager is empty. Pressure conversion
performs runtime EOS dispatch and belongs in `Setup`.

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

Register networks and ODE strings in `BurnDispatch.h`. Dense linear solvers
provide templated `solve`, and where needed `factorize`/`solve_with_factors`.
`BurnLimits::MAX_SPECIES` is 30, with one additional temperature equation.

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
allocation, IO shape, reconstruction reach, and planned CUDA layout.

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

### Checkpoint file version 1

Attributes include `checkpoint_version`, `time`, `step`, `chk_index`,
`plt_index`, `dim`, `geometry`, `num_species`, and `cells_per_block`.

Datasets:

```text
Blocks/level
Blocks/logical_x1, logical_x2, logical_x3
Data/rho, Data/mom_u, Data/mom_v, Data/mom_w, Data/eng
Data/rhoX    [species, block, interior cell]
```

Restart compatibility checks dimension, geometry, species count, and cells per
block. Scientific provenance—parameter file, compiler, EOS, species order,
solver, boundaries, and commit—stays external. Checkpoint structural failures
throw.

## Known limitations

- The `main` branch executes the CPU backend. CUDA parity and quantitative AMR
  convergence remain pending; self gravity, the Jeans indicator, and SparseKLU
  are not implemented.
- Runtime selection is string based, and several policy surfaces are compile-time
  or duck-typed contracts rather than a stable public ABI.
- State repair, interface clamping, and fallback defaults can alter strict
  conservation or hide malformed numerical selections; production runs must
  inspect their resolved configuration and diagnostics.
- Unit metadata and complete checkpoint provenance remain external to HDF5;
  release flags also prevent a cross-machine bitwise-reproducibility guarantee.
- Case builds assume the `simulation/<Case>/` layout, and plot-write failures are
  reported without aborting the simulation.
- The historical AMR visual archive is qualitative and retains an input/renderer
  species-name mismatch pending archive regeneration.
- Sedov deposits a normalized continuous finite-radius profile at cell centres,
  so its discrete injected energy remains resolution dependent.

## Source map

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
