# Problem initialization interfaces

[ProblemGenerator.h](ProblemGenerator.h) defines case setup and grid initialization.
[GenericProblem.h](GenericProblem.h) adapts registered initialization callbacks
to that interface.

Case implementations live in [simulation](../../simulation/README.md), and
registration helpers live in [core](../core/README.md). Initialization produces
common state before backend execution; do not put a second CPU/GPU physical
model in a problem adapter. Follow the [case guide](../../docs/guides/SimulationCase.md).
