# Shared curvilinear-metric correction and qualification

Status: originally deferred on 2026-09-05; reopened explicitly by the owner on
2026-09-06 for the release refactor. Shared metric and covariant vector-diffusion
corrections are implemented. Independent CPU/CUDA area/length/CFL/rest-balance
and species/viscous spatial convergence tests pass. Time-evolved coupled AMR
qualification remains open.

## Corrected convention

Fields/fluxes store orthonormal components. In spherical 3D, with
I1=(r_right^2-r_left^2)/2 and I2=(r_right^3-r_left^3)/3:

- radial face area: r_face^2 (cos(theta_left)-cos(theta_right)) dphi;
- theta face area: I1 sin(theta_face) dphi;
- phi face area: I1 dtheta; volume: I2 (cos(theta_left)-cos(theta_right)) dphi.

Physical CFL/gradient spacings share `GridMetrics::PhysicalSpacing`: Cartesian
(dx,dy,dz), cylindrical 3D (dr,dz,r*dphi), spherical 3D
(dr,r*dtheta,r*sin(theta)*dphi); both 2D curved specializations use (dr,r*dphi).
No arbitrary angular length floor is substituted for an active cell center.

The geometric source explicitly uses the mathematically exact volume average of `1/r`: `I1/I2` in spherical 1D/3D, and `dr/I1` in cylindrical/polar cells. Furthermore, the volume-weighted angular cotangent simplifies cleanly to `cot(theta_center)` on this uniform angular grid. This change not only fixes the angular dimensions but crucially corrects the former spherical constant-pressure rest imbalance. As a result, the independent source fixture on the interval `r=[1,3]` is updated from `1/2` to `6/13`. These expected values are derived completely analytically, rather than being regenerated from solver output.

