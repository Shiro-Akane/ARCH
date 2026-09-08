# Rayleigh–Taylor instability

[RT_instab.cpp](RT_instab.cpp) registers `RT`. It constructs a stratified
two-fluid state with a velocity perturbation for Cartesian Rayleigh–Taylor
mixing in two or three dimensions. Species fields identify the two fluids.

[RT_instab.par](RT_instab.par) is the reusable example input. Review its grid,
external-gravity and output choices for the intended calculation; parameter
definitions are in the [Reference](../../docs/Reference.md).

This directory owns the initial condition, not a separate gravity, AMR or flux
implementation. Scientific acceptance is recorded separately in
[Validation](../../validation/README.md).
