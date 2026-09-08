# Stellar conductivity

[diffusion_math.hpp](diffusion_math.hpp) owns the stellar thermal-conductivity
model. It consumes thermodynamic and species data and returns a material
coefficient; it does not advance the fluid state.

CPU and CUDA use the same callable mathematics. EOS views supply thermodynamic
inputs, while [numerics/diffusion](../../numerics/diffusion/README.md) owns the
spatial diffusion operator and time integration.

The conductivity implementation is adapted from AMReX-Astro Microphysics.
Preserve its source attribution and the applicable
[third-party notice](../../../THIRD_PARTY_NOTICES.md) and
[retained license](../../../LICENSES/AMReX-Astro-Microphysics.txt).

See the [Reference](../../../docs/Reference.md) and
[diffusion validation](../../../validation/diffusion/README.md).
