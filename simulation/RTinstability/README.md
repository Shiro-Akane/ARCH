# Rayleigh–Taylor instability

[RT_instab.cpp](RT_instab.cpp) registers `RT`. It constructs a stratified
two-fluid state with a velocity perturbation for Cartesian Rayleigh–Taylor
mixing in two or three dimensions. Species fields identify the two fluids.

[RT_instab.par](RT_instab.par) is the reusable example input. Review its grid,
external-gravity and output choices for the intended calculation; parameter
definitions are in the [Reference](../../docs/Reference.md).

This directory exclusively owns the initial condition setup; it does not contain separate gravity, AMR, or flux implementations. Scientific acceptance criteria and records are maintained strictly within the [Validation](../../validation/README.md) module.
