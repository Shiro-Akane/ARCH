# Custom reaction-network packages

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
The commands below use a named Conda environment; adjust that launcher to match
your own environment.

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py --check
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py
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
CMake also validates `manifest.json`, rejecting unknown manifest schemas and
packages produced before generator version 3 safeguards.

Existing-ID handling is guarded and recoverable. An identical
recipe/pynucastro/generator version is a no-op. A changed recipe requires
`--replace`, which first moves the previous package to `.backup/`; failure to
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

Version-4 packages declaring `device_callable_math=true` in `manifest.json`
are registered for CUDA execution. Each backend stores recognized embedded weak
tables as read-only data and accesses them through explicit borrowed views.
Version-3 packages and packages not converted for device execution remain
CPU-only; regenerate them to use the current shared interfaces.
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

Generated SimpleCxx code is namespace-isolated. Version 4 places the original
reaction expressions and immutable lookup data in one math header consumed by
the ordinary C++ adapter and CUDA instantiations. It preserves numerical
constants and the original adapter energy weights, rather than substituting a
different nuclear-mass convention. Device code does not dereference Host
global arrays. Include guards are package-specific, and screening macros are
scoped so screened and unscreened packages can coexist without contamination.

ARCH converts pynucastro's molar
RHS/Jacobian to mass-fraction form, includes nuclear and weak-neutrino energy in
the RHS, and evaluates the complete temperature Jacobian column through the
shared fourth-order difference policy with a precision-derived step and boundary
stencil. Recognized weak tables include their rho*Ye composition chain rule and
signed energy-source gradient. Generator version 3 removes only literal
`jac.set(..., 0.0)` calls emitted by pynucastro; runtime numerical zeros remain
in the structural pattern so KLU refactorization is safe. Version 4 derives the
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
