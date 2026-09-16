# Core services

- [RuntimeParams.h](RuntimeParams.h) collects typed runtime settings.
- [ProblemRegistry.h](ProblemRegistry.h), [ProblemHelper.h](ProblemHelper.h) and
  [UserInterface.h](UserInterface.h) connect registered cases to initialization.
- [ArchPortability.h](ArchPortability.h) owns host/device and inline annotations.
- [CompensatedSum.h](CompensatedSum.h) owns compensated scalar accumulation.
- [FileFingerprint.h](FileFingerprint.h) fingerprints scientific input files.

These core services must remain independent of any specific reaction network or CUDA runtime management code. It is important to treat inline annotations as a critical part of the reviewed compilation contract; altering any heavy device-call boundary requires careful build and runtime performance measurements.
For further details, refer to the [source guide](../README.md) and the comprehensive [parameter reference](../../docs/Reference.md).
