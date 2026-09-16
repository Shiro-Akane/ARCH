# Problem initialization interfaces

[ProblemGenerator.h](ProblemGenerator.h) defines case setup and grid initialization.
[GenericProblem.h](GenericProblem.h) adapts registered initialization callbacks
to that interface.

Specific case implementations are maintained in the [simulation](../../simulation/README.md) directory, while their registration helpers reside in [core](../core/README.md). The initialization phase strictly produces a common state prior to any backend execution; therefore, you must not embed duplicate CPU or GPU physical models within a problem adapter. For more information, follow the [case guide](../../docs/guides/SimulationCase.md).
