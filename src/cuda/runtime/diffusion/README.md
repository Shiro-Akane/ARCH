# Diffusion runtime binding

[CudaBackendDiffusion.h](CudaBackendDiffusion.h) declares the launch interface;
[CudaBackendDiffusion.cu](CudaBackendDiffusion.cu) binds device execution.
Cross-module stage control stays in
[CudaBackendMicrophysicsControl.cpp](../control/CudaBackendMicrophysicsControl.cpp).

You should rely on [CUDA diffusion kernels](../../diffusion/README.md) to handle device-state traversal, but strictly utilize the common [diffusion operators](../../../numerics/diffusion/README.md) for the actual mathematics. It is imperative that RKL scheduling logic, stability rules, and numerical coefficients never acquire a secondary backend-specific definition.
