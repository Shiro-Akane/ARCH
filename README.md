# ARCH: Adaptive Reactive CUDA Hydrodynamics

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text; update it first when behavior or interfaces
change.

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH is a block-adaptive finite-volume framework for compressible and reactive
hydrodynamics. CPU and CUDA execution share the same mathematical and physical
implementations. Enable `ARCH_ENABLE_CUDA=ON` to build the CUDA backend, then
select the backend in your case configuration.

The project is intended for two groups:

- students learning how a CFD case is configured, initialized, advanced, and inspected;
- researchers extending Riemann solvers, equations of state, reaction networks,
  diffusion, gravity, AMR, or diagnostics directly in the source tree.

## Project status

CPU and CUDA support the features below. The release profile has passed its
numerical, application, device-safety, build and resource checks.
[Validation](validation/README.md) records the tested configurations and final
delivery-review status.

| Capability | Shared behavior and backend choices |
| --- | --- |
| Hydrodynamics | 1D, 2D and 3D Cartesian, cylindrical and spherical grids |
| Dynamic block AMR | Conservative refinement, coarsening, ghost exchange and flux correction. CUDA computes indicators and transfers cell data on the GPU; the CPU manages the mesh tree. |
| Equations of state | Ideal gas, Helmholtz and 3D/4D tabular EOS |
| Diffusion | Thermal, viscous and species diffusion with RKL1/RKL2 time stepping |
| Gravity | Prescribed external gravity |
| Nuclear burning | Four built-in networks and generated pynucastro networks; NSE projection for the built-ins. CUDA uses device-callable version-4 generated packages. |
| Linear solvers | DenseLU for small systems; KLU on CPU and cuDSS on CUDA for sparse systems |
| Output and restart | HDF5 plots and checkpoints use the same format on both backends, including the AMR hierarchy, burn energy and timestep-controller state. |

Version-1/2 checkpoints remain readable as legacy/unverified inputs. They lack
the ordered scientific identity and `ENUC`; version 1 also lacks the timestep
controller state. Version 3 records the active burn/network/NSE identity and
the digest of the table actually loaded by the EOS owner. Version 4 also stores
native mass fractions, avoiding a lossy reconstruction from species densities.
CPU and CUDA use the
same schema, and backend selection is intentionally not a restart-compatibility
field.

`compute_backend = cpu`, `cuda`, or `auto` is resolved once before backend
construction. Explicit CUDA never silently falls back. `auto` may select CPU
only at that pre-construction boundary when the requested run is outside the
CUDA capability matrix or no usable device is available, and only when the CPU
capability gate accepts the same run.

The backend split is deliberately narrow: policy registration, AMR indicator
and transfer mathematics, geometry, EOS/network/solver mathematics, scheduling,
and HDF5 schemas are shared. Morton/topology decisions stay host-only; CUDA
executes the shared numerical leaves for indicators and conservative migration
on the device. CUDA-specific code owns kernels, device storage, transfers,
streams, sparse-library adapters, and retirement fences.

## Implemented capabilities

- conservative block AMR with ghost exchange, prolongation/restriction, flux
  registers, and reflux;
- dimension-aware 1D, 2D, and 3D storage on Cartesian, cylindrical, and spherical grids;
- SW, VL, Roe, HLL, and HLLC flux policies;
- PCM, MUSCL/PLM, and PPM reconstruction with Euler, SSPRK2, or SSPRK3 time stepping;
- ideal, automatically ranked 3D/4D tabular (including normalized free-energy tables), and Timmes Helmholtz equations of state;
- shared external gravity, built-in or generated pynucastro nuclear burning, DenseLU/optional CPU KLU or CUDA cuDSS linear solves, NSE projection, and RKL1/RKL2 super-time-stepping diffusion;
- HDF5 plot and checkpoint files, including restart of the AMR leaf hierarchy.

## Build

ARCH targets a Linux/WSL-style C++ environment. Required tools and
libraries are:

- a C++20 compiler;
- CMake 3.22 or newer for CPU builds (CUDA requirements are listed below);
- Python 3.10 or newer for the default testing configuration and validation tools;
- OpenMP unless configured off;
- HDF5 C++ and HL libraries;
- Git and network access during configuration, because CMake fetches HighFive and, when no installed KLU package is found, pinned SuiteSparse;
- Git LFS when cloning EOS `.dat` or `.h5` assets tracked through LFS.

Configure and build from the repository root:

```bash
git lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 1
```

