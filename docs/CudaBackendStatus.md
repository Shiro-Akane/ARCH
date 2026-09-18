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
the isotope set of the selected supported network: a built-in network or an
explicitly certified generated model.
See [the API and parameter reference](Reference.md) for precise configuration.

In two dimensions, both cylindrical and spherical grids use the polar
`(r, phi)` plane; `phi` is the azimuthal angle in radians. Three-dimensional
spherical grids use `(r, theta, phi)`.

## GPU-AMR execution model

Implementation entry points are indexed in [CUDA runtime](../src/cuda/runtime/README.md)
and the [shared AMR module](../src/amr/README.md).

The CPU owns the mesh topology, Morton ordering, and all refinement/coarsening decisions.

- **Topology** describes the connectivity between mesh blocks.
- **Morton ordering** assigns a spatially contiguous index to those blocks.
- **Refinement** splits cells into smaller ones for higher resolution, while **coarsening** merges them when configured indicators indicate that high resolution is no longer needed.

Conversely, the GPU computes the actual cell indicators and transfers just one summary value per block back to the host to inform those decisions. Conservative field migration operates directly on device buffers, utilizing the exact same transfer mathematics as the CPU path. This design ensures that high-level mesh decisions remain under CPU control, while the heavy bulk field computations stay on the GPU. During checkpoints and plot outputs, only the required fields are explicitly copied back to the shared host writer.

## Choosing a backend for performance

**Measured end-to-end acceleration reaches about 5× for coupled AMR workloads.**
The highest result was 5.08× for hydrodynamics, BD burning, RKL2 full transport
and dynamic AMR at 128 initial mesh blocks: 423.55 seconds on CPU16 versus
83.35 seconds on CUDA. This compares the two backends running the same physical
problem from startup through completion.

These measurements used an H100-20C **20 GiB vGPU** and a Xeon Gold 6338
**32-vCPU virtual machine**, not a dedicated full H100 or 32 dedicated physical
CPU cores. Burning and coupled cases use aprox13, the Helmholtz EOS and
DenseLU; diffusion-only cases use the ideal-gas EOS. The CPU reference is
the fastest median among 1, 8 and 16 threads, with one warm-up
and five formal runs per configuration. CPU/CUDA samples were alternated.

The table shows `speedup = CPU time / CUDA time`: above 1 means CUDA is faster,
below 1 means CPU is faster. Block counts refer to **initial AMR mesh blocks**,
not CUDA thread blocks or final refined cell counts. "Coupled" includes
hydrodynamics, burning, species/thermal/viscous diffusion and dynamic AMR.

| Workload | 8 blocks | 32 blocks | 128 blocks |
| --- | ---: | ---: | ---: |
| Diffusion, RKL1 | 0.099× | 0.392× | 1.004× |
| Diffusion, RKL2 | 0.116× | 0.489× | 1.347× |
| Burning, BE_NR | 0.964× | 1.325× | 1.978× |
| Burning, BD | 1.046× | 1.519× | 2.612× |
| Burning, ROS4 | 0.975× | 1.300× | 1.997× |
| Coupled, BE_NR + RKL1 | 1.183× | 1.460× | 2.876× |
| Coupled, BE_NR + RKL2 | 1.293× | 1.962× | 3.999× |
| Coupled, BD + RKL1 | 1.378× | 3.179× | 3.924× |
| Coupled, BD + RKL2 | 1.460× | 2.856× | **5.082×** |
| Coupled, ROS4 + RKL1 | 1.248× | 2.245× | 3.681× |
| Coupled, ROS4 + RKL2 | 1.363× | 2.868× | **5.007×** |

The separate two-dimensional Sedov test with dynamic AMR measured 1.588× and
1.667× at initial block layouts of 4×4 and 8×8. Those are combined Hydro/AMR
times, not isolated AMR speedups. The
[campaign summary](../validation/backend/results/hpc-cuda-optimization/README.md)
provides absolute times, acceptance counts and source-pinned reports.

