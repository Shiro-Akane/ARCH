# ARCH Verification and Validation

Chinese translation: [README.zh-CN.md](README.zh-CN.md).

This directory describes numerical tests for hydrodynamics, diffusion, gravity,
burning, AMR, EOS, restart and generated networks. Each module guide explains
its reference solution, error budget, results and reproduction inputs. Start
with the [feature list](../docs/Features.md) for capabilities, the
[reference](../docs/Reference.md) for parameter combinations, and
[platform support](../docs/releases/PlatformSupport.md) for tested Linux and
Windows configurations.

## Test coverage

Convergence checks measure how errors change with mesh or timestep refinement.
Conservation checks account for physical sources and boundary transport.
Independent analytic or manufactured references test numerical accuracy;
CPU/CUDA comparisons test agreement between execution backends. The linked
records identify the source, binary, input and data used for each measurement.

<a id="completed-results-on-the-release-candidate"></a>

| Area | Recorded checks | Record |
| --- | --- | --- |
| Native cylindrical 2D | Real walls, three momenta, RK/RKL, AMR transactions, prescribed Poisson, seven CPU/GPU pairs, restart from a new process, memcheck and synccheck; bounded scope, original angular-momentum budget | [Device results](backend/results/release-readiness/Cyl5NativeDevice-20261019.json) |
| Native cylindrical scientific cases | 15 original-time axial cases, 306 legal method/EOS pairs, strong/weak source admission, required coupled endpoints and multiphysics restart from a new process; documented table domains and error budgets | [Scientific results](backend/results/release-readiness/Cyl6ComprehensiveAdmission-20261019.json) |
| Smooth hydro | PCM/MUSCL/PPM spatial convergence at 64/128/256 cells; independent Euler/RK2/RK3 time accuracy; 1,016-step periodic advection | [hydro](hydro/README.md) |
| Riemann and strong shocks | Sod and planar Sedov analytic profiles, three-resolution convergence and shock positions | [hydro](hydro/README.md#sod-shock-tube) |
| Species diffusion | RKL1/RKL2 applications at three resolutions; second-order RKL2 spatial convergence | [diffusion](diffusion/README.md) |
| External gravity | RK2/RK3 analytic source balance, including AMR and RKL2 species-diffusion coupling | [gravity](gravity/README.md) |
| Built-in burning | Six aprox13 application runs, twelve Host network/ODE temporal controls and separate CUDA policy checks | [burn](burn/README.md) |
| Dynamic AMR | Ten Cartesian and 24 curved cases; conservative transfer and thermal/viscous/species coupling, plus complete runtime refine/coarsen cycles in Cartesian 3-D and cylindrical/spherical 2-D | [AMR](amr/README.md) |
| Three-dimensional AMR instrumentation | Memcheck and racecheck each pass seven CUDA executions covering complete eight-child refine/coarsen/refine transitions; clean reports with original field and conservation checks | [AMR device checks](amr/README.md) |
| Coupled curved AMR instrumentation | Memcheck and racecheck each pass two spherical three-dimensional thermal/viscous/species CUDA runs on mixed-level meshes; clean reports with original five-stage RKL2, field and physical-volume conservation checks | [AMR device checks](amr/README.md) |
| EOS | Twelve normalized-table application cases and 24 physical endpoints, plus independent Helmholtz and manufactured thermodynamic controls | [EOS](eos/README.md) |
| HDF5/restart | Four backend directions from intermediate and terminal checkpoints; native composition, controller and output continuity | [restart](restart/README.md) |
| Burn/AMR restart instrumentation | Memcheck and racecheck each pass six actual CUDA runs and all nine strict comparisons; complete clean reports with native fields, controller and output-phase checks | [restart safety records](restart/README.md#completed-checks-on-the-release-candidate) |
| Generated networks and sparse burn | Six actual generated/weak application routes, three-ODE KLU/cuDSS trajectories and independent weak references | [network](network/README.md) |
| Built-in NSE | Sixteen application cases and 32 physical endpoints, with independent equilibrium and source-energy checks | [burn](burn/README.md) |
| Geometry | Twelve high-precision metric references; thermal/species/viscous spatial convergence and radial-origin stability | [AMR](amr/README.md#independent-geometry-and-diffusion-checks) |
| Sustained execution | 500 hydro and 100 diffusion AMR steps; twelve cycles per restart chain; 72 exact native burn-state replays and seven fixed-time burn comparisons | [AMR](amr/README.md#sustained-regridding-and-continuation), [restart](restart/README.md#sustained-native-restoration) |

Native cylindrical 2D uses the axisymmetric `cylindrical-rz-pwc` chart, with
three physical momentum components. The linked native-chart records include
axial cases and coordinate-wall checks. Older cylindrical polar-plane records
use a different chart and remain attached to their measured source.

The [CPU/CUDA integration results](backend/results/release-readiness/REL4CudaDeviceAndIntegration-20261019.json)
include 17 resource/batch contracts, nine legal coordinate-wall combinations,
public application endpoints, regridding and input rejection. In the recorded
24-step Cellular run, the ENUC comparison passes the `5e-9` relative plus
`5e-12` absolute budget at all 25 checkpoints, with byte-identical values.
Three rotating-AMR pairs pass their `1e-12` budget; both backends also pass
continuation from a new process. The [burn guide](burn/README.md) explains
which energy diagnostic ENUC represents.

## Run the tests

Follow [Build](../docs/guides/Build.md#build-tests-only-when-you-need-them) to
configure a test build. Inspect the tests registered for that build and run
the complete selected suite:

```sh
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

Module guides provide application-level commands and inputs. Python validators
use NumPy and h5py; tests with independent ODE or equilibrium references list
their additional dependencies. CPU/CUDA comparisons require both binaries and
the same physical input. Use a new output directory for each measurement.
The [backend validation guide](backend/README.md) describes group selection,
coupled runs and timing instrumentation. Hosted checks are described in the
[CI guide](../.github/workflows/README.md).

## Error conventions

Verification compares the implementation with analytic, manufactured or
independently converged solutions. Validation against experimental or published
physical data is identified separately. FLASH comparisons are optional
performance studies; ARCH tests do not require a FLASH installation.

For cell-volume-weighted field error,

$$
L_1(q)=\frac{\sum_i V_i\lvert q_i-q_i^{ref}\rvert}{\sum_i V_i},
\qquad
L_2(q)=\sqrt{\frac{\sum_i V_i(q_i-q_i^{ref})^2}{\sum_i V_i}}.
$$

The observed rate between resolutions $N$ and $2N$ is
$p=\log_2(E_N/E_{2N})$. Each record specifies any alternative norm or
normalization. AMR comparisons place numerical and reference fields on a common
mesh and use physical cell volumes. Trajectory comparisons use the same
prescribed physical time; exact checkpoint restoration separately compares
native state, topology and controller metadata.

## Add or reproduce a result

Use [CASE_TEMPLATE.md](CASE_TEMPLATE.md) to record:

1. the equations, modules, dimension, geometry and property under test;
2. the source commit, binary, compiler flags, backend, thread count and hardware;
3. input files and checksums for external EOS or reference data;
4. configure, build and run commands, reference provenance and sampling rules;
5. field errors, residuals or invariants, acceptance tolerances and pass/fail results;
6. CSV/JSON metrics, useful figures, and any safeguard activation or limitation.

Each module keeps its summary, `inputs/`, metrics and optional `figures/`
together. Example simulations belong in `simulation/`; runtime tables belong
in `EOS_toolkit/`. Update a result's input, metrics and summary together when
its definition changes. Source-identified measurements remain reproducible
through their recorded commit and data checksums.

Git stores compact metrics, reference samples, parameter files and reproduction
scripts. Large checkpoints, plotfiles and complete execution traces are
indexed separately; see [data availability](archive/README.md).

## Interpreting the results

A component test establishes the combinations it exercises. The [EOS guide](eos/README.md)
distinguishes normalized-table applications from focused native-table loading,
component-completion and inversion checks. The [network guide](network/README.md)
separates representative generated/weak networks from large-network trajectories
and resource measurements. For GPU performance, use the
[CUDA guide](../docs/CudaBackendStatus.md) and compare complete runs of a
representative input on your hardware.

<a id="final-candidate-verification"></a>

## Detailed verification records

The following records retain their measured sources and error budgets:

- [Uniform application matrix](backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json): 22 cases, 180 executions and 90 comparisons.
- [Generated-network matrix](network/results/runtime-native-20260907/release-875/backend-validation-evidence.json): six cases, 48 executions and 24 comparisons, including CPU KLU and GPU cuDSS for the 32-equation audit31 network.
- [Device instrumentation](backend/results/final-first-law-20260907/README.md): memcheck and racecheck each cover 23 routes, including three ODEs, both sparse storage sizes and four subdivisions. Scientific runs and memcheck use `1e-10 s`; racecheck uses a separate `1e-12 s` observation interval.
- [Three-dimensional AMR lifecycle](amr/results/dynamic-3d-final-20260907/release-919/evidence.json) and [curved AMR lifecycle](amr/results/dynamic-curved-final-20260907/release-923/evidence.json).
- [Build measurements](backend/results/cold-core-first-law-20260907/release-909/README.md): cold, incremental and no-op builds, including a two-heavy-job/four-total-job comparison.
- [Memory capacity](backend/results/device-memory-first-law-20260907/README.md): regrid migration, a 16,384-row cuDSS matrix, generated burning and AMR, with observed dynamic allocations released.
- [GPU performance measurements](backend/results/hpc-cuda-optimization/README.md) and [ARCH/FLASH comparison](gravity/flash/README.md).
