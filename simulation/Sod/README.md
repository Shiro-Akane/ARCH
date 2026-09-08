# Sod shock tube

[Sod.cpp](Sod.cpp) registers `Sod` and initializes left/right primitive states on
a one-dimensional Cartesian grid. The initial discontinuity exercises shocks,
contacts and rarefactions through the common hydro driver.

- [Sod_beginner.par](Sod_beginner.par): small introductory case used by the
  [simulation guide](../../docs/guides/SimulationCase.md).
- [Sod.par](Sod.par): reusable standard example.
- [Hydro validation](../../validation/hydro/README.md): reference solution,
  convergence inputs and measured results.

Note that the parameter files are solely responsible for selecting the reconstruction method, flux calculation, and time-integration scheme; this problem definition itself does not contain any separate numerical solver logic.
