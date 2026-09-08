# Cross-backend validation

[cases.json](cases.json) defines the canonical uniform-grid CPU/CUDA application
matrix: problems, owned inputs, execution checkpoints, expected policy choices
and scientific comparison budgets. The shared
[backend runner](../../tools/validate_backend_results.py) executes that matrix;
the owning physics modules describe its reference methods.

Related coverage is kept with its module:

- [AMR](../amr/README.md): Cartesian and curvilinear meshes, runtime topology
  changes, conservation and diffusion coupling.
- [Network](../network/README.md): generated packages, weak rates and sparse solves.
- [Restart](../restart/README.md): native restore and continuation across backends.

`results/` preserves identified application, regression, sanitizer, build,
resource and final-review records, including earlier failed attempts. Start
from the [central Validation index](../README.md) for the combined status and
the selected evidence; directory names alone do not establish acceptance.
Maintenance checks retain their own source identity and do not relabel an
earlier scientific run.
