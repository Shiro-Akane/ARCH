# Build and architecture tool tests

These Python checks exercise developer tooling without running a simulation:

- [test_audit_combination_v2.py](test_audit_combination_v2.py): architecture and
  shared-authority checks in the source audit.
- [test_cuda_code_images.py](test_cuda_code_images.py): configured CUDA image
  selection and compatibility metadata.
- [test_cuda_compile_memory.py](test_cuda_compile_memory.py): compiler-command
  and memory-summary handling.

The tools themselves are indexed in [tools/](../../tools/README.md). Validation
runner and process-guard contract tests remain at the [tests root](../README.md).
These checks validate tooling behavior; build timing and runtime capacity use
their separately recorded measurements.
