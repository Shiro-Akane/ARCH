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
writing a package. Generation also rejects an empty network or nuclei with
incomplete mass data. Rerun CMake after every generated-package set change.

## Coexistence and replacement

The output is `src/physics/network/custom/<NETWORK_ID>/` and the runtime value
is `network_name = custom:<NETWORK_ID>`. CMake registers every valid sibling
package, while one run selects exactly one.
Use `-DARCH_CUSTOM_NETWORKS="id1;id2"` to restrict template instantiation in a
large build.

Existing-ID handling is transactional. An identical
recipe/pynucastro/generator version is a no-op. A changed recipe requires
`--replace`, which first moves the previous package to `.backup/`. Two variants
that remain selectable in one executable use distinct IDs.

Set `linear_solver = Auto`. It retains the dedicated DenseLU backend through 30
isotopes and selects SuiteSparse KLU above 30; explicit DenseLU rejects a larger
network.

## Adapter boundary

Generated SimpleCxx code is namespace-isolated. ARCH converts pynucastro's molar
RHS/Jacobian to mass-fraction form, includes nuclear and weak-neutrino energy in
the RHS, and evaluates the temperature Jacobian column by a centered relative
`1e-4` finite difference. The energy Jacobian excludes the weak-neutrino
composition derivative. Generated packages set `SUPPORTS_NSE=false` for the
Timmes NSE projection. Production qualification for each generated network covers its
RHS/Jacobian, tolerances, conservation, energy, and trajectory behavior.
Pynucastro is a generation-time dependency only.
