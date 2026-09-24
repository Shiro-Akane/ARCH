# Reactive-shock initial conditions

[Cellular.cpp](Cellular.cpp) registers `CellularDet`. It initializes ambient and
perturbed thermodynamic states, a directed shock and transverse perturbations
for cellular-detonation calculations.

[Cellular.par](Cellular.par) is the reusable Helmholtz-EOS burning example.
[CellularFlash2D.par](CellularFlash2D.par) preserves the 2D helium-rich
Cellular initial state used in the user-provided FLASH 4.8 archive comparison. It runs the normal
ARCH hydro, Helmholtz EOS, aprox19 burn and AMR paths. FLASH and ARCH use
different burn temperature coupling and timestep rules, so compare at the
same physical end time and report work and field error together. The measured
setup and its limits are recorded in the [O5 report](../../validation/gravity/flash/O5OptimizationReport.zh-CN.md).
Network selection and optional generated-network preparation follow the
[Reference](../../docs/Reference.md) and
[custom-network guide](../../src/physics/network/custom/README.md).

This example defines the starting state for a focused research calculation. All qualified burn and hydro comparisons are maintained within the [Validation](../../validation/README.md) module, strictly separate from any particular standalone detonation study.

In the measured FLASH input, `EOSforRiemann=false` also omits EOS recovery at
reconstructed faces, and `algebra=2` selects the network-specific GIFT solver.
The rate-table shortcut is disabled and burning is active. Together with
different stage counts and AMR histories, these choices prevent interpreting
the total time ratio as equal-work solver efficiency. The linked assessment
includes a face-EOS control experiment; it does not establish equal-error
cross-code performance.
