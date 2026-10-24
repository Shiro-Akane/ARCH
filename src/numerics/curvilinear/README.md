# Curvilinear finite-volume methods

This directory owns shared coordinate layouts, measures, geometric sources and
stress mathematics. CPU traversal and CUDA kernels consume the same bodies.
Grid storage/topology, EOS, gravity, boundary callbacks and AMR scheduling have
their own owners.

| Chart | Physical component order | Representation |
| --- | --- | --- |
| Cartesian, 1–3 dimensions | x, y, z | Conservative cell means |
| Cylindrical radial 1D or full 3D | r, z, phi | Orthonormal momentum |
| Cylindrical axisymmetric 2D | r, z, phi | Two spatial directions; all three physical momenta |
| Spherical 2D polar plane | r, phi, z | Polar-plane conservative means |
| Spherical radial 1D or full 3D | r, theta, phi | Spherical conservative means |

Native axisymmetric geometry and bounded Host evolution use these same headers.
Optional Host gravity is composed through the existing
[gravity binding](../../physics/gravity/HydroSourceBinding.h); its field,
three-dimensional isolated ring physics and workspace stay with that owner.
Public CPU configuration, new-chart checkpoint/Plot and Studio paths are qualified
by the [release qualification index](../../../docs/releases/README.md), together with scoped native
Device and method/EOS admission. Each receipt retains its tested combinations
and original limits; coordinate admission is not an unrestricted release.
The old cylindrical polar alias remains retired; spherical 2D is the `(r,phi)` route.

For native `(r,z)`, physical PWC momentum remains `(rho*ur,rho*uz,rho*uphi)`.
Ordinary fields use volume `V=integral r dr dz` and face area `A`; azimuthal
momentum uses `W=integral r^2 dr dz` and torque measure `T=integral r dA`, so
`J=sum W*Mphi`. No evolved angular-momentum array or separate solver is added.
The applied centrifugal quadrature is paired with this torque transport and
with authenticated reflecting faces. Pressure retains its ordinary geometric
source, and neither a second azimuthal source nor an energy source is added.
The analytical point-source helper remains a separate oracle; the applied
interior quadrature is second-order consistent, while the one-cell wall closure
has a shrinking first-order integral contribution. This does not claim an
exactly well-balanced rotating equilibrium scheme.

- [GridGeometryView.h](GridGeometryView.h) defines component and derivative
  roles without owning fields or EOS resources.
- [GridMetrics.h](GridMetrics.h) supplies physical volumes, areas and lengths.
- [GeometricSources.h](GeometricSources.h) supplies point and transport-compatible
  coordinate momentum terms, plus the local centrifugal-impulse timestep bound.
- [CurvilinearViscousStress.h](CurvilinearViscousStress.h) binds metric gradients,
  face traction and work to the shared Stokes constitutive law.

PCM/MUSCL/PPM reconstruction, point flux and conservative limiting stay in
[Reconstruction.h](../reconstruction/Reconstruction.h) and
[InvariantDomainFlux.h](../flux/InvariantDomainFlux.h). Runtime owns completed
state acceptance. See the [ownership map](../../../docs/development/ImplementationOwnership.md)
for callers and resource lifetimes. Optional gravity introduces no geometry
implementation or required geometry cache. Host stages, AMR/RKL and optional
gravity have scoped evidence in the release plan. Native Device walls, indices,
resident RK/RKL, AMR migration, streams and fresh now have actual-device checks;
the full method/EOS scientific matrix remains separate. Physical wall selection
belongs to BCHandler and topology preparation; these numeric leaves consume the
prepared view without discovering boundary policy or optional gravity.

## Axisymmetric cylindrical contract

The `(r,z)` chart is the axisymmetric reduction of the existing `(r,z,phi)`
layout: `d/dphi=0` removes a spatial derivative, while all three orthonormal
momenta remain. Spherical 2D `(r,phi)` is a common-flow and cost reference;
its planar gravity is not the three-dimensional axisymmetric Newton field.

