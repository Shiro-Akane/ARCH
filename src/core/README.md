# Core services

- [RuntimeParams.h](config/RuntimeParams.h) collects typed runtime settings.
- [ProblemRegistry.h](problem/ProblemRegistry.h), [ProblemHelper.h](problem/ProblemHelper.h) and
  [UserInterface.h](config/UserInterface.h) connect registered cases to initialization.
- [ArchPortability.h](ArchPortability.h) owns host/device and inline annotations.
- [CompensatedSum.h](CompensatedSum.h) owns compensated scalar accumulation.
- [FileFingerprint.h](files/FileFingerprint.h) fingerprints scientific input files.

These core services must remain independent of any specific reaction network or CUDA runtime management code. It is important to treat inline annotations as a critical part of the reviewed compilation contract; altering any heavy device-call boundary requires careful build and runtime performance measurements.
For further details, refer to the [source guide](../README.md) and the comprehensive [parameter reference](../../docs/Reference.md).

Cases use `<UserInterface.h>` and `<GlobalDefs.h>` through the stable
[public entry points](../../include/README.md), not the internal config path.
