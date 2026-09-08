# Reactive-shock initial conditions

[Cellular.cpp](Cellular.cpp) registers `CellularDet`. It initializes ambient and
perturbed thermodynamic states, a directed shock and transverse perturbations
for cellular-detonation calculations.

[Cellular.par](Cellular.par) is the reusable Helmholtz-EOS burning example.
Network selection and optional generated-network preparation follow the
[Reference](../../docs/Reference.md) and
[custom-network guide](../../src/physics/network/custom/README.md).

This example defines a research calculation's starting state. Qualified burn
and hydro comparisons live in [Validation](../../validation/README.md), separate
from any particular detonation study.
