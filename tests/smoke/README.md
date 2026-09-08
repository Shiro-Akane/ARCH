# Short CPU/CUDA AMR development smoke

Configure with `ARCH_ENABLE_CUDA=ON` and `BUILD_TESTING=ON`, then build `ARCH`
and `arch_cuda_single_level_validation` in the same build tree. The examples
below use `build-cuda` and assume `ARCH_RUNTIME_OUTPUT_DIRECTORY` points to
`build-cuda/bin`, as in the [CUDA build instructions](../../README.md#build).
Adjust `--arch` to the actual output location if configured differently; its
default is the repository's `bin/`, not the build tree. Use the comparator from
that same build. From the repository root, run:

```sh
python3 tools/smoke_cuda_amr_runtime.py --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation
```

This is **not scientific validation or merge qualification**. It does not
compare errors/conservation with independent references, or certify restart
field parity. It checks process/step completion, explicit backend selection,
CUDA hydro/diffusion work and ghost-completion traces, real device topology
change in the Cartesian case, and a short checkpoint continuation. The existing
comparator reads each actual HDF5 checkpoint using `--metrics CHECKPOINT
--parameters ACTUAL_RUN.par`: source/terminal metadata must report the requested
step, and a restart source must report the source lane's completed step before
ARCH is invoked. An initial checkpoint cannot stand in for missing final output.
No Python HDF5 dependency or separate checkpoint schema is introduced.

The checked-in manifest reads existing canonical `.par` files without changing
them. All overrides and commands are recorded; every invocation defaults to a
new temporary output directory that is retained for inspection. An explicit
`--output-root` must be absent or empty. Nonzero exits (including 77), timeouts,
missing outputs, fallback, or missing required work fail the smoke, never skip.

The small lanes run sequentially with one OpenMP thread by default:

- Cartesian 1D Sedov uses `DIVV` refinement: initial velocity/divergence is zero,
  then the pressure-driven flow should trigger device AMR after an accepted step.
- Cylindrical and spherical 1D Gaussian species diffusion run on the annulus
  `r in [1,2]`, avoiding coordinate singularities. The ordinary Gaussian problem,
  hydro, RKL2 diffusion, and AMR paths execute; each source run must actually
  create refined initial leaves. Each CPU/CUDA checkpoint is resumed for one
  further accepted step using the normal restart CLI.

`--case CASE` and `--backend cpu|cuda` select a subset explicitly; these choices
are recorded. Example fast first check:

```sh
python3 tools/smoke_cuda_amr_runtime.py --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --case cartesian_dynamic_sedov --timeout 30
```

The curved cases are initial-refined AMR execution/restart checks; their success
alone does not prove dynamic curved regrid happened after backend creation.
Full shared migration kernels and staged-store tests cover that separate seam.
