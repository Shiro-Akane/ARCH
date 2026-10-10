# ARCH feature list

[中文](Features.zh-CN.md) · [Case guide](guides/SimulationCase.md) · [Parameter and API reference](Reference.md) · [Validation scope](../validation/README.md)

Use this page to choose the capabilities needed for a simulation. The reference defines configuration and combination rules; validation records identify the tested scientific scope. Inputs, outputs and physical constants use CGS units.

| Area | Available capability | Check when choosing a case |
| --- | --- | --- |
| Fluid dynamics | Compressible finite-volume flow in one to three dimensions; SW, VL, Roe, HLL and HLLC fluxes; PCM, MUSCL/PLM and PPM reconstruction; Euler, SSPRK2 and SSPRK3 steps | Flux/EOS compatibility, coarse/fine face reconstruction and coupled temporal order |
| Mesh and boundaries | Cartesian, cylindrical and spherical block grids; periodic, outflow, reflecting, prescribed inflow and user physical boundaries | Two-dimensional cylindrical grids use axisymmetric `(r,z)`; two-dimensional spherical grids use the equatorial `(r,phi)` plane; three-dimensional cylindrical grids use `(r,z,phi)` |
| Adaptive mesh | Dynamic refinement/coarsening, conservative state migration, block exchange and hydro/diffusion reflux | Applicability of the selected refinement indicator to the physical problem |
| Equation of state | Ideal gas, Helmholtz, normalized three/four-dimensional free-energy tables, and supported native EOSDriver and baryon tables | Declared table components, valid domain and temperature inversion |
| Diffusion | Thermal, viscous and species channels with RKL1/RKL2 integration | The selected material model supplies the coefficients; the Helmholtz stellar branch supplies thermal conduction |
| Nuclear reactions | iso7, aprox13, aprox19 and aprox21; generated networks meeting the package contract; BE_NR, ROS4 and BD; NSE for qualified networks | Device-callable generated math and compatibility between the network and EOS |
| Linear solves | DenseLU for small burning ODE systems; KLU for larger sparse CPU burning; optional cuDSS for sparse CUDA burning; composite multigrid for self gravity | Burn and Poisson solvers operate on different equations |
| Gravity | External acceleration and self-consistent potential, acceleration and energy work | See the self-gravity domain table below |
| Execution | CPU/OpenMP and CUDA backends share physics and mathematics; the host owns AMR topology while the device executes numerical work | Build and network capabilities select the backend; speed depends on workload size |
| Output and restart | HDF5 fields, checkpoints, solver and repair diagnostics; cross-backend restart for compatible configurations | EOS, network, geometry, gravity and control identity must agree on restart |
| Studio (optional) | Standalone Linux/WSLg window; configuration, build, run/restart, initial-field/AMR Preview and read-only Plotfile queries | Preview follows each model’s advertised capabilities; RZ initial AMR provides root-mesh snapshots. Plotfile overviews cover Cartesian 1D/2D and stored-cell queries cover recorded native 1D–3D charts. See the [Studio guide](guides/Studio.md) |
| Custom cases | Define `Setup`, `Init` and parameters through `<UserInterface.h>` and `<GlobalDefs.h>` | The [case guide](guides/SimulationCase.md) gives the workflow |

General radiation transport is outside the current feature set. Thermal diffusion and radiation thermodynamics in an EOS follow their respective models.

Small curved cells near the origin or poles can substantially restrict timesteps and increase AMR evolution cost. Assess near-origin accuracy with convergence checks for the chosen problem.

## Self-gravity domains

`gravity_type=self` uses shared Poisson mathematics and the composite AMR solver. Available domains and backends are listed below; gravity boundaries, fluid-face boundaries and mesh topology must be compatible.

| Geometry | Available capability | Conditions and validated scope |
| --- | --- | --- |
| Cartesian | Periodic 1D–3D; isolated 3D; CPU/CUDA | Periodic gravity removes volume-mean density; isolated gravity uses a finite-mass boundary |
| Cylindrical/spherical radial 1D | Isolated domains; CPU/CUDA | Nonnegative radius and a reflecting inner fluid face |
| Spherical polar 2D `(r,phi)` | Full-azimuth isolated domains; CPU/CUDA | Complete azimuth with periodic fluid faces and a reflecting inner radial face |
| Axisymmetric cylindrical 2D `(r,z)` | Host prescribed potential/flux and user gravity boundaries | Short dynamic AMR and strict restart are validated; isolated-field spatial convergence and a finite-step continuum energy reference have passed. Complete isolated evolution, reacting dynamic AMR and CUDA execution remain under validation |
| Cylindrical/spherical 3D | Full-azimuth isolated domains; CPU/CUDA | Cylindrical `(r,z,phi)` or spherical `(r,theta,phi)`; corresponding reflecting joins at axes/poles |
| Supported 1D–3D scopes of all three geometries | Prescribed potential/outward normal gradient/linear Robin and user boundaries | Nonperiodic domains, annuli or sectors; periodic directions match AMR topology and pure Neumann requires Gauss compatibility. Backend scope follows the geometry rows above |

External mass sources are outside self gravity. Representative CPU/CUDA runs combine fluid dynamics, thermal diffusion, burning, self gravity and AMR; see [combining methods and physics](Reference.md#combining-methods-and-physics) for model and method conditions. See [user boundaries](guides/UserBoundaries.md) for interfaces and [gravity validation](../validation/gravity/README.md) for tested configurations, error budgets and processed evidence.

The Jeans field and refinement indicator `JENS` support tested CPU evolution/restart of uniform periodic constant-specific-heat IdealGas backgrounds, and Host prescribed-potential axisymmetric initial refinement, short output evolution and strict restart. Explicit Cartesian CUDA wiring is implemented but awaits final GPU scientific validation; applicability to general EOS and nonuniform gravity coupling remains under validation. See [AMR and plot variable vocabulary](Reference.md#amr-and-plot-variable-vocabulary) for configuration conditions and `jeans_cells`.
