# Network recipe example

Copy [CustomNetworkRecipe.py](CustomNetworkRecipe.py) and choose a new network ID,
nuclei and rate-selection policy. Its comments also show where an advanced
recipe can provide `build_network(pynucastro)`.

The [custom-network guide](../../src/physics/network/custom/README.md) explains
generation, registration, solver selection and package contents. Prepare the
tested Python/pynucastro environment using the
[network setup](../../validation/network/README.md#reproduce-the-records), then
run the [generator](../../tools/network/GenerateNetwork.py).

This is an editable starting recipe. Preserve the recipe used for a calculation
alongside its package manifest; successful generation does not qualify its
physical isotope coverage or trajectories.

For an explicit ground-state NSE model, the
[light-isotope example](../../validation/network/inputs/nse_light.py) supplies
forward ReacLib rates and their `DerivedRate(..., use_pf=False)` reverse rates
in a `SimpleCxxNetwork` with `do_screening=False`. The
[two-species alpha example](../../validation/network/inputs/nse_alpha.py)
illustrates the one-independent-constraint case. These are small contract
witnesses, not production supernova networks. ARCH reports the actual
eligibility from data, detailed balance and exact conservation rank; it does
not rewrite another recipe to match these examples.

`WITH_REVERSE=True` in the starting recipe requests ReacLib inverse rates and
does not certify detailed balance with the nuclear data. Use `use_nse=auto`
for ordinary ODE fallback when the selected package is NSE-ineligible, or
`use_nse=true` to require eligibility at startup. Both apply the same
`nseTempThreshold` and `nseDensThreshold`. Screening, weak evolution and
temperature-dependent partition functions currently retain explicit NSE
eligibility restrictions; their presence does not prevent otherwise supported
ordinary network burning. The [full contract](../../src/physics/network/custom/README.md#generated-network-nse-eligibility)
explains why physical validity and activation thresholds are separate.
