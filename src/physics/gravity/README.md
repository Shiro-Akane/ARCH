# Gravitational sources

This directory provides the configured gravity policy and its source coupling.

- [IGravityPolicy.h](IGravityPolicy.h) is the host patch interface.
- [GravityDispatch.h](GravityDispatch.h) binds the resolved `GravityId` through
  `make_gravity(config, GravityId)`; disabled gravity returns no policy. Names
  are parsed by the shared resolver, and unknown names remain errors.
- [ExternalGravity.h](ExternalGravity.h) traverses host patches;
  [GravitySource.h](GravitySource.h) owns the shared cell update.

The external constant-acceleration source uses the same mathematics on CPU and
CUDA; device kernels handle traversal and state access. Hydro stage scheduling
remains in the common integrator and driver modules.

[HydroSourceBinding.h](HydroSourceBinding.h) owns Host source discovery and each
patch's borrowed source claim. It selects the actual prepared external or self
field, validates its operands, consumes face-flux work, supplies AMR's optional
Energy interpretation and commits the original receipt. Hydro controls when
these operations run; it does not select concrete gravity frames. Geometric
sources and physical cell/face measures stay in the geometry owners and run
with gravity disabled as well. Native cylindrical 2D composes optional CPU
external or self gravity through these same owners. Its isolated source uses
three-dimensional full-ring physics; `g_phi=0` and field energy/output measures
are per unit azimuth. The retired RZ-specific solver/cache infrastructure is
absent. Native cylindrical 2D also uses the existing CUDA gravity owner; scoped
field, work and continuation evidence is indexed in the [release plan](../../../ARCH_RELEASE_PLAN.md).
The resolved execution plan still rejects unsupported routes.

[UniformGravity.h](UniformGravity.h) is the standalone uniform CPU adapter from a
borrowed Host density view to CGS potential and acceleration. It owns the
physical source and periodic density-mean removal; the Poisson operator and MG
cycle live in `numerics/elliptic` and `numerics/multigrid`. It supports one
uniform Cartesian domain with periodic or prescribed-potential boundaries.
It remains an independently tested uniform mathematical adapter.

`self/SelfGravity` owns the production domain field on CPU or CUDA. Its workspace
caches native leaf mappings, hierarchy plans, resident fields and boundary trees.
`GravityExecution` owns shared physical work descriptors; `GravityBoundary` owns
the finite-domain mass/dipole/quadrupole tree. `GravityPatchView` contains borrowed
source pointers and shared momentum/face-mass-flux energy work.
Acceleration is stored per physical face. Curvilinear energy work keeps separate
lower and upper coefficients for each cell because its volume and potential
enter the compatible mass-flux work. Both backends consume that same layout.

The generic operator and MG/FGMRES stay in `numerics`. `CompositeMultigrid`
uses one mathematical flow on both backends. The bounded coarse inverse factors
the actual composite operator with shared DenseLU for Cartesian and radial geometry;
no whole-domain KLU/cuDSS or FFT route is introduced.

Validated production models are Cartesian periodic 1D–3D and isolated 3D, with
burning/diffusion coupling. CPU/CUDA 1D isolated spherical/cylindrical gravity
has radial field and AMR acceptance. Tested CPU/CUDA spherical 2D full-azimuth polar and
3D cylindrical/spherical isolated gravity include origin, axis and pole AMR
chart mapping. The driver solves at each actual RK input,
invalidates after state changes and explicitly materializes fields for output.
CUDA allocation and launches live together in `cuda/runtime/gravity`.

Explicit zero Dirichlet/Neumann and user per-side Dirichlet, Neumann, linear
Robin and periodic combinations use the same composite operator in all three
geometries, including valid sectors/wedges. `self/GravityUserBoundary` translates
the typed callback and checks topology/coefficients. Pure Neumann Gauss
compatibility uses the full density; only fully periodic models subtract the
mean. Boundary structure changes invalidate coarse factors, while value-only
changes update the RHS. `self/GravityBoundaryDiagnostics` samples the published
field's surface Green exchange; it does not replace fluid source-work accounting.
See [user boundaries](../../../docs/guides/UserBoundaries.md).

See [GravityBox](../../../simulation/GravityBox/README.md), the
[Reference](../../../docs/Reference.md) and
[P5–P7 acceptance](../../../docs/histroy/science/P5P7GravityAcceptance.zh-CN.md).
