/** Compatibility aliases; all sparse ODE methods share one backend executor. */
#pragma once
#include "SparseOdeBatch.cuh"

namespace arch::cuda {
template <class Network>
using SparseBeNrOde = SparseOdePolicy<Network, Solver_BE_NR>;
template <class Network>
using SparseBeNrBatchView = SparseOdeBatchView<Network, Solver_BE_NR>;
template <class Network>
using SparseBeNrBatchExecutor = SparseOdeBatchExecutor<Network, Solver_BE_NR>;
} // namespace arch::cuda
