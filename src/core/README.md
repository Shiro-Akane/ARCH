# Core services

- [RuntimeParams.h](RuntimeParams.h) collects typed runtime settings.
- [ProblemRegistry.h](ProblemRegistry.h), [ProblemHelper.h](ProblemHelper.h) and
  [UserInterface.h](UserInterface.h) connect registered cases to initialization.
- [ArchPortability.h](ArchPortability.h) owns host/device and inline annotations.
- [CompensatedSum.h](CompensatedSum.h) owns compensated scalar accumulation.
- [FileFingerprint.h](FileFingerprint.h) fingerprints scientific input files.

Keep these services independent of a particular reaction network or CUDA
runtime owner. Inline annotations are part of the reviewed compile contract;
changing a heavy device call boundary requires build and runtime measurements.
See the [source guide](../README.md) and [parameter reference](../../docs/Reference.md).
