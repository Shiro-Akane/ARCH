# Tooling contract tests

All Python test entry points live here. Run them together from the repository
root without a GPU:

```bash
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

- [test_audit_architecture.py](test_audit_architecture.py) checks shared-authority
  and dependency rules in the source audit.
- CUDA-image and compile-memory tests check build metadata, not GPU execution
  or measured compilation performance.
- Backend, restart, provenance and input tests exercise validation protocols.
- Resource-guard tests check process ownership, interruption and memory limits.
  Linux `/proc`, pidfd and child-subreaper support are needed for full coverage.
- Network-generator tests exercise generation contracts and rejection paths;
  actual nuclear trajectories belong to network Validation.

The core implementation logic resides in [tools/](../../tools/README.md). Fixed protocol inputs are securely maintained in [fixtures/validation_provenance/](../fixtures/validation_provenance/README.md), not within any runtime results directory. It is crucial to remember that these tooling tests do not generate any form of scientific evidence.
