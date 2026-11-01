# Configuration v3 response samples

These responses were captured from the CPU ARCH executable identified in [capture.json](capture.json). That file records the source commit, executable hash, exact arguments, input hashes and exit codes. Samples cover the schema, valid Sod input, empty input and aggregated invalid input.

Use the recorded arguments with the corresponding `.par` on stdin to inspect the response structure. Compare parsed fields rather than JSON whitespace. Model source hashes, parameter counts and coordinate support depend on the selected executable; obtain those from its current schema. The stored capture includes the former cylindrical 2D chart and is not a current chart catalogue.

The `configuration_v3_contract` test runs the actual binary and checks missing, invalid, repeated, default, case-defined and derived values, request identity, byte limits and filesystem side effects. The [configuration protocol](../../docs/ConfigurationProtocol.md) defines field meanings.
