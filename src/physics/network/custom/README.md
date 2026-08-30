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

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py --check
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py
cmake -S . -B build
cmake --build build --parallel 4
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

Set `linear_solver = Auto`. It retains the dedicated DenseLU backend through 30
isotopes and selects SuiteSparse KLU above 30; explicit DenseLU rejects a larger
network. Sparse matrix values use CSC storage, but the current entry-to-slot
lookup allocates `N*N` integers. The retained compatibility audit reaches 200
isotopes; substantially larger packages require their own memory qualification.

## Adapter boundary

Generated SimpleCxx code is namespace-isolated. ARCH converts pynucastro's molar
RHS/Jacobian to mass-fraction form, includes nuclear and weak-neutrino energy in
the RHS, and evaluates the temperature Jacobian column by a centered relative
`1e-4` finite difference. The energy Jacobian excludes the weak-neutrino
composition derivative. Generator version 3 removes only literal
`jac.set(..., 0.0)` calls emitted by pynucastro; runtime numerical zeros remain
in the structural pattern so KLU refactorization is safe. Generated packages
set `SUPPORTS_NSE=false` for the Timmes NSE projection. Production
qualification for each generated network covers its RHS/Jacobian, tolerances,
conservation, energy, and trajectory behavior. Pynucastro is a generation-time
dependency only. Multi-size compatibility evidence is centralized in
[`validation/network`](../../../../validation/network/README.md).

Weak-neutrino energy is present in the RHS, but its composition derivative and
time-integrated contribution to the burn solver's acceptance closure are not
yet represented. Networks in which weak losses are material therefore require
solver work before production qualification.
