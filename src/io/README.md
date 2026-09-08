# Shared input and output

[ConfigParser.h](ConfigParser.h) parses case input, [Logger.h](Logger.h) supplies
diagnostics and [IO.h](IO.h) declares plot/checkpoint operations.

- [hdf5](hdf5/README.md) owns shared serialization.
- [chk](chk/README.md) owns checkpoint/restart routing and compatibility.
- [plot](plot/README.md) owns analysis-output routing.

CPU and CUDA environments utilize the exact same schemas and writers. At IO boundaries, the driver explicitly materializes any required device fields. It is strictly required to keep all numerical state updates completely separated from the serialization logic, and you must coordinate any format changes with [restart validation](../../validation/restart/README.md).
