# Network recipe example

Copy [CustomNetworkRecipe.py](CustomNetworkRecipe.py) and choose a new network ID,
nuclei and rate-selection policy. Its comments also show where an advanced
recipe can provide `build_network(pynucastro)`.

The [custom-network guide](../../src/physics/network/custom/README.md) explains
generation, registration, solver selection and package contents. Prepare the
tested Python/pynucastro environment using the
[network setup](../../validation/network/README.md#reproduce-the-records), then
run the [generator](../../tools/network/GenerateNetwork.py).

Please note that this is strictly an editable starting recipe; it is absolutely not a generated package, nor does it imply that any particular network has been scientifically qualified. You must rigorously preserve the specific recipe used for a calculation alongside its corresponding package manifest.