| Quantity | Convention |
| --- | --- |
| Axes and state | `x1=r`, `x2=z`, both lengths; `dx3=0`. Momentum is `(rho*ur,rho*uz,rho*uphi)`; `mom_w` is azimuthal momentum density. Retain the established cylindrical orientation in curls and cross products. |
| Volume and areas | `Q=(rR-rL)*(rR+rL)/2`, `V=Q*dz`, `A_r=r_face*dz`, `A_z=Q`, `<1/r>=(rR-rL)/Q`. Internal measures are per unit azimuth; `V` has units `cm^3`, not `cm^2`. Full-ring integrals explicitly multiply by `2*pi`. |
| Angular-momentum moment | `W=dz*(rR-rL)*(rR^2+rR*rL+rL^2)/3`, `J=sum(mom_w*W)`. Torque measures are `T_r=r_face^2*dz`, `T_z=W/dz`. Do not replace `W` by `r_mid*V` or add an evolved angular-momentum field. PWC storage and point-sampled initialization do not promise exact cell averages of arbitrary analytic profiles. |
| Euler terms | The continuous point expressions are `S_r=(rho*uphi^2+p)*<1/r>`, `S_z=0`, `S_phi=-rho*ur*uphi*<1/r>`. The transport-compatible update uses `DeltaJ=-dt*sum_outward(T*F_phi)+external_torque_impulse`, `Delta mom_w=DeltaJ/W`; it already incorporates the azimuthal connection term. There is no extra `S_phi`, mass source or geometric total-energy source. The radial implementation retains the paired quadrature described above. |
| Axis regularity | `r=0` is a cell face with zero radial area. Scalars and `uz` are even; `ur` and `uphi` are odd. Shared seams/physical boundaries own ghost and donor preparation. Do not insert a radial epsilon or apply a half-turn azimuth map to `z`. |
| Optional gravity | Geometry supplies measures and regularity, never field selection or solver ownership. Gravity owns solve/boundary/work/release; disabling it performs no solve or dedicated numerical allocation. Full-ring mass in its boundary moments uses `2*pi*V`; Poisson keeps `-4*pi*G*rho`. Existing `g_x/g_y/g_z` slots mean `g_r/g_axial/g_phi` here; an axisymmetric self-field has `g_phi=0`. |
| Public identity | Chart `cylindrical-rz-pwc`, revision `1`; axes `r,z,inactive`, units `cm,cm,inactive`, volume `cm3/per_unit_azimuth`; the third physical velocity remains `uphi`. Reject retired `axisymmetric-rz` products and malformed active inputs. Native CHK has no new build/source/profile gate. |

The original angular-momentum and torque-budget standard remains `1e-12`.
Metric-leaf checks retain their own `2e-12` bound and independent integrals;
that bound is not a universal physical tolerance. Near-zero high-pressure raw
momentum parity and SNIa qualification limits remain in the release plan.
Model centers are initialized by their model owners from Grid's `(r,0,z)` point;
they do not belong to a geometry or gravity cache.

The frozen [contract and baseline](../../../validation/backend/results/release-readiness/Cyl0ContractAndBaselines-20261018.json),
[metric](../../../validation/backend/results/release-readiness/Cyl1SharedGeometry-20261018.json),
[conservative transport](../../../validation/backend/results/release-readiness/Cyl2GravityOffConservation-20261018.json)
and [optional-gravity](../../../validation/backend/results/release-readiness/Cyl3OptionalGravity-20261018.json)
receipts define their original scope. Public interfaces, native Device execution
and method/EOS admission are recorded separately by the
[interface](../../../validation/backend/results/release-readiness/Cyl4PublicInterfacesAndFresh-20261018.json),
[Device](../../../validation/backend/results/release-readiness/Cyl5NativeDevice-20261019.json)
and [comprehensive-admission](../../../validation/backend/results/release-readiness/Cyl6ComprehensiveAdmission-20261019.json)
receipts. No retired cylindrical-specific Grid, flux, state, rollback, cache or
control infrastructure is required.