More mesh work allows CUDA to amortize kernel launches and synchronization.
Small diffusion cases still favor CPU; RKL1 at 128 blocks is effectively at
parity. Increasing the isotope count is a different scaling problem and does
not, by itself, improve GPU utilization.

**Large-network performance warning:** the tested production 150/200-isotope
sparse applications still take approximately **5.0–10.3 times as long on CUDA
as on CPU8** (speedup about 0.10–0.20×). Their numerical comparisons passed,
but the current host-controlled cuDSS route remains a performance limitation
for those small full-application workloads. Choose CPU for these workloads
unless timing on your representative case demonstrates a GPU benefit. Larger
mesh/network combinations need their own measurements; the 5× result above
does not apply to them. Experimental sparse providers are not production options.

Use a representative input to compare end-to-end time on your machine.
`compute_backend = auto` selects an available supported backend; it does not
benchmark your problem. End-to-end time includes initialization and output;
regrid measurements overlap that total and must not be added again.

The optional [AMR recorder](../src/runtime/predictive_amr/README.md) exports host
statistics for offline analysis. It adds state transfers only when explicitly
enabled and does not change refinement decisions or enable predictive inference.
Use the same recorder and output settings on both backends when timing them.

### What the CUDA optimization changes

- Hydro, diffusion and small-network burning process multiple mesh blocks per
  launch where their workspace layout permits. They still call the common
  cell/face mathematics and registered physical policies.
- AMR field migration stays on the device, with compact indicator summaries
  returned to the CPU for shared mesh-tree decisions.
- Device workspaces and host exchange buffers reuse allocated capacity. A
  topology change rebuilds the affected views rather than duplicating their
  physical models or retaining stale mesh references.
- Completion and field-version checks coordinate ghost updates, exchanges and
  publication of results. Reusing already completed boundaries also avoids a
  redundant refresh in the optional recorder path.
- Sparse corrections use one shared original-system residual check. cuDSS
  factor storage and its bounded cache remain backend-specific; further
  large-network acceleration is a separate optimization task.

Together these changes reduce launch, allocation and transfer overhead without
changing the physical equations or acceptance tolerances. The measured speedups
describe complete workloads; they do not assign a separate gain to each change.

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
  inputs. CUDA requires a generated package with device-callable math and a
  supported package interface, checked from its manifest during configuration.
  CPU-only packages execute on CPU. The exact manifest fields are documented
  in the [network contract](Reference.md). Independent
  Urca trajectory results are available in [network validation](../validation/network/README.md).
- Self-gravity is not a production capability of either backend. Generated
  networks that pass the nuclear-data and equilibrium-model checks can use
  shared NSE math, covered by focused CPU/CUDA tests rather than a new full
  application qualification.
  See the [model limits](../src/physics/nse/README.md). Both built-in and generated
  NSE are constrained to the selected species set; alpha-chain networks are not
  a substitute for a general NSE network.
- Normalized ARCH EOS tables are not interchangeable with arbitrary native
  nuclear-matter tables. Units, thermodynamic components and energy zero must
  satisfy the documented [tabular data contract](../src/physics/eos/TabularEOS.md).
  Native EOSDriver and the supported baryon ASCII source format reuse the
  existing table owners. Component completion happens on the host; strict
  interpolation, derivatives and all-root temperature inversion share one
  CPU/CUDA implementation. Their focused extension checks are separate from
  the full historical CUDA release profile.
- The local release profile uses representative generated and weak networks.
  Larger-model trajectories and resource measurements run on machines sized for
  those workloads. ARCH currently runs on one CPU node or one CUDA GPU.

The [validation index](../validation/README.md) distinguishes curated numerical
results from implementation claims and links to reproducible inputs and metrics.
Contributor experiment logs and machine-specific evidence are kept separately
in [development records](development/README.md), not in this feature guide.
