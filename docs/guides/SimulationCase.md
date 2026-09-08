# ARCH Simulation Case Guide

Chinese translation: [SimulationCase.zh-CN.md](SimulationCase.zh-CN.md). The English file
is the authoritative source text.

This guide is the continuous learning path for a first ARCH case. Complete it in
order; the exact parameter catalogue and framework extension interfaces are kept
in the searchable [Research and API Reference](../Reference.md).

## 1. Run a small shock tube

Build ARCH from the repository root if needed:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
```

Then run the small one-dimensional Sod input:

```bash
export OMP_NUM_THREADS=4
./bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

The command-line contract is always:

```text
./bin/ARCH <registered-problem-name> <parameter-file>
```

`REGISTER_PROBLEM_CLASS` in `Sod.cpp` registers the runtime name `Sod`. The
directory organizes its source and inputs. A successful run writes its log,
plot files, and checkpoints to `output/first_sod/`.

The teaching input uses 64 cells, HLLC fluxes, MUSCL-MC reconstruction, SSPRK2,
an ideal gas, and no AMR, burning, gravity, or diffusion. Its scope is teaching
and smoke testing.

## 2. Read the problem as a CFD calculation

ARCH evolves cell averages of the conservative state

\[
U=(\rho,\rho u,\rho v,\rho w,E,\rho X_1,\ldots,\rho X_N).
\]

A case author supplies the easier primitive state

\[
W=(\rho,u,v,w,p,X_1,\ldots,X_N)
\]

at the initial time. The selected EOS derives momentum and total energy.

The main choices in `Sod_beginner.par` have different jobs:

| Parameter | Role |
| --- | --- |
| `solver = HLLC` | face flux/Riemann approximation |
| `reconstruct = muscl` | left/right face-state reconstruction |
| `limiter = mc` | MUSCL slope limiting near discontinuities |
| `time_integrator = RK2` | hydro method-of-lines time integration |
| `cfl = 0.4` | fraction of the explicit wave-stability step |
| `eos_type = ideal` | pressure, energy, sound-speed, and temperature closure |

Coupled temporal accuracy follows the operator composition and its substeps.
The burn/diffusion/hydro composition is symmetric:

```text
B(dt/2) -> D(dt/2) -> H(dt) -> D(dt/2) -> B(dt/2)
```

and is therefore at most second order as a coupled operator split, even when
SSPRK3 and PPM are selected. Shocks, limiters, AMR interfaces, and positivity
repairs can further reduce local observed order. The Reference records the
complete accuracy contract.

### Units

The ideal-gas Euler equations accept any internally consistent unit system.
Helmholtz EOS, nuclear networks, NSE thresholds, and stellar transport use CGS
quantities such as `g`, `cm`, `s`, `K`, and `erg`.

## 3. Understand the minimal parameter file

### Grid and dimension

```ini
geometry = cartesian
nblockx1 = 4
nblockx2 = 0
nblockx3 = 0
```

Every block contains 16 interior cells per active dimension. Setting
`nblockx2 = nblockx3 = 0` creates a true 1D allocation; setting only
`nblockx3 = 0` creates 2D. `nblockx2 = 0` with positive `nblockx3` is invalid.

The six boundary keys accept `outflow`, `reflect`, or `periodic`. Inactive-axis
values are inert.

### Numerics

```ini
solver = HLLC
reconstruct = muscl
limiter = mc
time_integrator = RK2
cfl = 0.4
```

Maintained runtime solvers are `SW`, `VL`, `Roe`, `HLL`, and `HLLC`.
Reconstruction accepts `pcm`, `muscl`/`plm`, and `ppm`. MUSCL limiters are
`minmod`, `superbee`, `vanleer`, and `mc`. Unknown numerical choices emit a
warning and select a fallback; the startup strategy line reports the effective
configuration.

### Time and output

```ini
tmax = 0.15
out_dir = output/first_sod
base_name = SodBeginner
plt_dt = 0.05
plt_variables = DENS, PRES, VELX, ENER
```

Paths resolve from the process working directory. Plot files contain physical
coordinates, AMR level/Morton
metadata, and requested derived fields. Checkpoints contain conservative state
and AMR leaf topology for restart.

### Physics and AMR

```ini
eos_type = ideal
gamma = 1.4
gravity_type = none
use_burn = false
use_diffusion = false
lrefinemax = 0
```

Begin with hydro-only results, then add optional physics. `lrefinemax = 0`
disables AMR; parameter loading still validates `refine_var`.

## 4. Make controlled experiments

Copy the input before changing it:

```bash
mkdir -p runs/sod_experiment
cp simulation/Sod/Sod_beginner.par runs/sod_experiment/arch.par
```

