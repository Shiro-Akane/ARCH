# Custom-network generation tools

[GenerateNetwork.py](GenerateNetwork.py) is the command-line entry point: it reads
a recipe, generates a pynucastro network, writes its manifest and registers the
package. Start with the [recipe example](../../examples/network/README.md), then
follow the [custom-network contract](../../src/physics/network/custom/README.md)
and tested [environment setup](../../validation/network/README.md#reproduce-the-records).

Choose the network in your recipe; the generator writes package metadata for
CMake to check automatically. Package contract identifiers are not ARCH release
numbers and do not need to be set by the user. CUDA registration checks the
generated device-math contract and its declared capabilities.

The supporting modules have separate responsibilities:

- [PortableCxx.py](PortableCxx.py): generated-header annotations, bounded storage
  and sparse Jacobian structure.
- [PortableAdapter.py](PortableAdapter.py): adapt the generated interface for one
  shared host/device numerical body.
- [WeakTables.py](WeakTables.py): connect recognized weak-table interfaces and
  coordinate derivatives to the generated interpolant.
- [WeakStorage.py](WeakStorage.py): expose read-only weak data through explicit
  storage views for backend-owned memory.

Generated network packages are build outputs, not files to maintain by hand.
Keep the recipe and generation metadata so others can reproduce a package, and
retain the upstream notices. When changing the generator, run
[test_portable_network_generator.py](../../tests/tooling/test_portable_network_generator.py)
to check its output contract.
