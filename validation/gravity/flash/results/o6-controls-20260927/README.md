# Shared controls and strict optimization evidence

- `compiler/`: one intermediate source, three serial samples for O1/LTO,
  O3/LTO and O3/LTO/no-math-errno; standalone floating-point semantics probe.
  The final link command is also retained. This intermediate source still had
  a Hermite weight shortcut subsequently removed after a negative cost result.
- `probes/`: rejected face approximations and bounded execution probes.
  These results do not count as accepted physical alternatives or final timings.
- `earlier/`: pre-energy-key measurements. The default Sod comparison exceeded
  the original historical-field bound, so these do not qualify the final source.
- `final/`: final executable identities, complete-task timings and field checks;
  exact AMR step/leaf histories confirm unchanged active cell macro-step work.
- `contended-gpu/`: timings taken while another heavy GPU workload was active,
  plus an unqualified diagnostic repeat; excluded from the final performance
  baseline. The accepted `final/*_idle*.json` groups were fully rerun after the
  user confirmed the competing load had ended. All original samples are retained.
- `checks/`: CPU/CUDA contracts and focused safety checks. The original screening
  body is retained only as a diagnostic oracle, not as a second production route.

See [the acceptance report](../../O6ControlsAcceptanceReport.zh-CN.md). Raw HDF5
remains in each manifest's local output directory; no new output binaries are
added to source control. CPU and FLASH end at the same recorded physical time;
a wall-clock ratio is not proof of equal-error scientific equivalence.

CPU history/variant/thread analysis is recorded by `checks/analyze_cpu_controls.py`,
which reuses the existing field comparators and the predeclared variant budget.
The stored script references this workstation's raw output directories. Tooling
coverage is split between conda work and system Python only because the conda
interpreter lacks Linux pidfd; `checks/validation_status.json` records both runs.

Reproduction uses the existing `run_comparison.py` entry point. From the
repository root after building the CPU and CUDA presets, the following produces
new local references and checks every GPU sample against them. `conda work`
provides the required Python/h5py/numpy environment on the original workstation.
Choose thread counts and optional `--affinity` / `--cuda-affinity` lists for the
current machine; omitted masks inherit the caller's allowed CPUs. The original
i7 masks in archived records must not be copied blindly to another host.

```bash
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases a b noburn jeans128 fine snia2d snia3d \
  --threads 8 --repeats 3 --prefix cpu8 --output output/reproduce \
  --manifest output/reproduce/cpu8.json
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases a b noburn jeans128 fine snia2d snia3d \
  --threads 16 --repeats 3 --prefix cpu16 --output output/reproduce \
  --manifest output/reproduce/cpu16.json
python validation/gravity/flash/run_comparison.py \
  --routes cuda --cases a b noburn jeans128 fine snia2d snia3d \
  --repeats 3 --prefix cuda --output output/reproduce \
  --manifest output/reproduce/cuda.json
python validation/gravity/flash/run_comparison.py --report-only \
  --manifest output/reproduce/cuda.json \
  --reference-manifest output/reproduce/cpu8.json \
  --reference-manifest output/reproduce/cpu16.json
```

The default executable paths match the presets: `build-cpu/bin/ARCH` and
`build-cuda/bin/ARCH`. For a custom build pass `--arch-cpu /path/to/ARCH` and/or
`--arch-cuda /path/to/ARCH`. Use a fresh output/prefix/manifest for every run;
existing evidence is never overwritten. The eight/sixteen-thread choices above
are examples, not a requirement or a claim that SMT is always faster. The
Davis variant uses `--cases fine --arch-parameter hll_wave_speed=davis` on all
three runs with separate prefixes/manifests and matching variant CPU references.
Do not use the archived manifests as local field references: their HDF5 paths
belong to the original workstation. This sequence compares ARCH backends;
FLASH comparisons additionally require its separately prepared reference build,
matching MPI launcher and, for Cellular, `--helm-binary`.

Formal timings are serial and begin after the relevant correctness checks;
there is no concurrent build, profiling or other simulation.

The CPU-only rerun after the exact-energy-key fix did not rerun FLASH. Older
manifests retain the runner's generic "alternating" scope text; their actual
`records` list identifies the routes that were executed, and the report states
which FLASH samples are reused. New manifests explicitly list selected routes.

These local thread counts and affinities describe the i7-10700 workstation.
A future H100 comparison must freeze its own build, physical-core CPU baseline
and device identity while preserving inputs, field budgets and complete-task
wall-clock accounting. RTX measurements do not predict the H100 speedup.
