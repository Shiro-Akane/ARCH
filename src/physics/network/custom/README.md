# Custom reaction-network packages

A reaction network specifies which isotopes and reactions a burn calculation
follows. A recipe is the small Python file that makes those choices; the
generator turns it into C++ source and a manifest describing the resulting
package. Python is used to prepare the package, while ARCH runs the compiled
network through its normal physics and solver interfaces.

This directory is the output root managed by
`tools/network/GenerateNetwork.py`. Generated packages use one subdirectory per
network ID. The `aprox*`, `iso*`, and `timmes_common` trees are separate,
maintained built-in-network namespaces.

## User-owned recipe

Copy `examples/network/CustomNetworkRecipe.py` outside this directory. The user
chooses:

- a unique lowercase `NETWORK_ID`, which becomes both the package folder and
  the runtime suffix;
- `NUCLEI` plus the default ReacLib linking controls; or
- an advanced `build_network(pyna)` returning a `pyna.SimpleCxxNetwork`.

`NETWORK_ID` must match `^[a-z][a-z0-9_]{0,47}$`. Names beginning with
`aprox` or `iso` and other runtime-reserved names are rejected.

First prepare a Python/pynucastro environment using the
[network setup](../../../../validation/network/README.md#reproduce-the-records).
Activate that environment before running the commands below so that `python`
uses the intended pynucastro installation.

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
python tools/network/GenerateNetwork.py MyNetwork.py --check
python tools/network/GenerateNetwork.py MyNetwork.py
cmake -S . -B build
cmake --build build --parallel 1
~~~

`--check` imports pynucastro and constructs/validates the network without
writing a package. The default `NUCLEI` path retains requested nuclei with no
linked rate as explicit inert species instead of silently dropping them, and
rejects duplicates, an empty network, or incomplete mass data. Rerun CMake
after every generated-package set change.

## Coexistence and replacement

The output is `src/physics/network/custom/<NETWORK_ID>/` and the runtime value
is `network_name = custom:<NETWORK_ID>`. CMake registers every valid sibling
package, while one run selects exactly one.
Use `-DARCH_CUSTOM_NETWORKS="id1;id2"` to restrict template instantiation in a
large build. The generic source scan excludes the complete custom subtree;
only adapter sources selected by this registry are compiled.
CMake also validates `manifest.json`, including the package structure, generator
contract, species order and declared Jacobian sparsity. The generator writes
this metadata automatically; users do not need to edit contract identifiers.
Packages whose metadata or generated interfaces are unsupported are rejected.

Existing-ID handling is guarded and recoverable. An identical
recipe and generation identity is a no-op. A changed recipe requires
`--replace`, which first moves the installed package to `.backup/`; failure to
activate the staged package restores that backup. Two variants that remain
selectable in one executable use distinct IDs.

Set `linear_solver = Auto`. Solver names are case-insensitive. `Auto` selects
DenseLU for up to 31 total ODE equations, counting species, temperature
and any auxiliary energy state. Larger systems use SuiteSparse KLU on CPU
or cuDSS on CUDA. SparseKLU is CPU-only and cuDSS
is CUDA-only; an incompatible explicit backend/solver pair is rejected before
backend construction, without silent solver replacement. Explicit DenseLU
rejects more than 31 total equations. That solver limit is independent of the runtime
species scratch used by CUDA transport and AMR.

CUDA registration requires a supported device-math contract and
`device_callable_math=true` in the generated manifest. Each backend stores
recognized embedded weak tables as read-only data and accesses them through
explicit borrowed views. Accepted packages without a supported device-math
contract execute on CPU only.
CUDA sparse burning additionally requires the
optional cuDSS library: configure with `ARCH_ENABLE_CUDA=ON`,
`ARCH_ENABLE_CUDSS=ON`, and, if needed, `CUDSS_ROOT` pointing to an installed
prefix (which may be user-local). The adapter requires the cuDSS 0.8 API and
checks the runtime version. Sparse CUDA burning is available only when the
library and code for the selected network/EOS combination are linked;
otherwise that configuration is rejected.

CPU sparse values use CSC storage, with the existing `N*N` integer
entry-to-slot lookup. CUDA uses the declared CSR structure, bounded per-lane
workspace and cuDSS factor storage within a memory budget, while sharing the
BE_NR/ROS4/BD continuations. Memory requirements depend on sparse fill-in and
the active workload. For very large networks, a model's scientific reliability
depends on its isotope set, reaction data and range of applicability. See
[network validation](../../../../validation/network/README.md) for coverage and
capacity records. Start expensive builds with `--parallel 1` and the
[memory guard](../../../../README.md#build), then tune concurrency from measured
compiler memory usage.

## Adapter boundary

Generated SimpleCxx reaction expressions and immutable lookup data live in one
package-specific math header. The CPU adapter and CUDA instantiations use that
same header, with the numerical constants and adapter energy weights preserved.
Backend storage owners provide table views; device code does not dereference
host global arrays. Each package has its own namespace and include guards.
Screening macros are locally scoped so screened and unscreened packages can
coexist in the same build.

ARCH converts pynucastro's molar
RHS/Jacobian to mass-fraction form, includes nuclear and weak-neutrino energy in
the RHS, and evaluates the complete temperature Jacobian column through the
shared fourth-order difference policy with a precision-derived step and boundary
stencil. Recognized weak tables include their rho*Ye composition chain rule and
signed energy-source gradient. The generator removes only literal
`jac.set(..., 0.0)` calls emitted by pynucastro; runtime numerical zeros remain
in the structural pattern so KLU refactorization is safe. It derives the
declared symbolic Jacobian structure from those writes, not by sampling
numerical nonzeros, and rejects unrecognized write indices instead of silently
omitting structure. Generated packages
set `SUPPORTS_NSE=false` for the Timmes NSE projection. Production
qualification for each generated network covers its RHS/Jacobian, tolerances,
conservation, energy, and trajectory behavior. Pynucastro is a generation-time
dependency only. Focused device math/solver smoke checks do not qualify a full
burn trajectory or the final executable. Multi-size compatibility evidence is centralized in
[`validation/network`](../../../../validation/network/README.md).

Recognized weak networks integrate a signed energy source as an additional ODE
state, using the same BE_NR/BD/ROS4 stages, error control and rollback. Nuclear
energy and the source integral enter the common accepted-energy accounting.
CPU functions borrow host data; CUDA storage managers upload read-only tables
once and retain them across grid-storage changes. No second interpolator or ODE is maintained.
Independent scientific weak trajectories and final application qualification
remain required; focused math/factory controls do not close those gates.
