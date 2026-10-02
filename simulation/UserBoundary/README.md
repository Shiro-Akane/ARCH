# Class-form user boundary example

[case.cpp](case.cpp) registers `UserBoundary` with `REGISTER_PROBLEM_CLASS` and
initializes a uniform one-dimensional state (`rho=1 g/cm^3`, `p=1 erg/cm^3`,
zero velocity, one neutral species with `Cv=1 erg/(g K)` and `A=1`, `Z=0`).
Setup validates the geometry, gamma and the consistent ideal-gas temperature
`T = p / ((gamma-1) rho Cv)` before any state is written.

The same problem name publishes both class-form user callbacks:

- [physical_boundary.cpp](physical_boundary.cpp) registers `UserBoundaryPhysical`
  with `REGISTER_PHYSICAL_BOUNDARY_CLASS`. A `Hydro` request returns the complete
  uniform interior primitive (density, three native velocities, pressure and
  composition). A `Diffusion` request fills only the temperature channel: a
  desired `Value` equal to the interior temperature, or an outward heat flux
  when `user_boundary_heat_flux` is nonzero. Velocity and species channels stay
  unset, so hydro and diffusion answers never mix.
- [gravity_boundary.cpp](gravity_boundary.cpp) registers `UserBoundaryGravity`
  with `REGISTER_GRAVITY_BOUNDARY_CLASS`. Each physical potential face receives a
  per-side Dirichlet value of the analytic uniform-density field
  `Phi = 2 pi G rho r^2 / d` with `d = 1` slab, `2` cylinder, `3` sphere (`2` for a 2D equatorial polar grid) and `r`
  the native radius/x position of the face. `G` is the public CGS constant
  `arch::constants::gravity::cgs::gravitational_constant` from `<GlobalDefs.h>`;
  a gravity_G override is rejected so the values stay consistent with the solver.

[UserBoundaryCart.par](UserBoundaryCart.par) is a 1D Cartesian slab smoke input:
both `x1` faces are `user`, `gravity_type = self`, `gravity_boundary = user` and
thermal diffusion is enabled. Output cadences are disabled; enable `plt_dt` only
when exploring interactively.

```
./bin/ARCH UserBoundary simulation/UserBoundary/UserBoundaryCart.par
```

The class-form case supports `geometry = cartesian`, `cylindrical` and
`spherical` in one to three dimensions; the initializer reads native `PointCoords`.
Core validates the physical radial domain and owns signed ghost chart joins. Only the
`user_boundary_heat_flux` key is added beyond the standard parameters.

The uniform interior state and the uniform-density potential are initial
conditions for a short smoke run. This example does not claim a manufactured
self-consistent gravity solution, a converged result, or any validation of the
evolved state.
