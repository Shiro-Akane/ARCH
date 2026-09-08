# Sod shock tube

[Sod.cpp](Sod.cpp) registers `Sod` and initializes left/right primitive states on
a one-dimensional Cartesian grid. The initial discontinuity exercises shocks,
contacts and rarefactions through the common hydro driver.

- [Sod_beginner.par](Sod_beginner.par): small introductory case used by the
  [simulation guide](../../docs/guides/SimulationCase.md).
- [Sod.par](Sod.par): reusable standard example.
- [Hydro validation](../../validation/hydro/README.md): reference solution,
  convergence inputs and measured results.

Parameter files select reconstruction, flux and time integration; the problem
does not contain a separate numerical solver.
