# CUDA gravity execution

This directory owns the gravity backend bindings, device allocation, kernel
launches, reduction orchestration and stream completion. CMake lists both
translation units explicitly; cross-module includes use `cuda/runtime/gravity/`.

- `CudaBackendGravity.cpp` borrows the selected resident density slot, provides
  the executor and publishes generation-stamped patch views.
- `CudaGravityExecution.h/.cu` implements the storage/loop interface using the
  existing backend stream and allocation owners. A lifetime lease keeps that
  stream alive until all gravity allocations are retired.

The mathematical owners remain `numerics/elliptic`, `numerics/multigrid` and
`physics/gravity`. Work descriptors, compensated reduction arithmetic, MG/FGMRES
control, coarse inverse, boundary moments, density source and flux work are shared
with CPU across Cartesian and native cylindrical/spherical geometry. There
is no separate CUDA Poisson stencil or solver algorithm. Singular-coordinate
Hydro ghosts use the shared [AMR seam math](../../../amr/exchange/CoordinateSeamMath.h)
through the [CUDA AMR runtime](../amr/README.md), outside the gravity source.

Vectors and fields remain resident through a solve. Topology metadata is uploaded
once per rebuild; each solve uploads only borrowed patch pointers. Krylov decisions
read scalar reductions; mean projection stays on device. Output explicitly
materializes potential/acceleration, while checkpoint restart recomputes them.
The backend cannot silently substitute a Host gravity solve.

When a reduction's original 64-element partials fit within one 128-thread
block, its final pass runs in that same block. Small periodic projections also
combine maximum, normalized mean and subtraction using block barriers. The
shared reduction/compensation order and `ProjectWork` arithmetic are unchanged;
larger vectors keep the multi-kernel path. This scheduling choice adds no
physical parameter and does not move a solve to the host.
