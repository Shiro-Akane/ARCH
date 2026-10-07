# Physical and gravitational user boundaries

Cases define fluid/transport boundaries separately from potential boundaries.
Both interfaces are exported by `<UserInterface.h>` and `<GlobalDefs.h>`, with
distinct contexts, result types and registration macros. Initial conditions
are described in the [case guide](SimulationCase.md); see the
[Reference](../Reference.md) for parameter tables.

## Files and registration

Keep the boundary sources beside the registered case source:

```text
simulation/MyCase/
├── case.cpp
├── physical_boundary.cpp   # Hydro, heat, viscosity, species
├── gravity_boundary.cpp    # Potential
└── MyCase.par
```

Existing case sources may retain their names. Reconfigure and rebuild after
adding sources; CMake discovers the sibling files. Callbacks are compiled into
the program, with no runtime path setting or dynamic compilation. Use the same
registration name as the case. Missing, duplicate or incorrectly located
registrations fail explicitly. Only the boundary file actually needed is required.

## Physical conditions

The six `x1l/x1r/x2l/x2r/x3l/x3r_boundary_type` keys refer to native grid axes.

| Setting | Meaning |
| --- | --- |
| `outflow`, `neumann` | Extrapolated interior state, homogeneous normal gradient; not a general nonreflecting condition |
| `reflect`, `reflecting` | Reverse normal momentum, inherit other components |
| `periodic` | Pair both faces of one direction |
| `user` | Callback in `physical_boundary.cpp`; `inflow` and fluid `dirichlet` alias this prescribed-state interface |

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>

arch::boundary::PhysicalBoundaryData PhysicalWall(
    const arch::boundary::PhysicalBoundaryContext& ctx)
{
    using namespace arch::boundary;
    PhysicalBoundaryData result;
    if (ctx.purpose == BoundaryPurpose::Hydro)
        result.hydro = ctx.interior;
    else if (ctx.purpose == BoundaryPurpose::Diffusion)
        result.temperature = {ScalarBoundaryKind::OutwardFlux, 0.0};
    return result;
}
REGISTER_PHYSICAL_BOUNDARY("MyCase", PhysicalWall)
```

Hydro requires a complete primitive: positive density, valid pressure or
temperature, all three native velocity components and every species fraction.
The selected EOS constructs energy. Invalid composition, nonfinite or
out-of-domain states are rejected rather than repaired at this interface.
See [UserGravity](../../simulation/UserGravity/README.md) for free functions
and [UserBoundary](../../simulation/UserBoundary/README.md) for callable classes.

Diffusion uses `temperature`, `velocity[0..2]` and a complete `species` vector.
Transverse velocity still exists in 1D. Prescribed channels must be enabled.
When diffusion is enabled and a physical callback is registered, its Diffusion
branch is evaluated on nonperiodic physical faces, allowing heat conditions
on reflecting fluid walls.

| Scalar condition | Operation |
| --- | --- |
| `None` | Inherit the completed builtin ghost, including reflection |
| `Value(W)` | `Wghost = 2 W - Winterior` |
| `NormalGradient(g)` | `Wghost = Winterior + 2 d g`, using physical face-to-ghost distance d |
| `OutwardFlux(q)` | Retain the ghost base and replace the actual diffusion face flux |

Positive flux points out of the domain. CGS units are K for temperature, cm/s
for velocity, erg/(cm² s) for heat flux, g/(cm s²) for momentum flux, and
g/(cm² s) for species mass flux; gradients divide the scalar unit by cm.
Species gradients and diffusion fluxes must sum to zero. Zero heat flux is
adiabatic; reflection alone does not prescribe a wall temperature. Changing
viscous momentum flux retains its associated mechanical energy work.
An explicit Diffusion `hydro` primitive replaces the inherited base. Hydro
requests reject state-changing Value/NormalGradient transport channels.

## Potential conditions

For `gravity_type=self`, `gravity_boundary` accepts:

| Setting | Meaning |
| --- | --- |
| `periodic` | Fully periodic source with volume-mean density removed and zero-mean potential gauge |
| `isolated` | Existing finite-mass, radial or 2D logarithmic exterior model; curved domains require full azimuth |
| `dirichlet` | Zero potential on nonperiodic physical faces |
| `neumann` | Zero outward potential gradient; usually incompatible with positive total mass |
| `user` | Conditions from `gravity_boundary.cpp`, including paired periodic directions |

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>

arch::boundary::GravityBoundaryData PotentialWall(
    const arch::boundary::GravityBoundaryContext& ctx)
{
    const double G = arch::constants::gravity::cgs::gravitational_constant;
    const double rho = 1.0; // g/cm³
    const double x = ctx.native_position[0];
    return arch::boundary::GravityBoundaryData::Dirichlet(
        2.0 * arch::constants::math::pi * G * rho * x * x);
}
REGISTER_GRAVITY_BOUNDARY("MyCase", PotentialWall)
```