These measures follow direct integration of the orthogonal metric factors;
the [Athena++ spherical-coordinate implementation](https://github.com/PrincetonUniversity/athena/blob/main/src/coordinates/spherical_polar.cpp)
is an external convention cross-check, not copied implementation code. Flux,
diagnostics, AMR/reflux and CUDA metric caches consume the shared areas; the
former diffusion-only spacing wrapper now delegates to the shared authority.

## Questions to resolve

1. **Three-dimensional spherical angular face areas.**
   `src/grid/GridMetrics.h`, `FaceArea`, formerly used
   `radial_shell_volume` for both angular directions. This factor has dimensions
   of length cubed, whereas an ordinary physical face area has dimensions of
   length squared. Review the finite-volume flux and orthonormal-component
   conventions together before selecting a correction.
2. **Angular CFL lengths.**
   `DriverUtils::compute_cfl_candidate` formerly received native `dx2`/`dx3`.
   Review the corresponding physical propagation lengths, including radius and
   polar-angle dependence, all supported dimensional specializations, and the
   existing treatment of singularities and ghost cells.

The one-dimensional annular refactor cases exercise neither angular question.
A CPU/CUDA parity result cannot by itself establish scientific correctness.

## Implementation boundary

### Additional shared viscous-vector defect (2026-09-06)

The local independent constant-vector diagnostic (`build/` logs 480/481,
details in the release ledger) shows a non-vanishing diffusion term for a
spatially uniform Cartesian velocity expressed in polar components. Halving
the spacing does not remove the error. Both CPU and CUDA consume the same
incomplete component-gradient/diagonal-source model; backend parity alone
therefore cannot qualify this operator.

For example, cylindrical vector diffusion includes angular cross-component
derivatives, not just scalar Laplacians and diagonal inverse-radius terms.
See the defining [vector-Laplacian formulas C.52–C.54](https://farside.ph.utexas.edu/teaching/336L/Fluid/node257.html).
The correction must include coordinate-basis derivatives in face gradients and
their matching divergence/source terms, including variable transport
coefficients and the corresponding conservative energy flux. Preserve the
project's 2D polar coordinate convention. This is not an implementation of a
new full Navier–Stokes symmetric-stress model; the existing diffusion model
must first be geometrically consistent. Independent null-field and spatial
refinement tests, followed by actual CPU/CUDA coupled runs, are required.

Correction contract: for each orthonormal direction d, form the covariant
velocity gradient `D_d(v) = partial_d(v) + C_d(v)`, where `C_d` is the basis
rotation per physical distance. The momentum face flux is `F_d = -mu*D_d(v)`;
the component-wise conservative divergence needs the matching source
`-sum_d C_d(F_d)`. Use the same basis-rotation body at faces and cells. The
energy face flux remains `dot(F_d, v_face)`, so no separate nonconservative
energy correction is introduced. Face coefficients retain the derivative of
variable `mu = rho*nu`; adding only constant-coefficient cross terms to the
old source would be incomplete.

Independent test inputs in `tests/math/ViscousGeometryCases.h` use a uniform
Cartesian vector and a quadratic Cartesian field with constant or linear
density, plus radial cubic flow for the 1D symmetry reductions. Analytic
momentum and work-flux derivatives are derived in Cartesian/radial form, not
from the production connection implementation. Both backends pass 30 profiles
at three spacings in Release; the largest finest-grid normalized error is
7.26751e-5 (fixed budget 1e-4), with at least 3.5x error reduction per halving
above roundoff. The current [spatial evidence](../../validation/amr/results/curved-viscosity-release-20260907/evidence.json)
records actual artifact identity and transcripts. CUDA memcheck reports zero
errors. Full coupled/time-dependent validation remains a separate gate.

### Radial-origin balance and diffusion stability

The additional origin witness uses `v=r e_r`, a Cartesian linear vector field.
Its momentum Laplacian is zero and its conservative work divergence is `d*nu`
in physical dimension d. Before correction, the first spherical cell has an
error proportional to `1/dr` (diagnostic-495). The viscous divergence connection
now consumes `GridMetrics::InverseRadiusVolumeAverage`, exactly as the hydro
source does. The inner velocity gradient still uses the cell-center radius.
This changes the hand-derived spherical shell source fixture by `12/13` on
`r=[1,3]`; no empirical correction or backend-dependent tolerance is used.

The old Cartesian diffusion timestep also misses geometric damping. Matrices
assembled by applying the actual operator to unit vectors have default-CFL
forward-Euler amplification 1.54609 (cylindrical) and 3.46425 (spherical) in
diagnostic-498. The corrected frozen-coefficient rate uses physical face
area/volume/spacing, plus the same vector-connection bounds. In radial 1D this
is the scalar stencil diagonal plus angular damping. Diagnostic-500 has
amplification below one at all three spacings and origin momentum at roundoff.
Retained CPU/CUDA controls additionally check that the actual radial forward-
Euler matrix has nonnegative entries and row sums at most one; this establishes
max-norm contraction without calling the timestep formula for an oracle.
Multidimensional coupled evolution and nonlinear/variable-coefficient stability
remain separate validation work; a frozen-coefficient bound is not that proof.

Subsequent actual-matrix control-513 shows that alternating rho=1/100 is still
unstable if dt ignores face density. The scalar frozen-transport diagonal is
`sum_faces(A * transport_face / (V * spacing * capacity_cell))`, with capacities
rho for velocity/species and rho*cv for temperature. The same face EOS and
transport-coefficient helper now serves the operator and timestep. Geometric
face rotation and cell-source bounds are added only for viscosity. Probe-515
passes the original variable-density spectral witness; retained CPU/CUDA tests
check nine geometry/density-contrast matrices per backend.

This also exposes an inconsistent historical cutoff: the flux applies positive
coefficients below 1e-12, while dt used to treat them as zero. Any positive
coefficient now contributes to the stability bound. The density and cv floors
already present in the operator are separate and unchanged. Original constant-
capacity dt numbers remain checked on the uniform-coefficient formula. Varying-
capacity timestep tests use an independently derived long-double Cartesian
face/cell-capacity reference, not numbers captured from the repaired solver.
Original flux, spatial-operator and RKL-state fixtures remain unchanged.

The new neighbour reads require current ghosts. The driver uses its existing
physical/same-level/coarse-fine exchange before dt; CUDA performs these exchanges
on device without full-state Host materialization. RKL and application/restart
reruns are required for this changed timestep contract.

### Shared metric and implementation rules

- Resolve the mathematical convention first; retain the documented two-dimensional
  polar specialization unless a separately approved change requires otherwise.
- Correct the shared metric/CFL authority once. CPU and CUDA consume that same
  implementation; do not add backend-specific numerical patches.
- Audit downstream hydro divergence, geometric sources, diffusion, AMR indicator
  diagnostics, coarse/fine flux exchange, reflux and conservation measurements
  for consistency with the chosen convention.
- Identify changes to stable timesteps and historical reference results explicitly.
  Do not refresh reference values or loosen tolerances merely to obtain a pass.

## Acceptance evidence

- Independent dimension and analytic-geometry checks for each affected face and
  direction, including limiting cases away from and near coordinate singularities.
- Multi-dimensional equilibrium/convergence checks that isolate angular terms,
  followed by mixed-level AMR conservation and reflux checks.
- CFL checks against physical propagation lengths at different radii/angles.
- CPU and real-CUDA execution of the same fixtures after the shared correction,
  with fresh source/binary/input identities and documented reference changes.

Track this separately from the current [refactor smoke record](../development/CudaRefactorSmoke.md).
