# Custom-network generation tools

[GenerateNetwork.py](GenerateNetwork.py) is the command-line entry point: it reads
a recipe, generates a pynucastro network, writes its manifest and registers the
package. Start with the [recipe example](../../examples/network/README.md), then
follow the [custom-network contract](../../src/physics/network/custom/README.md)
and tested [environment setup](../../validation/network/README.md#reproduce-the-records).

Choose the network in your recipe; the generator writes package metadata for
CMake to check automatically. CUDA registration checks the generated device-math
contract and its declared capabilities; no manual package-format selection is
required.

The `--check` report also explains NSE eligibility. It leaves the recipe's
rates and physical choices intact. A supported ground-state NSE recipe pairs
forward rates with `DerivedRate(..., use_pf=False)` and disables screening;
see the [eligibility contract](../../src/physics/network/custom/README.md#generated-network-nse-eligibility).
`use_nse=auto` permits ordinary ODE burning when that certificate is absent,
while explicit `true` requires it. Both modes retain the same configured
temperature and density activation thresholds.

The supporting modules have separate responsibilities:

- [PortableCxx.py](PortableCxx.py): generated-header annotations, bounded storage
  and sparse Jacobian structure.
- [PortableAdapter.py](PortableAdapter.py): adapt the generated interface for one
  shared host/device numerical body.
- [WeakTables.py](WeakTables.py): connect recognized weak-table interfaces and
  coordinate derivatives to the generated interpolant.
- [WeakStorage.py](WeakStorage.py): expose read-only weak data through explicit
  storage views for backend-owned memory.
- [NseMetadata.py](NseMetadata.py): export nuclear-data provenance and certify
  detailed balance and the exact baryon/charge conservation space for NSE.

Generated network packages are build outputs, not files to maintain by hand.
Keep the recipe and generation metadata so others can reproduce a package, and
retain the upstream notices. When changing the generator, run
[test_portable_network_generator.py](../../tests/tooling/test_portable_network_generator.py)
to check its output contract.