Useful first experiments are:

1. double `nblockx1` and compare the shock/contact width;
2. compare `pcm`, `muscl`, and `ppm` while keeping HLLC and CFL fixed;
3. compare HLL and HLLC at the same resolution;
4. enable `lrefinemax = 1` and inspect `Grid/level` in the HDF5 output;
5. reduce `cfl` and check whether a feature is a time-step artifact.

Change one numerical choice at a time. Accuracy assessment uses the analytic
Sod solution, L1/L2 errors, and conservation measures.

## 5. Maintained benchmark cases

The simulation directory separates physically different benchmarks:

```text
simulation/
├── Sod/
│   ├── Sod.cpp
│   ├── Sod.par
│   └── Sod_beginner.par
└── Sedov/
    ├── Sedov.cpp
    └── Sedov.par
```

- `Sod` is strictly a one-dimensional Cartesian shock tube. `Sod.par` uses 128
  uniform cells and the standard left/right states at `t = 0.2`.
- `Sedov` is a Cartesian blast with a finite deposition radius. In 2D,
  `explosion_energy` is energy per unit depth. The continuous injection pressure
  is normalized to the requested energy, but cell-centre sampling introduces a
  resolution-dependent deposition error.

Separate directories give each benchmark one physical contract.

The following [simulation directories](../../simulation/README.md) define
verification problems shared by CPU and CUDA. Their canonical parameter files
live under the owning `validation/<module>/inputs/` directory:

| Directory | Purpose |
| --- | --- |
| `SmoothAdvection/` | PCM/MUSCL/PPM smooth hydro convergence |
| `DiffusionMode/` | analytic cosine-mode species diffusion with RKL1/RKL2 |
| `ExternalGravity/` | constant-acceleration source update |
| `BurnOneZone/` | aprox13/Helmholtz ODE solver comparison |

Their pass/fail decisions and compact CSV results are kept together in the
[validation index](../../validation/README.md); this guide does not duplicate the
analysis tables.

## 6. Create a new case

Create `simulation/MyCase/MyCase.cpp`. `REGISTER_PROBLEM_CLASS` wraps a plain,
default-constructible case class with these methods:

```cpp
void Setup(SimConfig &config, SpeciesManager &specs);
void Init(const PointCoords &point, PrimitiveData &out) const;
```

Use these two ARCH headers from a directory one level below `simulation/`:

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
```

These are the complete case-facing ARCH header surface. Standard-library
headers may be added as needed, but a simulation case must not include concrete
EOS headers, `eos_Utils.h`, or `eosdispatch.h`. `UserInterface.h` re-exports the
stable registration, case types, and `ProblemHelper` operations; keeping
`GlobalDefs.h` as the second explicit include exposes the typed runtime
configuration without coupling a case to an EOS policy.

`Setup` runs before grid allocation. Read case parameters, validate them, and
register species there. `Init` is called under OpenMP to populate the allocated
root-grid cells once. Initial and later fine blocks are constructed by
conservative AMR transfer instead of calling `Init` again. `Init` must still be
deterministic, thread-safe, and free of order-dependent side effects.

Minimal complete example:

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

#include <cmath>
#include <stdexcept>

class GaussianDensity
{
    double rho0_ = 1.0;
    double pressure_ = 1.0;
    double amplitude_ = 0.1;
    double width_ = 0.1;
    int gas_id_ = -1;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        rho0_ = config.Get<double>("rho0", 1.0);
        pressure_ = config.Get<double>("pressure0", 1.0);
        amplitude_ = config.Get<double>("amplitude", 0.1);
        width_ = config.Get<double>("width", 0.1);
        if (rho0_ <= 0.0 || pressure_ <= 0.0 || width_ <= 0.0) {
            throw std::invalid_argument("GaussianDensity parameters must be positive.");
        }
        gas_id_ = specs.add_species(
            "Gas", 1.0, 1.0, config.physics.gamma, 1.0);
    }

    void Init(const PointCoords &point, PrimitiveData &out) const
    {
        const double radius2 = point.x * point.x + point.y * point.y;
        out.rho = rho0_ + amplitude_ * std::exp(-radius2 / (width_ * width_));
        out.p = pressure_;
        out.u = 0.0;
        out.v = 0.0;
        out.w = 0.0;
        out.SetMassFraction(gas_id_, 1.0);
    }
};

REGISTER_PROBLEM_CLASS("GaussianDensity", GaussianDensity);
```

