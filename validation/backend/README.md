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

## Optimization and coupled checks

The [HPC-CUDA evidence index](results/hpc-cuda-optimization/README.md) summarizes
the completed optimization campaign and links its original source-pinned data.
Historical server cleanup scripts and experimental providers are archived there,
not part of the maintained solver or routine test suite.

[run_microphysics_validation.py](run_microphysics_validation.py) selects existing
scientific gates. [verify_runtime_matrix.py](verify_runtime_matrix.py) delegates
runtime, AMR, restart and instrumentation checks to the shared validators.
[verify_microphysics_coupling.py](verify_microphysics_coupling.py) combines burn,
physical transport, Hydro and ENUC-driven AMR; its optional
`--restart --active-enuc-factor VALUE --terminal-time TIME` requires an actually
binding ENUC timestep limit and runtime regridding. Adaptive CPU/CUDA runs meet
at the prescribed physical time with unchanged field budgets; same-backend
restart retains strict controller-state and output-history comparisons. The
endpoint must follow the three-step source run. The console's `dt_burn` is the
executed Strang half-step, so the ENUC witness reads checkpoint controller state.

The optional recorder has a separate short regression:

```bash
python3 validation/backend/validate_predictive_amr.py \
  --arch build/bin/ARCH --source . --output build/recorder-check \
  --backend cpu cuda
```

Install NumPy and h5py for these Python checks. Use a new output directory for
each run. The recorder regression tests 1D/2D/3D off/on and split-run equivalence;
it does not repeat the formal scientific or timing campaign.

`results/` preserves identified application, regression, sanitizer, build,
resource and final-review records, including earlier failed attempts. Start
from the [central Validation index](../README.md) for the combined status and
the selected evidence; directory names alone do not establish acceptance.
Maintenance checks retain their own source identity and do not relabel an
earlier scientific run.
