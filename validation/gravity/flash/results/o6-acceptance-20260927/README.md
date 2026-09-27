# O6/O6+ acceptance evidence, 2026-09-27

This directory preserves the third optimization round on `compute/optim`.
[The acceptance report](../../O6AcceptanceReport.zh-CN.md) separates physical
correctness, fixed-input task cost, backend speedup and unclosed release gates.
No HDF5 outputs or build products are added here.

- [Source and executable identity](verification.json) records the base commit,
  changed runtime/test content and measured executable hashes.
- `final/` contains the final CPU/FLASH, CPU1/16, CPU8 coupled and CUDA samples.
  Every timing point retains all three runs; CPU/FLASH sampling alternates routes.
- `earlier/` preserves earlier frozen measurements from this round. They do not
  replace the final CPU denominator or certify a later executable.
- `probes/` contains attribution/CFL experiments, the rejected temperature-hint
  probe, MPI launch failure and CPU wait-policy check. These are not medians.
- `checks/` contains final CPU/CUDA CTest and production compatibility records;
  its `device/` directory preserves all six zero-error sanitizer reports.

The maintained [runner](../../run_comparison.py) uses existing fixtures. Run
formal measurements serially after correctness checks and with no other build
or simulation active. Its `--report-only --manifest ...` mode analyzes saved
samples; repeat `--reference-manifest ...` for the CPU1/8/16 manifests. The CUDA
report uses the fastest measured CPU configuration and checks physical inputs,
actual endpoints, AMR topology and fields. SNIa uses the existing coupled verifier
and endpoint budget; fixed-time FLASH fixtures retain exact endpoint equality.
Raw output paths in the manifests refer to local retained data and are not a
promise that another checkout contains the HDF5 files.

The common cross-code reacting-flow error budget and the finest-grid CPU twofold
cost target remain open. The 2D four-module case has a current measured GPU gain;
other tested RTX points remain faster on CPU. This evidence does not authorize
an O6 release or change scientific thresholds.
