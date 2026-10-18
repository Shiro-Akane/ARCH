# Curvilinear finite-volume methods

This directory owns shared coordinate layouts, measures, geometric sources and
stress mathematics. CPU traversal and CUDA kernels consume the same bodies.
Grid storage/topology, EOS, gravity, boundary callbacks and AMR scheduling have
their own owners.

| Chart | Physical component order | Representation |
| --- | --- | --- |
| Cartesian, 1–3 dimensions | x, y, z | Conservative cell means |
| Cylindrical radial 1D or full 3D | r, z, phi | Orthonormal momentum |
| Spherical 2D polar plane | r, phi, z | Polar-plane conservative means |
| Spherical radial 1D or full 3D | r, theta, phi | Spherical conservative means |

Two-dimensional cylindrical grids are unsupported. Both their former `(r,z)`
chart and the older cylindrical polar alias are retired; spherical 2D remains
the `(r,phi)` route.

- [GridGeometryView.h](GridGeometryView.h) defines component and derivative
  roles without owning fields or EOS resources.
- [GridMetrics.h](GridMetrics.h) supplies physical volumes, areas and lengths.
- [GeometricSources.h](GeometricSources.h) supplies coordinate momentum terms.
- [CurvilinearViscousStress.h](CurvilinearViscousStress.h) binds metric gradients,
  face traction and work to the shared Stokes constitutive law.

PCM/MUSCL/PPM reconstruction, point flux and conservative limiting stay in
[Reconstruction.h](../reconstruction/Reconstruction.h) and
[InvariantDomainFlux.h](../flux/InvariantDomainFlux.h). Runtime owns completed
state acceptance. See the [ownership map](../../../docs/development/ImplementationOwnership.md)
for callers and resource lifetimes. Optional gravity introduces no geometry
implementation or required geometry cache.
