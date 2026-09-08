# Diffusion input sets

The `rkl1_n*.par` and `rkl2_n*.par` families exercise the same periodic species
cosine mode with the two RKL integrators at several resolutions. Representative
entries are [rkl1_n64.par](rkl1_n64.par) and [rkl2_n64.par](rkl2_n64.par).

[DiffusionMode](../../../simulation/DiffusionMode/README.md) owns initialization;
[diffusion validation](../README.md) owns analytic decay, convergence comparisons
and budgets. The [backend matrix](../../backend/cases.json) specifies the
CPU/CUDA application executions.

Keep the canonical inputs unchanged when replaying archived results. Record any
new case's overrides and source identity alongside its evidence.
