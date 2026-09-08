# ARCH: Adaptive Reactive CUDA Hydrodynamics

Chinese translation: [README.zh-CN.md](README.zh-CN.md).

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH is a framework for simulating the motion, heat transfer, and reactions of compressible fluids. It is built both for newcomers learning computational fluid dynamics (CFD) and for researchers looking to extend the underlying physical equations and numerical methods directly in the source code.

ARCH uses the finite-volume method: it divides the fluid domain into cells and tracks the exchange of mass, momentum, and energy between them. To capture fine details efficiently, adaptive mesh refinement (AMR) dynamically inserts smaller cells only where needed, avoiding the cost of a uniformly fine mesh. Both CPU and CUDA execution share the same mathematical and physical core; their respective backends simply handle how calculations are scheduled and where data is stored.

To run your first simulation, follow the [Build](#build) and [First run](#first-run) sections below. Afterward, the [Simulation Case Guide](docs/guides/SimulationCase.md) will walk you through reading the output, modifying parameters, and creating your own scenarios. Note that the beginner example doesn't require a GPU or any nuclear reaction networks.

## Project status

Both CPU and CUDA backends fully support the following features. The release version has successfully passed rigorous numerical, application, device-safety, build, and resource checks. Detailed testing configurations and the final delivery-review status are documented in the [Validation](validation/README.md) suite.

| Capability | Shared behavior and backend choices |
| --- | --- |
| Hydrodynamics | 1D, 2D and 3D Cartesian, cylindrical and spherical grids |
| Dynamic block AMR | Conservative refinement, coarsening, ghost exchange and flux correction. CUDA computes indicators and transfers cell data on the GPU; the CPU manages the mesh tree. |
| Equations of state (EOS) | Relations between density, temperature, pressure and energy: ideal gas, Helmholtz and 3D/4D tables |
| Diffusion | Thermal, viscous and species diffusion with RKL1/RKL2 time stepping |
| Gravity | Prescribed external gravity |
| Nuclear burning | Four built-in networks and generated pynucastro networks. Built-in networks also support nuclear statistical equilibrium (NSE), which determines composition from equilibrium conditions. |
| Linear solvers | DenseLU for small systems; KLU on CPU and cuDSS on CUDA for sparse systems |
| Output and restart | HDF5 plots and checkpoints use the same format on both backends, including the AMR hierarchy, burn energy and timestep-controller state. |

Set `compute_backend = cpu`, `cuda`, or `auto` in your parameter file to choose where the simulation runs. Requesting `cuda` explicitly will trigger an error if the build or hardware doesn't support it. With `auto`, ARCH will gracefully fall back to the CPU at startup if CUDA is unavailable but the CPU supports the requested features. The backend remains fixed once the run begins. The [CUDA guide](docs/CudaBackendStatus.md) details these choices and explains how the CPU and GPU cooperate during AMR.

Checkpoints save all the state information—including the mesh and fluid composition—needed to seamlessly resume a simulation. Because the CPU and CUDA backends share the exact same format, you can freely restart a simulation on a different backend. The [Reference Manual](docs/Reference.md) details the saved fields and the physical settings that must remain consistent when resuming.

For smaller AMR workloads, start with CPU and compare a representative run
before choosing CUDA for speed. The [backend performance guide](docs/CudaBackendStatus.md#choosing-a-backend-for-performance)
explains the measured CPU/CUDA comparison and how to interpret it.

## Implemented capabilities

ARCH offers SW and VL flux-vector splitting alongside Roe, HLL, and HLLC Riemann solvers to estimate transport across cell boundaries. For spatial reconstruction at cell faces, it supports PCM, MUSCL/PLM, and PPM. Time integration is handled by Euler, SSPRK2, or SSPRK3 schemes, while diffusion uses RKL1 or RKL2 super-time stepping. All of these numerical choices are completely independent of the selected CPU/CUDA backend.

Our included teaching cases are pre-configured with appropriate methods, so you can safely start there. The [Simulation Case Guide](docs/guides/SimulationCase.md) explains the role of each method before diving into individual parameters. For a complete list of valid combinations, see the [Reference Manual](docs/Reference.md).

## Build

ARCH must be built in a Linux environment; Windows users should use a WSL2 Linux terminal. Choose either the CPU-only or the combined CPU/CUDA build below. Since the CUDA executable also supports CPU execution, there's no need to build both.

### Get the source

Use `main` for the recommended user checkout:

```bash
git clone --branch main --single-branch https://github.com/Shiro-Akane/ARCH.git
cd ARCH
```

To update an existing `main` checkout, save your local changes first, then run
`git pull --ff-only` from its repository directory. If Git cannot update it
directly, it stops without resetting your work.

### Prepare the tools

Install these tools and development libraries in the Linux environment:

- A C++20 compiler, CMake 3.22 or newer, Ninja and Git.
- HDF5 with its C++ and high-level libraries, and OpenMP for CPU parallelism.
- For CUDA: CMake 3.25.2 or newer, CUDA Toolkit 12.0 or newer, a host compiler
  supported by that toolkit, and a working NVIDIA driver. The memory-monitored
  build command also uses Python 3.10 or newer.

On WSL2, install the NVIDIA driver on **Windows** and the CUDA Toolkit inside
WSL; do not install a Linux display driver inside WSL. Follow
[NVIDIA's WSL setup guide](https://docs.nvidia.com/cuda/wsl-user-guide/index.html).

CMake downloads HighFive during configuration. KLU, the CPU sparse solver, is
enabled by default: CMake uses an installed library or downloads pinned
SuiteSparse v7.13.0. Keep network access available for this step.

All commands below run from the repository root. `cmake --preset ...` checks
dependencies and prepares the build directory using the project's saved
settings; `cmake --build ...` compiles the program. The presets use Ninja, so no
separate `make` command is needed. Use a fresh directory if one with the same
name was configured with a different compiler or build tool.

### Option A: CPU

```bash
cmake --preset cpu-release
cmake --build build-cpu --target ARCH --parallel 1
```

This produces the executable at **`build-cpu/bin/ARCH`**. You can now skip to [First run](#first-run).
Note that `BUILD_TESTING=OFF` simply skips compiling the test suite—it does not disable any simulation features.
The `--parallel 1` flag restricts compilation to a single job to conserve memory.

### Option B: CPU and CUDA

Run this on the machine whose GPU will execute ARCH. The preset selects that
GPU as the compilation target with `CMAKE_CUDA_ARCHITECTURES=native` and limits
heavy compile jobs to one.

```bash
cmake --preset cuda-release
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 1
```

The executable is **`build-cuda/bin/ARCH`**. The Python wrapper monitors memory
and disk pressure while CMake builds the program. Here it stops the build if
available memory falls below 1.5 GiB, swap grows by more than 256 MiB, or either
memory or I/O stalls persist. It does not change the compiled numerical methods.

For **sparse nuclear burning on CUDA**, also install cuDSS 0.8 before
configuration. CMake searches for it automatically; if it is in a custom
location, add `-DCUDSS_ROOT=/your/installed/cudss` to the configuration command,
using the actual installation directory. Without cuDSS, the executable supports
the other CUDA features and dense burning, but rejects sparse CUDA burning
requests. KLU serves CPU sparse solves and cannot substitute for cuDSS on CUDA.

### Build speed and optional data

The commands start with one compile job. For more speed, the
[build guide](docs/guides/Build.md) explains how to choose parallel limits and
monitor memory. Its measured mid-range example uses WSL2, an i7-10700, 16 GB
system RAM and an RTX 3060 Ti with 8 GB VRAM, with two heavy compile jobs and
four total jobs. The guide also covers compiler selection, builds for other
GPUs and the test suite. Release optimization remains enabled.

The first Sod example needs no EOS table. For Helmholtz or other LFS-managed
table data, install Git LFS and run `git lfs pull` from the repository root.
ARCH itself does not need Python to run a simulation.

To verify a checkout, follow the [test guide](tests/README.md): start with the
GPU-free tooling checks, then build CPU or CUDA tests and run the application
and restart checks appropriate to your configuration. Test sources and small
reference data are included; test executables are built locally.

## First run

The Sod shock tube starts with two regions of gas at different densities and
pressures. Removing the imaginary partition between them produces a shock, a
contact surface and an expanding wave. This small one-dimensional example is
a useful first check that the program runs and produces readable output.

Run it from the repository root after Option A:

```bash
export OMP_NUM_THREADS=4
./build-cpu/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Here `Sod` selects the problem definition, and the `.par` file supplies its
parameters. `OMP_NUM_THREADS` controls the number of CPU worker threads; it is
not a numerical accuracy setting. For Option B, the
executable is `./build-cuda/bin/ARCH`. To request GPU execution, add
`compute_backend = cuda` to a copy of the example parameter file and run that
copy.

A successful run prints the selected methods and a table of time steps, then
writes files under `output/first_sod/`:

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

The log is readable text. Files containing `_plt_` hold fluid fields for
inspection, while `_chk_` files are checkpoints for restarting a run. HDF5 is
the data format used for these binary files. The
[case guide](docs/guides/SimulationCase.md) explains how to inspect the fields
and compare runs. This example uses 64 cells; `simulation/Sod/Sod.par` provides
the 128-cell standard shock tube, and `simulation/Sedov/` contains a blast-wave
example.

## Tabular EOS and custom networks

These extensions are useful when an ideal gas or a built-in reaction network
does not describe the intended problem. They are not needed for the first run.

Tabular EOS configuration supplies the HDF5 path; rank selection is file-driven:

~~~text
eos_type = tabular
eos_table_path = /path/to/model.h5
~~~

`table_rank` inside the file selects the 3D or 4D policy. New EOS tables should
store specific Helmholtz free energy and follow the
[local HDF5 contract](src/physics/eos/TabularEOS.md); upstream Shen/LS/HS or
CompOSE files require a family-specific converter to that contract. No external
EOS converter is currently bundled; the real Shen source-table assessment is
recorded in [validation/eos](validation/eos/README.md).

Use pynucastro 2.12.0 for the documented network-generation workflow. The
[network validation guide](validation/network/README.md#reproduce-the-records)
shows the Python environment and complete build setup.
Copy and edit the example recipe, choosing a unique `NETWORK_ID` and the required
nuclei, then run:

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
python3 tools/network/GenerateNetwork.py MyNetwork.py --check
python3 tools/network/GenerateNetwork.py MyNetwork.py
cmake -S . -B build
cmake --build build --parallel 1
~~~

Each generated package occupies `src/physics/network/custom/<id>/`. CMake
registers the generated IDs present in that directory, supports multiple IDs,
and reserves the `aprox*`/`iso*` namespaces for built-in networks. Existing-ID
replacement follows the guarded generator workflow. Select one package per run:

~~~text
network_name = custom:<id>
use_burn = true
use_nse = false
linear_solver = Auto
~~~

For CUDA, use the supplied generator to create a package with device-callable
math. Its manifest declares that capability, and CMake checks the required
package interface before registering a CUDA route. Both backends then use the
same generated math header and declared Jacobian structure. For recognized
embedded weak tables, each backend manages its own read-only storage while
sharing the interpolation, derivatives and signed energy integration.
CPU-only packages remain available for CPU execution. Generated networks do
not support the Timmes NSE projection.

Linear-solver names are case-insensitive. `Auto` selects DenseLU for systems of
up to 31 total ODE equations, counting species, temperature and any auxiliary
state. Larger systems use SparseKLU on CPU or cuDSS on CUDA, provided the
required library and network/EOS code were built. Explicit SparseKLU is CPU-only
and explicit cuDSS is CUDA-only; incompatible combinations are rejected before
backend construction, without silently substituting another solver. cuDSS is
an optional dependency, required for sparse CUDA burning. Independent weak trajectories and real
generated-network applications are recorded in
[network validation](validation/network/README.md). The complete contract
and generator requirements are in the
[Research and API Reference](docs/Reference.md); checks across network sizes
are kept with the same validation results.

## Documentation paths

The [Simulation Case Guide](docs/guides/SimulationCase.md) provides a continuous
student path through the first run, core CFD parameters, and a new
`Setup`/`Init` case.

For exact parameter names, accepted values, API signatures, output formats
and extension requirements, use the searchable
[Research and API Reference](docs/Reference.md).

The [documentation index](docs/README.md) groups learning guides, physics
notes, API reference material, and legal-document pointers by audience.

The [CUDA and GPU-AMR guide](docs/CudaBackendStatus.md) describes supported
features, backend responsibilities and solver selection.

The [validation index](validation/README.md) explains what was tested and how
to read the results. Its module pages introduce the scientific checks before
linking to detailed reports, logs and measured hardware configurations. Those
records are intended for reproduction and review; they are not extra setup
steps for a first simulation. Developers changing the code should also read
the [contributor guide](docs/development/README.md).

## Repository map

The [source guide](src/README.md) links each implementation module. Local README
files explain responsibilities and important entry points; the
[contributor guide](docs/development/README.md) covers ownership and review work.

```text
ARCH/
├── README.md                  # Entry point and first run
├── LICENSE                    # MIT license for ARCH-authored material
├── THIRD_PARTY_NOTICES.md     # Scientific-source provenance and terms
├── LICENSES/                  # Retained third-party license texts
├── CMakeLists.txt             # CPU/CUDA build and dispatch targets
├── cmake/                    # Dependency discovery and generated build bindings
├── simulation/               # Case implementations and reusable example inputs
├── docs/                     # Guides, reference, physics notes, legal index
├── validation/               # Single V&V tree: inputs, records, metrics, figures
├── EOS_toolkit/              # Runtime EOS tables grouped by model
├── src/
│   ├── core/                 # Parameter loading, case registry, public facade
│   ├── interface/            # ProblemGenerator adapters
│   ├── data/                 # Conservative and case-facing state types
│   ├── grid/                 # Coordinates and finite-volume metrics
│   ├── amr/                  # Hierarchy, pool, exchange, flux registers
│   ├── driver/               # Runtime dispatch and operator sequence
│   ├── cuda/                 # Device kernels, storage and provider adapters
│   ├── numerics/             # Flux, reconstruction, integration, burn, diffusion
│   ├── physics/              # EOS, gravity, species, networks, NSE, diagnostics
│   ├── io/                   # Parameters, logging, HDF5 plot/checkpoint IO
│   └── main.cpp
├── build/                    # Generated, ignored
├── bin/                      # Generated, ignored
└── output/                   # Generated, ignored
```

## Current numerical boundaries

Choosing a higher-order fluid integrator doesn't automatically elevate the accuracy of all coupled physical processes. Burning, diffusion, and fluid motion are integrated using a symmetric operator splitting sequence: `B(dt/2)-D(dt/2)-H(dt)-D(dt/2)-B(dt/2)`, where each letter advances one process for the indicated interval. Because of this coupling, the combined method is at most second-order accurate, even if the fluid step itself uses SSPRK3. Note that this limitation doesn't apply to pure fluid calculations. Additionally, at AMR coarse-fine boundaries, the code safely falls back to a MUSCL-MinMod reconstruction instead of the wider PPM.

For a convergence study, also check whether density, velocity, internal-energy
or species safeguards were activated: they can modify an update in invalid or
near-vacuum states. The [reference](docs/Reference.md) explains these numerical
choices, and the validation pages show how errors and conservation are measured.

Release builds retain CPU optimization and LTO. The shared build settings
disable fast-math and floating-point contraction on supported GNU, Clang and
NVIDIA toolchains to preserve the required numerical behavior; this does not
promise bitwise-identical results on different machines. Extensions currently
use the source interfaces, rather than an installed binary-library interface.

## License

ARCH-authored material is released under the [MIT License](LICENSE).
Third-party-derived scientific code and data retain their upstream provenance
and terms; see [Third-Party Provenance and Notices](THIRD_PARTY_NOTICES.md).
In particular, the MIT license does not relicense the Timmes-derived networks,
NSE implementation, Helmholtz EOS, or table data. The optional KLU backend retains its SuiteSparse LGPL/BSD terms in [LICENSES](LICENSES/) and [third-party notices](THIRD_PARTY_NOTICES.md).

## Development roadmap

The two tracks below show the development direction beyond the capabilities
listed above. The first item in each track is the current focus. A `?` marks a
later proposal whose scope and design remain open; the arrows indicate planning
order, not a software or physical dependency.

```text
Physics:   Self-gravity → MHD? → { BSSN? | Z4c? }
Software:  MPI         → GNN? → { FP32/FP64 selection? | RT-core acceleration? }
```

Self-gravity would calculate the gravitational field produced by the simulated
matter. Magnetohydrodynamics (MHD) would add magnetic fields to the fluid model.
BSSN and Z4c are possible future formulations for evolving spacetime in general
relativity; they are alternatives under consideration, not implemented modules.

MPI would distribute a simulation across processes and machines. Later
exploration includes graph neural networks (GNN), a choice between 32-bit and
64-bit floating-point arithmetic, and use of GPU ray-tracing cores (RT cores)
where the algorithm is suitable. These proposals are separate from current
feature support. Performance, memory use, build efficiency, documentation and
validation will continue to improve alongside both tracks, with one maintained
mathematical and physical implementation shared by the backends.
