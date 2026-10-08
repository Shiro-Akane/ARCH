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
| Custom cases | Define `Setup`, `Init` and parameters through `<UserInterface.h>` and `<GlobalDefs.h>` | The [case guide](guides/SimulationCase.md) gives the workflow |

General radiation transport is outside the current feature set. Thermal diffusion and radiation thermodynamics in an EOS follow their respective models.

Small curved cells near the origin or poles can substantially restrict timesteps and increase AMR evolution cost; near-origin accuracy limits depend on the tested problem. Releases use the current mesh, with radial mapping and AMR mesh redesign considered separately.

## Self-gravity domains

`gravity_type=self` uses the same Poisson mathematics and composite AMR path on CPU and CUDA. Fluid-face boundaries must match `gravity_boundary` and the mesh topology.

| Geometry | Validated gravity boundary and dimension | Key condition |
| --- | --- | --- |
| Cartesian | Periodic in one to three dimensions; isolated in three dimensions | Periodic gravity removes volume-mean density; isolated gravity uses a finite-mass boundary |
| Cylindrical/spherical | Isolated radial one-dimensional domains | Nonnegative radius and a reflecting inner fluid face |
| Two-dimensional spherical polar | Isolated domains spanning a full azimuth | `(r,phi)` covers a complete turn; azimuthal fluid faces are periodic and the inner radial face reflects |
| Axisymmetric cylindrical 2D `(r,z)` | Scientific qualification in progress | Host prescribed potential/flux and user gravity boundaries have short dynamic-AMR and strict-restart evidence; isolated continuum fields, gravitational energy, reacting dynamic AMR and CUDA remain under qualification, with Runtime capability checks |
| Three-dimensional cylindrical/spherical | Isolated domains spanning a full azimuth | Cylindrical `(r,z,phi)` or spherical `(r,theta,phi)`; axis/pole joins use their reflecting conditions |
| All geometries, 1D–3D | Prescribed potential/normal gradient/linear Robin and user boundaries | Valid physical domains, annuli or sectors; periodic pairs match AMR topology; pure Neumann requires Gauss compatibility |

The established Cartesian, radial and full-azimuth scopes have elliptic-field, AMR, restart and selected coupled checks. Historical cylindrical 2D polar records retain their original chart and do not qualify axisymmetric `(r,z)`. Per-side Dirichlet, Neumann, coercive linear Robin and user callbacks support all three geometries and valid sectors; isolated mass models still require full azimuth. External mass sources remain outside self gravity. See [user boundaries](guides/UserBoundaries.md) for transport channels and backend costs. Representative CPU/CUDA runs combine fluid dynamics, thermal diffusion, burning, self gravity and AMR; changing the models calls for the relevant [combination checks](Reference.md#combining-methods-and-physics). See [gravity parameters and interfaces](Reference.md#eos-and-gravity) for boundaries, residual controls and output fields, and [gravity validation](../validation/gravity/README.md) for measured scope.

The Jeans field and refinement indicator `JENS` are wired on CPU, with current validation covering a uniform periodic constant-specific-heat IdealGas background and Host prescribed-boundary RZ initial resolution repair, short output evolution and strict restart. Explicit Cartesian CUDA wiring is an engineering candidate awaiting final GPU validation; general EOS, nonuniform gravity coupling and the complete RZ scientific exits remain unqualified. Selecting this indicator requires self gravity and an explicit backend; `auto`, curved-coordinate CUDA and invalid combinations are rejected. See [AMR and plot variable vocabulary](Reference.md#amr-and-plot-variable-vocabulary) for configuration conditions and `jeans_cells`.
