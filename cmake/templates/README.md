# CUDA burn binding templates

These templates produce small translation units for configured network/EOS
combinations. They bind types and forward calls to the shared burn code, keeping
compilation responsibilities separate from numerical implementation.

- [CudaBurnDenseRoute.cu.in](CudaBurnDenseRoute.cu.in): specialize the compact
  network/EOS launch through the existing equation-count policy and ODE dispatcher.
- [CudaBurnSparseOwner.cu.in](CudaBurnSparseOwner.cu.in): specialize construction
  of the sparse burn owner for the selected network and EOS.

[Dense registration](../CudaBurnDenseRoutes.cmake) and
[sparse registration](../CudaBurnSparseRoutes.cmake) supply substitutions and
write the generated files into the build tree. The separate
[custom dense delegate](../CudaCustomDenseRoute.cu.in) follows the same binding-only
contract. Mathematical changes belong to their production owners, not these templates.