After adding a new `.cpp`, rerun CMake configuration to refresh the source glob:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
./bin/ARCH GaussianDensity path/to/arch.par
```

## 7. Case-facing types and helpers

### `SimConfig`

Use `config.Get<double/int/string>(key, default)` for case-specific parameters.
Numeric values use `std::stod`; write evaluated decimal values for expressions
such as `2*pi`. Unknown keys are retained as custom parameters without spelling
validation.

Core settings should be read from their typed members when needed, for example:

```cpp
config.grid.dim
config.grid.x1_min
config.physics.gamma
config.physics.gravity.g_y
```

### `PointCoords`

Every point exposes Cartesian, spherical, and cylindrical representations:

```text
point.x, point.y, point.z
point.r, point.theta, point.phi
point.r_cy, point.phi_cy, point.z_cy
```

Logical axes depend on geometry and dimension:

| Geometry | 1D | 2D | 3D |
| --- | --- | --- | --- |
| Cartesian | x | x, y | x, y, z |
| Spherical | r | r, phi (polar plane) | r, theta, phi |
| Cylindrical | r | r, phi (polar plane) | r, z, phi |

Converted coordinates use origin `(0,0,0)`. Apply case-specific center offsets
inside `Init`.

### `PrimitiveData`

Assign positive `rho` and `p`, velocity components, and a complete composition.
The framework computes conservative momenta and energy through the selected EOS.

For a state defined by density and temperature, call
`state.SetTemperature(temperature)` in `Init`. This makes temperature the energy
initialization authority; `p` may still be set to the corresponding diagnostic
pressure. Without this call, ARCH derives energy from `rho` and `p`.

`GetSpeciesID` returns `-1` when a name is absent. Require `id >= 0` before
calling `SetMassFraction`. Mass fractions should be non-negative and sum to one.

### `ProblemHelper`

Cases using a built-in network should call this before manually adding species:

```cpp
std::vector<double> default_X;
ProblemHelper::SetupNetworkAndFractions(config, specs, default_X);
```

Supported names are `aprox13`, `aprox19`, `aprox21`, and `iso7`.

For a temperature-defined initial reference state, compute pressure once in
`Setup`:

```cpp
const double pressure = ProblemHelper::GetPressureFromRhoT(
    config, specs, density, temperature, default_X.data());
```

This helper performs runtime EOS dispatch and belongs in `Setup`.

If a cell-average initializer needs the root-level physical cell width, keep
the AMR implementation private and call:

```cpp
const double dx = ProblemHelper::GetRootCellWidth(config, 1);
```

The logical axis is one-based. ARCH currently uses 16 active cells per block
direction and four guard cells on each side (`16 + 8` stored along x). Guard
cells do not contribute to `dx`; do not include `AmrDefines.h` in a case to
reconstruct this value.

To construct a nearby state at the same composition and specific entropy, use
the EOS-independent helper from the same two-header surface:

```cpp
const ProblemHelper::IsentropicState compressed =
    ProblemHelper::GetIsentropicStateAtPressureFactor(
        config, specs, density, temperature,
        default_X.data(), 1.001);

// compressed.rho
// compressed.temperature
// compressed.pressure
// compressed.sound_speed
```

The final argument is `P_target/P_reference`, not `dP/P`. The active EOS
supplies `c_v`, `(dP/dT)_rho`, pressure, and sound speed through one common
fixed-composition isentrope implementation. Call the helper only in `Setup`;
it performs runtime EOS dispatch and integrates a local thermodynamic path. A
request that would move farther than `0.25` in `ln(rho)` is rejected rather than
silently extrapolated. Do not use an ideal-gas `T-P` relation for Helmholtz or
tabular states.

Use `SetTemperature` in `Init` with the same value when the initial internal
energy must match the specified temperature exactly.

## 8. Pre-run checklist

- Rerun CMake configuration after adding a case source.
- Match the command-line problem name to its registration string.
- Resolve file paths from the process working directory.
- Use a maintained MUSCL limiter; `none` selects the MinMod fallback.
- Interpret 2D spherical `x2` as planar `phi`.
- Use CGS material data with Helmholtz and network modules.
- Materialize `EOS_toolkit/tables/helmholtz/helm_table.dat` with Git LFS
  and verify its checksum for Helmholtz validation runs.
- Require `GetSpeciesID >= 0` before indexing composition.
- Initialize every species fraction and enforce a unit sum.
- Keep EOS dispatch helpers in `Setup`.
- Keep the case-facing ARCH includes limited to `UserInterface.h` and
  `GlobalDefs.h`; access EOS operations through `ProblemHelper`.
- Record floors, clamps, and fallback warnings in convergence studies.
- Select `compute_backend = cpu`, `cuda` or `auto` for the configured build;
  see [backend selection](../CudaBackendStatus.md) for supported combinations.

For all exact accepted values and extension contracts, continue with
[docs/Reference.md](../Reference.md).