The example is a uniform Cartesian slab potential, not a general isolated
model. Constructors are `Dirichlet(Phi)`, `Neumann(dPhi_dn)`,
`Robin(a,b,c)` and `Periodic()`. Linear Robin means `a Phi + b dPhi/dn = c`,
with finite `a>=0`, `b>0`; use the separate Dirichlet constructor for fixed
potential. Potential has units cm²/s² and gradient cm/s². For dimensionless a
and b in cm, c has potential units. Each side uses one kind and constant a,b
within a stage; c may vary with position/time. Changing kind/a/b across stages
rebuilds the operator/coarse cache; changing c alone updates the RHS.

Pure Neumann data must satisfy Gauss compatibility against the **full** source:

$$
\oint_{\partial\Omega}\partial_n\Phi\,dA
=4\pi G\int_\Omega\rho\,dV.
$$

Compatible problems use a zero-volume-mean gauge; incompatible ones stop
without subtracting mass. Periodic sides must pair and match fluid/AMR topology.
Fluid reflection does not supply gravitational image mass.

## Coordinates and scheduling

Contexts expose read-only `config`, `species`, converted `point`,
`native_position`, face `axis/side`, Cartesian unit outward `cartesian_normal`,
and actual RK/RKL input-stage `time`. Velocity components remain native
orthonormal components. `ghost_depth`, `ghost_point` and `physical_distance`
describe requested ghost layers. Callbacks may run concurrently: do not retain
context references, mutate domain state or depend on invocation order.
The same input state, stage time and purpose must produce the same result.
The runtime may reuse completed ghosts; invocation counts, global mutable
state and random draws must not determine boundary data.

| Geometry | 1D | 2D | 3D |
| --- | --- | --- | --- |
| Cartesian | x | (x,y) | (x,y,z) |
| Cylindrical | r | (r,z) | (r,z,phi) |
| Spherical | r | Equatorial (r,phi) | (r,theta,phi) |

Cylindrical 2D points have `x=r`, `y=0`, `z=z` and `phi_cy=0`; radial, axial and azimuthal velocities are distinct physical components. Its full Native RZ scientific and Device qualification remains in progress, and execution is subject to Runtime capability checks. Historical cylindrical polar boundary checks retain their original chart.

Explicit potential data permits valid annuli, azimuthal sectors and spherical
wedges. Existing root-grid and 2:1 AMR constraints still apply. Origin, axis and
pole regularity belongs to the coordinate-join machinery; zero-area joins and
internal AMR faces are not overwritten by user callbacks. Corners have fixed
x1→x2→x3 priority, with the last active axis owning intersecting ghosts and
both executors reading the same immutable donor/base snapshots. Periodic joins
require a complete consistent coordinate chart.

## Backends, accounting and restart

Ordinary C++ callbacks execute on Host. CUDA transfers only the required donor,
ghost, control and observation surfaces, using shared EOS and face mathematics.
Builtin boundaries retain their resident fast path. Callback synchronization
can dominate tiny grids; measure matched physical endpoints and domain sizes.

- `boundary_fluxes.tsv` integrates actual RK/RKL face fluxes: mass, native
  momenta, fluid energy, species and heat. Positive means outward; units follow
  physical `GridMetrics` measures.
- `gravity_boundary_exchange.tsv` records `0.5∫rho Phi dV`, the Green boundary
  exchange between successive published fields and sampling costs. RK field
  times can reverse order. This alone is not a macro-step total-energy proof;
  open mass flux, gauge and fluid gravitational work need joint analysis.
- CUDA observer transfers, kernels and synchronization are reported separately.

Budgets start at process launch and restart their accumulation after resume.
Native curved-coordinate momenta are not global Cartesian conserved components.
Checkpoint format 7 requires physical face types, case/callback source digests
and scientific custom inputs. Changing or omitting this identity rejects
restart. Source/output absolute paths and backend choice are excluded, allowing
compatible restarts across machines and CPU/CUDA.

## 配置 v3 与函数式注册

每个算例必须声明它读取的参数。函数式算例使用 `REGISTER_PROBLEM_WITH_CONFIGURATION(NAME, SETUP_FUNC, INIT_FUNC, DESCRIBE_FUNC)`，由最后一个回调返回完整 `CaseConfiguration`；原三参数宏不补造声明或默认值。示例见 [UserGravity/case.cpp](../../simulation/UserGravity/case.cpp)。`user_boundary_heat_flux` 显式声明为浮点输入，单位 `erg/(cm^2*s)`；完整输入保留现有物理值。自引力使用内部固定 CGS 常数，不能再通过 `gravity_G` / `G_const` 改写。

完整的参数声明用于配置/初始化边界校验，与完整场及 AMR 的能力登记分别维护。新任意算例不会仅因注册便自动获得 Preview。
