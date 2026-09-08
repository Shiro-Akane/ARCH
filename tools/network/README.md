# Custom-network generation tools

[GenerateNetwork.py](GenerateNetwork.py) is the command-line entry point: it reads
a recipe, generates a pynucastro network, writes its manifest and registers the
package. Start with the [recipe example](../../examples/network/README.md), then
follow the [custom-network contract](../../src/physics/network/custom/README.md)
and tested [environment setup](../../validation/network/README.md#reproduce-the-records).

The supporting modules have separate responsibilities:

- [PortableCxx.py](PortableCxx.py): generated-header annotations, bounded storage
  and sparse Jacobian structure.
- [PortableAdapter.py](PortableAdapter.py): adapt the generated interface for one
  shared host/device numerical body.
- [WeakTables.py](WeakTables.py): connect recognized weak-table interfaces and
  coordinate derivatives to the generated interpolant.
- [WeakStorage.py](WeakStorage.py): expose read-only weak data through explicit
  storage views for backend-owned memory.

Generated packages are output, not hand-maintained generator modules. Keep
recipes and generation metadata reproducible, preserve upstream notices, and
exercise generator changes with [test_portable_network_generator.py](../../tests/test_portable_network_generator.py).
