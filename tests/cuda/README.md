# CUDA execution and parity tests

These tests exercise production CUDA launchers and compare shared operations
with host execution or independent references. Use the responsibility index
below to find a focused check; CMake defines target names and optional
dependencies centrally.

| Responsibility | Useful entry points |
|---|---|
| Availability, policy and factory selection | [compile probe](test_cuda_compile_probe.cu), [policy resolution](test_cuda_policy_resolution.cu), [mainline authority](test_mainline_authority.cpp) |
| Hydro state, flux and dispatch | [leaf parity](test_hydro_leaf_parity.cu), [hydro dispatch](test_hydro_dispatch.cu), [multiblock hydro](test_cuda_multiblock_hydro.cu) |
| EOS views, ownership and failures | [EOS parity](test_eos_host_device_parity.cu), [table owner](test_tabular_free_energy_owner.cu), [native table owner](test_native_tabular_owner.cu), [completed strict tables](test_tabular_completion_device.cu), [hydro failure](test_hydro_eos_failure.cu), [burn failure](test_burn_eos_failure.cu) |
| AMR transactions and migration | [transaction](test_cuda_regrid_transaction.cpp), [migration](test_cuda_regrid_migration.cu), [composition](test_cuda_amr_composition.cu), [store lifecycle](test_cuda_store_lifecycle.cpp) |
| AMR indicators, ghosts and boundaries | [indicators](test_refinement_indicators.cpp), [exchange](test_cuda_amr_exchange.cpp), [boundary parity](test_boundary_plan_parity.cu) |
| Geometry and diffusion | [metric cache](test_grid_metrics_cache.cu), [geometry witnesses](test_curvilinear_geometry_smoke.cu), [RKL parity](test_diffusion_rkl_parity.cu), [multiblock diffusion](test_cuda_multiblock_diffusion.cu) |
| Burn algorithms and controller | [thermal math](test_burn_thermal_math.cu), [derivatives](test_network_derivative.cu), [policy parity](test_burn_policy_parity.cu), [controller parity](test_burn_controller_parity.cu), [multiblock burn](test_cuda_multiblock_burn.cu) |
| Sparse solve and continuation | [cuDSS solver](test_cudss_sparse_solver.cpp), [sparse batch](test_sparse_be_nr_batch.cu), [burn factory](test_cuda_sparse_burn_factory.cpp) |
| Generated networks, weak rates and NSE | [generated math](test_generated_network_math.cu), [sparse integration](test_generated_sparse_burn.cpp), [weak factory](test_generated_weak_factory.cu), [weak trajectory](test_generated_weak_trajectory.cu), [built-in NSE](test_network_nse_device.cu), [generated NSE witnesses](test_generated_nse_device.cu) |
| Reductions and checkpoint comparison | [compensated sum](test_compensated_sum.cu), [reduction contract](test_cuda_reduction_contract.cu), [checkpoint comparator](test_cuda_single_level_validation.cpp) |

[CMakeLists.txt](../../CMakeLists.txt) defines the executable targets and optional
dependencies. [Network validation](../../validation/network/README.md) supplies
the generated-package setup. The shared [math witnesses](../math/README.md) and
[fixtures](../fixtures/README.md) own reusable test data.

Passing an individual test successfully establishes its specific stated contract. However, full application coverage, sanitizer runs, and combined acceptance criteria are rigorously recorded and maintained exclusively within the [Validation](../../validation/README.md) module.

The generated sparse trajectory harness accepts optional `--storage-cells FIRST SECOND`
and `--pool-cells COUNT`; defaults remain 2/3 cells and a requested pool of 2.
Non-default transcripts identify those controls and retain the same three ODE
methods and numerical budgets. This capacity extension is pending real-device
validation; see the [S0 tooling review](../../validation/backend/results/hpc-cuda-optimization/S0/ToolingReview.zh-CN.md).
