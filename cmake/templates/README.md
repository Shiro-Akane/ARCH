# CUDA burn binding templates

These templates produce small translation units for configured network/EOS
combinations. They bind types and forward calls to the shared burn code, keeping
compilation responsibilities separate from numerical implementation.

- [CudaBurnDenseRoute.cu.in](CudaBurnDenseRoute.cu.in): specialize the compact
  network/EOS launch through the existing equation-count policy and ODE dispatcher.
- [CudaBurnSparseOwner.cu.in](CudaBurnSparseOwner.cu.in): specialize construction
  of the sparse burn owner for the selected network and EOS.

The [dense registration](../CudaBurnDenseRoutes.cmake) and [sparse registration](../CudaBurnSparseRoutes.cmake) scripts supply the necessary substitutions and explicitly write the generated files into the build tree. Note that the separate [custom dense delegate](../CudaCustomDenseRoute.cu.in) strictly adheres to the same binding-only contract. As always, any mathematical changes firmly belong to their production owners, rather than within these binding templates.
