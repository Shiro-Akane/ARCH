# Tooling contract tests

All Python test entry points live here. Run them together from the repository
root without a GPU:

```bash
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

Install NumPy and h5py for the checkpoint and microphysics protocol tests. The
Tooling CI job installs these dependencies; no CUDA compiler or GPU is required.

- [test_audit_architecture.py](test_audit_architecture.py) checks shared-authority
  and dependency rules in the source audit.
- CUDA-image and compile-memory tests check build metadata, not GPU execution
  or measured compilation performance.
- Backend, restart, provenance and input tests exercise validation protocols.
- Resource-guard tests check process ownership, interruption and memory limits.
  Linux `/proc`, pidfd and child-subreaper support are needed for full coverage.
- Network-generator tests exercise generation contracts and rejection paths;
  actual nuclear trajectories belong to network Validation.

The tested implementation lives in [tools/](../../tools/README.md) or its owning
Validation module. Fixed protocol inputs live in
[fixtures/validation_provenance/](../fixtures/validation_provenance/README.md),
outside runtime results. These unit tests check the validation machinery; they
do not establish scientific accuracy.

## Coupled validation controls

The microphysics tests check case selection, conservation accounting, process
failure handling and timing summaries. Large-network tests check manifests and
measurement accounting without generating or compiling a network. Scientific
trajectories are run separately through the corresponding Validation entry points.
