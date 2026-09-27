# Problem initialization interfaces

[ProblemGenerator.h](ProblemGenerator.h) defines case setup and grid initialization.
[GenericProblem.h](GenericProblem.h) adapts registered initialization callbacks
to that interface.

Cases live in [simulation](../../simulation/README.md), with registration helpers
in [core](../core/README.md). Initialization produces the common state before
backend execution. Problem adapters should use shared physical models rather
than supply separate CPU/GPU formulas. See the [case guide](../../docs/guides/SimulationCase.md).
