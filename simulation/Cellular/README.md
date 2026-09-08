# Reactive-shock initial conditions

[Cellular.cpp](Cellular.cpp) registers `CellularDet`. It initializes ambient and
perturbed thermodynamic states, a directed shock and transverse perturbations
for cellular-detonation calculations.

[Cellular.par](Cellular.par) is the reusable Helmholtz-EOS burning example.
Network selection and optional generated-network preparation follow the
[Reference](../../docs/Reference.md) and
[custom-network guide](../../src/physics/network/custom/README.md).

This example defines the starting state for a focused research calculation. All qualified burn and hydro comparisons are maintained within the [Validation](../../validation/README.md) module, strictly separate from any particular standalone detonation study.
