# Function-form user boundary example

[case.cpp](case.cpp) registers `UserGravity` with `REGISTER_PROBLEM` from the
free functions `UserGravitySetup` and `UserGravityInit`, and initializes the
same uniform one-dimensional state as the class-form example (`rho=1 g/cm^3`,
`p=1 erg/cm^3`, zero velocity, one neutral species with `Cv=1 erg/(g K)`,
`A=1`, `Z=0`). Setup validates geometry, gamma and the consistent ideal-gas
temperature `T = p / ((gamma-1) rho Cv)`.

The same problem name publishes both free-function user callbacks:

- [physical_boundary.cpp](physical_boundary.cpp) registers `UserGravityPhysical`
  with `REGISTER_PHYSICAL_BOUNDARY`. A `Hydro` request returns the complete
  uniform interior primitive; a `Diffusion` request fills only the temperature
  channel with a desired `Value` or, when the documented optional
  `user_boundary_heat_flux` key is nonzero, the outward heat flux
  `q(t) = q0 * (1 + t / tau)`, with `q0` in `erg/(cm^2 s)` and the fixed
  example scale `tau = 1 s`. Core supplies the actual RK/RKL stage time.
  Velocity and species channels stay unset. The class-form example instead
  demonstrates a constant heat flux.
- [gravity_boundary.cpp](gravity_boundary.cpp) registers `UserGravityPotential`
  with `REGISTER_GRAVITY_BOUNDARY`. Physical potential faces receive per-side
  Dirichlet values of the analytic uniform-density field
  `Phi = 2 pi G rho r^2 / d`, with `d = 1` slab, `2` cylinder, `3` sphere (`2` for a 2D equatorial polar grid) and
  `r` the native radius/x position of the face. `G` is the public CGS constant
  `arch::constants::gravity::cgs::gravitational_constant` from `<GlobalDefs.h>`;
  a gravity_G override is rejected so the values stay consistent with the solver.

[UserGravityCylinder.par](UserGravityCylinder.par) is a 1D cylindrical radial
smoke input: the `r=0` face stays `reflecting` as the natural regularity face
that Core skips, the outer face is `user`, `gravity_type = self`,
`gravity_boundary = user` and thermal diffusion is enabled. Output cadences are
disabled; enable `plt_dt` only when exploring interactively.

```
./bin/ARCH UserGravity simulation/UserGravity/UserGravityCylinder.par
```

The function-form case supports `geometry = cartesian`, `cylindrical` and
`spherical` in one to three dimensions; the free initializer has no access to
the configuration, so the uniform state is geometry-independent and the
geometry token is validated in Setup. Only the `user_boundary_heat_flux` key is
added beyond the standard parameters.

The uniform interior state and the uniform-density potential are initial
conditions for a short smoke run. This example does not claim a manufactured
self-consistent gravity solution, a converged result, or any validation of the
evolved state.