For the CUDA backend, use CMake 3.25.2 or newer, NVCC 12.0 or newer and a host
compiler supported by that toolkit. These versions provide the required
[CUDA C++20 language support](https://cmake.org/cmake/help/latest/release/3.25.html);
the reference build uses CMake 3.28 and CUDA 12.3. Select code images with
`CMAKE_CUDA_ARCHITECTURES`: `native` for a local build, or an explicit list such
as `80;86;90` for several device generations.
Startup checks the compiled images and driver before selecting the backend.
The following example uses Ninja for separate heavy-compile job limits; install
Ninja and use a fresh build directory when changing generators.

```bash
cmake -S . -B build-cuda -G Ninja -DARCH_ENABLE_CUDA=ON \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_CUDA_HEAVY_COMPILE_JOBS=2 \
  -DARCH_ENABLE_CUDSS=ON -DCUDSS_ROOT=/path/to/cudss \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-cuda/bin"
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 4
```

This example retains the default CPU KLU library and adds CUDA cuDSS, so the
same build can run sparse burning on either backend.
It builds the executable, not the full validation suite. The ARCH executable
does not require Python at runtime. cuDSS is optional:
omit `CUDSS_ROOT` when discovery already finds it, or use
`-DARCH_ENABLE_CUDSS=OFF` for a build without sparse CUDA burning. A user-local
installation prefix is sufficient. The cuDSS adapter requires the 0.8 API and
checks the runtime version. Sparse CUDA burning is available only when the
library and code for the selected network/EOS combination are linked;
otherwise the configuration is rejected. KLU remains CPU-only; cuDSS remains CUDA-only.

The build guard leaves 1.5 GiB of available RAM and allows up to 256 MiB of
additional swap in this example. With `--pressure-guard`, it also watches Linux
memory and I/O stalls and stops its build if pressure stays high. This option
requires Linux memory and I/O PSI full counters. The guard also needs working
pidfd and child-subreaper support and readable `/proc` data; it checks these
capabilities before starting the build. Current WSL2 provides them.
Use `Ctrl+C` or `SIGTERM` to stop a guarded build and its compiler children.

When selecting GCC explicitly, use matching versions for `CMAKE_C_COMPILER`,
`CMAKE_CXX_COMPILER` and `CMAKE_CUDA_HOST_COMPILER`. Release builds retain LTO,
which also requires compatible compiler versions for locally built dependencies.

With Ninja, `ARCH_CUDA_HEAVY_COMPILE_JOBS` controls the heavy compile pool
(default `1`), independently of the overall `--parallel` ceiling. For the
mid-range reference below, we recommend `ARCH_CUDA_HEAVY_COMPILE_JOBS=2` with
`--parallel 4`, as used in the [completed core-build measurements](validation/backend/results/cold-core-first-law-20260907/release-909/README.md).
This is a measured reference configuration, not a minimum requirement or a
universal optimum. Use lower limits when less memory is available; larger
systems can raise them while checking the guard's measurements. Numerical
methods and Release optimization remain unchanged.
Contributor-only measurements and refactor records are in
[development documentation](docs/development/README.md).

Local build measurements use WSL2, an i7-10700-class CPU, 16 GB system RAM and
an RTX 3060 Ti-class 8 GB GPU as the mid-range reference. Choose simulation
memory for your mesh, refinement levels, species count and sparse solver
workspace. Check WSL's assigned memory separately and leave room for Windows.
The numerical methods and accuracy settings are the same on every machine.

Release builds optimize for the local CPU, so rebuild when moving to a different
CPU architecture. Set `CMAKE_CUDA_ARCHITECTURES` for the GPUs that will run the
executable, using a toolkit that supports those targets.

The default executable is `bin/ARCH`; the isolated CUDA example above writes
`build-cuda/bin/ARCH`, avoiding cross-build overwrite. KLU is enabled by default; CMake
uses an installed package or fetches pinned SuiteSparse v7.13.0.

## Tabular EOS and custom networks

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

Generator version 4 enables CUDA only for packages whose manifest declares
`device_callable_math=true`. Both backends then use the same generated math
header and declared Jacobian structure. For recognized embedded weak tables,
each backend manages its own read-only storage while sharing the interpolation,
derivatives and signed energy integration. Version-3 packages and packages not
converted for device execution remain CPU-only. Generated networks do not
support the Timmes NSE projection.

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
[Research and API Reference](docs/Reference.md); multi-size compatibility
records are kept with the same validation results.

## First run

Run the small one-dimensional Sod teaching case:

```bash
export OMP_NUM_THREADS=4
./bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

A successful run prints the selected EOS, solver, reconstruction, time
integrator, AMR resolution, and a step table. It writes files under
`output/first_sod/`:

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

`Sod_beginner.par` uses 64 cells; `Sod.par` provides the 128-cell standard
shock tube. The blast benchmark is in `simulation/Sedov/`.

## Documentation paths

The [Simulation Case Guide](docs/guides/SimulationCase.md) provides a continuous
student path through the first run, core CFD parameters, and a new
`Setup`/`Init` case.

For exact parameter names, accepted values, API signatures, output schemas,
extension contracts, and known compromises, use the single searchable
[Research and API Reference](docs/Reference.md).

The [documentation index](docs/README.md) groups learning guides, physics
notes, API reference material, and legal-document pointers by audience.

The [CUDA and GPU-AMR guide](docs/CudaBackendStatus.md) describes supported
features, backend responsibilities and solver selection.

The [validation index](validation/README.md) summarizes numerical results and
release acceptance. The [AMR validation page](validation/amr/README.md) covers
mesh adaptation, conservation, geometry and restart checks, with links to their
reproducible records.

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

- Burning, diffusion, and hydro use the symmetric composition
  `B(dt/2)-D(dt/2)-H(dt)-D(dt/2)-B(dt/2)`; the coupled method is therefore at
  most second order, even when SSPRK3 is selected for hydro;
- Coarse-fine AMR faces use MUSCL-MinMod in place of PPM's wide stencil;
- Density, velocity, internal-energy, and species safeguards can modify the
  conservative update in invalid or near-vacuum states;
- The shared build contract disables fast-math and floating-point contraction
  on supported GNU/Clang/NVIDIA toolchains. Release still uses `-march=native`;
  bitwise-identical results across machines are not guaranteed;
- ARCH currently exposes source-extension interfaces rather than an installed
  public library ABI.

Convergence and production studies should follow the Reference. Official
validation claims require reproducible inputs, reference solutions, norms, and
tolerances.

## License

ARCH-authored material is released under the [MIT License](LICENSE).
Third-party-derived scientific code and data retain their upstream provenance
and terms; see [Third-Party Provenance and Notices](THIRD_PARTY_NOTICES.md).
In particular, the MIT license does not relicense the Timmes-derived networks,
NSE implementation, Helmholtz EOS, or table data. The optional KLU backend retains its SuiteSparse LGPL/BSD terms in [LICENSES](LICENSES/) and [third-party notices](THIRD_PARTY_NOTICES.md).
