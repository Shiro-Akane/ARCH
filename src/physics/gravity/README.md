# Gravitational sources

This directory provides the configured gravity policy and its source coupling.

- [IGravityPolicy.h](IGravityPolicy.h) is the host patch interface.
- [GravityDispatch.h](GravityDispatch.h) binds the resolved `GravityId` through
  `make_gravity(config, GravityId)` and owns the compact disabled policy; names are parsed by the shared resolver.
- [ExternalGravity.h](ExternalGravity.h) traverses host patches;
  [GravitySource.h](GravitySource.h) owns the shared cell update.

The external constant-acceleration source uses the same mathematics on CPU and
CUDA; device kernels handle traversal and state access. Hydro stage scheduling
remains in the common integrator and driver modules.

[UniformGravity.h](UniformGravity.h) is the standalone P2 CPU adapter from a
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

The generic operator and MG/FGMRES stay in `numerics`. `CompositeMultigrid`
uses one mathematical flow on both backends. The bounded coarse inverse factors
the actual composite operator with shared DenseLU for Cartesian and radial geometry;
no whole-domain KLU/cuDSS or FFT route is introduced.

Validated production models are Cartesian periodic 1D–3D and isolated 3D, with
burning/diffusion coupling. CPU/CUDA 1D isolated spherical/cylindrical gravity
has radial field and AMR acceptance. Tested CPU/CUDA 2D full-azimuth polar and
3D cylindrical/spherical isolated gravity include origin, axis and pole AMR
chart mapping. The driver solves at each actual RK input,
invalidates after state changes and explicitly materializes fields for output.
CUDA allocation and launches live together in `cuda/runtime/gravity`.

See [GravityBox](../../../simulation/GravityBox/README.md), the
[Reference](../../../docs/Reference.md) and
[P5–P7 acceptance](../../../docs/development/P5P7GravityAcceptance.zh-CN.md).
