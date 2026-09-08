# Network recipe example

Copy [CustomNetworkRecipe.py](CustomNetworkRecipe.py) and choose a new network ID,
nuclei and rate-selection policy. Its comments also show where an advanced
recipe can provide `build_network(pynucastro)`.

The [custom-network guide](../../src/physics/network/custom/README.md) explains
generation, registration, solver selection and package contents. Prepare the
tested Python/pynucastro environment using the
[network setup](../../validation/network/README.md#reproduce-the-records), then
run the [generator](../../tools/network/GenerateNetwork.py).

This is an editable starting recipe, not a generated package or a claim that a
particular network has been scientifically qualified. Preserve the recipe used
for a calculation alongside its package manifest.
