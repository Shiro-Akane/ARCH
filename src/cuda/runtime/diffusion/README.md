# Diffusion runtime binding

[CudaBackendDiffusion.h](CudaBackendDiffusion.h) declares the launch interface;
[CudaBackendDiffusion.cu](CudaBackendDiffusion.cu) binds device execution.
Cross-module stage control stays in
[CudaBackendMicrophysicsControl.cpp](../control/CudaBackendMicrophysicsControl.cpp).

Use [CUDA diffusion kernels](../../diffusion/README.md) to traverse device state
and the common [diffusion operators](../../../numerics/diffusion/README.md) for
mathematics. RKL scheduling, stability rules and coefficients must not gain a
second backend-specific definition.
