/**
 * @file SparseBeNrBatch.cuh
 * @brief BE-NR aliases of the common sparse ODE backend executor.
 *
 * These names bind a method to SparseOdeBatch; they define no second solver.
 */
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
