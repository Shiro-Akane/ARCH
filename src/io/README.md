# Shared input and output

[ConfigParser.h](ConfigParser.h) parses case input, [Logger.h](Logger.h) supplies
diagnostics and [IO.h](IO.h) declares plot/checkpoint operations.

- [hdf5](hdf5/README.md) owns shared serialization.
- [chk](chk/README.md) owns checkpoint/restart routing and compatibility.
- [plot](plot/README.md) owns analysis-output routing.

CPU and CUDA use the same schemas and writers. The driver materializes required
device fields at IO boundaries. Keep numerical updates out of serialization and
coordinate format changes with [restart validation](../../validation/restart/README.md).
