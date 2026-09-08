# CUDA backend and GPU-AMR capabilities

Chinese translation: [CudaBackendStatus.zh-CN.md](CudaBackendStatus.zh-CN.md).
The English file is authoritative.

A *backend* refers to the specific execution engine in ARCH responsible for performing calculations on your chosen hardware. CUDA serves as our GPU backend. Selecting it alters how execution and memory management are handled under the hood, but it strictly retains the exact same physical models and logic as the CPU backend. *Adaptive mesh refinement (AMR)* dynamically adjusts cell sizes during a simulation, ensuring that complex regions receive higher spatial resolution. This guide details the division of labor between processors and helps you select a compatible configuration.

The CUDA backend executes hydrodynamics and adaptive-mesh numerical work on the
GPU. CPU and CUDA share the same mathematical and physical implementations;
device memory, kernels, streams and linear-solver libraries are backend-specific.
Both backends support the feature scope below. The
[validation index](../validation/README.md) records the tested configurations,
technical results and combined acceptance status.

## Shared functional scope

| Facility | Implementation scope |
|---|---|
| Hydrodynamics | Van Leer, Steger-Warming, Roe, HLL, HLLC; PCM/MUSCL/PPM; MinMod/MC/SuperBee/Van Leer limiters; Euler/SSPRK2/SSPRK3 |
| Mesh and geometry | One-, two- and three-dimensional block meshes; Cartesian, cylindrical and spherical geometry with the common CPU conventions |
| Boundaries | Periodic, outflow and reflecting boundaries |
| Dynamic AMR | Refinement indicators, conservative prolongation/restriction, mixed-level exchange, hydro/diffusion reflux and transactional state migration |
| EOS | Ideal gas, Helmholtz, normalized Tabular3D and Tabular4D data layouts |
| Diffusion | Species, thermal and viscous modes; RKL1/RKL2 integration |
| Gravity | External gravity using common stage source terms |
| Built-in burning | iso7, aprox13, aprox19, aprox21; BE_NR, BD, ROS4 and network-constrained NSE |
| Generated burning | Registered networks with device-callable math, including recognized embedded weak tables stored read-only on each backend; dense or sparse solving as described below |
| Output and restart | Shared HDF5/checkpoint facilities, with state transfers at IO boundaries and CPU/CUDA restart routes |

Policy names, aliases and supported combinations share one registration system.
The equation of state (EOS) relates pressure, density, energy and composition;
a reaction network describes how the composition changes. Their implementations
use static duck-typed interfaces: a model supplies the required operations, and
the compiler connects them to the caller. Backend adapters connect storage or
solver libraries without introducing another physical model.
Nuclear statistical equilibrium (NSE) computes an equilibrium composition within
the isotope set of the selected built-in network.
See [the API and parameter reference](Reference.md) for precise configuration.

## GPU-AMR execution model

Implementation entry points are indexed in [CUDA runtime](../src/cuda/runtime/README.md)
and the [shared AMR module](../src/amr/README.md).

The CPU owns the mesh topology, Morton ordering, and all refinement/coarsening decisions.
- **Topology** describes the connectivity between mesh blocks.
- **Morton ordering** assigns a spatially contiguous index to those blocks.
- **Refinement** splits cells into smaller ones for higher resolution, while **coarsening** merges them when configured indicators indicate that high resolution is no longer needed.

Conversely, the GPU computes the actual cell indicators and transfers just one summary value per block back to the host to inform those decisions. Conservative field migration operates directly on device buffers, utilizing the exact same transfer mathematics as the CPU path. This design ensures that high-level mesh decisions remain under CPU control, while the heavy bulk field computations stay on the GPU. During checkpoints and plot outputs, only the required fields are explicitly copied back to the shared host writer.

## Dense and sparse burning

Burning is advanced as a system of ordinary differential equations (ODEs).
Each ODE system contains one equation per isotope plus temperature. Networks that
integrate signed weak losses also include one energy-source state. With `linear_solver = Auto`,
up to 31 total equations use the shared DenseLU (30 isotopes without that source,
29 with it).
Larger systems select CPU KLU or GPU cuDSS; the selected library and code for the
network/EOS combination must have been built. An explicit CPU+cuDSS or CUDA+KLU request
is rejected. Explicit CUDA selection does not silently execute CPU physics.
`compute_backend = auto` may select an available supported backend at startup
and reports that choice; selection is closed once construction begins.

Dense solving stores the complete small matrix. Sparse solving stores its
nonzero entries; CSR (compressed sparse row) organizes those entries by row.
The CUDA sparse executor keeps numerical states and CSR matrices on the GPU.
Host control invokes the cuDSS API and exchanges execution requests/responses.
Factor storage is bounded; memory requirements depend on matrix fill-in and
the active workload. The cuDSS integration requires the 0.8 API, uses
nonsymmetric BTF/COLAMD ordering, and checks corrections against the original
matrix. Native execution/resource errors are reported, not hidden as ODE retries.

ARCH supports pynucastro-generated networks with CPU KLU / GPU cuDSS sparse
execution. For very large networks, the scientific reliability of a model
depends on its isotope set, reaction data and range of applicability. Resource
requirements grow with the network and mesh workload.

## Choosing networks and table data

- Generated weak tables share interpolation, derivatives and energy integration
  across backends. The generator checks the table layout and reports unsupported
  inputs. The manifest's `generator_version` identifies the package's generator
  contract, not an ARCH release. Value 3 supports CPU execution; CUDA requires
  a value of at least 4 and `device_callable_math=true`. Packages without device-callable math
  execute on CPU only. Independent
  Urca trajectory results are available in [network validation](../validation/network/README.md).
- Self-gravity and custom-network NSE are not production capabilities of either
  backend. Built-in NSE is constrained to the selected species set; alpha-chain
  networks are not a substitute for a general NSE network.
- Normalized ARCH EOS tables are not interchangeable with arbitrary native
  nuclear-matter tables. Units, thermodynamic components and energy zero must
  satisfy the documented [tabular data contract](../src/physics/eos/TabularEOS.md).
- The local release profile uses representative generated and weak networks.
  Larger-model trajectories and resource measurements run on machines sized for
  those workloads. ARCH currently runs on one CPU node or one CUDA GPU.

The [validation index](../validation/README.md) distinguishes curated numerical
results from implementation claims and links to reproducible inputs and metrics.
Contributor experiment logs and machine-specific evidence are kept separately
in [development records](development/README.md), not in this feature guide.
